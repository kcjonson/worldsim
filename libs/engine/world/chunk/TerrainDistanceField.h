#pragma once

// TerrainDistanceField - the per-chunk textures the chunk shader paints water
// from (docs/technical/organic-terrain/terrain-polygons-architecture.md D10,
// 10.1 and 10.4), baked on the generation worker from the chunk's unclipped
// rings and thalwegs. CPU side only: every texel is already in its GPU format
// (RGB16F as IEEE half bits, RGBA8), so the upload is a straight copy.
//
// The bake region is the chunk square. Every lattice is world-aligned: texel k
// of a lattice with spacing s covers [k*s, (k+1)*s) in world mm with its center
// at +s/2, so two chunks that both store a world position produce the same texel
// there, bit for bit, given the same rings and thalwegs within reach of it (D4,
// D14). Each texture also stores a one-texel gutter ring past the square holding
// the true neighboring samples, so bilinear filtering never needs another chunk.
//
//  - terrainSdf, two levels (10.4), RGB16F. Near: kSdfNearTexelMm texels in
//    sparse 16 m tiles, allocated only where a tile lies within kSdfNearM plus
//    one texel of the shoreline, each stored with its own one-texel gutter.
//    Far: 2 m texels over the whole square. tileMap says which 16 m tiles have
//    a near tile. R: signed distance (m) to the shoreline, negative in water,
//    clamped to +-kSdfNearM. G: min over thalweg segments of distance / local
//    half-width, kNoThalweg where none is within 2 hw. B: WaterKind (0..4) of
//    the nearest shoreline piece's ring within kSdfNearM, else of the containing
//    ring in water, else 0.
//  - shoreProfile, RGBA8 at 1 m: {slope, exposure, sand, mud} of the nearest
//    shoreline ring vertex within kSdfNearM, else 0.
//  - channelFrame, RGB16F at 1 m, only where terrainSdf G < kNoThalweg (else 0):
//    R arc coordinate mod kArcWrapM, G width ratio, B curvature x half-width.
//
// The shoreline is the boundary of the water union, not every ring edge: ring
// edges are split where other rings cross them and the pieces lying inside other
// water (a channel bank submerged in the lake it flows into) are dropped, as are
// synthetic (D4) and fordable-cut (D7) edges. So a river mouth reads one
// continuous shore around the junction, with no bank line inside the lake.
//
// Containment (the sign) is D9's classification: even-odd over Waterline rings,
// union over Channel and Pond rings, synthetic edges included (they close rings).
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

/// One RGB16F texel: each channel is an IEEE binary16 bit pattern. Rows are 6
/// bytes per texel, so an upload sets GL_UNPACK_ALIGNMENT to 2 (or 1).
struct HalfTexel {
	uint16_t r = 0;
	uint16_t g = 0;
	uint16_t b = 0;

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
	[[nodiscard]] static TerrainDistanceField bake(const ChunkTerrainPolygons& polygons, ChunkCoordinate coord);

	// ============ Lattices (10.1, 10.4) ============

	static constexpr int64_t kChunkMm = static_cast<int64_t>(kChunkSize) * 1000;

	/// The near level's texel. One constant: the perf task can revisit it.
	static constexpr int64_t kSdfNearTexelMm = 500;
	static constexpr int64_t kNearTileMm = 16000;
	static constexpr int32_t kTilesPerSide = static_cast<int32_t>(kChunkMm / kNearTileMm);
	static constexpr int32_t kNearTileTexels = static_cast<int32_t>(kNearTileMm / kSdfNearTexelMm);
	/// A stored near tile: its texels plus a one-texel gutter per side.
	static constexpr int32_t kNearTileStride = kNearTileTexels + 2;
	static constexpr size_t kNearTileTexelCount = static_cast<size_t>(kNearTileStride) * kNearTileStride;
	static constexpr uint16_t kNoNearTile = 0xFFFF;

	static constexpr int64_t kFarTexelMm = 2000;
	static constexpr int32_t kFarTexels = static_cast<int32_t>(kChunkMm / kFarTexelMm);
	static constexpr int32_t kFarStride = kFarTexels + 2;

	/// shoreProfile and channelFrame.
	static constexpr int64_t kDetailTexelMm = 1000;
	static constexpr int32_t kDetailTexels = static_cast<int32_t>(kChunkMm / kDetailTexelMm);
	static constexpr int32_t kDetailStride = kDetailTexels + 2;

	// ============ Values ============

