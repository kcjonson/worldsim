// TerrainDistanceField tests (terrain-polygons-architecture.md D10 10.1/10.4,
// D14, section 5): analytic distances, the union shoreline at a river mouth,
// thalweg ratio and frame, the worldgen arc coordinate, containment (holes,
// synthetic and cut edges), near-tile allocation and gutters, bit-identical
// gutter texels across a chunk border for hand-built and builder-built rings,
// and determinism across threads.

#include "world/chunk/TerrainDistanceField.h"

#include "world/chunk/Chunk.h"
#include "world/chunk/ChunkCoordinate.h"
#include "world/chunk/ChunkSampleResult.h"
#include "world/chunk/TerrainPolygonBuilder.h"
#include "world/chunk/TerrainPolygonTestSupport.h"

#include <polygon/Polygon.h>
#include <predicates/Predicates.h>

#include <glm/gtc/packing.hpp>
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <future>
#include <iostream>
#include <limits>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

using namespace engine::world;
using namespace engine::world::terrain_test;

namespace {

	using Field	  = TerrainDistanceField;
	using Segment = TerrainPolygonBuilder::RiverSegment;

	constexpr uint64_t kWorldSeed	= 0xD157F1E1DULL;
	constexpr double   kTwoPi		= 2.0 * std::numbers::pi;
	constexpr int32_t  kNearLattice = Field::kTilesPerSide * Field::kNearTileTexels;

	int64_t mm(double meters) {
		return std::llround(meters * 1000.0);
	}

	float decode(uint16_t half) {
		return glm::unpackHalf1x16(half);
	}

	// Half precision: 11 significant bits, plus the float rounding before it.
	void expectHalfNear(uint16_t half, double expected, const std::string& what) {
		EXPECT_NEAR(decode(half), expected, std::abs(expected) * 1.0e-3 + 1.0e-6) << what;
	}

	// ---- Hand-built rings ----

	Ring rectRing(double x0, double y0, double x1, double y1, bool ccw = true) {
		Ring r = {{mm(x0), mm(y0)}, {mm(x1), mm(y0)}, {mm(x1), mm(y1)}, {mm(x0), mm(y1)}};
		if (!ccw) {
			std::reverse(r.begin(), r.end());
		}
		return r;
	}

	// Extra vertices every stepM along each edge, so vertex-based data (profiles)
	// covers the whole ring.
	Ring densify(const Ring& ring, double stepM) {
		Ring out;
		for (size_t i = 0; i < ring.size(); ++i) {
			const Vec2i64& a	 = ring[i];
			const Vec2i64& b	 = ring[(i + 1) % ring.size()];
			const double   len	 = std::hypot(static_cast<double>(b.x - a.x), static_cast<double>(b.y - a.y)) / 1000.0;
			const int	   steps = std::max(1, static_cast<int>(std::floor(len / stepM)));
			for (int k = 0; k < steps; ++k) {
				const double t = static_cast<double>(k) / static_cast<double>(steps);
				out.push_back({a.x + std::llround(static_cast<double>(b.x - a.x) * t), a.y + std::llround(static_cast<double>(b.y - a.y) * t)});
			}
		}
		return out;
	}

	ShoreProfile profileFromPosition(const Vec2i64& v) {
		ShoreProfile p;
		p.slope	   = static_cast<uint8_t>(1 + (v.x / 1000) % 200);
		p.exposure = static_cast<uint8_t>(1 + (v.y / 1000) % 200);
		p.sand	   = static_cast<uint8_t>(40 + (v.x / 7000) % 60);
		p.mud	   = static_cast<uint8_t>(20 + (v.y / 3000) % 60);
		return p;
	}

	TerrainRing makeRing(Ring ring, TerrainRingKind kind, WaterKind water, bool withProfiles = false) {
		TerrainRing r;
		r.profiles.resize(ring.size());
		if (withProfiles) {
			for (size_t i = 0; i < ring.size(); ++i) {
				r.profiles[i] = profileFromPosition(ring[i]);
			}
		}
		r.ring		  = std::move(ring);
		r.kind		  = kind;
		r.water		  = water;
		r.holeCapable = kind == TerrainRingKind::Waterline;
		return r;
	}

	// A thalweg through the points (meters), sampled every stepM, with per-point
	// data from functions of arc length; its arc coordinate starts at arcStartM.
	ThalwegPath thalwegThrough(
		const std::vector<std::pair<double, double>>& corners,
		double										   stepM,
		const std::function<float(double)>&			   hw,
		const std::function<float(double)>&			   ratio	 = [](double) { return 1.0F; },
		const std::function<float(double)>&			   curvature = [](double) { return 0.0F; },
		double										   arcStartM = 0.0
	) {
		ThalwegPath path;
		double		s = 0.0;
		for (size_t c = 0; c + 1 < corners.size(); ++c) {
			const auto [x0, y0] = corners[c];
			const auto [x1, y1] = corners[c + 1];
			const double len	= std::hypot(x1 - x0, y1 - y0);
			const int	 steps	= static_cast<int>(std::lround(len / stepM));
			for (int k = (c == 0 ? 0 : 1); k <= steps; ++k) {
				const double t	= static_cast<double>(k) / static_cast<double>(steps);
				const double at = s + len * t;
				path.points.push_back({mm(x0 + (x1 - x0) * t), mm(y0 + (y1 - y0) * t)});
				path.halfWidthM.push_back(hw(at));
				path.widthRatio.push_back(ratio(at));
				path.curvature.push_back(curvature(at));
				path.arcLengthM.push_back(arcStartM + at);
			}
			s += len;
		}
		return path;
	}

	// ---- Lattice access ----

	double centerM(const Field& f, int64_t texelMm, int32_t i, bool x) {
		return static_cast<double>((x ? f.originMm.x : f.originMm.y) + static_cast<int64_t>(i) * texelMm + texelMm / 2) / 1000.0;
	}

	double nearCenterM(const Field& f, int32_t i, bool x) {
		return centerM(f, Field::kSdfNearTexelMm, i, x);
	}

	double farCenterM(const Field& f, int32_t i, bool x) {
		return centerM(f, Field::kFarTexelMm, i, x);
	}

	double detailCenterM(const Field& f, int32_t i, bool x) {
		return centerM(f, Field::kDetailTexelMm, i, x);
	}

