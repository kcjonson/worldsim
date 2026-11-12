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
	const auto		anywhere = [](uint64_t, const glm::vec2&) { return true; };
	const auto		straight = [](uint64_t, const glm::vec2& p, float) { return std::optional<float>(glm::length(p)); };
	std::vector<ecs::SourceCandidate> scratch;

	ecs::Memory eastFirst;
	ASSERT_TRUE(eastFirst.rememberWorldEntity(east, kDefId, kSourceMask));
	ASSERT_TRUE(eastFirst.rememberWorldEntity(north, kDefId, kSourceMask));
	ecs::Memory northFirst;
	ASSERT_TRUE(northFirst.rememberWorldEntity(north, kDefId, kSourceMask));
	ASSERT_TRUE(northFirst.rememberWorldEntity(east, kDefId, kSourceMask));

	const auto a = ecs::findNearestReachableWithCapability(eastFirst, kSourceCapability, {0.0F, 0.0F}, anySource, straight, anywhere, scratch);
	const auto b = ecs::findNearestReachableWithCapability(northFirst, kSourceCapability, {0.0F, 0.0F}, anySource, straight, anywhere, scratch);
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
	const auto found  = ecs::findNearestReachableWithCapability(
		 memory, kSourceCapability, {0.0F, 0.0F}, anySource,
		 [&](uint64_t, const glm::vec2& p, float) {
			 ++tested;
			 return p == farthest ? std::optional<float>(glm::length(p)) : std::nullopt;
		 },
		 [&](uint64_t, const glm::vec2& p) { return p == farthest; }, scratch
	 );
	ASSERT_TRUE(found.has_value());
	EXPECT_EQ(found->position, farthest);
	EXPECT_GE(tested, kSources) << "each candidate walked";
	EXPECT_LE(tested, 2 * kSources) << "to twice its straight line, then at most once more";

	EXPECT_FALSE(ecs::findNearestReachableWithCapability(
					 memory, kSourceCapability, {0.0F, 0.0F}, anySource, [](uint64_t, const glm::vec2&, float) { return std::optional<float>{}; },
					 [](uint64_t, const glm::vec2&) { return false; }, scratch
	)
					 .has_value());
}

// The pick is by walk: a source 10 m off in a straight line but a 100 m walk round loses to one 40 m
// off that is a 40 m walk. A first walk goes to twice the straight line, the next in full or only as
// far as the best walk found, and a candidate no nearer in a straight line than the best walk is never
// walked.
TEST(MemoryQueries, NearestSourceIsByWalkNotStraightLine) {
	ecs::Memory		memory;
	const glm::vec2 acrossRiver{10.0F, 0.0F};
	const glm::vec2 behind{-40.0F, 0.0F};
	const glm::vec2 farAway{0.0F, 90.0F};
	for (const glm::vec2 p : {acrossRiver, behind, farAway}) {
		ASSERT_TRUE(memory.rememberWorldEntity(p, kDefId, kSourceMask));
	}
	std::vector<ecs::SourceCandidate> scratch;
	std::vector<std::pair<glm::vec2, float>> asked;
	const auto walk = [&](uint64_t, const glm::vec2& p, float maxWalk) {
		asked.emplace_back(p, maxWalk);
		const float length = p == acrossRiver ? 100.0F : glm::length(p);
		return length <= maxWalk ? std::optional<float>(length) : std::nullopt;
	};
	const auto found = ecs::findNearestReachableWithCapability(
		memory, kSourceCapability, {0.0F, 0.0F}, anySource, walk, [](uint64_t, const glm::vec2&) { return true; }, scratch
	);
	ASSERT_TRUE(found.has_value());
	EXPECT_EQ(found->position, behind);
	// The near one first, to twice its straight line (20 m: longer, so keyed at 20 m, still nearer than
	// the one behind), then in full (100 m), then the one behind only as far as that (40 m found).
	ASSERT_EQ(asked.size(), 3u) << "the far source is never walked";
	EXPECT_EQ(asked[0], std::pair(acrossRiver, 20.0F));
	EXPECT_EQ(asked[1], std::pair(acrossRiver, std::numeric_limits<float>::infinity()));
	EXPECT_EQ(asked[2], std::pair(behind, 100.0F));
}

