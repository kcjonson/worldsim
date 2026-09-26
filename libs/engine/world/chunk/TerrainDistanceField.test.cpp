// TerrainDistanceField tests (terrain-polygons-architecture.md D10 10.1/10.4,
// D14, section 5): analytic distances, thalweg ratio and frame, arc-length
// anchoring, containment (holes, overlaps, synthetic and cut edges), near-tile
// allocation and gutters, bit-identical texels across a chunk border for
// hand-built and builder-built rings, and determinism across threads.

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

	constexpr uint64_t kWorldSeed = 0xD157F1E1DULL;
	constexpr double   kTwoPi	  = 2.0 * std::numbers::pi;

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
	// data from functions of arc length.
	ThalwegPath thalwegThrough(
		const std::vector<std::pair<double, double>>& corners,
		double										   stepM,
		const std::function<float(double)>&			   hw,
		const std::function<float(double)>&			   ratio	 = [](double) { return 1.0F; },
		const std::function<float(double)>&			   curvature = [](double) { return 0.0F; }
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
			}
			s += len;
		}
		return path;
	}

	// ---- Lattice access ----

	double nearCenterM(const Field& f, int32_t i, bool x) {
		return static_cast<double>((x ? f.originMm.x : f.originMm.y) + static_cast<int64_t>(i) * Field::kNearTexelMm + Field::kNearTexelMm / 2) /
			   1000.0;
	}

	double farCenterM(const Field& f, int32_t i, bool x) {
		return static_cast<double>((x ? f.originMm.x : f.originMm.y) + static_cast<int64_t>(i) * Field::kFarTexelMm + Field::kFarTexelMm / 2) /
			   1000.0;
	}

	double detailCenterM(const Field& f, int32_t i, bool x) {
		return static_cast<double>(
				   (x ? f.originMm.x : f.originMm.y) + static_cast<int64_t>(i) * Field::kDetailTexelMm + Field::kDetailTexelMm / 2
			   ) /
			   1000.0;
	}

	// Near lattice texel (i, j), from the tile that owns it (not a gutter copy).
	std::optional<HalfTexel> nearAt(const Field& f, int32_t i, int32_t j) {
		if (i < 0 || j < 0 || i >= Field::kTilesPerSide * Field::kNearTileTexels || j >= Field::kTilesPerSide * Field::kNearTileTexels) {
			return std::nullopt;
		}
		const int32_t  tx	= i / Field::kNearTileTexels;
		const int32_t  ty	= j / Field::kNearTileTexels;
		const uint16_t tile = f.nearTileAt(tx, ty);
		if (tile == Field::kNoNearTile) {
			return std::nullopt;
		}
		return f.nearTexel(tile, i - tx * Field::kNearTileTexels + 1, j - ty * Field::kNearTileTexels + 1);
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

	// Checks every far and near texel against a rectangle lake's analytic field.
	void expectRectLake(const Field& f, double x0, double y0, double x1, double y1, uint8_t kind) {
		for (int32_t j = 0; j < Field::kFarSize; ++j) {
			for (int32_t i = 0; i < Field::kFarSize; ++i) {
				const double	x = farCenterM(f, i, true);
				const double	y = farCenterM(f, j, false);
				const double	d = rectSignedDistance(x, y, x0, y0, x1, y1);
				const HalfTexel t = f.farTexel(i, j);
				expectHalfNear(t.r, clampSdf(d), "far R");
				EXPECT_EQ(decode(t.g), 2.0F);
				EXPECT_EQ(decode(t.b), static_cast<float>(std::abs(d) <= Field::kSdfNearM || d < 0.0 ? kind : 0)) << x << ", " << y;
				EXPECT_EQ(t.a, 0);
				if (::testing::Test::HasFailure()) {
					return;
				}
			}
		}
		for (int32_t ty = 0; ty < Field::kTilesPerSide; ++ty) {
			for (int32_t tx = 0; tx < Field::kTilesPerSide; ++tx) {
				const double bx0 = static_cast<double>(f.originMm.x + tx * Field::kNearTileMm) / 1000.0;
				const double by0 = static_cast<double>(f.originMm.y + ty * Field::kNearTileMm) / 1000.0;
				const double bx1 = bx0 + 16.0;
				const double by1 = by0 + 16.0;
				const double dist = std::min(
					{axisSegmentBoxDistance(x0, y0, x1, y0, bx0, by0, bx1, by1), axisSegmentBoxDistance(x1, y0, x1, y1, bx0, by0, bx1, by1),
					 axisSegmentBoxDistance(x0, y1, x1, y1, bx0, by0, bx1, by1), axisSegmentBoxDistance(x0, y0, x0, y1, bx0, by0, bx1, by1)}
				);
				EXPECT_EQ(f.nearTileAt(tx, ty) != Field::kNoNearTile, dist <= 8.25) << "tile " << tx << ", " << ty << " at " << dist << " m";
			}
		}
		for (uint16_t tile = 0; tile < f.nearTileCount(); ++tile) {
			const auto	  it = std::find(f.tileMap.begin(), f.tileMap.end(), tile);
			const auto	  at = static_cast<int32_t>(it - f.tileMap.begin());
			const int32_t tx = at % Field::kTilesPerSide;
			const int32_t ty = at / Field::kTilesPerSide;
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

	// ---- Seam comparison over the strip two east-west neighbors both bake ----

	struct SeamDiff {
		size_t compared		 = 0;
		size_t different	 = 0;	// texels whose bits differ, any texture
		size_t tileMismatch	 = 0;	// near texels one chunk has a tile for and the other not
		double sdf			 = 0.0; // max |decoded difference| over R, G, B where both have the texel
		int	   profile		 = 0;	// max byte difference
		double frame		 = 0.0; // max |difference|, arc length compared on the circle
		size_t inReach		 = 0;	// sdf texels whose nearest shore is within both chunks' reach
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
		const double d	= std::max(
			 {dr, std::abs(static_cast<double>(decode(a.g) - decode(b.g))), std::abs(static_cast<double>(decode(a.b) - decode(b.b)))}
		 );
		diff.sdf			= std::max(diff.sdf, d);
		const double reach	= static_cast<double>(kApronTiles) - std::abs(offsetM);
		if (std::abs(decode(a.r)) < reach && std::abs(decode(b.r)) < reach) {
			++diff.inReach;
			diff.inReachDiffer += a.r == b.r && a.b == b.b ? 0 : 1;
			diff.inReachSdf = std::max(diff.inReachSdf, dr);
		}
	}

	// Texels of the west chunk's bake and the east chunk's at the same world
	// position, within `bandM` of the shared border (the whole shared strip when
	// bandM >= the margin).
	SeamDiff compareSeam(const Field& west, const Field& east, double bandM) {
		SeamDiff	  diff;
		const int64_t border = east.originMm.x + Field::kMarginMm;
		auto		  offset = [border](int64_t centerMm) { return static_cast<double>(centerMm - border) / 1000.0; };

		const int32_t farShift = static_cast<int32_t>((east.originMm.x - west.originMm.x) / Field::kFarTexelMm);
		for (int32_t j = 0; j < Field::kFarSize; ++j) {
			for (int32_t i = farShift; i < Field::kFarSize; ++i) {
				const double at = offset(west.originMm.x + i * Field::kFarTexelMm + Field::kFarTexelMm / 2);
				if (std::abs(at) <= bandM) {
					noteHalf(diff, west.farTexel(i, j), east.farTexel(i - farShift, j), at);
				}
			}
		}
		const int32_t nearShift = static_cast<int32_t>((east.originMm.x - west.originMm.x) / Field::kNearTexelMm);
		const int32_t nearSize	= Field::kTilesPerSide * Field::kNearTileTexels;
		for (int32_t j = 0; j < nearSize; ++j) {
			for (int32_t i = nearShift; i < nearSize; ++i) {
				const double at = offset(west.originMm.x + i * Field::kNearTexelMm + Field::kNearTexelMm / 2);
				if (std::abs(at) > bandM) {
					continue;
				}
				const auto a = nearAt(west, i, j);
				const auto b = nearAt(east, i - nearShift, j);
				if (a.has_value() != b.has_value()) {
					++diff.compared;
					++diff.different;
					++diff.tileMismatch;
				} else if (a) {
					noteHalf(diff, *a, *b, at);
				}
			}
		}
		const int32_t detailShift = static_cast<int32_t>((east.originMm.x - west.originMm.x) / Field::kDetailTexelMm);
		for (int32_t j = 0; j < Field::kDetailSize; ++j) {
			for (int32_t i = detailShift; i < Field::kDetailSize; ++i) {
				if (std::abs(offset(west.originMm.x + i * Field::kDetailTexelMm + Field::kDetailTexelMm / 2)) > bandM) {
					continue;
				}
				const ByteTexel pa = west.shoreProfileTexel(i, j);
				const ByteTexel pb = east.shoreProfileTexel(i - detailShift, j);
				diff.compared += 2;
				diff.profiles += pa == ByteTexel{} ? 0 : 1;
				diff.different += pa == pb ? 0 : 1;
				diff.profile = std::max({diff.profile, std::abs(pa.r - pb.r), std::abs(pa.g - pb.g), std::abs(pa.b - pb.b), std::abs(pa.a - pb.a)});
				const HalfTexel fa = west.channelFrameTexel(i, j);
				const HalfTexel fb = east.channelFrameTexel(i - detailShift, j);
				diff.frames += fa == HalfTexel{} ? 0 : 1;
				diff.different += fa == fb ? 0 : 1;
				double ds = std::abs(static_cast<double>(decode(fa.r) - decode(fb.r)));
				ds		  = std::min(ds, Field::kArcWrapM - ds);
				diff.frame = std::max({diff.frame, ds, std::abs(static_cast<double>(decode(fa.g) - decode(fb.g))),
									   std::abs(static_cast<double>(decode(fa.b) - decode(fb.b)))});
			}
		}
		return diff;
	}

	std::string describe(const SeamDiff& d) {
		return std::to_string(d.compared) + " texels compared, " + std::to_string(d.different) + " differ (" +
			   std::to_string(d.tileMismatch) + " near-tile allocation); max sdf " + std::to_string(d.sdf) + ", profile " +
			   std::to_string(d.profile) + ", frame " + std::to_string(d.frame) + "; in reach of both: " + std::to_string(d.inReachDiffer) +
			   " of " + std::to_string(d.inReach) + " differ, max R " + std::to_string(d.inReachSdf) + " (" + std::to_string(d.waterSdf) +
			   " water sdf, " + std::to_string(d.profiles) + " profile, " + std::to_string(d.frames) + " frame texels)";
	}

	bool sameField(const Field& a, const Field& b) {
		return a.originMm == b.originMm && a.version == b.version && a.tileMap == b.tileMap && a.nearTexels == b.nearTexels &&
			   a.farTexels == b.farTexels && a.shoreProfile == b.shoreProfile && a.channelFrame == b.channelFrame;
	}

} // namespace

// ============================================================================
// Analytic fields
// ============================================================================

TEST(TerrainDistanceFieldTest, LandDefaultMatchesHalfEncoding) {
	EXPECT_EQ(Field::kLandSdfTexel.r, glm::packHalf1x16(8.0F));
	EXPECT_EQ(Field::kLandSdfTexel.g, glm::packHalf1x16(2.0F));
	EXPECT_EQ(Field::kLandSdfTexel.b, 0);
	EXPECT_EQ(Field::kTilesPerSide, 34);
	EXPECT_EQ(Field::kFarSize, 272);
	EXPECT_EQ(Field::kDetailSize, 544);
}

TEST(TerrainDistanceFieldTest, SquareLakeIsTheExactSignedDistance) {
	const Field f = Field::bake(
		polygonsOf({makeRing(densify(rectRing(100.0, 200.0, 160.0, 260.0), 1.0), TerrainRingKind::Waterline, WaterKind::Lake)}), {0, 0}
	);
	EXPECT_EQ(f.version, 7U);
	EXPECT_EQ(f.originMm, (Vec2i64{-16000, -16000}));
	expectRectLake(f, 100.0, 200.0, 160.0, 260.0, static_cast<uint8_t>(WaterKind::Lake));
	EXPECT_GT(f.nearTileCount(), 0U);
	EXPECT_TRUE(f.channelFrame.empty());
	EXPECT_TRUE(f.shoreProfile.empty()); // every vertex profile is zero
}

TEST(TerrainDistanceFieldTest, StraightChannelThalwegRatioFrameAndProfile) {
	// A 4 m channel from x = 60 to 440 m along y = 300.5, its thalweg down the middle.
	const double x0 = 60.0;
	const double x1 = 440.0;
	const double yc = 300.5;
	TerrainRing	 channel = makeRing(densify(rectRing(x0, yc - 2.0, x1, yc + 2.0), 1.0), TerrainRingKind::Channel, WaterKind::River, true);
	const ThalwegPath thalweg = thalwegThrough(
		{{x0, yc}, {x1, yc}}, 0.5, [](double) { return 2.0F; }, [](double) { return 1.2F; }, [](double) { return 0.05F; }
	);
	const Field f = Field::bake(polygonsOf({channel}, {thalweg}), {0, 0});

	for (int32_t j = 0; j < Field::kFarSize; ++j) {
		for (int32_t i = 0; i < Field::kFarSize; ++i) {
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

	// Frame: no chunk border crossing, so arc length runs from the path start,
	// which reads the anchor value like a crossing would.
	int framed = 0;
	for (int32_t j = 0; j < Field::kDetailSize; ++j) {
		for (int32_t i = 0; i < Field::kDetailSize; ++i) {
			const double	x  = detailCenterM(f, i, true);
			const double	y  = detailCenterM(f, j, false);
			const double	dt = std::hypot(std::max({x0 - x, 0.0, x - x1}), y - yc);
			const HalfTexel t  = f.channelFrameTexel(i, j);
			if (dt / 2.0 >= 2.0) {
				EXPECT_EQ(t, HalfTexel{}) << x << ", " << y;
				continue;
			}
			++framed;
			expectHalfNear(t.r, std::fmod(Field::kArcAnchorValueM + std::clamp(x, x0, x1) - x0, 256.0), "frame s");
			expectHalfNear(t.g, 1.2, "frame width ratio");
			expectHalfNear(t.b, 0.05 * 2.0, "frame curvature x hw");
			EXPECT_EQ(t.a, 0);
		}
	}
	EXPECT_GT(framed, 2500); // 7 rows within 4 m of the thalweg, 381 m long

	// Profile: every vertex 1 m apart, so within 7.5 m of the ring a vertex is in
	// reach; past 8 m of every vertex, zero.
	for (int32_t j = 0; j < Field::kDetailSize; ++j) {
		for (int32_t i = 0; i < Field::kDetailSize; ++i) {
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
	const ByteTexel bank = f.shoreProfileTexel(216, 319);
	const ShoreProfile a = profileFromPosition({mm(200.0), mm(302.5)});
	const ShoreProfile b = profileFromPosition({mm(201.0), mm(302.5)});
	EXPECT_TRUE((bank == ByteTexel{a.slope, a.exposure, a.sand, a.mud}) || (bank == ByteTexel{b.slope, b.exposure, b.sand, b.mud}));
}

TEST(TerrainDistanceFieldTest, ArcLengthIsAnchoredAtChunkBorders) {
	// Straight across chunk (0, 0) and beyond both borders: the reach between the
	// x = 0 and x = 512 m crossings is exactly two wraps, so s = (x + 128) mod 256 everywhere.
	const double	  y		  = 300.5;
	const ThalwegPath through = thalwegThrough({{-100.0, y}, {700.0, y}}, 0.5, [](double) { return 2.0F; });
	const Field		  f		  = Field::bake(polygonsOf({}, {through}), {0, 0});
	const int32_t	  row	  = static_cast<int32_t>(std::lround(y - 0.5 + 16.0));
	for (int32_t i = 0; i < Field::kDetailSize; ++i) {
		const double x		  = detailCenterM(f, i, true);
		double		 expected = std::fmod(x + Field::kArcAnchorValueM, 256.0);
		expected			  = expected < 0.0 ? expected + 256.0 : expected;
		double diff			  = std::abs(static_cast<double>(decode(f.channelFrameTexel(i, row).r)) - expected);
		diff				  = std::min(diff, 256.0 - diff);
		EXPECT_LE(diff, 0.07) << "x = " << x;
	}

	// A bend: west-east from x = -100 to 250.5 m, then north across y = 512 m. The
	// reach between the crossings is 250.5 + 211.5 = 462 m, stretched to 512 m in
	// its middle; within kArcAnchorHoldM of either crossing it is exact.
	const double	  xc   = 250.5;
	const ThalwegPath bend = thalwegThrough({{-100.0, y}, {xc, y}, {xc, 700.0}}, 0.5, [](double) { return 2.0F; });
	const Field		  g	   = Field::bake(polygonsOf({}, {bend}), {0, 0});
	const int32_t	  col  = static_cast<int32_t>(std::lround(xc - 0.5 + 16.0));
	auto			  sAt  = [&g](int32_t i, int32_t j) { return static_cast<double>(decode(g.channelFrameTexel(i, j).r)); };
	auto			  circ = [](double a, double b) {
		 double d = std::fmod(b - a, 256.0);
		 d		  = d < -128.0 ? d + 256.0 : (d > 128.0 ? d - 256.0 : d);
		 return d;
	};
	for (int32_t i = 16; i < 16 + 24; ++i) { // x in [0.5, 23.5]: exact from the x = 0 crossing
		EXPECT_NEAR(sAt(i, row), Field::kArcAnchorValueM + detailCenterM(g, i, true), 0.07);
	}
	for (int32_t j = 16 + 489; j < 16 + 512; ++j) { // y in [489.5, 511.5]: exact to the y = 512 crossing
		EXPECT_NEAR(sAt(col, j), Field::kArcAnchorValueM - (512.0 - detailCenterM(g, j, false)), 0.07);
	}
	for (int32_t j = 16 + 512; j < Field::kDetailSize; ++j) { // past the crossing: arc from it
		EXPECT_NEAR(circ(sAt(col, j), Field::kArcAnchorValueM + detailCenterM(g, j, false) - 512.0), 0.0, 0.07);
	}
	// Continuous and increasing along the whole bend, each 1 m step stretched by
	// at most 1 + 50 / 414.
	double prev = sAt(0, row);
	for (int32_t i = 1; i <= col; ++i) {
		const double s = sAt(i, row);
		EXPECT_GE(circ(prev, s), 0.8) << "x = " << detailCenterM(g, i, true);
		EXPECT_LE(circ(prev, s), 1.3) << "x = " << detailCenterM(g, i, true);
		prev = s;
	}
	for (int32_t j = row + 1; j < Field::kDetailSize; ++j) {
		const double s = sAt(col, j);
		EXPECT_GE(circ(prev, s), 0.8) << "y = " << detailCenterM(g, j, false);
		EXPECT_LE(circ(prev, s), 1.3) << "y = " << detailCenterM(g, j, false);
		prev = s;
	}
}

// ============================================================================
// Containment
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

	// Island center: 9 m from the island's shore, land.
	const HalfTexel island = f.farTexel(82, 82); // (149, 149)
	EXPECT_EQ(decode(island.r), 8.0F);
	EXPECT_EQ(decode(island.b), 0.0F);
	const HalfTexel islandShore = f.farTexel(79, 82); // (143, 149): 3 m inland
	EXPECT_NEAR(decode(islandShore.r), 3.0F, 1.0e-3F);
	EXPECT_EQ(decode(islandShore.b), 1.0F);
	const HalfTexel openLake = f.farTexel(68, 82); // (121, 149): 19 m from the island, 21 m from the shore
	EXPECT_EQ(decode(openLake.r), -8.0F);
	EXPECT_EQ(decode(openLake.b), 1.0F);
	const HalfTexel pondMiddle = f.farTexel(198, 203); // (381, 391)
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
			const bool water = solid || parity % 2 == 1;
			EXPECT_EQ(decode(t.r) < 0.0F, water) << p.x << ", " << p.y;
		}
	};
	for (int32_t j = 0; j < Field::kFarSize; ++j) {
		for (int32_t i = 0; i < Field::kFarSize; ++i) {
			check(f.farTexel(i, j), {mm(farCenterM(f, i, true)), mm(farCenterM(f, j, false))});
		}
	}
	const int32_t nearSize = Field::kTilesPerSide * Field::kNearTileTexels;
	for (int32_t j = 0; j < nearSize; j += 3) {
		for (int32_t i = 0; i < nearSize; i += 3) {
			if (const auto t = nearAt(f, i, j)) {
				check(*t, {f.originMm.x + i * Field::kNearTexelMm + 125, f.originMm.y + j * Field::kNearTexelMm + 125});
			}
		}
	}
}

// A river mouth: the channel ribbon runs 30 m into the lake. Overlapping water
// is water, and R there is the distance to the nearest ring edge, which inside
// the lake includes the submerged channel banks: still negative, but shallow.
TEST(TerrainDistanceFieldTest, ChannelLakeOverlapIsWaterAndMeasuresToTheNearestEdge) {
	const Field f = Field::bake(
		polygonsOf(
			{makeRing(rectRing(200.0, 200.0, 300.0, 300.0), TerrainRingKind::Waterline, WaterKind::Lake),
			 makeRing(rectRing(100.0, 248.0, 230.0, 252.0), TerrainRingKind::Channel, WaterKind::River)}
		),
		{0, 0}
	);
	const HalfTexel overlap = f.farTexel(120, 133); // (225, 251): in both, 1 m from the channel's north bank
	EXPECT_EQ(decode(overlap.r), -1.0F);
	EXPECT_EQ(decode(overlap.b), 3.0F);
	const HalfTexel beside = f.farTexel(115, 135); // (215, 255): lake only, 3 m from the submerged bank
	EXPECT_EQ(decode(beside.r), -3.0F);
	EXPECT_EQ(decode(beside.b), 3.0F);
	const HalfTexel upstream = f.farTexel(75, 133); // (135, 251): channel only
	EXPECT_EQ(decode(upstream.r), -1.0F);
	const HalfTexel open = f.farTexel(133, 108); // (251, 201)... lake interior 1 m off the south shore
	EXPECT_EQ(decode(open.r), -1.0F);
	EXPECT_EQ(decode(open.b), 1.0F);
}

TEST(TerrainDistanceFieldTest, SyntheticAndCutEdgesCloseRingsButAreNotShore) {
	// A lake closed along the extended boundary x = 520 m (synthetic), and a
	// fordable channel piece whose east end is a cut.
	TerrainRing lake = makeRing(rectRing(400.0, 100.0, 520.0, 200.0), TerrainRingKind::Waterline, WaterKind::Lake);
	lake.profiles[1].flags |= ShoreProfile::kFlagSynthetic; // (520, 100) -> (520, 200)
	TerrainRing channel = makeRing(rectRing(100.0, 300.0, 200.0, 304.0), TerrainRingKind::Channel, WaterKind::River);
	channel.profiles[1].flags |= ShoreProfile::kFlagFordableCut; // (200, 300) -> (200, 304)
	channel.profiles[2].flags |= ShoreProfile::kFlagFordableCut;
	const Field f = Field::bake(polygonsOf({lake, channel}), {0, 0});

	const HalfTexel inside = f.farTexel(266, 83); // (517, 151): 3 m from the synthetic edge, 49 m from real shore
	EXPECT_EQ(decode(inside.r), -8.0F);
	EXPECT_EQ(decode(inside.b), 1.0F);
	const HalfTexel beyond = f.farTexel(269, 83); // (523, 151): outside the closure
	EXPECT_EQ(decode(beyond.r), 8.0F);
	EXPECT_EQ(decode(beyond.b), 0.0F);
	EXPECT_EQ(f.nearTileAt(33, 10), Field::kNoNearTile); // only the synthetic edge is near it

	// Near texels either side of the cut at y = 302.125 m.
	const auto inCut = nearAt(f, 863, 1272); // (199.875, 302.125)
	ASSERT_TRUE(inCut.has_value());
	expectHalfNear(inCut->r, -1.875, "inside the cut");
	const auto pastCut = nearAt(f, 865, 1272); // (200.375, 302.125)
	ASSERT_TRUE(pastCut.has_value());
	expectHalfNear(pastCut->r, std::hypot(0.375, 1.875), "past the cut");
}

// The bake prunes its edge and thalweg searches by bounds; on irregular rings and
// crossing thalwegs of very different widths it must still find what an
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

	auto segmentDistance = [](const Vec2i64& p, const Vec2i64& a, const Vec2i64& b, double& t) {
		const double abx = static_cast<double>(b.x - a.x);
		const double aby = static_cast<double>(b.y - a.y);
		const double apx = static_cast<double>(p.x - a.x);
		const double apy = static_cast<double>(p.y - a.y);
		const double len = abx * abx + aby * aby;
		t				 = len > 0.0 ? std::clamp((apx * abx + apy * aby) / len, 0.0, 1.0) : 0.0;
		return std::hypot(apx - abx * t, apy - aby * t);
	};
	int checked = 0;
	for (int32_t j = 0; j < Field::kFarSize; j += 3) {
		for (int32_t i = 0; i < Field::kFarSize; i += 3) {
			const Vec2i64 p{mm(farCenterM(f, i, true)), mm(farCenterM(f, j, false))};
			double		  edge = std::numeric_limits<double>::max();
			for (const TerrainRing& r : rings) {
				for (size_t k = 0; k < r.ring.size(); ++k) {
					double t = 0.0;
					edge	 = std::min(edge, segmentDistance(p, std::min(r.ring[k], r.ring[(k + 1) % r.ring.size()]),
															  std::max(r.ring[k], r.ring[(k + 1) % r.ring.size()]), t));
				}
			}
			double ratio = 2.0;
			for (const ThalwegPath& path : thalwegs) {
				for (size_t k = 0; k + 1 < path.points.size(); ++k) {
					double		 t	= 0.0;
					const double d	= segmentDistance(p, path.points[k], path.points[k + 1], t);
					const double hw = (path.halfWidthM[k] + (path.halfWidthM[k + 1] - path.halfWidthM[k]) * t) * 1000.0;
					ratio			= std::min(ratio, d / hw);
				}
			}
			const HalfTexel texel = f.farTexel(i, j);
			EXPECT_NEAR(std::abs(decode(texel.r)), std::min(edge / 1000.0, 8.0), 5.0e-3) << p.x << ", " << p.y;
			EXPECT_NEAR(decode(texel.g), ratio, 2.0e-3) << p.x << ", " << p.y;
			checked += ratio < 2.0 && edge < 8000.0 ? 1 : 0;
		}
	}
	EXPECT_GT(checked, 20); // texels near both a shore and a thalweg
}

// ============================================================================
// Near tiles
// ============================================================================

TEST(TerrainDistanceFieldTest, GuttersHoldTheNeighboringSamples) {
	// Chunk (-1, 2) bakes x in [-528, 16] m, y in [1008, 1552] m.
	const double x0 = -300.3;
	const double y0 = 1100.6;
	const double x1 = -228.1;
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
					if (const auto owner = nearAt(f, i, j)) {
						EXPECT_EQ(gutter, *owner) << "tile " << tx << ", " << ty << " gutter " << u << ", " << v;
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
}

// ============================================================================
// Seams (D4, D14, section 5)
// ============================================================================

// Two chunks given the same world near their border (rings whole, in a
// different order, each with an unrelated ring the other never sees; one
// thalweg cut differently per chunk, one whole): every texel both bake is
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
	const TerrainRing westOnly = makeRing(rectRing(50.0, 50.0, 80.0, 80.0), TerrainRingKind::Waterline, WaterKind::Ocean, true);
	const TerrainRing eastOnly = makeRing(rectRing(900.0, 50.0, 950.0, 80.0), TerrainRingKind::Waterline, WaterKind::Wetland, true);

	auto hw	   = [](double s) { return static_cast<float>(3.0 + 0.5 * std::sin(s / 20.0)); };
	auto ratio = [](double s) { return static_cast<float>(1.0 + 0.2 * std::sin(s / 13.0)); };
	auto curv  = [](double s) { return static_cast<float>(0.04 * std::sin(s / 31.0)); };
	// The channel's thalweg, each chunk's copy cut at a different place past the
	// shared strip plus two half-widths.
	const ThalwegPath westThalweg = thalwegThrough({{300.0, 403.0}, {540.0, 403.0}}, 0.5, hw, ratio, curv);
	ThalwegPath		  eastThalweg = thalwegThrough({{300.0, 403.0}, {700.0, 403.0}}, 0.5, hw, ratio, curv);
	{
		const auto first = static_cast<size_t>(std::find_if(eastThalweg.points.begin(), eastThalweg.points.end(),
															 [](const Vec2i64& p) { return p.x >= 480000; }) -
											   eastThalweg.points.begin());
		eastThalweg.points.erase(eastThalweg.points.begin(), eastThalweg.points.begin() + static_cast<std::ptrdiff_t>(first));
		eastThalweg.halfWidthM.erase(eastThalweg.halfWidthM.begin(), eastThalweg.halfWidthM.begin() + static_cast<std::ptrdiff_t>(first));
		eastThalweg.widthRatio.erase(eastThalweg.widthRatio.begin(), eastThalweg.widthRatio.begin() + static_cast<std::ptrdiff_t>(first));
		eastThalweg.curvature.erase(eastThalweg.curvature.begin(), eastThalweg.curvature.begin() + static_cast<std::ptrdiff_t>(first));
	}
	// A wide river crossing the border diagonally, whole in both.
	const ThalwegPath wide = thalwegThrough(
		{{380.0, 300.0}, {640.0, 360.0}}, 0.5, [](double s) { return static_cast<float>(14.0 + 4.0 * std::sin(s / 40.0)); }, ratio, curv
	);

	const Field west = Field::bake(polygonsOf({westOnly, lake, island, channel, pondRing}, {westThalweg, wide}), {0, 0});
	const Field east = Field::bake(polygonsOf({pondRing, channel, island, lake, eastOnly}, {wide, eastThalweg}), {1, 0});

	const SeamDiff diff = compareSeam(west, east, 16.0);
	std::cout << "[ hand seam ] " << describe(diff) << "\n";
	EXPECT_EQ(diff.different, 0U) << describe(diff);
	EXPECT_GT(diff.waterSdf, 1000U);
	EXPECT_GT(diff.profiles, 100U);
	EXPECT_GT(diff.frames, 100U);
	for (int32_t ty = 0; ty < Field::kTilesPerSide; ++ty) {
		for (int32_t k = 0; k < 2; ++k) {
			EXPECT_EQ(west.nearTileAt(32 + k, ty) == Field::kNoNearTile, east.nearTileAt(k, ty) == Field::kNoNearTile) << ty;
		}
	}
}

// The same check on rings the builder makes, each chunk from its own extended
// tiles and gathered segments: a lake across the border, a meandering river
// crossing it into the lake, and a narrow creek crossing it. Compared in the
// band the shader samples across the border (the texels within 2 m of it) and,
// for the report, over the whole shared strip.
TEST(TerrainDistanceFieldTest, BuilderRingsBakeIdenticallyAcrossABorder) {
	auto biome = [](int64_t tx, int64_t ty) {
		return (tx >= 490 && tx < 560 && ty >= 200 && ty < 290) ? Biome::Lake : Biome::TemperateGrassland;
	};
	std::vector<Segment> rivers;
	auto				 riverAlong = [&rivers](double x0, double x1, const std::function<double(double)>& y, const std::function<double(double)>& hw) {
		for (double x = x0; x < x1; x += 15.0) {
			rivers.push_back({x, y(x), x + 15.0, y(x + 15.0), static_cast<float>(hw(x)), static_cast<float>(hw(x + 15.0))});
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
		ChunkTerrainPolygons polys = TerrainPolygonBuilder::build(c, kWorldSeed, HandTiles(c, biome).fn(), gathered, {});
		polys.version			   = 1;
		return polys;
	};
	const ChunkTerrainPolygons westPolys = build({0, 0});
	const ChunkTerrainPolygons eastPolys = build({1, 0});
	ASSERT_FALSE(westPolys.thalwegs.empty());
	ASSERT_FALSE(eastPolys.thalwegs.empty());
	const Field west = Field::bake(westPolys, {0, 0});
	const Field east = Field::bake(eastPolys, {1, 0});

	const SeamDiff band	 = compareSeam(west, east, 2.0);
	const SeamDiff strip = compareSeam(west, east, 16.0);
	std::cout << "[ builder seam, 2 m band ] " << describe(band) << "\n";
	std::cout << "[ builder seam, 16 m strip ] " << describe(strip) << "\n";
	EXPECT_GT(band.waterSdf, 100U);
	EXPECT_GT(band.frames, 10U);
	EXPECT_EQ(band.different, 0U) << describe(band);
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
	EXPECT_EQ(f.originMm, (Vec2i64{3 * 512000 - 16000, -2 * 512000 - 16000}));
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
		rivers.push_back({x, y(x), x + 15.0, y(x + 15.0), 3.5F, 3.5F});
	}
	const std::vector<TerrainPolygonBuilder::Pond> ponds = {{512.4, 400.2, 14.0F, 0.9F, 2.3F, 200}};
	const std::vector<ChunkCoordinate>			   coords = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};
	auto generate = [&rivers, &ponds](ChunkCoordinate c) {
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