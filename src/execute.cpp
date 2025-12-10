#include <hardware.h>
#include <plan.h>
#include <table.h>

#if defined(HASH_ROBINHOOD)
#include "hashmap_robinhood.hpp"
#define LOAD_FACTOR 0.75
template<typename Key, typename Value>
using HashMap = HashMapRobinhood<Key, Value>;
#elif defined(HASH_HOPSCOTCH)
#include "hashmap_hopscotch.hpp"
#define LOAD_FACTOR 0.75
template<typename Key, typename Value>
using HashMap = HashMapHopscotch<Key, Value>;
#elif defined(HASH_CUCKOO)
#include "hashmap_cuckoo.hpp"
#define LOAD_FACTOR 0.90
template<typename Key, typename Value>
using HashMap = HashMapCuckoo<Key, Value>;
#elif defined(HASH_UNCHAINED)
#include "hashmap_unchained.hpp"
#define LOAD_FACTOR 0.65
template<typename Key, typename Value>
using HashMap = HashMapUnchained<Key, Value>;
#elif defined(HASH_STANDARD)
#include <unordered_map>
#define LOAD_FACTOR 0.75
template<typename Key, typename Value>
using HashMap = std::unordered_map<Key, Value>;
#endif

#include "../include/columnar_utils.hpp"
#include "../include/column_store_utils.hpp"
// #include <iostream>
#include <stdio.h>

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

#if defined(HASH_UNCHAINED)
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
#else

        void run() {
            size_t left_num_rows = left[left_col].num_rows;
            size_t right_num_rows = right[right_col].num_rows;

            size_t build_rows = build_left ? left_num_rows : right_num_rows;
            size_t init_cap = static_cast<size_t>(std::max<size_t>(1, build_rows / LOAD_FACTOR));

            HashMap<int32_t, std::vector<size_t>> hash_map(init_cap);

            if (build_left) {
                // build from left
                for (size_t row = 0; row < left_num_rows; row++) {
                    const auto& value = get_value(left, left_col, row);
                    if (value.is_null()) {
                        continue;
                    }

                    int32_t key = value.as_i32();
                    auto itr = hash_map.find(key);
                    if (itr == hash_map.end()) {
                        hash_map.emplace(key, std::vector<size_t>{row});
                    }
                    else {
                        itr->second.push_back(row);
                    }
                }
                // probe with right
                for (size_t row = 0; row < right_num_rows; row++) {
                    const auto& value = get_value(right, right_col, row);
                    if (value.is_null()) {
                        continue;
                    }

                    int32_t key = value.as_i32();
                    auto itr = hash_map.find(key);
                    if (itr == hash_map.end()) {
                        continue;
                    }

                    for (size_t left_row : itr->second) {
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
                // build from right
                for (size_t row = 0; row < right_num_rows; row++) {
                    const auto& value = get_value(right, right_col, row);
                    if (value.is_null()) {
                        continue;
                    }

                    int32_t key = value.as_i32();
                    auto itr = hash_map.find(key);
                    if (itr == hash_map.end()) {
                        hash_map.emplace(key, std::vector<size_t>{row});
                    }
                    else {
                        itr->second.push_back(row);
                    }
                }
                // probe with left
                for (size_t row = 0; row < left_num_rows; row++) {
                    const auto& value = get_value(left, left_col, row);
                    if (value.is_null()) {
                        continue;
                    }

                    int32_t key = value.as_i32();
                    auto itr = hash_map.find(key);
                    if (itr == hash_map.end()) {
                        continue;
                    }

                    for (size_t right_row : itr->second) {
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
#endif
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

#if defined(HASH_UNCHAINED)
        join_algorithm.run_unchained();
#else
        join_algorithm.run();
#endif

        return results;
    }

    ExecuteResult execute_scan(const Plan& plan,
        const ScanNode& scan,
        const std::vector<std::tuple<size_t, DataType>>& output_attrs) {
        auto                           table_id = scan.base_table_id;
        auto& input = plan.inputs[table_id];
        ExecuteResult temp = ColumnStoreUtils::my_copy(input, output_attrs, table_id);
        return temp;
        // return ColumnStoreUtils::my_copy(input, output_attrs, table_id);
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

    void* build_context() {
        return nullptr;
    }

    void destroy_context([[maybe_unused]] void* context) {}

} // namespace Contest
