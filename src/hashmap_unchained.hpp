#pragma once
#include <cstdint>
#include <vector>
#include <type_traits>
#include <cstring>
#include <stdexcept>
#include <algorithm>

#ifdef __SSE4_2__
#include <nmmintrin.h>
#endif

// HashMapUnchained
// - contiguous adjacency array (tuple_storage)
// - directory: vector<directory_entry> of size buckets+1
//   directory[i] = (start_index << 16) | filter16
// - bucket i contains tuples in [start_index, start_index_of_next_bucket)
// - CRC32-based hash for Key (Key must be trivially copyable)

template <typename Key, typename Value>
class HashMapUnchained {
    static_assert(std::is_trivially_copyable<Key>::value,
        "Key must be trivially copyable for CRC32 hashing.");

public:
    struct Tuple {
        Key key;
        Value value;
    };

private:
    // directory entry: upper 48 bits = start index (tuple index),
    // lower 16 bits = bloom/filter bits
    using directory_entry = uint64_t;

    std::vector<directory_entry> directory;
    std::vector<Tuple> tuples;
    std::vector<uint16_t> tags;     // Tags used to set bloom filters

    size_t tuple_count = 0;  // Total number of tuples in the hash map
    int buckets_log2 = 0;    // Used to set shift (shift = 64 - buckets_log2)
    uint32_t shift = 0;      // Shift amount for directory (directory_index = hash >> shift)

public:
    // Generates the tag table and sets the directory size
    HashMapUnchained(size_t initial_buckets = 16) {
        // Generates a table with tags that are 16-bit numbers with exactly 4 bits set to 1
        generate_tag_table();
        if (initial_buckets == 0) {
            initial_buckets = 1;
        }

        // Find the next power of 2 to set the size of the directory
        size_t p = 1;
        buckets_log2 = 0;
        while (p < initial_buckets) {
            p <<= 1;
            buckets_log2++;
        }
        shift = 64 - buckets_log2;

        init_directory(p);
    }

    // Reserve memory for n tuples
    void reserve(size_t n) {
        tuples.reserve(n);
    }

    // Check if there are no tuples in the map
    bool empty() const noexcept {
        return tuple_count == 0;
    }

    // Return the amount of tuples in the map
    size_t size() const noexcept {
        return tuple_count;
    }

    // Helping function to test reserve()
    size_t tuples_capacity() const noexcept {
        return tuples.capacity();
    }

    // Just place the new tuple in the tuples vector. Process the directory later
    bool emplace(const Key& key, const Value& value) {
        tuples.push_back(Tuple{ key, value });
        tuple_count++;
        return true;
    }

    // Create the directory for the tuples already inserted
    void create_directory() {
        const size_t buckets = bucket_count();
        if (buckets == 0) {
            return;
        }

        // Sort the tuples based on hash (not key)
        std::sort(tuples.begin(), tuples.end(), [](const Tuple& t1, const Tuple& t2) { return compute_hash(t1.key) < compute_hash(t2.key); });

        // Step 1: compute hash for every tuple
        std::vector<uint64_t> hashes(tuple_count);
        for (size_t i = 0; i < tuple_count; i++) {
            hashes[i] = compute_hash(tuples[i].key);
        }

        // Step 2: count tuples per bucket
        std::vector<size_t> count(buckets, 0);
        for (size_t i = 0; i < tuple_count; i++) {
            size_t bucket_idx = bucket_index_from_hash(hashes[i]);
            count[bucket_idx]++;
        }

        // Step 3: prefix sum → start offsets
        std::vector<size_t> start(buckets + 1, 0);
        for (size_t i = 0; i < buckets; i++) {
            start[i + 1] = start[i] + count[i];
        }

        // Step 4: Set filters
        std::vector<uint16_t> filters(buckets, 0);
        for (size_t i = 0; i < tuple_count; i++) {
            size_t bucket_idx = bucket_index_from_hash(hashes[i]);
            uint16_t slot = uint32_t(hashes[i]) >> (32 - 11);
            uint16_t tag = tags[slot];
            filters[bucket_idx] |= tag;
        }

        // Step 5: write directory
        for (size_t bucket_idx = 0; bucket_idx < buckets; bucket_idx++) {
            directory[bucket_idx] = pack_entry(start[bucket_idx], filters[bucket_idx]);
        }

        directory[buckets] = pack_entry(start[buckets], 0);
    }

    // Lookup range in which a key could be in
    std::pair<size_t, size_t> lookup_range(const Key& key) const {
        if (tuple_count == 0) {
            return { 0, 0 };
        }

        uint64_t hash = compute_hash(key);
        size_t bucket_idx = bucket_index_from_hash(hash);

        if (!could_contain((uint16_t)(directory[bucket_idx]), hash)) {
            return { 0, 0 };
        }

        return bucket_range(bucket_idx);
    }

    // Return the tuple in position 'idx'
    const Tuple& storage_at(size_t idx) const {
        if (idx >= tuple_count) {
            throw std::runtime_error("Out of bounds access");
        }
        return tuples[idx];
    }

    // Useful operator for testing
    template<typename Index>
    Tuple operator[](Index index) {
        size_t i = static_cast<size_t>(index);
        if (i >= tuple_count) {
            throw std::runtime_error("Out of bounds access");
        }

        return tuples[i];
    }

    // Return the size of the directory
    size_t bucket_count() const {
        return directory.size() == 0 ? 0 : directory.size() - 1;
    }

private:
    //-------------------------
    // Directory entry helpers
    //-------------------------

    // Creates a directory entry (upper 48 bits = start index, lower 16 = bloom filter)
    static directory_entry pack_entry(uint64_t start, uint16_t filter) {
        return (start << 16) | filter;
    }

    // Return the index part of a directory entry
    static uint64_t unpack_start(directory_entry entry) {
        return entry >> 16;
    }

    // Return the bloom filter part of a directory entry
    static uint16_t unpack_filter(directory_entry entry) {
        return entry & 0xFFFF;
    }

    //-------------------------

    // Check if a key can be in the map based on the filter
    bool could_contain(uint16_t entry, uint64_t hash) const {
        uint16_t slot = ((uint32_t)hash) >> (32 - 11); // shr
        uint16_t tag = tags[slot]; // mov
        return !(tag & ~entry); // andn
    }

    // Compute bucket index
    inline size_t bucket_index_from_hash(uint64_t hash) const {
        if (buckets_log2 == 0) {
            return 0;
        }

        return static_cast<size_t>(hash >> shift);
    }

    // Directory range
    std::pair<size_t, size_t> bucket_range(size_t slot) const {
        uint64_t start = unpack_start(directory[slot]);
        uint64_t end = unpack_start(directory[slot + 1]);
        return { size_t(start), size_t(end) };
    }

    // Generate tag table
    void generate_tag_table() {
        tags.reserve(2048);

        // C(16,4) = 1820 tags
        for (int a = 0; a < 16; a++) {
            for (int b = a + 1; b < 16; b++) {
                for (int c = b + 1; b < 16; b++) {
                    for (int d = c + 1; d < 16; d++) {
                        uint16_t mask = (1 << a) | (1 << b) | (1 << c) | (1 << d);
                        tags.push_back(mask);
                    }
                }
            }
        }

        // Fill the table with tags
        while (tags.size() < 2048) {
            tags.push_back(tags[tags.size() % 1820]);
        }
    }

    // Hash function: hardware CRC if available
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
    }

    void init_directory(size_t buckets) {
        directory.assign(buckets + 1, pack_entry(0, 0));
    }
};