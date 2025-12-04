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
#elif defined(HASH_STANDARD)
#include <unordered_map>
#define LOAD_FACTOR 0.75
template<typename Key, typename Value>
using HashMap = std::unordered_map<Key, Value>;
#endif

#include "../include/columnar_utils.hpp"
// #include <iostream>
#include <stdio.h>

namespace Contest {

    using ExecuteResult = std::vector<std::vector<ColumnarUtils::value_t>>;

    ExecuteResult execute_impl(const Plan& plan, size_t node_idx);

    struct MyJoinAlgorithm {
        bool                                             build_left;
        ExecuteResult& left;
        ExecuteResult& right;
        ExecuteResult& results;
        size_t                                           left_col, right_col;
        const std::vector<std::tuple<size_t, DataType>>& output_attrs;

        void run() {
            namespace views = ranges::views;
            size_t build_rows = build_left ? left.size() : right.size();
            HashMap<int32_t, std::vector<size_t>> hash_map;
            hash_map.reserve(static_cast<size_t>(std::max<size_t>(1, build_rows / LOAD_FACTOR)));

            if (build_left) {
                // build from left
                for (size_t idx = 0; idx < left.size(); idx++) {
                    const auto& value = left[idx][left_col];
                    if (value.is_null()) {
                        continue;
                    }
                    int32_t key = value.as_i32();
                    auto itr = hash_map.find(key);
                    if (itr == hash_map.end()) {
                        hash_map.emplace(key, std::vector<size_t>{idx});
                    }
                    else {
                        itr->second.push_back(idx);
                    }
                }
                // probe with right
                for (size_t rid = 0; rid < right.size(); rid++) {
                    const auto& value = right[rid][right_col];
                    if (value.is_null()) {
                        continue;
                    }
                    int32_t key = value.as_i32();
                    auto itr = hash_map.find(key);
                    if (itr == hash_map.end()) {
                        continue;
                    }
                    const auto& right_record = right[rid];
                    for (size_t left_idx : itr->second) {
                        const auto& left_record = left[left_idx];
                        std::vector<ColumnarUtils::value_t> new_record;
                        new_record.reserve(output_attrs.size());
                        // For each output slot, decide whether it refers to left or right
                        for (auto [col_idx, _] : output_attrs) {
                            if (col_idx < left_record.size()) {
                                new_record.emplace_back(left_record[col_idx]);
                            }
                            else {
                                new_record.emplace_back(right_record[col_idx - left_record.size()]);
                            }
                        }
                        results.emplace_back(std::move(new_record));
                    }
                }
            }
            else {
                // build from right
                for (size_t idx = 0; idx < right.size(); idx++) {
                    const auto& value = right[idx][right_col];
                    if (value.is_null()) {
                        continue;
                    }
                    int32_t key = value.as_i32();
                    auto itr = hash_map.find(key);
                    if (itr == hash_map.end()) {
                        hash_map.emplace(key, std::vector<size_t>{idx});
                    }
                    else {
                        itr->second.push_back(idx);
                    }
                }
                // probe with left
                for (size_t lid = 0; lid < left.size(); lid++) {
                    const auto& value = left[lid][left_col];
                    if (value.is_null()) {
                        continue;
                    }
                    int32_t key = value.as_i32();
                    auto itr = hash_map.find(key);
                    if (itr == hash_map.end()) {
                        continue;
                    }
                    const auto& left_record = left[lid];
                    for (size_t right_idx : itr->second) {
                        const auto& right_record = right[right_idx];
                        std::vector<ColumnarUtils::value_t> new_record;
                        new_record.reserve(output_attrs.size());
                        for (auto [col_idx, _] : output_attrs) {
                            if (col_idx < left_record.size()) {
                                new_record.emplace_back(left_record[col_idx]);
                            }
                            else {
                                new_record.emplace_back(right_record[col_idx - left_record.size()]);
                            }
                        }
                        results.emplace_back(std::move(new_record));
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

        std::vector<std::vector<ColumnarUtils::value_t>> results;

        MyJoinAlgorithm join_algorithm{ .build_left = join.build_left,
            .left = left,
            .right = right,
            .results = results,
            .left_col = join.left_attr,
            .right_col = join.right_attr,
            .output_attrs = output_attrs };
        if (join.build_left) {
            switch (std::get<1>(left_types[join.left_attr])) {
            case DataType::INT32:   join_algorithm.run(); break;
                // case DataType::INT64:   join_algorithm.run<int64_t>(); break;
                // case DataType::FP64:    join_algorithm.run<double>(); break;
            // case DataType::VARCHAR: join_algorithm.run(); break;
            }
        }
        else {
            switch (std::get<1>(right_types[join.right_attr])) {
            case DataType::INT32:   join_algorithm.run(); break;
                // case DataType::INT64:   join_algorithm.run<value_t>(); break;
                // case DataType::FP64:    join_algorithm.run<value_t>(); break;
            // case DataType::VARCHAR: join_algorithm.run(); break;
            }
        }

        return results;
    }

    ExecuteResult execute_scan(const Plan& plan,
        const ScanNode& scan,
        const std::vector<std::tuple<size_t, DataType>>& output_attrs) {
        auto                           table_id = scan.base_table_id;
        auto& input = plan.inputs[table_id];
        return ColumnarUtils::my_copy(input, output_attrs, table_id);
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

        ColumnarUtils::MyTable my_table{ std::move(ret), std::move(ret_types) };
        return my_table.from_value_t_to_columnar(std::move(plan));
        // std::vector<std::vector<Data>> materialized = ColumnarUtils::materialize(plan, ret, ret_types);
        // Table table{ std::move(materialized), std::move(ret_types) };
        // return table.to_columnar();
    }

    void* build_context() {
        return nullptr;
    }

    void destroy_context([[maybe_unused]] void* context) {}

} // namespace Contest