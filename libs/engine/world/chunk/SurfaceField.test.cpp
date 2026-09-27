// SurfaceField: the land field's guard, paint rule, warp, bed rule, and render
// tiles (terrain-polygons-architecture.md D16). The warp is switched off where a
// test measures a shape, so the numbers are the D5 blur and guard alone.

#include "SurfaceField.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

using namespace engine::world;

namespace {

	constexpr uint64_t kSeed = 0xC0FFEEULL;

	using SurfaceAt = std::function<Surface(int64_t tx, int64_t ty)>;

	// Render tiles for a window, built from the surfaces of the window grown by the
	// interior reach, exactly as a chunk builds its own.
	struct HandField {
		int64_t						originX;
		int64_t						originY;
		int32_t						width;
		int32_t						height;
		std::vector<TileRenderData> tiles;

		[[nodiscard]] RenderTileView view() const { return {tiles, width, height, originX, originY}; }
	};

	HandField handField(int64_t originX, int64_t originY, int32_t width, int32_t height, const SurfaceAt& surfaceAt) {
		constexpr int32_t	 kGrow		 = kInteriorReachTiles;
		const int32_t		 grownWidth	 = width + 2 * kGrow;
		const int32_t		 grownHeight = height + 2 * kGrow;
		std::vector<uint8_t> paint(static_cast<size_t>(grownWidth) * static_cast<size_t>(grownHeight));
		for (int32_t gy = 0; gy < grownHeight; ++gy) {
			for (int32_t gx = 0; gx < grownWidth; ++gx) {
				paint[static_cast<size_t>(gy) * static_cast<size_t>(grownWidth) + static_cast<size_t>(gx)] =
					static_cast<uint8_t>(surfaceAt(originX - kGrow + gx, originY - kGrow + gy));
			}
		}
		return {originX, originY, width, height, buildRenderTiles(paint, width, height)};
	}

	SurfaceFieldParams unwarped() {
		SurfaceFieldParams params = defaultSurfaceFieldParams(kSeed);
		params.warpFineM.fill(0.0F);
		params.warpLowM.fill(0.0F);
		return params;
	}

	Surface paintedAt(const HandField& field, double x, double y, const SurfaceFieldParams& params) {
		return evaluateSurfaceField(field.view(), tilePointOf(x, y), params).surface;
	}

	// A lone tile of `patch` at (10, 10) in `sea`.
	HandField loneTile(Surface patch, Surface sea) {
		return handField(0, 0, 21, 21, [patch, sea](int64_t tx, int64_t ty) { return tx == 10 && ty == 10 ? patch : sea; });
	}

} // namespace

// The lone tile's field is floored at 0.85 and falls to its neighbors' 0.125 along
// an axis, crossing 0.5 at 0.483 m from its center; on the diagonal toward the
// 0.0625 corner it crosses at 0.276 tile per axis. A rounded blob about 1 m across.
TEST(SurfaceFieldTest, LoneTileSurvivesAsRoundedBlob) {
	const SurfaceFieldParams params = unwarped();
	for (const auto& [patch, sea] : {std::pair{Surface::Grass, Surface::Dirt}, std::pair{Surface::Dirt, Surface::Grass}}) {
		const HandField field = loneTile(patch, sea);
		EXPECT_EQ(paintedAt(field, 10.5, 10.5, params), patch);
		for (const double sign : {-1.0, 1.0}) {
			EXPECT_EQ(paintedAt(field, 10.5 + sign * 0.46, 10.5, params), patch);
			EXPECT_EQ(paintedAt(field, 10.5 + sign * 0.51, 10.5, params), sea);
			EXPECT_EQ(paintedAt(field, 10.5, 10.5 + sign * 0.46, params), patch);
			EXPECT_EQ(paintedAt(field, 10.5, 10.5 + sign * 0.51, params), sea);
			EXPECT_EQ(paintedAt(field, 10.5 + sign * 0.26, 10.5 + sign * 0.26, params), patch);
			EXPECT_EQ(paintedAt(field, 10.5 + sign * 0.30, 10.5 + sign * 0.30, params), sea);
		}
		EXPECT_EQ(paintedAt(field, 10.0, 10.0, params), sea) << "a lone tile rounds off its corners";
	}
}