	// Near lattice texel (i, j), each in [-1, kNearLattice], from the first tile
	// that stores it (its owner or a neighbor's gutter).
	std::optional<HalfTexel> nearAt(const Field& f, int32_t i, int32_t j) {
		for (int32_t ty = std::max(0, (j - 1) / Field::kNearTileTexels - 1); ty <= std::min(Field::kTilesPerSide - 1, (j + 1) / Field::kNearTileTexels + 1);
			 ++ty) {
			const int32_t v = j - ty * Field::kNearTileTexels + 1;
			if (v < 0 || v >= Field::kNearTileStride) {
				continue;
			}
			for (int32_t tx = std::max(0, (i - 1) / Field::kNearTileTexels - 1);
				 tx <= std::min(Field::kTilesPerSide - 1, (i + 1) / Field::kNearTileTexels + 1); ++tx) {
				const int32_t u = i - tx * Field::kNearTileTexels + 1;
				if (u < 0 || u >= Field::kNearTileStride) {
					continue;
				}
				const uint16_t tile = f.nearTileAt(tx, ty);
				if (tile != Field::kNoNearTile) {
					return f.nearTexel(tile, u, v);
				}
			}
		}
		return std::nullopt;
	}

	// Signed distance (m) to the boundary of the axis-aligned rectangle, negative inside.
	double rectSignedDistance(double x, double y, double x0, double y0, double x1, double y1) {
		if (x > x0 && x < x1 && y > y0 && y < y1) {
			return -std::min({x - x0, x1 - x, y - y0, y1 - y});
		}
		const double ox = std::max({x0 - x, 0.0, x - x1});
		const double oy = std::max({y0 - y, 0.0, y - y1});
		return std::hypot(ox, oy);
	}

	double clampSdf(double d) {
		return std::clamp(d, -Field::kSdfNearM, Field::kSdfNearM);
	}

	double segmentDistance(double px, double py, double ax, double ay, double bx, double by) {
		const double abx = bx - ax;
		const double aby = by - ay;
		const double len = abx * abx + aby * aby;
		const double t	 = len > 0.0 ? std::clamp(((px - ax) * abx + (py - ay) * aby) / len, 0.0, 1.0) : 0.0;
		return std::hypot(px - ax - abx * t, py - ay - aby * t);
	}

	// Distance (m) from the axis-aligned segment [(ax, ay), (bx, by)] to the box.
	double axisSegmentBoxDistance(double ax, double ay, double bx, double by, double x0, double y0, double x1, double y1) {
		const double gx = std::max({x0 - std::max(ax, bx), 0.0, std::min(ax, bx) - x1});
		const double gy = std::max({y0 - std::max(ay, by), 0.0, std::min(ay, by) - y1});
		return std::hypot(gx, gy);
	}

	ChunkTerrainPolygons polygonsOf(std::vector<TerrainRing> rings, std::vector<ThalwegPath> thalwegs = {}) {
		ChunkTerrainPolygons p;
		p.rings	   = std::move(rings);
		p.thalwegs = std::move(thalwegs);
		p.version  = 7;
		return p;
	}

	// Checks every far and near texel (gutters included) and the tile allocation
	// against a rectangle lake's analytic field.
	void expectRectLake(const Field& f, double x0, double y0, double x1, double y1, uint8_t kind) {
		for (int32_t j = -1; j <= Field::kFarTexels; ++j) {
			for (int32_t i = -1; i <= Field::kFarTexels; ++i) {
				const double	x = farCenterM(f, i, true);
				const double	y = farCenterM(f, j, false);
				const double	d = rectSignedDistance(x, y, x0, y0, x1, y1);
				const HalfTexel t = f.farTexel(i, j);
				expectHalfNear(t.r, clampSdf(d), "far R");
				EXPECT_EQ(decode(t.g), 2.0F);
				EXPECT_EQ(decode(t.b), static_cast<float>(std::abs(d) <= Field::kSdfNearM || d < 0.0 ? kind : 0)) << x << ", " << y;
				if (::testing::Test::HasFailure()) {
					return;
				}
			}
		}
		for (int32_t ty = 0; ty < Field::kTilesPerSide; ++ty) {
			for (int32_t tx = 0; tx < Field::kTilesPerSide; ++tx) {
				const double bx0  = static_cast<double>(f.originMm.x + tx * Field::kNearTileMm) / 1000.0;
				const double by0  = static_cast<double>(f.originMm.y + ty * Field::kNearTileMm) / 1000.0;
				const double bx1  = bx0 + 16.0;
				const double by1  = by0 + 16.0;
				const double dist = std::min(
					{axisSegmentBoxDistance(x0, y0, x1, y0, bx0, by0, bx1, by1), axisSegmentBoxDistance(x1, y0, x1, y1, bx0, by0, bx1, by1),
					 axisSegmentBoxDistance(x0, y1, x1, y1, bx0, by0, bx1, by1), axisSegmentBoxDistance(x0, y0, x0, y1, bx0, by0, bx1, by1)}
				);
				EXPECT_EQ(f.nearTileAt(tx, ty) != Field::kNoNearTile, dist <= 8.5) << "tile " << tx << ", " << ty << " at " << dist << " m";
			}
		}
		for (int32_t ty = 0; ty < Field::kTilesPerSide; ++ty) {
			for (int32_t tx = 0; tx < Field::kTilesPerSide; ++tx) {
				const uint16_t tile = f.nearTileAt(tx, ty);
				if (tile == Field::kNoNearTile) {
					continue;
				}
				for (int32_t v = 0; v < Field::kNearTileStride; ++v) {
					for (int32_t u = 0; u < Field::kNearTileStride; ++u) {
						const double x = nearCenterM(f, tx * Field::kNearTileTexels + u - 1, true);
						const double y = nearCenterM(f, ty * Field::kNearTileTexels + v - 1, false);
						expectHalfNear(f.nearTexel(tile, u, v).r, clampSdf(rectSignedDistance(x, y, x0, y0, x1, y1)), "near R");
						if (::testing::Test::HasFailure()) {
							return;
						}
					}
				}
			}
		}
	}

	// ---- Seam comparison over the gutter overlap of two east-west neighbors ----

	struct SeamDiff {
		size_t compared		 = 0;
		size_t different	 = 0;	// texels whose bits differ, any texture
		double sdf			 = 0.0; // max |decoded difference| over R, G, B
		int	   profile		 = 0;	// max byte difference
		double frame		 = 0.0; // max |difference|, arc coordinate compared on the circle
		size_t inReach		 = 0;	// sdf texels whose nearest shore is within both chunks' rings
		size_t inReachDiffer = 0;
		double inReachSdf	 = 0.0;
		size_t waterSdf		 = 0; // compared sdf texels that are not the land default
		size_t profiles		 = 0; // compared nonzero profile texels
		size_t frames		 = 0; // compared nonzero frame texels
	};

