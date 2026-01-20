#include <catch2/catch_test_macros.hpp>
#include <string>
#include <algorithm>

#include "../src/hashmap_unchained.hpp"

TEST_CASE("HashMapUnchained basic insertion and lookup", "[hashmap]") {
    HashMapUnchained<int32_t, size_t> map;

    REQUIRE(map.empty());
    REQUIRE(map.bucket_count() == 16);

    map.reserve(3);
    REQUIRE(map.tuples_capacity() == 3);

    // Raw insert: (key, row_index)
    REQUIRE(map.emplace(10, 0));
    REQUIRE(map.emplace(20, 1));
    REQUIRE(map.emplace(30, 2));

    REQUIRE(map.size() == 3);

    // Finalize
    map.create_directory();

    // Lookup existing keys
    auto r10 = map.lookup_range(10);
    REQUIRE(r10.second - r10.first >= 1);
    bool found10 = false;
    for (size_t idx = r10.first; idx < r10.second; idx++) {
        if (map[idx].key == 10 && map[idx].value == 0) {
            found10 = true;
            break;
        }
    }
    REQUIRE(found10);

    auto r20 = map.lookup_range(20);
    REQUIRE(r20.second - r20.first >= 1);
    bool found20 = false;
    for (size_t idx = r20.first; idx < r20.second; idx++) {
        if (map[idx].key == 20 && map[idx].value == 1) {
            found20 = true;
            break;
        }
    }
    REQUIRE(found20);

    // Lookup non-existing
    auto r40 = map.lookup_range(40);
    REQUIRE(r40.first == r40.second); // empty
}

TEST_CASE("HashMapUnchained handles duplicate keys", "[hashmap][duplicate]") {
    HashMapUnchained<int32_t, size_t> map(7);

    REQUIRE(map.bucket_count() == 8);

    // Insert duplicates for key 5
    REQUIRE(map.emplace(5, 0));
    REQUIRE(map.emplace(5, 3));
    REQUIRE(map.emplace(5, 7));
    REQUIRE(map.emplace(10, 1));
    REQUIRE(map.emplace(10, 2));

    map.create_directory();

    auto r5 = map.lookup_range(5);
    REQUIRE(r5.second - r5.first == 3);

    std::vector<size_t> rows5;
    for (size_t i = r5.first; i < r5.second; i++) {
        rows5.push_back(map[i].value);
    }

    std::sort(rows5.begin(), rows5.end());
    REQUIRE(rows5 == std::vector<size_t>{0, 3, 7});

    auto r10 = map.lookup_range(10);
    REQUIRE(r10.second - r10.first == 2);

    std::vector<size_t> rows10;
    for (size_t i = r10.first; i < r10.second; i++) {
        rows10.push_back(map[i].value);
    }

    std::sort(rows10.begin(), rows10.end());
    REQUIRE(rows10 == std::vector<size_t>{1, 2});
}

TEST_CASE("HashMapUnchained Bloom filter behavior", "[hashmap][bloom]") {
    HashMapUnchained<int32_t, size_t> map(16);

    REQUIRE(map.emplace(100, 0));
    REQUIRE(map.emplace(200, 1));
    REQUIRE(map.emplace(300, 2));

    map.create_directory();

    // True positives always must be allowed
    REQUIRE(map.lookup_range(100).second - map.lookup_range(100).first == 1);

    // Non-existing keys usually rejected (but bloom can false positive)
    auto r = map.lookup_range(9999);
    // Correct behavior: false positive OR empty  
    // Never a crash or wrong key match
    for (size_t i = r.first; i < r.second; i++) {
        REQUIRE(map[i].key == 9999);  // if true positive, key must match
    }
}

// Hash function: hardware CRC if available
template<typename Key>
static uint64_t compute_hash(const Key& key) {
    const uint8_t* data = (const uint8_t*)&key;
    uint64_t hash;

#ifdef __SSE4_2__
    if constexpr (sizeof(Key) == 8) {
        uint64_t value;
        memcpy(&value, data, 8);
        hash = _mm_crc32_u64(0, value);
    }
    else {
        uint32_t c = 0;
        for (size_t i = 0; i < sizeof(Key); i++) {
            c = _mm_crc32_u8(c, data[i]);
        }
        hash = ((uint64_t)c << 32) | c;
    }
#else
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < sizeof(Key); i++) {
        c ^= data[i];
        for (int k = 0; k < 8; k++) {
            c = (c >> 1) ^ (0xEDB88320u & -(c & 1));
        }
    }
    c ^= 0xFFFFFFFFu;
    hash = ((uint64_t)c << 32) | c;
