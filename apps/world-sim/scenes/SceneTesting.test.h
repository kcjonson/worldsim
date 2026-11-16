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
#include <input/InputTypes.h>
#include <platform/HeadlessPlatform.h>
#include <testing/HeadlessLayout.h>
#include <theme/Fonts.h>
#include <theme/InterfaceScale.h>
#include <ui/Component.h>
#include <ui/Context.h>
#include <utils/ResourcePath.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <format>
#include <initializer_list>
#include <limits>
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

	// A scroller whose bar is expected, by its id, on canvases shorter than `belowCanvasHeight` ui px.
	struct ExpectedScrollbar {
		std::string_view id;
		float			 belowCanvasHeight = std::numeric_limits<float>::infinity();
	};

	class SceneTest : public ::testing::Test {
	  protected:
		static void SetUpTestSuite() {
			Foundation::setResourceRoots({projectRoot()});
			assets::initializeAssetSystem(false);
			ASSERT_NE(assets::AssetRegistry::Get().getDefinition("CraftingSpot"), nullptr);
			ASSERT_GT(assets::RecipeRegistry::Get().size(), 0U);
			ASSERT_TRUE(assets::ConstructionRegistry::Get().load((projectRoot() / "assets" / "config" / "construction").string()));
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

		// The top scene's component with this id, if it and everything above it is shown.
		[[nodiscard]] ui::Component* shown(std::string_view id) const {
			app::Scene* top = app->scenes().top();
			if (top == nullptr || !top->hasUi()) {
				return nullptr;
			}
			const ui::Context&			 context = top->ui();
			std::vector<ui::ComponentId> pending = context.getRoots();
			while (!pending.empty()) {
				ui::Component* component = context.find(pending.back());
				pending.pop_back();
				if (component == nullptr || !component->isShown()) {
					continue;
				}
				if (component->getId() == id) {
					return component;
				}
				pending.insert(pending.end(), component->getChildren().begin(), component->getChildren().end());
			}
			return nullptr;
		}

		// A player's click on the shown component with this id, then a frame for it to land. A frame first, so the
		// layout is the one the current window and interface size give, whatever a lint left.
		void press(std::string_view id) {
			frame();
			const ui::Component* target = shown(id);
			ASSERT_NE(target, nullptr) << id << " isn't shown";
			const Foundation::Rect bounds = target->screenBounds();
			const float			   scale = app->uiLayoutScale();
			const glm::vec2		   point{(bounds.x + (bounds.width * 0.5F)) * scale, (bounds.y + (bounds.height * 0.5F)) * scale};
			using Type = foundation::InputEvent::Type;
			for (const Type type : {Type::PointerMove, Type::PointerDown, Type::PointerUp}) {
				platform->push({.type = type, .position = point});
			}
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

		// The top scene's scrollers that show a bar at any window size and interface size other than those in
		// `expected` on the canvases they're expected on.
		[[nodiscard]] std::string unexpectedScrollbars(std::initializer_list<ExpectedScrollbar> expected) {
			return everywhere([this, expected](const WindowSize& window) {
				(void)app::lintAt(*app, *platform, window.width, window.height);
				const float				 canvasHeight = app->scenes().top()->ui().getCanvas().y;
				std::vector<std::string> found;
				for (const std::string& path : app::scrollbarsShown(*app)) {
					const std::string_view id = std::string_view(path).substr(path.rfind('/') + 1);
					if (std::ranges::none_of(expected, [id, canvasHeight](const ExpectedScrollbar& bar) {
							return bar.id == id && canvasHeight < bar.belowCanvasHeight;
						})) {
						found.push_back(std::format("{} shows a scrollbar on a canvas {:g} tall", path, canvasHeight));
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