	// `offsetM` is the texel center's distance past the border. Each chunk's rings
	// reach kApronTiles past its own square, so a shore the texel's R measures is
	// certainly in both chunks' rings when |R| < apron - |offsetM|.
	void noteHalf(SeamDiff& diff, const HalfTexel& a, const HalfTexel& b, double offsetM) {
		++diff.compared;
		diff.different += a == b ? 0 : 1;
		diff.waterSdf += a == Field::kLandSdfTexel ? 0 : 1;
		const double dr = std::abs(static_cast<double>(decode(a.r) - decode(b.r)));
		diff.sdf		= std::max(
			   {diff.sdf, dr, std::abs(static_cast<double>(decode(a.g) - decode(b.g))), std::abs(static_cast<double>(decode(a.b) - decode(b.b)))}
		   );
		const double reach = static_cast<double>(kApronTiles) - std::abs(offsetM);
		if (std::abs(decode(a.r)) < reach && std::abs(decode(b.r)) < reach) {
			++diff.inReach;
			diff.inReachDiffer += a.r == b.r && a.b == b.b ? 0 : 1;
			diff.inReachSdf = std::max(diff.inReachSdf, dr);
		}
	}

	// Every texel both chunks store at the same world position: the west chunk's
	// last column and gutter against the east chunk's gutter and first column,
	// on each lattice (near only where both have a tile storing it).
	SeamDiff compareSeam(const Field& west, const Field& east) {
		SeamDiff	  diff;
		const int64_t border = east.originMm.x;
		auto		  offset = [border](double centerM) { return centerM - static_cast<double>(border) / 1000.0; };

		for (int32_t k = 0; k < 2; ++k) {
			const int32_t wi = Field::kFarTexels - 1 + k;
			for (int32_t j = -1; j <= Field::kFarTexels; ++j) {
				noteHalf(diff, west.farTexel(wi, j), east.farTexel(k - 1, j), offset(farCenterM(west, wi, true)));
			}
			const int32_t ni = kNearLattice - 1 + k;
			for (int32_t j = -1; j <= kNearLattice; ++j) {
				const auto a = nearAt(west, ni, j);
				const auto b = nearAt(east, k - 1, j);
				if (a && b) {
					noteHalf(diff, *a, *b, offset(nearCenterM(west, ni, true)));
				}
			}
			const int32_t di = Field::kDetailTexels - 1 + k;
			for (int32_t j = -1; j <= Field::kDetailTexels; ++j) {
				const ByteTexel pa = west.shoreProfileTexel(di, j);
				const ByteTexel pb = east.shoreProfileTexel(k - 1, j);
				diff.compared += 2;
				diff.profiles += pa == ByteTexel{} ? 0 : 1;
				diff.different += pa == pb ? 0 : 1;
				diff.profile = std::max({diff.profile, std::abs(pa.r - pb.r), std::abs(pa.g - pb.g), std::abs(pa.b - pb.b), std::abs(pa.a - pb.a)});
				const HalfTexel fa = west.channelFrameTexel(di, j);
				const HalfTexel fb = east.channelFrameTexel(k - 1, j);
				diff.frames += fa == HalfTexel{} ? 0 : 1;
				diff.different += fa == fb ? 0 : 1;
				double ds  = std::abs(static_cast<double>(decode(fa.r) - decode(fb.r)));
				ds		   = std::min(ds, Field::kArcWrapM - ds);
				diff.frame = std::max({diff.frame, ds, std::abs(static_cast<double>(decode(fa.g) - decode(fb.g))),
									   std::abs(static_cast<double>(decode(fa.b) - decode(fb.b)))});
			}
		}
		return diff;
	}

	std::string describe(const SeamDiff& d) {
		return std::to_string(d.compared) + " texels compared, " + std::to_string(d.different) + " differ; max sdf " + std::to_string(d.sdf) +
			   ", profile " + std::to_string(d.profile) + ", frame " + std::to_string(d.frame) + "; in reach of both: " +
			   std::to_string(d.inReachDiffer) + " of " + std::to_string(d.inReach) + " differ, max R " + std::to_string(d.inReachSdf) + " (" +
			   std::to_string(d.waterSdf) + " water sdf, " + std::to_string(d.profiles) + " profile, " + std::to_string(d.frames) +
			   " frame texels)";
	}

	bool sameField(const Field& a, const Field& b) {
		return a.originMm == b.originMm && a.version == b.version && a.tileMap == b.tileMap && a.nearTexels == b.nearTexels &&
			   a.farTexels == b.farTexels && a.shoreProfile == b.shoreProfile && a.channelFrame == b.channelFrame;
	}

} // namespace

// ============================================================================
// Analytic fields
// ============================================================================

TEST(TerrainDistanceFieldTest, LayoutAndLandDefault) {
	EXPECT_EQ(Field::kLandSdfTexel.r, glm::packHalf1x16(8.0F));
	EXPECT_EQ(Field::kLandSdfTexel.g, glm::packHalf1x16(2.0F));
	EXPECT_EQ(Field::kLandSdfTexel.b, 0);
	EXPECT_EQ(sizeof(HalfTexel), 6U);
	EXPECT_EQ(Field::kTilesPerSide, 32);
	EXPECT_EQ(Field::kNearTileStride, 34);
	EXPECT_EQ(Field::kFarStride, 258);
	EXPECT_EQ(Field::kDetailStride, 514);
}

TEST(TerrainDistanceFieldTest, SquareLakeIsTheExactSignedDistance) {
	const Field f = Field::bake(
		polygonsOf({makeRing(densify(rectRing(100.0, 200.0, 160.0, 260.0), 1.0), TerrainRingKind::Waterline, WaterKind::Lake)}), {0, 0}
	);
	EXPECT_EQ(f.version, 7U);
	EXPECT_EQ(f.originMm, (Vec2i64{0, 0}));
	expectRectLake(f, 100.0, 200.0, 160.0, 260.0, static_cast<uint8_t>(WaterKind::Lake));
	EXPECT_GT(f.nearTileCount(), 0U);
	EXPECT_TRUE(f.channelFrame.empty());
	EXPECT_TRUE(f.shoreProfile.empty()); // every vertex profile is zero
}

