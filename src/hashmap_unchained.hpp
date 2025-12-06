#include <optional>
#include <vector>

template<typename Key, typename Value>
class HashMapUnchained {
private:
    struct Directory {
        uint64_t pointer_and_filter;
    }

    struct Tuple {
        Key key;
        Value value;
    }

    size_t _capacity;
    std::vector<Directory> directories;
    std::vector<Tuple> tuples;
    size_t _size;
    std::hash<Key> hasher;
};