// A lone hole is the same shape from the other side: the sea's field is capped at
// 0.15 on it, so a lower surface shows through a higher one and the reverse.
TEST(SurfaceFieldTest, LoneHoleStaysOpen) {
	const SurfaceFieldParams params = unwarped();
	const HandField			 field	= loneTile(Surface::Mud, Surface::Rock);
	EXPECT_EQ(paintedAt(field, 10.5, 10.5, params), Surface::Mud);
	EXPECT_EQ(paintedAt(field, 10.95, 10.5, params), Surface::Mud);
	EXPECT_EQ(paintedAt(field, 11.02, 10.5, params), Surface::Rock);
	EXPECT_NEAR(surfaceFieldValue(field.view(), tilePointOf(10.5, 10.5), static_cast<uint8_t>(Surface::Rock), params.thinFloor, params.thinCeil), 0.15F, 1e-6F);
}

// A straight 1-wide path has two same-side cardinal neighbors, so "fewer than two"
// (D5 as written) never guards it: its blur is exactly 0.5 all along its centerline
// and the path would vanish or flicker with the warp. With the axis rule it is a
// band 1.17 m wide (0.15 or 0.85 on the path, 0.75 or 0.25 on the rows beside it).
TEST(SurfaceFieldTest, StraightOneWidePathIsContinuous) {
	for (const auto& [path, sea] : {std::pair{Surface::Dirt, Surface::GrassTall}, std::pair{Surface::GrassTall, Surface::Dirt}}) {
		const HandField field = handField(0, 0, 21, 21, [path, sea](int64_t, int64_t ty) { return ty == 10 ? path : sea; });

		SurfaceFieldParams unguarded = unwarped();
		unguarded.thinFloor			 = 0.5F;
		unguarded.thinCeil			 = 0.5F;
		EXPECT_EQ(surfaceFieldValue(field.view(), tilePointOf(8.3, 10.5), static_cast<uint8_t>(path), 0.5F, 0.5F), 0.5F)
			<< "D5's rule leaves the path exactly on the isoline";
		const bool pathIsUpper = surfaceStackLevel(static_cast<uint8_t>(path)) > surfaceStackLevel(static_cast<uint8_t>(sea));
		EXPECT_EQ(paintedAt(field, 8.3, 10.5, unguarded), pathIsUpper ? path : sea) << "a tie at 0.5 goes to the higher surface";

		const SurfaceFieldParams params = unwarped();
		for (int k = 0; k <= 100; ++k) {
			const double x = 5.0 + 0.1 * k;
			EXPECT_EQ(paintedAt(field, x, 10.5, params), path) << "x=" << x;
			EXPECT_EQ(paintedAt(field, x, 10.5 - 0.56, params), path) << "x=" << x;
			EXPECT_EQ(paintedAt(field, x, 10.5 + 0.56, params), path) << "x=" << x;
			EXPECT_EQ(paintedAt(field, x, 10.5 - 0.61, params), sea) << "x=" << x;
			EXPECT_EQ(paintedAt(field, x, 10.5 + 0.61, params), sea) << "x=" << x;
		}
	}
}

