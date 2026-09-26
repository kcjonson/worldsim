#pragma once

// TerrainPolygons - the per-chunk ring set that is the terrain-polygons epic's
// runtime truth for water (D1/D2 in
// docs/technical/organic-terrain/terrain-polygons-architecture.md). This header
// only defines the types and Chunk's storage for them; TerrainPolygonBuilder is
// what fills a ChunkTerrainPolygons.

#include <polygon/Polygon.h>

#include <cstdint>
#include <vector>

namespace engine::world {

/// What kind of ring this is: the softened biome waterline, a stroked river
/// channel, or a pond rim (D2).
enum class TerrainRingKind : uint8_t { Waterline, Channel, Pond };

/// What body of water a ring belongs to. Waterline rings take Ocean/Lake/Wetland
/// from the sampled biome; Channel/Pond rings are always River/Pond (D2, D5).
enum class WaterKind : uint8_t { Ocean, Lake, Wetland, River, Pond };

/// Per-ring-vertex shading data (D2, D15): what a renderer or placement rule
/// needs to vary bands, foam, and vegetation along the ring instead of drawing a
/// uniform stroke. CPU-side truth; the GPU packing is a later renderer task.
struct ShoreProfile {
	uint8_t slope = 0;     ///< 0 gentle .. 255 steep, elevation gradient across the ring
	uint8_t exposure = 0;  ///< 0 sheltered bay .. 255 exposed headland
	uint8_t sand = 0;      ///< substrate weights, sand + mud + grass = 255; rock is a flag
	uint8_t mud = 0;
	uint8_t moisture = 0;  ///< TileData::moisture of the adjacent land tile
	uint8_t flags = 0;     ///< bit0 rock, bit1 synthetic edge (D4), bit2 fordable cut (D7)

	static constexpr uint8_t kFlagRock = 1U << 0U;
	/// The edge starting at this vertex is a closure along the extended region's
	/// boundary, not a real shore; consumers other than nav skip it (D4).
	static constexpr uint8_t kFlagSynthetic = 1U << 1U;
	/// This vertex is one end of a fordability cut (D7 step 6): the straight butt
	/// edge two channel pieces share where the width crosses kFordableWidthM. The
	/// edge between two flagged vertices is internal to the river, not a bank.
	static constexpr uint8_t kFlagFordableCut = 1U << 2U;

	bool operator==(const ShoreProfile&) const = default;
};

/// One waterline/channel/pond ring plus its per-vertex shading data (D2).
struct TerrainRing {
	geometry::Ring ring;                ///< integer mm, world-absolute, simple, CCW outer / CW hole
	std::vector<ShoreProfile> profiles; ///< same length as ring.size() (D15)
	TerrainRingKind kind = TerrainRingKind::Waterline;
	WaterKind water = WaterKind::Lake;
	bool blocksMovement = true; ///< false only for fordable channels (D7)
	bool holeCapable = true;    ///< Waterline: true (even-odd); Channel/Pond: false (solid)
	float meanHalfWidthM = 0.0F; ///< Channel only: mean bankfull half-width over the piece
};

/// One river reach's thalweg (D2, D7 step 3), for the distance-field bake: the
/// centerline samples offset toward the outer bank by the bend asymmetry, over
/// the extended region. Parallel arrays, one entry per point.
struct ThalwegPath {
	std::vector<geometry::Vec2i64> points; ///< integer mm, world-absolute
	std::vector<float> halfWidthM;         ///< bankfull half-width at each point
	std::vector<float> widthRatio;         ///< w / mean w over a 5 w window (riffle > 1, pool < 1)
	std::vector<float> curvature;          ///< signed, 1/m, positive turning left
};

/// A chunk's terrain polygon set. `rings` covers the extended region (chunk plus
/// apron, D4) unclipped, so no consumer of it ever sees a chunk border as a
/// shoreline; `navRings` is the same rings clipped to the chunk's own 512x512
/// square (D4, D9). `navRings` carries no per-vertex profiles: nav only reads the
/// ring and the blocksMovement/holeCapable flags, never shading. `thalwegs` has one
/// path per river reach over the extended region (D2, D10).
struct ChunkTerrainPolygons {
	std::vector<TerrainRing> rings;
	std::vector<TerrainRing> navRings;
	std::vector<ThalwegPath> thalwegs;
	uint32_t version = 0; ///< bumped with the rings, read like Chunk::renderDataVersion
};

} // namespace engine::world
