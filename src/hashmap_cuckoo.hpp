#pragma once

#include <optional>
#include <vector>

// The 2 hashers for cuckoo hashing
template<typename Key>
struct CuckooHashers {
    std::hash<Key> hasher;

    inline size_t h1(const Key& key) const noexcept {
        return hasher(key);
    }

    // Mix the hash value to improve distribution
    static size_t mix_hash(size_t i) noexcept {
        i += 1ull;
        i ^= i >> 33ull;
        i *= 0xff51afd7ed558ccdull;
        i ^= i >> 33ull;
        i *= 0xc4ceb9fe1a85ec53ull;
        i ^= i >> 33ull;
        return i;
    }

    inline size_t h2(const Key& key) const noexcept {
        return mix_hash(hasher(key));
    }
};

template<typename Key, typename Value>
class HashMapCuckoo {
private:
    struct Node {
        Key key;
        Value value;
    };

    size_t _capacity;
    std::vector<std::optional<Node>> buckets1;
    std::vector<std::optional<Node>> buckets2;
    size_t _size;
    CuckooHashers<Key> hashers;

    // Helper function for faster "%" operations (x % capacity == x & mask())
    inline size_t mask() const noexcept {
        return _capacity - 1;
    }

    // Compute the next power of two (used for setting the capacity)
    static size_t next_power_of_two(size_t n) noexcept {
        size_t p = 1;
        while (p < n) p <<= 1;
        return p;
    }

    void rehash() {
        std::vector<std::optional<Node>> old_buckets1 = std::move(buckets1);
        std::vector<std::optional<Node>> old_buckets2 = std::move(buckets2);
        size_t old_capacity = _capacity;

        // Keep trying to rehash until it's successful
        while (true) {
            _capacity <<= 1;
            buckets1.assign(_capacity, std::nullopt);
            buckets2.assign(_capacity, std::nullopt);

            _size = 0;
            const size_t cap_mask = mask();
            bool all_ok = true;

            // Rehash using the second hash function to avoid recursive rehashes that might result in data loss
            for (size_t i = 0; i < old_capacity; i++) {
                auto& bucket = old_buckets1[i];
                if (bucket.has_value()) {
                    if (!emplace_no_rehash(std::move(bucket->key), std::move(bucket->value), cap_mask)) {
                        all_ok = false;
                        break;
                    }
                }
                bucket = old_buckets2[i];
                if (bucket.has_value()) {
                    if (!emplace_no_rehash(std::move(bucket->key), std::move(bucket->value), cap_mask)) {
                        all_ok = false;
                        break;
                    }
                }
            }

            // Finish the rehash only if all elements were inserted successfully
            if (all_ok) {
                return;
            }
        }
    }

    // Helper for rehash(): insert without triggering another rehash
    bool emplace_no_rehash(Key&& key, Value&& value, size_t cap_mask) {
        bool in_buckets1 = true;
        size_t loop_count = 0;
        constexpr size_t MAX_KICKS = 512;

        while (loop_count < MAX_KICKS) {
            if (in_buckets1) {
                size_t index = hashers.h1(key) & cap_mask;
                auto& bucket = buckets1[index];
                if (!bucket.has_value()) {
                    bucket.emplace(Node{ std::move(key), std::move(value) });
                    _size++;
                    return true;
                }
                Node old_node = std::move(*bucket);
                bucket.emplace(Node{ std::move(key), std::move(value) });
                key = std::move(old_node.key);
                value = std::move(old_node.value);
            }
            else {
                size_t index = hashers.h2(key) & cap_mask;
                auto& bucket = buckets2[index];
                if (!bucket.has_value()) {
                    bucket.emplace(Node{ std::move(key), std::move(value) });
                    _size++;
                    return true;
                }
                Node old_node = std::move(*bucket);
                bucket.emplace(Node{ std::move(key), std::move(value) });
                key = std::move(old_node.key);
                value = std::move(old_node.value);
            }

            in_buckets1 = !in_buckets1;
            loop_count++;
        }

        return false;
    }

public:
    // Helper iterator class for similar functionality as std::unordered_map
    class iterator {
    private:
        friend class HashMapCuckoo;
        HashMapCuckoo* map;
        size_t index;
        bool in_buckets1;

        // Find the first bucket with a value
        void advance_to_valid() {
            if (!map) {
                return;
            }

            while (true) {
                // Start from the first table (if in_buckets1 == true)
                auto& table = in_buckets1 ? map->buckets1 : map->buckets2;
                while (index < map->_capacity && !table[index].has_value()) {
                    index++;
                }
                // If we found a valid bucket return
                if (index < map->_capacity) {
                    return;
                }
                // If we were in the second table and didn't find anything, switch to end() state
                if (!in_buckets1) {
                    map = nullptr;
                    return;
                }
                // Switch to search the second table
                in_buckets1 = false;
                index = 0;
            }
        }

