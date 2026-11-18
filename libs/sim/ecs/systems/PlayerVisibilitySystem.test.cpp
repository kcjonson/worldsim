#include "PlayerVisibilitySystem.h"

#include "NavigationSystem.h"
#include "TimeSystem.h"
#include "VisionSystem.h"

#include "../World.h"
#include "../components/Colonist.h"
#include "../components/Memory.h"
#include "../components/Movement.h"
#include "../components/Transform.h"

#include "nav/NavCoords.h"

#include <assets/ConstructionRegistry.h>
#include <construction/ConstructionWorld.h>
#include <nav/SightCover.h>
#include <predicates/Predicates.h>
#include <visibility/ForestShape.h>
#include <visibility/SightClip.h>
#include <visibility/SightPolygon.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <limits>
#include <numbers>
#include <random>
#include <set>
#include <vector>

#include <gtest/gtest.h>

using namespace ecs;
using geometry::Vec2i64;

namespace {

	constexpr float kSight = kDefaultSightRadius;

	// Vision every frame, so each frame's sight shape is the one this frame's position gives.
	struct Colony {
		World					world;
		VisionSystem&			vision	   = world.registerSystem<VisionSystem>();
		PlayerVisibilitySystem& visibility = world.registerSystem<PlayerVisibilitySystem>();

		Colony() { vision.setUpdateInterval(1); }

		EntityID addColonist(glm::vec2 at) {
			const EntityID colonist = world.createEntity();
			world.addComponent<Position>(colonist, Position{at});
			world.addComponent<Colonist>(colonist, Colonist{"Test"});
			world.addComponent<Memory>(colonist, Memory{});
			world.getComponent<Memory>(colonist)->owner = colonist;
			return colonist;
		}

		void tick() { world.update(0.016F); }

		void standAt(EntityID colonist, glm::vec2 at) {
			world.getComponent<Position>(colonist)->value = at;
			tick();
		}

		// Walks from where he stands to `to` in steps of `stepMeters`, one a frame.
		void walk(EntityID colonist, glm::vec2 to, float stepMeters) {
			const glm::vec2 from  = world.getComponent<Position>(colonist)->value;
			const int		steps = static_cast<int>(std::ceil(glm::distance(from, to) / stepMeters));
			for (int step = 1; step <= steps; ++step) {
				standAt(colonist, from + (to - from) * (static_cast<float>(step) / static_cast<float>(steps)));
			}
		}

		// Removes him, which ends his leg on the next frame, and lets the budget commit everything pending.
		void remove(EntityID colonist) {
			world.destroyEntity(colonist);
			tick();
			for (int frame = 0; frame < 5000 && (visibility.seenArea().shapesPending() > 0 || visibility.seenArea().sweepsWaiting() > 0); ++frame) {
				tick();
			}
			ASSERT_EQ(visibility.seenArea().shapesPending(), 0U);
		}
	};

	std::filesystem::path projectRoot() {
		std::filesystem::path path = __FILE__;
		return path.parent_path().parent_path().parent_path().parent_path().parent_path();
	}

	void buildWall(construction::ConstructionWorld& walls, Vec2i64 from, Vec2i64 to) {
		const construction::SegmentCommitResult result =
			walls.commitSegment(from, to, "Wood", "Standard", construction::kInvalidFoundation, walls.version());
		ASSERT_TRUE(result.ok());
		for (const construction::SegmentId segment : result.createdSegments) {
			walls.setSegmentState(segment, construction::FoundationState::Built);
		}
	}

	// Inside the sight polygon round the build of the shape's visibility polygon, by the 64-gon's
	// 36 mm sag and a margin: what of it the polygon holds is pending or in the record.
	bool withinSightOfBuild(const VisionSystem::SightShape& shape, const Vec2i64& point) {
		return shape.polygon == nullptr || geometry::withinDistanceOfSegment(point, nav::toMm(shape.center), nav::toMm(shape.center), std::llround(shape.radius * 1000.0F) - 50);
	}

	// The seen boxes hold everything the cover leaves showing (player-visibility.md 3.4): every point
	// of each committed triangle (its corners, edge midpoints and centre) and every vertex of each
	// pending outline lies in some box. Drawing culls to them, so a point outside would be lost.
	void expectSeenBoxesHoldWhatShows(const PlayerVisibilitySystem& visibility, int frame) {
		std::vector<PlayerVisibilitySystem::SeenBox> boxes;
		visibility.appendSeenBoxes({-10000.0F, -10000.0F}, {10000.0F, 10000.0F}, boxes);
		const auto held = [&boxes](const Vec2i64& point) {
			const glm::vec2 meters = nav::toMeters(point);
			return std::any_of(boxes.begin(), boxes.end(), [&meters](const PlayerVisibilitySystem::SeenBox& box) {
				return meters.x >= box.low.x && meters.x <= box.high.x && meters.y >= box.low.y && meters.y <= box.high.y;
			});
		};
		std::vector<vision::SeenArea::Triangle> triangles;
		visibility.seenArea().appendTriangles(triangles);
		std::size_t outside = 0;
		for (const vision::SeenArea::Triangle& triangle : triangles) {
			const auto& [first, second, third] = triangle.corners;
			const auto	between = [](const Vec2i64& sum, std::int64_t count) { return Vec2i64{sum.x / count, sum.y / count}; };
			const std::array<Vec2i64, 7> points{
				first, second, third, between(first + second, 2), between(second + third, 2), between(third + first, 2), between(first + second + third, 3)
			};
			outside += static_cast<std::size_t>(std::count_if(points.begin(), points.end(), [&held](const Vec2i64& point) { return !held(point); }));
		}
		std::vector<const std::vector<Vec2i64>*> pending;
		visibility.seenArea().appendPendingShapes(pending);
		for (const std::vector<Vec2i64>* outline : pending) {
			outside += static_cast<std::size_t>(std::count_if(outline->begin(), outline->end(), [&held](const Vec2i64& point) { return !held(point); }));
		}
		EXPECT_EQ(outside, 0U) << "frame " << frame << ": " << triangles.size() << " triangles, " << pending.size() << " pending";
	}

} // namespace

