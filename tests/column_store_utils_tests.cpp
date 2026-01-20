#include <catch2/catch_test_macros.hpp>
#include <vector>
#include <cstdint>
#include <string>
#include <cstring>

#include <plan.h>
#include <table.h>
#include <column_store_utils.hpp>
#include <columnar_utils.hpp>

using namespace ColumnStoreUtils;

TEST_CASE("page_t basic testing", "[column_store_utils][page_t]") {
    column_t::page_t page;

    REQUIRE(page.empty());

    for (int i = 0; i < column_t::ELEMENTS_PER_PAGE; i++) {
        page.insert(ColumnarUtils::value_t());
    }
    REQUIRE(page.full());
}

TEST_CASE("column_t basic testing", "[column_store_utils][column_t]") {
    column_t col;

    REQUIRE(col.num_rows == 0);
    REQUIRE(col.pages.empty());

    col.insert(ColumnarUtils::value_t());
    REQUIRE(col.num_rows == 1);
    REQUIRE(col.pages.size() == 1);

    for (int i = 0; i < column_t::ELEMENTS_PER_PAGE; i++) {
        col.insert(ColumnarUtils::value_t());
    }

    REQUIRE(col.num_rows == column_t::ELEMENTS_PER_PAGE + 1);
    REQUIRE(col.pages.size() == 2);
}

static void create_null_column(ColumnarTable& table) {
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
    bitmap_byte |= (1u << 0); // row0 non-null
    // row1 null
    bitmap_byte |= (1u << 2); // row2 non-null
    // write bitmap
    memcpy(p->data + PAGE_SIZE - 1, &bitmap_byte, 1);
}

static void create_non_null_column(ColumnarTable& table) {
    table.columns.emplace_back(DataType::INT32);
    Column& col = table.columns.back();
    // allocate one page
    Page* p = col.new_page();
    // clear page
    memset(p->data, 0, PAGE_SIZE);

    // header
    uint16_t rows_in_page = 4;
    uint16_t non_null = 4;
    *reinterpret_cast<uint16_t*>(p->data) = rows_in_page;
    *reinterpret_cast<uint16_t*>(p->data + 2) = non_null;

    // write two uint32 values starting at p->data + 4
    uint32_t* data_begin = reinterpret_cast<uint32_t*>(p->data + 4);
    data_begin[0] = 111;
    data_begin[1] = 222;
    data_begin[2] = 333;
    data_begin[3] = 444;

    // build bitmap at page end: bitmap size = ceil(rows_in_page/8) = 1 byte
    uint8_t bitmap_byte = 0;
    // set non-null for all rows
    bitmap_byte |= (1u << 0); // row0 non-null
    bitmap_byte |= (1u << 1); // row1 non-null
    bitmap_byte |= (1u << 2); // row2 non-null
    bitmap_byte |= (1u << 3); // row3 non-null
    // write bitmap
    memcpy(p->data + PAGE_SIZE - 1, &bitmap_byte, 1);
}

TEST_CASE("my_copy reads INT32 correctly", "[column_store_utils][my_copy][int]") {
    // Build a ColumnarTable with one INT32 column and a single page:
    // header: rows_in_page = 4, non_null_count = 2
    // data block: two uint32 values (for non-null)
    // bitmap at page end: indicates which rows are non-null
    Plan plan;
    ColumnarTable table;
    table.num_rows = 4;
    create_null_column(table);
    create_non_null_column(table);

    // Prepare output attrs: 2 columns, DataType::INT32
    std::vector<std::tuple<size_t, DataType>> output_attrs = { {0, DataType::INT32}, {1, DataType::INT32} };

    auto res = my_copy(table, output_attrs, 0);

    REQUIRE(res.size() == 2);
    REQUIRE(res[0].num_rows == 4);

    const column_t& column0 = res[0];
    REQUIRE(column0.has_nulls);
    REQUIRE_FALSE(column0.pages.empty());
    REQUIRE(column0.orig_col == nullptr);
    const auto* page0 = column0.pages[0];

    ColumnarUtils::value_t value = page0->data[0];
    REQUIRE_FALSE(value.is_null());
    REQUIRE(value.as_i32() == 111);

    value = page0->data[1];
    REQUIRE(value.is_null());

    value = page0->data[2];
    REQUIRE_FALSE(value.is_null());
    REQUIRE(value.as_i32() == 222);

    const column_t& column1 = res[1];
    REQUIRE_FALSE(column1.has_nulls);
    REQUIRE(column1.pages.empty());
    REQUIRE_FALSE(column1.orig_col == nullptr);
}

