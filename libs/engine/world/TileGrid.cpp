// TileGrid Implementation

#include "world/TileGrid.h"

namespace engine::world {

TileGrid::TileGrid(const TileGridConfig& config) : m_config(config) {
	// Reserve space for all tiles
	m_tiles.reserve(static_cast<size_t>(config.width * config.height));

	// Create tiles row by row
	for (int32_t y = 0; y < config.height; ++y) {
		for (int32_t x = 0; x < config.width; ++x) {
			Tile tile;
			tile.gridX = x;
			tile.gridY = y;
			tile.worldPos = {
				config.origin.x + static_cast<float>(x) * config.tileSize,
				config.origin.y + static_cast<float>(y) * config.tileSize
			};
			tile.width = config.tileSize;
			tile.height = config.tileSize;
			// Default biome is Grassland (from Tile default)
			m_tiles.push_back(tile);
		}
	}
}

Tile* TileGrid::getTile(int32_t gridX, int32_t gridY) {
	if (gridX < 0 || gridX >= m_config.width || gridY < 0 || gridY >= m_config.height) {
		return nullptr;
	}
	return &m_tiles[static_cast<size_t>(gridY * m_config.width + gridX)];
}

const Tile* TileGrid::getTile(int32_t gridX, int32_t gridY) const {
	if (gridX < 0 || gridX >= m_config.width || gridY < 0 || gridY >= m_config.height) {
		return nullptr;
	}
	return &m_tiles[static_cast<size_t>(gridY * m_config.width + gridX)];
}

Tile* TileGrid::getTileAtWorld(const Foundation::Vec2& worldPos) {
	// Calculate grid coordinates from world position
	float relX = worldPos.x - m_config.origin.x;
	float relY = worldPos.y - m_config.origin.y;

	if (relX < 0 || relY < 0) {
		return nullptr;
	}

	auto gridX = static_cast<int32_t>(relX / m_config.tileSize);
	auto gridY = static_cast<int32_t>(relY / m_config.tileSize);

	return getTile(gridX, gridY);
}

const Tile* TileGrid::getTileAtWorld(const Foundation::Vec2& worldPos) const {
	float relX = worldPos.x - m_config.origin.x;
	float relY = worldPos.y - m_config.origin.y;

	if (relX < 0 || relY < 0) {
		return nullptr;
	}

	auto gridX = static_cast<int32_t>(relX / m_config.tileSize);
	auto gridY = static_cast<int32_t>(relY / m_config.tileSize);

	return getTile(gridX, gridY);
}

void TileGrid::setAllBiomes(Biome biome) {
	for (auto& tile : m_tiles) {
		tile.setBiome(biome);
	}
}

}  // namespace engine::world