// A diagonal 1-wide path joins its tiles through bilinear saddles whose value is the
// mean of the two path tiles and the two beside it. At the spec's 0.70 floor that is
// (2 * 0.70 + 2 * 0.25) / 4 = 0.475 and the path breaks into beads; at 0.85 it is
// 0.55 and the path stays whole, about 0.58 m wide at its narrowest.
TEST(SurfaceFieldTest, DiagonalOneWidePathStaysConnected) {
	for (const auto& [path, sea] : {std::pair{Surface::Grass, Surface::Sand}, std::pair{Surface::Sand, Surface::Grass}}) {
		const HandField field = handField(0, 0, 21, 21, [path, sea](int64_t tx, int64_t ty) { return tx == ty ? path : sea; });

		const SurfaceFieldParams params = unwarped();
		for (int k = 0; k <= 200; ++k) {
			const double d = 5.5 + 0.05 * k;
			EXPECT_EQ(paintedAt(field, d, d, params), path) << "along the diagonal at " << d;
		}
		EXPECT_EQ(paintedAt(field, 10.0 + 0.2, 10.0 - 0.2, params), path) << "the saddle is about 0.58 m wide";
		EXPECT_EQ(paintedAt(field, 10.0 + 0.26, 10.0 - 0.26, params), sea);

		SurfaceFieldParams specValues = unwarped();
		specValues.thinFloor		  = 0.70F;
		specValues.thinCeil			  = 0.30F;
		EXPECT_EQ(paintedAt(field, 10.0, 10.0, specValues), sea) << "0.70 / 0.30 bead the path at every saddle";
	}
}

// Priority paint: where two surfaces' fields both pass 0.5 the higher one paints;
// where none passes (four surfaces meeting at a point) the largest field does, ties
// to the higher surface.
TEST(SurfaceFieldTest, HigherSurfacePaintsOverLowerAndLargestFieldFillsJunctions) {
	const SurfaceFieldParams params = unwarped();
	// A 1-wide Grass path along row 10 beside a lone Rock tile at (10, 9): both are
	// guarded up, and Rock (higher) takes the overlap between them.
	const HandField overlap = handField(0, 0, 21, 21, [](int64_t tx, int64_t ty) {
		if (tx == 10 && ty == 9) {
			return Surface::Rock;
		}
		return ty == 10 ? Surface::Grass : Surface::Mud;
	});
	const RenderTileView view  = overlap.view();
	const TilePoint		 point = tilePointOf(10.5, 9.95);
	EXPECT_GT(surfaceFieldValue(view, point, static_cast<uint8_t>(Surface::Rock), params.thinFloor, params.thinCeil), 0.5F);
	EXPECT_GT(surfaceFieldValue(view, point, static_cast<uint8_t>(Surface::Grass), params.thinFloor, params.thinCeil), 0.5F);
	EXPECT_EQ(evaluateSurfaceField(view, point, params).surface, Surface::Rock);

	const HandField quadrants = handField(0, 0, 21, 21, [](int64_t tx, int64_t ty) {
		if (tx < 10) {
			return ty < 10 ? Surface::Mud : Surface::Sand;
		}
		return ty < 10 ? Surface::Dirt : Surface::Snow;
	});
	const SurfaceFieldSample corner = evaluateSurfaceField(quadrants.view(), tilePointOf(10.0, 10.0), params);
	EXPECT_EQ(corner.surface, Surface::Snow) << "a four-way tie goes to the highest";
	EXPECT_FLOAT_EQ(corner.field, 0.25F);
}

// The warp is clamped per component whatever the amplitudes, which keeps every read
// within the render apron (the view here is exactly the apron around one tile).
TEST(SurfaceFieldTest, WarpStaysWithinClamp) {
	SurfaceFieldParams params = defaultSurfaceFieldParams(kSeed);
	params.warpFineM.fill(50.0F);
	params.warpLowM.fill(100.0F);
	const HandField field = handField(96, -40, 1 + 2 * kRenderApronTiles, 1 + 2 * kRenderApronTiles, [](int64_t tx, int64_t ty) {
		return ((tx * 7 + ty * 3) % 5) < 2 ? Surface::Grass : Surface::Dirt;
	});
	const int64_t tile = 96 + kRenderApronTiles;
	float		  most = 0.0F;
	for (int j = 0; j < 16; ++j) {
		for (int i = 0; i < 16; ++i) {
			const TilePoint			 p{tile, -40 + kRenderApronTiles, (static_cast<float>(i) + 0.5F) / 16.0F, (static_cast<float>(j) + 0.5F) / 16.0F};
			const SurfaceFieldSample s = evaluateSurfaceField(field.view(), p, params);
			EXPECT_LE(std::abs(s.warpXM), kMaxLandWarpM);
			EXPECT_LE(std::abs(s.warpYM), kMaxLandWarpM);
			most = std::max({most, std::abs(s.warpXM), std::abs(s.warpYM)});
		}
	}
	EXPECT_EQ(most, kMaxLandWarpM) << "amplitudes this large must hit the clamp";
}

