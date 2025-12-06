#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <cstring>
#include <stdexcept>
#include <algorithm>
#include <cassert>
#include <iostream>

#include "plan.h"
#include "table.h"
#include "columnar_utils.hpp"

namespace ColumnStoreUtils {

    struct column_t {
        static constexpr size_t PAGE_BYTES = PAGE_SIZE;
        static constexpr size_t ELEMENT_SIZE = sizeof(ColumnarUtils::value_t);
        static constexpr size_t ELEMENTS_PER_PAGE = PAGE_BYTES / ELEMENT_SIZE;

        struct page_t {
            ColumnarUtils::value_t data[ELEMENTS_PER_PAGE];
            size_t num_elements;

            page_t() : num_elements(0) {
            }

            bool full() const noexcept {
                return num_elements >= ELEMENTS_PER_PAGE;
            }

            bool empty() const noexcept {
                return num_elements == 0;
            }

            void insert(const ColumnarUtils::value_t& value) {
                if (full()) {
                    throw std::runtime_error("page is full");
                }
                data[num_elements++] = value;
            }
        };

        std::vector<page_t*> pages;
        size_t num_rows{ 0 };

        page_t* new_page() {
            page_t* p = new page_t();
            pages.push_back(p);
            return p;
        }

        column_t() = default;

        column_t(DataType data_type)
            : pages()
            , num_rows(0) {
        }

        void insert(const ColumnarUtils::value_t& value) {
            if (pages.empty() || pages.back()->full()) {
                new_page();
            }
            pages.back()->insert(value);
            num_rows++;
        }

        ~column_t() {
            for (auto* page : pages) {
                delete page;
            }
        }
    };

    // ---- helpers ----

    // read bitmap bit (true => non-null)
    inline bool get_bitmap(const uint8_t* bitmap, uint16_t idx) {
        size_t byte_idx = idx / 8;
        uint8_t bit = idx % 8;
        return (bitmap[byte_idx] >> bit) & 0x1u;
    }

