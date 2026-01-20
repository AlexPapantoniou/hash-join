#include <hardware.h>
#include <plan.h>
#include <table.h>

#include "hashmap_unchained.hpp"
#define LOAD_FACTOR 0.65

#include "../include/columnar_utils.hpp"
#include "../include/column_store_utils.hpp"
#include "../include/slab_allocator.hpp"

#include <thread>
#include <atomic>

namespace Contest {

    using ExecuteResult = std::vector<ColumnStoreUtils::column_t>;
    using Tuple = HashMapUnchained<int32_t, size_t>::Tuple;

    ExecuteResult execute_impl(const Plan& plan, size_t node_idx, void* context);

    struct MyJoinAlgorithm {
        bool                                             build_left;
        ExecuteResult& left;
        ExecuteResult& right;
        ExecuteResult& results;
        size_t                                           left_col, right_col;
        const std::vector<std::tuple<size_t, DataType>>& output_attrs;
        std::atomic<size_t> next_row;

        // Function to get the value from a certain position in a column_t
        static inline ColumnarUtils::value_t get_value(const ExecuteResult& table, size_t col, size_t row) {
            const ColumnStoreUtils::column_t& column = table[col];
            if (column.has_nulls) {
                size_t page_id = row / ColumnStoreUtils::column_t::ELEMENTS_PER_PAGE;
                size_t offset = row % ColumnStoreUtils::column_t::ELEMENTS_PER_PAGE;
                return column.pages[page_id]->data[offset];
            }

            const Column* orig_col = column.orig_col;
            const Page* first_page = orig_col->pages[0];
            const uint16_t rows_in_page = *reinterpret_cast<const uint16_t*>(first_page->data);

            size_t page_id = row / rows_in_page;
            size_t offset = row % rows_in_page;

            const Page* p = orig_col->pages[page_id];
            const uint32_t* data_begin = reinterpret_cast<const uint32_t*>(p->data + 4);
            return ColumnarUtils::value_t::make_int32(static_cast<int32_t>(data_begin[offset]));
        }

        // ----------------------------------------------
        // Functions which the threads will execute
        // ----------------------------------------------

        void build_phase(size_t build_rows, SlabAllocator::TupleCollector& collector) {
            const auto& build = build_left ? left : right;
            const size_t build_col = build_left ? left_col : right_col;
            const auto& col = build[build_col];
            const size_t page_rows = ColumnStoreUtils::column_t::ELEMENTS_PER_PAGE;

            // Each thread takes the first available rows (there are no 'owned' rows by any thread)
            while (true) {
                size_t start = next_row.fetch_add(page_rows, std::memory_order_relaxed);
                if (start >= build_rows) {
                    break;
                }

                size_t end = start + page_rows;
                if (end > build_rows) {
                    end = build_rows;
                }

                for (size_t row = start; row < end; row++) {
                    const auto& value = get_value(build, build_col, row);
                    if (value.is_null()) {
                        continue;
                    }

                    int32_t key = value.as_i32();
                    collector.consume({ key, row, SlabAllocator::compute_hash(key) });
                }
            }
        }

        void merge_phase(const SlabAllocator::Context* con, size_t p, HashMapUnchained<int32_t, size_t>& hash_map,
            const size_t* prefix, std::vector<size_t>& write_cursor) {
            for (size_t t = 0; t < NUM_PARTITIONS; t++) {
                auto& level3 = con->collectors[t].level3[p];
                if (level3.chunks.empty()) {
                    continue;
                }
                for (const auto& chunk : level3.chunks) {
                    uint8_t* ptr = chunk.start;
                    while (ptr < chunk.current) {
                        Tuple t;
                        memcpy(&t, ptr, sizeof(Tuple));
                        hash_map.count_and_tag(t);
                        ptr += sizeof(Tuple);
                    }
                }
            }

            size_t cur = prefix[p];
            size_t k = __builtin_ctzll(hash_map.bucket_count());

            size_t start = (p << k) / NUM_PARTITIONS;
            size_t end = ((p + 1) << k) / NUM_PARTITIONS;

            for (size_t i = start; i < end; i++) {
                size_t bucket_size = hash_map.prefix_sum(i, cur);
                write_cursor[i] = cur;
                cur += bucket_size;
            }

            for (size_t t = 0; t < NUM_PARTITIONS; t++) {
                auto& level3 = con->collectors[t].level3[p];
                for (const auto& chunk : level3.chunks) {
                    uint8_t* ptr = chunk.start;
                    while (ptr < chunk.current) {
                        Tuple t;
                        memcpy(&t, ptr, sizeof(Tuple));
                        size_t slot = hash_map.bucket_index_from_hash(t.hash);
                        size_t pos = write_cursor[slot]++;
                        hash_map.insert(t, pos);
                        ptr += sizeof(Tuple);
                    }
                }
            }
        }

