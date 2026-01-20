#include <catch2/catch_test_macros.hpp>
#include <vector>
#include <cstdint>
#include <string>
#include <cstring>

#include <slab_allocator.hpp>
#include "../src/hashmap_unchained.hpp"

using namespace SlabAllocator;
using Tuple = HashMapUnchained<int32_t, size_t>::Tuple;

TEST_CASE("Simple tests for the three levels of the slab allocator", "[Level1][Level2][Level3]") {
    // Level 1 slab
    Level1Slab level1;
    REQUIRE(level1.chunks.empty());

    level1.allocate_large_chunk();
    REQUIRE(level1.chunks.size() == 1);
    REQUIRE(level1.chunks.back().size == Level1Slab::LARGE_CHUNK_SIZE);

    // Level 2 slab
    Level2Slab level2;
    REQUIRE(level2.current == nullptr);
    REQUIRE(level2.free_space() == 0);

    Level2Slab::SmallChunk small_chunk = level2.allocate_small_chunk(level1);
    REQUIRE(small_chunk.size == SMALL_CHUNK_SIZE);
    REQUIRE(small_chunk.start == level1.chunks.back().start);
    REQUIRE(level2.remaining == Level1Slab::LARGE_CHUNK_SIZE - SMALL_CHUNK_SIZE);

    // Level 3 slab
    Level3Slab level3;
    REQUIRE(level3.chunks.empty());
    REQUIRE(level3.free_space() == 0);

    level3.add_space(small_chunk);
    REQUIRE(level3.chunks.size() == 1);
    REQUIRE(level3.chunks.back().current == small_chunk.start);
    REQUIRE(level3.chunks.back().end == small_chunk.start + SMALL_CHUNK_SIZE);

    Tuple tuple{ 1, 2 };
    *level3.allocate_tuple() = tuple;
    REQUIRE(level3.chunks.back().current == level1.chunks.back().start + sizeof(Tuple));
    Tuple* t = reinterpret_cast<Tuple*>(level3.chunks.back().current - sizeof(Tuple));
    REQUIRE(t->key == 1);
    REQUIRE(t->value == 2);
}

TEST_CASE("Tuple Collector is created correctly and consume adds to the slab allocator", "[TupleCollector]") {
    TupleCollector collector;
    REQUIRE(collector.shift == 59);

    Tuple tuple{ 1, 2 };
    collector.consume(tuple);
    REQUIRE_FALSE(collector.level1.chunks.empty());
    REQUIRE(collector.level1.chunks.back().start != nullptr);
    REQUIRE(collector.level2.current != nullptr);

    uint64_t hash = compute_hash(1) >> collector.shift;
    REQUIRE(collector.level3[hash].chunks.back().current == collector.level1.chunks.back().start + sizeof(Tuple));
    REQUIRE(collector.counts[hash] == 1);

    collector.reset();
    REQUIRE(collector.level3[hash].chunks.empty());
    REQUIRE(collector.level2.current == nullptr);
    REQUIRE(collector.level2.remaining == 0);
    REQUIRE(collector.level1.chunks.back().start != nullptr);   // Reset doesn't delete the chunk !!!
    REQUIRE(collector.level1.free_chunk_idx == 0);

    collector.level1.allocate_large_chunk();
    REQUIRE(collector.level1.chunks.size() == 1);
}