#pragma once

// TileAdjacency - Utilities for working with tile neighbor data.
//
// Each tile stores its neighbor surface types in a 64-bit adjacency field, read
// for shore detection (is water adjacent?).
//
// Bit layout (6 bits per direction, 48 bits used, 16 spare):
// [NW:6][W:6][SW:6][S:6][SE:6][E:6][NE:6][N:6][spare:16]
//  0-5   6-11 12-17 18-23 24-29 30-35 36-41 42-47 48-63

#include <cstdint>

namespace engine::world::TileAdjacency {

	/// Bits allocated per direction (supports up to 64 tile types)
	constexpr int kBitsPerDirection = 6;

	/// Mask for extracting a single direction's value
	constexpr uint64_t kDirectionMask = 0x3F; // 6 bits = 0b111111

	/// Direction indices for the adjacency field.
	/// Ordered clockwise starting from NW, with cardinals and ordinals interleaved.
	enum Direction : uint8_t {
		NW = 0, // North-West (diagonal)
		W = 1,	// West (cardinal)
		SW = 2, // South-West (diagonal)
		S = 3,	// South (cardinal)
		SE = 4, // South-East (diagonal)
		E = 5,	// East (cardinal)
		NE = 6, // North-East (diagonal)
		N = 7	// North (cardinal)
	};

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
		adj &= ~(kDirectionMask << shift); // Clear existing bits
		adj |= (static_cast<uint64_t>(surfaceType) & kDirectionMask) << shift;
	}

	/// Check if any cardinal direction (N/E/S/W) has the specified surface type.
	/// Used for shore detection - a tile is a shore if it has water in any cardinal direction.
	/// @param adj The adjacency field from TileData
	/// @param surfaceId The surface type to check for (e.g., Surface::Water)
	/// @return true if any cardinal neighbor matches
	[[nodiscard]] inline bool hasAdjacentSurface(uint64_t adj, uint8_t surfaceId) {
		return getNeighbor(adj, N) == surfaceId || getNeighbor(adj, E) == surfaceId || getNeighbor(adj, S) == surfaceId ||
			   getNeighbor(adj, W) == surfaceId;
	}

} // namespace engine::world::TileAdjacency
