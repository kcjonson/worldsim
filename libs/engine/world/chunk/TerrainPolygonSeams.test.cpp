// Border invariance for the distance-field bake (terrain-polygons-architecture.md
// D4, D7, D10, D14). The bake reads each chunk's unclipped rings and thalwegs, and
// a texel on a chunk border must come out bit-identical in both chunks. So:
//
//  - every non-synthetic ring edge with an endpoint within kSdfNearM plus one
//    0.25 m texel of a shared border is identical in both chunks, and identical to
//    a build with a much wider apron (no edge effect reaches it);
//  - every thalweg point a texel in both chunks' bake regions can read (within
//    kThalwegReachHalfWidths bankfull half-widths) is identical in both, with its
//    half-width, width ratio, and curvature, and identical to the wide build.
//
// Over a hand-built world with shores, straddling lakes and ponds, a fordable cut,
// narrow and wide rivers (hw 20-45 m), river mouths both chunks see and mouths
// only one chunk's extended region contains; then the carved-river world through
// Chunk::generate. Plus the pieces the invariant rests on: the pointwise
// waterline field is the fine lattice's own formula, and the apron covers the
// lattice cell next to the border plus the measured edge-effect depth.

#include "world/chunk/ApronField.h"
#include "world/chunk/Chunk.h"
#include "world/chunk/ChunkCoordinate.h"
#include "world/chunk/ChunkSampleResult.h"
#include "world/chunk/GeneratedWorldSampler.h"
#include "world/chunk/RiverTestWorld.h"
#include "world/chunk/TerrainPolygonBuilder.h"
#include "world/chunk/TerrainPolygonBuilderDetail.h"
#include "world/chunk/TerrainPolygonTestSupport.h"

#include <random/HashNoise.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <numbers>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

using namespace engine::world;
using namespace engine::world::terrain_test;

namespace {

	using Segment = TerrainPolygonBuilder::RiverSegment;
	using Pond	  = TerrainPolygonBuilder::Pond;
	using Builder = TerrainPolygonBuilder;

	constexpr uint64_t kWorldSeed	  = 0x5EA35EA3ULL;
	constexpr double   kTwoPi		  = 2.0 * std::numbers::pi;
	constexpr int32_t  kReferenceApron = 48;
	// The reference keeps samples out to its wider apron, so it gathers wider too.
	constexpr double kReferenceMarginM = kRiverGatherMarginM + static_cast<double>(kReferenceApron - kApronTiles);
	// A border texel reads kSdfNearM; the texel one beyond it (for bilinear) one more.
	constexpr int64_t kRingBandMm = static_cast<int64_t>(Builder::kSdfNearM * 1000.0) + 250;
	constexpr int64_t kBakeMarginMm = static_cast<int64_t>(Builder::kBakeMarginM * 1000.0);

	// ---- What the bake reads ----

	struct Box {
		Vec2i64 min;
		Vec2i64 max;

		[[nodiscard]] bool contains(const Vec2i64& p) const { return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y; }

		[[nodiscard]] int64_t outsideMm(const Vec2i64& p) const {
			return std::max({min.x - p.x, p.x - max.x, min.y - p.y, p.y - max.y, int64_t{0}});
		}
	};

	Box grown(const Box& b, int64_t mm) {
		return {{b.min.x - mm, b.min.y - mm}, {b.max.x + mm, b.max.y + mm}};
	}

	Box chunkBox(ChunkCoordinate c) {
		const Vec2i64 min{static_cast<int64_t>(c.x) * kChunkMm, static_cast<int64_t>(c.y) * kChunkMm};
		return {min, {min.x + kChunkMm, min.y + kChunkMm}};
	}

	Box intersect(const Box& a, const Box& b) {
		return {{std::max(a.min.x, b.min.x), std::max(a.min.y, b.min.y)}, {std::min(a.max.x, b.max.x), std::min(a.max.y, b.max.y)}};
	}

	// Where two chunks meet (a border segment or a corner point), from their squares.
	Box sharedBorder(ChunkCoordinate a, ChunkCoordinate b) {
		return intersect(chunkBox(a), chunkBox(b));
	}

	using Edge = std::pair<Vec2i64, Vec2i64>;

