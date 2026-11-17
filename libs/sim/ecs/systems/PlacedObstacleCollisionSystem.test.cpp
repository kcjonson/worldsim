#include "PlacedObstacleCollisionSystem.h"

#include "NavigationSystem.h"
#include "PlacedNothing.test.h"

#include "../World.h"
#include "../components/AgentRadius.h"
#include "../components/Colonist.h"
#include "../components/Transform.h"

#include <assets/AssetDefinition.h>
#include <assets/AssetRegistry.h>
#include <assets/placement/InstanceTransform.h>
#include <assets/placement/PlacementExecutor.h>
#include <assets/placement/SpatialIndex.h>

#include <nav/NavCoords.h>
#include <nav/NavInputBuilder.h>
#include <nav/PlacedObstacle.h>

#include <predicates/Predicates.h>

#include <world/chunk/ChunkCoordinate.h>
#include <world/chunk/ChunkManager.h>
#include <world/chunk/SyntheticWorldSampler.h>

#include <glm/geometric.hpp>
#include <glm/vec2.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

using namespace ecs;
using assets::AssetDefinition;
using assets::AssetRegistry;
using assets::CollisionShapeType;
using assets::PlacedEntity;
using assets::PlacementExecutor;
using assets::SpatialIndex;

namespace {

	// The pad nav puts round a Rect collider, in metres.
	constexpr float kPadMeters = static_cast<float>(nav::kPlacedObstaclePadMm) / 1000.0F;

	geometry::Ring ringMm(std::initializer_list<glm::vec2> points) {
		geometry::Ring ring;
		for (const glm::vec2 point : points) {
			ring.push_back(nav::toMm(point));
		}
		return ring;
	}

	// The axis-aligned box about `center`, corners in the order nav builds a rect's.
	geometry::Ring boxMm(glm::vec2 center, glm::vec2 half) {
		return ringMm({center + glm::vec2{-half.x, -half.y}, center + glm::vec2{half.x, -half.y}, center + glm::vec2{half.x, half.y},
					   center + glm::vec2{-half.x, half.y}});
	}

	// How far `point` is from the ring's boundary, in mm.
	double distanceToRingMm(glm::vec2 point, const geometry::Ring& ring) {
		double nearest = std::numeric_limits<double>::max();
		for (std::size_t i = 0; i < ring.size(); ++i) {
			nearest = std::min(nearest, geometry::distanceToSegment(nav::toMm(point), ring[i], ring[(i + 1) % ring.size()]));
		}
		return nearest;
	}

	// A U, 4 m by 2 m about its pivot, notched 1 m wide and 1 m deep from its top edge: concave, with
	// two reflex corners at (+-0.5, 0).
	const std::vector<glm::vec2> kNotchedPoints{{-2.0F, -1.0F}, {2.0F, -1.0F}, {2.0F, 1.0F}, {0.5F, 1.0F},
												{0.5F, 0.0F},	{-0.5F, 0.0F}, {-0.5F, 1.0F}, {-2.0F, 1.0F}};

	// A simple (SVG) obstacle 6 m by 1 m, pivot at its centre, whose 0.2 m square collider is at its
	// east end, kFarColliderOffset from the pivot: past the 2 m the query used to reach.
	constexpr const char* kFarColliderDefName = "Test_FarCollider";
	constexpr glm::vec2	  kFarColliderOffset{2.7F, 0.0F};
	constexpr float		  kFarColliderHalfExtent = 0.1F;

	// Loads the def from a temporary folder and returns its template id.
	std::uint32_t loadFarColliderDef(AssetRegistry& registry) {
		static std::atomic<int>		counter{0};
		const std::filesystem::path root =
			std::filesystem::temp_directory_path() /
			("placed_obstacle_collision_test_" + std::to_string(reinterpret_cast<std::uintptr_t>(&counter)) + "_" + std::to_string(counter++));
		const std::filesystem::path folder = root / kFarColliderDefName;
		std::filesystem::create_directories(folder);
		std::ofstream(folder / "shape.svg") << "<?xml version=\"1.0\"?>"
											   "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 600 100\" width=\"600\" height=\"100\">"
											   "<metadata><collision><shape type=\"aabb\" min=\"560,40\" max=\"580,60\"/></collision></metadata>"
											   "<path d=\"M0,0 L600,0 L600,100 L0,100 Z\" fill=\"#777777\"/>"
											   "</svg>";
		std::ofstream(folder / (std::string(kFarColliderDefName) + ".xml")) << "<?xml version=\"1.0\"?><AssetDefinitions><AssetDef>"
																			   "<defName>"
																			<< kFarColliderDefName
																			<< "</defName>"
																			   "<assetType>simple</assetType>"
																			   "<svgPath>shape.svg</svgPath>"
																			   "<worldHeight>1.0</worldHeight>"
																			   "</AssetDef></AssetDefinitions>";
		registry.loadDefinitionsFromFolder(root.string());
		const std::uint32_t templateId = registry.templateId(kFarColliderDefName, {});
		std::error_code		ignored;
		std::filesystem::remove_all(root, ignored);
		return templateId;
	}

} // namespace

