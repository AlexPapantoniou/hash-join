#include <hardware.h>
#include <plan.h>
#include <table.h>

#include "hashmap_unchained.hpp"
#define LOAD_FACTOR 0.65

#include "../include/columnar_utils.hpp"
#include "../include/column_store_utils.hpp"

#define NumPartitions 16

namespace Contest {

    using ExecuteResult = std::vector<ColumnStoreUtils::column_t>;

    ExecuteResult execute_impl(const Plan& plan, size_t node_idx);

    struct MyJoinAlgorithm {
        bool                                             build_left;
        ExecuteResult& left;
        ExecuteResult& right;
        ExecuteResult& results;
        size_t                                           left_col, right_col;
        const std::vector<std::tuple<size_t, DataType>>& output_attrs;

        inline ColumnarUtils::value_t get_value(const ExecuteResult& table, size_t col, size_t row) const {
            const ColumnStoreUtils::column_t& column = table[col];
            size_t page_id = row / ColumnStoreUtils::column_t::ELEMENTS_PER_PAGE;
            size_t offset = row % ColumnStoreUtils::column_t::ELEMENTS_PER_PAGE;
            return column.pages[page_id]->data[offset];
        }

        void run_unchained() {
            size_t left_num_rows = left[left_col].num_rows;
            size_t right_num_rows = right[right_col].num_rows;

            size_t build_rows = build_left ? left_num_rows : right_num_rows;
            size_t init_cap = static_cast<size_t>(std::max<size_t>(1, build_rows / LOAD_FACTOR));

            HashMapUnchained<int32_t, size_t> hash_map(init_cap);
            hash_map.reserve(build_rows);

            //------------
            // Build phase
            //------------
            if (build_left) {
                for (size_t row = 0; row < left_num_rows; row++) {
                    const auto& value = get_value(left, left_col, row);
                    if (value.is_null()) {
                        continue;
                    }

                    int32_t key = value.as_i32();
                    hash_map.emplace(key, row);
                }
            }
            else {
                for (size_t row = 0; row < right_num_rows; row++) {
                    const auto& value = get_value(right, right_col, row);
                    if (value.is_null()) {
                        continue;
                    }

                    int32_t key = value.as_i32();
                    hash_map.emplace(key, row);
                }
            }

            hash_map.create_directory();

            //------------
            // Probe phase
            //------------
            if (build_left) {
                // Build from left, probe with right
                for (size_t row = 0; row < right_num_rows; row++) {
                    const auto& value = get_value(right, right_col, row);
                    if (value.is_null()) {
                        continue;
                    }

                    int32_t key = value.as_i32();
                    auto pr = hash_map.lookup_range(key);
                    if (pr.first == pr.second) {
                        // Empty
                        continue;
                    }

                    for (size_t idx = pr.first; idx < pr.second; idx++) {
                        const auto& tuple = hash_map[idx];
                        if (tuple.key != key) {
                            continue;
                        }

                        size_t left_row = tuple.value;
                        for (size_t j = 0; j < output_attrs.size(); j++) {
                            size_t col_idx = std::get<0>(output_attrs[j]);
                            ColumnarUtils::value_t v;

                            if (col_idx < left.size()) {
                                v = get_value(left, col_idx, left_row);
                            }
                            else {
                                v = get_value(right, col_idx - left.size(), row);
                            }
                            results[j].insert(v);
                        }
                    }
                }
            }
            else {
                // Build from right, probe with left
                for (size_t row = 0; row < left_num_rows; row++) {
                    const auto& value = get_value(left, left_col, row);
                    if (value.is_null()) {
                        continue;
                    }

                    int32_t key = value.as_i32();
                    auto pr = hash_map.lookup_range(key);
                    if (pr.first == pr.second) {
                        // Empty
                        continue;
                    }

                    for (size_t idx = pr.first; idx < pr.second; idx++) {
                        const auto& tuple = hash_map[idx];
                        if (tuple.key != key) {
                            continue;
                        }

                        size_t right_row = tuple.value;
                        for (size_t j = 0; j < output_attrs.size(); j++) {
                            size_t col_idx = std::get<0>(output_attrs[j]);
                            ColumnarUtils::value_t v;

                            if (col_idx < left.size()) {
                                v = get_value(left, col_idx, row);
                            }
                            else {
                                v = get_value(right, col_idx - left.size(), right_row);
                            }
                            results[j].insert(v);
                        }
                    }
                }
            }
        }
    };


    ExecuteResult execute_hash_join(const Plan& plan,
        const JoinNode& join,
        const std::vector<std::tuple<size_t, DataType>>& output_attrs) {
        auto                           left_idx = join.left;
        auto                           right_idx = join.right;
        auto& left_node = plan.nodes[left_idx];
        auto& right_node = plan.nodes[right_idx];
        auto& left_types = left_node.output_attrs;
        auto& right_types = right_node.output_attrs;
        auto                           left = execute_impl(plan, left_idx);
        auto                           right = execute_impl(plan, right_idx);

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

        join_algorithm.run_unchained();

        return results;
    }

    ExecuteResult execute_scan(const Plan& plan,
        const ScanNode& scan,
        const std::vector<std::tuple<size_t, DataType>>& output_attrs) {
        auto                           table_id = scan.base_table_id;
        auto& input = plan.inputs[table_id];
        return ColumnStoreUtils::my_copy(input, output_attrs, table_id);
    }

