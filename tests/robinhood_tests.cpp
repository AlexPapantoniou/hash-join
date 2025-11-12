#include <catch2/catch_test_macros.hpp>
#include <string>

#include "../src/hashmap_robinhood.hpp"

// Test that simple insertions work and the keys are found successfully 
TEST_CASE("HashMapRobinhood basic insertion and lookup", "[hashmap]") {
    HashMapRobinhood<int, std::vector<size_t>> map;

    REQUIRE(map.size() == 0);
    REQUIRE(map.capacity() == 16);
    REQUIRE(map.empty());

    REQUIRE(map.emplace(10, std::vector<size_t>{1}));
    REQUIRE(map.emplace(20, std::vector<size_t>{2, 3}));
    REQUIRE(map.emplace(30, std::vector<size_t>{4}));

    REQUIRE_FALSE(map.empty());
    REQUIRE(map.size() == 3);

    auto val10 = map.find(10);
    REQUIRE(val10 != map.end());
    REQUIRE(val10->first == 10);
    REQUIRE(val10->second == std::vector<size_t>{1});

    auto val20 = map.find(20);
    REQUIRE(val20 != map.end());
    REQUIRE(val20->first == 20);
    REQUIRE(val20->second == std::vector<size_t>{2, 3});

    auto val40 = map.find(40);
    REQUIRE(val40 == map.end());    // Key '40' shouldn't exist
}

// Test that when collisions happen, each key gets moved to the expected position and increment operators work
TEST_CASE("HashMapRobinhood handles collisions via Robin Hood hashing", "[hashmap][collision][hashfunction]") {
    HashMapRobinhood<size_t, std::vector<size_t>> map(8);

    REQUIRE(map.emplace(11, std::vector<size_t>{11}));  // buckets[5]
    auto key11 = map[5];     // Search keys via exact position in the hash map
    REQUIRE(key11 != map.end());
    REQUIRE(key11->first == 11);

    REQUIRE(map.emplace(10, std::vector<size_t>{10}));  // buckets[4]
    auto key10 = map[4];
    REQUIRE(key10 != map.end());
    REQUIRE(key10->first == 10);

    REQUIRE(map.emplace(12, std::vector<size_t>{12}));  // buckets[5] is occupied -> buckets[6]
    auto key12 = map[6];
    REQUIRE(key12 != map.end());
    REQUIRE(key12->first == 12);

    // buckets[4] is occupied -> buckets[5], kicks 11, 11 kicks 12, 12 moves to buckets[7]
    REQUIRE(map.emplace(13, std::vector<size_t>{13}));

    // Check that each key is placed at the correct spot
    key10 = map[4];
    auto key13 = map[5];
    key11 = map[6];
    key12 = map[7];
    REQUIRE(key10 != map.end());
    REQUIRE(key13 != map.end());
    REQUIRE(key11 != map.end());
    REQUIRE(key12 != map.end());
    REQUIRE(key10->first == 10);
    REQUIRE(key13->first == 13);
    REQUIRE(key11->first == 11);
    REQUIRE(key12->first == 12);

    // Test increment operators
    REQUIRE((++key10)->second == std::vector<size_t>{13});
    REQUIRE(key12++ != map.end());
    REQUIRE(key12 == map.end());
}

// Test proper rehashing
TEST_CASE("HashMapRobinhood rehashes correctly when load factor exceeds 0.75", "[hashmap][rehash]") {
    HashMapRobinhood<size_t, std::vector<size_t>> map(3);
    size_t old_capacity = 3;

    REQUIRE(map.capacity() == 4);

    for (size_t i = 0; i < 10; i++) {
        map.emplace(i, std::vector<size_t>{i});
    }

    REQUIRE(map.size() == 10);
    REQUIRE(map.capacity() == 16);

    for (size_t i = 0; i < 10; i++) {
        auto res = map.find(i);
        REQUIRE(res != map.end());
        REQUIRE(res->second == std::vector<size_t>{i});
    }
}

// Test that hash map works with other data types of keys
TEST_CASE("HashMapRobinhood supports string keys", "[hashmap][string]") {
    HashMapRobinhood<std::string, std::vector<size_t>> map;

    map.emplace("apple", { 1 });
    map.emplace("banana", { 2, 3 });
    map.emplace("cherry", { 4 });

    REQUIRE(map.size() == 3);

    auto res = map.find("banana");
    REQUIRE(res != map.end());
    REQUIRE(res->first == "banana");
    REQUIRE(res->second == std::vector<size_t>{2, 3});

    REQUIRE_FALSE(map.find("mango") != map.end());
}