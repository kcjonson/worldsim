#include "GeometryIndex.h"

#include <assets/ConstructionRegistry.h>
#include <construction/ConstructionWorld.h>
#include <construction/OpeningGeometry.h>
#include <offset/WallOffset.h>
#include <predicates/Predicates.h>
#include <visibility/Visibility.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

using ecs::GeometryIndex;
using assets::ConstructionRegistry;
using construction::ConstructionWorld;
using construction::FoundationState;
using construction::kInvalidFoundation;
using construction::kInvalidOpening;
using construction::OpeningId;
using construction::SegmentCommitResult;
using construction::SegmentId;
using geometry::OccluderSegment;
using geometry::Vec2i64;

namespace {

	// Far past every wall in these tests, so the sight radius never decides a ray.
	constexpr std::int64_t kFarSightMm = 100000;

	// Project root from __FILE__: this file lives at <root>/libs/sim/vision/.
	std::filesystem::path projectRoot() {
		std::filesystem::path p = __FILE__;
		return p.parent_path().parent_path().parent_path().parent_path();
	}

	std::string constructionConfigFolder() {
		return (projectRoot() / "assets" / "config" / "construction").string();
	}

	// Build a single built wall segment (every created sub-segment marked Built).
	SegmentId buildWall(ConstructionWorld& cw, Vec2i64 a, Vec2i64 b, const std::string& thickness = "Standard") {
		SegmentCommitResult r = cw.commitSegment(a, b, "Wood", thickness, kInvalidFoundation, cw.version());
		EXPECT_TRUE(r.ok());
		for (SegmentId id : r.createdSegments) {
			cw.setSegmentState(id, FoundationState::Built);
		}
		return r.id;
	}

	// Add a BUILT opening at parameter t.
	OpeningId addBuiltOpening(ConstructionWorld& cw, SegmentId seg, float t, const std::string& type) {
		OpeningId op = cw.addOpening(seg, t, type, "Wood");
		EXPECT_NE(op, kInvalidOpening);
		EXPECT_TRUE(cw.setOpeningState(op, FoundationState::Built));
		return op;
	}

	std::int64_t halfThickness(const ConstructionWorld& cw) {
		return construction::halfThicknessMm(cw.segments().front());
	}

	std::vector<OccluderSegment> occludersOf(const ConstructionWorld& cw) {
		GeometryIndex index;
		index.setConstructionWorld(&cw);
		index.rebuildIfStale();
		std::vector<OccluderSegment> occluders;
		index.queryOccluders({0, 0}, kFarSightMm, occluders);
		return occluders;
	}

	bool sees(const std::vector<OccluderSegment>& occluders, Vec2i64 from, Vec2i64 to) {
		return geometry::hasLineOfSight(from, to, kFarSightMm, occluders);
	}

	// The solid stretches of each wall's centerline: the wall less the spans its built
	// transparent openings leave open.
	std::vector<std::pair<Vec2i64, Vec2i64>> solidCenterlines(const ConstructionWorld& cw, const GeometryIndex& index) {
		std::vector<std::pair<Vec2i64, Vec2i64>> solid;
		for (const construction::WallSegment& wall : cw.segments()) {
			const Vec2i64		 start = cw.getVertex(wall.v0)->pos;
			const Vec2i64		 end   = cw.getVertex(wall.v1)->pos;
			std::vector<Vec2i64> cuts  = {start};
			for (const GeometryIndex::OpeningRecord& opening : index.builtOpenings()) {
				if (opening.segment == wall.id && opening.transparentToSight) {
					cuts.push_back(opening.jambA);
					cuts.push_back(opening.jambB);
				}
			}
			cuts.push_back(end);
			std::sort(cuts.begin() + 1, cuts.end() - 1, [&](const Vec2i64& first, const Vec2i64& second) {
				return geometry::dot(first - start, first - start) < geometry::dot(second - start, second - start);
			});
			for (std::size_t cut = 0; cut + 1 < cuts.size(); cut += 2) {
				solid.emplace_back(cuts[cut], cuts[cut + 1]);
			}
		}
		return solid;
	}