TEST(TerrainDistanceFieldTest, StraightChannelThalwegRatioFrameAndProfile) {
	// A 4 m channel from x = 60 to 440 m along y = 300.5, its thalweg down the
	// middle, its arc coordinate starting at 1000 m.
	const double x0		 = 60.0;
	const double x1		 = 440.0;
	const double yc		 = 300.5;
	const double arc0	 = 1000.0;
	TerrainRing	 channel = makeRing(densify(rectRing(x0, yc - 2.0, x1, yc + 2.0), 1.0), TerrainRingKind::Channel, WaterKind::River, true);
	const ThalwegPath thalweg = thalwegThrough(
		{{x0, yc}, {x1, yc}}, 0.5, [](double) { return 2.0F; }, [](double) { return 1.2F; }, [](double) { return 0.05F; }, arc0
	);
	const Field f = Field::bake(polygonsOf({channel}, {thalweg}), {0, 0});

	for (int32_t j = -1; j <= Field::kFarTexels; ++j) {
		for (int32_t i = -1; i <= Field::kFarTexels; ++i) {
			const double	x  = farCenterM(f, i, true);
			const double	y  = farCenterM(f, j, false);
			const double	d  = rectSignedDistance(x, y, x0, yc - 2.0, x1, yc + 2.0);
			const double	dt = std::hypot(std::max({x0 - x, 0.0, x - x1}), y - yc);
			const HalfTexel t  = f.farTexel(i, j);
			expectHalfNear(t.r, clampSdf(d), "channel R");
			expectHalfNear(t.g, dt / 2.0 < 2.0 ? dt / 2.0 : 2.0, "channel G");
			EXPECT_EQ(decode(t.b), std::abs(d) <= 8.0 || d < 0.0 ? 3.0F : 0.0F);
		}
	}

	int framed = 0;
	for (int32_t j = -1; j <= Field::kDetailTexels; ++j) {
		for (int32_t i = -1; i <= Field::kDetailTexels; ++i) {
			const double	x  = detailCenterM(f, i, true);
			const double	y  = detailCenterM(f, j, false);
			const double	dt = std::hypot(std::max({x0 - x, 0.0, x - x1}), y - yc);
			const HalfTexel t  = f.channelFrameTexel(i, j);
			if (dt / 2.0 >= 2.0) {
				EXPECT_EQ(t, HalfTexel{}) << x << ", " << y;
				continue;
			}
			++framed;
			double s = std::fmod(arc0 + std::clamp(x, x0, x1) - x0, Field::kArcWrapM);
			double e = std::abs(static_cast<double>(decode(t.r)) - s);
			EXPECT_LE(std::min(e, Field::kArcWrapM - e), 0.035) << "frame s at x = " << x;
			expectHalfNear(t.g, 1.2, "frame width ratio");
			expectHalfNear(t.b, 0.05 * 2.0, "frame curvature x hw");
		}
	}
	EXPECT_GT(framed, 2500); // 7 rows within 4 m of the thalweg, 381 m long

	// Profile: every vertex 1 m apart, so within 7 m of the ring a vertex is in
	// reach; past 8 m of every vertex, zero.
	for (int32_t j = -1; j <= Field::kDetailTexels; ++j) {
		for (int32_t i = -1; i <= Field::kDetailTexels; ++i) {
			const double	x = detailCenterM(f, i, true);
			const double	y = detailCenterM(f, j, false);
			const double	d = std::abs(rectSignedDistance(x, y, x0, yc - 2.0, x1, yc + 2.0));
			const ByteTexel p = f.shoreProfileTexel(i, j);
			if (d <= 7.0) {
				EXPECT_NE(p, ByteTexel{}) << x << ", " << y;
			} else if (d > 8.0) {
				EXPECT_EQ(p, ByteTexel{}) << x << ", " << y;
			}
		}
	}
	// The texel just north of the bank at x = 200.5 m reads the vertex (200, 302.5) or (201, 302.5).
	const ByteTexel	   bank = f.shoreProfileTexel(200, 303);
	const ShoreProfile a	= profileFromPosition({mm(200.0), mm(302.5)});
	const ShoreProfile b	= profileFromPosition({mm(201.0), mm(302.5)});
	EXPECT_TRUE((bank == ByteTexel{a.slope, a.exposure, a.sand, a.mud}) || (bank == ByteTexel{b.slope, b.exposure, b.sand, b.mud}));
}

// The builder carries RiverNetwork2D's arc coordinate onto the thalweg, linear
// within each centerline span, and the frame reads it mod kArcWrapM.
TEST(TerrainDistanceFieldTest, ThalwegCarriesTheWorldgenArcCoordinate) {
	// A straight river along y = 256 m, 20 m segments, s = x + 5000 m.
	std::vector<Segment> rivers;
	for (double x = -60.0; x < 600.0; x += 20.0) {
		rivers.push_back({x, 256.0, x + 20.0, 256.0, 3.0F, 3.0F, x + 5000.0, x + 20.0 + 5000.0});
	}
	const ChunkCoordinate	   coord{0, 0};
	const ChunkTerrainPolygons polys = buildHand(HandTiles(coord, Biome::TemperateGrassland), kWorldSeed, rivers);
	ASSERT_EQ(polys.thalwegs.size(), 1U);
	const ThalwegPath& path = polys.thalwegs.front();
	ASSERT_EQ(path.arcLengthM.size(), path.points.size());
	for (size_t i = 0; i < path.points.size(); ++i) {
		EXPECT_NEAR(path.arcLengthM[i], static_cast<double>(path.points[i].x) / 1000.0 + 5000.0, 1.0e-3) << i;
	}

	const Field f = Field::bake(polys, coord);
	for (int32_t i = -1; i <= Field::kDetailTexels; ++i) {
		const HalfTexel t = f.channelFrameTexel(i, 255); // y = 255.5, on the thalweg
		const double	s = std::fmod(detailCenterM(f, i, true) + 5000.0, Field::kArcWrapM);
		double			e = std::abs(static_cast<double>(decode(t.r)) - s);
		EXPECT_LE(std::min(e, Field::kArcWrapM - e), 0.035) << "x = " << detailCenterM(f, i, true);
	}
}

// ============================================================================
// Containment and the union shoreline
// ============================================================================