        void probe_phase(size_t probe_rows, const HashMapUnchained<int32_t, size_t>& hash_map, ExecuteResult& local_results) {
            const auto& probe = build_left ? right : left;
            const size_t probe_col = build_left ? right_col : left_col;
            const size_t page_rows = ColumnStoreUtils::column_t::ELEMENTS_PER_PAGE;

            while (true) {
                size_t start = next_row.fetch_add(page_rows, std::memory_order_relaxed);
                if (start >= probe_rows) {
                    break;
                }

                size_t end = start + page_rows;
                if (end > probe_rows) {
                    end = probe_rows;
                }

                for (size_t row = start; row < end; row++) {
                    const auto& v = get_value(probe, probe_col, row);
                    if (v.is_null()) {
                        continue;
                    }

                    int32_t key = v.as_i32();
                    auto pr = hash_map.lookup_range(key);
                    if (pr.first == pr.second) {
                        // Empty
                        continue;
                    }
                    for (size_t i = pr.first; i < pr.second; i++) {
                        const auto& tuple = hash_map[i];
                        if (tuple.key != key) {
                            continue;
                        }

                        size_t left_row = build_left ? tuple.value : row;
                        size_t right_row = build_left ? row : tuple.value;

                        for (size_t c = 0; c < output_attrs.size(); c++) {
                            size_t idx = std::get<0>(output_attrs[c]);
                            auto val = (idx < left.size())
                                ? get_value(left, idx, left_row)
                                : get_value(right, idx - left.size(), right_row);
                            local_results[c].insert(val);
                        }
                    }
                }
            }
        }

        // ----------------------------------------------

        // Function to merge all the local results from each thread into the final result table
        void merge_results(const std::vector<ExecuteResult>& local_results, ExecuteResult& results) {
            if (local_results.empty()) {
                return;
            }

            // Create output columns once
            size_t num_cols = local_results[0].size();

            // Append rows partition by partition
            for (size_t p = 0; p < NUM_PARTITIONS; p++) {
                const auto& part = local_results[p];
                if (part.empty()) {
                    continue;
                }

                size_t rows = part[0].num_rows;
                for (size_t row = 0; row < rows; row++) {
                    size_t page_id = row / ColumnStoreUtils::column_t::ELEMENTS_PER_PAGE;
                    size_t offset = row % ColumnStoreUtils::column_t::ELEMENTS_PER_PAGE;
                    for (size_t col = 0; col < num_cols; col++) {
                        results[col].insert(part[col].pages[page_id]->data[offset]);
                    }
                }
            }
        }

