#pragma once

// SurfaceField - which land surface is painted at a point
// (docs/technical/organic-terrain/terrain-polygons-architecture.md D16). Each
// surface's tile indicator is softened by the D5 3x3 binomial with the thin-feature
// guard, bilinear over tile centers, and read through one world-space domain warp
// shared by every surface; surfaces paint low to high (D13) where their field passes
// 0.5. No boundary is drawn on the tile grid.
//
// The one evaluation of the field. shaders/includes/land.glsl is the same steps in
// the same order (SurfaceFieldGolden.test.cpp renders it and compares), and
// placement asks this, so what grows on a spot matches what is painted there.
//
// Positions are a whole world tile plus the offset inside it, and every noise and
// lattice read is formed from those two parts, so the field holds its precision at
// any distance from the origin, in C++ and GLSL alike.

#include "world/chunk/Chunk.h"
#include "world/chunk/RenderTiles.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace engine::world {

inline constexpr size_t kSurfaceCount = static_cast<size_t>(Surface::Count);

/// D13 stack level, 1 (Mud) to 9 (Snow): a higher level paints over a lower one.
/// Water is not in the stack (0).
[[nodiscard]] int surfaceStackLevel(uint8_t surfaceId);

/// A tile's edge surface is the highest in the window this far around it.
inline constexpr int32_t kEdgeReachTiles = 2;
/// A tile is interior when every tile this far around it has its surface.
inline constexpr int32_t kInteriorReachTiles = 3;
/// A Water tile's bed is the nearest land tile at most this far away.
inline constexpr int32_t kBedReachTiles = 3;
/// A chunk's render tiles are a function of the final tile surfaces this far past
/// its square: the render apron, the interior reach around it, and a bed reach more.
inline constexpr int32_t kRenderSurfaceReachTiles = kRenderApronTiles + kInteriorReachTiles + kBedReachTiles;
/// Each warp component is clamped here. With the bilinear footprint and the blur
/// that is exactly kRenderApronTiles of reach from a point's own tile.
inline constexpr float kMaxLandWarpM = 1.5F;
static_assert(kRenderApronTiles == kInteriorReachTiles, "a point of the chunk square reads three tiles out");

inline constexpr int kWarpFineOctaves = 4;
inline constexpr int kWarpLowOctaves = 2;
inline constexpr float kWarpLowGain = 0.5F;
/// The warp's noise is read this far off the tile grid on both axes. Gradient noise
/// is zero at its lattice points, which at whole-meter wavelengths sit on tile
/// corners (the finer octaves' on every one), and tile corners are where unwarped
/// edges run: the wobble would be pinned to the grid there.
inline constexpr float kWarpNoisePhaseM = 0.375F;

/// A world position as a whole tile and the offset inside it, each in [0, 1).
struct TilePoint {
	int64_t x = 0;
	int64_t y = 0;
	float	fx = 0.0F;
	float	fy = 0.0F;
};

[[nodiscard]] TilePoint tilePointOf(double worldX, double worldY);

/// Per-purpose noise seeds of the warp, from the world seed: one per term, whose
/// noise pair gives the offset's x and y.
struct SurfaceFieldSeeds {
	uint32_t fine = 0;
	uint32_t low  = 0;
};

[[nodiscard]] SurfaceFieldSeeds surfaceFieldSeeds(uint64_t worldSeed);

struct SurfaceFieldParams {
	/// Warp amplitude, meters, of the fine (bank) and low-frequency terms, by surface
	/// id: the upper surface of an edge sets its character (D16 step 2).
	std::array<float, kSurfaceCount> warpFineM{};
	std::array<float, kSurfaceCount> warpLowM{};
	/// Base wavelengths in whole meters (landWavelength): the split-input noise
	/// divides world tiles into whole wavelengths.
	int32_t warpFineWavelengthM = 4;
	int32_t warpLowWavelengthM	= 26;
	/// Amplitude kept per octave of the fine term: above 0.5 its 1 m and 0.5 m
	/// octaves are strong enough to make a lone tile lopsided.
	float warpFineGain = 0.6F;
	/// Thin-feature guard (D16 step 1).
	float			  thinFloor = 0.85F;
	float			  thinCeil	= 0.15F;
	SurfaceFieldSeeds seeds{};
};

/// A tunable wavelength (meters) as the whole meters the noise uses, at least 1.
[[nodiscard]] int32_t landWavelength(float meters);

/// The field's live tunables (terrain/land/*), registered with their defaults on
/// first use. Game thread only, like Foundation::Tunables.
struct SurfaceFieldTunables {
	std::array<const float*, kSurfaceCount> warpFineM{};
	std::array<const float*, kSurfaceCount> warpLowM{};
	const float*							warpFineWavelengthM = nullptr;
	const float*							warpLowWavelengthM	= nullptr;
	const float*							warpFineGain		= nullptr;
	const float*							thinFloor			= nullptr;
	const float*							thinCeil			= nullptr;

	static const SurfaceFieldTunables& get();

	/// The current values, for an evaluation.
	[[nodiscard]] SurfaceFieldParams params(uint64_t worldSeed) const;
};

/// The defaults the tunables register, without the registry.
[[nodiscard]] SurfaceFieldParams defaultSurfaceFieldParams(uint64_t worldSeed);

struct SurfaceFieldSample {
	Surface surface = Surface::Grass; ///< painted (never Water)
	float	field	= 1.0F;			  ///< the painted surface's field
	float	warpXM	= 0.0F;			  ///< the warp offset read at the point, meters
	float	warpYM	= 0.0F;
	/// The least change to any field value that could change `surface`: every
	/// present surface's distance from 0.5 and, where no field passes 0.5, the gap
	/// between the two largest. Two evaluations whose fields differ by rounding agree
	/// on the surface wherever this exceeds the difference.
	float margin = 0.5F;
};

/// The painted surface at `point`. `tiles` must hold every tile within
/// kRenderApronTiles of the point's own tile (a chunk's render tiles do, for any point
/// of its square).
[[nodiscard]] SurfaceFieldSample evaluateSurfaceField(const RenderTileView& tiles, TilePoint point, const SurfaceFieldParams& params);

/// The unwarped field of `surfaceId` at `at`: D5 blur and guard at the tile centers
/// around it, bilinear between them. Every tile within two of `at`'s tile must be in
/// `tiles`.
[[nodiscard]] float surfaceFieldValue(const RenderTileView& tiles, TilePoint at, uint8_t surfaceId, float thinFloor, float thinCeil);

/// Paint surfaces of a width x height window: each tile's surface, or for a Water
/// tile its bed, the surface of the nearest non-Water tile within kBedReachTiles (least
/// squared distance, then least world y, then least world x), Sand when there is
/// none. A function of the world around a tile alone, so every chunk agrees on it.
/// `surfaces` covers the window grown by kBedReachTiles on each side, row-major.
[[nodiscard]] std::vector<uint8_t> paintSurfaces(std::span<const uint8_t> surfaces, int32_t width, int32_t height);

/// Render tiles of a width x height window from the paint surfaces of the window
/// grown by kInteriorReachTiles on each side (row-major): the paint surface, the
/// highest surface within kEdgeReachTiles as the edge surface, and the interior bit.
[[nodiscard]] std::vector<TileRenderData> buildRenderTiles(std::span<const uint8_t> paint, int32_t width, int32_t height);

} // namespace engine::world
