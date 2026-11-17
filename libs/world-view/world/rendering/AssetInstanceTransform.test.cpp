// Tests for the shared asset instance transform (Story C): the one local<->world
// affine that render, outline, hit-test, and a placed entity's collider share. Guards the
// forward/inverse round-trip and parity with the render paths' own math, so a hit-test or a
// collider can't drift off the drawn sprite.

#include "world/rendering/AssetInstanceTransform.h"

#include <assets/AssetRegistry.h>
#include <assets/placement/SpatialIndex.h>
#include <nav/PlacedObstacle.h>

#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>

using worldview::AssetInstanceTransform;
using worldview::instanceTransform;

namespace {

	// One simple (SVG) def and one procedural (Lua) def, each with a rect collider whose
	// outer corner is a vertex of its drawing, and neither drawing about its own origin.
	// The SVG's drawing spans x 20..80, y 10..40 (pivot (50, 25)); its collider's corner
	// (80, 40) is the drawing's. The Lua drawing spans x 1..2, y 0..1 m about the generator's
	// origin; its collider's corner (2, 1) is the drawing's.
	struct ColliderDefs {
		std::filesystem::path root;

		ColliderDefs() {
			namespace fs = std::filesystem;
			root		 = fs::temp_directory_path() / "asset_instance_transform_collider";
			fs::remove_all(root);
			fs::create_directories(root / "SvgBlock");
			fs::create_directories(root / "LuaBlock");
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

	// The world point a placed entity's collider puts its local (+x, +y) corner at, read back
	// off the padded box nav and static collision use.
	glm::vec2 colliderOuterCornerInWorld(const assets::PlacedEntity& entity, const assets::CollisionShape& collision) {
		const std::optional<nav::OrientedRect> box = nav::placedObstacleBox(entity, collision);
		EXPECT_TRUE(box.has_value());
		const float pad = std::abs(entity.scale) * static_cast<float>(nav::kPlacedObstaclePadMm) / 1000.0F;
		return box->center + box->axisX * (box->halfExtents.x - pad) + box->axisY * (box->halfExtents.y - pad);
	}

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

	void expectVecNear(glm::vec2 got, glm::vec2 want, float eps = 1e-4F) {
		EXPECT_NEAR(got.x, want.x, eps);
		EXPECT_NEAR(got.y, want.y, eps);
	}

	// The formula, computed independently (matches BakedEntityMesh.cpp).
	glm::vec2 bakedFormula(glm::vec2 local, glm::vec2 position, float rotation, float scale) {
		const float sx = local.x * scale;
		const float sy = local.y * scale;
		if (rotation == 0.0F) {
			return {sx + position.x, sy + position.y};
		}
		const float c = std::cos(rotation);
		const float s = std::sin(rotation);
		return {sx * c - sy * s + position.x, sx * s + sy * c + position.y};
	}

} // namespace

TEST(AssetInstanceTransform, ForwardMatchesBakedFormula) {
	const glm::vec2 position{12.0F, -7.0F};
	const float		rotation = 0.6F;
	const float		scale	 = 1.3F;
	const auto		t		 = instanceTransform(position, rotation, scale);
	for (glm::vec2 local : {glm::vec2{0, 0}, glm::vec2{1, 0}, glm::vec2{-2, 3}, glm::vec2{0.5F, -0.5F}}) {
		expectVecNear(t.toWorld(local), bakedFormula(local, position, rotation, scale));
	}
}

TEST(AssetInstanceTransform, RoundTrip) {
	const auto t = instanceTransform({12.0F, -7.0F}, 0.6F, 1.3F);
	for (glm::vec2 local : {glm::vec2{0, 0}, glm::vec2{2, -1}, glm::vec2{-3, 4}}) {
		expectVecNear(t.toLocal(t.toWorld(local)), local);
	}
}

TEST(AssetInstanceTransform, ZeroScaleToLocalDoesNotDivideByZero) {
	const auto t = instanceTransform({1.0F, 2.0F}, 0.0F, 0.0F);
	const auto l = t.toLocal({3.0F, 4.0F});
	EXPECT_EQ(l.x, 0.0F);
	EXPECT_EQ(l.y, 0.0F);
}

// A placed entity's collider stands on what the renderer draws for it: the collider's outer
// corner lands where the instance transform puts the drawing's matching corner, for a simple
// and a procedural def alike, turned and scaled. The SVG drawing is not about its own origin and
// the Lua one is not about its pivot, so a frame mix between them shows here.
TEST(AssetInstanceTransform, ColliderCornerLandsOnTheDrawnCorner) {
	const ColliderDefs		defs;
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

		const glm::vec2 drawn = instanceTransform(entity.position, entity.rotation, entity.scale).toWorld(meshOuterCorner(*mesh));
		expectVecNear(colliderOuterCornerInWorld(entity, def->collisionFor(0)), drawn);
	}
}
