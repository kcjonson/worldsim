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
// Then one tail for all three: world-lattice pins, resample, simplify, validate.
// Rings come out over the extended region (`rings`, with per-vertex
// ShoreProfiles) and clipped to the chunk square (`navRings`); channels also
// emit thalwegs.
//
// Everything is a pure function of world position, the tiles, the gathered
// segments and ponds, and the world seed. Pins sit on a world lattice, so every
// resampled run depends only on the curve between two lattice crossings, and the
// apron and channel reaches are sized so that curve is free of edge effects
// wherever a border texel of the distance-field bake can see it. Two chunks built
// independently therefore produce bit-identical ring edges within the bake's
// reach of their shared border, bit-identical thalweg points within reach of
// their bake regions, and bit-identical vertices where their clipped rings meet
// on the border (D4, D10, D14).

#include "world/chunk/Chunk.h"
#include "world/chunk/ChunkCoordinate.h"
#include "world/chunk/TerrainPolygons.h"

#include <worldgen/sampling/PondNetwork2D.h>
#include <worldgen/sampling/RiverNetwork2D.h>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <span>

namespace engine::world {

/// Biome water per D5: ocean, lake, and both wetlands. Never TileData::surface,
/// which is also Water for river and pond tiles (stroked separately, D7/D8).
[[nodiscard]] bool isBiomeWater(Biome primaryBiome);
[[nodiscard]] bool isBiomeWater(const TileData& tile);

class TerrainPolygonBuilder {
  public:
	/// Tile at extended-region coordinates (ex, ey), each in [0, kChunkSize + 2
	/// apronTiles); (apronTiles, apronTiles) is the chunk's own tile (0, 0).
	using ExtendedTileFn = std::function<const TileData&(int32_t ex, int32_t ey)>;

	/// isBiomeWater of world tile (tx, ty), the tile covering [tx, tx + 1) m x
	/// [ty, ty + 1) m. Must agree with ExtendedTileFn over the extended region and
	/// answer for every tile within kBiomeWaterReachM of the chunk square: river
	/// mouths are found by evaluating the waterline field along the centerline
	/// out there (D7). Chunk::generate answers from its 3x3 neighborhood's biome
	/// grids (NeighborhoodGrids), which is what each neighbor's own tiles read.
	using BiomeWaterFn = std::function<bool(int64_t tx, int64_t ty)>;

	using RiverSegment = worldgen::RiverNetwork2D::Segment;
	using Pond = worldgen::PondNetwork2D::Pond;

	/// Build the chunk's rings from its extended-region tiles, the biome water
	/// around it, and the river segments and ponds gathered for it
	/// (ChunkSampleResult, gathered over the chunk square plus
	/// kRiverGatherMarginM). Chunk::generate passes ExtendedTiles over its own
	/// tiles plus an ApronField; tests can pass hand-built tiles and segment
	/// lists, and build a reference with a wider apron to measure edge effects.
	[[nodiscard]] static ChunkTerrainPolygons build(
		ChunkCoordinate coord,
		uint64_t worldSeed,
		const ExtendedTileFn& tiles,
		const BiomeWaterFn& biomeWater,
		std::span<const RiverSegment> riverSegments,
		std::span<const Pond> ponds,
		int32_t apronTiles = kApronTiles
	);

	// ============ Recipe parameters (spec section 4) ============

	static constexpr int64_t kTileMm = 1000;
	/// Every ring is pinned where it crosses a line x = k * kPinLatticeMm or
	/// y = k * kPinLatticeMm (D4, D6). Chunk borders are lattice lines, so the
	/// nav clip meets its pins there. A resample and simplify run then spans one
	/// lattice cell at most.
	static constexpr int64_t kPinLatticeMm = 16000;
	static_assert((static_cast<int64_t>(kChunkSize) * kTileMm) % kPinLatticeMm == 0, "chunk borders must be lattice lines");
	/// The distance-field bake (D10): exact distance clamped at kSdfNearM, and
	/// texels over the chunk square grown by kBakeMarginM, where the thalweg
	/// channels (G, channelFrame) are read out to kThalwegReachHalfWidths bankfull
	/// half-widths from a thalweg.
	static constexpr double kSdfNearM = 8.0;
	static constexpr double kBakeMarginM = 16.0;
	static constexpr double kThalwegReachHalfWidths = 2.0;
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

