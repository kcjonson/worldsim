#pragma once

// What the scene layout tests share: the App on salal's headless platform with the game's own ui scale, minimum
// window and fonts, and no settings file, since a player's saved interface size would move every canvas; the
// asset library and construction config the game loads, read from the source tree; and the top scene linted at
// the window sizes and interface sizes the players have, through salal's HeadlessLayout.

#include "BaseShader.h"
#include "SceneTypes.h"

#include <application/App.h>
#include <assets/AssetBootstrap.h>
#include <assets/AssetRegistry.h>
#include <assets/ConstructionRegistry.h>
#include <assets/RecipeRegistry.h>
#include <platform/HeadlessPlatform.h>
#include <testing/HeadlessLayout.h>
#include <theme/Fonts.h>
#include <theme/InterfaceScale.h>
#include <utils/ResourcePath.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <format>
#include <initializer_list>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace world_sim::testing {

	// <root>/apps/world-sim/scenes/SceneTesting.test.h
	inline std::filesystem::path projectRoot() {
		return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
	}

	struct WindowSize {
		int width = 0;
		int height = 0;
	};

	// The smallest window, the reference, 1080p, a 21:9 ultrawide and one taller than the reference, in device pixels.
	inline std::array<WindowSize, 5> windowSizes() {
		const app::UiScaleConfig scale = theme::uiScaleConfig();
		return {{
			{.width = theme::kMinWindowWidth, .height = theme::kMinWindowHeight},
			{.width = static_cast<int>(scale.referenceWidth), .height = static_cast<int>(scale.referenceHeight)},
			{.width = 1920, .height = 1080},
			{.width = 3440, .height = 1440},
			{.width = 1600, .height = 1200},
		}};
	}

	// The largest first, so the canvases under the reference are reached as a player reaches them.
	inline std::array<float, 3> interfaceSizes() {
		return {theme::kInterfaceSizes.back(), theme::kDefaultInterfaceSize, theme::kInterfaceSizes.front()};
	}

	class SceneTest : public ::testing::Test {
	  protected:
		static void SetUpTestSuite() {
			Foundation::setResourceRoots({projectRoot()});
			assets::initializeAssetSystem(false);
			assets::ConstructionRegistry::Get().load((projectRoot() / "assets" / "config" / "construction").string());
		}

		static void TearDownTestSuite() {
			assets::ConstructionRegistry::Get().clear();
			assets::RecipeRegistry::Get().clear();
			assets::AssetRegistry::Get().clear();
		}

		void SetUp() override {
			const WindowSize reference = windowSizes()[1];
			auto			 headless = std::make_unique<app::HeadlessPlatform>(reference.width, reference.height);
			headless->setMinimumSize(theme::kMinWindowWidth, theme::kMinWindowHeight);
			platform = headless.get();
			app = app::App::create(
				{.applicationName = "world-sim-tests",
				 .uiScale = theme::uiScaleConfig(),
				 .baseShader = base_shader::source(),
				 .fontAtlases = theme::kFontAtlases,
				 .defaultFont = theme::fontDefault,
				 .resourceRoots = {projectRoot()}},
				std::move(headless)
			);
			ASSERT_NE(app, nullptr);
			addScenes(app->scenes());
		}

		void TearDown() override { app.reset(); }

		bool frame() {
			platform->clock().advance(1.0 / 60.0);
			return app->tick();
		}

		void go(std::string_view scene) {
			ASSERT_TRUE(app->scenes().switchTo(scene));
			frame();
		}

		// The top scene at every window size and interface size, a line per violation led by where it was found; empty
		// when it holds together on all of them.
		[[nodiscard]] std::string lintEverywhere() {
			return everywhere([this](const WindowSize& window) {
				const std::string		 lines = app::lintAt(*app, *platform, window.width, window.height);
				std::vector<std::string> found;
				std::istringstream		 each(lines);
				for (std::string line; std::getline(each, line);) {
					found.push_back(line);
				}
				return found;
			});
		}

		// The top scene's scrollers that show a bar at any window size and interface size, other than those in
		// `expected`, the ones whose content is meant to scroll.
		[[nodiscard]] std::string unexpectedScrollbars(std::initializer_list<std::string_view> expected) {
			return everywhere([this, expected](const WindowSize& window) {
				(void)app::lintAt(*app, *platform, window.width, window.height);
				std::vector<std::string> found;
				for (const std::string& path : app::scrollbarsShown(*app)) {
					if (std::ranges::none_of(expected, [&path](std::string_view allowed) { return path.ends_with(allowed); })) {
						found.push_back(path + " shows a scrollbar");
					}
				}
				return found;
			});
		}

		app::HeadlessPlatform*	  platform = nullptr;
		std::unique_ptr<app::App> app;

	  private:
		template <typename Check>
		std::string everywhere(const Check& check) {
			std::string found;
			for (const WindowSize& window : windowSizes()) {
				for (const float size : interfaceSizes()) {
					app->setUiScale(size);
					for (const std::string& line : check(window)) {
						found += std::format("{}x{} at {:g}: {}\n", window.width, window.height, size, line);
					}
				}
			}
			app->setUiScale(theme::kDefaultInterfaceSize);
			return found;
		}
	};

} // namespace world_sim::testing
