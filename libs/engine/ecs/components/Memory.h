#pragma once

// Memory Component for Colonist Knowledge System
// Optimized for millions of entities with:
// - String interning (uint32_t defNameId instead of std::string)
// - Capability-indexed storage for O(1) capability queries
// - Fixed capacity with LRU eviction
// See /docs/design/game-systems/colonists/memory.md for design details.

#include "../EntityID.h"

#include "assets/AssetRegistry.h"

#include <glm/vec2.hpp>

#include <array>
#include <cstdint>
#include <list>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ecs {

	/// Default sight radius for colonists in meters.
	/// Centralized constant - change this value to adjust all colonist sight ranges.
	inline constexpr float kDefaultSightRadius = 10.0F;

	/// A known world entity (static PlacedEntity from SpatialIndex)
	/// Optimized: uses defNameId instead of string (~28 bytes saved per entity)
	struct KnownWorldEntity {
		uint32_t  defNameId; // Asset definition ID from AssetRegistry::getDefNameId()
		glm::vec2 position;	 // World position in meters
	};

	/// A known dynamic entity (ECS entity like other colonists, animals)
	struct KnownDynamicEntity {
		EntityID  entityId;			 // ECS entity ID
		glm::vec2 lastKnownPosition; // Last observed position (for mobile entities)
	};

	/// Hash for world entity key (position + defNameId)
	/// Uses quantized position (0.1m grid) combined with defNameId
	inline uint64_t hashWorldEntity(const glm::vec2& position, uint32_t defNameId) {
		auto qx = static_cast<int32_t>(position.x * 10.0F);
		auto qy = static_cast<int32_t>(position.y * 10.0F);
		// Pack defNameId and quantized position into 64-bit key
		// This is more efficient than string hashing
		// Cast through uint32_t to preserve bit pattern without sign extension
		uint64_t posHash = (static_cast<uint64_t>(static_cast<uint32_t>(qx)) << 32) | static_cast<uint32_t>(qy);
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

		/// Number of capability types (must match AssetRegistry::kCapabilityTypeCount)
		static constexpr size_t kCapabilityTypeCount = 4;

		// --- Primary Storage ---

		/// Known static world entities (from SpatialIndex)
		/// Key: hash of position + defNameId
		std::unordered_map<uint64_t, KnownWorldEntity> knownWorldEntities;

		/// Known dynamic ECS entities (colonists, animals, etc.)
		/// Key: EntityID
		std::unordered_map<EntityID, KnownDynamicEntity> knownDynamicEntities;

		// --- Capability Index (for O(1) capability queries) ---

		/// Per-capability sets of entity keys
		/// capabilityIndex[capabilityType] contains all entity keys with that capability
		std::array<std::unordered_set<uint64_t>, kCapabilityTypeCount> capabilityIndex;

		// --- LRU Eviction ---

		/// LRU order: front = oldest (evict first), back = newest
		std::list<uint64_t> lruOrder;

		/// Map from entity key to LRU list iterator (for O(1) updates)
		std::unordered_map<uint64_t, std::list<uint64_t>::iterator> lruMap;

		// --- Configuration ---

		/// Sight radius in meters (MVP: simple circle, sees through walls)
		float sightRadius = kDefaultSightRadius;

		// --- Legacy API (string-based, for compatibility) ---

		/// Hash function for world entity keys using string defName
		/// @deprecated Use the uint32_t defNameId version instead
		static uint64_t hashWorldEntity(const glm::vec2& position, const std::string& defName) {
			auto qx = static_cast<int32_t>(position.x * 10.0F);
			auto qy = static_cast<int32_t>(position.y * 10.0F);
			// Cast through uint32_t to preserve bit pattern without sign extension
			uint64_t posHash = (static_cast<uint64_t>(static_cast<uint32_t>(qx)) << 32) | static_cast<uint32_t>(qy);
			uint64_t nameHash = std::hash<std::string>{}(defName);
			return posHash ^ (nameHash + 0x9e3779b9 + (posHash << 6) + (posHash >> 2));
		}

		// --- Query Methods ---

		/// Check if a world entity at position with defNameId is known
		[[nodiscard]] bool knowsWorldEntity(const glm::vec2& position, uint32_t defNameId) const {
			uint64_t key = ecs::hashWorldEntity(position, defNameId);
			return knownWorldEntities.find(key) != knownWorldEntities.end();
		}

		/// Check if a world entity at position with defName is known (string version)
		/// @deprecated Use the defNameId version for better performance
		[[nodiscard]] bool knowsWorldEntity(const glm::vec2& position, const std::string& defName) const {
			// Convert to defNameId to use the same hash as rememberWorldEntity
			auto&	 registry = engine::assets::AssetRegistry::Get();
			uint32_t defNameId = registry.getDefNameId(defName);
			if (defNameId == 0) {
				return false; // Unknown defName
			}
			return knowsWorldEntity(position, defNameId);
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
		void rememberWorldEntity(const glm::vec2& position, uint32_t defNameId, uint8_t capabilityMask) {
			uint64_t key = ecs::hashWorldEntity(position, defNameId);

			// Check if already known - if so, just update LRU position
			auto it = knownWorldEntities.find(key);
			if (it != knownWorldEntities.end()) {
				touchLRU(key);
				return;
			}

			// Evict oldest entries if at capacity
			while (knownWorldEntities.size() >= kMaxWorldEntities && !lruOrder.empty()) {
				evictOldest();
			}

			// Add to primary storage
			knownWorldEntities[key] = KnownWorldEntity{defNameId, position};

			// Add to capability indices
			for (size_t cap = 0; cap < kCapabilityTypeCount; ++cap) {
				if ((capabilityMask & (1 << cap)) != 0) {
					capabilityIndex[cap].insert(key);
				}
			}

			// Add to LRU list (back = newest)
			lruOrder.push_back(key);
			lruMap[key] = std::prev(lruOrder.end());
		}

		/// Add a world entity to memory (string version, converts to ID)
		/// @deprecated Use the defNameId version for better performance
		void rememberWorldEntity(const glm::vec2& position, const std::string& defName) {
			auto&	 registry = engine::assets::AssetRegistry::Get();
			uint32_t defNameId = registry.getDefNameId(defName);
			if (defNameId == 0) {
				return; // Unknown defName
			}
			uint8_t capabilityMask = registry.getCapabilityMask(defNameId);
			rememberWorldEntity(position, defNameId, capabilityMask);
		}

		/// Add or update a dynamic entity in memory
		void rememberDynamicEntity(EntityID entityId, const glm::vec2& position) {
			knownDynamicEntities[entityId] = KnownDynamicEntity{entityId, position};
		}

		/// Forget a world entity (e.g., when it's destroyed)
		void forgetWorldEntity(const glm::vec2& position, uint32_t defNameId) {
			uint64_t key = ecs::hashWorldEntity(position, defNameId);
			removeEntity(key);
		}

		/// Clear all memory
		void clear() {
			knownWorldEntities.clear();
			knownDynamicEntities.clear();
			for (auto& index : capabilityIndex) {
				index.clear();
			}
			lruOrder.clear();
			lruMap.clear();
		}

		// --- Capability Query Methods ---

		/// Get all known entity keys with a specific capability
		/// @param capability The capability type to query
		/// @return Reference to set of entity keys (do not modify)
		[[nodiscard]] const std::unordered_set<uint64_t>& getEntitiesWithCapability(engine::assets::CapabilityType capability) const {
			size_t idx = static_cast<size_t>(capability);
			if (idx >= kCapabilityTypeCount) {
				static const std::unordered_set<uint64_t> empty;
				return empty;
			}
			return capabilityIndex[idx];
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
		[[nodiscard]] size_t countWithCapability(engine::assets::CapabilityType capability) const {
			size_t idx = static_cast<size_t>(capability);
			if (idx >= kCapabilityTypeCount) {
				return 0;
			}
			return capabilityIndex[idx].size();
		}

	  private:
		/// Update LRU position for an entity (move to back = most recently accessed)
		void touchLRU(uint64_t key) {
			auto mapIt = lruMap.find(key);
			if (mapIt != lruMap.end()) {
				lruOrder.erase(mapIt->second);
				lruOrder.push_back(key);
				mapIt->second = std::prev(lruOrder.end());
			}
		}

		/// Evict the oldest entity from memory
		void evictOldest() {
			if (lruOrder.empty()) {
				return;
			}
			uint64_t oldestKey = lruOrder.front();
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
				knownWorldEntities.erase(it);
			}

			// Remove from LRU tracking
			auto lruIt = lruMap.find(key);
			if (lruIt != lruMap.end()) {
				lruOrder.erase(lruIt->second);
				lruMap.erase(lruIt);
			}
		}
	};

} // namespace ecs
