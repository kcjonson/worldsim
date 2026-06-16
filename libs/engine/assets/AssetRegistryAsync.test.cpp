// Tests for AssetRegistry asynchronous loading (beginLoadAsync).

#include "assets/AssetRegistry.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <thread>

using namespace engine::assets;

namespace {

	// <root>/libs/engine/assets/AssetRegistryAsync.test.cpp -> walk up to <root>.
	std::filesystem::path projectRoot() {
		std::filesystem::path p = __FILE__;
		return p.parent_path().parent_path().parent_path().parent_path();
	}

} // namespace

TEST(AssetRegistryAsyncTest, BeginLoadAsyncPopulatesRegistry) {
	auto& reg = AssetRegistry::Get();
	reg.clear();

	const std::filesystem::path world = projectRoot() / "assets" / "world";
	if (!std::filesystem::exists(world)) {
		GTEST_SKIP() << "assets/world not found";
	}

	reg.setSharedScriptsPath(projectRoot() / "assets" / "shared" / "scripts");

	EXPECT_FALSE(reg.loadProgress().started.load());
	reg.beginLoadAsync(world.string());
	EXPECT_TRUE(reg.loadProgress().started.load());

	// Poll up to ~10s for the worker to finish.
	for (int i = 0; i < 1000 && !reg.isLoadComplete(); ++i) {
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}

	ASSERT_TRUE(reg.isLoadComplete());
	EXPECT_GT(reg.getDefinitionNames().size(), 0U);
	EXPECT_GT(reg.loadProgress().defsLoaded.load(), 0);
	EXPECT_EQ(reg.getValidationReport().errorCount(), 0); // shipped library is clean

	// clear() joins the worker and resets progress.
	reg.clear();
	EXPECT_FALSE(reg.loadProgress().started.load());
}
