// Main Menu Scene - the title screen.
//
// A left-aligned column, centred top to bottom: the identity (diamond glyph,
// WORLD-SIM and the tagline), a kicker, and the menu's rows, each an icon,
// a label and a hint. A footer under it. The decorative planet docks off the
// right screen edge behind a scrim.

#include "GameStartConfig.h"
#include "NewGameSetup.h"
#include "SceneTypes.h"
#include "scenes/shared/DecorativePlanet.h"
#include "scenes/shared/SalalUi.h"
#include "scenes/shared/Starfield.h"
#include <theme/Theme.h>

#include <application/App.h>
#include <primitives/Primitives.h>
#include <ui/components/ListRow.h>
#include <ui/shapes/Text.h>
#include <utils/Log.h>
#include <utils/ResourcePath.h>

#include <algorithm>
#include <functional>
#include <string>

namespace {

	constexpr const char* kSceneName = "main_menu";
	constexpr float		  kColX = 80.0F;
	constexpr float		  kRowWidth = 500.0F;
	constexpr float		  kRowHeight = 44.0F;

	namespace tokens = world_sim::tokens;
	namespace family = ui::tokens::font::family;

	class MainMenuScene : public app::Scene {
	  public:
		void onEnter(app::App& app) override {
			owner = &app;
			LOG_INFO(Game, "MainMenuScene - Entering");
			theme::apply(ui());
			build();
		}

		void update(const app::FrameContext& frame) override { planet.update(static_cast<float>(frame.dt)); }

		void render(float /*alpha*/) override {
			const ui::Theme&		theme = ui().getTheme();
			const Foundation::Color backdrop = theme.get(tokens::color::bgVoid);
			Renderer::Primitives::clear(backdrop);

			const float screenW = Renderer::Primitives::PercentWidth(100.0F);
			const float screenH = Renderer::Primitives::PercentHeight(100.0F);
			world_sim::renderStarfield(theme, static_cast<int>(screenW), static_cast<int>(screenH), 11U);

			// Docked off the right screen edge, behind the menu (its scrim keeps the text legible), sized
			// against the viewport with a floor for small windows; a quarter of it hangs off the edge.
			const float planetSize = std::max(kPlanetMinSize, screenH * 0.55F);
			const float overhang = planetSize * 0.25F;
			planet.render(
				{screenW - (planetSize - overhang), (screenH - planetSize) * 0.5F, planetSize, planetSize}, screenW, screenH, backdrop
			);
		}

		void onExit() override { LOG_INFO(Game, "MainMenuScene - Exiting"); }

		std::string exportState() const override { return R"({"scene": "main_menu"})"; }
		const char* name() const override { return kSceneName; }

	  private:
		static constexpr float kPlanetMinSize = 640.0F;

