#include <catch2/catch_test_macros.hpp>
#include <string>

#include "../src/hashmap_cuckoo.hpp"

TEST_CASE("HashMapCuckoo basic insertion and lookup", "[hashmap]") {
    HashMapCuckoo<int, std::string> map(16);

    REQUIRE(map.size() == 0);
    REQUIRE(map.empty());

    SECTION("Single Insert") {
        REQUIRE(map.emplace(1, "one"));
        REQUIRE(map.size() == 1);
        REQUIRE_FALSE(map.empty());

        auto it = map.find(1);
        REQUIRE(it != map.end());
        REQUIRE(it->second == "one");
    }

    SECTION("Multiple Inserts And Lookups") {
        REQUIRE(map.emplace(10, "ten"));
        REQUIRE(map.emplace(20, "twenty"));
        REQUIRE(map.emplace(30, "thirty"));

        REQUIRE(map.size() == 3);

        auto it10 = map.find(10);
        auto it20 = map.find(20);
        auto it30 = map.find(30);

        REQUIRE(it10 != map.end());
        REQUIRE(it10->second == "ten");

        REQUIRE(it20 != map.end());
        REQUIRE(it20->second == "twenty");

        REQUIRE(it30 != map.end());
        REQUIRE(it30->second == "thirty");

        auto it40 = map.find(40);

        REQUIRE(it40 == map.end());
    }
}

TEST_CASE("HashMapCuckoo rehashes correctly under load", "[hashmap][rehash]") {
    HashMapCuckoo<size_t, size_t> map(3);

    REQUIRE(map.capacity() == 4);

    const size_t num_elements = 100;
    for (size_t i = 0; i < num_elements; i++) {
        REQUIRE(map.emplace(i, i * i));
    }

    REQUIRE(map.size() == num_elements);
    REQUIRE(map.capacity() >= 128);

    for (size_t i = 0; i < num_elements; i++) {
        auto it = map.find(i);
        REQUIRE(it != map.end());
        REQUIRE(it->second == i * i);
    }
}

TEST_CASE("HashMapCuckoo supports string keys", "[hashmap][string]") {
    HashMapCuckoo<std::string, int> map;

    map.emplace("apple", 5);
    map.emplace("banana", 10);
    map.emplace("cherry", 7);

    REQUIRE(map.size() == 3);

    auto it = map.find("banana");
    REQUIRE(it != map.end());
    REQUIRE(it->first == "banana");
    REQUIRE(it->second == 10);

    REQUIRE_FALSE(map.find("mango") != map.end());
}

TEST_CASE("CuckooHash supports operator[] index-based iteration", "[cuckoo][index]") {
    HashMapCuckoo<int, int> map(8);

    map.emplace(1, 11);
    map.emplace(2, 22);
    map.emplace(3, 33);

    bool found_any = false;
    for (size_t i = 0; i < map.capacity(); ++i) {
        auto it = map[i];
        if (it != map.end()) {
            found_any = true;
        }
    }

    REQUIRE(found_any);
}