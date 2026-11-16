#include "OpeningGeometry.h"

#include "ConstructionValidator.h"
#include "ConstructionWorld.h"
#include "SnapEngine.h"

#include <assets/ConstructionRegistry.h>
#include <core/Vec2i64.h>
#include <offset/WallOffset.h>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <gtest/gtest.h>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

using namespace construction;
using assets::ConstructionRegistry;
using geometry::Vec2i64;

namespace {

	constexpr double kDoorWidthMeters = 0.9;

	// The end margin the validator and the snap read (0.3 m in the shipped config).
	double marginMeters() {
		return static_cast<double>(ConstructionRegistry::Get().constraints().openingMarginMeters);
	}

	class OpeningRangeTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			ConstructionRegistry::Get().clear();
			std::filesystem::path folder = __FILE__; // libs/sim/construction/...test.cpp
			folder = folder.parent_path().parent_path().parent_path().parent_path() / "assets" / "config" / "construction";
			ASSERT_TRUE(ConstructionRegistry::Get().load(folder.string()));
			ASSERT_NEAR(marginMeters(), 0.3, 1e-6);
		}
		void TearDown() override { ConstructionRegistry::Get().clear(); }
	};

	// A freestanding Wood wall from `a` to `b` (mm), built so an opening can snap to it.
	SegmentId addWall(ConstructionWorld& world, Vec2i64 a, Vec2i64 b, const std::string& thickness) {
		const SegmentCommitResult result = world.commitSegment(a, b, "Wood", thickness, kInvalidFoundation);
		EXPECT_TRUE(result.ok());
		for (const SegmentId id : result.createdSegments) {
			EXPECT_TRUE(world.setSegmentState(id, FoundationState::Built));
		}
		return result.id;
	}

	// The point `lengthMm` from `from` at `degrees` from +x.
	Vec2i64 towards(Vec2i64 from, double degrees, double lengthMm) {
		const double radians = degrees * std::numbers::pi / 180.0;
		return {from.x + std::llround(lengthMm * std::cos(radians)), from.y + std::llround(lengthMm * std::sin(radians))};
	}

	std::int64_t halfOf(const WallSegment& wall) {
		return ConstructionRegistry::Get().getThicknessPreset(wall.material, wall.thicknessPreset)->halfThicknessMm;
	}

	// The trims (mm) at `segment`'s v0 and v1 the renderer draws: resolveWallBands over every wall.
	std::pair<std::int64_t, std::int64_t> drawnTrimsMm(const ConstructionWorld& world, SegmentId segment) {
		std::vector<geometry::WallSegment> walls;
		std::size_t						   target = 0;
		for (const WallSegment& wall : world.segments()) {
			if (wall.id == segment) {
				target = walls.size();
			}
			walls.push_back({world.getVertex(wall.v0)->pos, world.getVertex(wall.v1)->pos, halfOf(wall)});
		}
		const geometry::WallBands bands = geometry::resolveWallBands(walls, geometry::kDefaultMiterLimit);
		EXPECT_EQ(bands.status, geometry::OffsetStatus::Ok);
		if (bands.status != geometry::OffsetStatus::Ok) {
			return {-1, -1};
		}
		return {bands.trimAtA[target], bands.trimAtB[target]};
	}

	// The trim (mm) of `segment` at `vertexId` from resolveJunction over the walls meeting
	// there alone, the way openingRange measured it before it read the whole graph.
	std::int64_t junctionTrimMm(const ConstructionWorld& world, SegmentId segment, VertexId vertexId) {
		const Vertex* vertex = world.getVertex(vertexId);
		if (vertex->segments.size() < 2) {
			return 0;
		}
		std::vector<geometry::IncidentSegment> incidents;
		std::size_t							   own = 0;
		for (const SegmentId id : vertex->segments) {
			const WallSegment* other = world.getSegment(id);
			if (id == segment) {
				own = incidents.size();
			}
			const VertexId far = other->v0 == vertexId ? other->v1 : other->v0;
			incidents.push_back({world.getVertex(far)->pos, halfOf(*other), incidents.size()});
		}
		const geometry::JunctionResolution junction = geometry::resolveJunction(vertex->pos, incidents, geometry::kDefaultMiterLimit);
		EXPECT_EQ(junction.status, geometry::OffsetStatus::Ok);
		for (const geometry::JunctionTrim& trim : junction.trims) {
			if (trim.index == own) {
				return trim.trimMm;
			}
		}
		return 0;
	}

	// minT and maxT as openingRange worked them out before, from per-junction trims.
	std::pair<float, float> rangeFromJunctions(const ConstructionWorld& world, SegmentId segment) {
		const WallSegment* wall = world.getSegment(segment);
		const Vec2i64	   a = world.getVertex(wall->v0)->pos;
		const Vec2i64	   b = world.getVertex(wall->v1)->pos;
		const double	   dx = static_cast<double>(b.x - a.x);
		const double	   dy = static_cast<double>(b.y - a.y);
		const double	   length = std::sqrt(dx * dx + dy * dy) / 1000.0;
		const double	   trimV0 = static_cast<double>(junctionTrimMm(world, segment, wall->v0)) / 1000.0;
		const double	   trimV1 = static_cast<double>(junctionTrimMm(world, segment, wall->v1)) / 1000.0;
		const double	   half = kDoorWidthMeters / 2.0;
		return {static_cast<float>((trimV0 + marginMeters() + half) / length),
				static_cast<float>((length - trimV1 - marginMeters() - half) / length)};
	}

	// Every wall's door range in `world` is what the per-junction trims gave, and those
	// are the trims the renderer draws: the layout never takes resolveWallBands' retry.
	void expectRangesUnchanged(const ConstructionWorld& world, const std::string& layout) {
		for (const WallSegment& wall : world.segments()) {
			const std::optional<OpeningRange> range = openingRange(world, wall.id, kDoorWidthMeters, marginMeters());
			ASSERT_TRUE(range.has_value()) << layout;
			const auto [drawnV0, drawnV1] = drawnTrimsMm(world, wall.id);
			EXPECT_EQ(drawnV0, junctionTrimMm(world, wall.id, wall.v0)) << layout << " wall " << wall.id;
			EXPECT_EQ(drawnV1, junctionTrimMm(world, wall.id, wall.v1)) << layout << " wall " << wall.id;
			const auto [minT, maxT] = rangeFromJunctions(world, wall.id);
			EXPECT_EQ(range->minT, minT) << layout << " wall " << wall.id;
			EXPECT_EQ(range->maxT, maxT) << layout << " wall " << wall.id;
		}
	}

} // namespace