TEST(TerrainDistanceFieldTest, IslandInALakeReadsLandAndSignMatchesPointInPolygon) {
	// Plus an irregular pond, so crossings land between integer mm.
	Ring pond;
	for (int k = 0; k < 57; ++k) {
		const double a = kTwoPi * k / 57.0;
		const double r = 20.0 + 6.0 * std::sin(3.0 * a) + 2.0 * std::cos(7.0 * a);
		pond.push_back({mm(380.0 + r * std::cos(a)) + k % 3, mm(390.0 + r * std::sin(a)) - k % 5});
	}
	const std::vector<TerrainRing> rings = {
		makeRing(rectRing(100.0, 100.0, 200.0, 200.0), TerrainRingKind::Waterline, WaterKind::Lake),
		makeRing(rectRing(140.0, 140.0, 160.0, 160.0, false), TerrainRingKind::Waterline, WaterKind::Lake),
		makeRing(pond, TerrainRingKind::Pond, WaterKind::Pond),
	};
	const Field f = Field::bake(polygonsOf(rings), {0, 0});

	const HalfTexel island = f.farTexel(74, 74); // (149, 149): island center, 9 m from its shore
	EXPECT_EQ(decode(island.r), 8.0F);
	EXPECT_EQ(decode(island.b), 0.0F);
	const HalfTexel islandShore = f.farTexel(71, 74); // (143, 149): 3 m inland
	EXPECT_NEAR(decode(islandShore.r), 3.0F, 1.0e-3F);
	EXPECT_EQ(decode(islandShore.b), 1.0F);
	const HalfTexel openLake = f.farTexel(60, 74); // (121, 149): 19 m from the island, 21 m from the shore
	EXPECT_EQ(decode(openLake.r), -8.0F);
	EXPECT_EQ(decode(openLake.b), 1.0F);
	const HalfTexel pondMiddle = f.farTexel(190, 195); // (381, 391)
	EXPECT_EQ(decode(pondMiddle.r), -8.0F);
	EXPECT_EQ(decode(pondMiddle.b), 4.0F);

	// Every far and near texel's sign agrees with exact point-in-polygon (even-odd
	// over the lake rings, solid for the pond) wherever the center is off the rings.
	auto check = [&rings](const HalfTexel& t, Vec2i64 p) {
		bool onBoundary = false;
		int	 parity		= 0;
		bool solid		= false;
		for (const TerrainRing& r : rings) {
			const auto where = geometry::pointInPolygon(p, r.ring);
			onBoundary		 = onBoundary || where == geometry::PointInPolygon::OnBoundary;
			if (where == geometry::PointInPolygon::Inside) {
				if (r.holeCapable) {
					++parity;
				} else {
					solid = true;
				}
			}
		}
		if (!onBoundary) {
			EXPECT_EQ(decode(t.r) < 0.0F, solid || parity % 2 == 1) << p.x << ", " << p.y;
		}
	};
	for (int32_t j = -1; j <= Field::kFarTexels; ++j) {
		for (int32_t i = -1; i <= Field::kFarTexels; ++i) {
			check(f.farTexel(i, j), {mm(farCenterM(f, i, true)), mm(farCenterM(f, j, false))});
		}
	}
	for (int32_t j = -1; j <= kNearLattice; j += 3) {
		for (int32_t i = -1; i <= kNearLattice; i += 3) {
			if (const auto t = nearAt(f, i, j)) {
				check(*t, {mm(nearCenterM(f, i, true)), mm(nearCenterM(f, j, false))});
			}
		}
	}
}

// A river mouth: the channel ribbon runs 30 m into the lake. The shoreline is
// the boundary of the union, so the submerged channel banks are not shore, and
// the shore runs continuously around the junction corners.
TEST(TerrainDistanceFieldTest, MouthShorelineIsTheUnionBoundary) {
	const Field f = Field::bake(
		polygonsOf(
			{makeRing(rectRing(200.0, 200.0, 300.0, 300.0), TerrainRingKind::Waterline, WaterKind::Lake),
			 makeRing(rectRing(100.0, 248.0, 230.0, 252.0), TerrainRingKind::Channel, WaterKind::River)}
		),
		{0, 0}
	);
	const HalfTexel beside = f.farTexel(107, 127); // (215, 255): 3 m from the submerged bank, 15 m from the real shore
	EXPECT_EQ(decode(beside.r), -8.0F);
	EXPECT_EQ(decode(beside.b), 1.0F);
	const HalfTexel overlap = f.farTexel(112, 125); // (225, 251): in both, 25 m from the real shore
	EXPECT_EQ(decode(overlap.r), -8.0F);
	const HalfTexel upstream = f.farTexel(67, 125); // (135, 251): channel only, 1 m from its bank
	EXPECT_EQ(decode(upstream.r), -1.0F);
	EXPECT_EQ(decode(upstream.b), 3.0F);

	// Everywhere: |R| is the distance to the union polygon's boundary.
	const std::vector<std::pair<double, double>> uni = {{100.0, 248.0}, {200.0, 248.0}, {200.0, 200.0}, {300.0, 200.0},
														 {300.0, 300.0}, {200.0, 300.0}, {200.0, 252.0}, {100.0, 252.0}};
	auto unionSigned = [&uni](double x, double y) {
		double d = std::numeric_limits<double>::max();
		for (size_t k = 0; k < uni.size(); ++k) {
			const auto [ax, ay] = uni[k];
			const auto [bx, by] = uni[(k + 1) % uni.size()];
			d					= std::min(d, segmentDistance(x, y, ax, ay, bx, by));
		}
		const bool inside = (x > 200.0 && x < 300.0 && y > 200.0 && y < 300.0) || (x > 100.0 && x < 230.0 && y > 248.0 && y < 252.0);
		return inside ? -d : d;
	};
	for (int32_t j = -1; j <= Field::kFarTexels; ++j) {
		for (int32_t i = -1; i <= Field::kFarTexels; ++i) {
			const double x = farCenterM(f, i, true);
			const double y = farCenterM(f, j, false);
			expectHalfNear(f.farTexel(i, j).r, clampSdf(unionSigned(x, y)), "far union R");
		}
	}
	int near = 0;
	for (int32_t j = -1; j <= kNearLattice; ++j) {
		for (int32_t i = -1; i <= kNearLattice; ++i) {
			if (const auto t = nearAt(f, i, j)) {
				const double x = nearCenterM(f, i, true);
				const double y = nearCenterM(f, j, false);
				expectHalfNear(t->r, clampSdf(unionSigned(x, y)), "near union R at " + std::to_string(x) + ", " + std::to_string(y));
				++near;
				if (::testing::Test::HasFailure()) {
					return;
				}
			}
		}
	}
	EXPECT_GT(near, 10000);
}

