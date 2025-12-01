#pragma once

// Biome types for world representation.
// These are engine-defined and NOT moddable. Assets reference biomes by name.

#include <cstdint>

namespace engine::world {

/// Biome types - determines what assets can spawn on a tile.
enum class Biome : uint8_t {
	Grassland,
	Forest,
	Desert,
	Tundra,
	Wetland,
	Mountain,
	Beach,
	Ocean,

	Count  // For iteration/validation
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

/// Convert string name to biome enum (returns Grassland if not found)
constexpr Biome stringToBiome(const char* name) {
	// Simple string comparison for constexpr compatibility
	auto streq = [](const char* a, const char* b) -> bool {
		while (*a && *b) {
			if (*a != *b) {
				return false;
			}
			++a;
			++b;
		}
		return *a == *b;
	};

	if (streq(name, "Grassland")) {
		return Biome::Grassland;
	}
	if (streq(name, "Forest")) {
		return Biome::Forest;
	}
	if (streq(name, "Desert")) {
		return Biome::Desert;
	}
	if (streq(name, "Tundra")) {
		return Biome::Tundra;
	}
	if (streq(name, "Wetland")) {
		return Biome::Wetland;
	}
	if (streq(name, "Mountain")) {
		return Biome::Mountain;
	}
	if (streq(name, "Beach")) {
		return Biome::Beach;
	}
	if (streq(name, "Ocean")) {
		return Biome::Ocean;
	}
	return Biome::Grassland;  // Default fallback
}

}  // namespace engine::world