		void build() {
			const ui::Theme& theme = ui().getTheme();
			const float		 space3 = theme.get(tokens::spacing::space3);

			ui::Ref<ui::Component> column = root().addChild<ui::Component>({
				.id = "main_menu",
				.size = {ui::percent(100.0F), ui::percent(100.0F)},
				.layout = {
					.display = ui::Display::Flex,
					.flexDirection = ui::FlexDirection::Column,
					.gap = space3,
					.padding = {0.0F, 0.0F, 0.0F, kColX},
					.alignItems = ui::AlignItems::Start,
					.justifyContent = ui::JustifyContent::Center
				},
			});

			// The identity: the diamond and the title on one row, the tagline below.
			ui::Ref<ui::Component> identity = column.addChild<ui::Component>({
				.id = "main_menu_identity",
				.layout = {
					.display = ui::Display::Flex, .flexDirection = ui::FlexDirection::Column, .gap = theme.get(tokens::spacing::space2)
				},
			});
			ui::Ref<ui::Component> titleRow = identity.addChild<ui::Component>({
				.id = "main_menu_title_row",
				.layout = {.display = ui::Display::Flex, .gap = 16.0F, .alignItems = ui::AlignItems::Center},
			});
			titleRow.addChild<world_sim::Diamond>({.id = "menu_diamond", .radius = 11.0F});
			titleRow.addChild<ui::Text>({
				.id = "menu_title",
				.style = world_sim::text(
					theme,
					{.family = family::display,
					 .size = tokens::fontSize::fs4xl,
					 .color = tokens::color::textBright,
					 .tracking = tokens::letterSpacing::lsWider}
				),
				.text = "WORLD-SIM",
			});
			identity.addChild<ui::Text>({
				.id = "menu_tagline",
				.style = world_sim::text(
					theme,
					{.family = family::mono,
					 .size = tokens::fontSize::fsSm,
					 .color = tokens::color::accent,
					 .tracking = tokens::letterSpacing::lsWide,
					 .transform = Foundation::TextTransform::Uppercase}
				),
				.text = "Prospecting Expedition 28-B",
			});

			column.addChild<ui::Text>({
				.id = "menu_kicker",
				.style = world_sim::text(
					theme,
					{.family = family::mono,
					 .size = tokens::fontSize::fs2xs,
					 .color = tokens::color::textFaint,
					 .tracking = tokens::letterSpacing::lsWider}
				),
				.text = "// MAIN MENU",
			});

			menu = column.addChild<ui::Component>({
				.id = "main_menu_items",
				.size = {kRowWidth, ui::kAuto},
				.layout = {.display = ui::Display::Flex, .flexDirection = ui::FlexDirection::Column, .gap = 2.0F},
			});
			item("menu_new_game", "New Game", "Begin a new expedition", "play", true, [this] {
				world_sim::NewGameSetup::Reset();
				owner->scenes().switchTo(world_sim::scenes::ScenarioSelect.name);
			});
			item("menu_continue", "Continue", "Resume your last colony", "refresh", false, nullptr);
			item("menu_load_game", "Load Game", "Restore a saved expedition", "save", false, nullptr);
			// Quick Start needs the prebuilt planet shipped next to the exe; if the build step that bakes
			// it hasn't run, the entry is disabled rather than dropping the player into a dead loading
			// screen.
			const bool quickstartReady = Foundation::findResource(world_sim::kQuickstartPlanetResource).has_value();
			item(
				"menu_quick_start",
				"Quick Start",
				quickstartReady ? "Jump in on the prebuilt planet" : "Unavailable - run the quickstart-planet build target",
				"bolt",
				quickstartReady,
				[this] {
					auto config = std::make_unique<world_sim::GameStartConfig>();
					config->source = world_sim::GameStartConfig::Source::QuickStart;
					world_sim::GameStartConfig::SetPending(std::move(config));
					owner->scenes().switchTo(world_sim::scenes::GameLoading.name);
				}
			);
			item("menu_settings", "Settings", "Graphics, audio, controls", "gear", true, [this] {
				owner->scenes().switchTo(world_sim::scenes::Settings.name);
			});
			item("menu_credits", "Credits", "The crew behind the crew", "users", false, nullptr);
			item("menu_exit", "Exit", "Quit to desktop", "close", true, [this] { owner->requestExit(); });
			const ui::Style footer =
				world_sim::text(theme, {.family = family::mono, .size = tokens::fontSize::fs2xs, .color = tokens::color::textFaint});
			root().addChild<ui::Text>({
				.id = "menu_version",
				.layout = {.position = ui::Position::Absolute, .inset = {.bottom = 16.0F, .left = kColX}},
				.style = footer,
				.text = "v0.1.0 - Development Build",
			});
			root().addChild<ui::Text>({
				.id = "menu_system",
				.layout = {.position = ui::Position::Absolute, .inset = {.right = kColX, .bottom = 16.0F}},
				.style = footer,
				.text = "Star system: Maed - Sector 28-B",
			});
		}

		ui::Ref<ui::ListRow>
		item(const char* id, const char* label, const std::string& hint, const char* icon, bool enabled, std::function<void()> action) {
			return menu.addChild<ui::ListRow>({
				.id = id,
				.enabled = enabled,
				.size = {ui::percent(100.0F), kRowHeight},
				.label = label,
				.trailing = hint,
				.icon = icon,
				.onPress = std::move(action),
			});
		}

		app::App*					owner = nullptr;
		world_sim::DecorativePlanet planet;
		ui::Ref<ui::Component>		menu;
	};

} // namespace

// Export scene info for registry
namespace world_sim::scenes {
	extern const world_sim::SceneInfo MainMenu = {kSceneName, []() { return std::make_unique<MainMenuScene>(); }};
} // namespace world_sim::scenes