TEST_F(OpeningRangeTest, LayoutsWithoutTheSteppedRetryKeepTheirRange) {
	{
		ConstructionWorld world;
		addWall(world, {0, 0}, {4000, 0}, "Standard");
		expectRangesUnchanged(world, "free-ended wall");
	}
	{
		ConstructionWorld world;
		addWall(world, {0, 0}, {4000, 0}, "Standard");
		addWall(world, {4000, 0}, {8000, 0}, "Heavy");
		expectRangesUnchanged(world, "straight join");
	}
	for (const double degrees : {30.0, 35.0, 45.0, 90.0, 135.0, 155.0}) {
		for (const auto& [thickness, otherThickness] :
			 std::vector<std::pair<std::string, std::string>>{{"Heavy", "Heavy"}, {"Light", "Heavy"}, {"Standard", "Light"}}) {
			ConstructionWorld world;
			addWall(world, {4000, 0}, {0, 0}, thickness);
			addWall(world, {4000, 0}, towards({4000, 0}, degrees, 4000.0), otherThickness);
			expectRangesUnchanged(world, std::to_string(static_cast<int>(degrees)) + " deg " + thickness + "/" + otherThickness);
		}
	}
	{
		ConstructionWorld world;
		addWall(world, {0, 0}, {8000, 0}, "Light");
		addWall(world, {4000, 0}, {4000, 4000}, "Heavy"); // splits the Light wall
		expectRangesUnchanged(world, "T-junction");
	}
	{
		ConstructionWorld world;
		addWall(world, {0, 0}, {5000, 0}, "Standard");
		addWall(world, {5000, 0}, {5000, 4000}, "Heavy");
		addWall(world, {5000, 4000}, {0, 4000}, "Standard");
		addWall(world, {0, 4000}, {0, 0}, "Light");
		addWall(world, {2500, 0}, {2500, 4000}, "Standard"); // a divider, a T at each end
		expectRangesUnchanged(world, "closed room with a divider");
	}
}

