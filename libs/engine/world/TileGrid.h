#pragma once

// TileGrid - Grid container for tiles.
// Manages creation and access of a 2D grid of tiles.
// Future: Could delegate to IChunkProvider for streaming.

#include "world/Tile.h"

#include <vector>

namespace engine::world {

/// Configuration for creating a tile grid
struct TileGridConfig {
	int32_t			 width = 10;		// Tiles in X direction
	int32_t			 height = 10;		// Tiles in Y direction
	float			 tileSize = 64.0F;	// Size of each tile in world units
	Foundation::Vec2 origin{0, 0};		// World position of grid's bottom-left corner
};

/// A 2D grid of tiles.
/// Provides convenient access to tiles by grid coordinate or world position.
class TileGrid {
  public:
	/// Default constructor - creates empty grid
	TileGrid() = default;

	/// Create a tile grid with the given configuration
	explicit TileGrid(const TileGridConfig& config);

	/// Get tile at grid coordinates (returns nullptr if out of bounds)
	[[nodiscard]] Tile* getTile(int32_t gridX, int32_t gridY);
	[[nodiscard]] const Tile* getTile(int32_t gridX, int32_t gridY) const;

	/// Get tile containing world position (returns nullptr if outside grid)
	[[nodiscard]] Tile* getTileAtWorld(const Foundation::Vec2& worldPos);
	[[nodiscard]] const Tile* getTileAtWorld(const Foundation::Vec2& worldPos) const;

	/// Set all tiles to the same biome (convenience for single-biome grids)
	void setAllBiomes(Biome biome);

	/// Access all tiles for iteration
	[[nodiscard]] const std::vector<Tile>& tiles() const { return m_tiles; }
	[[nodiscard]] std::vector<Tile>& tiles() { return m_tiles; }

	/// Grid dimensions
	[[nodiscard]] int32_t width() const { return m_config.width; }
	[[nodiscard]] int32_t height() const { return m_config.height; }
	[[nodiscard]] float tileSize() const { return m_config.tileSize; }
	[[nodiscard]] size_t tileCount() const { return m_tiles.size(); }

	/// Get grid origin (bottom-left corner in world coordinates)
	[[nodiscard]] Foundation::Vec2 origin() const { return m_config.origin; }

	/// Check if grid is empty
	[[nodiscard]] bool empty() const { return m_tiles.empty(); }

  private:
	TileGridConfig	   m_config;
	std::vector<Tile>  m_tiles;
};

}  // namespace engine::world