// A straight walk of any length is one ring, pending while he walks and committed when the leg ends.
TEST(PlayerVisibilitySystem, StraightWalkIsOneRing) {
	Colony		   colony;
	const EntityID colonist = colony.addColonist({0.0F, 0.0F});
	colony.tick();
	EXPECT_TRUE(colony.visibility.isVisible({kSight - 1.0F, 0.0F}));
	EXPECT_FALSE(colony.visibility.isVisible({kSight + 1.0F, 0.0F}));

	colony.walk(colonist, {200.0F, 0.0F}, 0.05F);
	EXPECT_EQ(colony.visibility.seenArea().ringsRegistered(), 0U);
	EXPECT_EQ(colony.visibility.seenArea().shapesPending(), 1U);
	EXPECT_TRUE(colony.visibility.isSeen({100.0F, kSight - 1.0F})) << "the open leg is seen";
	EXPECT_FALSE(colony.visibility.isVisible({100.0F, 0.0F})) << "behind him, out of sight";

	colony.remove(colonist);
	EXPECT_EQ(colony.visibility.seenArea().ringsRegistered(), 1U);
	EXPECT_TRUE(colony.visibility.isSeen({100.0F, kSight - 0.1F}));
	EXPECT_TRUE(colony.visibility.isSeen({200.0F + kSight - 0.1F, 0.0F}));
	EXPECT_FALSE(colony.visibility.isSeen({200.0F + kSight + 0.1F, 0.0F}));
	EXPECT_FALSE(colony.visibility.isSeen({100.0F, kSight + 0.1F}));
}

// A wobble inside the slack keeps one leg; a turn ends it.
TEST(PlayerVisibilitySystem, TurnEndsTheLeg) {
	Colony		   colony;
	const EntityID colonist = colony.addColonist({0.0F, 0.0F});
	for (int step = 1; step <= 1000; ++step) {
		const float x = 0.05F * static_cast<float>(step);
		colony.standAt(colonist, {x, 0.02F * std::sin(x)});
	}
	EXPECT_EQ(colony.visibility.seenArea().ringsRegistered(), 0U) << "20 mm either side of the line is inside the slack";
	colony.walk(colonist, {50.0F, 50.0F}, 0.05F);
	EXPECT_EQ(colony.visibility.seenArea().ringsRegistered(), 1U) << "the turn north ended the leg east";
	colony.remove(colonist);
	EXPECT_EQ(colony.visibility.seenArea().ringsRegistered(), 2U);
	EXPECT_TRUE(colony.visibility.isSeen({50.0F + kSight - 0.1F, 50.0F}));
}

// A step longer than kSeenJumpMeters ends the leg, and nothing bridges the jump.
TEST(PlayerVisibilitySystem, JumpDoesNotBridge) {
	Colony		   colony;
	const EntityID colonist = colony.addColonist({0.0F, 0.0F});
	colony.walk(colonist, {20.0F, 0.0F}, 0.05F);
	colony.standAt(colonist, {120.0F, 0.0F});
	colony.walk(colonist, {140.0F, 0.0F}, 0.05F);
	colony.remove(colonist);
	EXPECT_EQ(colony.visibility.seenArea().ringsRegistered(), 2U);
	EXPECT_FALSE(colony.visibility.isSeen({70.0F, 0.0F}));
	EXPECT_TRUE(colony.visibility.isSeen({20.0F + kSight - 0.1F, 0.0F}));
	EXPECT_TRUE(colony.visibility.isSeen({120.0F - kSight + 0.1F, 0.0F}));
}

// The landing is committed before anyone exists; a leg inside it registers nothing.
TEST(PlayerVisibilitySystem, LandingAndACoveredLeg) {
	Colony colony;
	colony.visibility.revealLanding({10.0F, 10.0F}, 150.0F);
	EXPECT_EQ(colony.visibility.seenArea().ringsRegistered(), 1U);
	EXPECT_TRUE(colony.visibility.isSeen({110.0F, 10.0F}));
	EXPECT_FALSE(colony.visibility.isSeen({170.0F, 10.0F}));
	EXPECT_FALSE(colony.visibility.isVisible({10.0F, 10.0F})) << "seen is not visible";

	const std::uint64_t epoch	 = colony.visibility.epoch();
	const EntityID		colonist = colony.addColonist({0.0F, 0.0F});
	colony.walk(colonist, {50.0F, 30.0F}, 0.05F);
	colony.remove(colonist);
	EXPECT_EQ(colony.visibility.seenArea().ringsRegistered(), 1U);
	EXPECT_EQ(colony.visibility.seenArea().shapesDropped(), 1U);
	EXPECT_EQ(colony.visibility.epoch(), epoch);
}

// With a wall in sight his leg ends, and each polygon vision builds goes in cut to sight round its
// build, grown by the skirt: every point deep in a polygon within the leg radius of its build is
// seen, nothing past the skirt is, and the wall he saw is seen ground, its footprint in once.
// Walking the same way again registers nothing.
TEST(PlayerVisibilitySystem, IndoorPolygonsEnterSkirtedAndTheWallJoins) {
	assets::ConstructionRegistry::Get().clear();
	ASSERT_TRUE(assets::ConstructionRegistry::Get().load((projectRoot() / "assets" / "config" / "construction").string()));
	construction::ConstructionWorld walls;
	buildWall(walls, {0, 0}, {0, 10000});

	Colony colony;
	colony.vision.setConstructionWorld(&walls);
	colony.visibility.setConstructionWorld(&walls);
	const EntityID colonist = colony.addColonist({-60.0F, 5.0F});
	colony.walk(colonist, {-40.0F, 5.0F}, 0.05F);
	EXPECT_EQ(colony.visibility.seenArea().shapesPending(), 1U) << "outdoors: one open leg";

	const std::int64_t				  legMm = std::llround(kSight * 1000.0F) - vision::kSeenSlackMm;
	std::vector<std::vector<Vec2i64>> polygons;
	std::uint64_t					  revision = 0;
	for (int step = 1; step <= 400; ++step) {
		colony.standAt(colonist, {-40.0F + 0.05F * static_cast<float>(step), 5.0F});
		const std::optional<VisionSystem::SightShape> shape = colony.vision.sightShape(colonist);
		ASSERT_TRUE(shape.has_value());
		if (shape->polygon != nullptr && shape->polygonRevision != revision) {
			revision = shape->polygonRevision;
			polygons.push_back(geometry::starClippedToConvex(*shape->polygon, nav::toMm(shape->center), geometry::sightPolygon(nav::toMm(shape->center), legMm)));
		}
	}
	ASSERT_GT(polygons.size(), 10U) << "the wall came into sight";
	EXPECT_EQ(colony.visibility.footprintsEntered(), 1U) << "one wall, one band";
	EXPECT_TRUE(colony.visibility.isVisible({-0.11F, 5.0F})) << "short of the near face";
	EXPECT_FALSE(colony.visibility.isVisible({-0.05F, 5.0F})) << "sight ends at the near face, so the wall's top is out of it";
	colony.remove(colonist);
	const vision::SeenArea& seen = colony.visibility.seenArea();
	ASSERT_GE(seen.ringsRegistered(), 3U) << "the leg, polygons, the wall";
	EXPECT_EQ(seen.shapesSkipped(), 0U);
	EXPECT_FALSE(colony.visibility.isSeen({2.0F, 5.0F})) << "behind the wall";
	EXPECT_TRUE(colony.visibility.isSeen({-1.0F, 5.0F}));
	EXPECT_TRUE(colony.visibility.isSeen({0.0F, 5.0F})) << "inside the wall's body: a seen wall is seen ground";
	EXPECT_TRUE(colony.visibility.isSeen({0.09F, 5.0F})) << "to the far face";
	EXPECT_FALSE(colony.visibility.isSeen({0.3F, 5.0F})) << "past the far face, never in sight";

	std::mt19937 random(23);
	for (const std::vector<Vec2i64>& polygon : polygons) {
		for (int attempt = 0; attempt < 20; ++attempt) {
			const glm::vec2 point{-35.0F + 70.0F * static_cast<float>(random() % 1000) / 1000.0F, -25.0F + 60.0F * static_cast<float>(random() % 1000) / 1000.0F};
			const Vec2i64	millimetres = nav::toMm(point);
			if (geometry::pointInPolygon(millimetres, polygon) == geometry::PointInPolygon::Inside) {
				bool deep = true;
				for (std::size_t i = 0; i < polygon.size() && deep; ++i) {
					deep = !geometry::withinDistanceOfSegment(millimetres, polygon[i], polygon[(i + 1) % polygon.size()], 6);
				}
				EXPECT_TRUE(!deep || colony.visibility.isSeen(point)) << "(" << point.x << ", " << point.y << ") lost";
			}
		}
	}

	const std::size_t ringsBefore = seen.ringsRegistered();
	const EntityID	  again		  = colony.addColonist({-40.0F, 5.0F});
	for (int step = 1; step <= 400; ++step) {
		colony.standAt(again, {-40.0F + 0.05F * static_cast<float>(step), 5.0F});
	}
	colony.remove(again);
	EXPECT_EQ(seen.ringsRegistered(), ringsBefore) << "a re-walk registers nothing";
	EXPECT_EQ(colony.visibility.footprintsEntered(), 1U);
	assets::ConstructionRegistry::Get().clear();
}