// A 0.5 m Light wall A and a 2 m Heavy wall meeting its end 8 degrees off straight.
// The miter there trims A by more than its length, so the offsetter draws the join
// as a step instead, which trims the Heavy wall by nothing. The door range on the
// Heavy wall comes from that step, not the 0.53 m miter.
TEST_F(OpeningRangeTest, DoorRangeOnAWallBesideASteppedJoinIsMeasuredFromTheStep) {
	ConstructionWorld world;
	const SegmentId	  shortWall = addWall(world, {0, 0}, {500, 0}, "Light");
	const SegmentId	  heavy = addWall(world, {500, 0}, towards({500, 0}, 8.0, 2000.0), "Heavy");
	const VertexId	  join = world.getSegment(heavy)->v0;
	ASSERT_EQ(world.getVertex(join)->pos, (Vec2i64{500, 0}));
	EXPECT_EQ(junctionTrimMm(world, heavy, join), 532);
	EXPECT_GT(junctionTrimMm(world, shortWall, join), 500) << "the miter overruns the short wall";
	EXPECT_EQ(drawnTrimsMm(world, heavy), (std::pair<std::int64_t, std::int64_t>{0, 0}));

	const std::optional<OpeningRange> range = openingRange(world, heavy, kDoorWidthMeters, marginMeters());
	ASSERT_TRUE(range.has_value());
	ASSERT_TRUE(range->fits());
	EXPECT_DOUBLE_EQ(range->trimAtV0Meters, 0.0);
	EXPECT_DOUBLE_EQ(range->trimAtV1Meters, 0.0);
	EXPECT_NEAR(range->lengthMeters, 2.0, 1e-3);
	EXPECT_NEAR(range->minT * range->lengthMeters, 0.75, 1e-3);
	EXPECT_NEAR(range->maxT * range->lengthMeters, 1.25, 1e-3);

	const auto&					registry = ConstructionRegistry::Get();
	const ConstructionValidator validator(registry.constraints(), world);
	EXPECT_TRUE(validator.validateOpening(heavy, 0.5F, "Door", "Wood").ok());
	EXPECT_TRUE(validator.validateOpening(heavy, range->minT, "Door", "Wood").ok());
	EXPECT_EQ(validator.validateOpening(heavy, range->minT - 0.002F, "Door", "Wood").code, ValidationCode::OpeningMarginTooSmall);

	// The cursor just past the join: the snap stops where the validator does.
	const SnapEngine  engine(registry.snapping(), world);
	const OpeningSnap snap = engine.snapOpening({0.55F, 0.0F}, static_cast<float>(kDoorWidthMeters));
	ASSERT_TRUE(snap.valid);
	EXPECT_EQ(snap.segment, heavy);
	EXPECT_FLOAT_EQ(snap.t, range->minT);
	EXPECT_TRUE(validator.validateOpening(heavy, snap.t, "Door", "Wood").ok());
}

// A 1 m Light wall between two 2 m Heavy walls, each 8 degrees off straight. Both
// joins are stepped, so each Heavy wall's range runs from the margin at the join, and
// the short wall's is measured from the step's small trims, not the miters' 1.09 m.
TEST_F(OpeningRangeTest, ShortWallBetweenTwoThickerNearStraightWalls) {
	ConstructionWorld world;
	const Vec2i64	  left{0, 0};
	const Vec2i64	  right{1000, 0};
	const SegmentId	  before = addWall(world, towards(left, 172.0, 2000.0), left, "Heavy");
	const SegmentId	  shortWall = addWall(world, left, right, "Light");
	const SegmentId	  after = addWall(world, right, towards(right, 8.0, 2000.0), "Heavy");
	const WallSegment& middle = *world.getSegment(shortWall);
	ASSERT_GT(junctionTrimMm(world, shortWall, middle.v0) + junctionTrimMm(world, shortWall, middle.v1), 1000)
		<< "the miters overrun the short wall";

	for (const SegmentId heavy : {before, after}) {
		const auto [drawnV0, drawnV1] = drawnTrimsMm(world, heavy);
		EXPECT_EQ(drawnV0, 0);
		EXPECT_EQ(drawnV1, 0);
		const std::optional<OpeningRange> range = openingRange(world, heavy, kDoorWidthMeters, marginMeters());
		ASSERT_TRUE(range.has_value());
		ASSERT_TRUE(range->fits());
		EXPECT_NEAR(range->minT * range->lengthMeters, 0.75, 1e-3);
		EXPECT_NEAR(range->maxT * range->lengthMeters, 1.25, 1e-3);
	}

	const auto [drawnV0, drawnV1] = drawnTrimsMm(world, shortWall);
	EXPECT_LT(drawnV0 + drawnV1, 50);
	const std::optional<OpeningRange> range = openingRange(world, shortWall, kDoorWidthMeters, marginMeters());
	ASSERT_TRUE(range.has_value());
	EXPECT_DOUBLE_EQ(range->trimAtV0Meters, static_cast<double>(drawnV0) / 1000.0);
	EXPECT_DOUBLE_EQ(range->trimAtV1Meters, static_cast<double>(drawnV1) / 1000.0);
}