// --- Pure helper: resolveCenterAgainstOutline -------------------------------

// A center well outside the outline is not touched.
TEST(PlacedObstacleResolveTest, OutsideReturnsNullopt) {
	EXPECT_FALSE(resolveCenterAgainstOutline({5.0F, 5.0F}, boxMm({0.0F, 0.0F}, {0.35F, 0.35F})).has_value());
}

// A center just inside a box's +X face goes to the face, y unchanged.
TEST(PlacedObstacleResolveTest, JustInsidePlusXPushedToBoundary) {
	const std::optional<glm::vec2> corrected = resolveCenterAgainstOutline({0.34F, 0.0F}, boxMm({0.0F, 0.0F}, {0.35F, 0.35F}));
	ASSERT_TRUE(corrected.has_value());
	EXPECT_FLOAT_EQ(corrected->x, 0.35F);
	EXPECT_FLOAT_EQ(corrected->y, 0.0F);
}

// A center deep inside a box goes out the NEAREST face, along it only.
TEST(PlacedObstacleResolveTest, DeepInsidePushedNearestFace) {
	const std::optional<glm::vec2> corrected = resolveCenterAgainstOutline({0.2F, 0.05F}, boxMm({0.0F, 0.0F}, {0.35F, 0.35F}));
	ASSERT_TRUE(corrected.has_value());
	EXPECT_FLOAT_EQ(corrected->x, 0.35F);
	EXPECT_FLOAT_EQ(corrected->y, 0.05F);
}

// Inside a convex hexagon near a slanted edge, the center goes onto that edge along its normal: on the
// boundary, the move square to the edge.
TEST(PlacedObstacleResolveTest, ConvexPolygonPushesAlongTheEdgeNormal) {
	const glm::vec2		 a{1.0F, 0.0F};
	const glm::vec2		 b{0.5F, 0.866F};
	const geometry::Ring hexagon = ringMm({a, b, {-0.5F, 0.866F}, {-1.0F, 0.0F}, {-0.5F, -0.866F}, {0.5F, -0.866F}});
	const glm::vec2		 start	 = (a + b) * 0.5F - glm::vec2{0.866F, 0.5F} * 0.1F; // 0.1 m in from the edge's middle
	const std::optional<glm::vec2> corrected = resolveCenterAgainstOutline(start, hexagon);
	ASSERT_TRUE(corrected.has_value());
	EXPECT_LE(distanceToRingMm(*corrected, hexagon), 1.0);
	const glm::vec2 move = *corrected - start;
	EXPECT_NEAR(glm::length(move), 0.1F, 1e-3F);
	EXPECT_NEAR(glm::dot(move, glm::normalize(b - a)), 0.0F, 1e-3F);
}

// Inside a concave U's arm beside the notch, the nearest way out is INTO the notch, not out the arm's
// far side; beside a reflex corner it is the corner itself, equally near both its edges, and still a
// finite point.
TEST(PlacedObstacleResolveTest, ConcaveNotchPushesIntoTheNotchAndOntoTheCorner) {
	geometry::Ring notched;
	for (const glm::vec2 point : kNotchedPoints) {
		notched.push_back(nav::toMm(point));
	}

	const std::optional<glm::vec2> beside = resolveCenterAgainstOutline({0.6F, 0.05F}, notched);
	ASSERT_TRUE(beside.has_value());
	EXPECT_FLOAT_EQ(beside->x, 0.5F);
	EXPECT_FLOAT_EQ(beside->y, 0.05F);

	const std::optional<glm::vec2> nearCorner = resolveCenterAgainstOutline({0.53F, -0.03F}, notched);
	ASSERT_TRUE(nearCorner.has_value());
	EXPECT_FLOAT_EQ(nearCorner->x, 0.5F);
	EXPECT_FLOAT_EQ(nearCorner->y, 0.0F);
}