// A corner's junction is tagged with its smaller wall's id, but joins the record when either wall is
// seen: here he stands by the larger-id wall with the corner out of his sight, so only that wall is
// seen, and the junction goes in with it.
TEST(PlayerVisibilitySystem, CornerJunctionJoinsWithEitherWall) {
	assets::ConstructionRegistry::Get().clear();
	ASSERT_TRUE(assets::ConstructionRegistry::Get().load((projectRoot() / "assets" / "config" / "construction").string()));
	construction::ConstructionWorld walls;
	buildWall(walls, {0, 0}, {10000, 0});
	buildWall(walls, {10000, 0}, {10000, 60000});
	ASSERT_EQ(walls.segments().size(), 2U);
	construction::SegmentId hiddenWall = construction::kInvalidSegment;
	construction::SegmentId seenWall   = construction::kInvalidSegment;
	for (const construction::WallSegment& segment : walls.segments()) {
		const construction::Vertex* start = walls.getVertex(segment.v0);
		const construction::Vertex* end	  = walls.getVertex(segment.v1);
		if (start->pos.x == end->pos.x) {
			seenWall = segment.id;
		} else {
			hiddenWall = segment.id;
		}
	}
	ASSERT_GT(seenWall, hiddenWall) << "the wall he sees has the larger id";

	Colony colony;
	colony.vision.setConstructionWorld(&walls);
	colony.visibility.setConstructionWorld(&walls);
	const EntityID colonist = colony.addColonist({15.0F, 40.0F});
	colony.tick();
	const std::optional<VisionSystem::SightShape> shape = colony.vision.sightShape(colonist);
	ASSERT_TRUE(shape.has_value());
	ASSERT_NE(shape->segmentsSeen, nullptr);
	ASSERT_EQ(*shape->segmentsSeen, std::vector<construction::SegmentId>{seenWall}) << "only the larger-id wall is in sight";

	colony.remove(colonist);
	EXPECT_EQ(colony.visibility.footprintsEntered(), 2U) << "the seen wall's band and the junction";
	EXPECT_TRUE(colony.visibility.isSeen({9.95F, 0.05F})) << "the junction";
	EXPECT_FALSE(colony.visibility.isSeen({5.0F, 0.0F})) << "the hidden wall's body";
	assets::ConstructionRegistry::Get().clear();
}

// The containments of player-visibility.md 3.1 on a path that curves and wobbles: the disc drawn as
// visible round him lies in seen ground every frame, every seen point lies within sight of some
// position he held, and no point once seen stops being seen.
TEST(PlayerVisibilitySystem, DrawnVisibleInSeenInSweptOnAWobblingPath) {
	Colony					colony;
	glm::vec2				at{0.0F, 0.0F};
	const EntityID			colonist = colony.addColonist(at);
	std::vector<glm::vec2>	held{at};
	std::vector<glm::vec2>	seenPoints;
	std::mt19937			random(17);
	std::uniform_real_distribution<float> unit(-1.0F, 1.0F);
	const float drawnRadius = static_cast<float>(vision::drawnVisibleRadiusMm(std::llround(kSight * 1000.0F))) / 1000.0F;
	ASSERT_GT(drawnRadius, kSight - 0.2F);

	float heading = 0.0F;
	for (int frame = 1; frame <= 1500; ++frame) {
		heading += 0.004F * std::sin(static_cast<float>(frame) * 0.01F);
		const float stride = 0.1F + 0.1F * std::sin(static_cast<float>(frame) * 0.07F) * std::sin(static_cast<float>(frame) * 0.07F);
		const float wobble = 0.02F * std::sin(static_cast<float>(frame) * 0.9F);
		at += stride * glm::vec2{std::cos(heading), std::sin(heading)} + wobble * glm::vec2{-std::sin(heading), std::cos(heading)};
		colony.standAt(colonist, at);
		held.push_back(at);
		if (frame % 10 != 0) {
			continue;
		}
		for (int k = 0; k < 32; ++k) {
			const float angle = 2.0F * std::numbers::pi_v<float> * static_cast<float>(k) / 32.0F;
			const glm::vec2 drawn = at + drawnRadius * glm::vec2{std::cos(angle), std::sin(angle)};
			ASSERT_TRUE(colony.visibility.isSeen(drawn)) << "frame " << frame << ": drawn visible but not seen, direction " << k;
		}
		for (int sample = 0; sample < 16; ++sample) {
			const float		offsetX = unit(random) * 35.0F;
			const float		offsetY = unit(random) * 35.0F;
			const glm::vec2 point	= at + glm::vec2{offsetX, offsetY};
			if (!colony.visibility.isSeen(point)) {
				continue;
			}
			float nearest = std::numeric_limits<float>::max();
			for (const glm::vec2 position : held) {
				nearest = std::min(nearest, glm::distance(position, point));
			}
			ASSERT_LE(nearest, kSight + 0.01F) << "frame " << frame << ": seen but never in sight";
			seenPoints.push_back(point);
		}
		for (const glm::vec2 point : seenPoints) {
			ASSERT_TRUE(colony.visibility.isSeen(point)) << "frame " << frame << ": (" << point.x << ", " << point.y << ") lost";
		}
	}
	colony.remove(colonist);
	EXPECT_GT(colony.visibility.seenArea().ringsRegistered(), 1U) << "the curve ended legs";
	for (const glm::vec2 point : seenPoints) {
		EXPECT_TRUE(colony.visibility.isSeen(point)) << "(" << point.x << ", " << point.y << ") lost once committed";
	}
}