// An opening on the rescued wall itself. A wall the miters overrun is shorter than
// two miter-bound trims (a little under 1.2 m), too short for a door or a window with
// their margins, so the door is refused for its length, measured along the stepped
// band, and a narrower opening fits where the miters left no room at all.
TEST_F(OpeningRangeTest, OpeningOnTheRescuedShortWall) {
	ConstructionWorld world;
	const Vec2i64	  left{0, 0};
	const Vec2i64	  right{1000, 0};
	addWall(world, towards(left, 172.0, 2000.0), left, "Heavy");
	const SegmentId shortWall = addWall(world, left, right, "Light");
	addWall(world, right, towards(right, 8.0, 2000.0), "Heavy");
	const auto [drawnV0, drawnV1] = drawnTrimsMm(world, shortWall);
	const double drawnLength = 1.0 - static_cast<double>(drawnV0 + drawnV1) / 1000.0;

	const ConstructionValidator	   validator(ConstructionRegistry::Get().constraints(), world);
	const ValidationResult door = validator.validateOpening(shortWall, 0.5F, "Door", "Wood");
	EXPECT_EQ(door.code, ValidationCode::OpeningWallTooShort);
	EXPECT_NEAR(door.measuredValue, drawnLength, 1e-6);

	const std::optional<OpeningRange> narrow = openingRange(world, shortWall, 0.3, marginMeters());
	ASSERT_TRUE(narrow.has_value());
	ASSERT_TRUE(narrow->fits());
	EXPECT_NEAR(narrow->minT, 0.45 + static_cast<double>(drawnV0) / 1000.0, 1e-6);
	EXPECT_NEAR(narrow->maxT, 0.55 - static_cast<double>(drawnV1) / 1000.0, 1e-6);
}

// The renderer draws a graph the offsetter rejects as bare centerlines, and nav keeps
// every band in it whole, so no opening may go on any wall of it.
TEST_F(OpeningRangeTest, NoRangeOnAWallInARejectedGraph) {
	ConstructionWorld world;
	// A 0.4 m Heavy wall between two 30 degree corners: its trims overrun it even stepped.
	const Vec2i64	left{0, 0};
	const Vec2i64	right{400, 0};
	const SegmentId longWall = addWall(world, left, towards(left, 30.0, 4000.0), "Heavy");
	const SegmentId shortWall = addWall(world, left, right, "Heavy");
	addWall(world, right, towards(right, 210.0, 4000.0), "Heavy");
	std::vector<geometry::WallSegment> walls;
	for (const WallSegment& wall : world.segments()) {
		walls.push_back({world.getVertex(wall.v0)->pos, world.getVertex(wall.v1)->pos, halfOf(wall)});
	}
	ASSERT_NE(geometry::resolveWallBands(walls, geometry::kDefaultMiterLimit).status, geometry::OffsetStatus::Ok);

	EXPECT_FALSE(openingRange(world, longWall, kDoorWidthMeters, marginMeters()).has_value());
	EXPECT_FALSE(openingRange(world, shortWall, kDoorWidthMeters, marginMeters()).has_value());
	const ConstructionValidator validator(ConstructionRegistry::Get().constraints(), world);
	EXPECT_EQ(validator.validateOpening(longWall, 0.5F, "Door", "Wood").code, ValidationCode::OpeningSegmentInvalid);
}
