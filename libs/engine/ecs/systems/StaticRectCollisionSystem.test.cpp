#include "StaticRectCollisionSystem.h"

#include "../World.h"
#include "../components/AgentRadius.h"
#include "../components/Transform.h"

#include <assets/AssetDefinition.h>
#include <assets/AssetRegistry.h>
#include <assets/placement/PlacementExecutor.h>
#include <assets/placement/SpatialIndex.h>

#include <world/chunk/ChunkCoordinate.h>

#include <glm/geometric.hpp>
#include <glm/vec2.hpp>

#include <cmath>
#include <string>

#include <gtest/gtest.h>

using namespace ecs;
using engine::assets::AssetDefinition;
using engine::assets::AssetRegistry;
using engine::assets::CollisionShapeType;
using engine::assets::PlacedEntity;
using engine::assets::PlacementExecutor;
using engine::assets::SpatialIndex;

namespace {

	// An axis-aligned rect centered at `center` with the given half-extents (no
	// rotation, unit scale equivalent).
	OrientedRect axisAlignedRect(glm::vec2 center, glm::vec2 halfExtents) {
		OrientedRect rect;
		rect.center		 = center;
		rect.axisX		 = {1.0F, 0.0F};
		rect.axisY		 = {0.0F, 1.0F};
		rect.halfExtents = halfExtents;
		return rect;
	}

	OrientedRect rotatedRect(glm::vec2 center, glm::vec2 halfExtents, float angle) {
		const float	 c = std::cos(angle);
		const float	 s = std::sin(angle);
		OrientedRect rect;
		rect.center		 = center;
		rect.axisX		 = {c, s};
		rect.axisY		 = {-s, c};
		rect.halfExtents = halfExtents;
		return rect;
	}

	constexpr float kClearance = 0.05F;

} // namespace

// --- Pure helper: resolveCenterAgainstRect ---------------------------------

// A center well outside the inflated rect is not touched.
TEST(StaticRectResolveTest, OutsideReturnsNullopt) {
	const OrientedRect rect = axisAlignedRect({0.0F, 0.0F}, {0.3F, 0.3F});
	EXPECT_FALSE(resolveCenterAgainstRect({5.0F, 5.0F}, kClearance, rect).has_value());
}

// A center just inside the inflated +X face is pushed to exactly halfExtent+clearance
// along X, y unchanged.
TEST(StaticRectResolveTest, JustInsidePlusXPushedToBoundary) {
	const OrientedRect rect = axisAlignedRect({0.0F, 0.0F}, {0.3F, 0.3F});
	const float		   ex	= 0.3F + kClearance;
	// Slightly inside the +X boundary, well within the band in Y (so X is least pen).
	const glm::vec2 start{ex - 0.01F, 0.0F};

	auto corrected = resolveCenterAgainstRect(start, kClearance, rect);
	ASSERT_TRUE(corrected.has_value());
	EXPECT_FLOAT_EQ(corrected->x, ex);
	EXPECT_FLOAT_EQ(corrected->y, 0.0F);
}

// A center deep inside an axis-aligned rect is pushed out the NEAREST face.
TEST(StaticRectResolveTest, DeepInsidePushedNearestFace) {
	const OrientedRect rect = axisAlignedRect({0.0F, 0.0F}, {0.3F, 0.3F});
	// Closer to the +X face than to +Y: x penetration is smaller.
	const glm::vec2 start{0.2F, 0.05F};

	auto corrected = resolveCenterAgainstRect(start, kClearance, rect);
	ASSERT_TRUE(corrected.has_value());
	EXPECT_FLOAT_EQ(corrected->x, 0.3F + kClearance);
	EXPECT_FLOAT_EQ(corrected->y, 0.05F); // unchanged: pushed along X only
}

