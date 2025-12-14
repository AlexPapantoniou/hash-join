#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <cstring>
#include <stdexcept>
#include <algorithm>
#include <cassert>

#include "plan.h"
#include "table.h"

/*
 Compact representation for VARCHAR references and compact value_t union:
 - We pack a 3-bit kind in the low bits of a 64-bit word.
 - The remaining 61 bits are payload.
 - For INT32 we store the signed 32-bit value sign-extended into payload.
 - For STRING we pack a compact str_rep_t into payload.
 - We intentionally only support INT32, VARCHAR and NULL in this compact value_t,
   because INT64/FP64 cannot be represented losslessly together with a 3-bit tag
   inside 8 bytes without more complicated encoding.
*/

// Helper namespace 
namespace ColumnarUtils {

    // kind constants (3 low bits)
    constexpr uint8_t KIND_NULL = 0;
    constexpr uint8_t KIND_INT32 = 1;
    constexpr uint8_t KIND_STRING = 2;

    // ---- str_rep_t (fits inside 61 payload bits) ----
    // Layout chosen to match typical constraints:
    //  - table_id: 5 bits  (0..31)   (max table id <=16)
    //  - column_id: 3 bits (0..7)    (max column id <=6)
    //  - page_id: 20 bits (0..2^20-1)
    //  - offset: 16 bits  (0..65535) fits uint16_t offsets in page
    //
    // Total bits = 5 + 3 + 20 + 16 = 44 <= 61 payload bits
    typedef struct str_rep {
        uint64_t table_id : 5;
        uint64_t column_id : 3;
        uint64_t page_id : 20;
        uint64_t offset : 16;

        str_rep() = default;
        str_rep(uint64_t t, uint64_t c, uint64_t p, uint64_t o) :
            table_id(t), column_id(c), page_id(p), offset(o) {
        }

        bool operator==(const str_rep& o) const {
            return table_id == o.table_id && column_id == o.column_id
                && page_id == o.page_id && offset == o.offset;
        }
    } str_rep_t;

    // ---- compact 8-byte value_t ----
    // raw layout: lower 3 bits = kind, top 61 bits = payload
    struct value_t {
        uint64_t raw;

        value_t() noexcept : raw(0) {} // default NULL (kind == 0)

        static value_t make_null() noexcept {
            value_t x;
            x.raw = 0;
            return x;
        }

        static value_t make_int32(int32_t v) noexcept {
            // sign-extend 32-bit into 61-bit payload
            int64_t s = static_cast<int64_t>(v);
            // mask to 61 bits
            uint64_t payload = static_cast<uint64_t>(s) & ((1ull << 61) - 1ull);
            value_t x;
            x.raw = (payload << 3) | static_cast<uint64_t>(KIND_INT32);
            return x;
        }

        static value_t make_string(const str_rep_t& r) noexcept {
            uint64_t payload = 0;
            payload |= (static_cast<uint64_t>(r.table_id) & ((1ull << 5) - 1ull));
            payload |= ((static_cast<uint64_t>(r.column_id) & ((1ull << 3) - 1ull)) << 5);
            payload |= ((static_cast<uint64_t>(r.page_id) & ((1ull << 20) - 1ull)) << (5 + 3));
            payload |= ((static_cast<uint64_t>(r.offset) & ((1ull << 16) - 1ull)) << (5 + 3 + 20));
            value_t x;
            x.raw = (payload << 3) | static_cast<uint64_t>(KIND_STRING);
            return x;
        }

        bool is_null() const noexcept {
            return (raw & 0x7ull) == KIND_NULL;
        }

        uint8_t kind() const noexcept {
            return static_cast<uint8_t>(raw & 0x7ull);
        }

        // Interpret as int32 (only when kind()==KIND_INT32)
        int32_t as_i32() const noexcept {
            uint64_t payload = (raw >> 3) & ((1ull << 61) - 1ull);
            // sign-extend 61->64
            int64_t s;
            if (payload & (1ull << 60)) {
                s = static_cast<int64_t>(payload | (~((1ull << 61) - 1ull)));
            }
            else {
                s = static_cast<int64_t>(payload);
            }
            return static_cast<int32_t>(s);
        }

        // get str_rep (only when kind()==KIND_STRING)
        str_rep_t as_str_rep() const noexcept {
            uint64_t payload = (raw >> 3);
            str_rep_t r;
            r.table_id = (payload) & ((1ull << 5) - 1ull);
            r.column_id = (payload >> 5) & ((1ull << 3) - 1ull);
            r.page_id = (payload >> (5 + 3)) & ((1ull << 20) - 1ull);
            r.offset = (payload >> (5 + 3 + 20)) & ((1ull << 16) - 1ull);
            return r;
        }
    };

    // ---- helpers ----

