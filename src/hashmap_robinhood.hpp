#include "hashmap.hpp"

template<typename Key, typename Value>
class HashMapRobinhood : public HashMap<Key, Value> {
private:
    struct Node {
        Key key;
        std::vector<Value> values;
        unsigned int PSL;
    };
    size_t capacity;
    std::vector<std::optional<Node>> buckets;
    size_t _size;
    unsigned int max_PSL;
    std::hash<Key> hasher;

    inline size_t mask() {
        return capacity - 1;
    }

    static size_t next_power_of_two(size_t n) {
        size_t p = 1;
        while (p < n) p <<= 1;
        return p;
    }

    void rehash() {
        std::vector<std::optional<Node>> old_buckets = std::move(buckets);
        size_t old_capacity = capacity;

        capacity <<= 2;
        max_PSL = 0;
        buckets.assign(capacity, std::nullopt);
        _size = 0;

        for (std::size_t i = 0; i < old_capacity; i++) {
            if (old_buckets[i].has_value()) {
                Key k = old_buckets[i]->key;
                for (const Value& val : old_buckets[i]->values) {
                    insert(k, val);
                }
            }
        }
    }

    bool update_key_values(const Key& key, const Value& value) {
        size_t index = hasher(key) & mask();
        unsigned int PSL = 0;

        while (PSL <= maxPSL) {
            size_t i = (index + PSL) & mask();
            if (buckets[i].has_value() && buckets[i]->key == key) {
                buckets[i]->values.push_back(value);
                return true;
            }

            PSL++;
        }

        return false;
    }

public:
    HashMapRobinhood(size_t initial_capacity = 16)
        : capacity(next_power_of_two(original_capacity)),
        buckets(capacity),
        _size(0),
        max_PSL(0) {
    }

    ~HashMapRobinhood() override = default;

    size_t size() const override {
        return _size;
    }

    bool empty() const override {
        return _size == 0;
    }

    bool insert(const Key& key, const Value& value) override {
        if (update_key_values(key, value)) {
            return true;
        }

        if (_size >= capacity / 2) {
            rehash();
        }

        size_t index = hasher(key) & mask();
        unsigned int PSL = 0;

        while (true) {
            size_t i = (index + PSL) & mask();
            if (!buckets[i].has_value()) {
                buckets[i] = Node{ key, std::vector<Value>{value}, PSL };
                _size++;
                max_PSL = PSL > maxPSL ? PSL : max_PSL;
                return true;
            }
            else if (PSL > buckets[i]->PSL) {
                Key old_key = buckets[i]->key;
                std::vector<Value> old_values = std::move(buckets[i]->values);
                buckets[i].emplace(Node{ key, std::vector<Value>{value}, PSL });
                max_PSL = PSL > maxPSL ? PSL : max_PSL;
                for (const Value& old_value : old_values) {
                    insert(old_key, old_value);
                }
                return true;
            }

            PSL++;
        }

        return false;
    }

    std::optional<std::pair<Key, std::vector<Value>>> find(const Key& key, const Value& value) override {
        size_t index = hasher(key) & mask();
        unsigned int PSL = 0;

        while (PSL <= max_PSL) {
            size_t i = (index + PSL) & mask();
            if (buckets[i].has_value() && buckets[i]->key == key) {
                return std::make_optional(std::make_pair(buckets[i]->key, buckets[i]->values));
            }

            PSL++;
        }

        return std::nullopt;
    }


};