	// Every straight line between two points clear of every wall's band that crosses the
	// solid stretch of a wall's centerline passes through the walls' joined shape, so the
	// far point must lie outside the near one's visibility polygon, built from the facing
	// occluders. Points on a 250 mm grid over the box, a margin clear of each band. The
	// polygon is the test, not hasLineOfSight, which lets a line through a bare vertex pass
	// (its rule for grazing a wall's tip), and so a line exactly through two vertices of
	// one outline.
	void expectNoLeak(const ConstructionWorld& cw, Vec2i64 low, Vec2i64 high) {
		constexpr std::int64_t kGridMm			 = 250;
		constexpr std::int64_t kClearOfTheBandMm = 2; // past the millimetre the offset rounds to
		GeometryIndex		   index;
		index.setConstructionWorld(&cw);
		index.rebuildIfStale();
		std::vector<std::pair<Vec2i64, Vec2i64>> centerlines;
		for (const construction::WallSegment& wall : cw.segments()) {
			centerlines.emplace_back(cw.getVertex(wall.v0)->pos, cw.getVertex(wall.v1)->pos);
		}
		const std::vector<std::pair<Vec2i64, Vec2i64>> solid = solidCenterlines(cw, index);
		const std::int64_t							   half	 = halfThickness(cw);
		std::vector<Vec2i64>						   points;
		for (std::int64_t x = low.x; x <= high.x; x += kGridMm) {
			for (std::int64_t y = low.y; y <= high.y; y += kGridMm) {
				bool clear = true;
				for (const auto& [start, end] : centerlines) {
					clear = clear && !geometry::withinDistanceOfSegment({x, y}, start, end, half + kClearOfTheBandMm);
				}
				if (clear) {
					points.push_back({x, y});
				}
			}
		}
		std::vector<OccluderSegment> occluders;
		int							 leaks = 0;
		for (std::size_t first = 0; first < points.size(); ++first) {
			index.queryFacingOccluders(points[first], kFarSightMm, occluders);
			const geometry::Ring polygon = geometry::computeVisibilityPolygon(points[first], kFarSightMm, occluders);
			for (std::size_t second = 0; second < points.size(); ++second) {
				if (second == first) {
					continue;
				}
				bool crossesAWall = false;
				for (const auto& [start, end] : solid) {
					crossesAWall = crossesAWall ||
								   geometry::intersectSegments(points[first], points[second], start, end).relation != geometry::SegmentRelation::Disjoint;
				}
				if (crossesAWall && geometry::pointInPolygon(points[second], polygon) != geometry::PointInPolygon::Outside) {
					if (++leaks <= 5) {
						ADD_FAILURE() << "sight leaks from (" << points[first].x << ", " << points[first].y << ") to (" << points[second].x << ", "
									  << points[second].y << ")";
					}
				}
			}
		}
		EXPECT_EQ(leaks, 0);
	}

	// A closed 4 m room, a door in its south wall.
	void buildRoomWithADoor(ConstructionWorld& cw) {
		const SegmentId south = buildWall(cw, {0, 0}, {4000, 0});
		buildWall(cw, {4000, 0}, {4000, 4000});
		buildWall(cw, {4000, 4000}, {0, 4000});
		buildWall(cw, {0, 4000}, {0, 0});
		addBuiltOpening(cw, south, 0.5F, "Door");
	}

	// An L corner, a T junction, and a lone wall, apart.
	void buildJunctions(ConstructionWorld& cw) {
		buildWall(cw, {0, 0}, {4000, 0});
		buildWall(cw, {4000, 0}, {4000, 3000});
		buildWall(cw, {8000, 0}, {12000, 0});
		buildWall(cw, {10000, 0}, {10000, 3000});
		buildWall(cw, {16000, 0}, {16000, 3000});
	}

	// Every vertex of each ring within `toleranceMm` of the other's boundary.
	bool ringsAgree(const geometry::Ring& first, const geometry::Ring& second, std::int64_t toleranceMm) {
		const auto onBoundary = [&](const Vec2i64& point, const geometry::Ring& ring) {
			for (std::size_t corner = 0; corner < ring.size(); ++corner) {
				if (geometry::withinDistanceOfSegment(point, ring[corner], ring[(corner + 1) % ring.size()], toleranceMm)) {
					return true;
				}
			}
			return false;
		};
		return std::ranges::all_of(first, [&](const Vec2i64& point) { return onBoundary(point, second); }) &&
			   std::ranges::all_of(second, [&](const Vec2i64& point) { return onBoundary(point, first); });
	}