    // read bitmap bit (true => non-null)
    inline bool get_bitmap(const uint8_t* bitmap, uint16_t idx) {
        size_t byte_idx = idx / 8;
        uint8_t bit = idx % 8;
        return (bitmap[byte_idx] >> bit) & 0x1u;
    }

    // reconstruct string from a str_rep (created by make_info_into)
    inline std::string string_from_rep(const Plan& plan, const str_rep_t& rep) {
        size_t t = rep.table_id;
        if (t >= plan.inputs.size()) {
            return std::string();
        }
        const ColumnarTable& table = plan.inputs[t];
        size_t col_idx = rep.column_id;
        if (col_idx >= table.columns.size()) {
            return std::string();
        }
        const Column& col = table.columns[col_idx];
        size_t page_id = rep.page_id;
        if (page_id >= col.pages.size()) {
            return std::string();
        }
        Page* p = col.pages[page_id];
        if (!p) {
            return std::string();
        }

        uint16_t header = *reinterpret_cast<uint16_t*>(p->data);
        if (header == 0xFFFF) {
            // long string: append content of this page and any following 0xFFFE pages
            std::string out;
            size_t cur = page_id;
            while (cur < col.pages.size()) {
                Page* curp = col.pages[cur];
                if (!curp) {
                    break;
                }
                uint16_t ch = *reinterpret_cast<uint16_t*>(curp->data);
                if (ch != 0xFFFF && ch != 0xFFFE) {
                    break;
                }
                uint16_t chunk_len = *reinterpret_cast<uint16_t*>(curp->data + 2);
                const char* payload = reinterpret_cast<const char*>(curp->data + 4);
                if (chunk_len > PAGE_SIZE - 4) {
                    break;
                }
                out.append(payload, chunk_len);
                cur++;
            }
            return out;
        }
        else {
            // short-string page: offsets array (end offsets) at data+4, char block at data+4 + non_null*2
            uint16_t rows_in_page = header;
            uint16_t offset_count = *reinterpret_cast<uint16_t*>(p->data + 2);
            if (offset_count == 0) {
                return std::string();
            }
            const uint16_t* offsets = reinterpret_cast<const uint16_t*>(p->data + 4);
            const size_t data_block_start = 4 + static_cast<size_t>(offset_count) * sizeof(uint16_t);
            size_t start = static_cast<size_t>(rep.offset);

            // find the first offsets[k] > start -> that's the end offset
            size_t k = 0;
            while (k < offset_count && static_cast<size_t>(offsets[k]) <= start) {
                k++;
            }
            if (k >= offset_count) {
                // last string: end = end of data block (bitmap start)
                size_t bitmap_start = PAGE_SIZE - ((static_cast<size_t>(rows_in_page) + 7) / 8);
                size_t end_off = bitmap_start - data_block_start;
                if (end_off <= start) {
                    return std::string();
                }
                const char* data_ptr = reinterpret_cast<const char*>(p->data + data_block_start + start);
                return std::string(data_ptr, end_off - start);
            }
            size_t end_off = static_cast<size_t>(offsets[k]);
            if (end_off <= start) {
                return std::string();
            }
            const char* data_ptr = reinterpret_cast<const char*>(p->data + data_block_start + start);
            return std::string(data_ptr, end_off - start);
        }
    }

