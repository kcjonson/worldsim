#pragma once

// GenerationResult - Output from biome generators
// Contains the surface type and optional metadata.

#include "world/chunk/Chunk.h" // For Surface enum

#include <cstdint>

namespace engine::world::generation {

/// Result from a biome generator.
/// Primary output is surface type; moisture and attributes are optional overrides.
struct GenerationResult {
	Surface surface = Surface::Grass; ///< Selected surface type
	uint8_t moisture = 128;           ///< Moisture level (0-255), used if generator wants to override
	uint8_t attributes = 0;           ///< Reserved for future use
};

} // namespace engine::world::generation