TEST(TerrainDistanceFieldTest, SyntheticAndCutEdgesCloseRingsButAreNotShore) {
	// A lake closed along x = 500 m by a synthetic edge, and a fordable channel
	// piece whose east end is a cut.
	TerrainRing lake = makeRing(rectRing(400.0, 100.0, 500.0, 200.0), TerrainRingKind::Waterline, WaterKind::Lake);
	lake.profiles[1].flags |= ShoreProfile::kFlagSynthetic; // (500, 100) -> (500, 200)
	TerrainRing channel = makeRing(rectRing(100.0, 300.0, 200.0, 304.0), TerrainRingKind::Channel, WaterKind::River);
	channel.profiles[1].flags |= ShoreProfile::kFlagFordableCut; // (200, 300) -> (200, 304)
	channel.profiles[2].flags |= ShoreProfile::kFlagFordableCut;
	const Field f = Field::bake(polygonsOf({lake, channel}), {0, 0});

	const HalfTexel inside = f.farTexel(248, 75); // (497, 151): 3 m from the synthetic edge, 49 m from real shore
	EXPECT_EQ(decode(inside.r), -8.0F);
	EXPECT_EQ(decode(inside.b), 1.0F);
	const HalfTexel beyond = f.farTexel(251, 75); // (503, 151): outside the closure
	EXPECT_EQ(decode(beyond.r), 8.0F);
	EXPECT_EQ(decode(beyond.b), 0.0F);
	EXPECT_EQ(f.nearTileAt(31, 9), Field::kNoNearTile); // only the synthetic edge is near it

	// Near texels either side of the cut at y = 302.25 m.
	const auto inCut = nearAt(f, 399, 604); // (199.75, 302.25)
	ASSERT_TRUE(inCut.has_value());
	expectHalfNear(inCut->r, -1.75, "inside the cut");
	const auto pastCut = nearAt(f, 400, 604); // (200.25, 302.25)
	ASSERT_TRUE(pastCut.has_value());
	expectHalfNear(pastCut->r, std::hypot(0.25, 1.75), "past the cut");
}

// The bake prunes its shore and thalweg searches by bounds; on irregular rings
// and crossing thalwegs of very different widths it must still find what an
// exhaustive search finds.
TEST(TerrainDistanceFieldTest, PrunedSearchesMatchBruteForce) {
	std::vector<TerrainRing> rings;
	for (int b = 0; b < 3; ++b) {
		Ring blob;
		for (int k = 0; k < 90; ++k) {
			const double a = kTwoPi * k / 90.0;
			const double r = 18.0 + 5.0 * std::sin(3.0 * a + b) + 2.5 * std::cos(8.0 * a - b);
			blob.push_back({mm(150.0 + 70.0 * b + r * std::cos(a)) + (k * 37) % 11, mm(200.0 + 25.0 * b + r * std::sin(a)) - (k * 13) % 7});
		}
		rings.push_back(makeRing(blob, b == 1 ? TerrainRingKind::Pond : TerrainRingKind::Waterline, b == 1 ? WaterKind::Pond : WaterKind::Lake));
	}
	std::vector<ThalwegPath> thalwegs = {
		thalwegThrough({{100.0, 120.0}, {260.0, 300.0}, {420.0, 260.0}}, 0.5, [](double s) { return static_cast<float>(1.0 + s / 60.0); }),
		thalwegThrough({{120.0, 330.0}, {380.0, 150.0}}, 0.5, [](double s) { return static_cast<float>(0.4 + 0.3 * std::sin(s / 9.0)); }),
		thalwegThrough({{60.0, 60.0}, {480.0, 90.0}}, 0.5, [](double) { return 22.0F; }),
	};
	const Field f = Field::bake(polygonsOf(rings, thalwegs), {0, 0});

	int checked = 0;
	for (int32_t j = -1; j <= Field::kFarTexels; j += 3) {
		for (int32_t i = -1; i <= Field::kFarTexels; i += 3) {
			const double x	  = farCenterM(f, i, true);
			const double y	  = farCenterM(f, j, false);
			double		 edge = std::numeric_limits<double>::max();
			for (const TerrainRing& r : rings) {
				for (size_t k = 0; k < r.ring.size(); ++k) {
					const Vec2i64& a = r.ring[k];
					const Vec2i64& b = r.ring[(k + 1) % r.ring.size()];
					edge = std::min(edge, segmentDistance(x, y, a.x / 1000.0, a.y / 1000.0, b.x / 1000.0, b.y / 1000.0));
				}
			}
			double ratio = 2.0;
			for (const ThalwegPath& path : thalwegs) {
				for (size_t k = 0; k + 1 < path.points.size(); ++k) {
					const double ax	 = path.points[k].x / 1000.0;
					const double ay	 = path.points[k].y / 1000.0;
					const double bx	 = path.points[k + 1].x / 1000.0;
					const double by	 = path.points[k + 1].y / 1000.0;
					const double len = (bx - ax) * (bx - ax) + (by - ay) * (by - ay);
					const double t	 = len > 0.0 ? std::clamp(((x - ax) * (bx - ax) + (y - ay) * (by - ay)) / len, 0.0, 1.0) : 0.0;
					const double hw	 = path.halfWidthM[k] + (path.halfWidthM[k + 1] - path.halfWidthM[k]) * t;
					ratio			 = std::min(ratio, segmentDistance(x, y, ax, ay, bx, by) / hw);
				}
			}
			const HalfTexel texel = f.farTexel(i, j);
			EXPECT_NEAR(std::abs(decode(texel.r)), std::min(edge, 8.0), 5.0e-3) << x << ", " << y;
			EXPECT_NEAR(decode(texel.g), ratio, 2.0e-3) << x << ", " << y;
			checked += ratio < 2.0 && edge < 8.0 ? 1 : 0;
		}
	}
	EXPECT_GT(checked, 20); // texels near both a shore and a thalweg
}

// ============================================================================
// Near tiles and gutters
// ============================================================================

