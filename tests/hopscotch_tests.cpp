#include <catch2/catch_test_macros.hpp>
#include <string>

#include "../src/hashmap_hopscotch.hpp"

// Test that elements are inserted within the neighbourhood they hash to
template<typename Key, typename Value>
bool is_within_hop_range(HashMapHopscotch<Key, Value>& map, const Key& key, unsigned int hop_length) {
    std::hash<Key> hasher;
    size_t capacity = map.capacity();
    size_t home = hasher(key) & (capacity - 1);

    for (unsigned int i = 0; i < hop_length; i++) {
        size_t index = (home + i) & (capacity - 1);
        auto it = const_cast<HashMapHopscotch<Key, Value>&>(map)[index];
        if (it != map.end() && it->first == key) {
            return true;
        }
    }

    return false;
}

// Test that simple insertions work and the keys are found successfully and increment operators work
TEST_CASE("HashMapHopscotch basic insertion and lookup", "[hashmap]") {
    HashMapHopscotch<int, std::string> map(8);

    REQUIRE(map.size() == 0);
    REQUIRE(map.empty());

    REQUIRE(map.emplace(1, "one"));
    REQUIRE(map.emplace(2, "two"));
    REQUIRE(map.emplace(3, "three"));

    REQUIRE_FALSE(map.empty());
    REQUIRE(map.size() == 3);

    auto it1 = map.find(1);
    REQUIRE(it1 != map.end());
    REQUIRE(it1->first == 1);
    REQUIRE(it1->second == "one");

    auto it2 = map.find(2);
    REQUIRE(it2 != map.end());
    REQUIRE(it2->first == 2);
    REQUIRE(it2->second == "two");

    auto it3 = map.find(3);
    REQUIRE(it3 != map.end());
    REQUIRE(it3->first == 3);
    REQUIRE(it3->second == "three");

    auto not_found = map.find(10);
    REQUIRE(not_found == map.end()); //key '10' shouldn't exist

    // Test increment operators
    REQUIRE((++it3)->first == 0);
    REQUIRE((it2++)->first == 2);
    REQUIRE(it2->first == 3);
    REQUIRE((++it1) != map.end());
}

// Test collisions
TEST_CASE("HashMapHopscotch handles collisions via Hopscotch hashing", "[hashmap][collisions]") {
    HashMapHopscotch<int, std::string> map(8);
    unsigned int hop_len = map.neighbours_size();

    // keys are hashed at positions 4, 5, 6, 5, 7 respectively
    std::vector<int> keys = { 0, 8, 16, 24, 32 };

    for (int key : keys)
        map.emplace(key, "v" + std::to_string(key));

    REQUIRE(map.size() == keys.size());

    std::hash<int> hasher;
    size_t capacity = map.capacity();

    for (int key : keys) {
        size_t home = hasher(key) & (capacity - 1);
        bool found_in_neighborhood = false;

        for (unsigned int offset = 0; offset < hop_len; ++offset) {
            size_t idx = (home + offset) & (capacity - 1);
            auto it = map[idx];
            if (it != map.end() && (*it).first == key) {
                found_in_neighborhood = true;
                break;
            }
        }

        REQUIRE(found_in_neighborhood);
    }
}

// Insert elements and test that they are placed correctly within their hop neighborhood
TEST_CASE("HashMapHopscotch ensures keys stay within hop neighborhood", "[hashmap][collision]") {
    HashMapHopscotch<int, std::string> map(7);
    unsigned int hop_length = map.neighbours_size();

    REQUIRE(map.capacity() == 8);

    for (int i = 0; i < 20; i++) {
        REQUIRE(map.emplace(i, "val" + std::to_string(i)));
    }

    REQUIRE(map.size() == 20);
    REQUIRE(map.capacity() > 8);

    for (int i = 0; i < 20; i++) {
        REQUIRE(is_within_hop_range(map, i, hop_length));
    }
}

// Test proper rehashing
TEST_CASE("HashMapHopscotch rehashes correctly when load factor exceeds 0.75", "[hashmap][rehash]") {
    HashMapHopscotch<size_t, size_t> map(4);

    REQUIRE(map.capacity() == 4);

    for (size_t i = 0; i < 10; i++) {
        map.emplace(i, i * 10);
    }

    REQUIRE(map.size() == 10);
    REQUIRE(map.capacity() == 16);

    for (size_t i = 0; i < 10; i++) {
        auto it = map.find(i);
        REQUIRE(it != map.end());
        REQUIRE(it->second == i * 10);
    }
}

// Test that hash map works with other data types of keys
TEST_CASE("HashMapHopscotch supports string keys", "[hashmap][string]") {
    HashMapHopscotch<std::string, std::vector<size_t>> map;

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