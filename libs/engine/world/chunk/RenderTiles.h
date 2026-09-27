#pragma once

// RenderTiles - the per-tile data the land pass paints from
// (docs/technical/organic-terrain/terrain-polygons-architecture.md D16): a paint
// surface and an edge byte per tile, over a chunk's square plus kRenderApronTiles
// on every side. Uploaded as one RG8UI texel per tile; SurfaceField evaluates the
// same bytes on the CPU.

#include "world/chunk/ChunkCoordinate.h"

#include <cassert>
#include <cstdint>
#include <span>

namespace engine::world {

/// Tiles of render data past the chunk square on each side. A point of the square
/// reads its field up to three tiles out: the warp moves it at most kMaxLandWarpM
/// (1.5 m), the bilinear read spans the next tile center, and the blur one tile more.
inline constexpr int32_t kRenderApronTiles = 3;
inline constexpr int32_t kRenderTilesSide  = kChunkSize + 2 * kRenderApronTiles;

/// One tile of render data, two bytes (one RG8UI texel).
struct TileRenderData {
	/// Paint surface (a Surface value, never Water: a Water tile paints as its bed).
	uint8_t surfaceId = 0;
	/// Low nibble: the surface whose edge character (warp amplitudes) this tile's warp
	/// uses. kRenderInteriorBit: every tile within kInteriorReachTiles is one surface.
	uint8_t edge = 0;

	bool operator==(const TileRenderData&) const = default;
};

inline constexpr uint8_t kRenderEdgeSurfaceMask = 0x0F;
inline constexpr uint8_t kRenderInteriorBit = 0x80;

/// A rectangle of render tiles, addressed by world tile.
class RenderTileView {
  public:
	RenderTileView(std::span<const TileRenderData> newTiles, int32_t newWidth, int32_t newHeight, int64_t newOriginX, int64_t newOriginY)
		: tiles(newTiles),
		  width(newWidth),
		  height(newHeight),
		  originX(newOriginX),
		  originY(newOriginY) {
		assert(tiles.size() == static_cast<size_t>(width) * static_cast<size_t>(height));
	}

	[[nodiscard]] bool contains(int64_t tx, int64_t ty) const {
		return tx >= originX && ty >= originY && tx < originX + width && ty < originY + height;
	}

	[[nodiscard]] const TileRenderData& at(int64_t tx, int64_t ty) const {
		assert(contains(tx, ty));
		return tiles[static_cast<size_t>(ty - originY) * static_cast<size_t>(width) + static_cast<size_t>(tx - originX)];
	}

  private:
	std::span<const TileRenderData> tiles;
	int32_t							width;
	int32_t							height;
	int64_t							originX;
	int64_t							originY;
};

} // namespace engine::world