// A jump with walls in sight bridges nothing: the polygon built before it is not used after it, and
// no sweep crosses it.
TEST(PlayerVisibilitySystem, JumpWithWallsInSightDoesNotBridge) {
	assets::ConstructionRegistry::Get().clear();
	ASSERT_TRUE(assets::ConstructionRegistry::Get().load((projectRoot() / "assets" / "config" / "construction").string()));
	construction::ConstructionWorld walls;
	buildWall(walls, {0, 0}, {0, 10000});

	Colony colony;
	colony.vision.setConstructionWorld(&walls);
	colony.visibility.setConstructionWorld(&walls);
	const EntityID colonist = colony.addColonist({-20.0F, -5.0F});
	colony.walk(colonist, {-10.0F, -5.0F}, 0.05F);
	colony.standAt(colonist, {10.0F, -5.0F});
	colony.walk(colonist, {15.0F, -5.0F}, 0.05F);
	colony.remove(colonist);
	EXPECT_FALSE(colony.visibility.isSeen({0.0F, -5.0F - (kSight - 0.3F)})) << "within sight of the jump's line only";
	EXPECT_TRUE(colony.visibility.isSeen({-10.0F, -5.0F - (kSight - 0.1F)}));
	EXPECT_TRUE(colony.visibility.isSeen({10.0F, -5.0F - (kSight - 0.1F)}));
	assets::ConstructionRegistry::Get().clear();
}

// The containments of player-visibility.md 3.1 with walls in sight, walking past and through a
// doorway on a wobbling path: the bright shape the cover draws (vision's polygon cut to the disc of
// the drawn radius round where he stands) lies in seen ground every frame within sight of the
// polygon's build (beyond it, ahead of him, the cover's unseen fill hides it), every seen point lies
// within sight of some position he held (plus the skirt), and no point once seen stops being seen.
// Through the walk's sweeps and pieces, and once all is committed, the seen boxes hold the record.
TEST(PlayerVisibilitySystem, DrawnVisibleInSeenInSweptPastADoorway) {
	assets::ConstructionRegistry::Get().clear();
	ASSERT_TRUE(assets::ConstructionRegistry::Get().load((projectRoot() / "assets" / "config" / "construction").string()));
	construction::ConstructionWorld walls;
	buildWall(walls, {-20000, 0}, {-500, 0});
	buildWall(walls, {500, 0}, {20000, 0});

	Colony				   colony;
	colony.vision.setConstructionWorld(&walls);
	colony.visibility.setConstructionWorld(&walls);
	glm::vec2			   at{-12.0F, -3.0F};
	const EntityID		   colonist = colony.addColonist(at);
	std::vector<glm::vec2> held{at};
	std::vector<glm::vec2> seenPoints;
	std::mt19937		   random(29);
	std::uniform_real_distribution<float> unit(-1.0F, 1.0F);
	int					   drawnChecked = 0;

	for (int frame = 1; frame <= 900; ++frame) {
		const float stride = 0.03F + 0.03F * std::sin(static_cast<float>(frame) * 0.07F) * std::sin(static_cast<float>(frame) * 0.07F);
		const float wobble = 0.02F * std::sin(static_cast<float>(frame) * 0.9F);
		// East along the wall, then north through the door.
		at = frame <= 450 ? glm::vec2{at.x + stride, -3.0F + wobble} : glm::vec2{wobble, at.y + stride};
		if (frame == 451) {
			at = {0.0F, -3.0F};
		}
		colony.standAt(colonist, at);
		held.push_back(at);
		if (frame % 50 == 0) {
			expectSeenBoxesHoldWhatShows(colony.visibility, frame);
		}
		if (frame % 5 != 0) {
			continue;
		}
		const std::optional<VisionSystem::SightShape> shape = colony.vision.sightShape(colonist);
		ASSERT_TRUE(shape.has_value());
		const std::optional<std::vector<Vec2i64>> drawn = colony.visibility.drawnVisible(colonist);
		ASSERT_TRUE(drawn.has_value()) << "frame " << frame;
		drawnChecked += shape->polygon != nullptr ? 1 : 0;
		for (int sample = 0; sample < 24; ++sample) {
			const float	  alongX = unit(random);
			const float	  alongY = unit(random);
			const Vec2i64 point  = nav::toMm(at + glm::vec2{alongX, alongY} * kSight);
			if (geometry::pointInPolygon(point, *drawn) != geometry::PointInPolygon::Inside || !withinSightOfBuild(*shape, point)) {
				continue;
			}
			bool deep = true;
			for (std::size_t i = 0; i < drawn->size() && deep; ++i) {
				deep = !geometry::withinDistanceOfSegment(point, (*drawn)[i], (*drawn)[(i + 1) % drawn->size()], geometry::nav::kSeenCoverToleranceMm + 1);
			}
			ASSERT_TRUE(!deep || colony.visibility.isSeen(nav::toMeters(point))) << "frame " << frame << ": drawn visible but not seen at (" << point.x << ", " << point.y << ")";
		}
		for (int sample = 0; sample < 12; ++sample) {
			const float		offsetX = unit(random);
			const float		offsetY = unit(random);
			const glm::vec2 point	= at + glm::vec2{offsetX, offsetY} * 35.0F;
			if (!colony.visibility.isSeen(point)) {
				continue;
			}
			float nearest = std::numeric_limits<float>::max();
			for (const glm::vec2 position : held) {
				nearest = std::min(nearest, glm::distance(position, point));
			}
			ASSERT_LE(nearest, kSight + 0.06F) << "frame " << frame << ": seen but never in sight";
			seenPoints.push_back(point);
		}
		for (const glm::vec2 point : seenPoints) {
			ASSERT_TRUE(colony.visibility.isSeen(point)) << "frame " << frame << ": (" << point.x << ", " << point.y << ") lost";
		}
	}
	EXPECT_GT(drawnChecked, 100);
	colony.remove(colonist);
	for (const glm::vec2 point : seenPoints) {
		EXPECT_TRUE(colony.visibility.isSeen(point)) << "(" << point.x << ", " << point.y << ") lost once committed";
	}
	EXPECT_GT(colony.visibility.seenArea().sweepsBuilt(), 0U);
	EXPECT_GT(colony.visibility.seenArea().ringsRegistered(), 10U);
	expectSeenBoxesHoldWhatShows(colony.visibility, 901);
	assets::ConstructionRegistry::Get().clear();
}

