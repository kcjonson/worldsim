#pragma once

// Spatial Index for Entity Placement
//
// A grid-based spatial hash that provides O(1) average-case neighbor queries.
// This is a critical performance optimization for entity placement, where each
// potential spawn position must check for nearby entities to evaluate
// relationship-based probability modifiers.
//
// How It Works:
// - World space is divided into a grid of cells (default 4x4 tiles each)
// - Entities are stored in the cell containing their position
// - Queries check only cells that could contain entities within the radius
// - Cell size should be >= max relationship radius for best performance
//
// Key Operations:
// - insert(): O(1) - add entity to appropriate cell
// - remove(): O(n) within cell - remove specific entity by position+defName
// - hasNearby(): O(k) - check if any entity of type exists in radius (k = cells checked)
// - queryRadius(): O(k*m) - get all entities in radius (m = entities per cell)
//
// Used By:
// - PlacementExecutor: relationship checks during spawning
// - VisionSystem: entity discovery queries
// - AI systems: finding nearest resource of type
//
// Memory: One vector per occupied cell. Empty cells use no memory.
// Thread Safety: NOT thread-safe. Use separate instances per thread for parallel generation.

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

namespace engine::assets {

	/// A placed entity in the world
	struct PlacedEntity {
		std::string defName;		// Asset definition name
		glm::vec2	position;		// World position in tiles
		float		rotation = 0.0F; // Rotation in radians
		float		scale = 1.0F;	// Scale factor
		glm::vec4	colorTint{1.0F, 1.0F, 1.0F, 1.0F}; // RGBA color tint
	};

	/// Grid-based spatial index for efficient neighbor queries.
	/// Cells are square with configurable size.
	class SpatialIndex {
	  public:
		/// Create spatial index with given cell size
		/// @param cellSize Size of each grid cell in tiles (default 4.0)
		explicit SpatialIndex(float cellSize = 4.0F);

		/// Insert an entity into the index
		void insert(const PlacedEntity& entity);

		/// Remove an entity at specific position with matching defName
		/// @param position Entity position to remove
		/// @param defName Asset definition name to match
		/// @return true if entity was found and removed
		bool remove(glm::vec2 position, const std::string& defName);

		/// Clear all entities from the index
		void clear();

		/// Get total number of entities in the index
		[[nodiscard]] size_t size() const { return m_entityCount; }

		/// Find all entities within radius of center
		/// @param center Query center position
		/// @param radius Search radius in tiles
		/// @returns Pointers to entities within radius (valid until next modification)
		[[nodiscard]] std::vector<const PlacedEntity*> queryRadius(glm::vec2 center, float radius) const;

		/// Find all entities of specific defName within radius
		/// @param center Query center position
		/// @param radius Search radius in tiles
		/// @param defName Asset definition name to filter by
		[[nodiscard]] std::vector<const PlacedEntity*> queryRadius(glm::vec2 center, float radius,
																   const std::string& defName) const;

		/// Find all entities belonging to any of the specified defNames within radius
		/// @param center Query center position
		/// @param radius Search radius in tiles
		/// @param defNames Set of asset definition names (typically a group's members)
		[[nodiscard]] std::vector<const PlacedEntity*> queryRadiusGroup(
			glm::vec2 center, float radius, const std::unordered_set<std::string>& defNames) const;

		/// Check if any entity of specified defName exists within radius
		/// @param center Query center position
		/// @param radius Search radius in tiles
		/// @param defName Asset definition name to check for
		[[nodiscard]] bool hasNearby(glm::vec2 center, float radius, const std::string& defName) const;

		/// Check if any entity from the specified defNames exists within radius
		/// @param center Query center position
		/// @param radius Search radius in tiles
		/// @param defNames Set of asset definition names to check for
		[[nodiscard]] bool hasNearbyGroup(glm::vec2 center, float radius,
										  const std::unordered_set<std::string>& defNames) const;

		/// Get all entities in the index for rendering/iteration
		/// @returns Vector of all entities (copies for safe iteration)
		[[nodiscard]] std::vector<PlacedEntity> allEntities() const;

		/// Query all entities within an axis-aligned bounding box
		/// @param minX Minimum X coordinate in tiles
		/// @param minY Minimum Y coordinate in tiles
		/// @param maxX Maximum X coordinate in tiles
		/// @param maxY Maximum Y coordinate in tiles
		/// @returns Pointers to entities within bounds (valid until next modification)
		[[nodiscard]] std::vector<const PlacedEntity*> queryRect(float minX, float minY,
																 float maxX, float maxY) const;

	  private:
		float  m_cellSize;
		size_t m_entityCount = 0;

		// Cell storage: hash(cellX, cellY) → entities in that cell
		std::unordered_map<int64_t, std::vector<PlacedEntity>> m_cells;

		/// Get cell key from world position
		[[nodiscard]] int64_t getCellKey(glm::vec2 pos) const;

		/// Get cell key from cell coordinates
		[[nodiscard]] static int64_t getCellKey(int32_t cellX, int32_t cellY);

		/// Get cell coordinates from world position
		[[nodiscard]] std::pair<int32_t, int32_t> getCellCoords(glm::vec2 pos) const;

		/// Get all cell keys that could contain entities within radius
		[[nodiscard]] std::vector<int64_t> getCellsInRadius(glm::vec2 center, float radius) const;
	};

} // namespace engine::assets
