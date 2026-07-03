// Memory component tests, centered on the LRU bookkeeping.
//
// Regression: components live by value in ComponentPool's dense vector, which
// relocates them on growth (spawning a second colonist) and may COPY rather
// than move (the implicit move is not noexcept, so vector growth uses copies).
// The old std::list + map-of-list-iterators LRU dangled across that copy: the
// copied iterators still referenced the source component's list nodes, and the
// first touch after relocation erased a freed node — heap corruption that
// crashed the game whenever a second colonist spawned.

#include "Memory.h"

#include <gtest/gtest.h>

#include <vector>

namespace {

	constexpr uint32_t kDefId = 7;
	constexpr uint16_t kMask = 1;

	glm::vec2 posFor(size_t i) {
		return {static_cast<float>(i) * 0.5F, 0.0F};
	}

} // namespace

// The exact crash shape: fill a Memory, relocate it by growing the container
// it lives in (copy path), then exercise touch / forget / insert on the copy.
TEST(MemoryComponent, LruSurvivesContainerRelocation) {
	std::vector<ecs::Memory> pool;
	pool.emplace_back();
	constexpr size_t kEntries = 64;
	for (size_t i = 0; i < kEntries; ++i) {
		ASSERT_TRUE(pool[0].rememberWorldEntity(posFor(i), kDefId, kMask));
	}

	// Grow the vector past capacity several times so pool[0] relocates.
	for (int i = 0; i < 8; ++i) {
		pool.emplace_back();
	}

	ecs::Memory& m = pool[0];
	// Touch every key (the old implementation corrupted the heap here).
	for (size_t i = 0; i < kEntries; ++i) {
		EXPECT_FALSE(m.rememberWorldEntity(posFor(i), kDefId, kMask)) << "entry " << i << " should already be known";
	}
	// Forget them all, then verify the component is still fully usable.
	for (size_t i = 0; i < kEntries; ++i) {
		m.forgetWorldEntity(posFor(i), kDefId);
	}
	EXPECT_EQ(m.worldEntityCount(), 0U);
	EXPECT_TRUE(m.rememberWorldEntity({999.0F, 0.0F}, kDefId, kMask));
}

// An explicit copy must be independent of its source: mutating the copy's LRU
// never reaches into the original, and vice versa.
TEST(MemoryComponent, CopyIsIndependent) {
	ecs::Memory a;
	for (size_t i = 0; i < 16; ++i) {
		a.rememberWorldEntity(posFor(i), kDefId, kMask);
	}
	ecs::Memory b = a;
	for (size_t i = 0; i < 16; ++i) {
		b.forgetWorldEntity(posFor(i), kDefId);
	}
	EXPECT_EQ(b.worldEntityCount(), 0U);
	EXPECT_EQ(a.worldEntityCount(), 16U);
	for (size_t i = 0; i < 16; ++i) {
		EXPECT_FALSE(a.rememberWorldEntity(posFor(i), kDefId, kMask)); // touch still works on the source
	}
}

// At capacity the oldest un-touched entry is evicted; touching an entry
// protects it from the next eviction.
TEST(MemoryComponent, EvictsOldestRespectingTouch) {
	ecs::Memory m;
	for (size_t i = 0; i < ecs::Memory::kMaxWorldEntities; ++i) {
		ASSERT_TRUE(m.rememberWorldEntity(posFor(i), kDefId, kMask));
	}
	// Touch the very first entry so it is no longer the eviction candidate.
	EXPECT_FALSE(m.rememberWorldEntity(posFor(0), kDefId, kMask));

	// Overflow by one: evicts the second-oldest (index 1), not the touched index 0.
	EXPECT_TRUE(m.rememberWorldEntity({-10.0F, 0.0F}, kDefId, kMask));
	EXPECT_TRUE(m.knowsWorldEntity(posFor(0), kDefId));
	EXPECT_FALSE(m.knowsWorldEntity(posFor(1), kDefId));
	EXPECT_EQ(m.worldEntityCount(), ecs::Memory::kMaxWorldEntities);
}

// clear() resets the LRU as well: a cleared component accepts a fresh
// capacity's worth of entries and evicts in the new insertion order.
TEST(MemoryComponent, ClearResetsLru) {
	ecs::Memory m;
	for (size_t i = 0; i < 32; ++i) {
		m.rememberWorldEntity(posFor(i), kDefId, kMask);
	}
	m.clear();
	EXPECT_EQ(m.worldEntityCount(), 0U);
	for (size_t i = 0; i < 32; ++i) {
		EXPECT_TRUE(m.rememberWorldEntity(posFor(i), kDefId, kMask));
	}
	EXPECT_EQ(m.worldEntityCount(), 32U);
}