// The same world point evaluates to the same bits from any window that holds its
// reach: nothing depends on where a window starts.
TEST(SurfaceFieldTest, FieldIsAFunctionOfWorldPositionOnly) {
	const SurfaceAt world = [](int64_t tx, int64_t ty) {
		const int64_t h = (tx * 73856093LL) ^ (ty * 19349663LL);
		return (h & 7) < 3 ? Surface::GrassShort : ((h & 7) < 5 ? Surface::Dirt : Surface::Grass);
	};
	const HandField			 a		= handField(69990, -30, 40, 40, world);
	const HandField			 b		= handField(70001, -21, 25, 25, world);
	const SurfaceFieldParams params = defaultSurfaceFieldParams(kSeed);
	int						 compared = 0;
	for (int j = 0; j < 40; ++j) {
		for (int i = 0; i < 40; ++i) {
			const double			 x	= 70006.0 + 0.37 * i;
			const double			 y	= -16.0 + 0.29 * j;
			const TilePoint			 p	= tilePointOf(x, y);
			const SurfaceFieldSample sa = evaluateSurfaceField(a.view(), p, params);
			const SurfaceFieldSample sb = evaluateSurfaceField(b.view(), p, params);
			EXPECT_EQ(sa.surface, sb.surface);
			EXPECT_EQ(sa.field, sb.field);
			EXPECT_EQ(sa.warpXM, sb.warpXM);
			EXPECT_EQ(sa.warpYM, sb.warpYM);
			++compared;
		}
	}
	EXPECT_EQ(compared, 1600);
}

// 70 km out the warp still moves smoothly at millimeter steps, and the edges it
// shapes wobble off the tile lines: a straight tile edge is crossed away from x = 70010.
TEST(SurfaceFieldTest, WarpHoldsPrecisionFarFromOrigin) {
	const HandField			 field	= handField(70000, 0, 20, 20, [](int64_t tx, int64_t) { return tx < 70010 ? Surface::Grass : Surface::Sand; });
	const SurfaceFieldParams params = defaultSurfaceFieldParams(kSeed);
	float					 previousX = 0.0F;
	int						 changed   = 0;
	for (int k = 0; k < 1000; ++k) {
		const TilePoint			 p{70008, 9, static_cast<float>(k) / 1000.0F, 0.5F};
		const SurfaceFieldSample s = evaluateSurfaceField(field.view(), p, params);
		if (k > 0) {
			EXPECT_LT(std::abs(s.warpXM - previousX), 0.01F);
			changed += s.warpXM != previousX ? 1 : 0;
		}
		previousX = s.warpXM;
	}
	EXPECT_GT(changed, 900);

	int crossingsOffLine = 0;
	for (int row = 0; row < 12; ++row) {
		double last = 0.0;
		for (int k = 0; k < 800; ++k) {
			const double x = 70006.0 + 0.01 * k;
			if (paintedAt(field, x, 4.5 + row, params) == Surface::Grass) {
				last = x;
			}
		}
		crossingsOffLine += std::abs(last - 70010.0) > 0.05 ? 1 : 0;
	}
	EXPECT_GE(crossingsOffLine, 8) << "the boundary should wander off the tile line";
}