// A far source (the search gave up) is never "no": it loses to any source with a walk, however long,
// and is taken when it is the only one, the nearer far one first by straight line.
TEST(MemoryQueries, FarSourceLosesToAWalkAndWinsAlone) {
	const glm::vec2 farNear{30.0F, 0.0F};
	const glm::vec2 farFarther{0.0F, 60.0F};
	const glm::vec2 longWalk{-50.0F, 0.0F};
	const auto		walk = [&](uint64_t, const glm::vec2& p, float maxWalk) {
		 if (p != longWalk) {
			 return ecs::WalkAnswer::farAway();
		 }
		 return ecs::WalkAnswer(500.0F <= maxWalk ? std::optional<float>(500.0F) : std::nullopt);
	};
	const auto reachable = [](uint64_t, const glm::vec2&) { return true; };
	std::vector<ecs::SourceCandidate> scratch;

	ecs::Memory all;
	for (const glm::vec2 p : {farNear, farFarther, longWalk}) {
		ASSERT_TRUE(all.rememberWorldEntity(p, kDefId, kSourceMask));
	}
	const auto withWalk = ecs::findNearestReachableWithCapability(all, kSourceCapability, {0.0F, 0.0F}, anySource, walk, reachable, scratch);
	ASSERT_TRUE(withWalk.has_value());
	EXPECT_EQ(withWalk->position, longWalk) << "a 500 m walk beats a far source 30 m off in a straight line";

	ecs::Memory onlyFar;
	for (const glm::vec2 p : {farFarther, farNear}) {
		ASSERT_TRUE(onlyFar.rememberWorldEntity(p, kDefId, kSourceMask));
	}
	const auto alone = ecs::findNearestReachableWithCapability(onlyFar, kSourceCapability, {0.0F, 0.0F}, anySource, walk, reachable, scratch);
	ASSERT_TRUE(alone.has_value()) << "a far source is a candidate";
	EXPECT_EQ(alone->position, farNear);
}

// The reach memo: a colonist among unreachable sources tests each one twice (to kWalkStretch times its
// straight line, then in full), and never again under the key. Learning a new source tests only that
// one, and a changed key (a nav change here) tests them all again.
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

	const auto query = [&](const ecs::SourceReachMemo::Key& key, const ecs::SourceReachMemo::Anchor& anchor) {
		memo.keep(key, anchor, 2 * ecs::Memory::kMaxWorldEntities);
		return ecs::findNearestReachableWithCapability(
			memory, kSourceCapability, {0.0F, 0.0F}, anySource,
			[&](uint64_t sourceKey, const glm::vec2& position, float maxWalk) {
				return memo.walk(sourceKey, position, maxWalk, [&](const glm::vec2& p, float) {
					++probes;
					return p == reachableSource ? std::optional<float>(glm::length(p)) : std::nullopt;
				});
			},
			[&](uint64_t sourceKey, const glm::vec2&) { return memo.knownReachable(sourceKey); }, scratch
		);
	};

	const ecs::SourceReachMemo::Key	   meshA{.startCell = 5, .structuralEpoch = 1, .beliefVersion = 0};
	const ecs::SourceReachMemo::Anchor anchorA{.at = {0.0F, 0.0F}, .structural = 1, .beliefVersion = 0};
	EXPECT_FALSE(query(meshA, anchorA).has_value());
	EXPECT_EQ(probes, 2 * kSources) << "the first decision tests every source, stretched then in full";
	EXPECT_FALSE(query(meshA, anchorA).has_value());
	EXPECT_EQ(probes, 2 * kSources) << "the next decision tests none";

	reachableSource = posFor(kSources + 1);
	ASSERT_TRUE(memory.rememberWorldEntity(reachableSource, kDefId, kSourceMask));
	const auto learned = query(meshA, anchorA);
	ASSERT_TRUE(learned.has_value()) << "a newly learned reachable source is found";
	EXPECT_EQ(learned->position, reachableSource);
	EXPECT_EQ(probes, 2 * kSources + 1) << "only the new source is tested";

	const ecs::SourceReachMemo::Key	   meshB{.startCell = 5, .structuralEpoch = 2, .beliefVersion = 0};
	const ecs::SourceReachMemo::Anchor anchorB{.at = {0.0F, 0.0F}, .structural = 2, .beliefVersion = 0};
	const size_t before = probes;
	EXPECT_TRUE(query(meshB, anchorB).has_value());
	EXPECT_GE(probes - before, kSources + 1) << "a nav change tests every source again, stretched, and the reachable one is taken";
	EXPECT_LE(probes - before, 2 * kSources + 1) << "the ones nearer than its walk once more, bounded by it";
}

