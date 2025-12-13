#pragma once

// TilePostProcessor - Post-generation processing for tile data.
//
// Runs after all tiles in a chunk have been assigned their initial surface types.
// Responsible for:
// 1. Generating mud around water bodies (with organic gaps)
// 2. Computing adjacency data for all tiles
//
// The processing happens during Chunk::generate() after the initial tile loop.

#include "world/chunk/ChunkCoordinate.h"

#include <array>
#include <cstdint>

namespace engine::world {

// Forward declaration
struct TileData;

class TilePostProcessor {
  public:
	/// Post-process tiles after initial surface assignment.
	/// - Converts some tiles near water to Mud
	/// - Computes adjacency for all tiles
	///
	/// @param tiles The tile array to process (modified in place)
	/// @param seed World seed for deterministic mud generation
	static void process(std::array<TileData, kChunkSize * kChunkSize>& tiles, uint64_t seed);

  private:
	// ============ Mud Generation Parameters ============
	// These are tunable for visual tweaking

	/// Maximum distance from water where mud can appear (in tiles)
	static constexpr int kMudMaxDistance = 3;

	/// Probability that a tile near water becomes mud (0.0 - 1.0)
	/// Higher values = more complete mud rings around water
	static constexpr float kMudProbability = 0.95F;

	// ============ Internal Methods ============

	/// Generate mud around water bodies.
	/// Converts eligible Soil/Dirt tiles near water to Mud.
	static void generateMud(std::array<TileData, kChunkSize * kChunkSize>& tiles, uint64_t seed);

	/// Compute adjacency for all tiles.
	/// Sets the adjacency field based on neighbor surface types.
	static void computeAdjacency(std::array<TileData, kChunkSize * kChunkSize>& tiles);

	/// Check if a tile at (x, y) is within distance of water
	/// @return Distance to nearest water, or -1 if no water within kMudMaxDistance
	static int distanceToWater(
		const std::array<TileData, kChunkSize * kChunkSize>& tiles, uint16_t x, uint16_t y
	);

	/// Simple hash for deterministic mud generation
	static uint32_t hash(uint16_t x, uint16_t y, uint64_t seed);
};

}  // namespace engine::world
