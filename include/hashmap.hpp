#pragma once

#include <optional>
#include <vector>

template<typename Key, typename Value>
class HashMap {
public:
    virtual ~HashMap() = default;
    virtual bool emplace(const Key& key, const Value& value) = 0;
    virtual std::optional<std::pair<Key, Value>> find(const Key& key) = 0;
    virtual size_t size() const = 0;
    virtual size_t capacity() const = 0;
    virtual bool empty() const = 0;
};