#include <catch2/catch_test_macros.hpp>
#include <string>

#include "../src/hashmap_hopscotch.hpp"

size_t mix_hash(size_t i) noexcept {
    i += 1ull;
    i ^= i >> 33ull;
    i *= 0xff51afd7ed558ccdull;
    i ^= i >> 33ull;
    i *= 0xc4ceb9fe1a85ec53ull;
    i ^= i >> 33ull;
    return i;
}

template<typename Key, typename Value>
bool is_within_hop_range(HashMapHopscotch<Key, Value>& map, const Key& key, unsigned int hop_length) {
    std::hash<Key> hasher;
    size_t capacity = map.capacity();
    size_t home = mix_hash(hasher(key)) & (capacity - 1);

    for (unsigned int i = 0; i < hop_length; i++) {
        size_t index = (home + i) & (capacity - 1);
        auto it = const_cast<HashMapHopscotch<Key, Value>&>(map)[index];
        if (it != map.end() && it->first == key) {
            return true;
        }
    }

    return false;
}

TEST_CASE("HashMapHopscotch basic insertion and lookup", "[hashmap]") {
    HashMapHopscotch<int, std::string> map(8);

    REQUIRE(map.size() == 0);
    REQUIRE(map.empty());

    REQUIRE(map.emplace(1, "one"));
    REQUIRE(map.emplace(2, "two"));
    REQUIRE(map.emplace(3, "three"));

    REQUIRE_FALSE(map.empty());
    REQUIRE(map.size() == 3);

    auto it = map.find(1);
    REQUIRE(it != map.end());
    REQUIRE(it->first == 1);
    REQUIRE(it->second == "one");

    it = map.find(2);
    REQUIRE(it != map.end());
    REQUIRE(it->first == 2);
    REQUIRE(it->second == "two");

    auto not_found = map.find(10);
    REQUIRE(not_found == map.end());
}

TEST_CASE("HashMapHopscotch handles collisions via Hopscotch hashing", "[hashmap][collisions]") {
    constexpr unsigned int hop_len = 4;
    HashMapHopscotch<int, std::string> map(8);

    std::vector<int> keys = { 0, 8, 16, 24, 32 };

    for (int key : keys)
        map.emplace(key, "v" + std::to_string(key));

    REQUIRE(map.size() == keys.size());

    std::hash<int> hasher;
    size_t capacity = map.capacity();

    for (int key : keys) {
        size_t home = mix_hash(hasher(key)) & (capacity - 1);
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

TEST_CASE("HashMapHopscotch ensures keys stay within hop neighborhood", "[hashmap][collision]") {
    constexpr unsigned int hop_length = 4;
    HashMapHopscotch<int, std::string> map(7);

    REQUIRE(map.capacity() == 8);

    for (int i = 0; i < 20; i++) {
        REQUIRE(map.emplace(i, "val" + std::to_string(i)));
    }

    for (int i = 0; i < 20; i++) {
        REQUIRE(is_within_hop_range(map, i, hop_length));
    }
}

TEST_CASE("HashMapHopscotch rehashes correctly when load factor exceeds 0.7", "[hashmap][rehash]") {
    HashMapHopscotch<size_t, size_t> map(4);
    size_t old_capacity = map.capacity();

    for (size_t i = 0; i < 10; i++) {
        map.emplace(i, i * 10);
    }

    REQUIRE(map.size() == 10);
    REQUIRE(map.capacity() > old_capacity);

    for (size_t i = 0; i < 10; i++) {
        auto it = map.find(i);
        REQUIRE(it != map.end());
        REQUIRE(it->second == i * 10);
    }
}

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