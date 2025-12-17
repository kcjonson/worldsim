#include "SpatialIndex.h"

#include <cmath>

namespace engine::assets {

	SpatialIndex::SpatialIndex(float cellSize)
		: m_cellSize(cellSize) {}

	void SpatialIndex::insert(const PlacedEntity& entity) {
		int64_t key = getCellKey(entity.position);
		m_cells[key].push_back(entity);
		++m_entityCount;
	}

	bool SpatialIndex::remove(glm::vec2 position, const std::string& defName) {
		int64_t key = getCellKey(position);
		auto	cellIt = m_cells.find(key);
		if (cellIt == m_cells.end()) {
			return false;
		}

		auto& entities = cellIt->second;
		// Small tolerance for floating point comparison
		constexpr float kEpsilon = 0.001F;

		for (auto it = entities.begin(); it != entities.end(); ++it) {
			// Match by defName and approximate position
			if (it->defName == defName) {
				glm::vec2 diff = it->position - position;
				float	  distSq = diff.x * diff.x + diff.y * diff.y;
				if (distSq < kEpsilon * kEpsilon) {
					entities.erase(it);
					--m_entityCount;

					// Remove empty cell to keep map clean
					if (entities.empty()) {
						m_cells.erase(cellIt);
					}
					return true;
				}
			}
		}

		return false;
	}

	void SpatialIndex::clear() {
		m_cells.clear();
		m_entityCount = 0;
	}

	std::vector<const PlacedEntity*> SpatialIndex::queryRadius(glm::vec2 center, float radius) const {
		std::vector<const PlacedEntity*> result;
		const float					   radiusSq = radius * radius;

		for (int64_t key : getCellsInRadius(center, radius)) {
			auto cellIt = m_cells.find(key);
			if (cellIt != m_cells.end()) {
				for (const auto& entity : cellIt->second) {
					glm::vec2 diff = entity.position - center;
					float	  distSq = diff.x * diff.x + diff.y * diff.y;
					if (distSq <= radiusSq) {
						result.push_back(&entity);
					}
				}
			}
		}

		return result;
	}

	std::vector<const PlacedEntity*> SpatialIndex::queryRadius(glm::vec2 center, float radius,
															   const std::string& defName) const {
		std::vector<const PlacedEntity*> result;
		const float					   radiusSq = radius * radius;

		for (int64_t key : getCellsInRadius(center, radius)) {
			auto cellIt = m_cells.find(key);
			if (cellIt != m_cells.end()) {
				for (const auto& entity : cellIt->second) {
					if (entity.defName != defName) {
						continue;
					}
					glm::vec2 diff = entity.position - center;
					float	  distSq = diff.x * diff.x + diff.y * diff.y;
					if (distSq <= radiusSq) {
						result.push_back(&entity);
					}
				}
			}
		}

		return result;
	}

	std::vector<const PlacedEntity*> SpatialIndex::queryRadiusGroup(
		glm::vec2 center, float radius, const std::unordered_set<std::string>& defNames) const {
		std::vector<const PlacedEntity*> result;
		const float					   radiusSq = radius * radius;

		for (int64_t key : getCellsInRadius(center, radius)) {
			auto cellIt = m_cells.find(key);
			if (cellIt != m_cells.end()) {
				for (const auto& entity : cellIt->second) {
					if (defNames.find(entity.defName) == defNames.end()) {
						continue;
					}
					glm::vec2 diff = entity.position - center;
					float	  distSq = diff.x * diff.x + diff.y * diff.y;
					if (distSq <= radiusSq) {
						result.push_back(&entity);
					}
				}
			}
		}

		return result;
	}

	bool SpatialIndex::hasNearby(glm::vec2 center, float radius, const std::string& defName) const {
		const float radiusSq = radius * radius;

		for (int64_t key : getCellsInRadius(center, radius)) {
			auto cellIt = m_cells.find(key);
			if (cellIt != m_cells.end()) {
				for (const auto& entity : cellIt->second) {
					if (entity.defName != defName) {
						continue;
					}
					glm::vec2 diff = entity.position - center;
					float	  distSq = diff.x * diff.x + diff.y * diff.y;
					if (distSq <= radiusSq) {
						return true;
					}
				}
			}
		}

		return false;
	}

