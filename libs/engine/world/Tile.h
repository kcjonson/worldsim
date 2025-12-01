#pragma once

// Tile and Biome types for world representation
// Tiles are the fundamental unit of the game world grid.

#include <math/Types.h>

#include <cstdint>

namespace engine::world {

	/// Biome types - determines what assets can spawn on a tile.
	/// These are engine-defined and NOT moddable. Assets reference biomes by name.
	enum class Biome : uint8_t {
		Grassland,
		Forest,
		Desert,
		Tundra,
		Wetland,
		Mountain,
		Beach,
		Ocean,

		Count // For iteration/validation
	};

	/// Convert biome enum to string name (for asset definition matching)
	constexpr const char* biomeToString(Biome biome) {
		switch (biome) {
			case Biome::Grassland:
				return "Grassland";
			case Biome::Forest:
				return "Forest";
			case Biome::Desert:
				return "Desert";
			case Biome::Tundra:
				return "Tundra";
			case Biome::Wetland:
				return "Wetland";
			case Biome::Mountain:
				return "Mountain";
			case Biome::Beach:
				return "Beach";
			case Biome::Ocean:
				return "Ocean";
			default:
				return "Unknown";
		}
	}

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

		/// Biome type determines what assets can spawn here
		Biome biome = Biome::Grassland;

		/// Get the center position of this tile in world coordinates
		[[nodiscard]] Foundation::Vec2 center() const { return {worldPos.x + width / 2.0F, worldPos.y + height / 2.0F}; }

		/// Check if a world position is within this tile
		[[nodiscard]] bool contains(const Foundation::Vec2& pos) const {
			return pos.x >= worldPos.x && pos.x < worldPos.x + width && pos.y >= worldPos.y && pos.y < worldPos.y + height;
		}
	};

} // namespace engine::world