// A center exactly on an edge, or on a corner, is not inside: no push and no NaN.
TEST(PlacedObstacleResolveTest, CenterOnTheBoundaryIsLeftAlone) {
	const geometry::Ring box = boxMm({0.0F, 0.0F}, {0.35F, 0.35F});
	EXPECT_FALSE(resolveCenterAgainstOutline({0.35F, 0.1F}, box).has_value());
	EXPECT_FALSE(resolveCenterAgainstOutline({0.35F, 0.35F}, box).has_value());
}

// A center exactly at a shape's centroid, every edge of a wide box or a square equally near on some
// axis, resolves deterministically onto the boundary at the least depth: the first of the nearest
// edges in ring order.
TEST(PlacedObstacleResolveTest, CenterAtTheCentroidResolvesDeterministically) {
	const geometry::Ring		   wide		 = boxMm({0.0F, 0.0F}, {0.45F, 0.25F});
	const std::optional<glm::vec2> corrected = resolveCenterAgainstOutline({0.0F, 0.0F}, wide);
	ASSERT_TRUE(corrected.has_value());
	EXPECT_FLOAT_EQ(corrected->x, 0.0F);
	EXPECT_FLOAT_EQ(std::abs(corrected->y), 0.25F);
	EXPECT_EQ(resolveCenterAgainstOutline({0.0F, 0.0F}, wide), corrected);

	const std::optional<glm::vec2> square = resolveCenterAgainstOutline({0.0F, 0.0F}, boxMm({0.0F, 0.0F}, {0.3F, 0.3F}));
	ASSERT_TRUE(square.has_value());
	EXPECT_NEAR(glm::length(*square), 0.3F, 1e-6F);
}

// --- System integration ----------------------------------------------------

namespace {

	class PlacedObstacleCollisionSystemTest : public ::testing::Test {
	  protected:
		void TearDown() override { AssetRegistry::Get().clearDefinitions(); }

		EntityID spawnAgent(World& world, glm::vec2 pos, float radius = 0.3F) {
			EntityID e = world.createEntity();
			world.addComponent<Position>(e, Position{pos});
			world.addComponent<AgentRadius>(e, AgentRadius{radius, 1.0F});
			return e;
		}

		// Chunk (0, 0) and the eight round it, all land, generated once for every test: a mover's
		// limit near the origin reaches into the chunks west and south of it.
		static world::ChunkManager* landChunk() {
			static const std::unique_ptr<world::ChunkManager> chunks = [] {
				auto c = std::make_unique<world::ChunkManager>(std::make_unique<world::SyntheticWorldSampler>(7, world::SyntheticMap{}));
				c->setLoadRadius(1);
				c->setUnloadRadius(2);
				c->update({256.0F, 256.0F});
				c->finishPendingGeneration();
				return c;
			}();
			return chunks.get();
		}

		// A NavigationSystem over `executor`'s flora, settled, so the trees round the world's movers
		// are in the nav mesh. The collision system pushes only out of those. It is not ticked, so the
		// forward snap has not moved anyone and each test sees the push alone.
		static PlacedObstacleCollisionSystem& wire(World& world, PlacementExecutor& executor) {
			auto& nav = world.registerSystem<NavigationSystem>();
			nav.setChunkManager(landChunk());
			placeNothingElsewhere(executor, nullptr);
			nav.setPlacementData(&executor, nullptr);
			nav.finishPendingBuilds();
			auto& sys = world.registerSystem<PlacedObstacleCollisionSystem>();
			sys.setPlacementData(&executor);
			sys.setDetailSource(&nav);
			return sys;
		}
	};

} // namespace

// update() with no placement wired is a safe no-op.
TEST_F(PlacedObstacleCollisionSystemTest, UnwiredUpdateIsNoOp) {
	World world;
	const glm::vec2 start{1.0F, 1.0F};
	EntityID		agent = spawnAgent(world, start);

	auto& sys = world.registerSystem<PlacedObstacleCollisionSystem>();
	// Deliberately wire neither placement nor detail.
	sys.update(0.0F);

	auto* pos = world.getComponent<Position>(agent);
	ASSERT_NE(pos, nullptr);
	EXPECT_FLOAT_EQ(pos->value.x, start.x);
	EXPECT_FLOAT_EQ(pos->value.y, start.y);
}

