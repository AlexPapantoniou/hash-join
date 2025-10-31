#include <functional>
#include <utility>
#include <algorithm>
#include <iterator>
#include <cstddef>
#include <optional>
#include <vector>
#include <memory>

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

    inline size_t mask() {
        return _capacity - 1;
    }

    static size_t next_power_of_two(size_t n) {
        size_t p = 1;
        while (p < n) p <<= 1;
        return p;
    }

    void rehash() {
        std::vector<std::optional<Node>> old_buckets = std::move(buckets);
        std::vector<std::uint16_t> old_hop_info = std::move(hop_info);
        size_t old_capacity = _capacity;

        _capacity <<= 1;
        _size = 0;
        buckets.assign(_capacity, std::nullopt);
        hop_info.assign(_capacity, 0);

        for (size_t i = 0; i < old_capacity; i++) {
            if (old_buckets[i].has_value()) {
                emplace(old_buckets[i]->key, old_buckets[i]->value);
            }
        }
    }

public:
    class iterator {
        friend class HashMapHopscotch;
        HashMapHopscotch* map;
        size_t index;

        void advance_to_valid() {
            if (!map) {
                view.reset();
                return;
            }
            while (index < map->_capacity && !map->buckets[index].has_value()) index++;
            if (index < map->_capacity && map->buckets[index].has_value()) {
                view = std::make_unique<PairView>(
                    map->buckets[index]->key,
                    map->buckets[index]->value);
            }
            else {
                view.reset();
            }
        }

        iterator(HashMapHopscotch* m, size_t start)
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
            : map(nullptr),
            index(0) {
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

    HashMapHopscotch(size_t initial_capacity = 16, unsigned int neighb_init = 4)
        : _capacity(next_power_of_two(std::max<size_t>(1, initial_capacity))),
        buckets(_capacity),
        neighbours(neighb_init),
        hop_info(_capacity, 0),
        _size(0){
    }

    ~HashMapHopscotch() = default;

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
        if (_size >= _capacity / 2) {
            rehash();
        }

        size_t index = hasher(key) & mask();

        if (hop_info[index] == (1u << neighbours) - 1) {
            rehash();
            return emplace(key, value);
        }

        size_t j = index;
        while (buckets[j].has_value()) {
            j = (j + 1) & mask();
        }

        while (((j - index) & mask()) >= neighbours) {
            bool moved = false;

            for (size_t offset = neighbours - 1; offset > 0; offset--) {
                size_t k = (j - offset) & mask();

                uint16_t hop = hop_info[k];
                for (size_t bit = 0; bit < neighbours; bit++) {
                    if (hop & (1u << bit)) {
                        size_t y_index = (k + bit) & mask();
                        if (((j - y_index) & mask()) < neighbours) {
                            buckets[j] = buckets[y_index];
                            hop_info[k] &= ~(1u << bit);
                            hop_info[k] |= (1u << ((j - k) & mask()));
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
        hop_info[index] |= (1u << ((j - index) & mask()));
        _size++;
        return true;
    }

    iterator find(const Key& key) {
        size_t index = hasher(key) & mask();
        uint16_t hop = hop_info[index];

        for (size_t bit = 0; bit < neighbours; bit++) {
            if (hop & (1u << bit)) {
                size_t j = (index + bit) & mask();
                if (buckets[j].has_value() && buckets[j]->key == key) {
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