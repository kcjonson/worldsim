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

#include <limits>
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
	const auto		anywhere = [](ecs::SourceCandidate&) { return true; };
	std::vector<ecs::SourceCandidate> scratch;
	std::vector<ecs::SourceCandidate> chosen;

	ecs::Memory eastFirst;
	ASSERT_TRUE(eastFirst.rememberWorldEntity(east, kDefId, kSourceMask));
	ASSERT_TRUE(eastFirst.rememberWorldEntity(north, kDefId, kSourceMask));
	ecs::Memory northFirst;
	ASSERT_TRUE(northFirst.rememberWorldEntity(north, kDefId, kSourceMask));
	ASSERT_TRUE(northFirst.rememberWorldEntity(east, kDefId, kSourceMask));

	for (const ecs::Memory* memory : {&eastFirst, &northFirst}) {
		ecs::collectWithCapability(*memory, kSourceCapability, {0.0F, 0.0F}, anySource, scratch);
		ecs::nearestAdmitted(scratch, chosen, anywhere, ecs::kNearestOfCount);
		ASSERT_EQ(chosen.size(), 2U);
		EXPECT_EQ(chosen.front().position, expected);
		EXPECT_EQ(ecs::findNearestWithCapability(*memory, kSourceCapability, {0.0F, 0.0F})->position, expected);
	}
}

// A need's route request names the nearest kNearestOfCount sources by straight line that pass its
// filter, nearest first. Only the candidates down to the last one kept are asked about, and with fewer
// than that many passing, all of them are, in order.
TEST(MemoryQueries, NearestAdmittedKeepsTheNearestFewThatPass) {
	ecs::Memory memory;
	constexpr size_t kSources = 20;
	for (size_t i = 0; i < kSources; ++i) {
		ASSERT_TRUE(memory.rememberWorldEntity(posFor(kSources - i), kDefId, kSourceMask));
	}
	std::vector<ecs::SourceCandidate> scratch;
	std::vector<ecs::SourceCandidate> chosen;
	std::vector<glm::vec2>			  asked;
	// Every third source is sealed.
	const auto admit = [&](ecs::SourceCandidate& candidate) {
		asked.push_back(candidate.position);
		return asked.size() % 3 != 1;
	};
	ecs::collectWithCapability(memory, kSourceCapability, {0.0F, 0.0F}, anySource, scratch);
	ecs::nearestAdmitted(scratch, chosen, admit, ecs::kNearestOfCount);
	ASSERT_EQ(chosen.size(), ecs::kNearestOfCount);
	EXPECT_EQ(asked.size(), 12U) << "asked only down to the eighth that passed";
	for (size_t i = 0; i < asked.size(); ++i) {
		EXPECT_EQ(asked[i], posFor(i + 1)) << "nearest first";
	}
	for (size_t i = 1; i < chosen.size(); ++i) {
		EXPECT_LT(chosen[i - 1].distSq, chosen[i].distSq);
	}

	asked.clear();
	ecs::collectWithCapability(memory, kSourceCapability, {0.0F, 0.0F}, anySource, scratch);
	ecs::nearestAdmitted(scratch, chosen, [&](ecs::SourceCandidate& c) { return admit(c) && c.position.x > 8.0F; }, ecs::kNearestOfCount);
	EXPECT_EQ(asked.size(), kSources) << "every candidate asked when too few pass";
	EXPECT_EQ(chosen.size(), 3U) << "the four past 8 m, but the sealed one";
}

// The source the colonist is heading for is asked first and, while it passes, chosen alone, however many
// are nearer; once it fails, the nearest that pass are chosen as usual.
TEST(MemoryQueries, HeadedForSourceIsKeptWhileItPasses) {
	ecs::Memory memory;
	for (size_t i = 1; i <= 10; ++i) {
		ASSERT_TRUE(memory.rememberWorldEntity(posFor(i), kDefId, kSourceMask));
	}
	const uint64_t					  farthest = ecs::hashWorldEntity(posFor(10), kDefId);
	std::vector<ecs::SourceCandidate> scratch;
	std::vector<ecs::SourceCandidate> chosen;
	bool							  farthestPasses = true;
	const auto admit = [&](ecs::SourceCandidate& candidate) { return candidate.key != farthest || farthestPasses; };

	ecs::collectWithCapability(memory, kSourceCapability, {0.0F, 0.0F}, anySource, scratch);
	ecs::nearestAdmitted(scratch, chosen, admit, ecs::kNearestOfCount, farthest);
	ASSERT_EQ(chosen.size(), 1U);
	EXPECT_EQ(chosen.front().key, farthest);

	farthestPasses = false;
	ecs::collectWithCapability(memory, kSourceCapability, {0.0F, 0.0F}, anySource, scratch);
	ecs::nearestAdmitted(scratch, chosen, admit, ecs::kNearestOfCount, farthest);
	ASSERT_EQ(chosen.size(), ecs::kNearestOfCount);
	EXPECT_EQ(chosen.front().position, posFor(1));
}

// A verdict holds while the connectivity epoch and the belief it was found under stand; either moving
// forgets every verdict. One found under a key that has since moved is not held.
TEST(MemoryQueries, SourceReachMemoHoldsVerdictsUnderTheirKey) {
	ecs::SourceReachMemo			 memo;
	constexpr uint64_t				 kSource = 77;
	constexpr uint64_t				 kOther	 = 78;
	const ecs::SourceReachMemo::Key	 now{.connectivityEpoch = 3, .beliefVersion = 5};
	const ecs::SourceReachMemo::Key	 epochMoved{.connectivityEpoch = 4, .beliefVersion = 5};
	const ecs::SourceReachMemo::Key	 beliefMoved{.connectivityEpoch = 4, .beliefVersion = 6};

	memo.keep(now);
	memo.recordVerdict(kSource, now, now);
	EXPECT_TRUE(memo.hasVerdict(kSource));
	EXPECT_FALSE(memo.hasVerdict(kOther));
	memo.keep(now);
	EXPECT_TRUE(memo.hasVerdict(kSource)) << "held while nothing moves";

	memo.keep(epochMoved);
	EXPECT_FALSE(memo.hasVerdict(kSource)) << "the epoch moved";
	memo.recordVerdict(kOther, now, epochMoved);
	EXPECT_FALSE(memo.hasVerdict(kOther)) << "found under the old epoch";
	memo.recordVerdict(kOther, epochMoved, epochMoved);
	EXPECT_TRUE(memo.hasVerdict(kOther));
	memo.keep(beliefMoved);
	EXPECT_FALSE(memo.hasVerdict(kOther)) << "his belief moved";
	EXPECT_EQ(memo.verdictCount(), 0U);
}

TEST(MemoryQueries, SourceReachMemoStaysBounded) {
	ecs::SourceReachMemo			memo;
	const ecs::SourceReachMemo::Key now{.connectivityEpoch = 1, .beliefVersion = 1};
	for (uint64_t target = 0; target < 3 * ecs::SourceReachMemo::kMaxVerdicts; ++target) {
		memo.recordVerdict(target, now, now);
		ASSERT_LE(memo.verdictCount(), ecs::SourceReachMemo::kMaxVerdicts);
	}
}