	// The visibility polygon from every eye of a 250 mm grid over the box, and from points
	// on the line of each occluder past both its ends (an eye looking exactly along a face or
	// an end cap), is the same built from the facing occluders as from all of them: ring for
	// ring, or with `toleranceMm`, every vertex of each within it of the other's boundary.
	void expectFacingSightIsTheSame(const ConstructionWorld& cw, Vec2i64 low, Vec2i64 high, std::int64_t toleranceMm = 0) {
		constexpr std::int64_t kGridMm	= 250;
		constexpr std::int64_t kSightMm = 20000;
		GeometryIndex		   index;
		index.setConstructionWorld(&cw);
		index.rebuildIfStale();
		std::vector<Vec2i64> eyes;
		for (std::int64_t x = low.x; x <= high.x; x += kGridMm) {
			for (std::int64_t y = low.y; y <= high.y; y += kGridMm) {
				eyes.push_back({x, y});
			}
		}
		for (const GeometryIndex::OccluderRecord& occluder : index.occluders()) {
			const Vec2i64 along = occluder.seg.b - occluder.seg.a;
			eyes.push_back(occluder.seg.a - along);
			eyes.push_back(occluder.seg.b + along);
		}
		std::vector<OccluderSegment> all;
		std::vector<OccluderSegment> facing;
		int							 differ = 0;
		std::size_t				 culled = 0;
		for (const Vec2i64& eye : eyes) {
			index.queryOccluders(eye, kSightMm, all);
			index.queryFacingOccluders(eye, kSightMm, facing);
			culled += all.size() - facing.size();
			const geometry::Ring fromAll	= geometry::computeVisibilityPolygon(eye, kSightMm, all);
			const geometry::Ring fromFacing = geometry::computeVisibilityPolygon(eye, kSightMm, facing);
			const bool			 same		= toleranceMm == 0 ? fromAll == fromFacing : ringsAgree(fromAll, fromFacing, toleranceMm);
			if (!same && ++differ <= 5) {
				ADD_FAILURE() << "sight from (" << eye.x << ", " << eye.y << ") differs";
			}
		}
		EXPECT_EQ(differ, 0);
		EXPECT_GT(culled, 0u);
	}

	class GeometryIndexTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			ConstructionRegistry::Get().clear();
			ASSERT_TRUE(ConstructionRegistry::Get().load(constructionConfigFolder()));
		}
		void TearDown() override { ConstructionRegistry::Get().clear(); }
	};

} // namespace

// --- Transparency config -----------------------------------------------------

// Door and Window are both transparent to sight; pathable is independent of it
// (a window blocks movement but passes sight).
TEST_F(GeometryIndexTest, TransparencyConfigFlags) {
	const auto* door = ConstructionRegistry::Get().getOpeningType("Door");
	const auto* window = ConstructionRegistry::Get().getOpeningType("Window");
	ASSERT_NE(door, nullptr);
	ASSERT_NE(window, nullptr);

	EXPECT_TRUE(door->transparentToSight);
	EXPECT_TRUE(window->transparentToSight);

	// The two flags are independent: a window is sight-transparent but not pathable.
	EXPECT_FALSE(window->pathable);
	EXPECT_TRUE(window->transparentToSight);
	EXPECT_TRUE(door->pathable);
}

// --- Occluder extraction -----------------------------------------------------

// A straight wall's occluders are its band's four edges: both faces and both end
// caps, none of them inside the band.
TEST_F(GeometryIndexTest, StraightWallOccludersAreItsBandEdges) {
	ConstructionWorld cw;
	buildWall(cw, {0, 0}, {4000, 0});
	const std::int64_t half = halfThickness(cw);
	ASSERT_GT(half, 0);

	const std::vector<OccluderSegment> occluders = occludersOf(cw);
	EXPECT_EQ(occluders.size(), 4u);
	for (const OccluderSegment& occluder : occluders) {
		for (const Vec2i64& end : {occluder.a, occluder.b}) {
			EXPECT_TRUE(std::llabs(end.y) == half || end.x == 0 || end.x == 4000) << "(" << end.x << ", " << end.y << ") is off the band's edge";
			EXPECT_LE(std::llabs(end.y), half);
		}
		const Vec2i64 middle{(occluder.a.x + occluder.b.x) / 2, (occluder.a.y + occluder.b.y) / 2};
		EXPECT_FALSE(std::llabs(middle.y) < half && middle.x > 0 && middle.x < 4000) << "an occluder runs inside the band";
	}
}