// A blocking flora rect: an agent whose CENTER is inside it is pushed out past the
// 0.05 m inflated boundary; an agent far away is untouched.
TEST_F(PlacedObstacleCollisionSystemTest, AgentInsideRectIsPushedOut) {
	AssetRegistry&	reg = AssetRegistry::Get();
	AssetDefinition tree;
	tree.defName					 = "Test_PlacedRectTree";
	tree.setCollision(0, {.type = CollisionShapeType::Rect, .halfExtentsMeters = {0.3F, 0.3F}});
	reg.registerTestDefinition(tree);

	PlacementExecutor executor(reg);

	const glm::vec2					   treeAt{20.0F, 20.0F};
	const world::ChunkCoordinate coord = world::worldToChunk({treeAt.x, treeAt.y});
	SpatialIndex					   index;
	PlacedEntity					   pe;
	pe.defName	= "Test_PlacedRectTree";
	pe.position = treeAt;
	index.insert(pe);
	assets::AsyncChunkPlacementResult result;
	result.coord		= coord;
	result.spatialIndex = std::move(index);
	executor.storeChunkResult(std::move(result));

	World world;
	// Agent center just inside the rect (offset slightly off-axis so it's clearly
	// interior, not on a face).
	const glm::vec2 inside{treeAt.x + 0.1F, treeAt.y + 0.05F};
	EntityID		near = spawnAgent(world, inside);
	const glm::vec2 farStart{5.0F, 5.0F};
	EntityID		far = spawnAgent(world, farStart);

	wire(world, executor).update(0.0F);

	// Near agent: now outside the rect inflated by clearance. The rect is axis
	// aligned, so check the agent escaped the inflated AABB.
	auto* nearPos = world.getComponent<Position>(near);
	ASSERT_NE(nearPos, nullptr);
	const float ex	  = tree.collisionFor(0).halfExtentsMeters.x + kPadMeters;
	const float ey	  = tree.collisionFor(0).halfExtentsMeters.y + kPadMeters;
	const float dx	  = std::abs(nearPos->value.x - treeAt.x);
	const float dy	  = std::abs(nearPos->value.y - treeAt.y);
	EXPECT_TRUE(dx >= ex - 1e-4F || dy >= ey - 1e-4F) << "agent center still inside inflated rect";

	// Far agent: untouched.
	auto* farPos = world.getComponent<Position>(far);
	ASSERT_NE(farPos, nullptr);
	EXPECT_FLOAT_EQ(farPos->value.x, farStart.x);
	EXPECT_FLOAT_EQ(farPos->value.y, farStart.y);
}

// The rect an agent hits is the one of the entity's age class: an agent 0.2 m off the trunk is
// clear of a sapling's rect (half-extent 0.1 m) and inside a mature tree's (0.3 m).
TEST_F(PlacedObstacleCollisionSystemTest, RectFollowsAgeClass) {
	AssetRegistry&	reg = AssetRegistry::Get();
	AssetDefinition tree;
	tree.defName	= "Test_PlacedRectAged";
	tree.ageClasses = {{.name = "sapling", .weight = 0.5F, .variants = 1}, {.name = "mature", .weight = 0.5F, .variants = 1}};
	tree.setCollision(0, {.type = CollisionShapeType::Rect, .halfExtentsMeters = {0.1F, 0.1F}});
	tree.setCollision(1, {.type = CollisionShapeType::Rect, .halfExtentsMeters = {0.3F, 0.3F}});
	reg.registerTestDefinition(tree);

	for (const uint8_t ageClass : {uint8_t{0}, uint8_t{1}}) {
		PlacementExecutor executor(reg);
		const glm::vec2					   treeAt{20.0F, 20.0F};
		const world::ChunkCoordinate coord = world::worldToChunk({treeAt.x, treeAt.y});
		SpatialIndex					   index;
		PlacedEntity					   pe;
		pe.defName		= "Test_PlacedRectAged";
		pe.position		= treeAt;
		pe.traits.ageClass = ageClass;
		index.insert(pe);
		assets::AsyncChunkPlacementResult result;
		result.coord		= coord;
		result.spatialIndex = std::move(index);
		executor.storeChunkResult(std::move(result));

		World			world;
		const glm::vec2 start{treeAt.x + 0.2F, treeAt.y};
		EntityID		agent = spawnAgent(world, start);
		wire(world, executor).update(0.0F);

		const glm::vec2 end = world.getComponent<Position>(agent)->value;
		if (ageClass == 0) {
			EXPECT_FLOAT_EQ(end.x, start.x);
		} else {
			EXPECT_GT(end.x, start.x);
		}
	}
}

