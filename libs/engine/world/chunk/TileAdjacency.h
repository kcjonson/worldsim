#pragma once

// TileAdjacency - Utilities for working with tile neighbor data.
//
// Each tile stores its neighbor surface types in a 64-bit adjacency field.
// This enables fast lookups for:
// - Shore detection (is water adjacent?)
// - Edge rendering (which edges need decoration?)
// - Future tile transitions (blending between surface types)
//
// Bit layout (6 bits per direction, 48 bits used, 16 spare):
// [NW:6][W:6][SW:6][S:6][SE:6][E:6][NE:6][N:6][spare:16]
//  0-5   6-11 12-17 18-23 24-29 30-35 36-41 42-47 48-63

#include <cstdint>

namespace engine::world::TileAdjacency {

/// Bits allocated per direction (supports up to 64 tile types)
constexpr int kBitsPerDirection = 6;

/// Mask for extracting a single direction's value
constexpr uint64_t kDirectionMask = 0x3F;  // 6 bits = 0b111111

/// Direction indices for the adjacency field.
/// Ordered clockwise starting from NW, with cardinals and ordinals interleaved.
enum Direction : uint8_t {
	NW = 0,  // North-West (diagonal)
	W = 1,   // West (cardinal)
	SW = 2,  // South-West (diagonal)
	S = 3,   // South (cardinal)
	SE = 4,  // South-East (diagonal)
	E = 5,   // East (cardinal)
	NE = 6,  // North-East (diagonal)
	N = 7    // North (cardinal)
};

/// Number of directions
constexpr int kDirectionCount = 8;

/// Cardinal direction mask (for shore detection - only N/E/S/W matter)
constexpr uint8_t kCardinalMask = 0x0F;  // N=0x01, E=0x02, S=0x04, W=0x08

/// Get the neighbor surface type at the specified direction.
/// @param adj The adjacency field from TileData
/// @param dir The direction to query
/// @return The surface type ID (0-63)
[[nodiscard]] inline uint8_t getNeighbor(uint64_t adj, Direction dir) {
	return static_cast<uint8_t>((adj >> (dir * kBitsPerDirection)) & kDirectionMask);
}

/// Set the neighbor surface type at the specified direction.
/// @param adj The adjacency field to modify (in/out)
/// @param dir The direction to set
/// @param surfaceType The surface type ID (0-63)
inline void setNeighbor(uint64_t& adj, Direction dir, uint8_t surfaceType) {
	int shift = dir * kBitsPerDirection;
	adj &= ~(kDirectionMask << shift);  // Clear existing bits
	adj |= (static_cast<uint64_t>(surfaceType) & kDirectionMask) << shift;
}

/// Check if any cardinal direction (N/E/S/W) has the specified surface type.
/// Used for shore detection - a tile is a shore if it has water in any cardinal direction.
/// @param adj The adjacency field from TileData
/// @param surfaceId The surface type to check for (e.g., Surface::Water)
/// @return true if any cardinal neighbor matches
[[nodiscard]] inline bool hasAdjacentSurface(uint64_t adj, uint8_t surfaceId) {
	return getNeighbor(adj, N) == surfaceId ||
	       getNeighbor(adj, E) == surfaceId ||
	       getNeighbor(adj, S) == surfaceId ||
	       getNeighbor(adj, W) == surfaceId;
}

/// Get a bitmask indicating which cardinal directions have the specified surface type.
/// Used for edge rendering - tells renderer which edges need decoration.
///
/// @param adj The adjacency field from TileData
/// @param surfaceId The surface type to check for (e.g., Surface::Water)
/// @return Bitmask: bit 0 = N, bit 1 = E, bit 2 = S, bit 3 = W
///
/// Example: If water is to the North and East, returns 0x03 (0b0011)
[[nodiscard]] inline uint8_t getCardinalEdgeMask(uint64_t adj, uint8_t surfaceId) {
	uint8_t mask = 0;
	if (getNeighbor(adj, N) == surfaceId) {
		mask |= 0x01;
	}
	if (getNeighbor(adj, E) == surfaceId) {
		mask |= 0x02;
	}
	if (getNeighbor(adj, S) == surfaceId) {
		mask |= 0x04;
	}
	if (getNeighbor(adj, W) == surfaceId) {
		mask |= 0x08;
	}
	return mask;
}

/// Edge mask bit positions (for use with getCardinalEdgeMask result)
namespace EdgeBit {
	constexpr uint8_t North = 0x01;
	constexpr uint8_t East = 0x02;
	constexpr uint8_t South = 0x04;
	constexpr uint8_t West = 0x08;
}  // namespace EdgeBit

/// Corner mask bit positions (for diagonal adjacency)
namespace CornerBit {
	constexpr uint8_t NW = 0x01;  // Top-left corner
	constexpr uint8_t NE = 0x02;  // Top-right corner
	constexpr uint8_t SE = 0x04;  // Bottom-right corner
	constexpr uint8_t SW = 0x08;  // Bottom-left corner
}  // namespace CornerBit

/// Surface stacking order - higher values are "on top" and draw edges over lower surfaces.
/// When a tile is adjacent to a lower-stacked surface, it draws an edge on that side.
[[nodiscard]] inline int getSurfaceStackOrder(uint8_t surfaceId) {
	// Stack order from bottom to top:
	// Water (0) < Mud (1) < Sand (2) < Dirt (3) < Soil (4) < Rock (5) < Snow (6)
	switch (surfaceId) {
		case 4:  return 0;  // Water - lowest
		case 6:  return 1;  // Mud
		case 2:  return 2;  // Sand
		case 1:  return 3;  // Dirt
		case 0:  return 4;  // Soil
		case 3:  return 5;  // Rock
		case 5:  return 6;  // Snow - highest
		default: return 4;  // Default to Soil level
	}
}

/// Get a bitmask indicating which cardinal edges need decoration based on stacking order.
/// An edge is drawn when the neighbor is LOWER in the stack than this tile.
///
/// @param adj The adjacency field from TileData
/// @param thisSurfaceId The surface type of the current tile
/// @return Bitmask: bit 0 = N, bit 1 = E, bit 2 = S, bit 3 = W
[[nodiscard]] inline uint8_t getEdgeMaskByStack(uint64_t adj, uint8_t thisSurfaceId) {
	uint8_t mask = 0;
	int thisStack = getSurfaceStackOrder(thisSurfaceId);

	if (getSurfaceStackOrder(getNeighbor(adj, N)) < thisStack) {
		mask |= EdgeBit::North;
	}
	if (getSurfaceStackOrder(getNeighbor(adj, E)) < thisStack) {
		mask |= EdgeBit::East;
	}
	if (getSurfaceStackOrder(getNeighbor(adj, S)) < thisStack) {
		mask |= EdgeBit::South;
	}
	if (getSurfaceStackOrder(getNeighbor(adj, W)) < thisStack) {
		mask |= EdgeBit::West;
	}
	return mask;
}

/// Get a bitmask indicating which corners need decoration based on stacking order.
/// A corner is drawn when the diagonal neighbor is LOWER in the stack, but the
/// adjacent cardinal neighbors are NOT lower (otherwise edge strokes cover it).
///
/// @param adj The adjacency field from TileData
/// @param thisSurfaceId The surface type of the current tile
/// @return Bitmask: bit 0 = NW, bit 1 = NE, bit 2 = SE, bit 3 = SW
[[nodiscard]] inline uint8_t getCornerMaskByStack(uint64_t adj, uint8_t thisSurfaceId) {
	uint8_t mask = 0;
	int thisStack = getSurfaceStackOrder(thisSurfaceId);

	auto isLower = [&](Direction dir) {
		return getSurfaceStackOrder(getNeighbor(adj, dir)) < thisStack;
	};

	// NW corner: diagonal is lower, but N and W are not lower
	if (isLower(NW) && !isLower(N) && !isLower(W)) {
		mask |= CornerBit::NW;
	}

	// NE corner: diagonal is lower, but N and E are not lower
	if (isLower(NE) && !isLower(N) && !isLower(E)) {
		mask |= CornerBit::NE;
	}

	// SE corner: diagonal is lower, but S and E are not lower
	if (isLower(SE) && !isLower(S) && !isLower(E)) {
		mask |= CornerBit::SE;
	}

	// SW corner: diagonal is lower, but S and W are not lower
	if (isLower(SW) && !isLower(S) && !isLower(W)) {
		mask |= CornerBit::SW;
	}

	return mask;
}

}  // namespace engine::world::TileAdjacency
