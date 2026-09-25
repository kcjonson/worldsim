#pragma once

// TerrainPolygonBuilder - builds a chunk's terrain polygon rings on the
// generation worker (docs/technical/organic-terrain/terrain-polygons-architecture.md
// D4-D6, D15). The only place the waterline recipe is spelled out: biome water
// indicator over the extended region (chunk plus apron), 3x3 binomial blur,
// thin-feature guard, world-space domain warp onto a 250 mm lattice, one march
// at iso 0.5, Chaikin, border pins, resample, simplify, validate. Rings come out
// unclipped over the extended region (`rings`, with per-vertex ShoreProfiles)
// and clipped to the chunk square (`navRings`).
//
// Everything is a pure function of world position, the tiles, and the world
// seed, so two chunks built independently produce bit-identical vertices where
// their clipped rings meet on the shared border (D4, D14).
//
// Waterline only for now; channel and pond rings (D7, D8) are a later task.

#include "world/chunk/Chunk.h"
#include "world/chunk/ChunkCoordinate.h"
#include "world/chunk/TerrainPolygons.h"

#include <cstdint>
#include <functional>

namespace engine::world {

/// Biome water per D5: ocean, lake, and both wetlands. Never TileData::surface,
/// which is also Water for river and pond tiles (stroked separately, D7/D8).
[[nodiscard]] bool isBiomeWater(const TileData& tile);

class TerrainPolygonBuilder {
  public:
	/// Tile at extended-region coordinates (ex, ey), each in [0, kExtendedSize);
	/// (kApronTiles, kApronTiles) is the chunk's own tile (0, 0).
	using ExtendedTileFn = std::function<const TileData&(int32_t ex, int32_t ey)>;

	/// Build the chunk's rings from its extended-region tiles. Chunk::generate
	/// passes ExtendedTiles over its own tiles plus an ApronField; tests can pass
	/// hand-built tiles.
	[[nodiscard]] static ChunkTerrainPolygons build(ChunkCoordinate coord, uint64_t worldSeed, const ExtendedTileFn& tiles);

	// ============ Recipe parameters (spec section 4) ============

	static constexpr int64_t kTileMm = 1000;
	/// Fine lattice spacing the warped field is marched on (D6).
	static constexpr int64_t kFineCellMm = 250;
	static constexpr float kWaterlineIso = 0.5F;
	/// Thin-feature guard (D5): a water sample with fewer than two water cardinal
	/// neighbors is floored here, a land sample with fewer than two land cardinal
	/// neighbors capped here, so 1-tile pools, inlets, and islets survive. The
	/// spec's 0.70/0.30 march a lone tile to ~0.2 m^2, under kMinLoopAreaMm2 even
	/// before simplification; 0.85/0.15 gives ~0.35-0.47 m^2 (~0.7 m across).
	static constexpr float kThinWaterFloor = 0.85F;
	static constexpr float kThinLandCeil = 0.15F;

	/// Domain warp (D6): fine bank term plus a low-frequency shore term, per
	/// vector component, each an fBm normalized to [-1, 1] and scaled here.
	static constexpr int64_t kBankNoiseAmpMm = 250;
	static constexpr int kBankNoiseOctaves = 4;
	static constexpr double kBankNoiseWavelengthM = 6.0;
	static constexpr int64_t kShoreLowAmpMm = 1200;
	static constexpr int kShoreLowOctaves = 2;
	static constexpr double kShoreLowWavelengthM = 26.0;
	static constexpr float kNoiseLacunarity = 2.0F;
	static constexpr float kNoiseGain = 0.5F;
	/// Upper bound on either warp component: each term's noise is clamped to
	/// [-1, 1], so this is exact, which warpField's skip relies on.
	static constexpr int64_t kMaxWarpMm = kBankNoiseAmpMm + kShoreLowAmpMm;

	static constexpr int kChaikinIterations = 1;
	static constexpr int64_t kRingSpacingMm = 250;
	static constexpr int64_t kRingSimplifyEpsMm = 100;
	/// First retry when the simplified ring folds (D6 step 7).
	static constexpr int64_t kRingSimplifyRetryEpsMm = 50;
	/// Loops under a quarter tile are dropped (D6 step 8).
	static constexpr int64_t kMinLoopAreaMm2 = 250000;

	// ============ ShoreProfile heuristics (D15) ============

	/// How far off a ring vertex, along its normal, the land-side and water-side
	/// tiles are probed.
	static constexpr int64_t kProfileProbeMm = 600;
	/// Grade (rise over run) that maps to slope 255.
	static constexpr float kSlopeFullScale = 0.25F;
	/// Arc-length window the concavity term of exposure is measured over.
	static constexpr int64_t kExposureWindowMm = 20000;
	/// Bulge of a vertex off its window chord that maps to fully exposed
	/// (headland) or fully sheltered (bay).
	static constexpr float kExposureBulgeFullScaleMm = 3000.0F;
	/// Ocean rings only: open water distance along the water-side normal, capped
	/// here, and its share of exposure.
	static constexpr int kFetchCapTiles = 200;
	static constexpr float kFetchWeight = 0.5F;
	/// Sediment shores split between sand and mud: the base share of the
	/// dominant substrate, shifted toward sand when exposed and mud when
	/// sheltered by up to this much either way.
	static constexpr float kDominantSubstrateShare = 0.8F;
	static constexpr float kExposureSubstrateBias = 0.3F;
};

} // namespace engine::world