	/// How far in from the extended boundary the waterline can differ from a
	/// build with a wider apron: the outermost coarse sample's blur reads forced
	/// land, the bilinear read uses that sample out to 1.5 tiles in, the warp reads
	/// kMaxWarpMm further, and a ring vertex moves with its marching cell and
	/// Chaikin's neighbor (two fine cells). TerrainPolygonSeamsTest measures ~2 m
	/// of field on a lake-strewn world. The apron holds the pin-lattice cell next
	/// to the border (every run a border texel reads) clear of it.
	static constexpr int64_t kEdgeEffectDepthMm = 3 * kTileMm / 2 + kMaxWarpMm + 2 * kFineCellMm;
	static_assert(
		static_cast<int64_t>(kApronTiles) * kTileMm >= kPinLatticeMm + kEdgeEffectDepthMm,
		"the apron must keep the border lattice cell clear of the edge effect"
	);

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
	/// River mouths (D7): over kMouthFlareW widths above the mouth (at most
	/// kMouthFlareMaxM) the half-width ramps up to kMouthFlare x, and the ribbon
	/// runs kMouthExtendW widths (at most kMouthExtendMaxM) into the receiving
	/// body. The caps keep a mouth's reach, and so the distance a chunk must see
	/// along a river to agree with its neighbors, bounded for the widest rivers.
	static constexpr double kMouthFlare = 1.6;
	static constexpr double kMouthFlareW = 2.0;
	static constexpr double kMouthFlareMaxM = 64.0;
	static constexpr double kMouthExtendW = 1.0;
	static constexpr double kMouthExtendMaxM = 24.0;
	/// ThalwegPath::widthRatio window, in local widths, each half at most
	/// kThalwegHalfWindowMaxM of arc.
	static constexpr double kThalwegWindowW = 5.0;
	static constexpr double kThalwegHalfWindowMaxM = 64.0;

	// ============ Channel reach (D4, D10, D14) ============
	//
	// A centerline sample is relevant when it can shape a ring edge in the lattice
	// cell next to the chunk square (the run a border texel reads), a thalweg
	// point a bake texel reads, or a ring edge anywhere in the extended region:
	// within the reach base plus kChannelReachHalfWidths of its own raw
	// half-width of the chunk square (Chebyshev). Every decision about a relevant
	// sample (mouth flare, mouth extension, the whole-crossing rule) looks at most
	// kChannelDecisionReachM along the centerline, so samples that far from a
	// relevant one are kept too and read ground from the waterline field, which is
	// a function of world position alone. A kept run therefore ends where its
	// butt cap lies outside the extended region. The gather and the biome water
	// query cover all of it.

