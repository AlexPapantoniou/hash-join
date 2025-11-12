#include <catch2/catch_test_macros.hpp>
#include <string>

#include "../src/hashmap_cuckoo.hpp"

// Test that simple insertions work and the keys are found successfully 
TEST_CASE("HashMapCuckoo basic insertion and lookup", "[hashmap]") {
    HashMapCuckoo<int, std::string> map(16);

    REQUIRE(map.size() == 0);
    REQUIRE(map.capacity() == 16);
    REQUIRE(map.empty());

    SECTION("Single Insert") {
        REQUIRE(map.emplace(1, "one"));
        REQUIRE(map.size() == 1);
        REQUIRE_FALSE(map.empty());

        auto it = map.find(1);
        REQUIRE(it != map.end());
        REQUIRE(it->first == 1);
        REQUIRE(it->second == "one");
    }

    SECTION("Multiple Inserts And Lookups") {
        REQUIRE(map.emplace(10, "ten"));
        REQUIRE(map.emplace(20, "twenty"));
        REQUIRE(map.emplace(30, "thirty"));

        REQUIRE_FALSE(map.empty());
        REQUIRE(map.size() == 3);

        auto it10 = map.find(10);
        auto it20 = map.find(20);
        auto it30 = map.find(30);

        REQUIRE(it10 != map.end());
        REQUIRE(it10->first == 10);
        REQUIRE(it10->second == "ten");

        REQUIRE(it20 != map.end());
        REQUIRE(it20->first == 20);
        REQUIRE(it20->second == "twenty");

        REQUIRE(it30 != map.end());
        REQUIRE(it30->first == 30);
        REQUIRE(it30->second == "thirty");

        auto it40 = map.find(40);
        REQUIRE(it40 == map.end());    // Key '40' shouldn't exist
    }
}


// Test proper rehashing
TEST_CASE("HashMapCuckoo rehashes correctly when load factor exceeds 0.9", "[hashmap][rehash]") {
    HashMapCuckoo<size_t, size_t> map(3);


    size_t initial_capacity = map.capacity();
    REQUIRE(initial_capacity == 4);

    const size_t num_elements = 100;
    for (size_t i = 0; i < num_elements; i++) {
        REQUIRE(map.emplace(i, i * i));
    }

    REQUIRE(map.size() == num_elements);
    REQUIRE(map.capacity() == 128);

    for (size_t i = 0; i < num_elements; i++) {
        auto it = map.find(i);
        REQUIRE(it != map.end());
        REQUIRE(it->second == i * i);
    }
}

// Test collision handling and increment operators
TEST_CASE("HashMapCuckoo handles collisions through cuckoo hashing", "[hashmap][collision]") {
    HashMapCuckoo<size_t, size_t> map(8);

    REQUIRE(map.emplace(78, 78));   // buckets1[6]
    REQUIRE(map.emplace(7, 7));     // buckets1[7]
    REQUIRE(map.emplace(6, 6));     // 78 -> buckets2[0], 6 -> buckets1[6]

    auto it6 = map.find(6);
    REQUIRE(it6 != map.end());

    auto it7 = map.find(7);
    REQUIRE(it7 != map.end());

    auto it78 = map.find(78);
    REQUIRE(it78 != map.end());

    // Test increment operators
    REQUIRE((++it7)->first == 78);   // 7 is in the last spot of buckets1, next is first spot of buckets2 (78)
    REQUIRE((it6++)->first == 6);
    REQUIRE(it6->first == 7);
}

// Test that hash map works with other data types of keys
TEST_CASE("HashMapCuckoo supports string keys", "[hashmap][string]") {
    HashMapCuckoo<std::string, std::vector<size_t>> map;

    map.emplace("apple", { 1 });
    map.emplace("banana", { 2, 3 });
    map.emplace("cherry", { 4 });

    REQUIRE(map.size() == 3);

    auto it = map.find("banana");
    REQUIRE(it != map.end());
    REQUIRE(it->first == "banana");
    REQUIRE(it->second == std::vector<size_t>{2, 3});

    REQUIRE_FALSE(map.find("mango") != map.end());
}