TEST_CASE("my_copy + string_from_rep short VARCHAR page roundtrip", "[column_store_utils][my_copy][short_string]") {
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

    auto res = my_copy(plan.inputs[0], output_attrs, 0);

    REQUIRE(res.size() == 1);
    REQUIRE(res[0].num_rows == rows_in_page);

    const column_t& column = res[0];
    const auto* page = column.pages[0];

    ColumnarUtils::value_t value0 = page->data[0];
    REQUIRE_FALSE(value0.is_null());
    REQUIRE(value0.kind() == ColumnarUtils::KIND_STRING);

    ColumnarUtils::value_t value1 = page->data[1];
    REQUIRE_FALSE(value1.is_null());
    REQUIRE(value1.kind() == ColumnarUtils::KIND_STRING);

    // reconstruct strings via string_from_rep and compare
    ColumnarUtils::str_rep_t r0 = value0.as_str_rep();
    ColumnarUtils::str_rep_t r1 = value1.as_str_rep();

    std::string str0 = string_from_rep(plan, r0);
    std::string str1 = string_from_rep(plan, r1);

    REQUIRE(str0 == "foo"); // dummy check to ensure s0 available
    REQUIRE(str1 == "hello"); // dummy check to ensure s1 available
}

TEST_CASE("my_copy + string_from_rep long VARCHAR page roundtrip", "[column_store_utils][my_copy][long_string]") {
    Plan plan;
    ColumnarTable table;
    table.columns.emplace_back(DataType::VARCHAR);
    Column& col = table.columns.back();

    const size_t N = 10000;
    std::string long_str(10000, 'a');

    size_t remaining = N;
    size_t offset = 0;
    bool first = true;

    while (remaining > 0) {
        Page* p = col.new_page();
        memset(p->data, 0, PAGE_SIZE);

        uint16_t header = first ? 0xFFFF : 0xFFFE;
        *reinterpret_cast<uint16_t*>(p->data) = header;

        size_t cap = PAGE_SIZE - 4;
        size_t chunk_size = std::min(cap, remaining);

        *reinterpret_cast<uint16_t*>(p->data + 2) = static_cast<uint16_t>(chunk_size);

        memcpy(p->data + 4, long_str.data() + offset, chunk_size);

        remaining -= chunk_size;
        offset += chunk_size;
        first = false;
    }

    table.num_rows = 1;
    plan.inputs.emplace_back(std::move(table)); // so string_from_rep can read pages

    // prepare output attrs (single column)
    std::vector<std::tuple<size_t, DataType>> output_attrs = { {0, DataType::VARCHAR} };

    auto res = my_copy(plan.inputs[0], output_attrs, /*table_id=*/0);

    REQUIRE(res.size() == 1);
    REQUIRE(res[0].num_rows == 1);

    const column_t& column = res[0];
    const auto* page = column.pages[0];

    ColumnarUtils::value_t value0 = page->data[0];
    REQUIRE(value0.kind() == ColumnarUtils::KIND_STRING);

    // reconstruct strings via string_from_rep and compare
    ColumnarUtils::str_rep_t r0 = value0.as_str_rep();

    std::string str0 = string_from_rep(plan, r0);

    REQUIRE(str0 == long_str); // dummy check to ensure s0 available
}

static column_t make_int_column() {
    column_t col;
    col.insert(ColumnarUtils::value_t::make_int32(111));
    col.insert(ColumnarUtils::value_t::make_int32(222));
    return col;
}

static column_t make_non_null_int_column(Column& col) {
    // allocate one page
    Page* p = col.new_page();
    // clear page
    memset(p->data, 0, PAGE_SIZE);

    // header
    uint16_t rows_in_page = 4;
    uint16_t non_null = 4;
    *reinterpret_cast<uint16_t*>(p->data) = rows_in_page;
    *reinterpret_cast<uint16_t*>(p->data + 2) = non_null;

    // write two uint32 values starting at p->data + 4
    uint32_t* data_begin = reinterpret_cast<uint32_t*>(p->data + 4);
    data_begin[0] = 111;
    data_begin[1] = 222;
    data_begin[2] = 333;
    data_begin[3] = 444;

    // build bitmap at page end: bitmap size = ceil(rows_in_page/8) = 1 byte
    uint8_t bitmap_byte = 0;
    // set non-null for all rows
    bitmap_byte |= (1u << 0); // row0 non-null
    bitmap_byte |= (1u << 1); // row1 non-null
    bitmap_byte |= (1u << 2); // row2 non-null
    bitmap_byte |= (1u << 3); // row3 non-null
    // write bitmap
    memcpy(p->data + PAGE_SIZE - 1, &bitmap_byte, 1);

    column_t colt;
    colt.has_nulls = false;
    colt.orig_col = &col;
    return colt;
}