        void run_unchained(void* context) {
            const ExecuteResult& build = build_left ? left : right;
            const ExecuteResult& probe = build_left ? right : left;

            size_t build_col = build_left ? left_col : right_col;
            size_t probe_col = build_left ? right_col : left_col;
            size_t build_rows = build[build_col].num_rows;
            size_t probe_rows = probe[probe_col].num_rows;

            SlabAllocator::Context* con = static_cast<SlabAllocator::Context*>(context);
            con->reset();

            std::thread threads[NUM_PARTITIONS];

            next_row.store(0, std::memory_order_relaxed);

            for (size_t i = 0; i < NUM_PARTITIONS; i++) {
                threads[i] = std::thread([&, i]() {
                    build_phase(build_rows, con->collectors[i]);
                    });
            }

            for (size_t i = 0; i < NUM_PARTITIONS; i++) {
                if (threads[i].joinable()) {
                    threads[i].join();
                }
            }

            // Count how many tuples belong to each partition
            size_t counts[NUM_PARTITIONS] = { 0 };
            for (size_t i = 0; i < NUM_PARTITIONS; i++) {
                for (size_t j = 0; j < NUM_PARTITIONS; j++) {
                    counts[i] += con->collectors[j].counts[i];
                }
            }

            // Compute prefix sums
            size_t prefix[NUM_PARTITIONS];
            size_t total = 0;
            for (size_t i = 0; i < NUM_PARTITIONS; i++) {
                prefix[i] = total;
                total += counts[i];
            }

            size_t buckets = 1;
            while (buckets < size_t(double(total) / LOAD_FACTOR)) {
                buckets <<= 1;
            }

            if (buckets < NUM_PARTITIONS) {
                buckets = NUM_PARTITIONS;
            }

            HashMapUnchained<int32_t, size_t> hash_map(buckets);
            hash_map.resize(total);

            // Temporary vector to store the index of the next free bucket for each partition
            std::vector<size_t> write_cursor(hash_map.bucket_count());

            for (size_t p = 0; p < NUM_PARTITIONS; p++) {
                threads[p] = std::thread([&, p]() {
                    merge_phase(con, p, hash_map, prefix, write_cursor);
                    });
            }

            for (size_t i = 0; i < NUM_PARTITIONS; i++) {
                if (threads[i].joinable()) {
                    threads[i].join();
                }
            }

            hash_map.set_tuple_count(total);
            hash_map.finalize_directory();

            // ------------
            // Probe phase
            // ------------

            std::vector<ExecuteResult> local_results(NUM_PARTITIONS);
            for (size_t p = 0; p < NUM_PARTITIONS; p++) {
                local_results[p].reserve(output_attrs.size());
                for (auto&& [_, dt] : output_attrs) {
                    local_results[p].emplace_back(dt);
                }
            }

            next_row.store(0, std::memory_order_relaxed);

            // Each thread produces a local_results table of results
            for (size_t p = 0; p < NUM_PARTITIONS; p++) {
                threads[p] = std::thread([&, p, probe_rows]() {
                    probe_phase(probe_rows, hash_map, local_results[p]);
                    });
            }

            for (size_t i = 0; i < NUM_PARTITIONS; i++) {
                if (threads[i].joinable()) {
                    threads[i].join();
                }
            }

            // The main thread merges the local results
            merge_results(local_results, results);
        }
    };


    ExecuteResult execute_hash_join(const Plan& plan,
        const JoinNode& join,
        const std::vector<std::tuple<size_t, DataType>>& output_attrs,
        void* context) {
        auto                           left_idx = join.left;
        auto                           right_idx = join.right;
        auto& left_node = plan.nodes[left_idx];
        auto& right_node = plan.nodes[right_idx];
        auto& left_types = left_node.output_attrs;
        auto& right_types = right_node.output_attrs;
        auto                           left = execute_impl(plan, left_idx, context);
        auto                           right = execute_impl(plan, right_idx, context);

        std::vector<ColumnStoreUtils::column_t> results;

        results.reserve(output_attrs.size());
        for (auto&& [_, dt] : output_attrs) {
            results.emplace_back(dt);
        }

        MyJoinAlgorithm join_algorithm{ .build_left = join.build_left,
            .left = left,
            .right = right,
            .results = results,
            .left_col = join.left_attr,
            .right_col = join.right_attr,
            .output_attrs = output_attrs };

        join_algorithm.run_unchained(context);

        return results;
    }

    ExecuteResult execute_scan(const Plan& plan,
        const ScanNode& scan,
        const std::vector<std::tuple<size_t, DataType>>& output_attrs) {
        auto                           table_id = scan.base_table_id;
        auto& input = plan.inputs[table_id];
        return ColumnStoreUtils::my_copy(input, output_attrs, table_id);
    }

    ExecuteResult execute_impl(const Plan& plan, size_t node_idx, void* context) {
        auto& node = plan.nodes[node_idx];
        return std::visit(
            [&](const auto& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, JoinNode>) {     //if T is a join Node
                    return execute_hash_join(plan, value, node.output_attrs, context);
                }
                else {  //if T is a SCAN Node
                    return execute_scan(plan, value, node.output_attrs);
                }
            },
            node.data);
    }

    ColumnarTable execute(const Plan& plan, [[maybe_unused]] void* context) {
        namespace views = ranges::views;
        auto ret = execute_impl(plan, plan.root, context);
        auto ret_types = plan.nodes[plan.root].output_attrs
            | views::transform([](const auto& v) { return std::get<1>(v); })
            | ranges::to<std::vector<DataType>>();

        return ColumnStoreUtils::materialize(plan, ret, ret_types);
    }

    void* build_context() {
        return new SlabAllocator::Context();
    }

    void destroy_context([[maybe_unused]] void* context) {
        SlabAllocator::Context* con = static_cast<SlabAllocator::Context*>(context);
        delete con;
    }

} // namespace Contest