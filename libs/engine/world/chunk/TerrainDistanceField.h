#pragma once

// TerrainDistanceField - the per-chunk textures the chunk shader paints water
// from (docs/technical/organic-terrain/terrain-polygons-architecture.md D10,
// 10.1 and 10.4), baked on the generation worker from the chunk's unclipped
// rings and thalwegs. CPU side only: every texel is already in its GPU format
// (RGBA16F as IEEE half bits, RGBA8), so the upload is a straight copy.
//
// All lattices cover the bake region, the chunk square grown by one 16 m tile
// on every side (544 m), and are world-aligned: texel k of a lattice with
// spacing s covers [k*s, (k+1)*s) in world mm with its center at +s/2, so two
// chunks that both cover a world position produce the same texel there, bit for
// bit, given the same rings and thalwegs within reach of it (D4, D14).
//
//  - terrainSdf, two levels (10.4). Near: 0.25 m texels in sparse 16 m tiles,
//    allocated only where a tile lies within kSdfNearM plus one texel of a real
//    shore edge, each stored 66x66 with a one-texel gutter holding the
//    neighboring lattice samples so bilinear filtering never crosses tiles. Far:
//    2 m texels over the whole region. tileMap says which 16 m tiles have a near
//    tile. R: signed distance (m) to the nearest ring edge that is neither
//    synthetic (D4) nor a fordable cut (D7), negative in water, clamped to
//    +-kSdfNearM. G: min over thalweg segments of distance / local half-width,
//    kNoThalweg where none is within 2 hw. B: WaterKind (0..4) of the nearest
//    real edge's ring within kSdfNearM, else of the containing ring in water,
//    else 0. A: 0.
//  - shoreProfile, RGBA8 at 1 m: {slope, exposure, sand, mud} of the nearest
//    real ring vertex within kSdfNearM, else 0.
//  - channelFrame, RGBA16F at 1 m: R arc length along the nearest thalweg
//    (wrapped at kArcWrapM, anchored on the chunk grid, see bake()), G width
//    ratio, B curvature x half-width, A 0; only where terrainSdf G < kNoThalweg,
//    else 0.
//
// A texture whose texels would all equal its default (kLandSdfTexel for the far
// level, zero for the others) is stored empty: an all-land chunk costs nothing.

#include "world/chunk/ChunkCoordinate.h"
#include "world/chunk/TerrainPolygons.h"

#include <core/Vec2i64.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace engine::world {

/// One RGBA16F texel: each channel is an IEEE binary16 bit pattern.
struct HalfTexel {
	uint16_t r = 0;
	uint16_t g = 0;
	uint16_t b = 0;
	uint16_t a = 0;

	bool operator==(const HalfTexel&) const = default;
};

/// One RGBA8 texel.
struct ByteTexel {
	uint8_t r = 0;
	uint8_t g = 0;
	uint8_t b = 0;
	uint8_t a = 0;

	bool operator==(const ByteTexel&) const = default;
};

class TerrainDistanceField {
  public:
	/// Bake the three textures from a chunk's unclipped rings and thalwegs. The
	/// result carries polygons.version, so a GPU cache keyed on
	/// terrainPolygons().version stays in step with it.
	///
	/// channelFrame's arc length is anchored where a thalweg crosses a chunk
	/// border line (x or y a multiple of 512 m): every crossing reads
	/// kArcAnchorValueM, half a wrap, so the wrap (which bilinear filtering
	/// cannot interpolate across) never falls on a chunk seam. Within
	/// kArcAnchorHoldM of a crossing the value is the exact arc length from it;
	/// between two crossings L apart the arc length is stretched over the middle
	/// of the reach so the far crossing lands on round(L / wrap) whole wraps,
	/// keeping the value continuous along the river. A reach under half a wrap
	/// long cannot be stretched to a whole wrap; it reads the arc length from its
	/// nearer crossing and steps once at its midpoint. Only the crossings a
	/// texel's reach touches matter, so a chunk that holds that reach computes the
	/// same value as its neighbor. A path that crosses no border line inside the
	/// region counts from its first point.
	[[nodiscard]] static TerrainDistanceField bake(const ChunkTerrainPolygons& polygons, ChunkCoordinate coord);

	// ============ Lattices (10.1, 10.4) ============

	/// The bake region is the chunk square grown by one near tile per side.
	static constexpr int64_t kNearTileMm = 16000;
	static constexpr int64_t kMarginMm = kNearTileMm;
	static constexpr int64_t kRegionMm = static_cast<int64_t>(kChunkSize) * 1000 + 2 * kMarginMm;
	static constexpr int32_t kTilesPerSide = static_cast<int32_t>(kRegionMm / kNearTileMm);

	static constexpr int64_t kNearTexelMm = 250;
	static constexpr int32_t kNearTileTexels = static_cast<int32_t>(kNearTileMm / kNearTexelMm);
	/// A stored near tile: its 64x64 texels plus a one-texel gutter per side.
	static constexpr int32_t kNearTileStride = kNearTileTexels + 2;
	static constexpr size_t kNearTileTexelCount = static_cast<size_t>(kNearTileStride) * kNearTileStride;
	static constexpr uint16_t kNoNearTile = 0xFFFF;