// A Water tile paints as the nearest land within three tiles, ties broken by world
// y then x; with none in reach it paints Sand.
TEST(SurfaceFieldTest, WaterPaintsAsNearestLandBed) {
	constexpr uint8_t kW = static_cast<uint8_t>(Surface::Water);
	constexpr uint8_t kD = static_cast<uint8_t>(Surface::Dirt);
	constexpr uint8_t kS = static_cast<uint8_t>(Surface::Snow);
	constexpr uint8_t kR = static_cast<uint8_t>(Surface::Rock);
	constexpr int32_t kSide	 = 9;
	constexpr int32_t kGrown = kSide + 2 * kBedReachTiles;
	auto			  grid	 = [](const std::function<uint8_t(int32_t, int32_t)>& at) {
		std::vector<uint8_t> out(static_cast<size_t>(kGrown) * kGrown);
		for (int32_t y = 0; y < kGrown; ++y) {
			for (int32_t x = 0; x < kGrown; ++x) {
				out[static_cast<size_t>(y) * kGrown + static_cast<size_t>(x)] = at(x - kBedReachTiles, y - kBedReachTiles);
			}
		}
		return out;
	};
	auto paintAt = [](const std::vector<uint8_t>& paint, int32_t x, int32_t y) { return paint[static_cast<size_t>(y) * kSide + static_cast<size_t>(x)]; };

	// Water everywhere but Dirt at (5, 4) and Snow at (3, 4), both one tile from
	// (4, 4): the lesser x wins. Rock at (4, 3) is as near and has the lesser y.
	const std::vector<uint8_t> tieX = paintSurfaces(grid([](int32_t x, int32_t y) {
		return (x == 5 && y == 4) ? kD : ((x == 3 && y == 4) ? kS : kW);
	}), kSide, kSide);
	EXPECT_EQ(paintAt(tieX, 4, 4), kS);
	const std::vector<uint8_t> tieY = paintSurfaces(grid([](int32_t x, int32_t y) {
		return (x == 5 && y == 4) ? kD : ((x == 3 && y == 4) ? kS : ((x == 4 && y == 3) ? kR : kW));
	}), kSide, kSide);
	EXPECT_EQ(paintAt(tieY, 4, 4), kR);
	EXPECT_EQ(paintAt(tieY, 5, 4), kD) << "land paints as itself";

	// Nearest by distance, not by ring: (4, 7) is 3 tiles off, (6, 6) is 2.8.
	const std::vector<uint8_t> nearest = paintSurfaces(grid([](int32_t x, int32_t y) {
		return (x == 4 && y == 7) ? kD : ((x == 6 && y == 6) ? kS : kW);
	}), kSide, kSide);
	EXPECT_EQ(paintAt(nearest, 4, 4), kS);

	const std::vector<uint8_t> open = paintSurfaces(grid([](int32_t x, int32_t) { return x == 8 ? kD : kW; }), kSide, kSide);
	EXPECT_EQ(paintAt(open, 4, 4), static_cast<uint8_t>(Surface::Sand)) << "no land within three tiles";
	EXPECT_EQ(paintAt(open, 5, 4), kD);
}

// Render tiles: the edge surface is the highest within two tiles, and a tile is
// interior only when everything within three is its own surface.
TEST(SurfaceFieldTest, RenderTilesCarryEdgeSurfaceAndInteriorBit) {
	const HandField field = handField(0, 0, 21, 21, [](int64_t tx, int64_t ty) { return tx == 10 && ty == 10 ? Surface::Rock : Surface::Grass; });
	const RenderTileView view = field.view();
	for (int64_t y = 0; y < 21; ++y) {
		for (int64_t x = 0; x < 21; ++x) {
			const int64_t		  reach = std::max(std::abs(x - 10), std::abs(y - 10));
			const TileRenderData& tile	= view.at(x, y);
			EXPECT_EQ(tile.surfaceId, static_cast<uint8_t>(reach == 0 ? Surface::Rock : Surface::Grass));
			const Surface edge = reach <= kEdgeReachTiles ? Surface::Rock : Surface::Grass;
			EXPECT_EQ(tile.edge & kRenderEdgeSurfaceMask, static_cast<uint8_t>(edge)) << "(" << x << ", " << y << ")";
			EXPECT_EQ((tile.edge & kRenderInteriorBit) != 0, reach > kInteriorReachTiles) << "(" << x << ", " << y << ")";
		}
	}
}