// Sight stops at the face on the observer's side, from either side, and at an end cap.
TEST_F(GeometryIndexTest, SightStopsAtTheNearFace) {
	ConstructionWorld cw;
	buildWall(cw, {0, 0}, {4000, 0});
	const std::int64_t				   half		 = halfThickness(cw);
	const std::vector<OccluderSegment> occluders = occludersOf(cw);

	EXPECT_TRUE(sees(occluders, {2000, 3000}, {2000, half + 1}));
	EXPECT_FALSE(sees(occluders, {2000, 3000}, {2000, half - 1})) << "the near half of the band";
	EXPECT_TRUE(sees(occluders, {2000, -3000}, {2000, -half - 1}));
	EXPECT_FALSE(sees(occluders, {2000, -3000}, {2000, -half + 1}));
	EXPECT_TRUE(sees(occluders, {-3000, 0}, {-1, 0}));
	EXPECT_FALSE(sees(occluders, {-3000, 0}, {1, 0})) << "the end cap";
	EXPECT_FALSE(sees(occluders, {-3000, 0}, {5000, 0}));
}

// An L corner and a T junction (mitre and trims) leave no line of sight across a wall.
TEST_F(GeometryIndexTest, CornerAndJunctionDoNotLeak) {
	{
		ConstructionWorld cw;
		buildWall(cw, {0, 0}, {4000, 0});
		buildWall(cw, {4000, 0}, {4000, 4000});
		expectNoLeak(cw, {-1000, -1000}, {5000, 5000});
	}
	{
		ConstructionWorld cw;
		buildWall(cw, {0, 0}, {4000, 0});
		buildWall(cw, {2000, 0}, {2000, 3000});
		ASSERT_EQ(cw.segments().size(), 3u) << "the stem splits the wall";
		expectNoLeak(cw, {-1000, -1000}, {5000, 4000});
	}
}

// A door is a gap sight passes through, and its jambs block: a line into the gap
// that turns into the solid wall through a jamb is hidden.
TEST_F(GeometryIndexTest, DoorGapPassesSightAndItsJambsBlock) {
	ConstructionWorld cw;
	SegmentId		  seg = buildWall(cw, {0, 0}, {4000, 0});
	addBuiltOpening(cw, seg, 0.5F, "Door");
	const std::int64_t				   half		 = halfThickness(cw);
	const std::int64_t				   gapStart	 = construction::openingGaps(cw, *cw.getSegment(seg), {}).gaps.front().fromMm;
	const std::vector<OccluderSegment> occluders = occludersOf(cw);

	EXPECT_TRUE(sees(occluders, {2000, 3000}, {2000, -3000})) << "straight through the door";
	EXPECT_FALSE(sees(occluders, {gapStart - 50, 3000}, {gapStart - 50, -3000})) << "through the solid wall beside it";
	// Enters the gap at the near face and ends inside the solid wall beside it,
	// crossing nothing but the jamb.
	EXPECT_TRUE(sees(occluders, {gapStart + 300, 2000}, {gapStart + 5, half - 50})) << "inside the gap";
	EXPECT_FALSE(sees(occluders, {gapStart + 300, 2000}, {gapStart - 20, half - 150})) << "through the jamb";
}

// A window leaves the same sight gap as a door, though it is not pathable (nav
// keeps a window's band solid).
TEST_F(GeometryIndexTest, WindowGapPassesSight) {
	ConstructionWorld cw;
	SegmentId		  seg = buildWall(cw, {0, 0}, {4000, 0});
	addBuiltOpening(cw, seg, 0.5F, "Window");
	const std::vector<OccluderSegment> occluders = occludersOf(cw);
	EXPECT_TRUE(sees(occluders, {2000, 3000}, {2000, -3000}));
	EXPECT_FALSE(sees(occluders, {500, 3000}, {500, -3000}));
}

// A door and a window on one wall leave two gaps, three solid pieces between them.
TEST_F(GeometryIndexTest, DoorAndWindowOnOneWallLeaveTwoGaps) {
	ConstructionWorld cw;
	SegmentId		  seg = buildWall(cw, {0, 0}, {6000, 0});
	addBuiltOpening(cw, seg, 0.25F, "Door");
	addBuiltOpening(cw, seg, 0.75F, "Window");
	const std::vector<OccluderSegment> occluders = occludersOf(cw);
	EXPECT_EQ(occluders.size(), 12u) << "three pieces, each two faces and two ends";
	EXPECT_TRUE(sees(occluders, {1500, 3000}, {1500, -3000})) << "through the door";
	EXPECT_TRUE(sees(occluders, {4500, 3000}, {4500, -3000})) << "through the window";
	for (const std::int64_t solid : {500, 3000, 5500}) {
		EXPECT_FALSE(sees(occluders, {solid, 3000}, {solid, -3000})) << "through the wall at x = " << solid;
	}
}