	// Non-synthetic ring edges with an endpoint in `band`, each with its ends
	// ordered, sorted: a multiset.
	std::vector<Edge> ringEdgesIn(const ChunkTerrainPolygons& polys, const Box& band) {
		std::vector<Edge> out;
		for (const TerrainRing& ring : polys.rings) {
			const size_t n = ring.ring.size();
			for (size_t i = 0; i < n; ++i) {
				if ((ring.profiles[i].flags & ShoreProfile::kFlagSynthetic) != 0) {
					continue;
				}
				const Vec2i64& a = ring.ring[i];
				const Vec2i64& b = ring.ring[(i + 1) % n];
				if (band.contains(a) || band.contains(b)) {
					out.emplace_back(std::min(a, b), std::max(a, b));
				}
			}
		}
		std::sort(out.begin(), out.end());
		return out;
	}

	struct ThalwegPoint {
		Vec2i64 p;
		float	halfWidthM = 0.0F;
		float	widthRatio = 0.0F;
		float	curvature  = 0.0F;
		double	arcLengthM = 0.0;

		bool operator<(const ThalwegPoint& o) const {
			return std::tie(p, halfWidthM, widthRatio, curvature, arcLengthM) <
				   std::tie(o.p, o.halfWidthM, o.widthRatio, o.curvature, o.arcLengthM);
		}
		bool operator==(const ThalwegPoint& o) const {
			return p == o.p && halfWidthM == o.halfWidthM && widthRatio == o.widthRatio && curvature == o.curvature &&
				   arcLengthM == o.arcLengthM;
		}
	};

	// Thalweg points a texel in `texels` can read: within kThalwegReachHalfWidths
	// of their own bankfull half-width of it.
	std::vector<ThalwegPoint> thalwegPointsReading(const ChunkTerrainPolygons& polys, const Box& texels) {
		std::vector<ThalwegPoint> out;
		for (const ThalwegPath& path : polys.thalwegs) {
			for (size_t i = 0; i < path.points.size(); ++i) {
				const double reachMm = Builder::kThalwegReachHalfWidths * static_cast<double>(path.halfWidthM[i]) * 1000.0;
				if (static_cast<double>(texels.outsideMm(path.points[i])) <= reachMm) {
					out.push_back({path.points[i], path.halfWidthM[i], path.widthRatio[i], path.curvature[i], path.arcLengthM[i]});
				}
			}
		}
		std::sort(out.begin(), out.end());
		return out;
	}

	// The nearest (to `from`) element of a but not b or b but not a, and the counts.
	template <typename T, typename Pos>
	std::string describeDifference(const std::vector<T>& a, const std::vector<T>& b, const Box& from, Pos position) {
		std::vector<T> onlyA;
		std::vector<T> onlyB;
		std::set_difference(a.begin(), a.end(), b.begin(), b.end(), std::back_inserter(onlyA));
		std::set_difference(b.begin(), b.end(), a.begin(), a.end(), std::back_inserter(onlyB));
		int64_t nearest = -1;
		Vec2i64 where{};
		for (const std::vector<T>* side : {&onlyA, &onlyB}) {
			for (const T& t : *side) {
				const Vec2i64 p = position(t);
				const int64_t d = from.outsideMm(p);
				if (nearest < 0 || d < nearest) {
					nearest = d;
					where	= p;
				}
			}
		}
		return std::to_string(onlyA.size()) + " only in the first, " + std::to_string(onlyB.size()) +
			   " only in the second, nearest " + std::to_string(nearest) + " mm out at (" + std::to_string(where.x) + ", " +
			   std::to_string(where.y) + ")";
	}

	// Two builds agree on every ring edge in `band` and every thalweg point that
	// can reach `texels`. Returns the number of edges and points compared.
	std::pair<size_t, size_t> expectSameForBake(
		const ChunkTerrainPolygons& a, const ChunkTerrainPolygons& b, const Box& band, const Box& texels, const std::string& label
	) {
		const std::vector<Edge> edgesA = ringEdgesIn(a, band);
		const std::vector<Edge> edgesB = ringEdgesIn(b, band);
		EXPECT_TRUE(edgesA == edgesB) << label << " ring edges: "
									  << describeDifference(edgesA, edgesB, band, [](const Edge& e) { return e.first; });
		const std::vector<ThalwegPoint> pointsA = thalwegPointsReading(a, texels);
		const std::vector<ThalwegPoint> pointsB = thalwegPointsReading(b, texels);
		EXPECT_TRUE(pointsA == pointsB) << label << " thalweg points: "
										<< describeDifference(pointsA, pointsB, texels, [](const ThalwegPoint& t) { return t.p; });
		return {edgesA.size(), pointsA.size()};
	}

