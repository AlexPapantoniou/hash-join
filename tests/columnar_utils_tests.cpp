#include <catch2/catch_test_macros.hpp>
#include <vector>
#include <cstdint>
#include <string>
#include <cstring>

#include <plan.h>
#include <table.h>
#include <columnar_utils.hpp>

using namespace ColumnarUtils;

TEST_CASE("value_t basic constructors and accessors", "[columnar_utils]") {
    // NULL
    value_t vnull = value_t::make_null();
    REQUIRE(vnull.is_null());
    REQUIRE(vnull.kind() == KIND_NULL);

    // INT32
    int32_t x = -123456;
    value_t vint = value_t::make_int32(x);
    REQUIRE_FALSE(vint.is_null());
    REQUIRE(vint.kind() == KIND_INT32);
    REQUIRE(vint.as_i32() == x);

    // STRING / str_rep packing and unpacking
    str_rep_t rep_in(3 /*table*/, 2 /*col*/, 17 /*page*/, 42 /*offset*/);
    value_t vstr = value_t::make_string(rep_in);
    REQUIRE_FALSE(vstr.is_null());
    REQUIRE(vstr.kind() == KIND_STRING);
    str_rep_t rep_out = vstr.as_str_rep();
    REQUIRE(rep_out == rep_in);
}

TEST_CASE("bitmap helpers set/get/unset", "[columnar_utils][bitmap]") {
    std::vector<int8_t> bitmap; // ColumnarUtils::set_bitmap/unset operate on vector<int8_t>
    // set bits 0, 3, 10
    set_bitmap(bitmap, 0);
    set_bitmap(bitmap, 3);
    set_bitmap(bitmap, 10);

    // check using get_bitmap (takes const uint8_t*)
    const uint8_t* raw = reinterpret_cast<const uint8_t*>(bitmap.data());
    REQUIRE(get_bitmap(raw, 0) == true);
    REQUIRE(get_bitmap(raw, 1) == false);
    REQUIRE(get_bitmap(raw, 3) == true);
    REQUIRE(get_bitmap(raw, 10) == true);

    // unset 3, ensure cleared
    unset_bitmap(bitmap, 3);
    raw = reinterpret_cast<const uint8_t*>(bitmap.data());
    REQUIRE(get_bitmap(raw, 3) == false);

    // unset a bit that wasn't set (20) -> should be harmless
    unset_bitmap(bitmap, 20);
    REQUIRE(get_bitmap(raw, 20) == false);
}

TEST_CASE("my_copy reads INT32 page correctly", "[columnar_utils][my_copy][int]") {
    // Build a ColumnarTable with one INT32 column and a single page:
    // header: rows_in_page = 4, non_null_count = 2
    // data block: two uint32 values (for non-null)
    // bitmap at page end: indicates which rows are non-null
    Plan plan;
    ColumnarTable table;
    table.columns.emplace_back(DataType::INT32);
    Column& col = table.columns.back();

    // allocate one page
    Page* p = col.new_page();
    // clear page
    memset(p->data, 0, PAGE_SIZE);

    // header
    uint16_t rows_in_page = 4;
    uint16_t non_null = 2;
    *reinterpret_cast<uint16_t*>(p->data) = rows_in_page;
    *reinterpret_cast<uint16_t*>(p->data + 2) = non_null;

    // write two uint32 values starting at p->data + 4
    uint32_t* data_begin = reinterpret_cast<uint32_t*>(p->data + 4);
    data_begin[0] = 111;
    data_begin[1] = 222;

    // build bitmap at page end: bitmap size = ceil(rows_in_page/8) = 1 byte
    uint8_t bitmap_byte = 0;
    // set non-null for rows 0 and 2 (for example)
    bitmap_byte |= (1u << 0); // row 0 non-null
    // row1 null
    bitmap_byte |= (1u << 2); // row2 non-null
    // write bitmap
    memcpy(p->data + PAGE_SIZE - 1, &bitmap_byte, 1);

    table.num_rows = rows_in_page;

    // Prepare output attrs: single column 0, DataType::INT32
    std::vector<std::tuple<size_t, DataType>> output_attrs = { {0, DataType::INT32} };

    auto res = my_copy(table, output_attrs, /*table_id=*/0);

    // Expect rows_in_page rows, each a vector of 1 value_t
    REQUIRE(res.size() == rows_in_page);
    // row0 -> 111, row1 -> null, row2 -> 222, row3 -> null
    REQUIRE_FALSE(res[0][0].is_null());
    REQUIRE(res[0][0].as_i32() == 111);
    REQUIRE(res[1][0].is_null());
    REQUIRE_FALSE(res[2][0].is_null());
    REQUIRE(res[2][0].as_i32() == 222);
    REQUIRE(res[3][0].is_null());
}

