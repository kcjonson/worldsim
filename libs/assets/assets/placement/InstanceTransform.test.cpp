// The one local<->world affine every C++ consumer places an asset instance with, and that the
// baked statics really emit it.

#include "assets/placement/InstanceTransform.h"

#include "assets/AssetRegistry.h"
#include "assets/placement/BakedEntityMesh.h"

#include <world/chunk/ChunkCoordinate.h>

#include <gtest/gtest.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

using assets::InstanceTransform;

namespace {

	void expectVecNear(glm::vec2 got, glm::vec2 want, float eps = 1e-4F) {
		EXPECT_NEAR(got.x, want.x, eps);
		EXPECT_NEAR(got.y, want.y, eps);
	}

	// A temporary folder holding one simple def whose drawing is off its own origin.
	struct TempSimpleDef {
		std::filesystem::path root;

		TempSimpleDef() {
			static std::atomic<int> counter{0};
			root = std::filesystem::temp_directory_path() /
				   ("instance_transform_test_" + std::to_string(reinterpret_cast<std::uintptr_t>(&counter)) + "_" + std::to_string(counter++));
			std::filesystem::create_directories(root / "Block");
			std::ofstream(root / "Block" / "block.svg")
				<< "<?xml version=\"1.0\"?><svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 100 100\" width=\"100\" height=\"100\">"
				   "<path d=\"M20,10 L80,10 L70,40 L30,40 Z\" fill=\"#808080\"/></svg>";
			std::ofstream(root / "Block" / "Block.xml")
				<< "<?xml version=\"1.0\"?><AssetDefinitions><AssetDef><defName>Block</defName><assetType>simple</assetType>"
				   "<svgPath>block.svg</svgPath><worldHeight>1.5</worldHeight></AssetDef></AssetDefinitions>";
			assets::AssetRegistry::Get().clearDefinitions();
			assets::AssetRegistry::Get().loadDefinitionsFromFolder(root.string());
		}

		~TempSimpleDef() {
			assets::AssetRegistry::Get().clearDefinitions();
			std::error_code ignored;
			std::filesystem::remove_all(root, ignored);
		}

		TempSimpleDef(const TempSimpleDef&)			   = delete;
		TempSimpleDef& operator=(const TempSimpleDef&) = delete;
	};

} // namespace

// world = R(rotation) * (local * scale) + position, worked by hand.
TEST(InstanceTransform, TurnsScalesThenMoves) {
	const InstanceTransform t({12.0F, -7.0F}, 3.14159265F / 2.0F, 2.0F);
	expectVecNear(t.toWorld({1.0F, 0.0F}), {12.0F, -5.0F});
	expectVecNear(t.toWorld({0.0F, 1.0F}), {10.0F, -7.0F});
	expectVecNear(t.axisX(), {0.0F, 1.0F});
	expectVecNear(t.axisY(), {-1.0F, 0.0F});
	const glm::dvec2 precise = t.toWorldPrecise({1.0, 0.0});
	EXPECT_NEAR(precise.x, 12.0, 1e-6);
	EXPECT_NEAR(precise.y, -5.0, 1e-6);
}

TEST(InstanceTransform, RoundTrip) {
	const InstanceTransform t({12.0F, -7.0F}, 0.6F, 1.3F);
	for (glm::vec2 local : {glm::vec2{0, 0}, glm::vec2{2, -1}, glm::vec2{-3, 4}}) {
		expectVecNear(t.toLocal(t.toWorld(local)), local);
	}
}

TEST(InstanceTransform, ZeroScaleToLocalDoesNotDivideByZero) {
	const InstanceTransform t({1.0F, 2.0F}, 0.0F, 0.0F);
	const glm::vec2			l = t.toLocal({3.0F, 4.0F});
	EXPECT_EQ(l.x, 0.0F);
	EXPECT_EQ(l.y, 0.0F);
}

// The baked statics place every vertex of a real template exactly where InstanceTransform does,
// turned and scaled, so the bake, the colliders, and the hit-test can't drift apart.
TEST(InstanceTransform, BakedStaticsEmitIt) {
	const TempSimpleDef	   defs;
	assets::AssetRegistry& registry = assets::AssetRegistry::Get();

	assets::PlacedEntity entity;
	entity.defName	  = "Block";
	entity.templateId = registry.templateId("Block", {});
	entity.position	  = {10.0F, 12.0F};
	entity.rotation	  = 0.7F;
	entity.scale	  = 1.3F;
	const renderer::TessellatedMesh* mesh = registry.getTemplate(entity.templateId);
	ASSERT_NE(mesh, nullptr);
	ASSERT_FALSE(mesh->vertices.empty());

	const assets::BakedChunkCPUData baked =
		assets::bakeChunkEntities({&entity}, world::worldToChunk({entity.position.x, entity.position.y}), [&](const assets::PlacedEntity&) {
			return mesh;
		});
	const assets::BakedMeshCPU* bucket = nullptr;
	for (const auto& sub : baked.subChunks) {
		if (!sub.floraMesh.vertices.empty()) {
			bucket = &sub.floraMesh;
		}
	}
	ASSERT_NE(bucket, nullptr);
	ASSERT_EQ(bucket->vertices.size(), mesh->vertices.size());

	const InstanceTransform transform(entity.position, entity.rotation, entity.scale);
	for (size_t i = 0; i < mesh->vertices.size(); ++i) {
		const glm::vec2 want = transform.toWorld({mesh->vertices[i].x, mesh->vertices[i].y});
		EXPECT_EQ(bucket->vertices[i].position.x, want.x) << "vertex " << i;
		EXPECT_EQ(bucket->vertices[i].position.y, want.y) << "vertex " << i;
	}
}
