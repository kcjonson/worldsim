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
#include "SourceReachMemo.h"

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

namespace {

	/// Every remembered key sits in its def's list at its defSlot, and the lists hold nothing else.
	void expectDefListsMatch(const ecs::Memory& m) {
		size_t listed = 0;
		for (const auto& [defNameId, keys] : m.worldEntitiesByDef) {
			listed += keys.size();
			for (size_t slot = 0; slot < keys.size(); ++slot) {
				const ecs::KnownWorldEntity* entity = m.getWorldEntity(keys[slot]);
				ASSERT_NE(entity, nullptr) << "def " << defNameId << " lists a forgotten key";
				EXPECT_EQ(entity->defNameId, defNameId);
				EXPECT_EQ(entity->defSlot, slot);
			}
		}
		EXPECT_EQ(listed, m.worldEntityCount());
	}

} // namespace

// The per-def key lists follow every remember, forget, eviction, copy, and clear.
TEST(MemoryComponent, DefListsFollowRememberForgetAndEvict) {
	constexpr uint32_t kOtherDefId = 8;
	ecs::Memory		   m;
	for (size_t i = 0; i < 20; ++i) {
		m.rememberWorldEntity(posFor(i), i % 2 == 0 ? kDefId : kOtherDefId, kMask);
	}
	EXPECT_EQ(m.worldEntitiesOfDef(kDefId).size(), 10U);
	EXPECT_EQ(m.worldEntitiesOfDef(kOtherDefId).size(), 10U);
	EXPECT_TRUE(m.worldEntitiesOfDef(99).empty());

	// Forget the first, a middle, and the last of one def: each swap-removal re-slots the moved key.
	m.forgetWorldEntity(posFor(0), kDefId);
	m.forgetWorldEntity(posFor(10), kDefId);
	m.forgetWorldEntity(posFor(18), kDefId);
	EXPECT_EQ(m.worldEntitiesOfDef(kDefId).size(), 7U);
	expectDefListsMatch(m);

	ecs::Memory copy = m;
	copy.forgetWorldEntity(posFor(1), kOtherDefId);
	expectDefListsMatch(copy);
	expectDefListsMatch(m);

	// Fill to capacity and overflow: evictions leave the lists in step.
	for (size_t i = 100; m.worldEntityCount() < ecs::Memory::kMaxWorldEntities; ++i) {
		m.rememberWorldEntity(posFor(i), kOtherDefId, kMask);
	}
	m.rememberWorldEntity({-10.0F, 0.0F}, kDefId, kMask);
	m.rememberWorldEntity({-20.0F, 0.0F}, kDefId, kMask);
	EXPECT_EQ(m.worldEntityCount(), ecs::Memory::kMaxWorldEntities);
	expectDefListsMatch(m);

	m.clear();
	EXPECT_TRUE(m.worldEntitiesOfDef(kDefId).empty());
	expectDefListsMatch(m);
}

namespace {

	constexpr auto	   kSourceCapability = assets::CapabilityType::Harvestable;
	constexpr uint16_t kSourceMask = 1U << static_cast<unsigned>(kSourceCapability);

	const auto anySource = [](uint64_t, const ecs::KnownWorldEntity&) { return true; };

} // namespace