// Same setup run twice yields an identical corrected position (determinism).
TEST_F(PlacedObstacleCollisionSystemTest, DeterministicCorrection) {
	AssetRegistry&	reg = AssetRegistry::Get();
	AssetDefinition tree;
	tree.defName					 = "Test_PlacedRectTreeDet";
	tree.setCollision(0, {.type = CollisionShapeType::Rect, .halfExtentsMeters = {0.3F, 0.3F}});
	reg.registerTestDefinition(tree);

	auto buildExecutor = [&](PlacementExecutor& executor) {
		const glm::vec2					   treeAt{20.0F, 20.0F};
		const world::ChunkCoordinate coord = world::worldToChunk({treeAt.x, treeAt.y});
		SpatialIndex					   index;
		PlacedEntity					   pe;
		pe.defName	= "Test_PlacedRectTreeDet";
		pe.position = treeAt;
		index.insert(pe);
		assets::AsyncChunkPlacementResult result;
		result.coord		= coord;
		result.spatialIndex = std::move(index);
		executor.storeChunkResult(std::move(result));
	};

	const glm::vec2 inside{20.1F, 20.05F};

	auto runOnce = [&]() -> glm::vec2 {
		PlacementExecutor executor(reg);
		buildExecutor(executor);
		World	 world;
		EntityID agent = spawnAgent(world, inside);
		wire(world, executor).update(0.0F);
		return world.getComponent<Position>(agent)->value;
	};

	const glm::vec2 a = runOnce();
	const glm::vec2 b = runOnce();
	EXPECT_FLOAT_EQ(a.x, b.x);
	EXPECT_FLOAT_EQ(a.y, b.y);
}

// --- Non-unit scale regression (the bug guard) --------------------------------

// Before the fix, the collision box scaled the half-extent THEN added the unscaled
// 0.05 m pad, producing world boundary = scale*halfExtent + 0.05. The nav built
// scale*(halfExtent + 0.05). At scale 0.8 the collision boundary was 0.01 m OUTSIDE
// the nav boundary, spuriously pushing agents on valid nav routes. After the fix
// both read nav::placedObstacleOutline, scale*(halfExtent + 0.05), so boundaries coincide.
//
// Rect halfExtents {0.3, 0.3}, scale 0.8:
//   expected boundary = 0.8 * (0.3 + 0.05) = 0.28 m from center on each axis.
TEST_F(PlacedObstacleCollisionSystemTest, NonUnitScaleRegressionJustOutside) {
	AssetRegistry&	reg = AssetRegistry::Get();
	AssetDefinition tree;
	tree.defName					 = "Test_ScaleRect_JustOutside";
	tree.setCollision(0, {.type = CollisionShapeType::Rect, .halfExtentsMeters = {0.3F, 0.3F}});
	reg.registerTestDefinition(tree);

	const glm::vec2					   treeAt{50.0F, 50.0F};
	const world::ChunkCoordinate coord = world::worldToChunk({treeAt.x, treeAt.y});
	PlacementExecutor				   executor(reg);
	{
		SpatialIndex index;
		PlacedEntity pe;
		pe.defName	= "Test_ScaleRect_JustOutside";
		pe.position = treeAt;
		pe.scale	= 0.8F;
		index.insert(pe);
		assets::AsyncChunkPlacementResult result;
		result.coord		= coord;
		result.spatialIndex = std::move(index);
		executor.storeChunkResult(std::move(result));
	}

	// Expected boundary at scale 0.8: 0.8 * (0.3 + 0.05) = 0.28 m from tree center.
	const float boundary = 0.8F * (0.3F + kPadMeters);

	// (a) Agent just OUTSIDE the boundary (0.285 m along +X) must NOT be moved.
	World	 worldA;
	const glm::vec2 justOutside{treeAt.x + boundary + 0.005F, treeAt.y};
	EntityID agentA = spawnAgent(worldA, justOutside);
	wire(worldA, executor).update(0.0F);
	auto* posA = worldA.getComponent<Position>(agentA);
	ASSERT_NE(posA, nullptr);
	EXPECT_FLOAT_EQ(posA->value.x, justOutside.x) << "agent just outside boundary was wrongly moved (scale-0.8 bug)";
	EXPECT_FLOAT_EQ(posA->value.y, justOutside.y);

	// (b) Agent just INSIDE (0.27 m along +X) must be pushed to exactly x = treeAt.x + boundary.
	World	 worldB;
	const glm::vec2 justInside{treeAt.x + boundary - 0.01F, treeAt.y};
	EntityID agentB = spawnAgent(worldB, justInside);
	wire(worldB, executor).update(0.0F);
	auto* posB = worldB.getComponent<Position>(agentB);
	ASSERT_NE(posB, nullptr);
	EXPECT_NEAR(posB->value.x, treeAt.x + boundary, 1e-4F) << "agent inside boundary not pushed to correct boundary (scale-0.8 bug)";
	EXPECT_NEAR(posB->value.y, treeAt.y, 1e-4F);
}