static column_t make_varchar_column(const Plan& plan) {
    // We must reference real string storage in plan.inputs
    ColumnarTable table;
    table.columns.emplace_back(DataType::VARCHAR);

    // Build a real VARCHAR column so string_from_rep() works
    auto& column = table.columns[0];
    Page* p = column.new_page();
    memset(p->data, 0, PAGE_SIZE);

    *reinterpret_cast<uint16_t*>(p->data) = 2;      // rows
    *reinterpret_cast<uint16_t*>(p->data + 2) = 2;  // non-null

    uint16_t* offsets = reinterpret_cast<uint16_t*>(p->data + 4);
    offsets[0] = 3;
    offsets[1] = 8;

    char* data = reinterpret_cast<char*>(p->data + 8);
    memcpy(data + 0, "foo", 3);
    memcpy(data + 3, "hello", 5);

    uint8_t bitmap = 0x03;
    memcpy(p->data + PAGE_SIZE - 1, &bitmap, 1);

    table.num_rows = 2;

    // Register table inside plan
    const size_t table_id = plan.inputs.size();
    const_cast<Plan&>(plan).inputs.emplace_back(std::move(table));

    column_t col;
    col.insert(ColumnarUtils::value_t::make_string({ table_id, 0, 0, 0 }));
    col.insert(ColumnarUtils::value_t::make_string({ table_id, 0, 0, 3 }));
    return col;
}

TEST_CASE("materialize(INT32, VARCHAR) produces correct ColumnarTable", "[columnar_utils][materialize]") {
    Plan plan;

    column_t int_col = make_int_column();
    Column col(DataType::INT32);
    column_t non_null_int_col = make_non_null_int_column(col);
    column_t str_col = make_varchar_column(plan);

    std::vector<column_t> columns;
    columns.reserve(3);
    columns.push_back(std::move(int_col));
    columns.push_back(std::move(non_null_int_col));
    columns.push_back(std::move(str_col));

    std::vector<DataType> types = {
        DataType::INT32,
        DataType::INT32,
        DataType::VARCHAR
    };

    ColumnarTable result = materialize(plan, columns, types);

    // ----- INT32 checks -----
    {
        const Column& col = result.columns[0];
        REQUIRE(col.pages.size() == 1);

        Page* p = col.pages[0];
        uint16_t rows = *reinterpret_cast<uint16_t*>(p->data);
        REQUIRE(rows == 2);

        const uint32_t* values =
            reinterpret_cast<const uint32_t*>(p->data + 4);

        REQUIRE(values[0] == 111);
        REQUIRE(values[1] == 222);

        const uint8_t* bitmap =
            reinterpret_cast<const uint8_t*>(p->data + PAGE_SIZE - 1);

        REQUIRE((*bitmap & 0x1) != 0);
        REQUIRE((*bitmap & 0x2) != 0);
    }

    // ----- INT32 non null checks -----
    {
        const Column& col = result.columns[1];
        REQUIRE(col.pages.size() == 1);

        Page* p = col.pages[0];
        uint16_t rows = *reinterpret_cast<uint16_t*>(p->data);
        REQUIRE(rows == 4);

        const uint32_t* values =
            reinterpret_cast<const uint32_t*>(p->data + 4);

        REQUIRE(values[0] == 111);
        REQUIRE(values[1] == 222);
        REQUIRE(values[2] == 333);
        REQUIRE(values[3] == 444);

        const uint8_t* bitmap =
            reinterpret_cast<const uint8_t*>(p->data + PAGE_SIZE - 1);

        REQUIRE((*bitmap & 0x1) != 0);
        REQUIRE((*bitmap & 0x2) != 0);
        REQUIRE((*bitmap & 0x4) != 0);
        REQUIRE((*bitmap & 0x8) != 0);
    }

    // ----- VARCHAR checks -----
    {
        const Column& col = result.columns[2];
        REQUIRE(col.pages.size() == 1);

        Page* p = col.pages[0];
        uint16_t rows = *reinterpret_cast<uint16_t*>(p->data);
        REQUIRE(rows == 2);

        const uint16_t* offsets =
            reinterpret_cast<const uint16_t*>(p->data + 4);

        REQUIRE(offsets[0] == 3);
        REQUIRE(offsets[1] == 8);

        const char* strdata =
            reinterpret_cast<const char*>(p->data + 8);

        REQUIRE(std::string(strdata + 0, 3) == "foo");
        REQUIRE(std::string(strdata + 3, 5) == "hello");
    }
}