	// Two adjacent chunks (sharing a border segment or only a corner): everything
	// a texel on the shared border, or in both bake regions, reads.
	std::pair<size_t, size_t> expectPairSeamless(
		ChunkCoordinate ca, const ChunkTerrainPolygons& a, ChunkCoordinate cb, const ChunkTerrainPolygons& b, const std::string& label
	) {
		const Box border = sharedBorder(ca, cb);
		const Box texels = intersect(grown(chunkBox(ca), kBakeMarginMm), grown(chunkBox(cb), kBakeMarginMm));
		return expectSameForBake(a, b, grown(border, kRingBandMm), texels, label);
	}

	// A chunk against its wide-apron reference: everything its bake reads.
	std::pair<size_t, size_t> expectMatchesReference(
		ChunkCoordinate c, const ChunkTerrainPolygons& built, const ChunkTerrainPolygons& reference, const std::string& label
	) {
		return expectSameForBake(built, reference, grown(chunkBox(c), kRingBandMm), grown(chunkBox(c), kBakeMarginMm), label);
	}

	// ---- The hand-built world ----

	struct RiverPoint {
		double x;
		double y;
		float  hw;
	};

	// Consecutive points cut into segments, the arc coordinate the chord length
	// summed from the first point.
	std::vector<Segment> segmentsOf(const std::vector<RiverPoint>& pts) {
		std::vector<Segment> out;
		double				 s = 0.0;
		for (size_t i = 0; i + 1 < pts.size(); ++i) {
			const double next = s + std::hypot(pts[i + 1].x - pts[i].x, pts[i + 1].y - pts[i].y);
			out.push_back({pts[i].x, pts[i].y, pts[i + 1].x, pts[i + 1].y, pts[i].hw, pts[i + 1].hw, s, next});
			s = next;
		}
		return out;
	}

	// A river through `count + 1` points p(t), t from 0 to 1, half-width hw(t).
	std::vector<Segment> riverThrough(
		int count, const std::function<std::pair<double, double>(double)>& p, const std::function<double(double)>& hw
	) {
		std::vector<RiverPoint> pts;
		for (int i = 0; i <= count; ++i) {
			const double t		= static_cast<double>(i) / static_cast<double>(count);
			const auto [x, y] = p(t);
			pts.push_back({x, y, static_cast<float>(hw(t))});
		}
		return segmentsOf(pts);
	}

	void append(std::vector<Segment>& to, const std::vector<Segment>& from) {
		to.insert(to.end(), from.begin(), from.end());
	}

	// The west edge of the lake that only chunk (1, 0)'s extended region reaches:
	// one tile past chunk (0, 0)'s extended region.
	constexpr int64_t kOneSidedLakeX = kChunkSize + kApronTiles + 1;