// On open ground, with nothing in sight, the bright shape is the sight polygon of the drawn radius less
// the rim ease's 400 mm inset round where he stands (sight-softening.md 6.3), to the forest shape's
// few millimetres; a sight too small to hold a leg draws none, since nothing records it.
TEST(PlayerVisibilitySystem, DrawnVisibleOutdoorsIsTheDiscRoundWhereHeStands) {
	Colony		   colony;
	const EntityID colonist = colony.addColonist({0.0F, 0.0F});
	colony.walk(colonist, {4.0F, -2.0F}, 0.05F);
	const std::optional<std::vector<Vec2i64>> drawn = colony.visibility.drawnVisible(colonist);
	ASSERT_TRUE(drawn.has_value());
	const Vec2i64	   standing = nav::toMm({4.0F, -2.0F});
	const std::int64_t radiusMm = vision::drawnVisibleRadiusMm(std::llround(kSight * 1000.0F)) - 400;
	EXPECT_GE(drawn->size(), geometry::kSightSides - 1);
	for (const Vec2i64& vertex : *drawn) {
		EXPECT_TRUE(geometry::withinDistanceOfSegment(vertex, standing, standing, radiusMm + 2));
		EXPECT_FALSE(geometry::withinDistanceOfSegment(vertex, standing, standing, radiusMm - 8));
	}

	const EntityID shortSighted = colony.addColonist({100.0F, 0.0F});
	colony.world.getComponent<Memory>(shortSighted)->sightRadius = 1.0F;
	colony.tick();
	EXPECT_FALSE(colony.visibility.drawnVisible(shortSighted).has_value());
}

// With walls in sight the bright shape is vision's polygon cut to the disc round where he stands and
// the eased shape there, 400 mm inside it, so between vision's rebuilds its rim moves with him; the
// wall's face stays where it is.
TEST(PlayerVisibilitySystem, DrawnVisibleWithWallsInSightMovesWithHim) {
	assets::ConstructionRegistry::Get().clear();
	ASSERT_TRUE(assets::ConstructionRegistry::Get().load((projectRoot() / "assets" / "config" / "construction").string()));
	construction::ConstructionWorld walls;
	buildWall(walls, {3000, -40000}, {3000, 40000});

	Colony		   colony;
	colony.vision.setConstructionWorld(&walls);
	colony.visibility.setConstructionWorld(&walls);
	const EntityID	   colonist = colony.addColonist({0.0F, 0.0F});
	const std::int64_t drawnMm	= vision::drawnVisibleRadiusMm(std::llround(kSight * 1000.0F)) - 400;
	const auto		   byX		= [](const Vec2i64& first, const Vec2i64& second) { return first.x < second.x; };
	colony.tick();
	const std::uint64_t built = colony.vision.polygonBuildCount();
	for (int step = 0; step <= 4; ++step) {
		const float west = 0.1F * static_cast<float>(step);
		colony.standAt(colonist, {-west, 0.0F});
		const std::optional<std::vector<Vec2i64>> drawn = colony.visibility.drawnVisible(colonist);
		ASSERT_TRUE(drawn.has_value());
		EXPECT_NEAR(static_cast<double>(std::min_element(drawn->begin(), drawn->end(), byX)->x), -1000.0 * west - static_cast<double>(drawnMm), 8.0)
			<< "the rim is the disc round where he stands, " << west << " m west";
		EXPECT_LE(std::max_element(drawn->begin(), drawn->end(), byX)->x, 3000) << "the wall bounds it";
		EXPECT_GT(std::max_element(drawn->begin(), drawn->end(), byX)->x, 2800);
	}
	EXPECT_EQ(colony.vision.polygonBuildCount(), built) << "all within one polygon";
	assets::ConstructionRegistry::Get().clear();
}

// Walking toward the end of a wall, the shadow behind it turns as he goes, so ground near the far
// rim past its edge is in sight from some positions only. The record holds every point that was in
// sight, with some margin, from every position over 0.12 m of his path: the sweep leaves no step
// deeper than kSeenSweepSpacingMm, where vision's 0.5 m rebuilds alone left half-metre teeth there.
TEST(PlayerVisibilitySystem, SeenEdgePastAShadowIsSweptNotStepped) {
	assets::ConstructionRegistry::Get().clear();
	ASSERT_TRUE(assets::ConstructionRegistry::Get().load((projectRoot() / "assets" / "config" / "construction").string()));
	construction::ConstructionWorld walls;
	buildWall(walls, {0, 2000}, {0, 20000});

	Colony		   colony;
	colony.vision.setConstructionWorld(&walls);
	colony.visibility.setConstructionWorld(&walls);
	const EntityID colonist = colony.addColonist({-40.0F, 0.0F});
	colony.walk(colonist, {-5.0F, 0.0F}, 0.05F);
	std::vector<geometry::OccluderSegment> occluders;
	colony.vision.geometry().queryOccluders({0, 0}, 100000, occluders);
	ASSERT_FALSE(occluders.empty());
	colony.remove(colonist);
	EXPECT_GT(colony.visibility.seenArea().sweepsBuilt(), 0U);

	static constexpr std::int64_t kPathStepMm = 20;
	static constexpr std::int64_t kWindowMm	  = 120;
	const std::int64_t			  reachMm	  = std::llround(kSight * 1000.0F) - vision::kSeenSlackMm - 150;
	const auto					  inSightFrom = [&](const Vec2i64& from, const Vec2i64& point) {
		 return geometry::hasLineOfSight(from, point, reachMm + kWindowMm, occluders);
	};
	std::mt19937 random(41);
	int			 sweptPoints = 0;
	int			 missing	 = 0;
	for (int sample = 0; sample < 600; ++sample) {
		const std::int64_t x = static_cast<std::int64_t>(random() % 25001);
		const std::int64_t y = static_cast<std::int64_t>(random() % 25001);
		const Vec2i64	   point{x, y};
		bool			   swept = false;
		for (std::int64_t along = -40000 + kWindowMm; along <= -5000 && !swept; along += kPathStepMm) {
			const Vec2i64 at{along, 0};
			if (!geometry::withinDistanceOfSegment(point, at, at, reachMm)) {
				continue;
			}
			swept = true;
			for (std::int64_t back = 0; back <= kWindowMm && swept; back += 10) {
				swept = inSightFrom({along - back, 0}, point);
			}
		}
		if (!swept) {
			continue;
		}
		++sweptPoints;
		if (!colony.visibility.isSeen(nav::toMeters(point))) {
			++missing;
			ADD_FAILURE() << "(" << point.x << ", " << point.y << ") was in sight but is not seen";
		}
	}
	EXPECT_GT(sweptPoints, 50);
	EXPECT_EQ(missing, 0);
	assets::ConstructionRegistry::Get().clear();
}

