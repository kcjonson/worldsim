#pragma once

// Spatial Index for Entity Placement
// Grid-based spatial hash for O(1) neighbor queries during entity placement.
// Used to check nearby entities for relationship-based spawn probability.

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <glm/vec2.hpp>

namespace engine::assets {

	/// A placed entity in the world
	struct PlacedEntity {
		std::string defName;   // Asset definition name
		glm::vec2	position;  // World position in tiles
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
