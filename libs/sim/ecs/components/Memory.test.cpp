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
#include "MemoryQueries.h"

#include <gtest/gtest.h>

#include <utility>
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

// A recycled node slot must be fully relinked: forget a middle entry, remember
// it again (recycles the slot at the newest position), then overflow and check
// the eviction order end to end.
TEST(MemoryComponent, SlotRecycleKeepsEvictionOrder) {
	ecs::Memory m;
	for (size_t i = 0; i < ecs::Memory::kMaxWorldEntities; ++i) {
		ASSERT_TRUE(m.rememberWorldEntity(posFor(i), kDefId, kMask));
	}
	// Forget a middle entry, then re-remember it: its node slot is recycled and
	// it becomes the NEWEST entry.
	m.forgetWorldEntity(posFor(5), kDefId);
	EXPECT_EQ(m.worldEntityCount(), ecs::Memory::kMaxWorldEntities - 1);
	EXPECT_TRUE(m.rememberWorldEntity(posFor(5), kDefId, kMask));
	EXPECT_EQ(m.worldEntityCount(), ecs::Memory::kMaxWorldEntities);

	// Two overflows evict the two oldest untouched entries (0 then 1), not the
	// recycled entry.
	EXPECT_TRUE(m.rememberWorldEntity({-10.0F, 0.0F}, kDefId, kMask));
	EXPECT_TRUE(m.rememberWorldEntity({-20.0F, 0.0F}, kDefId, kMask));
	EXPECT_FALSE(m.knowsWorldEntity(posFor(0), kDefId));
	EXPECT_FALSE(m.knowsWorldEntity(posFor(1), kDefId));
	EXPECT_TRUE(m.knowsWorldEntity(posFor(2), kDefId));
	EXPECT_TRUE(m.knowsWorldEntity(posFor(5), kDefId));
	EXPECT_EQ(m.worldEntityCount(), ecs::Memory::kMaxWorldEntities);
}

// A moved-from Memory must be safely reusable (empty, not corrupting): the LRU
// move operations reset the source's head/tail/node pool together.
TEST(MemoryComponent, MovedFromIsReusable) {
	ecs::Memory a;
	for (size_t i = 0; i < 8; ++i) {
		a.rememberWorldEntity(posFor(i), kDefId, kMask);
	}

	ecs::Memory b = std::move(a);
	EXPECT_EQ(b.worldEntityCount(), 8U);

	// Reuse the moved-from source: remember, touch, forget, all through the LRU.
	for (size_t i = 0; i < 4; ++i) {
		EXPECT_TRUE(a.rememberWorldEntity(posFor(100 + i), kDefId, kMask));
	}
	EXPECT_FALSE(a.rememberWorldEntity(posFor(100), kDefId, kMask)); // touch
	for (size_t i = 0; i < 4; ++i) {
		a.forgetWorldEntity(posFor(100 + i), kDefId);
	}
	EXPECT_EQ(a.worldEntityCount(), 0U);

	// Same through move-assignment.
	ecs::Memory c;
	c = std::move(b);
	EXPECT_EQ(c.worldEntityCount(), 8U);
	EXPECT_TRUE(b.rememberWorldEntity(posFor(200), kDefId, kMask));
	EXPECT_FALSE(b.rememberWorldEntity(posFor(200), kDefId, kMask)); // touch
	EXPECT_EQ(b.worldEntityCount(), 1U);
}

// clear() resets the LRU as well: a cleared component accepts a fresh set of
// entries with fully working touch/forget bookkeeping.
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

// Two sources at the same distance resolve by memory key, whichever was remembered first, so every
// machine picks the same one.
TEST(MemoryQueries, NearestSourceTieBreaksOnKeyNotInsertionOrder) {
	const glm::vec2 east{3.0F, 0.0F};
	const glm::vec2 north{0.0F, 3.0F};
	const glm::vec2 expected = ecs::hashWorldEntity(east, kDefId) < ecs::hashWorldEntity(north, kDefId) ? east : north;
	const auto		anySource = [](const ecs::KnownWorldEntity&) { return true; };
	const auto		anywhere = [](const glm::vec2&) { return true; };
	std::vector<ecs::SourceCandidate> scratch;

	ecs::Memory eastFirst;
	ASSERT_TRUE(eastFirst.rememberWorldEntity(east, kDefId, kMask));
	ASSERT_TRUE(eastFirst.rememberWorldEntity(north, kDefId, kMask));
	ecs::Memory northFirst;
	ASSERT_TRUE(northFirst.rememberWorldEntity(north, kDefId, kMask));
	ASSERT_TRUE(northFirst.rememberWorldEntity(east, kDefId, kMask));

	const auto capability = static_cast<assets::CapabilityType>(0);
	const auto a = ecs::findNearestReachableWithCapability(eastFirst, capability, {0.0F, 0.0F}, anySource, anywhere, scratch);
	const auto b = ecs::findNearestReachableWithCapability(northFirst, capability, {0.0F, 0.0F}, anySource, anywhere, scratch);
	ASSERT_TRUE(a.has_value());
	ASSERT_TRUE(b.has_value());
	EXPECT_EQ(a->position, expected);
	EXPECT_EQ(b->position, expected);
}

// No cap on how far down the list the walk goes: with every nearer source unreachable, the farthest
// reachable one is still found, and nothing reachable is nullopt.
TEST(MemoryQueries, NearestReachableSourceWalksEveryCandidate) {
	ecs::Memory memory;
	constexpr size_t kSources = 12;
	for (size_t i = 0; i < kSources; ++i) {
		ASSERT_TRUE(memory.rememberWorldEntity(posFor(i + 1), kDefId, kMask));
	}
	const glm::vec2 farthest = posFor(kSources);
	const auto		anySource = [](const ecs::KnownWorldEntity&) { return true; };
	std::vector<ecs::SourceCandidate> scratch;
	const auto capability = static_cast<assets::CapabilityType>(0);

	size_t	   tested = 0;
	const auto found = ecs::findNearestReachableWithCapability(
		memory, capability, {0.0F, 0.0F}, anySource,
		[&](const glm::vec2& p) {
			++tested;
			return p == farthest;
		},
		scratch
	);
	ASSERT_TRUE(found.has_value());
	EXPECT_EQ(found->position, farthest);
	EXPECT_EQ(tested, kSources) << "each candidate tested once, nearest first";

	EXPECT_FALSE(ecs::findNearestReachableWithCapability(
					 memory, capability, {0.0F, 0.0F}, anySource, [](const glm::vec2&) { return false; }, scratch
	)
					 .has_value());
}