    // my_copy(): create late-materialized rowstore (vector<vector<value_t>>)
    // INT32 -> store integer materialized, VARCHAR -> store str_rep (no copy)
    inline std::vector<std::vector<value_t>> my_copy(const ColumnarTable& table, const std::vector<std::tuple<size_t, DataType>>& output_attrs, size_t table_id) {
        size_t num_rows = table.num_rows;
        size_t out_cols = output_attrs.size();
        std::vector<std::vector<value_t>> results;
        results.assign(num_rows, std::vector<value_t>(out_cols, value_t::make_null()));

        // for each output slot (column in projection)
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
                            results[row_idx][out_col] = value_t::make_int32(static_cast<int32_t>(v));
                        }
                        // else leave null
                        row_idx++;
                    }
                }
                else if (dt == DataType::VARCHAR) {
                    if (header == 0xFFFF) {
                        // long-string first page => one logical row
                        if (row_idx < num_rows) {
                            str_rep_t rep;
                            rep.table_id = table_id & ((1ull << 5) - 1);
                            rep.column_id = static_cast<uint64_t>(col_idx) & ((1ull << 3) - 1);
                            rep.page_id = page_id & ((1ull << 20) - 1);
                            rep.offset = 0;
                            results[row_idx][out_col] = value_t::make_string(rep);
                        }
                        row_idx++;
                    }
                    else if (header == 0xFFFE) {
                        // continuation page: no new logical row
                    }
                    else {
                        // short-string page
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
                                str_rep_t rep;
                                rep.table_id = table_id & ((1ull << 5) - 1);
                                rep.column_id = static_cast<uint64_t>(col_idx) & ((1ull << 3) - 1);
                                rep.page_id = page_id & ((1ull << 20) - 1);
                                rep.offset = static_cast<uint16_t>(start_off) & ((1ull << 16) - 1);
                                results[row_idx][out_col] = value_t::make_string(rep);
                                data_idx++;
                            }
                            row_idx++;
                        }
                    }
                }
                else {
                    // unsupported types: keep NULL
                    // we still must advance row_idx by number of logical rows on the page
                    if (header == 0xFFFF) {
                        row_idx++;
                    }
                    else if (header == 0xFFFE) {
                        // continuation page: no logical rows
                    }
                    else {
                        uint16_t rows_in_page = header;
                        row_idx += rows_in_page;
                    }
                }

                page_id++;
            } // pages

            // if remaining logical rows with no pages: leave as null
        } // output columns

        return results;
    }

    // Materialize vector<vector<value_t>> -> vector<vector<Data>> using plan (for strings)
    inline std::vector<std::vector<Data>> materialize(const Plan& plan, const std::vector<std::vector<value_t>>& rows, const std::vector<DataType>& ret_types) {
        std::vector<std::vector<Data>> results;
        results.assign(rows.size(), std::vector<Data>(ret_types.size(), std::monostate{}));
        for (size_t r = 0; r < rows.size(); r++) {
            for (size_t c = 0; c < ret_types.size(); c++) {
                if (c >= rows[r].size()) {
                    continue;
                }

                DataType dt = ret_types[c];
                const value_t& v = rows[r][c];
                if (v.is_null()) {
                    continue;
                }
                if (dt == DataType::INT32 && v.kind() == KIND_INT32) {
                    results[r][c] = static_cast<int32_t>(v.as_i32());
                }
                else if (dt == DataType::VARCHAR && v.kind() == KIND_STRING) {
                    str_rep_t rep = v.as_str_rep();
                    try {
                        std::string s = string_from_rep(plan, rep);
                        if (s.empty()) {
                            // Log warning for suspicious empty string from failed lookup
                            std::cerr << "WARNING: materialize: empty string from string_from_rep at row "
                                << r << " col " << c << " (table_id=" << rep.table_id
                                << ", col_id=" << rep.column_id << ", page_id=" << rep.page_id
                                << ", offset=" << rep.offset << ")" << std::endl;
                        }
                        results[r][c] = std::move(s);
                    }
                    catch (const std::exception& e) {
                        std::cerr << "ERROR: materialize: string_from_rep failed at row " << r
                            << " col " << c << ": " << e.what() << std::endl;
                        results[r][c] = std::monostate{};
                    }
                }
                else {
                    // fallback: try reasonable conversions or set null
                    results[r][c] = std::monostate{};
                }
            }
        }
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

    // Convert vector<vector<value_t>> -> ColumnarTable in one step
    ColumnarTable from_value_t_to_columnar(const Plan& plan, const std::vector<std::vector<value_t>>& rows, const std::vector<DataType>& types) {
        auto& table = rows;
        auto& data_types = types;
        namespace views = ranges::views;
        ColumnarTable ret;
        ret.num_rows = table.size();
        for (auto [col_idx, data_type] : data_types | views::enumerate) {
            ret.columns.emplace_back(data_type);
            auto& column = ret.columns.back();
            switch (data_type) {
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
                for (auto& record : table) {
                    const auto& value = record[col_idx];
                    if (value.is_null()) {
                        if (4 + (data.size()) * 4 + (num_rows / 8 + 1) > PAGE_SIZE) {
                            save_page();
                        }
                        unset_bitmap(bitmap, num_rows);
                        num_rows++;
                    }
                    else if (value.kind() == KIND_INT32) {
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
                for (auto& record : table) {
                    const auto& value = record[col_idx];
                    if (value.is_null()) {
                        if (4 + offsets.size() * 2 + data.size() + (num_rows / 8) + 1 > PAGE_SIZE) {
                            save_page();
                        }
                        unset_bitmap(bitmap, num_rows);
                        num_rows++;
                    }
                    else if (value.kind() == KIND_STRING) {
                        std::string s = string_from_rep(plan, value.as_str_rep());
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
                if (num_rows != 0) {
                    save_page();
                }
                break;
            }
            }
        }

        return ret;
    }

    struct MyTable {
        std::vector<std::vector<value_t>> rows;
        std::vector<DataType>            types;

        MyTable(std::vector<std::vector<value_t>> r, std::vector<DataType> t)
            : rows(std::move(r)), types(std::move(t)) {
        }

        ColumnarTable from_value_t_to_columnar(const Plan& plan) {
            return ColumnarUtils::from_value_t_to_columnar(plan, rows, types);
        }
    };

} // namespace ColumnarUtils