TEST_CASE("my_copy + string_from_rep short VARCHAR page roundtrip", "[columnar_utils][my_copy][string]") {
    Plan plan;
    ColumnarTable table;
    table.columns.emplace_back(DataType::VARCHAR);
    Column& col = table.columns.back();

    // Create a short-string page with 2 rows, both non-null.
    Page* p = col.new_page();
    memset(p->data, 0, PAGE_SIZE);

    uint16_t rows_in_page = 2;
    *reinterpret_cast<uint16_t*>(p->data) = rows_in_page;

    // set non_null count = 2
    *reinterpret_cast<uint16_t*>(p->data + 2) = 2;

    // offsets array: two end offsets (end of first string, end of second)
    // we'll store "foo" (3 bytes) and "hello" (5 bytes) concatenated
    const char* s1 = "foo";
    const char* s2 = "hello";
    size_t dpos = 4;
    uint16_t off1 = static_cast<uint16_t>(strlen(s1));       // 3
    uint16_t off2 = static_cast<uint16_t>(strlen(s1) + strlen(s2)); // 8

    // write offsets at p->data + 4
    uint16_t* offsets = reinterpret_cast<uint16_t*>(p->data + 4);
    offsets[0] = off1;
    offsets[1] = off2;

    // write the data block immediately after the offsets (4 + 2*2 = 8)
    char* datablock = reinterpret_cast<char*>(p->data + 4 + 2 * sizeof(uint16_t));
    memcpy(datablock + 0, s1, strlen(s1));
    memcpy(datablock + off1, s2, strlen(s2));

    // bitmap: 1 byte at page end, mark both rows non-null
    uint8_t b = 0x03; // bits 0 and 1 set
    memcpy(p->data + PAGE_SIZE - 1, &b, 1);

    table.num_rows = rows_in_page;
    plan.inputs.emplace_back(std::move(table)); // so string_from_rep can read pages

    // prepare output attrs (single column)
    std::vector<std::tuple<size_t, DataType>> output_attrs = { {0, DataType::VARCHAR} };

    auto res = my_copy(plan.inputs[0], output_attrs, /*table_id=*/0);

    REQUIRE(res.size() == 2);
    REQUIRE(res[0][0].kind() == KIND_STRING);
    REQUIRE(res[1][0].kind() == KIND_STRING);

    // reconstruct strings via string_from_rep and compare
    str_rep_t r0 = res[0][0].as_str_rep();
    str_rep_t r1 = res[1][0].as_str_rep();

    std::string str0 = string_from_rep(plan, r0);
    std::string str1 = string_from_rep(plan, r1);

    REQUIRE(str0 == "foo"); // dummy check to ensure s0 available
    REQUIRE(str1 == "hello"); // dummy check to ensure s0 available
}

