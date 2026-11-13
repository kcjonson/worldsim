#include "PlayerVisibilitySystem.h"

#include "VisionSystem.h"

#include "../World.h"
#include "../components/Colonist.h"
#include "../components/Memory.h"
#include "../components/Transform.h"

#include "nav/NavCoords.h"

#include <assets/ConstructionRegistry.h>
#include <construction/ConstructionWorld.h>
#include <predicates/Predicates.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <numbers>
#include <random>
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

		// Removes him, which ends his leg, and lets the budget commit everything pending.
		void remove(EntityID colonist) {
			world.destroyEntity(colonist);
			for (int frame = 0; frame < 100 && visibility.seenArea().shapesPending() > 0; ++frame) {
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
		const construction::SegmentCommitResult result = walls.commitSegment(from, to, "Wood", "Standard", construction::kInvalidFoundation);
		ASSERT_TRUE(result.ok());
		for (const construction::SegmentId segment : result.createdSegments) {
			walls.setSegmentState(segment, construction::FoundationState::Built);
		}
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

// With a wall in sight each new visibility polygon is queued, round where vision built it, and the
// leg he walked up to it ends. What goes in is the polygon's frontier grown by the skirt: every
// point deep in a polygon is seen, nothing past the skirt is, and the wall he saw is seen ground,
// its footprint in once. Walking the same way again registers nothing.
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

	std::vector<std::vector<Vec2i64>> polygons;
	std::uint64_t					  revision = 0;
	for (int step = 1; step <= 400; ++step) {
		colony.standAt(colonist, {-40.0F + 0.05F * static_cast<float>(step), 5.0F});
		const std::optional<VisionSystem::SightShape> shape = colony.vision.sightShape(colonist);
		ASSERT_TRUE(shape.has_value());
		if (shape->polygon != nullptr && shape->polygonRevision != revision) {
			revision = shape->polygonRevision;
			polygons.push_back(*shape->polygon);
		}
	}
	ASSERT_GT(polygons.size(), 10U) << "the wall came into sight";
	EXPECT_EQ(colony.visibility.footprintsEntered(), 1U) << "one wall, one band";
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
