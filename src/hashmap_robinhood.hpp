#pragma once

#include <functional>
#include <utility>
#include <algorithm>
#include <iterator>
#include <cstddef>
#include <optional>
#include <vector>
#include <memory>
#include <type_traits>

template<typename Key, typename Value>
class HashMapRobinhood {
private:
    struct Node {
        Key key;
        Value value;
        unsigned int PSL;
    };

    size_t _capacity;
    std::vector<std::optional<Node>> buckets;
    size_t _size;
    unsigned int max_PSL;
    std::hash<Key> hasher;

    inline size_t mask() const noexcept {
        return _capacity - 1;
    }

    static size_t next_power_of_two(size_t n) noexcept {
        size_t p = 1;
        while (p < n) p <<= 1;
        return p;
    }

    static size_t mix_hash(size_t h) noexcept {
        h ^= (h >> 33);
        h *= 0xff51afd7ed558ccdULL;
        h ^= (h >> 33);
        h *= 0xc4ceb9fe1a85ec53ULL;
        h ^= (h >> 33);
        return h;
    }

    void rehash() {
        std::vector<std::optional<Node>> old_buckets = std::move(buckets);
        size_t old_capacity = _capacity;

        _capacity <<= 1;
        _size = 0;
        max_PSL = 0;
        buckets.clear();
        buckets.resize(_capacity);

        for (std::size_t i = 0; i < old_capacity; i++) {
            auto& bucket = old_buckets[i];

            if (bucket.has_value()) {
                emplace(std::move(bucket->key), std::move(bucket->value));
            }
        }
    }

public:
    class iterator {
        friend class HashMapRobinhood;
        HashMapRobinhood* map;
        size_t index;

        void advance_to_valid() {
            while (map && index < map->_capacity && !map->buckets[index].has_value()) {
                index++;
            }
        }

        iterator(HashMapRobinhood* m, size_t start)
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
            : map(nullptr), index(0) {
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

    HashMapRobinhood(size_t initial_capacity = 16)
        : _capacity(next_power_of_two(std::max<size_t>(1, initial_capacity))),
        buckets(_capacity),
        _size(0),
        max_PSL(0) {
    }

    ~HashMapRobinhood() = default;

    size_t size() const noexcept {
        return _size;
    }

    size_t capacity() const noexcept {
        return _capacity;
    }

    bool empty() const noexcept {
        return _size == 0;
    }

    void reserve(size_t expected) {
        _capacity = next_power_of_two(static_cast<size_t>(expected * 1.5));
        buckets.assign(_capacity, std::nullopt);
    }

    bool emplace(const Key& key, const Value& value) {
        if ((_size + 1) * 4 >= _capacity * 3) {
            rehash();
        }

        size_t hash = mix_hash(hasher(key));
        const size_t cap_mask = mask();
        size_t index = hash & cap_mask;
        unsigned int PSL = 0;

        Key cur_key = key;
        Value cur_value = value;

        while (true) {
            size_t i = (index + PSL) & cap_mask;
            auto& bucket = buckets[i];

            if (!bucket.has_value()) {
                bucket.emplace(Node{ std::move(cur_key), std::move(cur_value), PSL });
                _size++;
                max_PSL = std::max(PSL, max_PSL);
                return true;
            }
            else if (PSL > bucket->PSL) {
                Node old_node = std::move(*bucket);
                bucket.emplace(Node{ std::move(cur_key), std::move(cur_value), PSL });
                cur_key = std::move(old_node.key);
                cur_value = std::move(old_node.value);
                PSL = old_node.PSL + 1;
                max_PSL = std::max(PSL, max_PSL);
            }
            else {
                PSL++;
            }
        }

        return false;
    }

    bool emplace(Key&& key, Value&& value) {
        if ((_size + 1) * 4 >= _capacity * 3) {
            rehash();
        }

        size_t hash = mix_hash(hasher(key));
        const size_t cap_mask = mask();
        size_t index = hash & cap_mask;
        unsigned int PSL = 0;

        Key cur_key = std::move(key);
        Value cur_value = std::move(value);

        while (true) {
            size_t i = (index + PSL) & cap_mask;
            auto& bucket = buckets[i];

            if (!bucket.has_value()) {
                bucket.emplace(Node{ std::move(cur_key), std::move(cur_value), PSL });
                _size++;
                max_PSL = std::max(PSL, max_PSL);
                return true;
            }
            else if (PSL > bucket->PSL) {
                Node old_node = std::move(*bucket);
                bucket.emplace(Node{ std::move(cur_key), std::move(cur_value), PSL });
                cur_key = std::move(old_node.key);
                cur_value = std::move(old_node.value);
                PSL = old_node.PSL + 1;
                max_PSL = std::max(PSL, max_PSL);
            }
            else {
                PSL++;
            }
        }

        return false;
    }

    iterator find(const Key& key) {
        size_t hash = mix_hash(hasher(key));
        const size_t cap_mask = mask();
        size_t index = hash & cap_mask;
        unsigned int PSL = 0;


        while (PSL <= max_PSL) {
            size_t i = (index + PSL) & cap_mask;
            const auto& bucket = buckets[i];

            if (!bucket.has_value()) {
                return end();
            }
            if (bucket->key == key) {
                return iterator(this, i);
            }
            if (bucket->PSL < PSL) {
                return end();
            }

            PSL++;
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