// A built opening whose type does not pass sight leaves its wall whole.
TEST_F(GeometryIndexTest, OpaqueOpeningLeavesTheWallSolid) {
	const std::filesystem::path folder = constructionConfigFolder();
	std::ifstream				real(folder / "materials.xml");
	std::string					materials((std::istreambuf_iterator<char>(real)), std::istreambuf_iterator<char>());
	const std::size_t			end = materials.find("</Opening>");
	ASSERT_NE(end, std::string::npos);
	materials.insert(end, R"(<Type name="Shutter">
      <material>Wood</material>
      <widthMeters>0.6</widthMeters>
      <pathable>false</pathable>
      <transparentToSight>false</transparentToSight>
      <costItems>2.0</costItems>
      <workUnits>40.0</workUnits>
    </Type>
  )");
	const std::filesystem::path path =
		std::filesystem::temp_directory_path() / ("geometry-index-opaque-opening-" + std::to_string(std::random_device{}()) + ".xml");
	std::ofstream(path) << materials;
	ConstructionRegistry& registry = ConstructionRegistry::Get();
	registry.clear();
	ASSERT_TRUE(registry.loadMaterials(path.string()));
	std::filesystem::remove(path);
	ASSERT_NE(registry.getOpeningType("Shutter"), nullptr);
	ASSERT_FALSE(registry.getOpeningType("Shutter")->transparentToSight);

	ConstructionWorld cw;
	SegmentId		  seg = buildWall(cw, {0, 0}, {4000, 0});
	addBuiltOpening(cw, seg, 0.5F, "Shutter");
	GeometryIndex index;
	index.setConstructionWorld(&cw);
	index.rebuildIfStale();
	EXPECT_EQ(index.occluderCount(), 4u);
	ASSERT_EQ(index.builtOpenings().size(), 1u);
	EXPECT_FALSE(index.builtOpenings().front().transparentToSight);
	std::vector<OccluderSegment> occluders;
	index.queryOccluders({2000, 0}, kFarSightMm, occluders);
	EXPECT_FALSE(sees(occluders, {2000, 3000}, {2000, -3000}));
}

// --- Blueprint handling ------------------------------------------------------

// A blueprint segment contributes no occluder; a blueprint opening does NOT cut
// the wall (only built transparent openings make gaps).
TEST_F(GeometryIndexTest, BlueprintSegmentAndOpeningContributeNoCut) {
	// Blueprint wall: commit but leave state Blueprint -> no occluder.
	{
		ConstructionWorld cw;
		SegmentCommitResult r = cw.commitSegment({0, 0}, {4000, 0}, "Wood", "Standard", kInvalidFoundation, cw.version());
		ASSERT_TRUE(r.ok());
		// Deliberately do NOT mark Built.

		GeometryIndex index;
		index.setConstructionWorld(&cw);
		index.rebuildIfStale();
		EXPECT_EQ(index.occluderCount(), 0u);
	}

	// Built wall + blueprint opening: the opening is not Built, so the wall stays solid.
	{
		ConstructionWorld cw;
		SegmentId seg = buildWall(cw, {0, 0}, {4000, 0});
		OpeningId op = cw.addOpening(seg, 0.5F, "Door", "Wood");
		ASSERT_NE(op, kInvalidOpening);
		// Opening left Blueprint.

		const std::vector<OccluderSegment> occluders = occludersOf(cw);
		EXPECT_EQ(occluders.size(), 4u);
		EXPECT_FALSE(sees(occluders, {2000, 3000}, {2000, -3000})) << "blueprint opening must not cut the wall";
	}
}

