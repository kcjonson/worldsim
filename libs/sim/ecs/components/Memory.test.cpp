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

#include <assets/ConfigValidator.h>
#include <assets/MemoryConfig.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <random>
#include <string>
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

	/// The first way the memory's indexes disagree with what it remembers, or empty: every key is in exactly
	/// one cell of its def, the one it lies in, with its slots right; each def's capability counts are the real
	/// number of its things with each bit, and the def is listed once under exactly those capabilities; each
	/// capability index holds exactly the keys remembered with that bit.
	std::string indexViolation(const ecs::Memory& memory) {
		std::size_t listed = 0;
		for (const auto& [defNameId, remembered] : memory.worldEntitiesByDef) {
			std::array<uint32_t, ecs::Memory::kCapabilityTypeCount> counted{};
			for (std::size_t slot = 0; slot < remembered.keys.size(); ++slot) {
				const ecs::KnownWorldEntity* entity = memory.getWorldEntity(remembered.keys[slot]);
				if (entity == nullptr || entity->defNameId != defNameId || entity->defSlot != slot) {
					return "def " + std::to_string(defNameId) + " key slot " + std::to_string(slot);
				}
				const ecs::MemoryCell* cell =
					remembered.cellAt(ecs::memoryCellCoordinate(entity->position.x), ecs::memoryCellCoordinate(entity->position.y));
				if (cell == nullptr || entity->cellSlot >= cell->keys.size() || cell->keys[entity->cellSlot] != remembered.keys[slot]) {
					return "def " + std::to_string(defNameId) + " key " + std::to_string(slot) + " is not in its cell";
				}
				for (std::size_t capability = 0; capability < counted.size(); ++capability) {
					counted[capability] += (entity->capabilityMask >> capability) & 1U;
				}
			}
			listed += remembered.keys.size();
			std::size_t filed = 0;
			for (const ecs::MemoryCell& cell : remembered.cells) {
				filed += cell.keys.size();
				const bool withinBounds = cell.x >= remembered.minCellX && cell.x <= remembered.maxCellX && cell.y >= remembered.minCellY &&
										  cell.y <= remembered.maxCellY;
				if (cell.keys.empty() || remembered.cellAt(cell.x, cell.y) != &cell || !withinBounds) {
					return "def " + std::to_string(defNameId) + " cell (" + std::to_string(cell.x) + ", " + std::to_string(cell.y) + ")";
				}
			}
			if (filed != remembered.keys.size() || remembered.cellIndex.size() != remembered.cells.size()) {
				return "def " + std::to_string(defNameId) + " files " + std::to_string(filed) + " of " +
					   std::to_string(remembered.keys.size());
			}
			for (std::size_t capability = 0; capability < counted.size(); ++capability) {
				const auto&		  withCapability = memory.definitionsWithCapability[capability];
				const std::size_t listings = static_cast<std::size_t>(std::count(withCapability.begin(), withCapability.end(), defNameId));
				if (remembered.capabilityCounts[capability] != counted[capability] || listings != (counted[capability] > 0 ? 1U : 0U) ||
					(listings == 1 && withCapability[remembered.capabilitySlots[capability]] != defNameId)) {
					return "def " + std::to_string(defNameId) + " capability " + std::to_string(capability);
				}
			}
		}
		if (listed != memory.worldEntityCount()) {
			return "defs list " + std::to_string(listed) + " of " + std::to_string(memory.worldEntityCount());
		}
		for (std::size_t capability = 0; capability < ecs::Memory::kCapabilityTypeCount; ++capability) {
			std::size_t withBit = 0;
			for (const auto& [key, known] : memory.knownWorldEntities) {
				if (((known.capabilityMask >> capability) & 1U) != 0) {
					++withBit;
					if (memory.capabilityIndex[capability].count(key) == 0) {
						return "capability index " + std::to_string(capability) + " misses a key";
					}
				}
			}
			if (withBit != memory.capabilityIndex[capability].size()) {
				return "capability index " + std::to_string(capability) + " holds a stale key";
			}
		}
		return {};
	}

	/// Every remembered key sits in its def's list at its defSlot and in the grid cell it lies in at its
	/// cellSlot, the lists and cells hold nothing else, and each def is listed under exactly the
	/// capabilities its remembered things carry.
	void expectDefListsMatch(const ecs::Memory& m) {
		EXPECT_EQ(indexViolation(m), "");
		size_t listed = 0;
		size_t filed = 0;
		for (const auto& [defNameId, remembered] : m.worldEntitiesByDef) {
			const auto& keys = remembered.keys;
			listed += keys.size();
			for (size_t slot = 0; slot < keys.size(); ++slot) {
				const ecs::KnownWorldEntity* entity = m.getWorldEntity(keys[slot]);
				ASSERT_NE(entity, nullptr) << "def " << defNameId << " lists a forgotten key";
				EXPECT_EQ(entity->defNameId, defNameId);
				EXPECT_EQ(entity->defSlot, slot);
			}
			EXPECT_EQ(remembered.cellIndex.size(), remembered.cells.size());
			for (size_t index = 0; index < remembered.cells.size(); ++index) {
				const ecs::MemoryCell& cell = remembered.cells[index];
				EXPECT_FALSE(cell.keys.empty()) << "an empty cell is kept";
				EXPECT_EQ(remembered.cellAt(cell.x, cell.y), &cell);
				filed += cell.keys.size();
				for (size_t slot = 0; slot < cell.keys.size(); ++slot) {
					const ecs::KnownWorldEntity* entity = m.getWorldEntity(cell.keys[slot]);
					ASSERT_NE(entity, nullptr) << "def " << defNameId << " files a forgotten key";
					EXPECT_EQ(entity->cellSlot, slot);
					EXPECT_EQ(ecs::memoryCellCoordinate(entity->position.x), cell.x);
					EXPECT_EQ(ecs::memoryCellCoordinate(entity->position.y), cell.y);
				}
			}
			for (size_t capability = 0; capability < ecs::Memory::kCapabilityTypeCount; ++capability) {
				const auto& withCapability = m.definitionsWithCapability[capability];
				const bool	isListed = std::find(withCapability.begin(), withCapability.end(), defNameId) != withCapability.end();
				EXPECT_EQ(isListed, remembered.capabilityCounts[capability] > 0) << "def " << defNameId << " capability " << capability;
			}
		}
		EXPECT_EQ(listed, m.worldEntityCount());
		EXPECT_EQ(filed, m.worldEntityCount());
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

	// A def is listed under a capability while any of its things carries it.
	constexpr uint32_t kThirdDefId = 9;
	m.rememberWorldEntity({-1.0F, 40.0F}, kDefId, 0b110);
	m.rememberWorldEntity({-2.0F, 40.0F}, kThirdDefId, 0b100);
	expectDefListsMatch(m);
	m.forgetWorldEntity({-1.0F, 40.0F}, kDefId);
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

// Seeded random remembers, forgets, copies, moves, and clears over a few defs and capability masks, at
// positions of mixed sign, on cell boundaries, and far from the origin, then a fill past the cap so it
// evicts: the whole index holds after every step. A position that is not a number is refused.
TEST(MemoryComponent, IndexHoldsUnderRandomRememberForgetEvictCopyAndMove) {
	const std::array<glm::vec2, 8> boundaries = {
		{{-16.0F, 0.0F},
		 {16.0F, -16.0F},
		 {-0.0F, -0.0F},
		 {0.0F, 16.0F},
		 {1.0e6F, -1.0e6F},
		 {-1.0e6F, 1.0e6F},
		 {15.999F, -16.001F},
		 {-32.0F, 32.0F}}
	};
	std::mt19937								generator(20261121);
	ecs::Memory									memory;
	std::vector<std::pair<glm::vec2, uint32_t>> remembered;
	const auto									randomPosition = [&]() {
		 if (generator() % 4 == 0) {
			 return boundaries[generator() % boundaries.size()];
		 }
		 const float x = static_cast<float>(static_cast<int>(generator() % 2001) - 1000) * 0.5F;
		 const float y = static_cast<float>(static_cast<int>(generator() % 2001) - 1000) * 0.5F;
		 return glm::vec2{x, y};
	};
	for (int step = 0; step < 1500; ++step) {
		const uint32_t operation = generator() % 100;
		if (operation < 60) {
			const glm::vec2 position = randomPosition();
			const uint32_t	def = 1 + generator() % 4;
			const auto		mask = static_cast<uint16_t>(1 + generator() % 1023);
			memory.rememberWorldEntity(position, def, mask);
			remembered.emplace_back(position, def);
		} else if (operation < 88 && !remembered.empty()) {
			const auto& [position, def] = remembered[generator() % remembered.size()];
			memory.forgetWorldEntity(position, def);
		} else if (operation < 93) {
			const ecs::Memory copy = memory;
			memory = copy;
		} else if (operation < 98) {
			ecs::Memory moved = std::move(memory);
			memory = std::move(moved);
		} else if (operation == 99) {
			memory.clear();
			remembered.clear();
		}
		ASSERT_EQ(indexViolation(memory), "") << "step " << step;
	}
	for (std::size_t i = 0; memory.worldEntityCount() < ecs::Memory::kMaxWorldEntities; ++i) {
		memory.rememberWorldEntity(posFor(i), 1 + static_cast<uint32_t>(i % 4), static_cast<uint16_t>(1 + i % 1023));
	}
	ASSERT_EQ(indexViolation(memory), "");
	for (int overflow = 0; overflow < 20; ++overflow) {
		const glm::vec2 position = randomPosition();
		const uint32_t	def = 1 + generator() % 4;
		const auto		mask = static_cast<uint16_t>(1 + generator() % 1023);
		memory.rememberWorldEntity(position, def, mask);
		ASSERT_EQ(memory.worldEntityCount(), ecs::Memory::kMaxWorldEntities);
		ASSERT_EQ(indexViolation(memory), "") << "overflow " << overflow;
	}

	const std::size_t before = memory.worldEntityCount();
	EXPECT_FALSE(memory.rememberWorldEntity({std::numeric_limits<float>::quiet_NaN(), 1.0F}, 1, 1));
	EXPECT_FALSE(memory.rememberWorldEntity({1.0F, std::numeric_limits<float>::infinity()}, 1, 1));
	EXPECT_FALSE(memory.rememberWorldEntity({-1.0e9F, 1.0F}, 1, 1)) << "past the key's integers";
	EXPECT_EQ(memory.worldEntityCount(), before);
}

namespace {

	constexpr uint32_t kColonistKind = 11;
	constexpr uint32_t kOtherKind = 12;

	std::vector<ecs::EntityID> moversSeenWithin(const ecs::Memory& memory, uint32_t kind, double now, double window) {
		std::vector<ecs::EntityID> seen;
		memory.forEachDynamicEntitySeenWithin(kind, now, window, [&](const ecs::KnownDynamicEntity& known) {
			seen.push_back(known.entityId);
			return true;
		});
		std::ranges::sort(seen);
		return seen;
	}

	std::filesystem::path shippedMemoryConfig() {
		return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path().parent_path() / "assets" / "world" /
			   "creatures" / "Human" / "memory.xml";
	}

	/// The recency window a memory.xml with `gameMinutes` loads, validated.
	double configuredWindow(const std::string& gameMinutes) {
		const std::filesystem::path path =
			std::filesystem::temp_directory_path() /
			("worldsim-memory-" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()) + "-" + gameMinutes +
			 ".xml");
		{
			std::ofstream file(path);
			file << "<Memory><RecentSighting gameMinutes=\"" << gameMinutes << "\"/></Memory>";
		}
		assets::MemoryConfig config;
		EXPECT_TRUE(config.loadFromFile(path.parent_path(), path.filename()));
		std::filesystem::remove(path);
		EXPECT_TRUE(assets::ConfigValidator::validateMemory(config)) << gameMinutes;
		return config.recentSightingGameMinutes();
	}

} // namespace