	static constexpr double kSdfNearM = 8.0;
	/// terrainSdf G where no thalweg is within 2 half-widths.
	static constexpr double kNoThalweg = 2.0;
	/// channelFrame R is ThalwegPath::arcLengthM mod this: half precision holds
	/// ~0.03 m below 64. For the shader task: bilinear filtering cannot cross the
	/// wrap, so fetch the four texels and unwrap them against one before
	/// blending (or use texelFetch), and snap any along-river wavelength lambda
	/// to kArcWrapM / round(kArcWrapM / lambda) so the pattern tiles the wrap. The
	/// arc coordinate also steps at coarse tile joints (~50 km apart) and feeder
	/// junctions; those steps are accepted.
	static constexpr double kArcWrapM = 64.0;

	/// The far texel with no water in reach: +8 m (land), no thalweg (2.0),
	/// kind 0, as half bits.
	static constexpr HalfTexel kLandSdfTexel{0x4800, 0x4000, 0};

	// ============ Data ============

	/// World mm of the chunk square's min corner.
	geometry::Vec2i64 originMm{};
	/// ChunkTerrainPolygons::version of the rings this was baked from.
	uint32_t version = 0;
	/// kTilesPerSide^2, row-major (y outer): index into the near tiles, or kNoNearTile.
	std::vector<uint16_t> tileMap{};
	/// Near tiles in tileMap order, each kNearTileStride^2 texels row-major;
	/// stored texel (u, v) of the tile at (tx, ty) is near lattice texel
	/// (tx * kNearTileTexels + u - 1, ty * kNearTileTexels + v - 1).
	std::vector<HalfTexel> nearTexels{};
	/// kFarStride^2 row-major (lattice -1 .. kFarTexels), or empty when every
	/// texel is kLandSdfTexel.
	std::vector<HalfTexel> farTexels{};
	/// kDetailStride^2 row-major (lattice -1 .. kDetailTexels), or empty when zero.
	std::vector<ByteTexel> shoreProfile{};
	/// kDetailStride^2 row-major (lattice -1 .. kDetailTexels), or empty when zero.
	std::vector<HalfTexel> channelFrame{};

	// ============ Access ============

	[[nodiscard]] size_t nearTileCount() const { return nearTexels.size() / kNearTileTexelCount; }

	/// Index of the near tile covering 16 m tile (tx, ty) of the chunk, or kNoNearTile.
	[[nodiscard]] uint16_t nearTileAt(int32_t tx, int32_t ty) const {
		return tileMap[static_cast<size_t>(ty) * static_cast<size_t>(kTilesPerSide) + static_cast<size_t>(tx)];
	}

	/// Stored texel (u, v), each in [0, kNearTileStride), of near tile `tile`.
	[[nodiscard]] const HalfTexel& nearTexel(uint16_t tile, int32_t u, int32_t v) const {
		return nearTexels
			[static_cast<size_t>(tile) * kNearTileTexelCount + static_cast<size_t>(v) * static_cast<size_t>(kNearTileStride) +
			 static_cast<size_t>(u)];
	}

	/// Far lattice texel (i, j), each in [-1, kFarTexels].
	[[nodiscard]] HalfTexel farTexel(int32_t i, int32_t j) const {
		return farTexels.empty() ? kLandSdfTexel : farTexels[gutteredIndex(i, j, kFarStride)];
	}

	/// Detail lattice texel (i, j), each in [-1, kDetailTexels].
	[[nodiscard]] ByteTexel shoreProfileTexel(int32_t i, int32_t j) const {
		return shoreProfile.empty() ? ByteTexel{} : shoreProfile[gutteredIndex(i, j, kDetailStride)];
	}

	[[nodiscard]] HalfTexel channelFrameTexel(int32_t i, int32_t j) const {
		return channelFrame.empty() ? HalfTexel{} : channelFrame[gutteredIndex(i, j, kDetailStride)];
	}

	/// Bytes held by each texture (tile map included with the near level).
	[[nodiscard]] size_t nearBytes() const { return nearTexels.size() * sizeof(HalfTexel) + tileMap.size() * sizeof(uint16_t); }
	[[nodiscard]] size_t farBytes() const { return farTexels.size() * sizeof(HalfTexel); }
	[[nodiscard]] size_t shoreProfileBytes() const { return shoreProfile.size() * sizeof(ByteTexel); }
	[[nodiscard]] size_t channelFrameBytes() const { return channelFrame.size() * sizeof(HalfTexel); }

  private:
	static size_t gutteredIndex(int32_t i, int32_t j, int32_t stride) {
		return static_cast<size_t>(j + 1) * static_cast<size_t>(stride) + static_cast<size_t>(i + 1);
	}
};

} // namespace engine::world
