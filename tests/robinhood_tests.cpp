#include <catch2/catch_test_macros.hpp>
#include <string>

#include "../src/hashmap_robinhood.hpp"

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
    REQUIRE(val40 == map.end());
}

TEST_CASE("HashMapRobinhood handles collisions via Robin Hood hashing", "[hashmap][collision]") {
    HashMapRobinhood<size_t, std::vector<size_t>> map(8);

    REQUIRE(map.emplace(0, std::vector<size_t>{0}));
    auto key0 = map[0];
    REQUIRE(key0 != map.end());
    REQUIRE(key0->first == 0);

    REQUIRE(map.emplace(1, std::vector<size_t>{1}));
    auto key1 = map[1];
    REQUIRE(key1 != map.end());
    REQUIRE(key1->first == 1);

    REQUIRE(map.emplace(9, std::vector<size_t>{9}));
    auto key2 = map[2];
    REQUIRE(key2 != map.end());
    REQUIRE(key2->first == 9);

    REQUIRE(map.emplace(8, std::vector<size_t>{8}));
    // Check that each key is placed at the correct spot
    key0 = map[0];
    key1 = map[1];
    key2 = map[2];
    auto key3 = map[3];
    REQUIRE(key0 != map.end());
    REQUIRE(key1 != map.end());
    REQUIRE(key2 != map.end());
    REQUIRE(key3 != map.end());
    REQUIRE(key0->first == 0);
    REQUIRE(key1->first == 8);
    REQUIRE(key2->first == 1);
    REQUIRE(key3->first == 9);
}

TEST_CASE("HashMapRobinhood rehashes correctly when load factor exceeds 0.5", "[hashmap][rehash]") {
    HashMapRobinhood<size_t, std::vector<size_t>> map(3);
    size_t old_capacity = 3;

    REQUIRE(map.capacity() == 4);

    for (size_t i = 0; i < 10; i++) {
        map.emplace(i, std::vector<size_t>{i});
    }

    REQUIRE(map.size() == 10);
    REQUIRE(map.capacity() == 32);

    for (size_t i = 0; i < 10; i++) {
        auto res = map.find(i);
        REQUIRE(res != map.end());
        REQUIRE(res->second == std::vector<size_t>{i});
    }
}

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