// A far verdict is held whatever bound it is asked with: the source is probed once, not again for the
// full walk after its stretched one.
TEST(MemoryQueries, SourceReachMemoHoldsAFarVerdictAtALargerBound) {
	ecs::SourceReachMemo memo;
	memo.keep({.startCell = 1, .structuralEpoch = 1, .beliefVersion = 0}, {.at = {0.0F, 0.0F}, .structural = 1, .beliefVersion = 0}, 16);
	constexpr uint64_t kSource = 9;
	constexpr glm::vec2 kAt{0.0F, 30.0F};
	size_t			   probes = 0;
	const auto		   probe  = [&](const glm::vec2&, float) {
		   ++probes;
		   return ecs::WalkAnswer::farAway();
	};
	EXPECT_TRUE(memo.walk(kSource, kAt, 60.0F, probe).far);
	EXPECT_TRUE(memo.walk(kSource, kAt, std::numeric_limits<float>::infinity(), probe).far);
	EXPECT_TRUE(memo.walk(kSource, kAt, 10.0F, probe).far);
	EXPECT_EQ(probes, 1U) << "the far verdict was probed again";
}

// A search that gave up is asked again only once the colonist has moved a quarter of the way to its
// target, and never for less than kGaveUpMoveFloorMeters: a target 4 m across a river holds through
// 100 m of walking about, one 2 km off through 200 m.
TEST(MemoryQueries, GaveUpRouteHoldsUntilTheColonistMovesFarEnough) {
	ecs::SourceReachMemo memo;
	constexpr uint64_t	 kNear = 1;
	constexpr uint64_t	 kFar  = 2;
	memo.recordGaveUpRoute(kNear, {.from = {0.0F, 0.0F}, .target = {4.0F, 0.0F}, .spanStamp = 1, .beliefVersion = 0});
	memo.recordGaveUpRoute(kFar, {.from = {0.0F, 0.0F}, .target = {2000.0F, 0.0F}, .spanStamp = 1, .beliefVersion = 0});

	memo.forgetGaveUpRoutes({100.0F, 0.0F}, 0);
	EXPECT_TRUE(memo.searchGaveUpOn(kNear)) << "the near target was asked again within the floor";
	memo.forgetGaveUpRoutes({0.0F, 200.0F}, 0);
	EXPECT_FALSE(memo.searchGaveUpOn(kNear)) << "past the floor the near target is a new question";
	EXPECT_TRUE(memo.searchGaveUpOn(kFar)) << "the far target was asked again within a quarter of its distance";
	memo.forgetGaveUpRoutes({600.0F, 0.0F}, 0);
	EXPECT_FALSE(memo.searchGaveUpOn(kFar));
}

// A failed route is held under the anchor, not the key: a step on (a new key, the anchor held) it
// still reads unreachable and isn't walked; a new anchor forgets it.
TEST(MemoryQueries, SourceReachMemoHoldsFailedRoutesUnderTheAnchor) {
	ecs::SourceReachMemo			   memo;
	constexpr uint64_t				   kSource = 77;
	const glm::vec2					   at{10.0F, 0.0F};
	int								   probes = 0;
	const auto						   probe  = [&](const glm::vec2& p, float) {
		 ++probes;
		 return std::optional<float>(glm::length(p));
	};
	const ecs::SourceReachMemo::Key	   here{.startCell = 5, .structuralEpoch = 1};
	const ecs::SourceReachMemo::Key	   stepOn{.startCell = 6, .structuralEpoch = 1};
	const ecs::SourceReachMemo::Anchor anchor{.at = {0.0F, 0.0F}, .structural = 1};
	const ecs::SourceReachMemo::Anchor moved{.at = {200.0F, 0.0F}, .structural = 1};

	memo.keep(here, anchor, 16);
	ASSERT_TRUE(memo.reachable(kSource, at, probe));
	memo.recordFailedRoute(kSource, {.target = at});
	memo.keep(stepOn, anchor, 16);
	EXPECT_TRUE(memo.knownUnreachable(kSource));
	EXPECT_FALSE(memo.reachable(kSource, at, probe));
	EXPECT_EQ(probes, 1) << "a failed route was walked again";
	memo.keep(stepOn, moved, 16);
	EXPECT_FALSE(memo.knownUnreachable(kSource));
	EXPECT_TRUE(memo.reachable(kSource, at, probe));
}