    inline std::vector<column_t> my_copy(
        const ColumnarTable& table,
        const std::vector<std::tuple<size_t, DataType>>& output_attrs,
        size_t table_id) {

        size_t num_rows = table.num_rows;
        size_t out_cols = output_attrs.size();
        std::vector<column_t> results;
        for (auto&& [_, dt] : output_attrs) {
            results.emplace_back(dt);
        }

        for (size_t out_col = 0; out_col < output_attrs.size(); out_col++) {
            size_t col_idx = std::get<0>(output_attrs[out_col]);
            DataType dt = std::get<1>(output_attrs[out_col]);
            if (col_idx >= table.columns.size()) {
                continue;
            }
            const Column& col = table.columns[col_idx];
            size_t row_idx = 0;
            size_t page_id = 0;

            for (Page* p : col.pages) {
                if (!p) {
                    page_id++;
                    continue;
                }

                uint16_t header = *reinterpret_cast<uint16_t*>(p->data);

                if (dt == DataType::INT32) {
                    uint16_t rows_in_page = header;
                    const uint32_t* data_begin = reinterpret_cast<const uint32_t*>(p->data + 4);
                    const uint8_t* bitmap = reinterpret_cast<const uint8_t*>(p->data + PAGE_SIZE - ((rows_in_page + 7) / 8));
                    uint16_t data_idx = 0;
                    for (uint16_t i = 0; i < rows_in_page && row_idx < num_rows; i++) {
                        if (get_bitmap(bitmap, i)) {
                            uint32_t v = data_begin[data_idx++];
                            results[out_col].insert(ColumnarUtils::value_t::make_int32(static_cast<int32_t>(v)));
                        }
                        else {
                            results[out_col].insert(ColumnarUtils::value_t::make_null());
                        }
                        // else leave null
                        row_idx++;
                    }
                }
                else if (dt == DataType::VARCHAR) {
                    if (header == 0xFFFF) {
                        // long-string first page => one logical row
                        if (row_idx < num_rows) {
                            ColumnarUtils::str_rep_t rep;
                            rep.table_id = table_id & ((1ull << 5) - 1);
                            rep.column_id = static_cast<uint64_t>(col_idx) & ((1ull << 3) - 1);
                            rep.page_id = page_id & ((1ull << 20) - 1);
                            rep.offset = 0;
                            results[out_col].insert(ColumnarUtils::value_t::make_string(rep));
                        }
                        row_idx++;
                    }
                    else if (header == 0xFFFE) {
                        // continuation page: no logical new row
                    }

                    else {
                        // short string page
                        uint16_t rows_in_page = header;
                        uint16_t non_null = *reinterpret_cast<uint16_t*>(p->data + 2);
                        const uint16_t* offsets = reinterpret_cast<const uint16_t*>(p->data + 4);
                        const uint8_t* bitmap = reinterpret_cast<const uint8_t*>(p->data + PAGE_SIZE - ((rows_in_page + 7) / 8));
                        uint16_t data_idx = 0; // index into offsets[] for non-null entries
                        for (uint16_t i = 0; i < rows_in_page && row_idx < num_rows; i++) {
                            if (get_bitmap(bitmap, i)) {
                                // offsets[data_idx] is the end offset; start = previous offset or 0
                                uint16_t end_off = offsets[data_idx];
                                uint16_t start_off = (data_idx == 0) ? 0 : offsets[data_idx - 1];
                                ColumnarUtils::str_rep_t rep;
                                rep.table_id = table_id & ((1ull << 5) - 1);
                                rep.column_id = static_cast<uint64_t>(col_idx) & ((1ull << 3) - 1);
                                rep.page_id = page_id & ((1ull << 20) - 1);
                                rep.offset = static_cast<uint16_t>(start_off) & ((1ull << 16) - 1);
                                results[out_col].insert(ColumnarUtils::value_t::make_string(rep));
                                data_idx++;
                            }
                            else {
                                results[out_col].insert(ColumnarUtils::value_t::make_null());
                            }
                            row_idx++;
                        }
                    }
                }
                else {
                    // unsupported types: keep NULL
                    // we still must advance row_idx by number of logical rows on the page
                    if (header == 0xFFFF) {
                        results[out_col].insert(ColumnarUtils::value_t::make_null());
                        row_idx++;
                    }
                    else if (header == 0xFFFE) {
                        // continuation page: no logical rows
                    }
                    else {
                        uint16_t rows_in_page = header;
                        for (uint16_t i = 0; i < rows_in_page && row_idx < num_rows; i++) {
                            results[out_col].insert(ColumnarUtils::value_t::make_null());
                            row_idx++;
                        }
                    }
                }

                page_id++;
            } // pages

            // if remaining logical rows with no pages: leave as null
            while (row_idx < num_rows) {
                results[out_col].insert(ColumnarUtils::value_t::make_null());
                row_idx++;
            }
        } // output columns

        return results;
    }

    void set_bitmap(std::vector<int8_t>& bitmap, uint16_t idx) {
        while (bitmap.size() < idx / 8 + 1) {
            bitmap.emplace_back(0);
        }
        auto byte_idx = idx / 8;
        auto bit = idx % 8;
        bitmap[byte_idx] |= (1u << bit);
    }

    void unset_bitmap(std::vector<int8_t>& bitmap, uint16_t idx) {
        while (bitmap.size() < idx / 8 + 1) {
            bitmap.emplace_back(0);
        }
        auto byte_idx = idx / 8;
        auto bit = idx % 8;
        bitmap[byte_idx] &= ~(1u << bit);
    }