	// Four chunks (0,0), (1,0), (0,1), (1,1), borders x = 512 m and y = 512 m.
	Biome seamWorldBiome(int64_t tx, int64_t ty) {
		const auto inRect = [tx, ty](int64_t x0, int64_t x1, int64_t y0, int64_t y1) { return tx >= x0 && tx < x1 && ty >= y0 && ty < y1; };
		// A lake straddling x = 512, fed by a river from the west.
		if (inRect(495, 531, 110, 170)) {
			return Biome::Lake;
		}
		// The receiving lakes of two mouths only chunk (1, 0) contains.
		if (inRect(kOneSidedLakeX, 640, 272, 310) || inRect(560, 700, 340, 470)) {
			return Biome::Lake;
		}
		// A lake on the four-chunk corner.
		const double cx = static_cast<double>(tx) + 0.5 - 515.0;
		const double cy = static_cast<double>(ty) + 0.5 - 508.0;
		if (cx * cx + cy * cy < 22.0 * 22.0) {
			return Biome::Lake;
		}
		// An ocean inlet across y = 512 with a ragged edge.
		if (tx >= 200 && tx < 300 && ty >= 500 + static_cast<int64_t>(foundation::hash3(static_cast<int32_t>(tx / 4), 0, 0, 9U) % 9U)) {
			return ty < 560 ? Biome::Ocean : Biome::TemperateGrassland;
		}
		// Small lakes and islets along both borders, away from the rivers.
		const bool nearBorder = (tx >= 460 && tx < 570 && ty >= 180 && ty < 230) || (ty >= 470 && ty < 560 && tx >= 340 && tx < 460);
		if (nearBorder && foundation::valueNoise3(static_cast<float>(tx) / 13.0F, static_cast<float>(ty) / 13.0F, 0.0F, 5U) > 0.6F) {
			return Biome::TemperateWetland;
		}
		return Biome::TemperateGrassland;
	}

	std::vector<Segment> seamWorldRivers() {
		std::vector<Segment> all;
		// Narrow, meandering east across x = 512.
		append(all, riverThrough(60, [](double t) { return std::pair{250.0 + 600.0 * t, 60.0 + 12.0 * std::sin(kTwoPi * 3.0 * t)}; },
								 [](double t) { return 2.5 + 0.5 * std::sin(kTwoPi * 2.0 * t); }));
		// Into the straddling lake from the west: a mouth both chunks see.
		append(all, riverThrough(12, [](double t) { return std::pair{380.0 + 130.0 * t, 140.0 + 4.0 * std::sin(kTwoPi * t)}; },
								 [](double) { return 2.0; }));
		// Tapering through the fordable width a few meters west of x = 512.
		append(all, riverThrough(40, [](double t) { return std::pair{300.0 + 400.0 * t, 205.0 + 5.0 * std::sin(kTwoPi * 2.0 * t)}; },
								 [](double t) { return std::clamp(1.1 - 0.9 * (300.0 + 400.0 * t - 480.0) / 60.0, 0.35, 1.1); }));
		// Narrow, into a lake only chunk (1, 0)'s extended region contains.
		append(all, riverThrough(20, [](double t) { return std::pair{360.0 + 240.0 * t, 290.0 + 3.0 * std::sin(kTwoPi * 2.0 * t)}; },
								 [](double) { return 1.3; }));
		// Wide (hw 25 m), into the other one-sided lake: its capped flare reaches back
		// across x = 512.
		append(all, riverThrough(30, [](double t) { return std::pair{250.0 + 400.0 * t, 405.0 + 10.0 * std::sin(kTwoPi * t)}; },
								 [](double) { return 25.0; }));
		// Wide (hw 40 m) north across y = 512, width varying like a riffle.
		append(all, riverThrough(40, [](double t) { return std::pair{150.0 + 25.0 * std::sin(kTwoPi * 1.5 * t), -150.0 + 900.0 * t}; },
								 [](double t) { return 40.0 + 6.0 * std::sin(kTwoPi * 4.0 * t); }));
		// Narrow north across y = 512.
		append(all, riverThrough(30, [](double t) { return std::pair{305.0 + 8.0 * std::sin(kTwoPi * 2.0 * t), 430.0 + 300.0 * t}; },
								 [](double) { return 1.8; }));
		// Diagonally through the corner lake: in and out right at the corner.
		append(all, riverThrough(24, [](double t) { return std::pair{430.0 + 180.0 * t, 450.0 + 150.0 * t}; }, [](double) { return 2.2; }));
		return all;
	}

	std::vector<Pond> seamWorldPonds() {
		return {{514.0, 245.0, 9.0F, 0.9F, 2.3F, 120}, {760.0, 700.0, 8.0F, 1.1F, 0.2F, 90}};
	}

	struct Gathered {
		std::vector<Segment> segments;
		std::vector<Pond>	 ponds;
	};

