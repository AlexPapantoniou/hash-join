#pragma once
#include <optional>
#include <vector>

template<typename Key, typename Value>
class HashMapHopscotch {
private:
    struct Node {
        Key key;
        Value value;
    };

    size_t _capacity;
    std::vector<std::optional<Node>> buckets;
    unsigned int neighbours;
    std::vector<std::uint64_t> hop_info;    // Vector to store the hop information
    size_t _size;
    std::hash<Key> hasher;

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

    // Dynamically set the neighborhood size depending on the capacity
    static unsigned int compute_neighborhood(size_t cap) noexcept {
        if (cap < 2048) return 4;
        if (cap < 1 << 14) return 8;       // < 16K buckets
        if (cap < 1 << 18) return 16;      // < 256K buckets
        if (cap < 1 << 22) return 32;      // < 4M buckets
        return 64;                              // cap at 64
    }

    void rehash() {
        std::vector<std::optional<Node>> old_buckets = std::move(buckets);
        size_t old_capacity = _capacity;

        _capacity <<= 1;
        neighbours = compute_neighborhood(_capacity);
        _size = 0;
        buckets.assign(_capacity, std::nullopt);
        hop_info.assign(_capacity, 0);

        for (size_t i = 0; i < old_capacity; i++) {
            auto& bucket = old_buckets[i];
            if (bucket.has_value()) {
                emplace(std::move(bucket->key), std::move(bucket->value));
            }
        }
    }

    // Mix the hash value to improve distribution
    size_t mix_hash(size_t i) noexcept {
        i += 1ull;
        i ^= i >> 33ull;
        i *= 0xff51afd7ed558ccdull;
        i ^= i >> 33ull;
        i *= 0xc4ceb9fe1a85ec53ull;
        i ^= i >> 33ull;
        return i;
    }

public:
    // Helper iterator class for similar functionality as std::unordered_map
    class iterator {
        friend class HashMapHopscotch;
        HashMapHopscotch* map;
        size_t index;

        // Find the first bucket with a value
        void advance_to_valid() {
            while (map && index < map->_capacity && !map->buckets[index].has_value()) {
                index++;
            }
        }

        iterator(HashMapHopscotch* m, size_t start)
            : map(m), index(start) {
            advance_to_valid();
        }


    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = std::pair<const Key, Value>;     // "Find" return value is a pair of Key-Value
        using difference_type = std::ptrdiff_t;
        using pointer = value_type*;
        using reference = value_type&;


        iterator()
            : map(nullptr),
            index(0) {
        }

        //--------------------------------------------------
        // Useful operators for iterator functionality
        reference operator*() const {
            auto& node = *map->buckets[index];
            return *reinterpret_cast<value_type*>(&node);
        }

        pointer operator->() const {
            auto& node = *map->buckets[index];
            return reinterpret_cast<value_type*>(&node);
        }

        iterator& operator++() {
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
            return map == other.map && index == other.index;
        }

        bool operator!=(const iterator& other) const {
            return !(*this == other);
        }
        //------------------------------------------------------
    };

    HashMapHopscotch(size_t initial_capacity = 16)
        : _capacity(next_power_of_two(std::max<size_t>(1, initial_capacity))),  // Capacity is always a power of 2 so that mask() works
        buckets(_capacity),
        neighbours(compute_neighborhood(_capacity)),
        hop_info(_capacity, 0),
        _size(0) {
    }

    ~HashMapHopscotch() = default;

    size_t size() const noexcept {
        return _size;
    }

    size_t capacity() const noexcept {
        return _capacity;
    }

    bool empty() const noexcept {
        return _size == 0;
    }

    unsigned int neighbours_size() const noexcept {
        return neighbours;
    }

    // Reserve space for expected number of elements
    void reserve(size_t expected) {
        _capacity = next_power_of_two(static_cast<size_t>(expected * 1.5));
        neighbours = compute_neighborhood(_capacity);
        buckets.assign(_capacity, std::nullopt);
        hop_info.assign(_capacity, 0);
    }

    // Insert a new Key-Value pair
    bool emplace(const Key& key, const Value& value) {
        // If load factor exceeds 75%, rehash
        if ((_size + 1) * 4 >= _capacity * 3) {
            rehash();
        }

        const size_t cap_mask = mask();
        size_t index = mix_hash(hasher(key)) & cap_mask;

        // Check if the neighborhood is full
        uint64_t neighborhood_mask = (neighbours >= 64) ? UINT64_MAX : ((1ull << neighbours) - 1);
        if (hop_info[index] == neighborhood_mask) {
            rehash();
            return emplace(key, value);
        }

        // Find the first free bucket
        size_t j = index;   // j will be the index of the bucket to insert the new key into
        while (buckets[j].has_value()) {
            j = (j + 1) & cap_mask;
        }

        // Try to bring the free bucket within the neighborhood
        while (((j - index) & cap_mask) >= neighbours) {
            bool moved = false;
            // Try to find a bucket within the neighborhood to move
            for (size_t offset = neighbours - 1; offset > 0; offset--) {
                size_t k = (j - offset) & cap_mask;
                uint64_t hop = hop_info[k];
                // Check each neighbor through the bit map
                for (size_t bit = 0; bit < neighbours; bit++) {
                    if (hop & (1ull << bit)) {
                        size_t y_index = (k + bit) & cap_mask;
                        // If moving the neighbor keeps it in its neighborhood, move it
                        if (((j - y_index) & cap_mask) < neighbours) {
                            buckets[j] = buckets[y_index];
                            hop_info[k] &= ~(1ull << bit);  // Remove old hop info bit
                            hop_info[k] |= (1ull << ((j - k) & cap_mask));  // Set new hop info bit
                            buckets[y_index].reset();
                            j = y_index;    // Update j to the moved bucket's position
                            moved = true;
                            break;
                        }
                    }
                }

                if (moved) break;   // If we moved a bucket, break out of the loop
            }

            // If we couldn't move any bucket, we need to rehash
            if (!moved) {
                rehash();
                return emplace(key, value);
            }
        }

        // Insert the new Key-Value pair
        buckets[j].emplace(Node{ std::move(key), std::move(value) });
        hop_info[index] |= (1ull << ((j - index) & cap_mask));
        _size++;
        return true;
    }

    iterator find(const Key& key) {
        const size_t cap_mask = mask();
        size_t index = mix_hash(hasher(key)) & cap_mask;
        uint64_t hop = hop_info[index];

        // Check each neighbor in the hop information
        for (size_t bit = 0; bit < neighbours; bit++) {
            if (hop & (1ull << bit)) {
                size_t j = (index + bit) & cap_mask;
                auto& bucket = buckets[j];
                if (bucket.has_value() && bucket->key == key) {
                    return iterator(this, j);
                }
            }
        }

        return end();
    }

    iterator begin() {
        return iterator(this, 0);
    }

    iterator end() {
        return iterator(this, _capacity);
    }

    // Useful operator for testing
    template<typename Index>
    iterator operator[](Index index) {
        size_t i = static_cast<size_t>(index);
        if (i >= _capacity) {
            return end();
        }

        if (buckets[i].has_value()) {
            return iterator(this, i);
        }

        return end();
    }
};