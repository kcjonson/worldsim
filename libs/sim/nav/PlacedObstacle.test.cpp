// A placed entity's collider stands on what is drawn for it.

#include "PlacedObstacle.h"

#include "NavCoords.h"
#include "NavInputBuilder.h"

#include <assets/AssetRegistry.h>
#include <assets/placement/InstanceTransform.h>
#include <assets/placement/SpatialIndex.h>

#include <glm/geometric.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>

namespace {

	// One simple (SVG) def and one procedural (Lua) def, each with a rect collider whose outer
	// corner is a vertex of its drawing, and neither drawing about its own origin. The SVG's
	// drawing spans x 20..80, y 10..40 (pivot (50, 25)); its collider's corner (80, 40) is the
	// drawing's. The Lua drawing spans x 1..2, y 0..1 m about the generator's origin; its
	// collider's corner (2, 1) is the drawing's.
	struct ColliderDefs {
		std::filesystem::path root;

		ColliderDefs() {
			static std::atomic<int> counter{0};
			root = std::filesystem::temp_directory_path() /
				   ("placed_obstacle_test_" + std::to_string(reinterpret_cast<std::uintptr_t>(&counter)) + "_" + std::to_string(counter++));
			std::filesystem::create_directories(root / "SvgBlock");
			std::filesystem::create_directories(root / "LuaBlock");
			std::ofstream(root / "SvgBlock" / "block.svg")
				<< "<?xml version=\"1.0\"?><svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 100 100\" width=\"100\" height=\"100\">"
				   "<metadata><collision><shape type=\"aabb\" min=\"60,30\" max=\"80,40\"/></collision></metadata>"
				   "<path d=\"M20,10 L80,10 L80,40 L20,40 Z\" fill=\"#808080\"/></svg>";
			std::ofstream(root / "SvgBlock" / "SvgBlock.xml")
				<< "<?xml version=\"1.0\"?><AssetDefinitions><AssetDef><defName>SvgBlock</defName><assetType>simple</assetType>"
				   "<svgPath>block.svg</svgPath><worldHeight>1.5</worldHeight></AssetDef></AssetDefinitions>";
			std::ofstream(root / "LuaBlock" / "block.lua") << "local p = Path()\n"
															   "p:addVertex(1, 0)\n"
															   "p:addVertex(2, 0)\n"
															   "p:addVertex(2, 1)\n"
															   "p:addVertex(1, 1)\n"
															   "p:setColor(0.5, 0.5, 0.5, 1.0)\n"
															   "p:close()\n"
															   "asset:addPath(p)\n"
															   "asset:setCollisionRect(0.25, 0.25, 1.75, 0.75)\n";
			std::ofstream(root / "LuaBlock" / "LuaBlock.xml")
				<< "<?xml version=\"1.0\"?><AssetDefinitions><AssetDef><defName>LuaBlock</defName><assetType>procedural</assetType>"
				   "<generator><scriptPath>block.lua</scriptPath></generator></AssetDef></AssetDefinitions>";
			assets::AssetRegistry::Get().clearDefinitions();
			assets::AssetRegistry::Get().loadDefinitionsFromFolder(root.string());
		}

		~ColliderDefs() {
			assets::AssetRegistry::Get().clearDefinitions();
			std::error_code ignored;
			std::filesystem::remove_all(root, ignored);
		}

		ColliderDefs(const ColliderDefs&)			 = delete;
		ColliderDefs& operator=(const ColliderDefs&) = delete;
	};

	// The template mesh's vertex with the largest x + y: the drawing's outer corner.
	glm::vec2 meshOuterCorner(const renderer::TessellatedMesh& mesh) {
		glm::vec2 best{0.0F, 0.0F};
		float	  bestSum = -std::numeric_limits<float>::max();
		for (const auto& v : mesh.vertices) {
			if (v.x + v.y > bestSum) {
				bestSum = v.x + v.y;
				best	= {v.x, v.y};
			}
		}
		return best;
	}

} // namespace