	// The gather GeneratedWorldSampler does (RiverNetwork2D and PondNetwork2D
	// footprint culls), with a margin.
	Gathered gatherFor(ChunkCoordinate coord, const std::vector<Segment>& segments, const std::vector<Pond>& ponds, double marginM) {
		const double minX = static_cast<double>(coord.x) * static_cast<double>(kChunkSize) - marginM;
		const double minY = static_cast<double>(coord.y) * static_cast<double>(kChunkSize) - marginM;
		const double maxX = minX + static_cast<double>(kChunkSize) + 2.0 * marginM;
		const double maxY = minY + static_cast<double>(kChunkSize) + 2.0 * marginM;
		Gathered	 out;
		for (const Segment& s : segments) {
			const double pad = static_cast<double>(std::max(s.halfWidth0, s.halfWidth1));
			if (std::max(s.x0, s.x1) + pad >= minX && std::min(s.x0, s.x1) - pad <= maxX && std::max(s.y0, s.y1) + pad >= minY &&
				std::min(s.y0, s.y1) - pad <= maxY) {
				out.segments.push_back(s);
			}
		}
		for (const Pond& p : ponds) {
			const double r = static_cast<double>(p.radius) * 1.5;
			if (p.cx + r >= minX && p.cx - r <= maxX && p.cy + r >= minY && p.cy - r <= maxY) {
				out.ponds.push_back(p);
			}
		}
		return out;
	}

	ChunkTerrainPolygons buildSeamWorld(ChunkCoordinate coord, int32_t apronTiles, double marginM) {
		const HandTiles tiles(coord, seamWorldBiome, apronTiles);
		const Gathered	g = gatherFor(coord, seamWorldRivers(), seamWorldPonds(), marginM);
		return buildHand(tiles, kWorldSeed, g.segments, g.ponds);
	}

	std::string name(ChunkCoordinate c) {
		return "(" + std::to_string(c.x) + ", " + std::to_string(c.y) + ")";
	}

	// Every pair among the four chunks meeting at the corner (1, 1) in chunk units.
	const std::vector<std::pair<ChunkCoordinate, ChunkCoordinate>> kCornerPairs = {
		{{0, 0}, {1, 0}}, {{0, 1}, {1, 1}}, {{0, 0}, {0, 1}}, {{1, 0}, {1, 1}}, {{0, 0}, {1, 1}}, {{1, 0}, {0, 1}}
	};

} // namespace

// ============================================================================
// What the invariant rests on
// ============================================================================

namespace {

	// Lakes and islets of every size everywhere, so shores run through every
	// part of the extended region.
	Biome archipelagoBiome(int64_t tx, int64_t ty) {
		return foundation::valueNoise3(static_cast<float>(tx) / 17.0F, static_cast<float>(ty) / 17.0F, 0.0F, 3U) > 0.55F
				   ? Biome::Lake
				   : Biome::TemperateGrassland;
	}

	struct FieldBuild {
		HandTiles			  tiles;
		terrain_detail::Region region;
		geometry::ScalarField fine;
	};

	FieldBuild fineFieldOf(ChunkCoordinate coord, int32_t apronTiles) {
		HandTiles							 tiles(coord, archipelagoBiome, apronTiles);
		const terrain_detail::Region		 region = terrain_detail::regionOf(coord, apronTiles);
		const Builder::ExtendedTileFn		 fn		= tiles.fn();
		const Builder::BiomeWaterFn			 water	= tiles.waterFn();
		const terrain_detail::ExtendedGrid	 grid(fn, water, region);
		geometry::ScalarField				 fine = terrain_detail::waterlineFineField(grid, region, kWorldSeed);
		return {std::move(tiles), region, std::move(fine)};
	}

} // namespace