    ExecuteResult execute_impl(const Plan& plan, size_t node_idx) {
        auto& node = plan.nodes[node_idx];
        return std::visit(
            [&](const auto& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, JoinNode>) {     //if T is a join Node
                    return execute_hash_join(plan, value, node.output_attrs);
                }
                else {  //if T is a SCAN Node
                    return execute_scan(plan, value, node.output_attrs);
                }
            },
            node.data);
    }

    ColumnarTable execute(const Plan& plan, [[maybe_unused]] void* context) {
        namespace views = ranges::views;
        auto ret = execute_impl(plan, plan.root);
        auto ret_types = plan.nodes[plan.root].output_attrs
            | views::transform([](const auto& v) { return std::get<1>(v); })
            | ranges::to<std::vector<DataType>>();

        return ColumnStoreUtils::materialize(plan, ret, ret_types);
    }

    struct Level1Slab {
        static constexpr size_t LARGE_CHUNK_SIZE = 4 * 1024 * 1024;  // 4 MB

        struct LargeChunk {
            uint8_t* start;
            size_t size;
        };

        std::vector<LargeChunk> chunks;

        LargeChunk allocate_large_chunk() {
            LargeChunk large_chunk;
            large_chunk.size = LARGE_CHUNK_SIZE;
            large_chunk.start = static_cast<uint8_t*>(malloc(LARGE_CHUNK_SIZE));

            chunks.push_back(large_chunk);
            return large_chunk;
        }
    };

    struct Level2Slab {
        static constexpr size_t SMALL_CHUNK_SIZE = 128 * 1024;  // 128 KB

        struct SmallChunk {
            uint8_t* start;
            size_t size;
        };

        uint8_t* current;
        size_t remaining;

        size_t free_space() {
            return remaining;
        }

        SmallChunk allocate_small_chunk(Level1Slab& level1) {
            if (free_space() < SMALL_CHUNK_SIZE) {
                auto large_chunk = level1.allocate_large_chunk();
                current = large_chunk.start;
                remaining = large_chunk.size;
            }

            SmallChunk small_chunk;
            small_chunk.start = current;
            small_chunk.size = SMALL_CHUNK_SIZE;

            current += SMALL_CHUNK_SIZE;
            remaining -= SMALL_CHUNK_SIZE;

            return small_chunk;
        }
    };

    struct Level3Slab {
        using Tuple = HashMapUnchained<int32_t, size_t>::Tuple;

        struct SmallChunk {
            uint8_t* current;
            uint8_t* end;
        };

        std::vector<SmallChunk> chunks;

        size_t free_space() {
            if (chunks.empty()) {
                return 0;
            }

            SmallChunk final_chunk = chunks.back();
            return final_chunk.end - final_chunk.current;
        }

        void add_space(const Level2Slab::SmallChunk& small_chunk) {
            SmallChunk new_chunk;
            new_chunk.current = small_chunk.start;
            new_chunk.end = small_chunk.start + small_chunk.size;
            chunks.push_back(new_chunk);
        }

        Tuple* allocate_tuple() {
            uint8_t* cur = chunks.back().current;
            chunks.back().current += sizeof(Tuple);
            return reinterpret_cast<Tuple*>(cur);
        }
    };

    uint64_t log2_num_partitions() {
        size_t p = 1;
        uint64_t log2 = 0;
        while (p < NumPartitions) {
            p <<= 1;
            log2++;
        }

        return log2;
    }

    // Hash function: hardware CRC if available
    template <typename Key>
    static uint64_t compute_hash(const Key& key) {
        // const uint8_t* data = (const uint8_t*)&key;
        // uint64_t hash;

// #ifdef __SSE4_2__
        //         if constexpr (sizeof(Key) == 8) {
        //             uint64_t value;
        //             memcpy(&value, data, 8);
        //             hash = _mm_crc32_u64(0, value);
        //         }
        //         else {
        //             uint32_t c = 0;
        //             for (size_t i = 0; i < sizeof(Key); i++) {
        //                 c = _mm_crc32_u8(c, data[i]);
        //             }
        //             hash = ((uint64_t)c << 32) | c;
        //         }
        // #else
        //         uint32_t c = 0xFFFFFFFFu;
        //         for (size_t i = 0; i < sizeof(Key); i++) {
        //             c ^= data[i];
        //             for (int k = 0; k < 8; k++) {
        //                 c = (c >> 1) ^ (0xEDB88320u & -(c & 1));
        //             }
        //         }
        //         c ^= 0xFFFFFFFFu;
        //         hash = ((uint64_t)c << 32) | c;
        // #endif
        //         return hash * 0x2545F4914F6CDD1DULL;
        constexpr uint64_t FIB64 = 11400714819323198485ULL;
        using U = std::make_unsigned_t<Key>;
        uint64_t k = static_cast<uint64_t>(static_cast<U>(key));
        return k * FIB64;
    }

    struct TupleCollector {
        using Tuple = HashMapUnchained<int32_t, size_t>::Tuple;

        Level1Slab level1;
        Level2Slab level2;
        Level3Slab level3[NumPartitions];
        size_t counts[NumPartitions];

        uint64_t shift = 64 - log2_num_partitions();

        void consume(Tuple tuple) noexcept {
            uint64_t part = compute_hash<int32_t>(tuple.key) >> shift;

            if (level3[part].free_space() < sizeof(Tuple)) {
                level3[part].add_space(level2.allocate_small_chunk(level1));
            }

            *level3[part].allocate_tuple() = tuple;
            counts[part] += 1;
        }
    };

    void* build_context() {
        return NULL;
    }

    void destroy_context([[maybe_unused]] void* context) {}

} // namespace Contest