// The window is data: one sighting is company under a longer configured window and not under a shorter.
TEST(MemoryQueries, TheConfiguredWindowDecidesWhoIsRecent) {
	const ecs::EntityID seen = ecs::makeEntityID(1, 1);
	const double		now = 1000.0;
	ecs::Memory			memory;
	memory.rememberDynamicEntity(seen, kColonistKind, {1.0F, 2.0F}, now - 20.0);

	EXPECT_EQ(moversSeenWithin(memory, kColonistKind, now, configuredWindow("30")), std::vector<ecs::EntityID>{seen});
	EXPECT_TRUE(moversSeenWithin(memory, kColonistKind, now, configuredWindow("10")).empty());
}

// A sighting exactly a window old is still recent; the least bit older is not.
TEST(MemoryQueries, TheWindowIncludesItsOwnEdge) {
	const double		window = 30.0;
	const double		now = 1000.0;
	const ecs::EntityID atEdge = ecs::makeEntityID(1, 1);
	const ecs::EntityID pastEdge = ecs::makeEntityID(2, 1);
	ecs::Memory			memory;
	memory.rememberDynamicEntity(atEdge, kColonistKind, {0.0F, 0.0F}, now - window);
	memory.rememberDynamicEntity(pastEdge, kColonistKind, {0.0F, 0.0F}, std::nextafter(now - window, 0.0));

	EXPECT_EQ(moversSeenWithin(memory, kColonistKind, now, window), std::vector<ecs::EntityID>{atEdge});
}