// A graph the band offsetter rejects occludes as each wall's untrimmed band.
TEST_F(GeometryIndexTest, RejectedGraphOccludesAsRawBands) {
	ConstructionWorld cw;
	// Two sharp corners round a 50 mm wall, which the offsetter rejects (as nav's test).
	const std::vector<geometry::WallSegment> rejected = {
		{{19000, 500}, {20000, 0}, 150}, {{20000, 0}, {20050, 0}, 150}, {{20050, 0}, {21050, 500}, 150}};
	ASSERT_NE(geometry::resolveWallBands(rejected, geometry::kDefaultMiterLimit).status, geometry::OffsetStatus::Ok);
	for (const geometry::WallSegment& wall : rejected) {
		buildWall(cw, wall.a, wall.b, "Heavy");
	}
	GeometryIndex index;
	index.setConstructionWorld(&cw);
	index.rebuildIfStale();
	ASSERT_EQ(index.occluderCount(), 12u) << "four edges a wall";
	for (const GeometryIndex::OccluderRecord& occluder : index.occluders()) {
		const construction::WallSegment* wall = cw.getSegment(occluder.source);
		ASSERT_NE(wall, nullptr);
		const geometry::Ring band = geometry::band(cw.getVertex(wall->v0)->pos, cw.getVertex(wall->v1)->pos, 150);
		EXPECT_NE(std::find(band.begin(), band.end(), occluder.seg.a), band.end());
		EXPECT_NE(std::find(band.begin(), band.end(), occluder.seg.b), band.end());
	}
}

// A graph the offsetter rejects still passes sight through its doors and windows: a
// closed room with a door and a window, and a stem outside meeting its south wall 150 mm
// short of a corner, which leaves a piece shorter than its trims.
TEST_F(GeometryIndexTest, RejectedGraphPassesSightThroughItsOpenings) {
	ConstructionWorld cw;
	buildWall(cw, {0, 0}, {4000, 0});
	buildWall(cw, {4000, 0}, {4000, 4000});
	const SegmentId north = buildWall(cw, {4000, 4000}, {0, 4000});
	const SegmentId west  = buildWall(cw, {0, 4000}, {0, 0});
	addBuiltOpening(cw, west, 0.5F, "Door");
	addBuiltOpening(cw, north, 0.5F, "Window");
	buildWall(cw, {3850, 0}, {2000, -3000});
	std::vector<geometry::WallSegment> walls;
	for (const construction::WallSegment& wall : cw.segments()) {
		walls.push_back({cw.getVertex(wall.v0)->pos, cw.getVertex(wall.v1)->pos, construction::halfThicknessMm(wall)});
	}
	ASSERT_EQ(geometry::resolveWallBands(walls, geometry::kDefaultMiterLimit).status, geometry::OffsetStatus::TrimOverrunsSegment);

	const std::vector<OccluderSegment> occluders = occludersOf(cw);
	EXPECT_TRUE(sees(occluders, {-3000, 2000}, {2000, 2000})) << "through the door";
	EXPECT_TRUE(sees(occluders, {2000, 7000}, {2000, 2000})) << "through the window";
	EXPECT_FALSE(sees(occluders, {-3000, 1000}, {2000, 1000})) << "through the west wall beside the door";
	EXPECT_FALSE(sees(occluders, {1000, 7000}, {1000, 2000})) << "through the north wall beside the window";
	EXPECT_FALSE(sees(occluders, {2000, -1000}, {2000, 2000})) << "through the south wall";
	expectNoLeak(cw, {-1000, -3000}, {5000, 5000});
}

// --- queryOccluders range ----------------------------------------------------

// An observer within radius gets the nearby occluders; one far beyond it gets none.
TEST_F(GeometryIndexTest, QueryRespectsRadius) {
	ConstructionWorld cw;
	buildWall(cw, {0, 0}, {4000, 0});

	GeometryIndex index;
	index.setConstructionWorld(&cw);
	index.rebuildIfStale();

	std::vector<OccluderSegment> near;
	index.queryOccluders({2000, 500}, 1000, near); // 400 mm off the near face, 1 m radius
	EXPECT_EQ(near.size(), 2u) << "both faces, neither cap";

	std::vector<OccluderSegment> far;
	index.queryOccluders({2000, 50000}, 1000, far); // 50 m off the wall, 1 m radius
	EXPECT_TRUE(far.empty());
}

// --- Facing occluders --------------------------------------------------------

