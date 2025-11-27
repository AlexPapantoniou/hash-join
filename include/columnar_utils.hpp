#pragma once
// #include <plan.h>
// #include <inner_column.h>
#include <atomic>
#include <charconv>

#include <common.h>
#include <csv_parser.h>
#include <inner_column.h>
#include <plan.h>
#include <table.h>

#if !defined(TEAMOPT_USE_DUCKDB) || defined(TEAMOPT_BUILD_CACHE)
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif

typedef struct str_rep {
    uint64_t table_id : 10;     // 10 bits for table id (2^10 tables)
    uint64_t column_id : 10;    // 10 bits for column id (2^10 columns per table)
    uint64_t page_id : 20;      // 20 bits for page id (2^20 pages => each table fits 2^10 pages)
    uint64_t offset : 24;       // 24 bits for offset in a table

    str_rep(uint64_t t_id, uint64_t c_id, uint64_t p_id, uint64_t off) :
        table_id(t_id), column_id(c_id), page_id(p_id), offset(off) {
    }
} str_rep_t;

typedef struct value_store {
    bool is_int;
    union {
        int32_t int_value;                  // value is an integer
        str_rep_t string_represent;        // value is a string (store the representation, dont copy the content)
    };

    value_store(int32_t value) : int_value(value), is_int(true) {};
    value_store(str_rep_t value) : string_represent(value), is_int(false) {};

} value_t;

namespace ColumnarUtils {

    bool get_bitmap(const uint8_t* bitmap, uint16_t idx) {
        auto byte_idx = idx / 8;
        auto bit = idx % 8;
        return bitmap[byte_idx] & (1u << bit);
    }

    std::vector<std::vector<value_t>> my_copy(const ColumnarTable& table, const std::vector<std::tuple<size_t, DataType>>& output_attrs, size_t table_id) {
        std::vector<std::vector<value_t>> results(table.num_rows, std::vector<value_t>(output_attrs.size(), value_t{}));
        std::vector<DataType> types(table.columns.size());
        namespace views = ranges::views;

        auto task = [&](size_t begin, size_t end) {
            size_t col_pap = 0;
            for (size_t column_idx = begin; column_idx < end; column_idx++) {
                size_t in_col_idx = std::get<0>(output_attrs[column_idx]);
                auto& column = table.columns[in_col_idx];
                types[in_col_idx] = column.type;
                size_t row_idx = 0;
                int64_t page_id = 0;
                for (auto* page : column.pages | views::transform([](auto* page) { return page->data; })) {
                    switch (column.type) {
                    case DataType::INT32: {
                        auto num_rows = *reinterpret_cast<uint16_t*>(page);
                        auto* data_begin = reinterpret_cast<uint32_t*>(page + 4);
                        auto* bitmap = reinterpret_cast<uint8_t*>(page + PAGE_SIZE - (num_rows + 7) / 8);
                        uint16_t data_idx = 0;
                        for (uint16_t i = 0; i < num_rows; i++) {
                            if (get_bitmap(bitmap, i)) {
                                auto value = data_begin[data_idx++];
                                if (row_idx >= table.num_rows) {
                                    throw std::runtime_error("row_idx");
                                }
                                results[row_idx++][column_idx] = std::move(value_t(value));
                            }
                            else {
                                row_idx++;
                            }
                        }
                        break;
                    }
                    case Datatype::VARCHAR: {
                        auto num_rows = *reinterpret_cast<uint16_t*>(page);
                        if (num_rows == 0xffff) {
                            auto num_chars = *reinterpret_cast<uint16_t*>(page + 2);
                            auto* data_begin = reinterpret_cast<char*>(page + 4);
                            if (row_idx >= table.num_rows) {
                                throw std::runtime_error("row_idx");
                            }
                            value_t value(str_rep_t(table_id, column_idx, page_id, data_begin + num_chars));
                            results[row_idx++][column_idx] = std::move(value);
                        }
                        else if (num_rows == 0xfffe) {
                            auto num_chars = *reinterpret_cast<uint16_t*>(page + 2);
                            auto* data_begin = reinterpret_cast<char*>(page + 4);
                            std::visit(
                                [data_begin, num_chars](auto& value) {
                                    using T = std::decay_t<decltype(value)>;
                                    if constexpr (std::is_same_v<T, value_t>) {
                                        value.string_respresent.offset += num_chars;
                                    }
                                    else {
                                        throw std::runtime_error(
                                            "long string page 0xfffe must follow a string");
                                    }
                                },
                                results[row_idx - 1][column_idx]);
                        }
                        else {
                            auto num_non_null = *reinterpret_cast<uint16_t*>(page + 2);
                            auto* offset_begin = reinterpret_cast<uint16_t*>(page + 4);
                            auto* data_begin = reinterpret_cast<char*>(page + 4 + num_non_null * 2);
                            auto* string_begin = data_begin;
                            auto* bitmap = reinterpret_cast<uint8_t*>(page + PAGE_SIZE - (num_rows + 7) / 8);
                            uint16_t data_idx = 0;
                            for (uint16_t i = 0; i < num_rows; i++) {
                                if (get_bitmap(bitmap, i)) {
                                    auto offset = offset_begin[data_idx++];
                                    string_begin = data_begin + offset;
                                    if (row_idx >= table.num_rows) {
                                        throw std::runtime_error("row_idx");
                                    }
                                    value_t value(str_rep_t(table_id, column_idx, page_id, offset));
                                    results[row_idx++][column_idx] = std::move(value);
                                }
                                else {
                                    row_idx++;
                                }
                            }
                        }
                        break;
                    }
                    }
                    page_id++;
                }
            }
            };
        filter_tp.run(task, output_attrs.size());
        return results;
    }
}