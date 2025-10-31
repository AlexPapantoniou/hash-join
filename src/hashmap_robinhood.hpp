#pragma once

#include <functional>
#include <utility>
#include <algorithm>
#include <iterator>
#include <cstddef>
#include <optional>
#include <vector>
#include <memory>

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

    inline size_t mask() const {
        return _capacity - 1;
    }

    static size_t next_power_of_two(size_t n) {
        size_t p = 1;
        while (p < n) p <<= 1;
        return p;
    }

    void rehash() {
        std::vector<std::optional<Node>> old_buckets = std::move(buckets);
        size_t old_capacity = _capacity;

        _capacity <<= 1;
        _size = 0;
        max_PSL = 0;
        buckets.assign(_capacity, std::nullopt);

        for (std::size_t i = 0; i < old_capacity; i++) {
            if (old_buckets[i].has_value()) {
                emplace(old_buckets[i]->key, old_buckets[i]->value);
            }
        }
    }

public:
    class iterator {
        friend class HashMapRobinhood;
        HashMapRobinhood* map;
        size_t index;

        void advance_to_valid() {
            if (!map) {
                view.reset();
                return;
            }
            while (index < map->_capacity && !map->buckets[index].has_value()) {
                index++;
            }
            if (index < map->_capacity && map->buckets[index].has_value()) {
                view = std::make_unique<PairView>(
                    map->buckets[index]->key,
                    map->buckets[index]->value);
            }
            else {
                view.reset();
            }
        }

        iterator(HashMapRobinhood* m, size_t start)
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

    HashMapRobinhood(size_t initial_capacity = 16)
        : _capacity(next_power_of_two(std::max<size_t>(1, initial_capacity))),
        buckets(_capacity),
        _size(0),
        max_PSL(0) {
    }

    ~HashMapRobinhood() = default;

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
        if (static_cast<double>(_size) / static_cast<double>(_capacity) >= 0.5) {
            rehash();
        }

        size_t index = hasher(key) & mask();
        unsigned int PSL = 0;

        Key cur_key = key;
        Value cur_value = value;

        while (true) {
            size_t i = (index + PSL) & mask();
            if (!buckets[i].has_value()) {
                buckets[i].emplace(Node{ std::move(cur_key), std::move(cur_value), PSL });
                _size++;
                max_PSL = std::max(PSL, max_PSL);
                return true;
            }
            else if (PSL > buckets[i]->PSL) {
                Node old_node = std::move(*buckets[i]);
                buckets[i].emplace(Node{ std::move(cur_key), std::move(cur_value), PSL });
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
        size_t index = hasher(key) & mask();
        unsigned int PSL = 0;

        while (PSL <= max_PSL) {
            size_t i = (index + PSL) & mask();
            if (!buckets[i].has_value()) {
                return end();
            }
            if (buckets[i]->key == key) {
                return iterator(this, i);
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