// A ROTATED, non-square rect: the corrected center lands on the inflated oriented
// face. Verify by projecting onto the rect axes: the least-penetration local coord
// equals +/-(halfExtent+clearance); the other local coord is unchanged.
TEST(StaticRectResolveTest, RotatedRectPushesAlongOrientedFace) {
	const float		   angle = 0.5235987756F; // 30 degrees
	const glm::vec2	   center{2.0F, -1.0F};
	const glm::vec2	   half{0.4F, 0.15F};
	const OrientedRect rect = rotatedRect(center, half, angle);

	// Start a point a little inside the +axisX inflated face: local coords
	// (lx slightly less than ex, ly small so X is least penetration).
	const float		ex = half.x + kClearance;
	const float		ly = 0.03F;
	const glm::vec2 start = center + (ex - 0.02F) * rect.axisX + ly * rect.axisY;

	auto corrected = resolveCenterAgainstRect(start, kClearance, rect);
	ASSERT_TRUE(corrected.has_value());

	const glm::vec2 d		 = *corrected - center;
	const float		newLx	 = glm::dot(d, rect.axisX);
	const float		newLy	 = glm::dot(d, rect.axisY);
	EXPECT_NEAR(newLx, ex, 1e-5F);	  // pushed to the inflated oriented face on X
	EXPECT_NEAR(newLy, ly, 1e-5F);	  // other axis unchanged
}

// A center exactly at the rect center resolves deterministically: least-penetration
// axis with sign +. For a wider-than-tall rect, Y is the smaller half-extent so the
// Y penetration is least -> push +Y.
TEST(StaticRectResolveTest, ExactCenterDeterministicPush) {
	const OrientedRect rect = axisAlignedRect({0.0F, 0.0F}, {0.4F, 0.2F});
	auto			   corrected = resolveCenterAgainstRect({0.0F, 0.0F}, kClearance, rect);
	ASSERT_TRUE(corrected.has_value());
	// penX = 0.4+c, penY = 0.2+c -> penY < penX -> push along +Y to 0.2+c.
	EXPECT_FLOAT_EQ(corrected->x, 0.0F);
	EXPECT_FLOAT_EQ(corrected->y, 0.2F + kClearance);
}

// --- System integration ----------------------------------------------------

namespace {

	class StaticRectCollisionSystemTest : public ::testing::Test {
	  protected:
		void TearDown() override { AssetRegistry::Get().clearDefinitions(); }

		EntityID spawnAgent(World& world, glm::vec2 pos, float radius = 0.3F) {
			EntityID e = world.createEntity();
			world.addComponent<Position>(e, Position{pos});
			world.addComponent<AgentRadius>(e, AgentRadius{radius, 1.0F});
			return e;
		}
	};

} // namespace

// update() with no placement wired is a safe no-op.
TEST_F(StaticRectCollisionSystemTest, UnwiredUpdateIsNoOp) {
	World world;
	const glm::vec2 start{1.0F, 1.0F};
	EntityID		agent = spawnAgent(world, start);

	auto& sys = world.registerSystem<StaticRectCollisionSystem>();
	// Deliberately do NOT setPlacementData.
	sys.update(0.0F);

	auto* pos = world.getComponent<Position>(agent);
	ASSERT_NE(pos, nullptr);
	EXPECT_FLOAT_EQ(pos->value.x, start.x);
	EXPECT_FLOAT_EQ(pos->value.y, start.y);
}

// A blocking flora rect: an agent whose CENTER is inside it is pushed out past the
// 0.05 m inflated boundary; an agent far away is untouched.
TEST_F(StaticRectCollisionSystemTest, AgentInsideRectIsPushedOut) {
	AssetRegistry&	reg = AssetRegistry::Get();
	AssetDefinition tree;
	tree.defName					 = "Test_StaticRectTree";
	tree.collision.type				 = CollisionShapeType::Rect;
	tree.collision.halfExtentsMeters = {0.3F, 0.3F};
	reg.registerTestDefinition(tree);

	PlacementExecutor executor(reg);

	const glm::vec2					   treeAt{20.0F, 20.0F};
	const engine::world::ChunkCoordinate coord = engine::world::worldToChunk({treeAt.x, treeAt.y});
	SpatialIndex					   index;
	PlacedEntity					   pe;
	pe.defName	= "Test_StaticRectTree";
	pe.position = treeAt;
	index.insert(pe);
	engine::assets::AsyncChunkPlacementResult result;
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

	auto& sys = world.registerSystem<StaticRectCollisionSystem>();
	sys.setPlacementData(&executor);
	sys.update(0.0F);

	// Near agent: now outside the rect inflated by clearance. The rect is axis
	// aligned, so check the agent escaped the inflated AABB.
	auto* nearPos = world.getComponent<Position>(near);
	ASSERT_NE(nearPos, nullptr);
	const float ex	  = tree.collision.halfExtentsMeters.x + kStaticRectClearanceMeters;
	const float ey	  = tree.collision.halfExtentsMeters.y + kStaticRectClearanceMeters;
	const float dx	  = std::abs(nearPos->value.x - treeAt.x);
	const float dy	  = std::abs(nearPos->value.y - treeAt.y);
	EXPECT_TRUE(dx >= ex - 1e-4F || dy >= ey - 1e-4F) << "agent center still inside inflated rect";

	// Far agent: untouched.
	auto* farPos = world.getComponent<Position>(far);
	ASSERT_NE(farPos, nullptr);
	EXPECT_FLOAT_EQ(farPos->value.x, farStart.x);
	EXPECT_FLOAT_EQ(farPos->value.y, farStart.y);
}