// Seen never shrinks (rule 16): walking toward the end of a wall, the shadow behind it widens as he
// goes, so ground near the far rim past its edge leaves his sight. Every frame, every point sampled
// near that rim that the record or a pending shape held is still held, and when he stops and is
// removed everything pending is committed.
TEST(PlayerVisibilitySystem, SeenNeverShrinksTowardAWideningShadow) {
	assets::ConstructionRegistry::Get().clear();
	ASSERT_TRUE(assets::ConstructionRegistry::Get().load((projectRoot() / "assets" / "config" / "construction").string()));
	construction::ConstructionWorld walls;
	buildWall(walls, {0, 2000}, {0, 20000});

	Colony		   colony;
	colony.vision.setConstructionWorld(&walls);
	colony.visibility.setConstructionWorld(&walls);
	const EntityID colonist = colony.addColonist({-16.0F, 0.0F});
	colony.tick();

	// Points 0.2 m apart behind the wall, in the band his sight's rim crosses over the walk.
	static constexpr float kStartX = -16.0F;
	static constexpr float kEndX   = -6.0F;
	std::vector<glm::vec2> samples;
	for (float x = 0.3F; x <= kSight; x += 0.2F) {
		for (float y = 2.0F; y <= kSight; y += 0.2F) {
			const float fromStart = glm::distance(glm::vec2{x, y}, glm::vec2{kStartX, 0.0F});
			const float fromEnd	  = glm::distance(glm::vec2{x, y}, glm::vec2{kEndX, 0.0F});
			if (fromEnd < kSight + 0.6F && fromStart > kSight - 0.6F) {
				samples.push_back({x, y});
			}
		}
	}
	ASSERT_GT(samples.size(), 1000U);
	std::vector<bool> held(samples.size(), false);
	int				  everHeld = 0;
	auto			  check	   = [&](const char* when) {
		 for (std::size_t i = 0; i < samples.size(); ++i) {
			 const bool now = colony.visibility.isSeen(samples[i]);
			 if (held[i]) {
				 ASSERT_TRUE(now) << when << ": (" << samples[i].x << ", " << samples[i].y << ") was seen and is not";
			 }
			 everHeld += !held[i] && now ? 1 : 0;
			 held[i] = held[i] || now;
		 }
	};
	for (int step = 1; step <= 200; ++step) {
		colony.standAt(colonist, {kStartX + (kEndX - kStartX) * static_cast<float>(step) / 200.0F, 0.0F});
		check("walking");
	}
	for (int frame = 0; frame < 10; ++frame) {
		colony.tick();
		check("resting");
	}
	colony.remove(colonist);
	check("committed");
	EXPECT_GT(everHeld, 100);
	assets::ConstructionRegistry::Get().clear();
}

namespace {

	// A colony among placed rings with vision every 5th frame, as the game runs it: each frame's time
	// jitters up to 5% either way about 60 frames a second, and the colonist walks 2 m/s with an active
	// MovementTarget, each frame's walk that frame's time at the game's speed.
	struct ForestColony {
		World					world;
		TimeSystem&				time	   = world.registerSystem<TimeSystem>();
		VisionSystem&			vision	   = world.registerSystem<VisionSystem>();
		NavigationSystem&		navigation = world.registerSystem<NavigationSystem>();
		PlayerVisibilitySystem& visibility = world.registerSystem<PlayerVisibilitySystem>();
		geometry::nav::RingId	nextRing   = 1;
		std::mt19937			jitter{3};

		// A square ring `sideMeters` wide round `centre`, arrived long ago.
		void place(glm::vec2 centre, float sideMeters) {
			const Vec2i64	   middle = nav::toMm(centre);
			const std::int64_t half	  = std::llround(sideMeters * 500.0F);
			navigation.placedOccluders().add(
				nextRing++,
				{middle + Vec2i64{-half, -half}, middle + Vec2i64{half, -half}, middle + Vec2i64{half, half}, middle + Vec2i64{-half, half}},
				"Test_Trunk",
				-1000000000
			);
		}

		// Trunks `sideMeters` wide at random, one to `spacingMeters` squared over the box, none within 1.5 m
		// of the line y = 0.
		void plant(glm::vec2 low, glm::vec2 high, float spacingMeters, float sideMeters, std::uint32_t seed) {
			std::mt19937						  random(seed);
			std::uniform_real_distribution<float> across(low.x, high.x);
			std::uniform_real_distribution<float> along(low.y, high.y);
			const int count = static_cast<int>((high.x - low.x) * (high.y - low.y) / (spacingMeters * spacingMeters));
			for (int planted = 0; planted < count;) {
				const glm::vec2 at{across(random), along(random)};
				if (std::abs(at.y) > 1.5F) {
					place(at, sideMeters);
					++planted;
				}
			}
		}

		EntityID addColonist(glm::vec2 at) {
			const EntityID colonist = world.createEntity();
			world.addComponent<Position>(colonist, Position{at});
			world.addComponent<Colonist>(colonist, Colonist{"Test"});
			world.addComponent<Memory>(colonist, Memory{});
			world.getComponent<Memory>(colonist)->owner = colonist;
			world.addComponent<MovementTarget>(colonist, MovementTarget{.target = at + glm::vec2{1000.0F, 0.0F}, .speed = 2.0F, .active = true});
			return colonist;
		}

		// One frame: he walks toward `heading` that frame's walk, with `sideways` as his offset from the
		// line y = 0. His walk in millimetres.
		double step(EntityID colonist, float sideways) {
			const float		frameSeconds = (1.0F / 60.0F) * std::uniform_real_distribution<float>(0.95F, 1.05F)(jitter);
			glm::vec2&		at			 = world.getComponent<Position>(colonist)->value;
			const glm::vec2 before		 = at;
			at							 = {at.x + 2.0F * frameSeconds * time.effectiveTimeScale(), sideways};
			world.update(frameSeconds);
			return static_cast<double>(glm::distance(before, at)) * 1000.0;
		}

