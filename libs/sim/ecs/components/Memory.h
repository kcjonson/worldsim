#pragma once

// Memory Component for Colonist Knowledge System
// Optimized for millions of entities with:
// - String interning (uint32_t defNameId instead of std::string)
// - Capability-indexed storage for O(1) capability queries
// - Fixed capacity with LRU eviction
// See /docs/design/game-systems/colonists/memory.md for design details.

#include "../EntityID.h"

#include "assets/AssetRegistry.h"
#include "assets/placement/DecimeterGrid.h"

#include <glm/vec2.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ecs {

	/// Default sight radius for colonists in meters.
	/// Centralized constant - change this value to adjust all colonist sight ranges.
	inline constexpr float kDefaultSightRadius = 30.0F;

	/// A known world entity (static PlacedEntity from SpatialIndex)
	/// Optimized: uses defNameId instead of string (~28 bytes saved per entity)
	struct KnownWorldEntity {
		uint32_t  defNameId;		  // Asset definition ID from AssetRegistry::getDefNameId()
		glm::vec2 position;			  // World position in meters
		uint32_t  defSlot = 0;		  // Index of this entity's key in its RememberedOfDefinition::keys
		uint32_t  cellSlot = 0;		  // Index of this entity's key in its MemoryCell::keys
		uint16_t  capabilityMask = 0; // The capabilities it was remembered with (its capability-index sets)
	};

	/// The side of a square cell of the grid remembered things are filed under, per definition.
	inline constexpr float kMemoryCellMeters = 16.0F;

	/// One cell of a definition's grid: the keys of its remembered things that lie in it, in no order.
	struct MemoryCell {
		int32_t				  x = 0;
		int32_t				  y = 0;
		std::vector<uint64_t> keys; // swap-removed: KnownWorldEntity::cellSlot is each key's index
	};

	/// The cell a position lies in, as one key for MemoryCell lookups.
	[[nodiscard]] inline int32_t memoryCellCoordinate(float meters) {
		return static_cast<int32_t>(std::floor(meters / kMemoryCellMeters));
	}
	[[nodiscard]] inline uint64_t memoryCellKey(int32_t x, int32_t y) {
		return (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32) | static_cast<uint32_t>(y);
	}

	/// What a creature remembers of one definition: every key in no order, and the same keys by the grid
	/// cell they lie in, so a walk nearest first opens only the cells near enough to matter
	/// (/docs/technical/queries-and-chains.md, Evaluation).
	struct RememberedOfDefinition {
		std::vector<uint64_t>				   keys;	  // swap-removed: KnownWorldEntity::defSlot is each key's index
		std::vector<MemoryCell>				   cells;	  // only cells holding a key; swap-removed when emptied
		std::unordered_map<uint64_t, uint32_t> cellIndex; // memoryCellKey -> index in cells
		/// How many of its things carry each capability, and while one does, the definition's index in
		/// Memory::definitionsWithCapability for it.
		std::array<uint32_t, assets::kCapabilityNames.size()> capabilityCounts{};
		std::array<uint32_t, assets::kCapabilityNames.size()> capabilitySlots{};

		[[nodiscard]] const MemoryCell* cellAt(int32_t x, int32_t y) const {
			const auto found = cellIndex.find(memoryCellKey(x, y));
			return found == cellIndex.end() ? nullptr : &cells[found->second];
		}
	};

	/// A mover the colonist has seen (another colonist; animals later), as it was at the last sighting.
	/// The entry is a snapshot: the mover may have walked on or no longer exist, and nothing here reaches
	/// into the world. entityId carries its generation, so a mover born into a reused index is another entry.
	struct KnownDynamicEntity {
		EntityID  entityId = kInvalidEntity;
		uint32_t  defNameId = 0;				 // what kind of mover: the asset def it appears as ("Human")
		glm::vec2 lastKnownPosition{0.0F, 0.0F}; // world position in meters at the last sighting
		double	  lastSeenAt = 0.0;				 // TimeSystem::simulationSeconds (game minutes) of the last sighting
	};

	/// Hash for world entity key (position + defNameId)
	/// Uses the position's cell on the 0.1 m grid (assets::decimeterCell) combined with defNameId
	inline uint64_t hashWorldEntity(const glm::vec2& position, uint32_t defNameId) {
		const assets::DecimeterCell cell = assets::decimeterCell(position);
		// Pack defNameId and quantized position into 64-bit key
		// This is more efficient than string hashing
		// Cast through uint32_t to preserve bit pattern without sign extension
		uint64_t posHash = (static_cast<uint64_t>(static_cast<uint32_t>(cell.x)) << 32) | static_cast<uint32_t>(cell.y);
		return posHash ^ (static_cast<uint64_t>(defNameId) * 0x9e3779b97f4a7c15ULL);
	}

	/// Memory component - stores a colonist's knowledge of the world.
	/// Colonists can only interact with entities they know about.
	///
	/// Performance optimizations:
	/// - String interning: stores uint32_t defNameId instead of std::string
	/// - Capability indexing: separate sets per capability for O(1) queries
	/// - LRU eviction: bounded memory with oldest-first eviction
	struct Memory {
		/// Maximum number of world entities a colonist can remember
		static constexpr size_t kMaxWorldEntities = 10000;

		/// Maximum number of movers a colonist can remember; past it the one seen longest ago is forgotten
		static constexpr size_t kMaxDynamicEntities = 512;

		/// Number of capability types (must match AssetRegistry::kCapabilityTypeCount)
		static constexpr size_t kCapabilityTypeCount = 10;

		// --- Owner ---

		/// Entity ID of the colonist that owns this memory
		EntityID owner = 0;

		// --- Primary Storage ---

		/// Known static world entities (from SpatialIndex)
		/// Key: hash of position + defNameId
		std::unordered_map<uint64_t, KnownWorldEntity> knownWorldEntities;

		/// Bumped whenever a world entity is remembered for the first time or forgotten, so a decision can
		/// tell by one compare that the set of remembered sources hasn't changed since it last looked.
		std::uint64_t worldEntityVersion = 0;

		/// Movers seen (colonists; animals later), keyed by EntityID
		std::unordered_map<EntityID, KnownDynamicEntity> knownDynamicEntities;

		// --- Capability Index (for O(1) capability queries) ---

		/// Per-capability sets of entity keys
		/// capabilityIndex[capabilityType] contains all entity keys with that capability
		std::array<std::unordered_set<uint64_t>, kCapabilityTypeCount> capabilityIndex;

		/// Every remembered world entity by def: its keys and its grid. Kept beside the capability index
		/// (same remember/forget points) so a question about one item visits only that item's entries, and a
		/// query walks them nearest first.
		std::unordered_map<uint32_t, RememberedOfDefinition> worldEntitiesByDef;

		/// The defs at least one remembered thing of which carries each capability, in no order.
		std::array<std::vector<uint32_t>, kCapabilityTypeCount> definitionsWithCapability;

		// --- LRU Eviction ---
		//
		// Index-linked LRU in flat vectors. Components live by value in the ECS
		// ComponentPool's dense vector, which relocates them on growth (adding a
		// second colonist) and move-assigns them on swap-remove — so a component
		// must survive being copied or moved wholesale. The previous std::list +
		// map-of-list-iterators scheme did not: a copy's iterators still pointed
		// into the SOURCE component's list nodes, and the first touchLRU after a
		// pool reallocation erased a freed node (heap corruption, crashed the game
		// whenever a second colonist spawned). Indices stay meaningful in a copy;
		// no pointers, nothing to dangle.

		static constexpr uint32_t kLruNull = 0xFFFFFFFFU;
		struct LruNode {
			uint64_t key = 0;
			uint32_t prev = kLruNull;
			uint32_t next = kLruNull;
		};

		/// The LRU bookkeeping as one unit so its move operations can uphold the
		/// invariant that head/tail always index into THIS instance's nodes. A
		/// member-wise move would empty the containers but copy the indices,
		/// leaving the moved-from side pointing past an empty node pool — an
		/// out-of-bounds write on reuse, the same corruption family the
		/// index-linked rewrite exists to prevent. Moves reset the source to
		/// empty, so a moved-from Memory stays safely reusable.
		struct Lru {
			std::vector<LruNode>				   nodes; // unlinked slots recycled via freeSlots
			std::vector<uint32_t>				   freeSlots;
			uint32_t							   head = kLruNull; // oldest (evict first)
			uint32_t							   tail = kLruNull; // newest
			std::unordered_map<uint64_t, uint32_t> map;				// key -> nodes index

			Lru() = default;
			Lru(const Lru&) = default;
			Lru& operator=(const Lru&) = default;
			Lru(Lru&& other)
				: nodes(std::move(other.nodes)),
				  freeSlots(std::move(other.freeSlots)),
				  head(other.head),
				  tail(other.tail),
				  map(std::move(other.map)) {
				other.reset();
			}
			Lru& operator=(Lru&& other) {
				if (this != &other) {
					nodes = std::move(other.nodes);
					freeSlots = std::move(other.freeSlots);
					head = other.head;
					tail = other.tail;
					map = std::move(other.map);
					other.reset();
				}
				return *this;
			}

			/// Back to the empty state. clear() on the (possibly moved-from)
			/// containers guarantees empty rather than "valid but unspecified".
			void reset() {
				nodes.clear();
				freeSlots.clear();
				head = kLruNull;
				tail = kLruNull;
				map.clear();
			}
		};
		Lru lru;

		// --- Configuration ---

		/// Sight radius in meters (MVP: simple circle, sees through walls)
		float sightRadius = kDefaultSightRadius;

		// --- Query Methods ---

		/// Check if a world entity at position with defNameId is known
		[[nodiscard]] bool knowsWorldEntity(const glm::vec2& position, uint32_t defNameId) const {
			uint64_t key = ecs::hashWorldEntity(position, defNameId);
			return knownWorldEntities.find(key) != knownWorldEntities.end();
		}

		/// Check if a dynamic entity is known
		[[nodiscard]] bool knowsDynamicEntity(EntityID entityId) const {
			return knownDynamicEntities.find(entityId) != knownDynamicEntities.end();
		}

		// --- Mutation Methods ---

		/// Add a world entity to memory with capability indexing and LRU tracking
		/// @param position World position of the entity
		/// @param defNameId Asset definition ID from AssetRegistry::getDefNameId()
		/// @param capabilityMask Bitmask of capabilities from AssetRegistry::getCapabilityMask()
		/// @return true if this was a NEW discovery, false if already known (just touched LRU)
		bool rememberWorldEntity(const glm::vec2& position, uint32_t defNameId, uint16_t capabilityMask) {
			// Skip entities with no capabilities - they're purely decorative (grass, etc.)
			// and have no gameplay relevance for colonist decision-making
			if (capabilityMask == 0) {
				return false;
			}

			uint64_t key = ecs::hashWorldEntity(position, defNameId);

			// Check if already known - if so, just update LRU position
			auto it = knownWorldEntities.find(key);
			if (it != knownWorldEntities.end()) {
				touchLRU(key);
				return false; // Already known
			}

			// Evict oldest entries if at capacity
			while (knownWorldEntities.size() >= kMaxWorldEntities && lru.head != kLruNull) {
				evictOldest();
			}

			// Add to primary storage
			RememberedOfDefinition& ofDef = worldEntitiesByDef[defNameId];
			const int32_t			cellX = memoryCellCoordinate(position.x);
			const int32_t			cellY = memoryCellCoordinate(position.y);
			const auto [cellAt, newCell] =
				ofDef.cellIndex.try_emplace(memoryCellKey(cellX, cellY), static_cast<uint32_t>(ofDef.cells.size()));
			if (newCell) {
				ofDef.cells.push_back(MemoryCell{.x = cellX, .y = cellY, .keys = {}});
			}
			MemoryCell& cell = ofDef.cells[cellAt->second];
			knownWorldEntities[key] = KnownWorldEntity{
				.defNameId = defNameId,
				.position = position,
				.defSlot = static_cast<uint32_t>(ofDef.keys.size()),
				.cellSlot = static_cast<uint32_t>(cell.keys.size()),
				.capabilityMask = capabilityMask
			};
			ofDef.keys.push_back(key);
			cell.keys.push_back(key);
			for (size_t cap = 0; cap < kCapabilityTypeCount; ++cap) {
				if ((capabilityMask & (1 << cap)) != 0 && ofDef.capabilityCounts[cap]++ == 0) {
					ofDef.capabilitySlots[cap] = static_cast<uint32_t>(definitionsWithCapability[cap].size());
					definitionsWithCapability[cap].push_back(defNameId);
				}
			}
			++worldEntityVersion;

			// Add to capability indices
			for (size_t cap = 0; cap < kCapabilityTypeCount; ++cap) {
				if ((capabilityMask & (1 << cap)) != 0) {
					capabilityIndex[cap].insert(key);
				}
			}

			// Add to LRU list (tail = newest)
			lru.map[key] = lruLinkTail(key);
			return true; // New discovery
		}

		/// Record a sighting of a mover: a new entry, or its entry moved to where and when it was seen. A new
		/// mover past kMaxDynamicEntities forgets the one seen longest ago (ties: the lowest id).
		void rememberDynamicEntity(EntityID entityId, uint32_t defNameId, const glm::vec2& position, double seenAt) {
			const KnownDynamicEntity sighting{.entityId = entityId, .defNameId = defNameId, .lastKnownPosition = position, .lastSeenAt = seenAt};
			if (auto found = knownDynamicEntities.find(entityId); found != knownDynamicEntities.end()) {
				found->second = sighting;
				return;
			}
			if (knownDynamicEntities.size() >= kMaxDynamicEntities) {
				auto oldest = knownDynamicEntities.begin();
				for (auto it = knownDynamicEntities.begin(); it != knownDynamicEntities.end(); ++it) {
					const KnownDynamicEntity& known = it->second;
					const KnownDynamicEntity& current = oldest->second;
					if (known.lastSeenAt < current.lastSeenAt || (known.lastSeenAt == current.lastSeenAt && known.entityId < current.entityId)) {
						oldest = it;
					}
				}
				knownDynamicEntities.erase(oldest);
			}
			knownDynamicEntities.emplace(entityId, sighting);
		}

		/// Hand `visit` each remembered mover of kind `defNameId` last seen within `windowGameMinutes` before
		/// `now` (both on TimeSystem::simulationSeconds), in no order, until it returns false. An older
		/// sighting is where someone was, not company. The usual window is
		/// assets::MemoryConfig::recentSightingGameMinutes().
		template <typename Visit>
		void forEachDynamicEntitySeenWithin(uint32_t defNameId, double now, double windowGameMinutes, Visit&& visit) const {
			const double since = now - windowGameMinutes;
			for (const auto& [entityId, known] : knownDynamicEntities) {
				if (known.defNameId == defNameId && known.lastSeenAt >= since && !visit(known)) {
					return;
				}
			}
		}

		/// Forget a world entity (e.g., when it's destroyed or proven stale)
		void forgetWorldEntity(const glm::vec2& position, uint32_t defNameId) {
			removeEntity(ecs::hashWorldEntity(position, defNameId));
		}

		/// Clear all memory
		void clear() {
			knownWorldEntities.clear();
			++worldEntityVersion;
			knownDynamicEntities.clear();
			for (auto& index : capabilityIndex) {
				index.clear();
			}
			worldEntitiesByDef.clear();
			for (auto& definitions : definitionsWithCapability) {
				definitions.clear();
			}
			lru.reset();
			knownSegments.clear();
			knownOpenings.clear();
			// Clearing structural belief is a belief change: bump so any path planned
			// against the old belief is detected as stale. Bump (not reset to 0) keeps
			// the stamp monotonic, so an old NavPath stamp can never accidentally match.
			++beliefVersion;
		}

		// --- Capability Query Methods ---

		/// Get all known entity keys with a specific capability
		/// @param capability The capability type to query
		/// @return Reference to set of entity keys (do not modify)
		[[nodiscard]] const std::unordered_set<uint64_t>& getEntitiesWithCapability(assets::CapabilityType capability) const {
			size_t idx = static_cast<size_t>(capability);
			if (idx >= kCapabilityTypeCount) {
				static const std::unordered_set<uint64_t> empty;
				return empty;
			}
			return capabilityIndex[idx];
		}

		/// The keys of the remembered world entities of one def, in no order.
		[[nodiscard]] std::span<const uint64_t> worldEntitiesOfDef(uint32_t defNameId) const {
			const auto it = worldEntitiesByDef.find(defNameId);
			return it != worldEntitiesByDef.end() ? std::span<const uint64_t>(it->second.keys) : std::span<const uint64_t>();
		}

		/// What is remembered of one def, keys and grid; nullptr when nothing of it ever was.
		[[nodiscard]] const RememberedOfDefinition* rememberedOfDefinition(uint32_t defNameId) const {
			const auto it = worldEntitiesByDef.find(defNameId);
			return it != worldEntitiesByDef.end() ? &it->second : nullptr;
		}

		/// The defs at least one remembered thing of which carries `capability`, in no order.
		[[nodiscard]] std::span<const uint32_t> definitionsWith(assets::CapabilityType capability) const {
			const auto index = static_cast<size_t>(capability);
			return index < kCapabilityTypeCount ? std::span<const uint32_t>(definitionsWithCapability[index]) : std::span<const uint32_t>();
		}

		/// Get the KnownWorldEntity for a given key
		/// @param key The entity key from capability index
		/// @return Pointer to entity, or nullptr if not found
		[[nodiscard]] const KnownWorldEntity* getWorldEntity(uint64_t key) const {
			auto it = knownWorldEntities.find(key);
			if (it != knownWorldEntities.end()) {
				return &it->second;
			}
			return nullptr;
		}

		// --- Statistics ---

		/// Get total count of known entities
		[[nodiscard]] size_t totalKnown() const { return knownWorldEntities.size() + knownDynamicEntities.size(); }

		/// Get count of known world entities
		[[nodiscard]] size_t worldEntityCount() const { return knownWorldEntities.size(); }

		/// Get count of known entities with a specific capability
		[[nodiscard]] size_t countWithCapability(assets::CapabilityType capability) const {
			size_t idx = static_cast<size_t>(capability);
			if (idx >= kCapabilityTypeCount) {
				return 0;
			}
			return capabilityIndex[idx].size();
		}

		// --- Known Structures (walls / openings) ---
		//
		// Structures are observables, not just occluders (vision-architecture D4): a
		// wall segment or opening the colonist has seen is recorded here by its stable
		// construction id. Discovery granularity = segment/opening id, matching belief
		// filtering. Keyed by plain uint64 because construction SegmentId/OpeningId are
		// uint64 typedefs; using the raw type here avoids pulling a heavy construction
		// header in. The ids are stable, never reused, and bounded (hundreds), so a
		// plain set suffices -- no LRU/eviction like the world entities above.

		std::unordered_set<std::uint64_t> knownSegments;
		std::unordered_set<std::uint64_t> knownOpenings;

		// Monotonic "my structural belief changed" stamp. Bumped whenever a segment or
		// opening is actually discovered or forgotten (not on a no-op re-remember). A
		// stored NavPath records the beliefVersion it was planned against, so the AI can
		// detect that the route is stale by a cheap integer compare instead of re-querying
		// the mesh every tick (see AIDecisionSystem's replan-on-discovery loop).
		std::uint64_t beliefVersion = 0;

		/// Record a seen wall segment. Returns true on first discovery (like
		/// rememberWorldEntity), false if already known. Bumps beliefVersion on discovery.
		bool rememberSegment(std::uint64_t id) {
			if (knownSegments.insert(id).second) {
				++beliefVersion;
				return true;
			}
			return false;
		}

		/// Record a seen opening. Returns true on first discovery, false if known.
		/// Bumps beliefVersion on discovery.
		bool rememberOpening(std::uint64_t id) {
			if (knownOpenings.insert(id).second) {
				++beliefVersion;
				return true;
			}
			return false;
		}

		[[nodiscard]] bool knowsSegment(std::uint64_t id) const { return knownSegments.count(id) != 0; }

		[[nodiscard]] bool knowsOpening(std::uint64_t id) const { return knownOpenings.count(id) != 0; }

		// Wired for future demolish-reconciliation (a removed structure leaves memory).
		// Bump beliefVersion only when something was actually erased, so a repath is
		// triggered just like a discovery.
		void forgetSegment(std::uint64_t id) {
			if (knownSegments.erase(id) != 0) {
				++beliefVersion;
			}
		}
		void forgetOpening(std::uint64_t id) {
			if (knownOpenings.erase(id) != 0) {
				++beliefVersion;
			}
		}

		[[nodiscard]] size_t knownSegmentCount() const { return knownSegments.size(); }
		[[nodiscard]] size_t knownOpeningCount() const { return knownOpenings.size(); }

	  private:
		/// Detach node `idx` from the linked order (slot stays allocated).
		void lruUnlink(uint32_t idx) {
			LruNode& n = lru.nodes[idx];
			if (n.prev != kLruNull) {
				lru.nodes[n.prev].next = n.next;
			} else {
				lru.head = n.next;
			}
			if (n.next != kLruNull) {
				lru.nodes[n.next].prev = n.prev;
			} else {
				lru.tail = n.prev;
			}
			n.prev = kLruNull;
			n.next = kLruNull;
		}

		/// Allocate (or recycle) a node for `key` and link it at the tail (newest).
		/// Returns the node index.
		uint32_t lruLinkTail(uint64_t key) {
			uint32_t idx = kLruNull;
			if (!lru.freeSlots.empty()) {
				idx = lru.freeSlots.back();
				lru.freeSlots.pop_back();
				lru.nodes[idx] = LruNode{key, kLruNull, kLruNull};
			} else {
				idx = static_cast<uint32_t>(lru.nodes.size());
				lru.nodes.push_back(LruNode{key, kLruNull, kLruNull});
			}
			lru.nodes[idx].prev = lru.tail;
			if (lru.tail != kLruNull) {
				lru.nodes[lru.tail].next = idx;
			} else {
				lru.head = idx;
			}
			lru.tail = idx;
			return idx;
		}

		/// Update LRU position for an entity (move to tail = most recently accessed)
		void touchLRU(uint64_t key) {
			auto mapIt = lru.map.find(key);
			if (mapIt == lru.map.end() || mapIt->second == lru.tail) {
				return;
			}
			const uint32_t idx = mapIt->second;
			lruUnlink(idx);
			lru.nodes[idx].prev = lru.tail;
			if (lru.tail != kLruNull) {
				lru.nodes[lru.tail].next = idx;
			} else {
				lru.head = idx;
			}
			lru.tail = idx;
		}

		/// Evict the oldest entity from memory
		void evictOldest() {
			if (lru.head == kLruNull) {
				return;
			}
			uint64_t oldestKey = lru.nodes[lru.head].key;
			removeEntity(oldestKey);
		}

		/// Remove an entity from all data structures
		void removeEntity(uint64_t key) {
			// Remove from primary storage
			auto it = knownWorldEntities.find(key);
			if (it != knownWorldEntities.end()) {
				// Remove from capability indices
				for (auto& index : capabilityIndex) {
					index.erase(key);
				}
				const KnownWorldEntity& known = it->second;
				RememberedOfDefinition& ofDef = worldEntitiesByDef.find(known.defNameId)->second;
				const uint32_t			slot = known.defSlot;
				const uint64_t			moved = ofDef.keys.back();
				ofDef.keys[slot] = moved;
				ofDef.keys.pop_back();
				if (moved != key) {
					knownWorldEntities.find(moved)->second.defSlot = slot;
				}

				const auto cellAt =
					ofDef.cellIndex.find(memoryCellKey(memoryCellCoordinate(known.position.x), memoryCellCoordinate(known.position.y)));
				const uint32_t cellIndex = cellAt->second;
				MemoryCell&	   cell = ofDef.cells[cellIndex];
				const uint64_t movedInCell = cell.keys.back();
				cell.keys[known.cellSlot] = movedInCell;
				cell.keys.pop_back();
				if (movedInCell != key) {
					knownWorldEntities.find(movedInCell)->second.cellSlot = known.cellSlot;
				}
				if (cell.keys.empty()) {
					ofDef.cellIndex.erase(cellAt);
					if (cellIndex + 1 != ofDef.cells.size()) {
						ofDef.cells[cellIndex] = std::move(ofDef.cells.back());
						ofDef.cellIndex[memoryCellKey(ofDef.cells[cellIndex].x, ofDef.cells[cellIndex].y)] = cellIndex;
					}
					ofDef.cells.pop_back();
				}

				for (size_t cap = 0; cap < kCapabilityTypeCount; ++cap) {
					if ((known.capabilityMask & (1 << cap)) == 0 || --ofDef.capabilityCounts[cap] != 0) {
						continue;
					}
					std::vector<uint32_t>& definitions = definitionsWithCapability[cap];
					const uint32_t		   capabilitySlot = ofDef.capabilitySlots[cap];
					const uint32_t		   movedDefinition = definitions.back();
					definitions[capabilitySlot] = movedDefinition;
					definitions.pop_back();
					if (movedDefinition != known.defNameId) {
						worldEntitiesByDef.find(movedDefinition)->second.capabilitySlots[cap] = capabilitySlot;
					}
				}
				knownWorldEntities.erase(it);
				++worldEntityVersion;
			}

			// Remove from LRU tracking (recycle the node slot)
			auto lruIt = lru.map.find(key);
			if (lruIt != lru.map.end()) {
				lruUnlink(lruIt->second);
				lru.freeSlots.push_back(lruIt->second);
				lru.map.erase(lruIt);
			}
		}
	};

} // namespace ecs