#endif
    return hash * 0x2545F4914F6CDD1DULL;
    // constexpr uint64_t FIB64 = 11400714819323198485ULL;
    // using U = std::make_unsigned_t<Key>;
    // uint64_t k = static_cast<uint64_t>(static_cast<U>(key));
    // return k * FIB64;
}

TEST_CASE("HashMapUnchained random stress test", "[hashmap][stress]") {
    HashMapUnchained<int32_t, size_t> map(64);

    std::vector<int32_t> keys;
    for (int i = 0; i < 500; i++) {
        int32_t k = rand() % 20; // lots of duplicates
        keys.push_back(k);
        REQUIRE(map.emplace(k, i));
    }

    map.create_directory();

    // Validate every key bucket contains the correct rows
    for (int32_t k = 0; k < 20; k++) {
        auto r = map.lookup_range(k);
        std::vector<size_t> expected;

        for (size_t i = 0; i < keys.size(); i++) {
            if (keys[i] == k) {
                expected.push_back(i);
            }
        }

        std::vector<size_t> found;
        for (size_t i = r.first; i < r.second; i++) {
            if (map.storage_at(i).key == k) {
                found.push_back(map.storage_at(i).value);
            }
        }

        std::sort(expected.begin(), expected.end());
        std::sort(found.begin(), found.end());
        REQUIRE(found == expected);
    }
}

TEST_CASE("Parallel hashmap functions test", "[hashmap][new_functions]") {
    HashMapUnchained<int32_t, int32_t> map(32);
    using Tuple = HashMapUnchained<int32_t, int32_t>::Tuple;

    constexpr size_t NUM_TUPLES = 50;
    constexpr size_t NUM_PARTITIONS = 32;

    Tuple tuples[NUM_TUPLES];

    // Count how many tuples belong to each partition
    size_t counts[NUM_PARTITIONS] = { 0 };
    for (size_t i = 0; i < NUM_TUPLES; i++) {
        int32_t key = std::rand();
        uint64_t hash = compute_hash(key);
        size_t slot = map.bucket_index_from_hash(hash);
        tuples[i] = { key, key, hash };
        counts[slot]++;
    }

    // Compute prefix sums
    size_t prefix[NUM_PARTITIONS];
    size_t total = 0;
    for (size_t i = 0; i < NUM_PARTITIONS; i++) {
        prefix[i] = total;
        total += counts[i];
    }

    map.resize(NUM_TUPLES);
    std::vector<size_t> write_cursor(NUM_PARTITIONS);

    for (int32_t i = 0; i < NUM_TUPLES; i++) {
        map.count_and_tag(tuples[i]);
    }

    for (size_t p = 0; p < NUM_PARTITIONS; p++) {
        size_t cur = prefix[p];
        size_t k = __builtin_ctzll(map.bucket_count());

        size_t start = (p << k) / NUM_PARTITIONS;
        size_t end = ((p + 1) << k) / NUM_PARTITIONS;

        for (size_t i = start; i < end; i++) {
            size_t bucket_size = map.prefix_sum(i, cur);
            write_cursor[i] = cur;
            cur += bucket_size;
        }
    }

    for (size_t i = 0; i < NUM_TUPLES; i++) {
        size_t slot = map.bucket_index_from_hash(tuples[i].hash);
        size_t pos = write_cursor[slot]++;
        map.insert(tuples[i], pos);
    }

    map.set_tuple_count(NUM_TUPLES);
    map.finalize_directory();

    REQUIRE(map.size() == NUM_TUPLES);
    for (int32_t i = 0; i < NUM_TUPLES; i++) {
        auto r = map.lookup_range(tuples[i].key);
        REQUIRE(r.second - r.first >= 1);
        bool found = false;
        for (size_t idx = r.first; idx < r.second; idx++) {
            if (map[idx].key == tuples[i].key && map[idx].value == tuples[i].value) {
                found = true;
                break;
            }
        }
        REQUIRE(found);
    }
}