TEST(TerrainDistanceFieldTest, GuttersHoldTheNeighboringSamples) {
	// Chunk (-1, 2) is x in [-512, 0] m, y in [1024, 1536] m; the lake crosses its
	// west border, so gutters past the square hold real water too.
	const double x0 = -515.3;
	const double y0 = 1100.6;
	const double x1 = -440.1;
	const double y1 = 1153.2;
	const Field	 f	= Field::bake(polygonsOf({makeRing(rectRing(x0, y0, x1, y1), TerrainRingKind::Waterline, WaterKind::Lake)}), {-1, 2});
	ASSERT_GT(f.nearTileCount(), 8U);
	int fromNeighbor = 0;
	int analytic	 = 0;
	for (int32_t ty = 0; ty < Field::kTilesPerSide; ++ty) {
		for (int32_t tx = 0; tx < Field::kTilesPerSide; ++tx) {
			const uint16_t tile = f.nearTileAt(tx, ty);
			if (tile == Field::kNoNearTile) {
				continue;
			}
			for (int32_t v = 0; v < Field::kNearTileStride; ++v) {
				for (int32_t u = 0; u < Field::kNearTileStride; ++u) {
					if (u != 0 && v != 0 && u != Field::kNearTileStride - 1 && v != Field::kNearTileStride - 1) {
						continue;
					}
					const int32_t	i	   = tx * Field::kNearTileTexels + u - 1;
					const int32_t	j	   = ty * Field::kNearTileTexels + v - 1;
					const HalfTexel gutter = f.nearTexel(tile, u, v);
					const int32_t	otx	   = i < 0 ? -1 : i / Field::kNearTileTexels;
					const int32_t	oty	   = j < 0 ? -1 : j / Field::kNearTileTexels;
					const bool		owned  = otx >= 0 && oty >= 0 && otx < Field::kTilesPerSide && oty < Field::kTilesPerSide &&
										f.nearTileAt(otx, oty) != Field::kNoNearTile;
					if (owned) {
						const HalfTexel owner = f.nearTexel(
							f.nearTileAt(otx, oty), i - otx * Field::kNearTileTexels + 1, j - oty * Field::kNearTileTexels + 1
						);
						EXPECT_EQ(gutter, owner) << "tile " << tx << ", " << ty << " gutter " << u << ", " << v;
						++fromNeighbor;
					} else {
						const double x = nearCenterM(f, i, true);
						const double y = nearCenterM(f, j, false);
						expectHalfNear(gutter.r, clampSdf(rectSignedDistance(x, y, x0, y0, x1, y1)), "gutter");
						++analytic;
					}
				}
			}
		}
	}
	EXPECT_GT(fromNeighbor, 0);
	EXPECT_GT(analytic, 0);
	// The far level's west gutter column, outside the square, holds the lake too.
	const HalfTexel farGutter = f.farTexel(-1, 38); // (-513, 1101)
	expectHalfNear(farGutter.r, clampSdf(rectSignedDistance(-513.0, 1101.0, x0, y0, x1, y1)), "far gutter");
	EXPECT_LT(decode(farGutter.r), 0.0F);
}

// ============================================================================
// Seams (D4, D14, section 5)
// ============================================================================

// Two chunks given the same world near their border (rings whole, in a
// different order, each with an unrelated ring the other never sees; one
// thalweg cut differently per chunk, one whole): every texel both store is
// bit-identical.
TEST(TerrainDistanceFieldTest, HandBuiltRingsBakeIdenticallyAcrossABorder) {
	Ring pond;
	for (int k = 0; k < 40; ++k) {
		const double a = kTwoPi * k / 40.0;
		pond.push_back({mm(512.3 + 11.0 * std::cos(a)), mm(96.7 + 9.0 * std::sin(a))});
	}
	const TerrainRing lake =
		makeRing(densify(rectRing(490.2, 200.4, 540.9, 260.1), 0.7), TerrainRingKind::Waterline, WaterKind::Lake, true);
	const TerrainRing island =
		makeRing(densify(rectRing(508.0, 220.0, 516.0, 231.0, false), 0.7), TerrainRingKind::Waterline, WaterKind::Lake, true);
	const TerrainRing channel =
		makeRing(densify(rectRing(300.0, 400.0, 700.0, 406.0), 1.3), TerrainRingKind::Channel, WaterKind::River, true);
	const TerrainRing pondRing = makeRing(pond, TerrainRingKind::Pond, WaterKind::Pond, true);
	// A second channel crossing the border into the lake: its submerged banks are
	// split and dropped the same way in both chunks.
	const TerrainRing mouth =
		makeRing(densify(rectRing(470.0, 238.0, 520.0, 243.0), 0.9), TerrainRingKind::Channel, WaterKind::River, true);
	const TerrainRing westOnly = makeRing(rectRing(50.0, 50.0, 80.0, 80.0), TerrainRingKind::Waterline, WaterKind::Ocean, true);
	const TerrainRing eastOnly = makeRing(rectRing(900.0, 50.0, 950.0, 80.0), TerrainRingKind::Waterline, WaterKind::Wetland, true);

	auto hw	   = [](double s) { return static_cast<float>(3.0 + 0.5 * std::sin(s / 20.0)); };
	auto ratio = [](double s) { return static_cast<float>(1.0 + 0.2 * std::sin(s / 13.0)); };
	auto curv  = [](double s) { return static_cast<float>(0.04 * std::sin(s / 31.0)); };
	// The channel's thalweg, each chunk's copy cut at a different place.
	const ThalwegPath westThalweg = thalwegThrough({{300.0, 403.0}, {540.0, 403.0}}, 0.5, hw, ratio, curv, 777.0);
	ThalwegPath		  eastThalweg = thalwegThrough({{300.0, 403.0}, {700.0, 403.0}}, 0.5, hw, ratio, curv, 777.0);
	{
		const auto first = static_cast<std::ptrdiff_t>(
			std::find_if(eastThalweg.points.begin(), eastThalweg.points.end(), [](const Vec2i64& p) { return p.x >= 480000; }) -
			eastThalweg.points.begin()
		);
		eastThalweg.points.erase(eastThalweg.points.begin(), eastThalweg.points.begin() + first);
		eastThalweg.halfWidthM.erase(eastThalweg.halfWidthM.begin(), eastThalweg.halfWidthM.begin() + first);
		eastThalweg.widthRatio.erase(eastThalweg.widthRatio.begin(), eastThalweg.widthRatio.begin() + first);
		eastThalweg.curvature.erase(eastThalweg.curvature.begin(), eastThalweg.curvature.begin() + first);
		eastThalweg.arcLengthM.erase(eastThalweg.arcLengthM.begin(), eastThalweg.arcLengthM.begin() + first);
	}
	// A wide river crossing the border diagonally, whole in both.
	const ThalwegPath wide = thalwegThrough(
		{{380.0, 300.0}, {640.0, 360.0}}, 0.5, [](double s) { return static_cast<float>(14.0 + 4.0 * std::sin(s / 40.0)); }, ratio, curv, 31.0
	);

	const Field west = Field::bake(polygonsOf({westOnly, lake, island, channel, mouth, pondRing}, {westThalweg, wide}), {0, 0});
	const Field east = Field::bake(polygonsOf({pondRing, mouth, channel, island, lake, eastOnly}, {wide, eastThalweg}), {1, 0});

	const SeamDiff diff = compareSeam(west, east);
	std::cout << "[ hand seam ] " << describe(diff) << "\n";
	EXPECT_EQ(diff.different, 0U) << describe(diff);
	EXPECT_GT(diff.waterSdf, 300U);
	EXPECT_GT(diff.profiles, 20U);
	EXPECT_GT(diff.frames, 20U);
}

