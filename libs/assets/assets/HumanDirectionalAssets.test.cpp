// Repro + regression test for the second-colonist spawn crash: the game loads
// Human_down at init, but the other directional templates (up/left/right)
// tessellate lazily the first time a colonist faces that way at runtime, and
// that first load crashed the process. Exercise the full lazy path (template +
// motion resolution) headlessly for every direction.

#include "assets/AssetRegistry.h"
#include "assets/AssetRegistry.test.h"
#include "assets/ConditionConfig.h"

#include <gtest/gtest.h>

#include <filesystem>

using namespace assets;

TEST(HumanDirectionalAssets, AllDirectionTemplatesAndMotionsResolve) {
	auto& reg = AssetRegistry::Get();
	reg.clear();
	if (!loadShippedLibrary(reg)) {
		GTEST_SKIP() << "assets/world not found";
	}

	EXPECT_NE(reg.getDefinition("Human"), nullptr) << "the base definition the game spawns";

	for (const char* name : {"Human_down", "Human_up", "Human_left", "Human_right"}) {
		SCOPED_TRACE(name);
		const renderer::TessellatedMesh* mesh = reg.getTemplate(name, TreeTraits{});
		if (mesh == nullptr) {
			ADD_FAILURE() << "getTemplate returned null";
			continue;
		}
		EXPECT_FALSE(mesh->vertices.empty());
		EXPECT_FALSE(mesh->indices.empty());
		// The animated render path needs named parts aligned with the motion clip.
		EXPECT_FALSE(mesh->parts.empty());
		EXPECT_NE(reg.getMotion(name), nullptr);
	}

	// A mover kind naming no shipped definition resolves to id 0, and near() on it reads Unknown.
	// <root>/libs/assets/assets/HumanDirectionalAssets.test.cpp -> <root>/assets/config/conditions
	const std::filesystem::path conditions =
		std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path() / "assets" / "config" / "conditions";
	const ConditionConfigLoad load = loadConditionConfig(conditions);
	ASSERT_TRUE(load.errors.empty());
	for (const ConditionKindDef& kind : load.config.kinds) {
		if (!kind.mover.empty()) {
			EXPECT_NE(reg.getDefinition(kind.mover), nullptr) << "kind " << kind.name << " names mover " << kind.mover;
		}
	}

	// Don't leak the populated global registry into other tests in this binary.
	reg.clear();
}