// The query stops when the visitor says it has seen enough.
TEST(MemoryQueries, TheVisitorEndsTheQuery) {
	ecs::Memory memory;
	for (uint32_t index = 1; index <= 5; ++index) {
		memory.rememberDynamicEntity(ecs::makeEntityID(index, 1), kColonistKind, {0.0F, 0.0F}, 100.0);
	}
	int visits = 0;
	memory.forEachDynamicEntitySeenWithin(kColonistKind, 100.0, 1.0, [&](const ecs::KnownDynamicEntity&) { return ++visits < 2; });
	EXPECT_EQ(visits, 2);
}

// The recency query, with the shipped window: a sighting inside it is company, one older is not, and
// a mover of another kind never answers.
TEST(MemoryQueries, MoversSeenWithinTheShippedWindow) {
	assets::MemoryConfig config;
	ASSERT_TRUE(config.loadFromFile(shippedMemoryConfig().parent_path(), shippedMemoryConfig().filename()));
	ASSERT_TRUE(assets::ConfigValidator::validateMemory(config));
	const double window = config.recentSightingGameMinutes();

	const ecs::EntityID fresh = ecs::makeEntityID(1, 1);
	const ecs::EntityID stale = ecs::makeEntityID(2, 1);
	const ecs::EntityID otherKind = ecs::makeEntityID(3, 1);
	const double		now = 1000.0;
	ecs::Memory			memory;
	memory.rememberDynamicEntity(fresh, kColonistKind, {1.0F, 2.0F}, now - window * 0.5);
	memory.rememberDynamicEntity(stale, kColonistKind, {3.0F, 4.0F}, now - window - 1.0);
	memory.rememberDynamicEntity(otherKind, kOtherKind, {5.0F, 6.0F}, now);

	EXPECT_EQ(moversSeenWithin(memory, kColonistKind, now, window), std::vector<ecs::EntityID>{fresh});
	EXPECT_EQ(moversSeenWithin(memory, kOtherKind, now, window), std::vector<ecs::EntityID>{otherKind});
	EXPECT_TRUE(memory.knowsDynamicEntity(stale)) << "an old sighting is still where he was, only not company";
}