// An agent inside the query box but clearly OUTSIDE the inflated OBB is not moved.
TEST_F(PlacedObstacleCollisionSystemTest, AgentInQueryBoxButOutsideObbNotMoved) {
	AssetRegistry&	reg = AssetRegistry::Get();
	AssetDefinition tree;
	tree.defName					 = "Test_ScaleRect_InBox";
	tree.setCollision(0, {.type = CollisionShapeType::Rect, .halfExtentsMeters = {0.3F, 0.3F}});
	reg.registerTestDefinition(tree);

	const glm::vec2					   treeAt{30.0F, 30.0F};
	const world::ChunkCoordinate coord = world::worldToChunk({treeAt.x, treeAt.y});
	PlacementExecutor				   executor(reg);
	{
		SpatialIndex index;
		PlacedEntity pe;
		pe.defName	= "Test_ScaleRect_InBox";
		pe.position = treeAt;
		index.insert(pe);
		assets::AsyncChunkPlacementResult result;
		result.coord		= coord;
		result.spatialIndex = std::move(index);
		executor.storeChunkResult(std::move(result));
	}

	// Place agent 0.6 m from the tree center: clearly beyond the 0.35 m inflated face
	// but well within the 2 m query box -- this is the case the earlier "far" agent
	// in AgentInsideRectIsPushedOut did NOT exercise (that one was 15 m away).
	const glm::vec2 start{treeAt.x + 0.6F, treeAt.y};
	World	 world;
	EntityID agent = spawnAgent(world, start);
	wire(world, executor).update(0.0F);
	auto* pos = world.getComponent<Position>(agent);
	ASSERT_NE(pos, nullptr);
	EXPECT_FLOAT_EQ(pos->value.x, start.x);
	EXPECT_FLOAT_EQ(pos->value.y, start.y);
}

namespace {

	constexpr const char* kNotchedDefName = "Test_NotchedPolygon";
	constexpr glm::vec2	  kNotchedAt{40.0F, 40.0F};
	constexpr float		  kNotchedRotation = 0.6F;
	constexpr float		  kNotchedScale	   = 1.5F;

	// The notched U, turned and scaled, placed at kNotchedAt.
	PlacedEntity placeNotched(PlacementExecutor& executor) {
		AssetDefinition notched;
		notched.defName = kNotchedDefName;
		notched.setCollision(0, {.type = CollisionShapeType::Polygon, .pointsMeters = kNotchedPoints});
		AssetRegistry::Get().registerTestDefinition(notched);

		PlacedEntity pe;
		pe.defName	= kNotchedDefName;
		pe.position = kNotchedAt;
		pe.rotation = kNotchedRotation;
		pe.scale	= kNotchedScale;
		SpatialIndex index;
		index.insert(pe);
		assets::AsyncChunkPlacementResult result;
		result.coord		= world::worldToChunk({kNotchedAt.x, kNotchedAt.y});
		result.spatialIndex = std::move(index);
		executor.storeChunkResult(std::move(result));
		return pe;
	}

} // namespace

// A turned and scaled concave polygon collider is pushed against like a rect: an agent in the U's arm
// beside the notch goes into the notch, to the point its pivot-frame (0.5, 0.05) lands on.
TEST_F(PlacedObstacleCollisionSystemTest, TurnedScaledConcavePolygonPushesOut) {
	PlacementExecutor				executor(AssetRegistry::Get());
	const PlacedEntity				placed = placeNotched(executor);
	const assets::InstanceTransform transform(placed.position, placed.rotation, placed.scale);

	const glm::vec2 start = transform.toWorld({0.6F, 0.05F});
	World			world;
	const EntityID	agent = spawnAgent(world, start);
	PlacedObstacleCollisionSystem& sys = wire(world, executor);
	ASSERT_FALSE(world.getSystem<NavigationSystem>().isValidPosition(start)) << "the polygon is in the mesh";
	sys.update(0.0F);

	const glm::vec2 expected = transform.toWorld({0.5F, 0.05F});
	const glm::vec2 end		 = world.getComponent<Position>(agent)->value;
	EXPECT_NEAR(end.x, expected.x, 1.5e-3F);
	EXPECT_NEAR(end.y, expected.y, 1.5e-3F);
}

