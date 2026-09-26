#pragma once

// TerrainPolygonBuilder - builds a chunk's terrain polygon rings on the
// generation worker (docs/technical/organic-terrain/terrain-polygons-architecture.md
// D4-D8, D15). The only place the recipes are spelled out:
//
//  - Waterline (D5, D6): biome water indicator over the extended region (chunk
//    plus apron), 3x3 binomial blur, thin-feature guard, world-space domain warp
//    onto a 250 mm lattice, one march at iso 0.5, Chaikin.
//  - Pond (D8): the PondNetwork2D rim sampled every 0.3 m, radially perturbed by
//    world-space noise.
//  - Channel (D7): gathered river segments joined into chains, Catmull-Rom
//    centerline, per-sample bank offsets (bend asymmetry, bank noise, a radius
//    clamp), mouth flares into lakes and ponds, split where the width crosses
//    the fordable threshold, stroked into ribbons.
//
// Then one tail for all three: border pins, resample, simplify, validate. Rings
// come out over the extended region (`rings`, with per-vertex ShoreProfiles) and
// clipped to the chunk square (`navRings`); channels also emit thalwegs.
//
// Everything is a pure function of world position, the tiles, the gathered
// segments and ponds, and the world seed, so two chunks built independently
// produce bit-identical vertices where their clipped rings meet on the shared
// border (D4, D14).

#include "world/chunk/Chunk.h"
#include "world/chunk/ChunkCoordinate.h"
#include "world/chunk/TerrainPolygons.h"

#include <worldgen/sampling/PondNetwork2D.h>
#include <worldgen/sampling/RiverNetwork2D.h>

#include <cstdint>
#include <functional>
#include <span>

namespace engine::world {

/// Biome water per D5: ocean, lake, and both wetlands. Never TileData::surface,
/// which is also Water for river and pond tiles (stroked separately, D7/D8).
[[nodiscard]] bool isBiomeWater(const TileData& tile);

class TerrainPolygonBuilder {
  public:
	/// Tile at extended-region coordinates (ex, ey), each in [0, kExtendedSize);
	/// (kApronTiles, kApronTiles) is the chunk's own tile (0, 0).
	using ExtendedTileFn = std::function<const TileData&(int32_t ex, int32_t ey)>;

	using RiverSegment = worldgen::RiverNetwork2D::Segment;
	using Pond = worldgen::PondNetwork2D::Pond;

	/// Build the chunk's rings from its extended-region tiles and the river
	/// segments and ponds gathered for it (ChunkSampleResult, gathered over the
	/// chunk square plus kRiverGatherMarginM). Chunk::generate passes
	/// ExtendedTiles over its own tiles plus an ApronField; tests can pass
	/// hand-built tiles and segment lists.
	[[nodiscard]] static ChunkTerrainPolygons build(
		ChunkCoordinate coord,
		uint64_t worldSeed,
		const ExtendedTileFn& tiles,
		std::span<const RiverSegment> riverSegments,
		std::span<const Pond> ponds
	);

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

	// ============ Channels (D7) ============

	/// Catmull-Rom centerline sampling, per span (D7 step 1).
	static constexpr double kCenterlineSpacingM = 0.5;
	/// A chain node closer than this to its predecessor is dropped before
	/// sampling: the gather leaves sub-centimeter steps at coarse-tile joints,
	/// and a step that short has no usable direction once quantized to the mm.
	static constexpr double kMinChainSpanM = 0.25;
	/// Centerline samples farther than this outside the extended region are
	/// trimmed. The gather cut and the Catmull-Rom span it distorts lie beyond
	/// it, so every kept sample is identical in every chunk that keeps it.
	static constexpr double kChainKeepMarginM = 16.0;
	/// A channel at least this wide blocks movement (D7 step 5).
	static constexpr double kFordableWidthM = 1.2;
	/// Bend asymmetry (D7 step 3): a = min(kAsymmetryMaxFrac, gain |k| hw) hw;
	/// the outer bank moves out by a, the inner bank by kInnerBankAsymmetryShare a.
	static constexpr double kAsymmetryMaxFrac = 0.25;
	static constexpr double kAsymmetryCurvatureGain = 2.0;
	static constexpr double kInnerBankAsymmetryShare = 0.4;
	/// Bank noise (D7 step 4, section 4), each term a fraction of hw per bank.
	static constexpr double kChannelBankNoiseFrac = 0.15;
	static constexpr int kChannelBankNoiseOctaves = 3;
	static constexpr double kChannelBankNoiseWavelengthM = 4.0;
	static constexpr double kChannelLowNoiseFrac = 0.35;
	static constexpr int kChannelLowNoiseOctaves = 2;
	static constexpr double kChannelLowNoiseWavelengthM = 14.0;
	/// Noise damping max(floor, 1 - turn / kNoiseDampTurnRad), where turn is the
	/// centerline's turning over one fine-noise wavelength, |k| x 4 m.
	static constexpr double kNoiseDampTurnRad = 1.2;
	static constexpr double kNoiseDampFloor = 0.25;
	/// Inner-bank offsets are clamped to this share of the local radius of
	/// curvature, so the stroke cannot fold (replaces a whole-ring retry, which
	/// a neighbor chunk would not repeat).
	static constexpr double kRadiusClampFrac = 0.9;
	static constexpr double kCapSpacingM = 0.5;
	/// River mouths (D7): over kMouthFlareW widths above the mouth the half-width
	/// ramps up to kMouthFlare x, and the ribbon runs kMouthExtendW widths into
	/// the receiving body.
	static constexpr double kMouthFlare = 1.6;
	static constexpr double kMouthFlareW = 2.0;
	static constexpr double kMouthExtendW = 1.0;
	/// ThalwegPath::widthRatio window, in local widths.
	static constexpr double kThalwegWindowW = 5.0;

	// ============ Ponds (D8) ============

	static constexpr double kPondRimSpacingM = 0.3;
	/// The D6 fine bank term at half amplitude, applied radially.
	static constexpr int64_t kPondRimNoiseAmpMm = kBankNoiseAmpMm / 2;

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