// The pointwise field the mouth test reads is the fine lattice's own formula:
// every lattice sample is exactly the warped point value or, where warpField
// skipped the warp (its whole neighborhood on one side of the isoline), exactly
// the unwarped read, and on the same side either way.
TEST(TerrainPolygonSeamsTest, PointFieldIsTheLatticeFormulaBitForBit) {
	const ChunkCoordinate		coord{2, -1};
	const FieldBuild			build = fineFieldOf(coord, kApronTiles);
	const Builder::BiomeWaterFn water = build.tiles.waterFn();
	const terrain_detail::WaterlineField field(water, kWorldSeed);
	auto unwarped = [&water](Vec2i64 p) {
		auto coarse = [&water](int64_t gx, int64_t gy) { return terrain_detail::coarseWaterValue(water, gx, gy); };
		return geometry::warpedBilinear(coarse, terrain_detail::kCoarsePhaseMm, Builder::kTileMm, p, {});
	};

	// Samples far enough inside that the lattice's coarse samples are all real.
	constexpr int kInset = 5 * static_cast<int>(Builder::kTileMm / Builder::kFineCellMm);
	int			  warped = 0;
	int			  checked = 0;
	for (int j = kInset; j < build.fine.height - kInset; j += 3) {
		for (int i = kInset; i < build.fine.width - kInset; i += 3) {
			const Vec2i64 p		= build.fine.samplePositionMm(i, j);
			const float	  value = build.fine.at(i, j);
			const float	  point = field.valueAt(p);
			ASSERT_EQ(value >= Builder::kWaterlineIso, point >= Builder::kWaterlineIso) << p.x << ", " << p.y;
			if (value == point) {
				warped += value != unwarped(p) ? 1 : 0;
			} else {
				ASSERT_EQ(value, unwarped(p)) << p.x << ", " << p.y;
			}
			++checked;
		}
	}
	EXPECT_GT(checked, 400000);
	EXPECT_GT(warped, 20000);
}

// How deep the extended region's edge reaches into the waterline field: fine
// samples that differ from a wide-apron build of the same world, measured in from
// the extended boundary. A ring vertex moves with the samples of its marching
// cell and Chaikin reads one more vertex along, so ring geometry is exact from
// half a meter further in. Every run a border texel reads lies in the lattice
// cell next to the border, so the apron must hold that cell, this depth, and a
// safety tile (D4).
TEST(TerrainPolygonSeamsTest, ApronHoldsTheBorderLatticeCellPlusTheEdgeEffect) {
	const ChunkCoordinate coord{0, 0};
	const FieldBuild	  game		= fineFieldOf(coord, kApronTiles);
	const FieldBuild	  reference = fineFieldOf(coord, kReferenceApron);
	const Box			  extended{game.region.extMin, game.region.extMax};

	int64_t depthMm = 0;
	for (int j = 0; j < game.fine.height; ++j) {
		for (int i = 0; i < game.fine.width; ++i) {
			const Vec2i64 p	 = game.fine.samplePositionMm(i, j);
			const int	  ri = static_cast<int>((p.x - reference.fine.originMm.x) / Builder::kFineCellMm);
			const int	  rj = static_cast<int>((p.y - reference.fine.originMm.y) / Builder::kFineCellMm);
			if (game.fine.at(i, j) != reference.fine.at(ri, rj)) {
				const int64_t inward = std::min({p.x - extended.min.x, extended.max.x - p.x, p.y - extended.min.y, extended.max.y - p.y});
				depthMm				 = std::max(depthMm, inward);
			}
		}
	}
	constexpr int64_t kRingSlackMm = 2 * Builder::kFineCellMm;
	std::cout << "[ edge effect ] fine samples differ up to " << depthMm << " mm in from the extended boundary; apron "
			  << kApronTiles << " tiles\n";
	EXPECT_GT(depthMm, 0);
	EXPECT_LE(Builder::kPinLatticeMm + depthMm + kRingSlackMm + Builder::kTileMm, static_cast<int64_t>(kApronTiles) * Builder::kTileMm);
}

// ============================================================================
// The invariant (D4, D10): hand-built world
// ============================================================================

TEST(TerrainPolygonSeamsTest, BorderTexelsReadTheSameGeometryFromEitherChunk) {
	const std::vector<ChunkCoordinate>				coords = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};
	std::map<std::pair<int32_t, int32_t>, ChunkTerrainPolygons> built;
	for (const ChunkCoordinate& c : coords) {
		built[{c.x, c.y}] = buildSeamWorld(c, kApronTiles, kRiverGatherMarginM);
		expectAllSimple(built[{c.x, c.y}], "seam world " + name(c));
	}

	size_t edges  = 0;
	size_t points = 0;
	for (const auto& [ca, cb] : kCornerPairs) {
		const auto [e, p] = expectPairSeamless(ca, built[{ca.x, ca.y}], cb, built[{cb.x, cb.y}], name(ca) + " / " + name(cb));
		edges += e;
		points += p;
	}
	std::cout << "[ seam world ] compared " << edges << " ring edges and " << points << " thalweg points across six borders\n";
	// Worth something only if every border carries plenty of both.
	EXPECT_GT(edges, 300U);
	EXPECT_GT(points, 1000U);
}