// The outline collision pushes against is the ring the nav mesh cuts, point for point: the polygon's
// pivot-frame points put in the world by the instance transform, and the pushed agent stands on it.
TEST_F(PlacedObstacleCollisionSystemTest, CollisionOutlineIsTheNavRing) {
	PlacementExecutor				executor(AssetRegistry::Get());
	const PlacedEntity				placed = placeNotched(executor);
	const assets::InstanceTransform transform(placed.position, placed.rotation, placed.scale);

	const std::optional<geometry::nav::NavInputPolygon> navRing = nav::placedObstacleRing(placed, AssetRegistry::Get());
	ASSERT_TRUE(navRing.has_value());
	ASSERT_EQ(navRing->ring.size(), kNotchedPoints.size());
	for (const glm::vec2 point : kNotchedPoints) {
		const geometry::Vec2i64 worldMm = nav::toMm(transform.toWorld(point));
		EXPECT_NE(std::find(navRing->ring.begin(), navRing->ring.end(), worldMm), navRing->ring.end()) << point.x << "," << point.y;
	}

	// Beside the reflex corner: pushed onto the corner, which is one of the ring's points.
	World		   world;
	const EntityID agent = spawnAgent(world, transform.toWorld({0.53F, -0.03F}));
	wire(world, executor).update(0.0F);
	const glm::vec2 end = world.getComponent<Position>(agent)->value;
	EXPECT_LE(distanceToRingMm(end, navRing->ring), 1.0);
	EXPECT_LE(glm::length(end - transform.toWorld({0.5F, 0.0F})), 1.5e-3F);
}

// Collision has no camera: two movers 650 m apart, each inside a tree its own disc meshed, one a
// colonist and one not, are both pushed out.
TEST_F(PlacedObstacleCollisionSystemTest, PushedOutOfAMeshedObstacleWhereverItIs) {
	AssetRegistry&	reg = AssetRegistry::Get();
	AssetDefinition tree;
	tree.defName					 = "Test_PlacedRectTreeMeshed";
	tree.setCollision(0, {.type = CollisionShapeType::Rect, .halfExtentsMeters = {0.3F, 0.3F}});
	reg.registerTestDefinition(tree);

	const glm::vec2				 nearTree{20.0F, 20.0F};
	const glm::vec2				 farTree{480.0F, 480.0F};
	const world::ChunkCoordinate coord = world::worldToChunk({nearTree.x, nearTree.y});
	ASSERT_EQ(coord, world::worldToChunk({farTree.x, farTree.y}));
	PlacementExecutor executor(reg);
	{
		SpatialIndex index;
		for (const glm::vec2 at : {nearTree, farTree}) {
			PlacedEntity pe;
			pe.defName	= "Test_PlacedRectTreeMeshed";
			pe.position = at;
			index.insert(pe);
		}
		assets::AsyncChunkPlacementResult result;
		result.coord		= coord;
		result.spatialIndex = std::move(index);
		executor.storeChunkResult(std::move(result));
	}
	const glm::vec2 offset{0.1F, 0.05F};

	World		   world;
	const EntityID mover	= spawnAgent(world, nearTree + offset);
	const EntityID colonist = spawnAgent(world, farTree + offset);
	world.addComponent<Colonist>(colonist, Colonist{"Far"});
	PlacedObstacleCollisionSystem& sys = wire(world, executor);
	const NavigationSystem&	   nav = world.getSystem<NavigationSystem>();
	ASSERT_FALSE(nav.isValidPosition(nearTree + glm::vec2{0.0F, 0.1F})) << "the near tree is in the mesh";
	ASSERT_FALSE(nav.isValidPosition(farTree + glm::vec2{0.0F, 0.1F})) << "the far tree is in the mesh";
	sys.update(0.0F);

	EXPECT_NEAR(world.getComponent<Position>(mover)->value.x, nearTree.x + 0.3F + kPadMeters, 1e-4F) << "the mover wasn't pushed out";
	EXPECT_NEAR(world.getComponent<Position>(colonist)->value.x, farTree.x + 0.3F + kPadMeters, 1e-4F)
		<< "the colonist wasn't pushed out";
}

