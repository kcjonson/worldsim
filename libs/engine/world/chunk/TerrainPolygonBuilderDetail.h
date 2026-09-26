#pragma once

// Internal to TerrainPolygonBuilder: what the waterline path
// (TerrainPolygonBuilder.cpp) and the pond and channel path
// (TerrainChannelBuilder.cpp) share. Not for use outside those two files.

#include "world/chunk/Chunk.h"
#include "world/chunk/ChunkCoordinate.h"
#include "world/chunk/TerrainPolygonBuilder.h"
#include "world/chunk/TerrainPolygons.h"

#include <contour/ClipRing.h>
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

	// The chunk square and the extended region (chunk plus apron), world mm, plus
	// the wider box river centerlines are kept in.
	struct Region {
		Vec2i64 chunkMin;
		Vec2i64 chunkMax;
		Vec2i64 extMin;
		Vec2i64 extMax;
		Vec2i64 keepMin;
		Vec2i64 keepMax;

		[[nodiscard]] geometry::RectMm chunkRect() const { return {chunkMin, chunkMax}; }
		[[nodiscard]] geometry::RectMm extendedRect() const { return {extMin, extMax}; }

		[[nodiscard]] bool inExtended(const Vec2i64& p) const {
			return p.x >= extMin.x && p.x <= extMax.x && p.y >= extMin.y && p.y <= extMax.y;
		}
		[[nodiscard]] bool inKept(const Vec2i64& p) const {
			return p.x >= keepMin.x && p.x <= keepMax.x && p.y >= keepMin.y && p.y <= keepMax.y;
		}
	};

	Region regionOf(ChunkCoordinate coord);

	// Extended-region tiles plus the biome water indicator, read once.
	class ExtendedGrid {
	  public:
		ExtendedGrid(const Builder::ExtendedTileFn& tiles, const Region& region)
			: m_tiles(tiles),
			  m_extMin(region.extMin),
			  m_water(static_cast<size_t>(kExtendedSize) * static_cast<size_t>(kExtendedSize)) {
			for (int32_t ey = 0; ey < kExtendedSize; ++ey) {
				for (int32_t ex = 0; ex < kExtendedSize; ++ex) {
					m_water[index(ex, ey)] = isBiomeWater(tiles(ex, ey)) ? 1 : 0;
					m_anyWater			   = m_anyWater || m_water[index(ex, ey)] != 0;
				}
			}
		}

		[[nodiscard]] bool anyWater() const { return m_anyWater; }

		[[nodiscard]] static bool contains(int32_t ex, int32_t ey) {
			return ex >= 0 && ey >= 0 && ex < kExtendedSize && ey < kExtendedSize;
		}

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
		static size_t index(int32_t ex, int32_t ey) {
			return static_cast<size_t>(ey) * static_cast<size_t>(kExtendedSize) + static_cast<size_t>(ex);
		}

		const Builder::ExtendedTileFn& m_tiles;
		Vec2i64						   m_extMin;
		std::vector<uint8_t>		   m_water;
		bool						   m_anyWater = false;
	};

	// The shared tail of every ring (D6 steps 4-7): resample between the pins,
	// then simplify, validating after the last change. The retry ladder only
	// loosens the simplification, never the shape: a per-chunk shape change
	// would break the seam, since the neighbor would not make the same choice.
	// Returns nullopt when every rung folds.
	std::optional<Ring> resampleSimplifyValidate(Ring loop, std::vector<uint8_t> pinned, ChunkCoordinate coord, const char* what);

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

	// D8: every gathered pond's rim, clipped to the extended region. Built before
	// the channels, so a channel's mouth can find a receiving pond (D7).
	void buildPonds(std::vector<TerrainRing>& rings, std::span<const Builder::Pond> ponds, const ExtendedGrid& grid,
					const Region& region, uint64_t worldSeed, ChunkCoordinate coord);

	// D7: the gathered segments as channel rings and thalwegs, flaring into the
	// waterline and pond rings already in `out`.
	void buildChannels(ChunkTerrainPolygons& out, std::span<const Builder::RiverSegment> segments, const ExtendedGrid& grid,
					   const Region& region, uint64_t worldSeed, ChunkCoordinate coord);

} // namespace engine::world::terrain_detail