TEST(TerrainPolygonSeamsTest, BorderGeometryMatchesAWideApronBuild) {
	for (const ChunkCoordinate c : {ChunkCoordinate{0, 0}, ChunkCoordinate{1, 0}, ChunkCoordinate{0, 1}, ChunkCoordinate{1, 1}}) {
		const ChunkTerrainPolygons built	 = buildSeamWorld(c, kApronTiles, kRiverGatherMarginM);
		const ChunkTerrainPolygons reference = buildSeamWorld(c, kReferenceApron, kReferenceMarginM);
		const auto [edges, points]			 = expectMatchesReference(c, built, reference, "reference " + name(c));
		EXPECT_GT(edges, 100U);
		EXPECT_GT(points, 200U);
	}
}

// The mouths only one chunk's extended region contains: chunk (0, 0) flares both
// rivers exactly as chunk (1, 0) does, because both read the ground along the
// centerline from the world, not from their rings.
TEST(TerrainPolygonSeamsTest, AMouthPastTheApronFlaresTheSameInBothChunks) {
	const ChunkTerrainPolygons west = buildSeamWorld({0, 0}, kApronTiles, kRiverGatherMarginM);
	const ChunkTerrainPolygons east = buildSeamWorld({1, 0}, kApronTiles, kRiverGatherMarginM);
	// Chunk (0, 0) has neither receiving lake among its rings.
	for (const TerrainRing& ring : west.rings) {
		if (ring.kind == TerrainRingKind::Waterline) {
			for (const Vec2i64& v : ring.ring) {
				EXPECT_LT(v.x, kOneSidedLakeX * 1000 - 1000) << "chunk (0, 0) sees a one-sided lake";
			}
		}
	}
	// Both flare: the thalweg half-width rises toward each mouth where (0, 0)'s thalweg points reach.
	auto flaredNear = [](const ChunkTerrainPolygons& polys, double yM, double hwM) {
		int flared = 0;
		for (const ThalwegPath& path : polys.thalwegs) {
			for (size_t i = 0; i < path.points.size(); ++i) {
				const Vec2i64& p = path.points[i];
				if (std::abs(static_cast<double>(p.y) / 1000.0 - yM) < 20.0 && p.x > 500000 && p.x < 533000 &&
					path.halfWidthM[i] > static_cast<float>(hwM * 1.05)) {
					++flared;
				}
			}
		}
		return flared;
	};
	EXPECT_GT(flaredNear(west, 290.0, 1.3), 0);
	EXPECT_GT(flaredNear(west, 405.0, 25.0), 0);
	EXPECT_EQ(flaredNear(west, 290.0, 1.3), flaredNear(east, 290.0, 1.3));
	EXPECT_EQ(flaredNear(west, 405.0, 25.0), flaredNear(east, 405.0, 25.0));
}

// ============================================================================
// The invariant: real river geometry through Chunk::generate
// ============================================================================

namespace {