		void remove(EntityID colonist) {
			world.destroyEntity(colonist);
			world.update(1.0F / 60.0F);
			for (int frame = 0; frame < 5000 && visibility.seenArea().shapesPending() > 0; ++frame) {
				world.update(1.0F / 60.0F);
			}
			ASSERT_EQ(visibility.seenArea().shapesPending(), 0U);
		}
	};

	// A shape's support along each side's normal from `centre`, in millimetres.
	std::array<double, geometry::kSightSides> supportsOf(const std::vector<Vec2i64>& shape, const Vec2i64& centre) {
		std::array<double, geometry::kSightSides> supports{};
		for (std::size_t side = 0; side < geometry::kSightSides; ++side) {
			double most = -1e300;
			for (const Vec2i64& vertex : shape) {
				most = std::max(
					most,
					(static_cast<double>(geometry::kSightSideNormals[side][0]) * static_cast<double>(vertex.x - centre.x) +
					 static_cast<double>(geometry::kSightSideNormals[side][1]) * static_cast<double>(vertex.y - centre.y)) /
						static_cast<double>(geometry::kSightSideNormalScale)
				);
			}
			supports[side] = most;
		}
		return supports;
	}

	double distanceOutside(const Vec2i64& point, const std::vector<Vec2i64>& ring) {
		if (geometry::pointInPolygon(point, ring) != geometry::PointInPolygon::Outside) {
			return 0.0;
		}
		double nearest = 1e300;
		for (std::size_t i = 0; i < ring.size(); ++i) {
			nearest = std::min(nearest, geometry::distanceToSegment(point, ring[i], ring[(i + 1) % ring.size()]));
		}
		return nearest;
	}

} // namespace

// Walking a stand with a sideways wobble at 1x and at 10x (sight-softening.md 6.2, 6.3): legs end by
// the spread and by turns; every frame the bright shape is in the record or a pending shape; the eased
// rim moves no side more in a frame than his walk over rimEaseMm of the ease's span, and nothing on the
// frame a build restarts the ease; and once he is gone no seen point lies farther from a shape a leg
// held (its build's ranges at the slack radius, where he stood) than 6.2's bound in the landing's
// forest, 1.16 m at 1x and 3.6 m at 10x as a distance.
class ForestLegs : public ::testing::TestWithParam<GameSpeed> {};

TEST_P(ForestLegs, BrightShapeSeenEaseBoundedAndRecordNearTheShapesHeld) {
	ForestColony colony;
	colony.time.setSpeed(GetParam());
	colony.plant({-40.0F, -40.0F}, {80.0F, 40.0F}, 4.0F, 0.65F, 5);
	const EntityID	   colonist = colony.addColonist({0.0F, 0.0F});
	const std::int64_t sightMm	= std::llround(kSight * 1000.0F);
	const std::int64_t drawnMm	= vision::drawnVisibleRadiusMm(sightMm) - 400;

	std::vector<std::vector<Vec2i64>>		  held;
	Vec2i64									  lastHeld{-1000000, 0};
	std::uint64_t							  lastBuild = 0;
	std::array<double, geometry::kSightSides> previous{};
	std::array<double, geometry::kSightSides> start{};
	std::array<double, geometry::kSightSides> newest{};
	bool									  drawnBefore = false;
	int										  frames	  = 0;
	Vec2i64									  before	  = nav::toMm(colony.world.getComponent<Position>(colonist)->value);
	while (colony.world.getComponent<Position>(colonist)->value.x < 40.0F) {
		const float x = colony.world.getComponent<Position>(colonist)->value.x;
		colony.step(colonist, 0.3F * std::sin(0.8F * x));
		++frames;
		const Vec2i64 at	 = nav::toMm(colony.world.getComponent<Position>(colonist)->value);
		const double  walked = std::ceil(std::sqrt(geometry::dot(at - before, at - before).toDouble()));
		before				 = at;
		const std::optional<VisionSystem::SightShape> shape = colony.vision.sightShape(colonist);
		ASSERT_TRUE(shape.has_value());
		ASSERT_EQ(shape->polygon, nullptr) << "no opaque body: off the polygon path";
		vision::SightRanges legRanges{};
		for (std::size_t side = 0; side < geometry::kSightSides; ++side) {
			legRanges[side] = (*shape->ranges)[side] - vision::kSeenSlackMm;
		}
		if (shape->buildRevision != lastBuild || std::abs(at.x - lastHeld.x) >= 100) {
			held.push_back(geometry::forestShape(at, legRanges));
			lastHeld = at;
		}

		const std::optional<std::vector<Vec2i64>> drawn = colony.visibility.drawnVisible(colonist);
		ASSERT_TRUE(drawn.has_value());
		for (std::size_t k = 0; k < drawn->size(); ++k) {
			const Vec2i64& vertex = (*drawn)[k];
			const Vec2i64& next	  = (*drawn)[(k + 1) % drawn->size()];
			ASSERT_TRUE(colony.visibility.seenArea().isSeen(vertex)) << "frame " << frames << ": a drawn vertex not seen";
			ASSERT_TRUE(colony.visibility.seenArea().isSeen({(vertex.x + next.x) / 2, (vertex.y + next.y) / 2})) << "frame " << frames;
		}

		const std::array<double, geometry::kSightSides> now = supportsOf(*drawn, at);
		if (shape->buildRevision != lastBuild) {
			// The newest build's offsets at the drawn radius, each the support of its forest shape (6.3).
			vision::SightRanges drawnRanges{};
			for (std::size_t side = 0; side < geometry::kSightSides; ++side) {
				drawnRanges[side] = (*shape->ranges)[side] - (sightMm - drawnMm);
			}
			newest = supportsOf(geometry::forestShape({0, 0}, drawnRanges), {0, 0});
			for (std::size_t side = 0; drawnBefore && side < geometry::kSightSides; ++side) {
				EXPECT_NEAR(now[side], previous[side], 4.0) << "frame " << frames << ": a restart moves nothing, side " << side;
			}
			start	  = now;
			lastBuild = shape->buildRevision;
		} else {
			for (std::size_t side = 0; side < geometry::kSightSides; ++side) {
				const double span = std::abs(newest[side] - start[side]);
				EXPECT_LE(std::abs(now[side] - previous[side]), walked / 400.0 * span + 4.0) << "frame " << frames << ", side " << side;
			}
		}
		previous	= now;
		drawnBefore = true;
	}
	EXPECT_GT(colony.visibility.legsOpenedCount(), 2U) << "legs ended on the walk";
	colony.remove(colonist);

	// Seen points away from the walk, where the record's edge is, against every shape a leg held.
	const double boundMm  = GetParam() == GameSpeed::VeryFast ? 3600.0 : 1160.0;
	double		 farthest = 0.0;
	int			 tested	  = 0;
	for (float x = -35.0F; x <= 75.0F; x += 1.0F) {
		for (float y = -35.0F; y <= 35.0F; y += 1.0F) {
			const float fromWalk = x < 0.0F ? std::hypot(x, y) : (x > 40.0F ? std::hypot(x - 40.0F, y) : std::abs(y));
			if (fromWalk < 18.0F || !colony.visibility.isSeen({x, y})) {
				continue;
			}
			++tested;
			const Vec2i64 point	  = nav::toMm({x, y});
			double		  nearest = 1e300;
			for (const std::vector<Vec2i64>& shape : held) {
				nearest = std::min(nearest, distanceOutside(point, shape));
				if (nearest == 0.0) {
					break;
				}
			}
			farthest = std::max(farthest, nearest);
			EXPECT_LE(nearest, boundMm) << "(" << x << ", " << y << ") seen " << nearest << " mm past every shape a leg held";
		}
	}
	EXPECT_GT(tested, 1000);
	std::printf("[ ForestLegs ] %d legs, the record at most %.0f mm past the shapes held\n", static_cast<int>(colony.visibility.legsOpenedCount()), farthest);
}