        iterator(HashMapCuckoo* m, size_t start, bool first)
            : map(m), index(start), in_buckets1(first) {
            advance_to_valid();
        }

    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = std::pair<const Key, Value>;     // "Find" return value is a pair of Key-Value
        using difference_type = std::ptrdiff_t;
        using pointer = value_type*;
        using reference = value_type&;

        iterator()
            : map(nullptr), index(0), in_buckets1(true) {
        }

        //-----------------------------------------------------------------------------
        // Useful operators for iterator functionality
        reference operator*() const {
            auto& node = in_buckets1 ? *map->buckets1[index] : *map->buckets2[index];
            return *reinterpret_cast<value_type*>(&node);
        }

        pointer operator->() const {
            auto& node = in_buckets1 ? *map->buckets1[index] : *map->buckets2[index];
            return reinterpret_cast<value_type*>(&node);
        }

        iterator& operator++() {
            if (!map) {
                return *this;
            }
            index++;
            advance_to_valid();
            return *this;
        }

        iterator operator++(int) {
            iterator temp = *this;
            ++(*this);
            return temp;
        }

        bool operator==(const iterator& other) const {
            return map == other.map && index == other.index && in_buckets1 == other.in_buckets1;
        }

        bool operator!=(const iterator& other) const {
            return !(*this == other);
        }
        //-----------------------------------------------------------------------------
    };

public:
    HashMapCuckoo(size_t initial_capacity = 16)
        : _capacity(next_power_of_two(std::max<size_t>(1, initial_capacity))),  // Capacity is always a power of 2 so that mask() works
        buckets1(_capacity),
        buckets2(_capacity),
        _size(0) {
    }

    ~HashMapCuckoo() = default;

    size_t size() const noexcept {
        return _size;
    }

    size_t capacity() const noexcept {
        return _capacity;
    }

    bool empty() const noexcept {
        return _size == 0;
    }

    // Reserve space for expected elements
    void reserve(size_t expected) {
        _capacity = next_power_of_two(std::max<size_t>(1, static_cast<size_t>(expected * 1.5)));
        buckets1.assign(_capacity, std::nullopt);
        buckets2.assign(_capacity, std::nullopt);
    }

    // Insert a new Key-Value pair
    bool emplace(const Key& key, const Value& value) {
        // If load factor exceeds 90%, rehash
        if ((_size + 1) * 10 >= _capacity * 9) {
            rehash();
        }

        Key cur_key = key;
        Value cur_value = value;

        // Try to insert without rehashing
        if (emplace_no_rehash(std::move(cur_key), std::move(cur_value), mask())) {
            return true;
        }

        // If it fails, repeatedly rehash and try again
        while (true) {
            rehash();
            cur_key = key;
            cur_value = value;
            if (emplace_no_rehash(std::move(cur_key), std::move(cur_value), mask())) {
                return true;
            }
        }

        return false;   // Unreachable
    }

    // Move version of emplace to avoid copies for time optimization (used in rehashing)
    bool emplace(Key&& key, Value&& value) {
        if ((_size + 1) * 10 >= _capacity * 9) {
            rehash();
        }

        // Move keys and values to avoid copies
        Key cur_key = std::move(key);
        Value cur_value = std::move(value);

        if (emplace_no_rehash(std::move(cur_key), std::move(cur_value), mask())) {
            return true;
        }

        while (true) {
            rehash();
            cur_key = std::move(key);
            cur_value = std::move(value);
            if (emplace_no_rehash(std::move(cur_key), std::move(cur_value), mask())) {
                return true;
            }
        }
    }

    // Search the only 2 positions in the tables in which the key may be in
    iterator find(const Key& key) {
        const size_t cap_mask = mask();
        size_t index1 = hashers.h1(key) & cap_mask;
        if (buckets1[index1].has_value() && buckets1[index1]->key == key) {
            return iterator(this, index1, true);
        }
        size_t index2 = hashers.h2(key) & cap_mask;
        if (buckets2[index2].has_value() && buckets2[index2]->key == key) {
            return iterator(this, index2, false);
        }

        return end();
    }

    iterator begin() {
        return iterator(nullptr, 0, true);
    }

    iterator end() {
        return iterator(nullptr, _capacity, true);
    }

    // Useful operator for testing
    template<typename Index>
    iterator operator[](Index index) {
        size_t i = static_cast<size_t>(index);
        if (i > _capacity) {
            return end();
        }

        if (buckets1[i].has_value()) {
            return iterator(this, i, true);
        }
        if (buckets2[i].has_value()) {
            return iterator(this, i, false);
        }

        return end();
    }

};