	// The chunk's extended tiles at any apron, computed the way ApronField does
	// (each tile from its owning neighbor's grid, hydrology from this chunk's
	// sample), so a reference build sees what a wider apron would.
	HandTiles realTiles(const Chunk& chunk, int32_t apronTiles, uint64_t worldSeed) {
		const ChunkCoordinate				  coord	 = chunk.coordinate();
		const ChunkSampleResult&			  sample = chunk.biomeData();
		const std::shared_ptr<NeighborhoodGrids> grids = std::make_shared<NeighborhoodGrids>(sample);
		HandTiles tiles(coord, [grids, coord](int64_t tx, int64_t ty) { return grids->primaryBiomeAt(coord, tx, ty); }, apronTiles);
		for (int32_t ey = 0; ey < tiles.size; ++ey) {
			for (int32_t ex = 0; ex < tiles.size; ++ex) {
				const int64_t lx = tiles.originX() + ex - static_cast<int64_t>(coord.x) * kChunkSize;
				const int64_t ly = tiles.originY() + ey - static_cast<int64_t>(coord.y) * kChunkSize;
				const int32_t dx = lx < 0 ? -1 : (lx >= kChunkSize ? 1 : 0);
				const int32_t dy = ly < 0 ? -1 : (ly >= kChunkSize ? 1 : 0);
				const auto	  nx = static_cast<uint16_t>(lx - dx * kChunkSize);
				const auto	  ny = static_cast<uint16_t>(ly - dy * kChunkSize);
				const ChunkSampleResult& grid = grids->grid(dx, dy);
				tiles.at(ex, ey) = Chunk::computeTileFrom({
					.coord			 = {coord.x + dx, coord.y + dy},
					.localX			 = nx,
					.localY			 = ny,
					.biomeWeights	 = grid.getTileBiome(nx, ny),
					.elevationMeters = grid.getTileElevation(nx, ny),
					.hydrology		 = &sample,
					.worldSeed		 = worldSeed,
				});
			}
		}
		return tiles;
	}

} // namespace

TEST(TerrainPolygonSeamsTest, RealRiverBordersReadTheSameGeometry) {
	const river_test::CarvedRiverWorld world = river_test::makeCarvedRiverWorld();
	const GeneratedWorldSampler		   sampler(world.world, world.landingLat, world.landingLon);

	std::map<std::pair<int32_t, int32_t>, std::unique_ptr<Chunk>> chunks;
	for (int32_t y = -1; y <= 0; ++y) {
		for (int32_t x = -1; x <= 1; ++x) {
			auto chunk = std::make_unique<Chunk>(ChunkCoordinate{x, y}, sampler.sampleChunk({x, y}), sampler.getWorldSeed());
			chunk->generate();
			chunks[{x, y}] = std::move(chunk);
		}
	}
	size_t edges  = 0;
	size_t points = 0;
	for (const auto& [key, chunk] : chunks) {
		for (const auto& [dx, dy] : {std::pair{1, 0}, std::pair{0, 1}, std::pair{1, 1}, std::pair{-1, 1}}) {
			const auto next = chunks.find({key.first + dx, key.second + dy});
			if (next == chunks.end()) {
				continue;
			}
			const auto [e, p] = expectPairSeamless(chunk->coordinate(), chunk->terrainPolygons(), next->second->coordinate(),
												   next->second->terrainPolygons(), "real " + name(chunk->coordinate()) + " / " +
																						name(next->second->coordinate()));
			edges += e;
			points += p;
		}
	}
	std::cout << "[ real river seams ] compared " << edges << " ring edges and " << points << " thalweg points\n";
	EXPECT_GT(edges, 300U);
	EXPECT_GT(points, 300U);

	// The source and trunk chunks against wide-apron references built from the
	// same sample data. The helper rebuilds the game's own apron first, which
	// must reproduce Chunk::generate exactly.
	for (const auto& key : {std::pair{0, 0}, std::pair{0, -1}}) {
		const Chunk&			 chunk	= *chunks.at(key);
		const ChunkSampleResult& sample = chunk.biomeData();
		const ChunkTerrainPolygons same = buildHand(realTiles(chunk, kApronTiles, sampler.getWorldSeed()), sampler.getWorldSeed(),
													sample.riverSegments, sample.pondBlobs);
		EXPECT_TRUE(sameRings(same.rings, chunk.terrainPolygons().rings)) << name(chunk.coordinate());
		EXPECT_TRUE(sameThalwegs(same.thalwegs, chunk.terrainPolygons().thalwegs)) << name(chunk.coordinate());
		const ChunkTerrainPolygons reference = buildHand(realTiles(chunk, kReferenceApron, sampler.getWorldSeed()),
														 sampler.getWorldSeed(), sample.riverSegments, sample.pondBlobs);
		const auto [e, p] =
			expectMatchesReference(chunk.coordinate(), chunk.terrainPolygons(), reference, "real reference " + name(chunk.coordinate()));
		EXPECT_GT(e, 100U);
		EXPECT_GT(p, 100U);
	}
}