// A tree whose placement has landed but which is not in the nav mesh yet is not pushed out of, even
// where detail is on: the forward snap gets the first look once the mesh has it.
TEST_F(PlacedObstacleCollisionSystemTest, TreeNotYetInNavMeshIsNotPushed) {
	AssetRegistry&	reg = AssetRegistry::Get();
	AssetDefinition tree;
	tree.defName					 = "Test_PlacedRectTreeLate";
	tree.setCollision(0, {.type = CollisionShapeType::Rect, .halfExtentsMeters = {0.3F, 0.3F}});
	reg.registerTestDefinition(tree);

	const glm::vec2	  treeAt{20.0F, 20.0F};
	const glm::vec2	  inside{treeAt.x + 0.1F, treeAt.y + 0.05F};
	PlacementExecutor executor(reg);
	World			  world;
	EntityID		  agent = spawnAgent(world, inside);
	PlacedObstacleCollisionSystem& sys = wire(world, executor);

	SpatialIndex index;
	PlacedEntity pe;
	pe.defName	= "Test_PlacedRectTreeLate";
	pe.position = treeAt;
	index.insert(pe);
	assets::AsyncChunkPlacementResult result;
	result.coord		= world::worldToChunk({treeAt.x, treeAt.y});
	result.spatialIndex = std::move(index);
	executor.storeChunkResult(std::move(result));
	sys.update(0.0F);
	EXPECT_EQ(world.getComponent<Position>(agent)->value, inside);
}

// A tree mirrored by a negative scale is collided with like any other: its box is the same size
// (|scale| times the padded half extents), its offset mirrored through its position, the box the mesh
// ring nav cuts (nav::placedObstacleOutline).
TEST_F(PlacedObstacleCollisionSystemTest, MirroredTreeIsCollidedWith) {
	AssetRegistry&	reg = AssetRegistry::Get();
	AssetDefinition tree;
	tree.defName					 = "Test_MirroredRect";
	tree.setCollision(0, {.type = CollisionShapeType::Rect, .offsetMeters = {0.5F, 0.0F}, .halfExtentsMeters = {0.3F, 0.3F}});
	reg.registerTestDefinition(tree);

	const glm::vec2				 treeAt{50.0F, 50.0F};
	const world::ChunkCoordinate coord = world::worldToChunk({treeAt.x, treeAt.y});
	PlacementExecutor			 executor(reg);
	{
		SpatialIndex index;
		PlacedEntity pe;
		pe.defName	= "Test_MirroredRect";
		pe.position = treeAt;
		pe.scale	= -1.0F;
		index.insert(pe);
		assets::AsyncChunkPlacementResult result;
		result.coord		= coord;
		result.spatialIndex = std::move(index);
		executor.storeChunkResult(std::move(result));
	}

	// Mirrored, the box's centre is 0.5 m WEST of the tree; an agent 0.1 m east of that centre is
	// inside it and is pushed out east, to the box's side 0.35 m from its centre.
	const glm::vec2 boxCentre{treeAt.x - 0.5F, treeAt.y};
	World			world;
	const EntityID	agent = spawnAgent(world, boxCentre + glm::vec2{0.1F, 0.0F});
	wire(world, executor).update(0.0F);
	EXPECT_NEAR(world.getComponent<Position>(agent)->value.x, boxCentre.x + 0.3F + kPadMeters, 1e-4F)
		<< "the mirrored tree was not collided with";
}

// A collider 2.7 m from its entity's position, past where the query box used to reach, is still
// found: an agent on it is pushed out, and one at the entity's position, off the rect, is left alone.
TEST_F(PlacedObstacleCollisionSystemTest, ColliderFarFromItsPositionIsFound) {
	AssetRegistry& reg = AssetRegistry::Get();

	const glm::vec2				 rockAt{20.0F, 20.0F};
	const world::ChunkCoordinate coord = world::worldToChunk({rockAt.x, rockAt.y});
	PlacementExecutor			 executor(reg);
	{
		SpatialIndex index;
		PlacedEntity pe;
		pe.defName	  = kFarColliderDefName;
		pe.templateId = loadFarColliderDef(reg);
		ASSERT_NE(pe.templateId, 0U);
		pe.position = rockAt;
		index.insert(pe);
		assets::AsyncChunkPlacementResult result;
		result.coord		= coord;
		result.spatialIndex = std::move(index);
		executor.storeChunkResult(std::move(result));
	}

	const glm::vec2 boxCentre = rockAt + kFarColliderOffset;
	const glm::vec2 atOrigin  = rockAt + glm::vec2{0.02F, 0.01F};
	World			world;
	const EntityID	onDrawing  = spawnAgent(world, boxCentre + glm::vec2{0.05F, 0.0F});
	const EntityID	onPosition = spawnAgent(world, atOrigin);
	wire(world, executor).update(0.0F);

	EXPECT_NEAR(
		world.getComponent<Position>(onDrawing)->value.x, boxCentre.x + kFarColliderHalfExtent + kPadMeters, 1e-4F
	) << "the far collider was not found";
	EXPECT_FLOAT_EQ(world.getComponent<Position>(onPosition)->value.x, atOrigin.x);
	EXPECT_FLOAT_EQ(world.getComponent<Position>(onPosition)->value.y, atOrigin.y);
}
