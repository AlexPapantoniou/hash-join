#pragma once

#include <functional>
#include <utility>
#include <algorithm>
#include <iterator>
#include <cstddef>
#include <optional>
#include <vector>
#include <memory>

template<typename Key>
struct CuckooHashers {
    std::hash<Key> hasher;

    size_t h1(const Key& key) const noexcept {
        return hasher(key);
    }

    size_t h2(const Key& key) const noexcept {
        size_t x = hasher(key);
        x ^= (x >> 33);
        x *= 0xff51afd7ed558ccdULL;
        x ^= (x >> 33);
        x *= 0xc4ceb9fe1a85ec53ULL;
        x ^= (x >> 33);
        return x;
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
    size_t _size = 0;
    CuckooHashers<Key> hashers;

    inline size_t mask() const {
        return _capacity - 1;
    }

    static size_t next_power_of_two(size_t n) {
        size_t p = 1;
        while (p < n) p <<= 1;
        return p;
    }

    void rehash() {
        std::vector<std::optional<Node>> old_buckets1 = std::move(buckets1);
        std::vector<std::optional<Node>> old_buckets2 = std::move(buckets2);
        size_t old_capacity = _capacity;

        _capacity <<= 1;
        _size = 0;
        buckets1.assign(_capacity, std::nullopt);
        buckets2.assign(_capacity, std::nullopt);

        for (size_t i = 0; i < old_capacity; i++) {
            if (old_buckets1[i].has_value()) {
                emplace(old_buckets1[i]->key, old_buckets1[i]->value);
            }
            if (old_buckets2[i].has_value()) {
                emplace(old_buckets2[i]->key, old_buckets2[i]->value);
            }
        }
    }

public:
    class iterator {
        friend class HashMapCuckoo;
        HashMapCuckoo* map;
        size_t index;

        void advance_to_valid() {
            if (!map) {
                view.reset();
                return;
            }
            while (index < map->_capacity && (!map->buckets1[index].has_value() && !map->buckets2[index].has_value())) {
                index++;
            }
            if (index < map->_capacity && map->buckets1[index].has_value()) {
                view = std::make_unique<PairView>(
                    map->buckets1[index]->key,
                    map->buckets1[index]->value
                );
            }
            else if (index < map->_capacity && map->buckets2[index].has_value()) {
                view = std::make_unique<PairView>(
                    map->buckets2[index]->key,
                    map->buckets2[index]->value
                );
            }
            else {
                view.reset();
                return;
            }
        }

        iterator(HashMapCuckoo* m, size_t start)
            : map(m), index(start) {
            advance_to_valid();
        }

        struct PairView {
            const Key& first;
            Value& second;
            PairView(const Key& f, Value& s) : first(f), second(s) {}
        };

        mutable std::unique_ptr<PairView> view;

    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = std::pair<const Key, Value>;
        using difference_type = std::ptrdiff_t;
        using pointer = PairView*;
        using reference = PairView&;

        iterator()
            : map(nullptr), index(0) {
        }

        reference operator*() const {
            return *view;
        }

        pointer operator->() const {
            return &*view;
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

    HashMapCuckoo(size_t initial_capacity = 16)
        : _capacity(std::max<size_t>(1, next_power_of_two(initial_capacity))),
        buckets1(_capacity),
        buckets2(_capacity),
        _size(0) {
    }

    ~HashMapCuckoo() = default;

    size_t size() const {
        return _size;
    }

    size_t capacity() const {
        return _capacity;
    }

    bool empty() const {
        return _size == 0;
    }

    bool emplace(const Key& key, const Value& value) {
        if (static_cast<double>(_size) / static_cast<double>(_capacity) >= 0.7) {
            rehash();
        }

        size_t index;
        bool in_buckets1 = true;
        size_t loop_count = 0;

        Key cur_key = key;
        Value cur_value = value;

        while (loop_count < _capacity) {
            if (in_buckets1) {
                index = hashers.h1(cur_key) & mask();
                if (!buckets1[index].has_value()) {
                    buckets1[index].emplace(Node{ std::move(cur_key), std::move(cur_value) });
                    _size++;
                    return true;
                }
                Node old_node = std::move(*buckets1[index]);
                buckets1[index].emplace(Node{ std::move(cur_key), std::move(cur_value) });
                cur_key = std::move(old_node.key);
                cur_value = std::move(old_node.value);
            }
            else {
                index = hashers.h2(cur_key) & mask();
                if (!buckets2[index].has_value()) {
                    buckets2[index].emplace(Node{ std::move(cur_key), std::move(cur_value) });
                    _size++;
                    return true;
                }
                Node old_node = std::move(*buckets2[index]);
                buckets2[index].emplace(Node{ std::move(cur_key), std::move(cur_value) });
                cur_key = std::move(old_node.key);
                cur_value = std::move(old_node.value);
            }
            in_buckets1 = !in_buckets1;
            loop_count++;
        }

        rehash();
        return emplace(std::move(cur_key), std::move(cur_value));
    }

    iterator find(const Key& key) {
        size_t index1 = hashers.h1(key) & mask();
        if (buckets1[index1].has_value() && buckets1[index1]->key == key) {
            return iterator(this, index1);
        }
        size_t index2 = hashers.h2(key) & mask();
        if (buckets2[index2].has_value() && buckets2[index2]->key == key) {
            return iterator(this, index2);
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
        if (i > _capacity) {
            return end();
        }

        if (buckets1[i].has_value() || buckets2[i].has_value()) {
            return iterator(this, i);
        }

        return end();
    }

};