// Same setup run twice yields an identical corrected position (determinism).
TEST_F(StaticRectCollisionSystemTest, DeterministicCorrection) {
	AssetRegistry&	reg = AssetRegistry::Get();
	AssetDefinition tree;
	tree.defName					 = "Test_StaticRectTreeDet";
	tree.collision.type				 = CollisionShapeType::Rect;
	tree.collision.halfExtentsMeters = {0.3F, 0.3F};
	reg.registerTestDefinition(tree);

	auto buildExecutor = [&](PlacementExecutor& executor) {
		const glm::vec2					   treeAt{20.0F, 20.0F};
		const engine::world::ChunkCoordinate coord = engine::world::worldToChunk({treeAt.x, treeAt.y});
		SpatialIndex					   index;
		PlacedEntity					   pe;
		pe.defName	= "Test_StaticRectTreeDet";
		pe.position = treeAt;
		index.insert(pe);
		engine::assets::AsyncChunkPlacementResult result;
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
		auto&	 sys   = world.registerSystem<StaticRectCollisionSystem>();
		sys.setPlacementData(&executor);
		sys.update(0.0F);
		return world.getComponent<Position>(agent)->value;
	};

	const glm::vec2 a = runOnce();
	const glm::vec2 b = runOnce();
	EXPECT_FLOAT_EQ(a.x, b.x);
	EXPECT_FLOAT_EQ(a.y, b.y);
}

// --- Non-unit scale regression (the bug guard) --------------------------------

// Before the fix, orientedRectFor scaled the half-extent THEN added the unscaled
// 0.05 m pad, producing world boundary = scale*halfExtent + 0.05. The nav built
// scale*(halfExtent + 0.05). At scale 0.8 the collision boundary was 0.01 m OUTSIDE
// the nav boundary, spuriously pushing agents on valid nav routes. After the fix
// both sides compute scale*(halfExtent + 0.05), so boundaries coincide.
//
// Rect halfExtents {0.3, 0.3}, scale 0.8:
//   expected boundary = 0.8 * (0.3 + 0.05) = 0.28 m from center on each axis.
TEST_F(StaticRectCollisionSystemTest, NonUnitScaleRegressionJustOutside) {
	AssetRegistry&	reg = AssetRegistry::Get();
	AssetDefinition tree;
	tree.defName					 = "Test_ScaleRect_JustOutside";
	tree.collision.type				 = CollisionShapeType::Rect;
	tree.collision.halfExtentsMeters = {0.3F, 0.3F};
	reg.registerTestDefinition(tree);

	const glm::vec2					   treeAt{50.0F, 50.0F};
	const engine::world::ChunkCoordinate coord = engine::world::worldToChunk({treeAt.x, treeAt.y});
	PlacementExecutor				   executor(reg);
	{
		SpatialIndex index;
		PlacedEntity pe;
		pe.defName	= "Test_ScaleRect_JustOutside";
		pe.position = treeAt;
		pe.scale	= 0.8F;
		index.insert(pe);
		engine::assets::AsyncChunkPlacementResult result;
		result.coord		= coord;
		result.spatialIndex = std::move(index);
		executor.storeChunkResult(std::move(result));
	}

	// Expected boundary at scale 0.8: 0.8 * (0.3 + 0.05) = 0.28 m from tree center.
	const float boundary = 0.8F * (0.3F + kStaticRectClearanceMeters);

	// (a) Agent just OUTSIDE the boundary (0.285 m along +X) must NOT be moved.
	World	 worldA;
	const glm::vec2 justOutside{treeAt.x + boundary + 0.005F, treeAt.y};
	EntityID agentA = spawnAgent(worldA, justOutside);
	auto&	 sysA   = worldA.registerSystem<StaticRectCollisionSystem>();
	sysA.setPlacementData(&executor);
	sysA.update(0.0F);
	auto* posA = worldA.getComponent<Position>(agentA);
	ASSERT_NE(posA, nullptr);
	EXPECT_FLOAT_EQ(posA->value.x, justOutside.x) << "agent just outside boundary was wrongly moved (scale-0.8 bug)";
	EXPECT_FLOAT_EQ(posA->value.y, justOutside.y);

	// (b) Agent just INSIDE (0.27 m along +X) must be pushed to exactly x = treeAt.x + boundary.
	World	 worldB;
	const glm::vec2 justInside{treeAt.x + boundary - 0.01F, treeAt.y};
	EntityID agentB = spawnAgent(worldB, justInside);
	auto&	 sysB   = worldB.registerSystem<StaticRectCollisionSystem>();
	sysB.setPlacementData(&executor);
	sysB.update(0.0F);
	auto* posB = worldB.getComponent<Position>(agentB);
	ASSERT_NE(posB, nullptr);
	EXPECT_NEAR(posB->value.x, treeAt.x + boundary, 1e-4F) << "agent inside boundary not pushed to correct boundary (scale-0.8 bug)";
	EXPECT_NEAR(posB->value.y, treeAt.y, 1e-4F);
}