ColumnarTable create_int_column(ColumnarTable table) {
    Column& col = table.columns[0];
    Page* p = col.new_page();
    memset(p->data, 0, PAGE_SIZE);

    uint16_t rows_in_page = 2;
    *reinterpret_cast<uint16_t*>(p->data) = rows_in_page;

    // set non_null count = 2
    *reinterpret_cast<uint16_t*>(p->data + 2) = 2;

    // write two uint32 values starting at p->data + 4
    uint32_t* data_begin = reinterpret_cast<uint32_t*>(p->data + 4);
    data_begin[0] = 111;
    data_begin[1] = 222;

    // build bitmap at page end: bitmap size = ceil(rows_in_page/8) = 1 byte
    uint8_t bitmap_byte = 0;
    // set non-null for rows 0 and 1 (for example)
    bitmap_byte |= (1u << 0); // row 0 non-null
    // row1 null
    bitmap_byte |= (1u << 1); // row2 non-null
    // write bitmap
    memcpy(p->data + PAGE_SIZE - 1, &bitmap_byte, 1);

    table.num_rows = rows_in_page;

    return table;
}

ColumnarTable create_varchar_column(ColumnarTable table) {
    Column& col = table.columns[1];
    Page* p = col.new_page();
    memset(p->data, 0, PAGE_SIZE);

    uint16_t rows_in_page = 2;
    *reinterpret_cast<uint16_t*>(p->data) = rows_in_page;

    // set non_null count = 2
    *reinterpret_cast<uint16_t*>(p->data + 2) = 2;

    // offsets array: two end offsets (end of first string, end of second)
    // we'll store "foo" (3 bytes) and "hello" (5 bytes) concatenated
    const char* s1 = "foo";
    const char* s2 = "hello";
    size_t dpos = 4;
    uint16_t off1 = static_cast<uint16_t>(strlen(s1));       // 3
    uint16_t off2 = static_cast<uint16_t>(strlen(s1) + strlen(s2)); // 8

    // write offsets at p->data + 4
    uint16_t* offsets = reinterpret_cast<uint16_t*>(p->data + 4);
    offsets[0] = off1;
    offsets[1] = off2;

    // write the data block immediately after the offsets (4 + 2*2 = 8)
    char* datablock = reinterpret_cast<char*>(p->data + 4 + 2 * sizeof(uint16_t));
    memcpy(datablock + 0, s1, strlen(s1));
    memcpy(datablock + off1, s2, strlen(s2));

    // bitmap: 1 byte at page end, mark both rows non-null
    uint8_t b = 0x03; // bits 0 and 1 set
    memcpy(p->data + PAGE_SIZE - 1, &b, 1);

    table.num_rows = rows_in_page;

    return table;
}

TEST_CASE("materialize works with both INT32 and VARCHAR", "[columnar_utils][materialize]") {
    Plan plan;
    ColumnarTable table;
    table.columns.emplace_back(DataType::INT32);
    table.columns.emplace_back(DataType::VARCHAR);

    // Create columns
    table = create_int_column(std::move(table));
    table = create_varchar_column(std::move(table));

    plan.inputs.emplace_back(std::move(table));

    // Prepare output attrs
    std::vector<std::tuple<size_t, DataType>> output_attrs = {
        {0, DataType::INT32},
        {1, DataType::VARCHAR}
    };

    // Perform copy
    auto ret = my_copy(plan.inputs[0], output_attrs, 0);

    // Extract return types
    std::vector<DataType> ret_types;
    for (auto& x : output_attrs) {
        ret_types.push_back(std::get<1>(x));
    }

    auto res = materialize(plan, ret, ret_types);

    // Variant extraction
    REQUIRE(std::holds_alternative<int32_t>(res[0][0]));
    REQUIRE(std::holds_alternative<int32_t>(res[1][0]));
    REQUIRE(std::holds_alternative<std::string>(res[0][1]));
    REQUIRE(std::holds_alternative<std::string>(res[1][1]));

    int32_t res00 = std::get<int32_t>(res[0][0]);
    int32_t res10 = std::get<int32_t>(res[1][0]);
    std::string res01 = std::get<std::string>(res[0][1]);
    std::string res11 = std::get<std::string>(res[1][1]);

    // Expectations
    REQUIRE(res00 == 111);
    REQUIRE(res10 == 222);
    REQUIRE(res01 == "foo");
    REQUIRE(res11 == "hello");
}
