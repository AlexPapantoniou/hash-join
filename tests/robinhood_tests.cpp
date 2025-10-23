#include <catch2/catch_test_macros.hpp>
#include <string>
// #include <vector>

#include "../src/hashmap_robinhood.hpp"

TEST_CASE("HashMapRobinhood basic insertion and lookup", "[hashmap]") {
    HashMapRobinhood<int, std::vector<size_t>> map(10);

    REQUIRE(map.size() == 0);
    REQUIRE(map.empty());

    REQUIRE(map.emplace(10, std::vector<size_t>{1}));
    REQUIRE(map.emplace(20, std::vector<size_t>{2, 3}));

    REQUIRE_FALSE(map.empty());
    REQUIRE(map.size() == 2);

    auto val10 = map.find(10);
    REQUIRE(val10.has_value());
    REQUIRE(val10->first == 10);
    REQUIRE(val10->second == std::vector<size_t>{1});

    auto val20 = map.find(20);
    REQUIRE(val20.has_value());
    REQUIRE(val20->first == 20);
    REQUIRE(val20->second == std::vector<size_t>{2, 3});

    auto val40 = map.find(40);
    REQUIRE_FALSE(val40.has_value());

}

TEST_CASE("HashMapRobinhood handles collisions via Robin Hood hashing", "[hashmap][collision]") {
    HashMapRobinhood<size_t, std::vector<size_t>> map(4);

    for (size_t i = 0; i < 8; i++) {
        map.emplace(i, std::vector<size_t>{i});
    }

    REQUIRE(map.size() == 8);

    // All keys should still be retrievable
    for (size_t i = 0; i < 8; i++) {
        auto res = map.find(i);
        REQUIRE(res.has_value());
        REQUIRE(res->second == std::vector<size_t>{i});
    }
}

TEST_CASE("HashMapRobinhood rehashes correctly when load factor exceeds 0.5", "[hashmap][rehash]") {
    HashMapRobinhood<size_t, std::vector<size_t>> map(2);
    size_t old_capacity = 2;

    for (size_t i = 0; i < 10; i++) {
        map.emplace(i, std::vector<size_t>{i});
    }

    REQUIRE(map.size() == 10);

    for (size_t i = 0; i < 10; i++) {
        auto res = map.find(i);
        REQUIRE(res.has_value());
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
    REQUIRE(res.has_value());
    REQUIRE(res->first == "banana");
    REQUIRE(res->second == std::vector<size_t>{2, 3});

    REQUIRE_FALSE(map.find("mango").has_value());
}