// An agent inside the query box but clearly OUTSIDE the inflated OBB is not moved.
TEST_F(StaticRectCollisionSystemTest, AgentInQueryBoxButOutsideObbNotMoved) {
	AssetRegistry&	reg = AssetRegistry::Get();
	AssetDefinition tree;
	tree.defName					 = "Test_ScaleRect_InBox";
	tree.collision.type				 = CollisionShapeType::Rect;
	tree.collision.halfExtentsMeters = {0.3F, 0.3F};
	reg.registerTestDefinition(tree);

	const glm::vec2					   treeAt{30.0F, 30.0F};
	const engine::world::ChunkCoordinate coord = engine::world::worldToChunk({treeAt.x, treeAt.y});
	PlacementExecutor				   executor(reg);
	{
		SpatialIndex index;
		PlacedEntity pe;
		pe.defName	= "Test_ScaleRect_InBox";
		pe.position = treeAt;
		index.insert(pe);
		engine::assets::AsyncChunkPlacementResult result;
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
	auto&	 sys   = world.registerSystem<StaticRectCollisionSystem>();
	sys.setPlacementData(&executor);
	sys.update(0.0F);
	auto* pos = world.getComponent<Position>(agent);
	ASSERT_NE(pos, nullptr);
	EXPECT_FLOAT_EQ(pos->value.x, start.x);
	EXPECT_FLOAT_EQ(pos->value.y, start.y);
}

// A non-Rect collision entity (Polygon) overlapping an agent is ignored.
TEST_F(StaticRectCollisionSystemTest, NonRectCollisionIgnored) {
	AssetRegistry&	reg = AssetRegistry::Get();
	AssetDefinition poly;
	poly.defName		  = "Test_PolyCollision_Ignored";
	poly.collision.type	  = CollisionShapeType::Polygon;
	poly.collision.pointsMeters = {
		{-1.0F, -1.0F}, {1.0F, -1.0F}, {1.0F, 1.0F}, {-1.0F, 1.0F}};
	reg.registerTestDefinition(poly);

	const glm::vec2					   treeAt{40.0F, 40.0F};
	const engine::world::ChunkCoordinate coord = engine::world::worldToChunk({treeAt.x, treeAt.y});
	PlacementExecutor				   executor(reg);
	{
		SpatialIndex index;
		PlacedEntity pe;
		pe.defName	= "Test_PolyCollision_Ignored";
		pe.position = treeAt;
		index.insert(pe);
		engine::assets::AsyncChunkPlacementResult result;
		result.coord		= coord;
		result.spatialIndex = std::move(index);
		executor.storeChunkResult(std::move(result));
	}

	// Agent is squarely inside the polygon's bounds -- it must not be moved because
	// StaticRectCollisionSystem only handles Rect collision shapes.
	const glm::vec2 start{treeAt.x + 0.1F, treeAt.y};
	World	 world;
	EntityID agent = spawnAgent(world, start);
	auto&	 sys   = world.registerSystem<StaticRectCollisionSystem>();
	sys.setPlacementData(&executor);
	sys.update(0.0F);
	auto* pos = world.getComponent<Position>(agent);
	ASSERT_NE(pos, nullptr);
	EXPECT_FLOAT_EQ(pos->value.x, start.x);
	EXPECT_FLOAT_EQ(pos->value.y, start.y);
}