// Two sources at the same distance resolve by memory key, whichever was remembered first, so every
// machine picks the same one.
TEST(MemoryQueries, NearestSourceTieBreaksOnKeyNotInsertionOrder) {
	const glm::vec2 east{3.0F, 0.0F};
	const glm::vec2 north{0.0F, 3.0F};
	const glm::vec2 expected = ecs::hashWorldEntity(east, kDefId) < ecs::hashWorldEntity(north, kDefId) ? east : north;
	const auto		anywhere = [](uint64_t, const glm::vec2&) { return true; };
	std::vector<ecs::SourceCandidate> scratch;

	ecs::Memory eastFirst;
	ASSERT_TRUE(eastFirst.rememberWorldEntity(east, kDefId, kSourceMask));
	ASSERT_TRUE(eastFirst.rememberWorldEntity(north, kDefId, kSourceMask));
	ecs::Memory northFirst;
	ASSERT_TRUE(northFirst.rememberWorldEntity(north, kDefId, kSourceMask));
	ASSERT_TRUE(northFirst.rememberWorldEntity(east, kDefId, kSourceMask));

	const auto a = ecs::findNearestReachableWithCapability(eastFirst, kSourceCapability, {0.0F, 0.0F}, anySource, anywhere, scratch);
	const auto b = ecs::findNearestReachableWithCapability(northFirst, kSourceCapability, {0.0F, 0.0F}, anySource, anywhere, scratch);
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
		ASSERT_TRUE(memory.rememberWorldEntity(posFor(i + 1), kDefId, kSourceMask));
	}
	const glm::vec2					  farthest = posFor(kSources);
	std::vector<ecs::SourceCandidate> scratch;

	size_t	   tested = 0;
	const auto found = ecs::findNearestReachableWithCapability(
		memory, kSourceCapability, {0.0F, 0.0F}, anySource,
		[&](uint64_t, const glm::vec2& p) {
			++tested;
			return p == farthest;
		},
		scratch
	);
	ASSERT_TRUE(found.has_value());
	EXPECT_EQ(found->position, farthest);
	EXPECT_EQ(tested, kSources) << "each candidate tested once, nearest first";

	EXPECT_FALSE(ecs::findNearestReachableWithCapability(
					 memory, kSourceCapability, {0.0F, 0.0F}, anySource, [](uint64_t, const glm::vec2&) { return false; },
					 scratch
	)
					 .has_value());
}

// The reach memo: a colonist among unreachable sources tests each one once. Learning a new source
// tests only that one, and a changed key (a nav rebuild here) tests them all again.
TEST(MemoryQueries, SourceReachMemoTestsEachSourceOncePerKey) {
	ecs::Memory memory;
	constexpr size_t kSources = 8;
	for (size_t i = 0; i < kSources; ++i) {
		ASSERT_TRUE(memory.rememberWorldEntity(posFor(i + 1), kDefId, kSourceMask));
	}
	std::vector<ecs::SourceCandidate> scratch;
	ecs::SourceReachMemo			  memo;
	size_t							  probes = 0;
	glm::vec2						  reachableSource{-1000.0F, -1000.0F}; // nothing reachable yet

	const auto query = [&](const ecs::SourceReachMemo::Key& key) {
		memo.keep(key, 2 * ecs::Memory::kMaxWorldEntities);
		return ecs::findNearestReachableWithCapability(
			memory, kSourceCapability, {0.0F, 0.0F}, anySource,
			[&](uint64_t sourceKey, const glm::vec2& position) {
				return memo.reachable(sourceKey, position, [&](const glm::vec2& p) {
					++probes;
					return p == reachableSource;
				});
			},
			scratch
		);
	};

	const ecs::SourceReachMemo::Key meshA{.regionId = 0, .regionGeneration = 1, .beliefVersion = 0, .reachRoot = 5};
	EXPECT_FALSE(query(meshA).has_value());
	EXPECT_EQ(probes, kSources) << "the first decision tests every source";
	EXPECT_FALSE(query(meshA).has_value());
	EXPECT_EQ(probes, kSources) << "the next decision tests none";

	reachableSource = posFor(kSources + 1);
	ASSERT_TRUE(memory.rememberWorldEntity(reachableSource, kDefId, kSourceMask));
	const auto learned = query(meshA);
	ASSERT_TRUE(learned.has_value()) << "a newly learned reachable source is found";
	EXPECT_EQ(learned->position, reachableSource);
	EXPECT_EQ(probes, kSources + 1) << "only the new source is tested";

	const ecs::SourceReachMemo::Key meshB{.regionId = 0, .regionGeneration = 2, .beliefVersion = 0, .reachRoot = 5};
	EXPECT_TRUE(query(meshB).has_value());
	EXPECT_EQ(probes, 2 * kSources + 2) << "a nav rebuild tests every source again, nearest first, up to the reachable one";
}
