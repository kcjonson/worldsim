#pragma once

// Internal to TerrainPolygonBuilder: what the waterline path
// (TerrainPolygonBuilder.cpp) and the pond and channel path
// (TerrainChannelBuilder.cpp) share. Not for use outside those two files.

#include "world/chunk/Chunk.h"
#include "world/chunk/ChunkCoordinate.h"
#include "world/chunk/TerrainPolygonBuilder.h"
#include "world/chunk/TerrainPolygons.h"

#include <contour/ClipRing.h>
#include <contour/WarpField.h>
#include <core/Vec2d.h>
#include <core/Vec2i64.h>
#include <polygon/Polygon.h>
#include <random/HashNoise.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace engine::world::terrain_detail {

	using geometry::Ring;
	using geometry::Vec2d;
	using geometry::Vec2i64;
	using Builder = TerrainPolygonBuilder;

	inline constexpr double kMmPerMeter = static_cast<double>(geometry::kMillimetersPerMeter);

	// Per-purpose salts mixed with the world seed, so each noise term has its own
	// stream and no other system that hashes the world seed lines up with them. Kept
	// in one list so no two purposes share one.
	inline constexpr uint32_t kSaltBankX = 0x5A17B001U;
	inline constexpr uint32_t kSaltBankY = 0x5A17B002U;
	inline constexpr uint32_t kSaltLowX = 0x5A17B003U;
	inline constexpr uint32_t kSaltLowY = 0x5A17B004U;
	inline constexpr uint32_t kSaltLeftBankFine = 0x5A17B005U;
	inline constexpr uint32_t kSaltLeftBankLow = 0x5A17B006U;
	inline constexpr uint32_t kSaltRightBankFine = 0x5A17B007U;
	inline constexpr uint32_t kSaltRightBankLow = 0x5A17B008U;
	inline constexpr uint32_t kSaltPondRim = 0x5A17B009U;

	inline uint32_t purposeSeed(uint64_t worldSeed, uint32_t salt) {
		return foundation::hash3(static_cast<int32_t>(salt), static_cast<int32_t>(worldSeed >> 32U), 0,
								 static_cast<uint32_t>(worldSeed));
	}

	// fBm at a world position, clamped to [-1, 1] (gradient noise can overshoot
	// slightly, and kMaxWarpMm must be a true bound). The input is formed from the
	// exact integer mm alone, never a chunk-relative value, so every chunk feeds
	// the noise bit-identical inputs for the same world point. fractalNoise2 is
	// fractalNoise3 at z = 0 bit for bit, at half the hashing.
	inline float worldNoise(Vec2i64 worldMm, double wavelengthM, uint32_t seed, int octaves) {
		const auto	x = static_cast<float>(static_cast<double>(worldMm.x) / kMmPerMeter / wavelengthM);
		const auto	y = static_cast<float>(static_cast<double>(worldMm.y) / kMmPerMeter / wavelengthM);
		const float n = foundation::fractalNoise2(x, y, seed, octaves, Builder::kNoiseLacunarity, Builder::kNoiseGain);
		return std::clamp(n, -1.0F, 1.0F);
	}

	inline Vec2i64 toMm(const Vec2d& meters) {
		return {std::llround(meters.x * kMmPerMeter), std::llround(meters.y * kMmPerMeter)};
	}

	inline Vec2d toMeters(const Vec2i64& mm) {
		return {static_cast<double>(mm.x) / kMmPerMeter, static_cast<double>(mm.y) / kMmPerMeter};
	}

	// Spelled out rather than std::lerp, whose rounding is left to each standard
	// library (D14).
	inline double lerp(double a, double b, double t) {
		return a + (b - a) * t;
	}

	inline double smoothstep(double x) {
		const double t = std::clamp(x, 0.0, 1.0);
		return t * t * (3.0 - 2.0 * t);
	}

	// The chunk square and the extended region (chunk plus apron), world mm.
	struct Region {
		Vec2i64 chunkMin;
		Vec2i64 chunkMax;
		Vec2i64 extMin;
		Vec2i64 extMax;
		int32_t apronTiles	 = 0;
		int32_t extendedSize = 0; // tiles per axis

		[[nodiscard]] geometry::RectMm chunkRect() const { return {chunkMin, chunkMax}; }
		[[nodiscard]] geometry::RectMm extendedRect() const { return {extMin, extMax}; }

		[[nodiscard]] bool inExtended(const Vec2i64& p) const {
			return p.x >= extMin.x && p.x <= extMax.x && p.y >= extMin.y && p.y <= extMax.y;
		}

		// Chebyshev distance from the chunk square, 0 inside it.
		[[nodiscard]] int64_t outsideChunkMm(const Vec2i64& p) const {
			return std::max({chunkMin.x - p.x, p.x - chunkMax.x, chunkMin.y - p.y, p.y - chunkMax.y, int64_t{0}});
		}

		// World tile index of extended tile (0, 0), per axis.
		[[nodiscard]] Vec2i64 extendedTileOrigin() const { return {extMin.x / Builder::kTileMm, extMin.y / Builder::kTileMm}; }
	};

	Region regionOf(ChunkCoordinate coord, int32_t apronTiles);

	// Extended-region tiles plus the biome water indicator, read once. The
	// indicator comes from the biome water query, the one source every waterline
	// read uses (the fine lattice here, the point field along river centerlines).
	class ExtendedGrid {
	  public:
		ExtendedGrid(const Builder::ExtendedTileFn& tiles, const Builder::BiomeWaterFn& biomeWater, const Region& region)
			: m_tiles(tiles),
			  m_extMin(region.extMin),
			  m_size(region.extendedSize),
			  m_water(static_cast<size_t>(m_size) * static_cast<size_t>(m_size)) {
			const Vec2i64 origin = region.extendedTileOrigin();
			for (int32_t ey = 0; ey < m_size; ++ey) {
				for (int32_t ex = 0; ex < m_size; ++ex) {
					m_water[index(ex, ey)] = biomeWater(origin.x + ex, origin.y + ey) ? 1 : 0;
					m_anyWater			   = m_anyWater || m_water[index(ex, ey)] != 0;
				}
			}
		}

		[[nodiscard]] bool anyWater() const { return m_anyWater; }
		[[nodiscard]] int32_t size() const { return m_size; }

		[[nodiscard]] bool contains(int32_t ex, int32_t ey) const { return ex >= 0 && ey >= 0 && ex < m_size && ey < m_size; }

		// Outside the extended region is land (D4).
		[[nodiscard]] bool water(int32_t ex, int32_t ey) const { return contains(ex, ey) && m_water[index(ex, ey)] != 0; }

		[[nodiscard]] const TileData& tile(int32_t ex, int32_t ey) const { return m_tiles(ex, ey); }

		// Extended tile containing world point (mm, fractional allowed).
		[[nodiscard]] std::pair<int32_t, int32_t> tileAt(double xMm, double yMm) const {
			const double tile = static_cast<double>(Builder::kTileMm);
			return {
				static_cast<int32_t>(std::floor((xMm - static_cast<double>(m_extMin.x)) / tile)),
				static_cast<int32_t>(std::floor((yMm - static_cast<double>(m_extMin.y)) / tile))
			};
		}

		[[nodiscard]] std::pair<double, double> tileCenterMm(int32_t ex, int32_t ey) const {
			const double half = static_cast<double>(Builder::kTileMm) / 2.0;
			return {
				static_cast<double>(m_extMin.x + static_cast<int64_t>(ex) * Builder::kTileMm) + half,
				static_cast<double>(m_extMin.y + static_cast<int64_t>(ey) * Builder::kTileMm) + half
			};
		}

	  private:
		[[nodiscard]] size_t index(int32_t ex, int32_t ey) const {
			return static_cast<size_t>(ey) * static_cast<size_t>(m_size) + static_cast<size_t>(ex);
		}

		const Builder::ExtendedTileFn& m_tiles;
		Vec2i64						   m_extMin;
		int32_t						   m_size;
		std::vector<uint8_t>		   m_water;
		bool						   m_anyWater = false;
	};

	// ============ The waterline field (D5 steps 1-3, D6 step 1) ============

	// D5 steps 1-3 at one tile: the biome water indicator blurred with the 3x3
	// binomial kernel, then the thin-feature guard on the indicator's cardinal
	// neighbors. `water(x, y)` is the indicator at tile (x, y). The one formula
	// for the fine lattice (over the extended grid) and for point queries (over
	// the biome water query).
	template <typename Water> float coarseWaterValue(const Water& water, int64_t x, int64_t y) {
		static constexpr int kBinomial[3][3] = {{1, 2, 1}, {2, 4, 2}, {1, 2, 1}};
		int					 sum			 = 0;
		for (int dy = -1; dy <= 1; ++dy) {
			for (int dx = -1; dx <= 1; ++dx) {
				if (water(x + dx, y + dy)) {
					sum += kBinomial[dy + 1][dx + 1];
				}
			}
		}
		const float value = static_cast<float>(sum) / 16.0F;
		const bool	self  = water(x, y);
		const int	same  = (water(x + 1, y) == self ? 1 : 0) + (water(x - 1, y) == self ? 1 : 0) +
						  (water(x, y + 1) == self ? 1 : 0) + (water(x, y - 1) == self ? 1 : 0);
		if (same >= 2) {
			return value;
		}
		return self ? std::max(value, Builder::kThinWaterFloor) : std::min(value, Builder::kThinLandCeil);
	}

	struct WarpSeeds {
		uint32_t bankX;
		uint32_t bankY;
		uint32_t lowX;
		uint32_t lowY;
	};

	WarpSeeds warpSeeds(uint64_t worldSeed);

	// D6 step 1: the world-space warp offset at a point, both components clamped
	// to kMaxWarpMm.
	geometry::WarpOffsetMm shoreWarpOffset(Vec2i64 worldMm, const WarpSeeds& seeds);

	// The coarse lattice sits on tile centers: global index k at world k tiles
	// plus half a tile, so index k is tile k.
	inline constexpr Vec2i64 kCoarsePhaseMm{Builder::kTileMm / 2, Builder::kTileMm / 2};

	// The waterline field at any world point, from the biome water query: the
	// value the fine lattice holds there when it warps, bit for bit, wherever the
	// lattice's coarse samples are free of the extended region's edge (D5, D6).
	// A function of world position alone, so every chunk reads the same ground
	// along a river (D7 mouths).
	class WaterlineField {
	  public:
		WaterlineField(const Builder::BiomeWaterFn& biomeWater, uint64_t worldSeed)
			: m_biomeWater(biomeWater),
			  m_seeds(warpSeeds(worldSeed)) {}

		[[nodiscard]] float valueAt(Vec2i64 worldMm) const;
		[[nodiscard]] bool	waterAt(Vec2i64 worldMm) const { return valueAt(worldMm) >= Builder::kWaterlineIso; }

	  private:
		const Builder::BiomeWaterFn& m_biomeWater;
		WarpSeeds					 m_seeds;
	};

	// D6 step 1 over the extended region: the warped fine lattice the waterline is
	// marched on, before its border is forced to land. Its coarse samples read
	// outside the extended grid as land (the edge effect the apron absorbs).
	geometry::ScalarField waterlineFineField(const ExtendedGrid& grid, const Region& region, uint64_t worldSeed);

	// ============ The shared tail ============

	// The shared tail of every ring (D6 steps 4-7): pin every crossing of the
	// world lattice (kPinLatticeMm) and of the extra lines, resample between the
	// pins, then simplify, validating after the last change. `extraPinned` marks
	// vertices pinned besides the lines (fordable cut vertices). The retry ladder
	// only loosens the simplification, never the shape: a per-chunk shape change
	// would break the seam, since the neighbor would not make the same choice.
	// Returns nullopt when every rung folds.
	std::optional<Ring> pinResampleSimplifyValidate(Ring loop, std::span<const int64_t> xLines, std::span<const int64_t> yLines,
													std::span<const Vec2i64> extraPins, ChunkCoordinate coord, const char* what);

	// Loops under kMinLoopAreaMm2 are dropped (D6 step 8).
	bool areaBelowFloor(const Ring& ring);

	// D4: an edge whose endpoints both lie within one fine cell of the same side
	// of the extended rectangle is the closure the forced-land boundary makes.
	bool isSyntheticEdge(const Vec2i64& a, const Vec2i64& b, const Region& region);

	// D4 for clipped vector rings: clipRingToRect closes a ring exactly along the
	// extended rectangle, so an edge is synthetic when both ends lie on one side.
	bool isOnExtendedSide(const Vec2i64& a, const Vec2i64& b, const Region& region);

	// Which tiles count as the land side and the water side of a ring vertex.
	// Waterline rings split on the biome water indicator. Channel and pond
	// water is not biome water, and the tile raster paints it Surface::Water
	// (with a 0.8 m half-width floor, so a probe past a narrow bank can still
	// land on it): their land side is a tile that is neither, their water side
	// whatever tile the probe falls in.
	enum class SideRule : uint8_t { BiomeWater, VectorWater };

	// D15 per-vertex profile. Every scale is a named constant on the builder.
	// Flags other than rock are the caller's (synthetic and fordable-cut edges
	// are found differently per ring kind).
	std::vector<ShoreProfile> shoreProfiles(const Ring& ring, WaterKind water, const ExtendedGrid& grid, SideRule rule);

	// D8: the rim of a pond, unclipped: kPondRimSpacingM arc spacing from theta
	// 0, each radius perturbed by world-space noise.
	Ring pondRim(const Builder::Pond& pond, uint64_t worldSeed);

	// D8: every gathered pond's rim, clipped to the extended region.
	void buildPonds(std::vector<TerrainRing>& rings, std::span<const Builder::Pond> ponds, const ExtendedGrid& grid,
					const Region& region, uint64_t worldSeed, ChunkCoordinate coord);

	// D7: the gathered segments as channel rings and thalwegs, flaring where a
	// centerline enters the waterline field or a pond rim.
	void buildChannels(ChunkTerrainPolygons& out, std::span<const Builder::RiverSegment> segments,
					   std::span<const Builder::Pond> ponds, const WaterlineField& waterline, const ExtendedGrid& grid,
					   const Region& region, uint64_t worldSeed, ChunkCoordinate coord);

} // namespace engine::world::terrain_detail