INSTANTIATE_TEST_SUITE_P(OneAndTenTimes, ForestLegs, ::testing::Values(GameSpeed::Normal, GameSpeed::VeryFast), [](const ::testing::TestParamInfo<GameSpeed>& info) {
	return std::string(info.param == GameSpeed::VeryFast ? "TenTimes" : "OneTimes");
});

// The spread past softLegToleranceMm ends a leg, the tolerance scaled by the distance between builds
// at the game's speed: 500 mm at 1x, 1,667 mm at 10x (sight-softening.md 6.6). Walking straight past a
// clump, a leg whose ranges spread 600 to 1,600 mm ends at 1x and not at 10x; past a denser clump,
// spreading more than 1,700 mm, it ends at 10x too.
TEST(PlayerVisibilitySystem, TheSpreadEndsALegByTheGameSpeedsTolerance) {
	EXPECT_EQ(PlayerVisibilitySystem::legToleranceMm(500, 2000.0 * 5.0 / 60.0, 1.0), 500);
	EXPECT_EQ(PlayerVisibilitySystem::legToleranceMm(500, 2000.0 * 5.0 / 60.0, 10.0), 1667);
	EXPECT_EQ(PlayerVisibilitySystem::legToleranceMm(500, 0.0, 10.0), 500) << "standing still";

	// The most any side's leg range spread over the walk, and the legs he opened.
	const auto walkPast = [](GameSpeed speed, int trunks) {
		ForestColony colony;
		colony.time.setSpeed(speed);
		for (int trunk = 0; trunk < trunks; ++trunk) {
			colony.place({9.0F + 0.9F * static_cast<float>(trunk % 4), 6.0F + 0.9F * static_cast<float>(trunk / 4)}, 0.6F);
		}
		const EntityID		colonist = colony.addColonist({0.0F, 0.0F});
		vision::SightRanges least{};
		vision::SightRanges most{};
		least.fill(std::numeric_limits<std::int64_t>::max());
		most.fill(0);
		while (colony.world.getComponent<Position>(colonist)->value.x < 8.0F) {
			colony.step(colonist, 0.0F);
			const std::optional<VisionSystem::SightShape> shape = colony.vision.sightShape(colonist);
			for (std::size_t side = 0; side < geometry::kSightSides; ++side) {
				least[side] = std::min(least[side], (*shape->ranges)[side]);
				most[side]	= std::max(most[side], (*shape->ranges)[side]);
			}
		}
		std::int64_t spread = 0;
		for (std::size_t side = 0; side < geometry::kSightSides; ++side) {
			spread = std::max(spread, most[side] - least[side]);
		}
		return std::pair{spread, colony.visibility.legsOpenedCount()};
	};
	const auto [spreadAtOne, legsAtOne] = walkPast(GameSpeed::Normal, 2);
	const auto [spreadAtTen, legsAtTen] = walkPast(GameSpeed::VeryFast, 2);
	ASSERT_GT(spreadAtOne, 600);
	ASSERT_LT(spreadAtTen, 1600);
	EXPECT_GT(legsAtOne, 1U) << "a " << spreadAtOne << " mm spread ends the leg at 1x";
	EXPECT_EQ(legsAtTen, 1U) << "a " << spreadAtTen << " mm spread does not at 10x";
	const auto [spreadDense, legsDense] = walkPast(GameSpeed::VeryFast, 8);
	ASSERT_GT(spreadDense, 1700);
	EXPECT_GT(legsDense, 1U) << "a " << spreadDense << " mm spread ends it at 10x";
}

// No sweep where every opaque body in reach is placed (sight-softening.md 6.6): walking past a rock
// on the polygon path queues no sweep point; with an opaque wall in reach as well, the walk is swept.
TEST(PlayerVisibilitySystem, OnlyAnOpaqueWallInReachSweepsThePolygonPath) {
	assets::ConstructionRegistry::Get().clear();
	ASSERT_TRUE(assets::ConstructionRegistry::Get().load((projectRoot() / "assets" / "config" / "construction").string()));
	const auto sweptWalkingPast = [](bool wall) {
		construction::ConstructionWorld walls;
		if (wall) {
			buildWall(walls, {20000, 2000}, {20000, 12000});
		}
		ForestColony colony;
		colony.vision.setConstructionWorld(&walls);
		colony.visibility.setConstructionWorld(&walls);
		colony.place({10.0F, 4.0F}, 1.6F);
		const EntityID colonist = colony.addColonist({0.0F, 0.0F});
		int			   polygonFrames = 0;
		while (colony.world.getComponent<Position>(colonist)->value.x < 8.0F) {
			colony.step(colonist, 0.0F);
			polygonFrames += colony.vision.sightShape(colonist)->polygon != nullptr ? 1 : 0;
		}
		EXPECT_GT(polygonFrames, 100) << "the rock puts him on the polygon path";
		colony.remove(colonist);
		return colony.visibility.seenArea().sweepsBuilt();
	};
	EXPECT_EQ(sweptWalkingPast(false), 0U) << "a placed rock alone";
	EXPECT_GT(sweptWalkingPast(true), 0U) << "a wall in reach";
	assets::ConstructionRegistry::Get().clear();
}
