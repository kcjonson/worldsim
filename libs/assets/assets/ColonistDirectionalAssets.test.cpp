// Repro + regression test for the second-colonist spawn crash: the game loads
// Colonist_down at init, but the other directional templates (up/left/right)
// tessellate lazily the first time a colonist faces that way at runtime, and
// that first load crashed the process. Exercise the full lazy path (template +
// motion resolution) headlessly for every direction.

#include "assets/AssetRegistry.h"
#include "assets/AssetRegistry.test.h"

#include <gtest/gtest.h>

using namespace assets;

TEST(ColonistDirectionalAssets, AllDirectionTemplatesAndMotionsResolve) {
	auto& reg = AssetRegistry::Get();
	reg.clear();
	if (!loadShippedLibrary(reg)) {
		GTEST_SKIP() << "assets/world not found";
	}

	for (const char* name : {"Colonist_down", "Colonist_up", "Colonist_left", "Colonist_right"}) {
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

	// Don't leak the populated global registry into other tests in this binary.
	reg.clear();
}
