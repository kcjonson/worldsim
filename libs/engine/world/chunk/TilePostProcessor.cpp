#include "TilePostProcessor.h"

#include "world/chunk/Chunk.h"
#include "world/chunk/TileAdjacency.h"

#include <vector>

namespace engine::world {

void TilePostProcessor::process(std::array<TileData, kChunkSize * kChunkSize>& tiles, uint64_t seed) {
	// Step 1: Generate mud around water bodies
	generateMud(tiles, seed);

	// Step 2: Compute adjacency for all tiles
	computeAdjacency(tiles);
}

void TilePostProcessor::generateMud(std::array<TileData, kChunkSize * kChunkSize>& tiles, uint64_t seed) {
	// Flood-fill mud generation: ensures contiguous mud rings around water.
	// Each wave can only extend from existing mud, preventing gaps in the middle.

	std::array<bool, kChunkSize * kChunkSize> isMud{};

	// Helper to check if tile at (x,y) is eligible for mud conversion
	auto canBeMud = [&](int x, int y) -> bool {
		if (x < 0 || x >= kChunkSize || y < 0 || y >= kChunkSize) {
			return false;
		}
		size_t idx = y * kChunkSize + x;
		if (isMud[idx]) {
			return false;  // Already mud
		}
		Surface s = tiles[idx].surface;
		return s == Surface::Soil || s == Surface::Dirt;
	};

	// Helper to check if tile at (x,y) is water
	auto isWater = [&](int x, int y) -> bool {
		if (x < 0 || x >= kChunkSize || y < 0 || y >= kChunkSize) {
			return false;
		}
		return tiles[y * kChunkSize + x].surface == Surface::Water;
	};

	// Helper to check if tile has adjacent mud
	auto hasAdjacentMud = [&](int x, int y) -> bool {
		return (x > 0 && isMud[y * kChunkSize + (x - 1)]) ||
		       (x < kChunkSize - 1 && isMud[y * kChunkSize + (x + 1)]) ||
		       (y > 0 && isMud[(y - 1) * kChunkSize + x]) ||
		       (y < kChunkSize - 1 && isMud[(y + 1) * kChunkSize + x]);
	};

	// Wave 1: Tiles directly adjacent to water (always mud)
	for (uint16_t y = 0; y < kChunkSize; ++y) {
		for (uint16_t x = 0; x < kChunkSize; ++x) {
			if (!canBeMud(x, y)) {
				continue;
			}

			// Check cardinal neighbors for water
			bool adjacentToWater = isWater(x - 1, y) || isWater(x + 1, y) ||
			                       isWater(x, y - 1) || isWater(x, y + 1);

			if (adjacentToWater) {
				isMud[y * kChunkSize + x] = true;
			}
		}
	}

	// Waves 2+: Extend mud outward with decreasing probability
	// Each wave only extends from existing mud, ensuring contiguity
	for (int wave = 2; wave <= kMudMaxDistance; ++wave) {
		// Probability decreases with each wave
		float probability = kMudProbability - (static_cast<float>(wave - 1) * 0.15F);

		// Collect candidates first (don't modify while iterating)
		std::vector<size_t> candidates;

		for (uint16_t y = 0; y < kChunkSize; ++y) {
			for (uint16_t x = 0; x < kChunkSize; ++x) {
				if (!canBeMud(x, y)) {
					continue;
				}

				// Only extend from existing mud (ensures contiguity)
				if (hasAdjacentMud(x, y)) {
					// Deterministic random check
					uint32_t h = hash(x, y, seed + static_cast<uint64_t>(wave) * 1000);
					float	 roll = static_cast<float>(h) / static_cast<float>(UINT32_MAX);

					if (roll < probability) {
						candidates.push_back(y * kChunkSize + x);
					}
				}
			}
		}

		// Apply this wave's mud
		for (size_t idx : candidates) {
			isMud[idx] = true;
		}
	}

	// Final pass: apply mud to tiles
	for (size_t idx = 0; idx < tiles.size(); ++idx) {
		if (isMud[idx]) {
			tiles[idx].surface = Surface::Mud;
		}
	}
}

void TilePostProcessor::computeAdjacency(std::array<TileData, kChunkSize * kChunkSize>& tiles) {
	// For each tile, sample neighbors in all 8 directions
	// Note: Tiles at chunk boundaries will have 0 for out-of-bounds neighbors

	for (uint16_t y = 0; y < kChunkSize; ++y) {
		for (uint16_t x = 0; x < kChunkSize; ++x) {
			size_t	 idx = y * kChunkSize + x;
			uint64_t adj = 0;

			// Helper to get surface at offset, or 0 if out of bounds
			auto getSurfaceAt = [&](int dx, int dy) -> uint8_t {
				int nx = static_cast<int>(x) + dx;
				int ny = static_cast<int>(y) + dy;

				if (nx < 0 || nx >= kChunkSize || ny < 0 || ny >= kChunkSize) {
					return 0;  // Out of bounds - return 0 (will be treated as unknown)
				}

				return static_cast<uint8_t>(tiles[ny * kChunkSize + nx].surface);
			};

			// Set each direction
			// Direction order: NW=0, W=1, SW=2, S=3, SE=4, E=5, NE=6, N=7
			TileAdjacency::setNeighbor(adj, TileAdjacency::NW, getSurfaceAt(-1, -1));
			TileAdjacency::setNeighbor(adj, TileAdjacency::W, getSurfaceAt(-1, 0));
			TileAdjacency::setNeighbor(adj, TileAdjacency::SW, getSurfaceAt(-1, 1));
			TileAdjacency::setNeighbor(adj, TileAdjacency::S, getSurfaceAt(0, 1));
			TileAdjacency::setNeighbor(adj, TileAdjacency::SE, getSurfaceAt(1, 1));
			TileAdjacency::setNeighbor(adj, TileAdjacency::E, getSurfaceAt(1, 0));
			TileAdjacency::setNeighbor(adj, TileAdjacency::NE, getSurfaceAt(1, -1));
			TileAdjacency::setNeighbor(adj, TileAdjacency::N, getSurfaceAt(0, -1));

			tiles[idx].adjacency = adj;
		}
	}
}

int TilePostProcessor::distanceToWater(
	const std::array<TileData, kChunkSize * kChunkSize>& tiles, uint16_t x, uint16_t y
) {
	// Check in expanding rings around the tile
	for (int dist = 1; dist <= kMudMaxDistance; ++dist) {
		// Check all tiles at this manhattan distance
		for (int dx = -dist; dx <= dist; ++dx) {
			for (int dy = -dist; dy <= dist; ++dy) {
				// Only check tiles at exactly this distance (manhattan)
				if (std::abs(dx) + std::abs(dy) != dist) {
					continue;
				}

				int nx = static_cast<int>(x) + dx;
				int ny = static_cast<int>(y) + dy;

				if (nx < 0 || nx >= kChunkSize || ny < 0 || ny >= kChunkSize) {
					continue;
				}

				if (tiles[ny * kChunkSize + nx].surface == Surface::Water) {
					return dist;
				}
			}
		}
	}

	return -1;  // No water within range
}

uint32_t TilePostProcessor::hash(uint16_t x, uint16_t y, uint64_t seed) {
	// Simple hash combining position and seed
	uint64_t h = seed;
	h ^= static_cast<uint64_t>(x) * 0x85EBCA6BULL;
	h ^= static_cast<uint64_t>(y) * 0xC2B2AE35ULL;
	h ^= h >> 33;
	h *= 0xFF51AFD7ED558CCDULL;
	h ^= h >> 33;
	return static_cast<uint32_t>(h);
}

}  // namespace engine::world