// Movers are bounded: a new one past the cap forgets the one seen longest ago, and seeing one again
// keeps its single entry.
TEST(MemoryComponent, MoverMemoryForgetsTheLongestUnseen) {
	ecs::Memory memory;
	for (uint32_t index = 1; index <= ecs::Memory::kMaxDynamicEntities; ++index) {
		memory.rememberDynamicEntity(ecs::makeEntityID(index, 1), kColonistKind, {0.0F, 0.0F}, static_cast<double>(index));
	}
	memory.rememberDynamicEntity(ecs::makeEntityID(1, 1), kColonistKind, {9.0F, 9.0F}, 10000.0);
	EXPECT_EQ(memory.knownDynamicEntities.size(), ecs::Memory::kMaxDynamicEntities);

	const ecs::EntityID newcomer = ecs::makeEntityID(100000, 1);
	memory.rememberDynamicEntity(newcomer, kColonistKind, {0.0F, 0.0F}, 10001.0);
	EXPECT_EQ(memory.knownDynamicEntities.size(), ecs::Memory::kMaxDynamicEntities);
	EXPECT_TRUE(memory.knowsDynamicEntity(newcomer));
	EXPECT_TRUE(memory.knowsDynamicEntity(ecs::makeEntityID(1, 1))) << "seen again, so not the longest unseen";
	EXPECT_FALSE(memory.knowsDynamicEntity(ecs::makeEntityID(2, 1)));
}

