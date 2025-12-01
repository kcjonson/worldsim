#pragma once

// Tile - Fundamental unit of the game world grid.
// Tiles have a fixed size in world units and contain biome information.
// Supports percentage-based biome blending for transition zones.

#include "world/Biome.h"
#include "world/BiomeWeights.h"

#include <math/Types.h>

namespace engine::world {

/// A single tile in the world grid.
/// Tiles have a fixed size in world units and contain biome information.
struct Tile {
	/// Tile grid coordinates (integer position in tile grid)
	int32_t gridX = 0;
	int32_t gridY = 0;

	/// World position of tile's bottom-left corner (in pixels/world units)
	Foundation::Vec2 worldPos{0.0F, 0.0F};

	/// Tile dimensions in world units
	float width = 64.0F;
	float height = 64.0F;

	/// Biome weights - supports blended tiles (e.g., 70% grassland, 30% forest)
	BiomeWeights biomeWeights;

	// ─────────────────────────────────────────────────────────────────────────
	// Convenience methods for single-biome tiles (preserves existing patterns)
	// ─────────────────────────────────────────────────────────────────────────

	/// Set tile to 100% single biome
	void setBiome(Biome biome) { biomeWeights = BiomeWeights::single(biome); }

	/// Get primary (dominant) biome
	[[nodiscard]] Biome primaryBiome() const { return biomeWeights.primary(); }

	/// Check if a biome is present (any weight > 0)
	[[nodiscard]] bool hasBiome(Biome biome) const { return biomeWeights.has(biome); }

	// ─────────────────────────────────────────────────────────────────────────
	// Position utilities
	// ─────────────────────────────────────────────────────────────────────────

	/// Get the center position of this tile in world coordinates
	[[nodiscard]] Foundation::Vec2 center() const { return {worldPos.x + width / 2.0F, worldPos.y + height / 2.0F}; }

	/// Check if a world position is within this tile
	[[nodiscard]] bool contains(const Foundation::Vec2& pos) const {
		return pos.x >= worldPos.x && pos.x < worldPos.x + width && pos.y >= worldPos.y && pos.y < worldPos.y + height;
	}
};

}  // namespace engine::world