// The same check on rings the builder makes, each chunk from its own extended
// tiles and gathered segments: a lake across the border, a meandering river
// crossing it into the lake, and a narrow creek crossing it. Fails until the
// unclipped rings are seam-exact near the border and reach far enough past it.
TEST(TerrainDistanceFieldTest, BuilderRingsBakeIdenticallyAcrossABorder) {
	auto biome = [](int64_t tx, int64_t ty) {
		return (tx >= 490 && tx < 560 && ty >= 200 && ty < 290) ? Biome::Lake : Biome::TemperateGrassland;
	};
	std::vector<Segment> rivers;
	auto riverAlong = [&rivers](double x0, double x1, const std::function<double(double)>& y, const std::function<double(double)>& hw) {
		for (double x = x0; x < x1; x += 15.0) {
			rivers.push_back({x, y(x), x + 15.0, y(x + 15.0), static_cast<float>(hw(x)), static_cast<float>(hw(x + 15.0)), x, x + 15.0});
		}
	};
	riverAlong(200.0, 530.0, [](double x) { return 330.0 + 20.0 * std::sin(kTwoPi * x / 130.0); }, [](double) { return 4.0; });
	riverAlong(250.0, 800.0, [](double x) { return 440.0 + 7.0 * std::sin(kTwoPi * x / 60.0); }, [](double) { return 0.45; });

	auto build = [&](ChunkCoordinate c) {
		const double		 minX = static_cast<double>(c.x) * kChunkSize - kRiverGatherMarginM;
		const double		 maxX = minX + kChunkSize + 2.0 * kRiverGatherMarginM;
		std::vector<Segment> gathered;
		for (const Segment& s : rivers) {
			const double pad = static_cast<double>(std::max(s.halfWidth0, s.halfWidth1));
			if (std::max(s.x0, s.x1) + pad >= minX && std::min(s.x0, s.x1) - pad <= maxX) {
				gathered.push_back(s);
			}
		}
		ChunkTerrainPolygons polys = buildHand(HandTiles(c, biome), kWorldSeed, gathered);
		polys.version			   = 1;
		return polys;
	};
	const ChunkTerrainPolygons westPolys = build({0, 0});
	const ChunkTerrainPolygons eastPolys = build({1, 0});
	ASSERT_FALSE(westPolys.thalwegs.empty());
	ASSERT_FALSE(eastPolys.thalwegs.empty());
	const Field west = Field::bake(westPolys, {0, 0});
	const Field east = Field::bake(eastPolys, {1, 0});

	const SeamDiff diff = compareSeam(west, east);
	std::cout << "[ builder seam ] " << describe(diff) << "\n";
	EXPECT_GT(diff.waterSdf, 100U);
	EXPECT_GT(diff.frames, 5U);
	EXPECT_EQ(diff.different, 0U) << describe(diff);
}

// ============================================================================
// Chunk integration and determinism (D14)
// ============================================================================

TEST(TerrainDistanceFieldTest, AllLandChunkStoresNothing) {
	auto chunk = std::make_unique<Chunk>(
		ChunkCoordinate{3, -2}, makeUniformChunkSampleResult(BiomeWeights::single(Biome::TemperateGrassland), 10.0F), kWorldSeed
	);
	chunk->generate();
	const Field& f = chunk->terrainDistanceField();
	EXPECT_EQ(f.version, chunk->terrainPolygons().version);
	EXPECT_EQ(f.version, 1U);
	EXPECT_EQ(f.originMm, (Vec2i64{3 * 512000, -2 * 512000}));
	EXPECT_EQ(f.nearTileCount(), 0U);
	EXPECT_TRUE(f.farTexels.empty());
	EXPECT_TRUE(f.shoreProfile.empty());
	EXPECT_TRUE(f.channelFrame.empty());
	EXPECT_EQ(f.farTexel(100, 100), Field::kLandSdfTexel);
	EXPECT_EQ(std::count(f.tileMap.begin(), f.tileMap.end(), Field::kNoNearTile), Field::kTilesPerSide * Field::kTilesPerSide);
}

TEST(TerrainDistanceFieldTest, IdenticalAcrossThreadCounts) {
	std::vector<Segment> rivers;
	for (double x = -40.0; x < 1060.0; x += 15.0) {
		auto y = [](double v) { return 250.0 + 30.0 * std::sin(kTwoPi * v / 150.0); };
		rivers.push_back({x, y(x), x + 15.0, y(x + 15.0), 3.5F, 3.5F, x + 40.0, x + 55.0});
	}
	const std::vector<TerrainPolygonBuilder::Pond> ponds  = {{512.4, 400.2, 14.0F, 0.9F, 2.3F, 200}};
	const std::vector<ChunkCoordinate>			   coords = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};
	auto										   generate = [&rivers, &ponds](ChunkCoordinate c) {
		ChunkSampleResult sample = makeUniformChunkSampleResult(BiomeWeights::single(Biome::TemperateGrassland), 10.0F);
		sample.riverSegments	 = rivers;
		sample.pondBlobs		 = ponds;
		auto chunk				 = std::make_unique<Chunk>(c, std::move(sample), kWorldSeed);
		chunk->generate();
		return chunk;
	};

	std::vector<std::unique_ptr<Chunk>> serial;
	for (const ChunkCoordinate& c : coords) {
		serial.push_back(generate(c));
	}
	std::vector<std::future<std::unique_ptr<Chunk>>> futures;
	for (auto it = coords.rbegin(); it != coords.rend(); ++it) {
		const ChunkCoordinate c = *it;
		futures.push_back(std::async(std::launch::async, [&generate, c] { return generate(c); }));
	}
	std::vector<std::unique_ptr<Chunk>> concurrent(coords.size());
	for (size_t i = 0; i < futures.size(); ++i) {
		concurrent[coords.size() - 1 - i] = futures[i].get();
	}

	int withWater = 0;
	for (size_t i = 0; i < coords.size(); ++i) {
		const Field& a = serial[i]->terrainDistanceField();
		EXPECT_TRUE(sameField(a, concurrent[i]->terrainDistanceField())) << "chunk " << i;
		EXPECT_EQ(a.version, serial[i]->terrainPolygons().version);
		withWater += a.nearTileCount() > 0 && !a.channelFrame.empty() ? 1 : 0;
	}
	EXPECT_GE(withWater, 2);
}