namespace {

	constexpr auto	   kSourceCapability = assets::CapabilityType::Harvestable;
	constexpr uint16_t kSourceMask = 1U << static_cast<unsigned>(kSourceCapability);

	const auto anySource = [](uint64_t, const ecs::KnownWorldEntity&) {
		return true;
	};

} // namespace

// Two sources at the same distance resolve by memory key, whichever was remembered first, so every
// machine picks the same one.
TEST(MemoryQueries, NearestSourceTieBreaksOnKeyNotInsertionOrder) {
	const glm::vec2 east{3.0F, 0.0F};
	const glm::vec2 north{0.0F, 3.0F};
	const glm::vec2 expected = ecs::hashWorldEntity(east, kDefId) < ecs::hashWorldEntity(north, kDefId) ? east : north;
	const auto		anywhere = [](ecs::SourceCandidate&) {
		 return true;
	};
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
	ecs::Memory		 memory;
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
	const auto						  admit = [&](ecs::SourceCandidate& candidate) {
		   return candidate.key != farthest || farthestPasses;
	};

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

// A verdict holds while nothing moves. His belief moving forgets every verdict at once; the connectivity
// epoch moving forgets only those at least kVerdictMinTicks old, however often it moves meanwhile, and one
// recorded after it last moved is held.
TEST(MemoryQueries, SourceReachMemoHoldsVerdictsUntilTheyMayNoLongerHold) {
	ecs::SourceReachMemo			memo;
	constexpr uint64_t				kSource = 77;
	constexpr uint64_t				kOther = 78;
	constexpr uint64_t				kAge = ecs::SourceReachMemo::kVerdictMinTicks;
	ecs::SourceReachMemo::Key		now{.connectivityEpoch = 3, .beliefVersion = 5};
	const ecs::SourceReachMemo::Key beliefMoved{.connectivityEpoch = 100, .beliefVersion = 6};

	memo.keep(now, 0);
	memo.recordVerdict(kSource, now, 0);
	EXPECT_TRUE(memo.hasVerdict(kSource));
	EXPECT_FALSE(memo.hasVerdict(kOther));
	memo.keep(now, 10 * kAge);
	EXPECT_TRUE(memo.hasVerdict(kSource)) << "held while nothing moves, however old";

	// Recorded at tick 0 and again at 10 * kAge (refreshed); the epoch moves every tick from there on.
	memo.recordVerdict(kSource, now, 10 * kAge);
	for (uint64_t tick = 10 * kAge + 1; tick < 11 * kAge; ++tick) {
		++now.connectivityEpoch;
		memo.keep(now, tick);
		ASSERT_TRUE(memo.hasVerdict(kSource)) << "forgotten young at tick " << tick;
		if (tick == 10 * kAge + 5) {
			memo.recordVerdict(kOther, now, tick);
		}
	}
	memo.keep(now, 11 * kAge);
	EXPECT_FALSE(memo.hasVerdict(kSource)) << "the epoch moved and it is old enough, though the epoch stands now";
	EXPECT_TRUE(memo.hasVerdict(kOther)) << "younger than kVerdictMinTicks";
	memo.keep(now, 11 * kAge + 5);
	EXPECT_FALSE(memo.hasVerdict(kOther)) << "the epoch moved after it was recorded, and it is old enough";

	memo.recordVerdict(kOther, now, 12 * kAge);
	memo.keep(now, 20 * kAge);
	EXPECT_TRUE(memo.hasVerdict(kOther)) << "recorded after the epoch last moved";
	memo.keep(beliefMoved, 20 * kAge + 1);
	EXPECT_FALSE(memo.hasVerdict(kOther)) << "his belief moved";
	EXPECT_EQ(memo.verdictCount(), 0U);
}

TEST(MemoryQueries, SourceReachMemoStaysBounded) {
	ecs::SourceReachMemo			memo;
	const ecs::SourceReachMemo::Key now{.connectivityEpoch = 1, .beliefVersion = 1};
	for (uint64_t target = 0; target < 3 * ecs::SourceReachMemo::kMaxVerdicts; ++target) {
		memo.recordVerdict(target, now, target);
		ASSERT_LE(memo.verdictCount(), ecs::SourceReachMemo::kMaxVerdicts);
	}
}