// Above a straight wall only its north face faces the eye; both caps and the south
// face are back faces.
TEST_F(GeometryIndexTest, FacingOccludersLeaveOutBackFaces) {
	ConstructionWorld cw;
	buildWall(cw, {0, 0}, {4000, 0});
	const std::int64_t half = halfThickness(cw);
	GeometryIndex	   index;
	index.setConstructionWorld(&cw);
	index.rebuildIfStale();

	std::vector<OccluderSegment> facing;
	index.queryFacingOccluders({2000, 3000}, kFarSightMm, facing);
	ASSERT_EQ(facing.size(), 1u);
	EXPECT_EQ(facing.front().a.y, half);
	EXPECT_EQ(facing.front().b.y, half);
}

// Culling the back faces changes no visibility polygon, round a room with a door and
// round an L corner, a T junction and a lone wall's end. The turned room's grid has an
// eye 0.12 mm off a face, whose ray into the wall snaps its hit onto the eye.
TEST_F(GeometryIndexTest, FacingOccludersGiveTheSameSight) {
	{
		ConstructionWorld cw;
		buildRoomWithADoor(cw);
		expectFacingSightIsTheSame(cw, {-1500, -1500}, {5500, 5500});
	}
	{
		ConstructionWorld cw;
		buildJunctions(cw);
		ASSERT_EQ(cw.segments().size(), 6u) << "the stem splits the T's wall";
		expectFacingSightIsTheSame(cw, {-1500, -1500}, {17500, 4500});
	}
	{
		// The room turned 30 degrees: a ray at a back face's hidden end rounds its hit on a
		// slanted face to the millimetre, a vertex the facing polygon has no need of. Hits
		// lie up to 0.71 mm off the face, so a vertex of one ring and a chord of the other
		// up to twice that apart.
		constexpr std::int64_t kHitRoundingMm = 2;
		ConstructionWorld	   cw;
		const SegmentId		   south = buildWall(cw, {0, 0}, {3464, 2000});
		buildWall(cw, {3464, 2000}, {1464, 5464});
		buildWall(cw, {1464, 5464}, {-2000, 3464});
		buildWall(cw, {-2000, 3464}, {0, 0});
		addBuiltOpening(cw, south, 0.5F, "Door");
		expectFacingSightIsTheSame(cw, {-3500, -1500}, {5000, 7000}, kHitRoundingMm);
	}
}

// The solid is what the outline loops wind round: a band's inside and its edge, not a
// closed room's floor or the open ground.
TEST_F(GeometryIndexTest, InsideSolidIsTheBandNotTheRoom) {
	ConstructionWorld cw;
	buildRoomWithADoor(cw);
	const std::int64_t half = halfThickness(cw);
	GeometryIndex	   index;
	index.setConstructionWorld(&cw);
	index.rebuildIfStale();
	EXPECT_TRUE(index.insideSolid({1000, 0}));
	EXPECT_TRUE(index.insideSolid({1000, half}));
	EXPECT_TRUE(index.insideSolid({0, 0}));
	EXPECT_TRUE(index.insideSolid({4000, 4000}));
	EXPECT_FALSE(index.insideSolid({2000, 0})) << "the doorway";
	EXPECT_FALSE(index.insideSolid({1000, half + 1}));
	EXPECT_FALSE(index.insideSolid({2000, 2000})) << "the room's floor";
	EXPECT_FALSE(index.insideSolid({-3000, 2000}));
}

// --- rebuildIfStale version gating -------------------------------------------

// After a rebuild, mutating the world (adding a door bumps version) is reflected by
// the next rebuildIfStale; a call with unchanged version does not rebuild.
TEST_F(GeometryIndexTest, RebuildIfStaleTracksVersion) {
	ConstructionWorld cw;
	SegmentId seg = buildWall(cw, {0, 0}, {4000, 0});

	GeometryIndex index;
	index.setConstructionWorld(&cw);
	index.rebuildIfStale();
	ASSERT_EQ(index.occluderCount(), 4u); // solid wall
	const std::uint64_t generation = index.generation();

	// No mutation: another rebuildIfStale is a no-op and the cache is unchanged.
	const std::uint64_t versionBefore = cw.version();
	index.rebuildIfStale();
	EXPECT_EQ(index.occluderCount(), 4u);
	EXPECT_EQ(index.generation(), generation);

	// Cut a built door: version bumps, next rebuildIfStale reflects the gap.
	addBuiltOpening(cw, seg, 0.5F, "Door");
	EXPECT_NE(cw.version(), versionBefore);

	index.rebuildIfStale();
	EXPECT_EQ(index.occluderCount(), 8u) << "two pieces, each two faces, a cap, and a jamb";

	std::vector<OccluderSegment> occ;
	index.queryOccluders({2000, 0}, kFarSightMm, occ);
	EXPECT_TRUE(sees(occ, {2000, 3000}, {2000, -3000})) << "rebuild should now show the door gap";
}

