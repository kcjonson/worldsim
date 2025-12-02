#pragma once

// ChunkCoordinate - Integer grid coordinates for chunks.
// Chunks are 512×512 tiles (512m × 512m at 1m per tile).
// Provides hash specialization for use in std::unordered_map.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>

namespace engine::world {

/// Constants for chunk system
inline constexpr int32_t kChunkSize = 512;	// Tiles per chunk dimension
inline constexpr float kTileSize = 1.0F;	// Meters per tile

/// Corners of a chunk (for biome sampling)
enum class ChunkCorner : uint8_t {
	NorthWest = 0,
	NorthEast = 1,
	SouthWest = 2,
	SouthEast = 3
};

/// World position in continuous 2D space (meters from origin)
struct WorldPosition {
	float x = 0.0F;
	float y = 0.0F;

	WorldPosition() = default;
	WorldPosition(float px, float py) : x(px), y(py) {}

	WorldPosition operator+(const WorldPosition& other) const { return {x + other.x, y + other.y}; }
	WorldPosition operator-(const WorldPosition& other) const { return {x - other.x, y - other.y}; }
	WorldPosition operator*(float scalar) const { return {x * scalar, y * scalar}; }

	bool operator==(const WorldPosition& other) const { return x == other.x && y == other.y; }
};

/// Integer chunk grid coordinates.
/// Use as keys in std::unordered_map<ChunkCoordinate, Chunk>.
struct ChunkCoordinate {
	int32_t x = 0;
	int32_t y = 0;

	ChunkCoordinate() = default;
	ChunkCoordinate(int32_t cx, int32_t cy) : x(cx), y(cy) {}

	bool operator==(const ChunkCoordinate& other) const { return x == other.x && y == other.y; }
	bool operator!=(const ChunkCoordinate& other) const { return !(*this == other); }

	/// Get world position of chunk's origin (bottom-left corner)
	[[nodiscard]] WorldPosition origin() const {
		return {
			static_cast<float>(x * kChunkSize) * kTileSize,
			static_cast<float>(y * kChunkSize) * kTileSize
		};
	}

	/// Get world position of chunk's center
	[[nodiscard]] WorldPosition center() const {
		float halfChunk = static_cast<float>(kChunkSize) * kTileSize * 0.5F;
		WorldPosition org = origin();
		return {org.x + halfChunk, org.y + halfChunk};
	}

	/// Get world position of a corner
	[[nodiscard]] WorldPosition corner(ChunkCorner c) const {
		WorldPosition org = origin();
		float size = static_cast<float>(kChunkSize) * kTileSize;
		switch (c) {
			case ChunkCorner::NorthWest:
				return org;
			case ChunkCorner::NorthEast:
				return {org.x + size, org.y};
			case ChunkCorner::SouthWest:
				return {org.x, org.y + size};
			case ChunkCorner::SouthEast:
				return {org.x + size, org.y + size};
		}
		return org;
	}

	/// Manhattan distance to another chunk
	[[nodiscard]] int32_t manhattanDistance(const ChunkCoordinate& other) const {
		return std::abs(x - other.x) + std::abs(y - other.y);
	}

	/// Chebyshev distance (max of x/y difference) to another chunk
	[[nodiscard]] int32_t chebyshevDistance(const ChunkCoordinate& other) const {
		return std::max(std::abs(x - other.x), std::abs(y - other.y));
	}
};

/// Convert world position to chunk coordinate
[[nodiscard]] inline ChunkCoordinate worldToChunk(const WorldPosition& pos) {
	float chunkWorldSize = static_cast<float>(kChunkSize) * kTileSize;
	return {
		static_cast<int32_t>(std::floor(pos.x / chunkWorldSize)),
		static_cast<int32_t>(std::floor(pos.y / chunkWorldSize))
	};
}

/// Convert world position to local tile coordinates within a chunk
[[nodiscard]] inline std::pair<uint16_t, uint16_t> worldToLocalTile(const WorldPosition& pos) {
	float chunkWorldSize = static_cast<float>(kChunkSize) * kTileSize;
	float localX = std::fmod(pos.x, chunkWorldSize);
	float localY = std::fmod(pos.y, chunkWorldSize);
	if (localX < 0) {
		localX += chunkWorldSize;
	}
	if (localY < 0) {
		localY += chunkWorldSize;
	}
	return {
		static_cast<uint16_t>(localX / kTileSize),
		static_cast<uint16_t>(localY / kTileSize)
	};
}

}  // namespace engine::world

// Hash specialization for std::unordered_map
namespace std {
template <>
struct hash<engine::world::ChunkCoordinate> {
	size_t operator()(const engine::world::ChunkCoordinate& coord) const noexcept {
		// Use standard hash combining technique for portability (works on 32-bit and 64-bit)
		size_t h1 = std::hash<int32_t>{}(coord.x);
		size_t h2 = std::hash<int32_t>{}(coord.y);
		return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
	}
};
}  // namespace std