// The collider's outer corner, read back off the padded box nav and static collision use, lands
// where the instance transform puts the drawing's matching corner, for a simple and a procedural
// def alike, turned and scaled. The SVG drawing is not about its own origin and the Lua one is
// not about its pivot, so a frame mix between collider and mesh shows here.
TEST(PlacedObstacle, ColliderCornerLandsOnTheDrawnCorner) {
	const ColliderDefs	   defs;
	assets::AssetRegistry& registry = assets::AssetRegistry::Get();
	for (const char* defName : {"SvgBlock", "LuaBlock"}) {
		SCOPED_TRACE(defName);
		const assets::AssetDefinition* def = registry.getDefinition(defName);
		ASSERT_NE(def, nullptr);
		ASSERT_EQ(def->collisionFor(0).type, assets::CollisionShapeType::Rect);
		assets::PlacedEntity entity;
		entity.defName	  = defName;
		entity.templateId = registry.templateId(defName, {});
		entity.position	  = {40.0F, -12.0F};
		entity.rotation	  = 0.7F;
		entity.scale	  = 1.3F;
		const renderer::TessellatedMesh* mesh = registry.getTemplate(entity.templateId);
		ASSERT_NE(mesh, nullptr);

		// The ring's padded outer corner is the drawn corner moved out by the pad on both of the
		// pivot frame's axes, then put in the world.
		geometry::Ring outline;
		ASSERT_TRUE(nav::placedObstacleOutline(entity, registry, outline));
		const float		pad	   = static_cast<float>(nav::kPlacedObstaclePadMm) / 1000.0F;
		const glm::vec2 padded = assets::InstanceTransform(entity.position, entity.rotation, entity.scale).toWorld(meshOuterCorner(*mesh) + glm::vec2{pad, pad});
		float			nearest = std::numeric_limits<float>::max();
		for (const geometry::Vec2i64& corner : outline) {
			nearest = std::min(nearest, glm::length(nav::toMeters(corner) - padded));
		}
		EXPECT_LT(nearest, 1e-3F);
	}
}

namespace {

	// How far the farthest point of a placed entity's outline, the padded ring nav cuts, stands from
	// its position.
	float outlineReach(const assets::PlacedEntity& entity) {
		geometry::Ring outline;
		EXPECT_TRUE(nav::placedObstacleOutline(entity, assets::AssetRegistry::Get(), outline));
		float reach = 0.0F;
		for (const geometry::Vec2i64& corner : outline) {
			reach = std::max(reach, glm::length(nav::toMeters(corner) - entity.position));
		}
		return reach;
	}

	assets::PlacedEntity placedAt(const char* defName, float rotation, float scale) {
		assets::PlacedEntity entity;
		entity.defName	= defName;
		entity.position = {10.0F, 10.0F};
		entity.rotation = rotation;
		entity.scale	= scale;
		return entity;
	}

} // namespace

// The reach covers every padded collider: one scaled up past 1 and turned, and one whose def only
// places below scale 1 but stands at scale 1 when the player places it or dev spawn makes it.
TEST(PlacedObstacle, ReachCoversEveryPaddedCollider) {
	assets::AssetRegistry& registry = assets::AssetRegistry::Get();
	registry.clearDefinitions();

	const assets::CollisionShape largeShape{.type = assets::CollisionShapeType::Rect, .offsetMeters = {2.0F, 0.0F}, .halfExtentsMeters = {0.5F, 0.25F}};
	assets::AssetDefinition		 large;
	large.defName		  = "Test_LargeCollider";
	large.placement.scale = {.min = 0.5F, .max = 3.0F};
	large.setCollision(0, largeShape);
	registry.registerTestDefinition(large);

	const assets::CollisionShape smallShape{.type = assets::CollisionShapeType::Rect, .offsetMeters = {2.4F, 0.0F}, .halfExtentsMeters = {0.3F, 0.3F}};
	assets::AssetDefinition		 small;
	small.defName		  = "Test_SmallRangeCollider";
	small.placement.scale = {.min = 0.4F, .max = 0.6F};
	small.setCollision(0, smallShape);
	registry.registerTestDefinition(small);

	const float reach = nav::placedObstacleReachMeters(registry);
	EXPECT_LE(outlineReach(placedAt("Test_LargeCollider", 0.7F, 3.0F)), reach + 1e-3F);
	EXPECT_LE(outlineReach(placedAt("Test_SmallRangeCollider", 2.0F, 1.0F)), reach + 1e-3F);
	registry.clearDefinitions();
}

// The reach over the shipped colliding defs is BigRock's: its polygon's farthest point, the rim's
// (136, 70), (58, 9) SVG units from its pivot at 1/60 m a unit, at its 1.2 largest scale, plus the
// 50 mm pad's corner at that scale (the reach takes the pad whatever the shape).
// docs/technical/nav-mesh-variable-resolution.md quotes it.
TEST(PlacedObstacle, ShippedReach) {
	const std::filesystem::path repo	 = SIM_TEST_REPO_DIR;
	assets::AssetRegistry&		registry = assets::AssetRegistry::Get();
	registry.clearDefinitions();
	registry.setSharedScriptsPath(repo / "assets" / "shared" / "scripts");
	for (const char* xml :
		 {"misc/BigRock/BigRock.xml", "flora/OakTree/OakTree.xml", "flora/MapleTree/MapleTree.xml", "flora/PineTree/PineTree.xml", "flora/PalmTree/PalmTree.xml"}) {
		ASSERT_TRUE(registry.loadDefinitions((repo / "assets" / "world" / xml).string())) << xml;
	}
	const float bigRock = std::hypot(58.0F, 9.0F) * (1.5F / 90.0F) * 1.2F;
	const float pad		= std::sqrt(2.0F) * 0.05F * 1.2F;
	EXPECT_NEAR(nav::placedObstacleReachMeters(registry), bigRock + pad, 1e-3F);
	registry.clearDefinitions();
}
