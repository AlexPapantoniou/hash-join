#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <cstring>
#include <stdexcept>
#include <algorithm>
#include <cassert>

#include "../src/hashmap_unchained.hpp"

#define NumPartitions 16

namespace SlabAllocator {

    struct Level1Slab {
        static constexpr size_t LARGE_CHUNK_SIZE = 4 * 1024 * 1024;  // 4 MB

        struct LargeChunk {
            uint8_t* start = nullptr;
            size_t size = 0;
        };

        std::vector<LargeChunk> chunks;

        Level1Slab() = default;

        LargeChunk allocate_large_chunk() {
            LargeChunk large_chunk;
            large_chunk.size = LARGE_CHUNK_SIZE;
            large_chunk.start = static_cast<uint8_t*>(malloc(LARGE_CHUNK_SIZE));

            chunks.push_back(large_chunk);
            return large_chunk;
        }

        ~Level1Slab() {
            for (auto& chunk : chunks) {
                free(chunk.start);
                // chunk.start = nullptr;
            }
        }
    };

    struct Level2Slab {
        static constexpr size_t SMALL_CHUNK_SIZE = 128 * 1024;  // 128 KB

        struct SmallChunk {
            uint8_t* start = nullptr;
            size_t size = 0;
        };

        uint8_t* current = nullptr;
        size_t remaining = 0;

        Level2Slab() = default;

        size_t free_space() const noexcept {
            return remaining;
        }

        SmallChunk allocate_small_chunk(Level1Slab& level1) {
            if (free_space() < SMALL_CHUNK_SIZE) {
                auto large_chunk = level1.allocate_large_chunk();
                current = large_chunk.start;
                remaining = large_chunk.size;
            }

            SmallChunk small_chunk;
            small_chunk.start = current;
            small_chunk.size = SMALL_CHUNK_SIZE;

            current += SMALL_CHUNK_SIZE;
            remaining -= SMALL_CHUNK_SIZE;

            return small_chunk;
        }
    };

    struct Level3Slab {
        using Tuple = HashMapUnchained<int32_t, size_t>::Tuple;

        struct SmallChunk {
            uint8_t* current = nullptr;
            uint8_t* end = nullptr;
        };

        std::vector<SmallChunk> chunks;

        Level3Slab() = default;

        size_t free_space() const {
            if (chunks.empty()) {
                return 0;
            }

            SmallChunk final_chunk = chunks.back();
            return final_chunk.end - final_chunk.current;
        }

        void add_space(const Level2Slab::SmallChunk& small_chunk) {
            SmallChunk new_chunk;
            new_chunk.current = small_chunk.start;
            new_chunk.end = small_chunk.start + small_chunk.size;
            chunks.push_back(new_chunk);
        }

        Tuple* allocate_tuple() {
            uint8_t* cur = chunks.back().current;
            chunks.back().current += sizeof(Tuple);
            return reinterpret_cast<Tuple*>(cur);
        }
    };

    uint64_t log2_num_partitions() noexcept {
        size_t p = 1;
        uint64_t log2 = 0;
        while (p < NumPartitions) {
            p <<= 1;
            log2++;
        }

        return log2;
    }

    // Hash function: hardware CRC if available
    template <typename Key>
    static uint64_t compute_hash(const Key& key) {
        // const uint8_t* data = (const uint8_t*)&key;
        // uint64_t hash;

// #ifdef __SSE4_2__
        //         if constexpr (sizeof(Key) == 8) {
        //             uint64_t value;
        //             memcpy(&value, data, 8);
        //             hash = _mm_crc32_u64(0, value);
        //         }
        //         else {
        //             uint32_t c = 0;
        //             for (size_t i = 0; i < sizeof(Key); i++) {
        //                 c = _mm_crc32_u8(c, data[i]);
        //             }
        //             hash = ((uint64_t)c << 32) | c;
        //         }
        // #else
        //         uint32_t c = 0xFFFFFFFFu;
        //         for (size_t i = 0; i < sizeof(Key); i++) {
        //             c ^= data[i];
        //             for (int k = 0; k < 8; k++) {
        //                 c = (c >> 1) ^ (0xEDB88320u & -(c & 1));
        //             }
        //         }
        //         c ^= 0xFFFFFFFFu;
        //         hash = ((uint64_t)c << 32) | c;
        // #endif
        //         return hash * 0x2545F4914F6CDD1DULL;
        constexpr uint64_t FIB64 = 11400714819323198485ULL;
        using U = std::make_unsigned_t<Key>;
        uint64_t k = static_cast<uint64_t>(static_cast<U>(key));
        return k * FIB64;
    }

    struct TupleCollector {
        using Tuple = HashMapUnchained<int32_t, size_t>::Tuple;

        Level1Slab level1;
        Level2Slab level2;
        Level3Slab level3[NumPartitions];
        size_t counts[NumPartitions];

        TupleCollector() = default;

        uint64_t shift = 64 - log2_num_partitions();

        void consume(const Tuple& tuple) {
            uint64_t part = compute_hash<int32_t>(tuple.key) >> shift;

            if (level3[part].free_space() < sizeof(Tuple)) {
                level3[part].add_space(level2.allocate_small_chunk(level1));
            }

            *level3[part].allocate_tuple() = tuple;
            counts[part] += 1;
        }
    };

    struct Context {
        TupleCollector collectors[NumPartitions];

        Context() = default;
    };
}