	static constexpr int64_t kFarTexelMm = 2000;
	static constexpr int32_t kFarSize = static_cast<int32_t>(kRegionMm / kFarTexelMm);

	/// shoreProfile and channelFrame.
	static constexpr int64_t kDetailTexelMm = 1000;
	static constexpr int32_t kDetailSize = static_cast<int32_t>(kRegionMm / kDetailTexelMm);

	// ============ Values ============

	static constexpr double kSdfNearM = 8.0;
	/// terrainSdf G where no thalweg is within 2 half-widths.
	static constexpr double kNoThalweg = 2.0;
	static constexpr double kArcWrapM = 256.0;
	static constexpr double kArcAnchorValueM = kArcWrapM / 2.0;
	/// Chunk border lines are the arc-length anchors.
	static constexpr int64_t kArcAnchorSpacingMm = static_cast<int64_t>(kChunkSize) * 1000;
	/// Arc length is exact (unstretched) this far from an anchor, which covers
	/// the bake margin a neighbor reads.
	static constexpr double kArcAnchorHoldM = 24.0;

	/// The far texel of a chunk with no water in reach: +8 m (land), no thalweg
	/// (2.0), kind 0, as half bits.
	static constexpr HalfTexel kLandSdfTexel{0x4800, 0x4000, 0, 0};

	// ============ Data ============

	/// World mm of the bake region's min corner: the chunk origin minus kMarginMm.
	geometry::Vec2i64 originMm{};
	/// ChunkTerrainPolygons::version of the rings this was baked from.
	uint32_t version = 0;
	/// kTilesPerSide^2, row-major (y outer): index into the near tiles, or kNoNearTile.
	std::vector<uint16_t> tileMap{};
	/// Near tiles in tileMap order (row-major over the tiles that have one), each
	/// kNearTileStride^2 texels row-major; stored texel (u, v) of the tile at
	/// (tx, ty) is lattice texel (tx * 64 + u - 1, ty * 64 + v - 1).
	std::vector<HalfTexel> nearTexels{};
	/// kFarSize^2 row-major, or empty when every texel is kLandSdfTexel.
	std::vector<HalfTexel> farTexels{};
	/// kDetailSize^2 row-major, or empty when every texel is zero.
	std::vector<ByteTexel> shoreProfile{};
	/// kDetailSize^2 row-major, or empty when every texel is zero.
	std::vector<HalfTexel> channelFrame{};

	// ============ Access ============

	[[nodiscard]] size_t nearTileCount() const { return nearTexels.size() / kNearTileTexelCount; }

	/// Index of the near tile covering 16 m tile (tx, ty) of the bake region, or kNoNearTile.
	[[nodiscard]] uint16_t nearTileAt(int32_t tx, int32_t ty) const {
		return tileMap[static_cast<size_t>(ty) * static_cast<size_t>(kTilesPerSide) + static_cast<size_t>(tx)];
	}

	/// Stored texel (u, v), each in [0, kNearTileStride), of near tile `tile`.
	[[nodiscard]] const HalfTexel& nearTexel(uint16_t tile, int32_t u, int32_t v) const {
		return nearTexels
			[static_cast<size_t>(tile) * kNearTileTexelCount + static_cast<size_t>(v) * static_cast<size_t>(kNearTileStride) +
			 static_cast<size_t>(u)];
	}

	[[nodiscard]] HalfTexel farTexel(int32_t i, int32_t j) const {
		return farTexels.empty() ? kLandSdfTexel : farTexels[detailIndex(i, j, kFarSize)];
	}

	[[nodiscard]] ByteTexel shoreProfileTexel(int32_t i, int32_t j) const {
		return shoreProfile.empty() ? ByteTexel{} : shoreProfile[detailIndex(i, j, kDetailSize)];
	}

	[[nodiscard]] HalfTexel channelFrameTexel(int32_t i, int32_t j) const {
		return channelFrame.empty() ? HalfTexel{} : channelFrame[detailIndex(i, j, kDetailSize)];
	}

	/// Bytes held by each texture (tile map included with the near level).
	[[nodiscard]] size_t nearBytes() const { return nearTexels.size() * sizeof(HalfTexel) + tileMap.size() * sizeof(uint16_t); }
	[[nodiscard]] size_t farBytes() const { return farTexels.size() * sizeof(HalfTexel); }
	[[nodiscard]] size_t shoreProfileBytes() const { return shoreProfile.size() * sizeof(ByteTexel); }
	[[nodiscard]] size_t channelFrameBytes() const { return channelFrame.size() * sizeof(HalfTexel); }

  private:
	static size_t detailIndex(int32_t i, int32_t j, int32_t size) {
		return static_cast<size_t>(j) * static_cast<size_t>(size) + static_cast<size_t>(i);
	}
};

} // namespace engine::world