// hasWorld / no-world inertness.
TEST_F(GeometryIndexTest, NoWorldIsInert) {
	GeometryIndex index;
	EXPECT_FALSE(index.hasWorld());
	index.rebuildIfStale(); // safe no-op
	EXPECT_EQ(index.occluderCount(), 0u);
}

// builtOpenings record carries the jamb points and transparency flag for V3.
TEST_F(GeometryIndexTest, OpeningRecordsExposedForV3) {
	ConstructionWorld cw;
	SegmentId seg = buildWall(cw, {0, 0}, {4000, 0});
	addBuiltOpening(cw, seg, 0.5F, "Window");

	GeometryIndex index;
	index.setConstructionWorld(&cw);
	index.rebuildIfStale();

	ASSERT_EQ(index.builtOpenings().size(), 1u);
	const GeometryIndex::OpeningRecord& rec = index.builtOpenings().front();
	EXPECT_EQ(rec.segment, seg);
	EXPECT_EQ(rec.type, "Window");
	EXPECT_TRUE(rec.transparentToSight);
	// Jamb points straddle the wall centerline midpoint (the window center).
	EXPECT_LT(rec.jambA.x, 2000);
	EXPECT_GT(rec.jambB.x, 2000);
}

// --- Sight polygon -----------------------------------------------------------

// The visibility polygon of an observer beside a wall runs along the wall's near
// face, and no point of the band past that face is inside it.
TEST_F(GeometryIndexTest, SightPolygonEndsAtTheNearFace) {
	ConstructionWorld cw;
	buildWall(cw, {0, 0}, {4000, 0});
	const std::int64_t				   half		 = halfThickness(cw);
	const std::vector<OccluderSegment> occluders = occludersOf(cw);
	const geometry::Ring			   polygon	 = geometry::computeVisibilityPolygon({1500, 2500}, 20000, occluders);
	ASSERT_GE(polygon.size(), 3u);

	constexpr std::int64_t kOnTheFaceMm = 1; // hits are snapped to the millimetre
	bool				   alongTheFace = false;
	for (const Vec2i64& vertex : polygon) {
		if (vertex.x >= 0 && vertex.x <= 4000 && std::llabs(vertex.y) <= half + kOnTheFaceMm) {
			EXPECT_LE(std::llabs(vertex.y - half), kOnTheFaceMm) << "vertex (" << vertex.x << ", " << vertex.y << ") is not on the near face";
			alongTheFace = true;
		}
	}
	EXPECT_TRUE(alongTheFace);
	for (std::int64_t x = 50; x < 4000; x += 50) {
		for (std::int64_t y = -half; y < half; y += 10) {
			EXPECT_EQ(geometry::pointInPolygon({x, y}, polygon), geometry::PointInPolygon::Outside) << "(" << x << ", " << y << ") is in sight";
		}
	}
}

// An eye inside a wall's band (nav carves walls out, so only by mistake) sees the
// inside of the band and nothing past it, without failing: the facing query culls
// nothing for it.
TEST_F(GeometryIndexTest, EyeInsideTheBandSeesOnlyTheBand) {
	ConstructionWorld cw;
	buildWall(cw, {0, 0}, {4000, 0});
	const std::int64_t half = halfThickness(cw);
	GeometryIndex	   index;
	index.setConstructionWorld(&cw);
	index.rebuildIfStale();
	std::vector<OccluderSegment> occluders;
	index.queryFacingOccluders({2000, 0}, 20000, occluders);
	EXPECT_EQ(occluders.size(), 4u);
	const geometry::Ring polygon = geometry::computeVisibilityPolygon({2000, 0}, 20000, occluders);
	ASSERT_GE(polygon.size(), 3u);
	EXPECT_NE(geometry::pointInPolygon({2000, 0}, polygon), geometry::PointInPolygon::Outside);
	for (const Vec2i64& vertex : polygon) {
		EXPECT_LE(std::llabs(vertex.y), half);
		EXPECT_GE(vertex.x, 0);
		EXPECT_LE(vertex.x, 4000);
	}
}