	bool SpatialIndex::hasNearbyGroup(glm::vec2 center, float radius,
									  const std::unordered_set<std::string>& defNames) const {
		const float radiusSq = radius * radius;

		for (int64_t key : getCellsInRadius(center, radius)) {
			auto cellIt = m_cells.find(key);
			if (cellIt != m_cells.end()) {
				for (const auto& entity : cellIt->second) {
					if (defNames.find(entity.defName) == defNames.end()) {
						continue;
					}
					glm::vec2 diff = entity.position - center;
					float	  distSq = diff.x * diff.x + diff.y * diff.y;
					if (distSq <= radiusSq) {
						return true;
					}
				}
			}
		}

		return false;
	}

	int64_t SpatialIndex::getCellKey(glm::vec2 pos) const {
		auto [cellX, cellY] = getCellCoords(pos);
		return getCellKey(cellX, cellY);
	}

	int64_t SpatialIndex::getCellKey(int32_t cellX, int32_t cellY) {
		// Combine two 32-bit coordinates into one 64-bit key
		// This avoids hash collisions from separate hashing
		return (static_cast<int64_t>(cellX) << 32) | (static_cast<uint32_t>(cellY));
	}

	std::pair<int32_t, int32_t> SpatialIndex::getCellCoords(glm::vec2 pos) const {
		int32_t cellX = static_cast<int32_t>(std::floor(pos.x / m_cellSize));
		int32_t cellY = static_cast<int32_t>(std::floor(pos.y / m_cellSize));
		return {cellX, cellY};
	}

	std::vector<PlacedEntity> SpatialIndex::allEntities() const {
		std::vector<PlacedEntity> result;
		result.reserve(m_entityCount);
		for (const auto& [key, entities] : m_cells) {
			for (const auto& entity : entities) {
				result.push_back(entity);
			}
		}
		return result;
	}

	std::vector<const PlacedEntity*> SpatialIndex::queryRect(float minX, float minY,
															 float maxX, float maxY) const {
		std::vector<const PlacedEntity*> result;

		// Calculate bounding box of cells to check
		int32_t minCellX = static_cast<int32_t>(std::floor(minX / m_cellSize));
		int32_t maxCellX = static_cast<int32_t>(std::floor(maxX / m_cellSize));
		int32_t minCellY = static_cast<int32_t>(std::floor(minY / m_cellSize));
		int32_t maxCellY = static_cast<int32_t>(std::floor(maxY / m_cellSize));

		for (int32_t cx = minCellX; cx <= maxCellX; ++cx) {
			for (int32_t cy = minCellY; cy <= maxCellY; ++cy) {
				auto cellIt = m_cells.find(getCellKey(cx, cy));
				if (cellIt != m_cells.end()) {
					for (const auto& entity : cellIt->second) {
						// Check if entity is actually within bounds
						if (entity.position.x >= minX && entity.position.x <= maxX &&
							entity.position.y >= minY && entity.position.y <= maxY) {
							result.push_back(&entity);
						}
					}
				}
			}
		}

		return result;
	}

	std::vector<int64_t> SpatialIndex::getCellsInRadius(glm::vec2 center, float radius) const {
		std::vector<int64_t> keys;

		// Calculate bounding box of cells to check
		int32_t minCellX = static_cast<int32_t>(std::floor((center.x - radius) / m_cellSize));
		int32_t maxCellX = static_cast<int32_t>(std::floor((center.x + radius) / m_cellSize));
		int32_t minCellY = static_cast<int32_t>(std::floor((center.y - radius) / m_cellSize));
		int32_t maxCellY = static_cast<int32_t>(std::floor((center.y + radius) / m_cellSize));

		// Reserve approximate capacity
		keys.reserve(static_cast<size_t>((maxCellX - minCellX + 1) * (maxCellY - minCellY + 1)));

		for (int32_t cx = minCellX; cx <= maxCellX; ++cx) {
			for (int32_t cy = minCellY; cy <= maxCellY; ++cy) {
				keys.push_back(getCellKey(cx, cy));
			}
		}

		return keys;
	}

} // namespace engine::assets