	/// Sample spacing (one parameter step can run ~10% over kCenterlineSpacingM),
	/// quantization, and a thalweg's neighbor point.
	static constexpr double kChannelReachSlackM = 1.0;
	/// The reach base at the game's apron; a build with a wider apron (a test
	/// reference) grows it to that apron. Two slacks: the thalweg keeps its points
	/// out to one past the bake region, and the samples either side of such a
	/// point must be relevant themselves.
	static constexpr double kChannelReachBaseM =
		std::max({static_cast<double>(kPinLatticeMm) / 1000.0, kBakeMarginM, static_cast<double>(kApronTiles)}) + 2.0 * kChannelReachSlackM;
	/// A thalweg point lies up to the asymmetry off its centerline sample and a
	/// texel reads it kThalwegReachHalfWidths out, both in flared half-widths; a
	/// bank lies at most (1 + asymmetry + both noise terms) flared half-widths out.
	static constexpr double kChannelReachHalfWidths = (kThalwegReachHalfWidths + kAsymmetryMaxFrac) * kMouthFlare;
	static_assert(
		(1.0 + kAsymmetryMaxFrac + kChannelBankNoiseFrac + kChannelLowNoiseFrac) * kMouthFlare <= kChannelReachHalfWidths,
		"a bank must lie within the channel reach of its centerline sample"
	);
	/// A flare looks kMouthFlareMaxM downstream; a water sample is kept by a
	/// transition up to kMouthExtendMaxM away, and the whole-crossing rule needs
	/// both ends of a crossing up to two extensions long, so three extensions.
	static constexpr double kChannelDecisionReachM = std::max(kMouthFlareMaxM, 3.0 * kMouthExtendMaxM) + 2.0 * kChannelReachSlackM;
	/// Farthest a kept centerline sample lies from the chunk square.
	static constexpr double kChannelKeepReachM =
		kChannelReachBaseM + kChannelReachHalfWidths * worldgen::RiverNetwork2D::kMaxHalfWidthMeters + kChannelDecisionReachM;
	/// The waterline field at a point reads biome water up to the warp, one
	/// bilinear cell, and the blur (plus the half tile to a tile's center) beyond it.
	static constexpr double kBiomeWaterReachM = kChannelKeepReachM + static_cast<double>(kMaxWarpMm + 5 * kTileMm / 2) / 1000.0;
	static_assert(
		kBiomeWaterReachM <= static_cast<double>(kChunkSize), "the biome water query must stay inside the 3x3 chunk neighborhood"
	);

	// ============ Ponds (D8) ============

	static constexpr double kPondRimSpacingM = 0.3;
	/// The D6 fine bank term at half amplitude, applied radially.
	static constexpr int64_t kPondRimNoiseAmpMm = kBankNoiseAmpMm / 2;

	// ============ ShoreProfile heuristics (D15) ============

	/// How far off a ring vertex, along its normal, the land-side and water-side
	/// tiles are probed.
	static constexpr int64_t kProfileProbeMm = 600;
	/// Slope (L2, R2, R3) is a shading heuristic, not a measurement: tile
	/// elevation is bilinear from chunk corners 512 m apart and carries almost no
	/// local relief. It is a baseline by water kind (channels by bank side), plus
	/// a land-rise term, plus along-shore noise, held to [kSlopeMin, kSlopeMax].
	static constexpr float kSlopeOcean = 0.40F;
	static constexpr float kSlopeLake = 0.35F;
	static constexpr float kSlopeWetland = 0.15F;
	static constexpr float kSlopePond = 0.28F;
	/// Channel banks: a straight reach, moved by a full bend toward a steep outer
	/// (cut) bank or a gentle inner (point-bar) bank. The bend is the bank's bulge
	/// off its chord over a window short enough to see a small stream's meanders.
	static constexpr float kSlopeChannelStraight = 0.45F;
	static constexpr float kSlopeChannelBendGain = 0.35F;
	static constexpr int64_t kChannelBendWindowMm = 10000;
	static constexpr float kChannelBendBulgeFullScaleMm = 1500.0F;
	/// Land rise above the water level kSlopeRunMm landward of the ring, as a
	/// grade, adding up to kSlopeRiseWeight: half of it at kSlopeRiseHalfGrade,
	/// so real coastal relief moves slope without saturating it. The run stays
	/// inside the apron for every vertex a neighbor's bake can read.
	static constexpr int64_t kSlopeRunMm = 8000;
	static constexpr float kSlopeRiseWeight = 0.3F;
	static constexpr float kSlopeRiseHalfGrade = 0.25F;
	/// A vertex whose probes don't resolve takes its rise from the nearest
	/// resolved vertices within this much arc length either side (D15: no zeros).
	static constexpr double kSlopeInheritReachMm = 4000.0;
	/// World-space fBm at the vertex, so no stretch of shore is uniform (D15).
	static constexpr double kSlopeNoiseWavelengthM = 20.0;
	static constexpr int kSlopeNoiseOctaves = 3;
	static constexpr float kSlopeNoiseAmp = 0.2F;
	static constexpr float kSlopeMin = 0.06F;
	static constexpr float kSlopeMax = 0.94F;
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
