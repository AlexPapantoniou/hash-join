#include <functional>
#include <utility>
#include <algorithm>
#include <iterator>
#include <cstddef>
#include <optional>
#include <vector>
#include <memory>
#include <iostream>

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
	std::vector<std::uint16_t> hop_info;
    size_t _size;
    std::hash<Key> hasher;

    inline size_t mask() const noexcept{
        return _capacity - 1;
    }

    static size_t next_power_of_two(size_t n) noexcept{
        size_t p = 1;
        while (p < n) p <<= 1;
        return p;
    }

    void rehash() {
        std::vector<std::optional<Node>> old_buckets = std::move(buckets);
        size_t old_capacity = _capacity;

        _capacity <<= 1;
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

    static size_t mix_hash(size_t h) noexcept {
        h ^= (h >> 33);
        h *= 0xff51afd7ed558ccdULL;
        h ^= (h >> 33);
        h *= 0xc4ceb9fe1a85ec53ULL;
        h ^= (h >> 33);
        return h;
    }

public:
    class iterator {
        friend class HashMapHopscotch;
        HashMapHopscotch* map;
        size_t index;

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
        using value_type = std::pair<const Key, Value>;
        using difference_type = std::ptrdiff_t;
        using pointer = value_type*;
        using reference = value_type&;


        iterator()
            : map(nullptr),
            index(0) {
        }

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
            (*this)++;
            return temp;
        }

        bool operator==(const iterator& other) const {
            return map == other.map && index == other.index;
        }

        bool operator!=(const iterator& other) const {
            return !(*this == other);
        }
    };

    HashMapHopscotch(size_t initial_capacity = 16, unsigned int neighb_init = 4)
        : _capacity(next_power_of_two(std::max<size_t>(1, initial_capacity))),
        buckets(_capacity),
        neighbours(neighb_init),
        hop_info(_capacity, 0),
        _size(0) {
    }

    ~HashMapHopscotch() = default;

    size_t size() const noexcept{
        return _size;
    }

    size_t capacity() const noexcept{
        return _capacity;
    }

    bool empty() const noexcept{
        return _size == 0;
    }

    unsigned int neighbours_size() const noexcept{
        return neighbours;
    }

    void reserve(size_t expected) {
        _capacity = next_power_of_two(static_cast<size_t>(expected * 1.5));
        buckets.assign(_capacity, std::nullopt);
        hop_info.assign(_capacity, 0);
    }

    bool emplace(const Key& key, const Value& value) {
        // std::cout << "CAPACITY" << _capacity << std::endl;
        // std::cout << "NEIGHBOURS" << neighbours << std::endl;
        
        if ((_size + 1) * 4 >= _capacity * 3) {
            rehash();
        }

        size_t hash = mix_hash(hasher(key));
        const size_t cap_mask = mask();
        size_t index = hash & cap_mask;

        if (hop_info[index] == ((1u << neighbours)) - 1) {
            rehash();
            return emplace(key, value);
        }

        size_t j = index;
        while (buckets[j].has_value()) {
            j = (j + 1) & cap_mask;
        }

        while (((j - index) & cap_mask) >= neighbours) {
            bool moved = false;

            for (size_t offset = neighbours - 1; offset > 0; offset--) {
                size_t k = (j - offset) & cap_mask;

                uint16_t hop = hop_info[k];
                for (size_t bit = 0; bit < neighbours; bit++) {
                    if (hop & (1u << bit)) {
                        size_t y_index = (k + bit) & cap_mask;
                        if (((j - y_index) & cap_mask) < neighbours) {
                            buckets[j] = buckets[y_index];
                            hop_info[k] &= ~(1u << bit);
                            hop_info[k] |= (1u << ((j - k) & cap_mask));
                            buckets[y_index].reset();
                            j = y_index;
                            moved = true;
                            break;
                        }
                    }
                }

                if (moved) break;
            }

            if (!moved) {
                rehash();
                return emplace(key, value);
            }
        }

        buckets[j].emplace(Node{ std::move(key), std::move(value) });
        hop_info[index] |= (1u << ((j - index) & cap_mask));
        _size++;
        return true;
    }

    iterator find(const Key& key) {
        size_t hash = mix_hash(hasher(key));
        const size_t cap_mask = mask();
        size_t index = hash & cap_mask;
        uint16_t hop = hop_info[index];

        for (size_t bit = 0; bit < neighbours; bit++) {
            if (hop & (1u << bit)) {
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