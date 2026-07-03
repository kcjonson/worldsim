// Repro + regression test for the second-colonist spawn crash: the game loads
// Colonist_down at init, but the other directional templates (up/left/right)
// tessellate lazily the first time a colonist faces that way at runtime, and
// that first load crashed the process. Exercise the full lazy path (template +
// motion resolution) headlessly for every direction.

#include "assets/AssetRegistry.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <thread>

using namespace engine::assets;

namespace {

	// <root>/libs/engine/assets/ColonistDirectionalAssets.test.cpp -> walk up to <root>.
	std::filesystem::path projectRoot() {
		std::filesystem::path p = __FILE__;
		return p.parent_path().parent_path().parent_path().parent_path();
	}

	// Complete-or-not, distinct from assets being absent: a load that never
	// finishes is a FAILURE, not a skip, so a real asset-load regression can't
	// silently stop being tested.
	bool loadWorld(AssetRegistry& reg) {
		reg.setSharedScriptsPath(projectRoot() / "assets" / "shared" / "scripts");
		reg.beginLoadAsync((projectRoot() / "assets" / "world").string());
		for (int i = 0; i < 1000 && !reg.isLoadComplete(); ++i) {
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
		return reg.isLoadComplete();
	}

} // namespace

TEST(ColonistDirectionalAssets, AllDirectionTemplatesAndMotionsResolve) {
	if (!std::filesystem::exists(projectRoot() / "assets" / "world")) {
		GTEST_SKIP() << "assets/world not found";
	}
	auto& reg = AssetRegistry::Get();
	reg.clear();
	ASSERT_TRUE(loadWorld(reg)) << "asset load did not complete within 10s";

	for (const char* name : {"Colonist_down", "Colonist_up", "Colonist_left", "Colonist_right"}) {
		SCOPED_TRACE(name);
		const renderer::TessellatedMesh* mesh = reg.getTemplate(name);
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