    inline ColumnarTable materialize(
        const Plan& plan,
        const std::vector<column_t>& columns,
        const std::vector<DataType>& types) {

        ColumnarTable ret;
        ret.num_rows = columns.empty() ? 0 : columns[0].num_rows;
        // ret.columns.reserve(columns.size());

        for (size_t c = 0; c < columns.size(); c++) {
            const column_t& col = columns[c];
            ret.columns.emplace_back(types[c]);
            Column& column = ret.columns.back();
            switch (types[c]) {
            case DataType::INT32: {
                uint16_t num_rows = 0;
                std::vector<int32_t> data;
                std::vector<int8_t> bitmap;
                data.reserve(2048);
                bitmap.reserve(256);
                auto save_page = [&column, &num_rows, &data, &bitmap]() {
                    auto* page = column.new_page()->data;
                    *reinterpret_cast<uint16_t*>(page) = num_rows;
                    *reinterpret_cast<uint16_t*>(page + 2) = static_cast<uint16_t>(data.size());
                    memcpy(page + 4, data.data(), data.size() * 4);
                    memcpy(page + PAGE_SIZE - bitmap.size(), bitmap.data(), bitmap.size());
                    num_rows = 0;
                    data.clear();
                    bitmap.clear();
                    };
                for (const auto* p : col.pages) {
                    for (size_t element = 0; element < p->num_elements; element++) {
                        const auto& value = p->data[element];
                        if (value.is_null()) {
                            if (4 + (data.size()) * 4 + (num_rows / 8 + 1) > PAGE_SIZE) {
                                save_page();
                            }
                            unset_bitmap(bitmap, num_rows);
                            num_rows++;
                        }
                        else if (value.kind() == ColumnarUtils::KIND_INT32) {
                            int32_t int_value = value.as_i32();
                            if (4 + (data.size() + 1) * 4 + (num_rows / 8 + 1) > PAGE_SIZE) {
                                save_page();
                            }
                            set_bitmap(bitmap, num_rows);
                            data.emplace_back(int_value);
                            num_rows++;
                        }
                        else {
                            throw std::runtime_error("not int32 or null");
                        }
                    }
                }
                if (num_rows != 0) {
                    save_page();
                }
                break;
            }
            case DataType::VARCHAR: {
                uint16_t num_rows = 0;
                std::vector<char>     data;
                std::vector<uint16_t> offsets;
                std::vector<int8_t>  bitmap;
                data.reserve(8192);
                offsets.reserve(4096);
                bitmap.reserve(512);
                auto save_long_string = [&column](std::string_view data) {
                    size_t offset = 0;
                    auto first_page = true;
                    while (offset < data.size()) {
                        auto* page = column.new_page()->data;
                        if (first_page) {
                            *reinterpret_cast<uint16_t*>(page) = 0xffff;
                            first_page = false;
                        }
                        else {
                            *reinterpret_cast<uint16_t*>(page) = 0xfffe;
                        }
                        auto page_data_len = std::min(data.size() - offset, PAGE_SIZE - 4);
                        *reinterpret_cast<uint16_t*>(page + 2) = page_data_len;
                        memcpy(page + 4, data.data() + offset, page_data_len);
                        offset += page_data_len;
                    }
                    };
                auto save_page = [&column, &num_rows, &data, &offsets, &bitmap]() {
                    auto* page = column.new_page()->data;
                    *reinterpret_cast<uint16_t*>(page) = num_rows;
                    *reinterpret_cast<uint16_t*>(page + 2) = static_cast<uint16_t>(offsets.size());
                    memcpy(page + 4, offsets.data(), offsets.size() * 2);
                    memcpy(page + 4 + offsets.size() * 2, data.data(), data.size());
                    memcpy(page + PAGE_SIZE - bitmap.size(), bitmap.data(), bitmap.size());
                    num_rows = 0;
                    data.clear();
                    offsets.clear();
                    bitmap.clear();
                    };
                for (const auto* p : col.pages) {
                    for (size_t element = 0; element < p->num_elements; element++) {
                        const auto& value = p->data[element];
                        if (value.is_null()) {
                            if (4 + offsets.size() * 2 + data.size() + (num_rows / 8) + 1 > PAGE_SIZE) {
                                save_page();
                            }
                            unset_bitmap(bitmap, num_rows);
                            num_rows++;
                        }
                        else if (value.kind() == ColumnarUtils::KIND_STRING) {
                            std::string s = ColumnarUtils::string_from_rep(plan, value.as_str_rep());
                            if (s.size() > PAGE_SIZE - 7) {
                                if (num_rows > 0) {
                                    save_page();
                                }
                                save_long_string(s);
                            }
                            else {
                                if (4 + (offsets.size() + 1) * 2 + (data.size() + s.size()) + (num_rows / 8 + 1) > PAGE_SIZE) {
                                    save_page();
                                }
                                set_bitmap(bitmap, num_rows);
                                data.insert(data.end(), s.begin(), s.end());
                                offsets.emplace_back(data.size());
                                num_rows++;
                            }
                        }
                        else {
                            throw std::runtime_error("not string or null");
                        }
                    }
                }
                if (num_rows != 0) {
                    save_page();
                }
                break;
            }
            }
        }

        return ret;
    }

} // namespace ColumnStoreUtils
