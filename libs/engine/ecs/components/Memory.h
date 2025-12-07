#pragma once

// Memory Component for Colonist Knowledge System
// Stores what entities a colonist knows about through observation.
// See /docs/design/game-systems/colonists/memory.md for design details.

#include "../EntityID.h"

#include <glm/vec2.hpp>

#include <string>
#include <unordered_map>
#include <unordered_set>

namespace ecs {

/// A known world entity (static PlacedEntity from SpatialIndex)
struct KnownWorldEntity {
	std::string defName;	// Asset definition name (e.g., "BerryBush")
	glm::vec2	position;	// World position in meters
};

/// A known dynamic entity (ECS entity like other colonists, animals)
struct KnownDynamicEntity {
	EntityID  entityId;		   // ECS entity ID
	glm::vec2 lastKnownPosition; // Last observed position (for mobile entities)
};

/// Memory component - stores a colonist's knowledge of the world.
/// Colonists can only interact with entities they know about.
struct Memory {
	/// Hash function for world entity keys (position-based)
	/// Uses position with 0.1m precision to deduplicate nearby placements
	static uint64_t hashWorldEntity(const glm::vec2& position, const std::string& defName) {
		// Quantize position to 0.1m grid to handle floating point
		auto qx = static_cast<int32_t>(position.x * 10.0F);
		auto qy = static_cast<int32_t>(position.y * 10.0F);
		// Combine position hash with string hash
		uint64_t posHash = (static_cast<uint64_t>(qx) << 32) | static_cast<uint64_t>(qy);
		uint64_t nameHash = std::hash<std::string>{}(defName);
		return posHash ^ (nameHash + 0x9e3779b9 + (posHash << 6) + (posHash >> 2));
	}

	/// Known static world entities (from SpatialIndex)
	/// Key: hash of position + defName
	std::unordered_map<uint64_t, KnownWorldEntity> knownWorldEntities;

	/// Known dynamic ECS entities (colonists, animals, etc.)
	/// Key: EntityID
	std::unordered_map<EntityID, KnownDynamicEntity> knownDynamicEntities;

	/// Sight radius in meters (MVP: simple circle, sees through walls)
	float sightRadius = 10.0F;

	/// Check if a world entity at position with defName is known
	[[nodiscard]] bool knowsWorldEntity(const glm::vec2& position, const std::string& defName) const {
		uint64_t key = hashWorldEntity(position, defName);
		return knownWorldEntities.find(key) != knownWorldEntities.end();
	}

	/// Check if a dynamic entity is known
	[[nodiscard]] bool knowsDynamicEntity(EntityID entityId) const {
		return knownDynamicEntities.find(entityId) != knownDynamicEntities.end();
	}

	/// Add a world entity to memory
	void rememberWorldEntity(const glm::vec2& position, const std::string& defName) {
		uint64_t key = hashWorldEntity(position, defName);
		knownWorldEntities[key] = KnownWorldEntity{defName, position};
	}

	/// Add or update a dynamic entity in memory
	void rememberDynamicEntity(EntityID entityId, const glm::vec2& position) {
		knownDynamicEntities[entityId] = KnownDynamicEntity{entityId, position};
	}

	/// Get total count of known entities
	[[nodiscard]] size_t totalKnown() const {
		return knownWorldEntities.size() + knownDynamicEntities.size();
	}
};

} // namespace ecs
