// Main Menu Scene - the title screen.
//
// A left-aligned column, centred top to bottom: the identity (diamond glyph,
// WORLD-SIM and the tagline), a kicker, and the menu's rows, each an icon and
// a label. A footer under it. The decorative planet docks off the right screen
// edge behind a scrim.

#include "GameStartConfig.h"
#include "NewGameSetup.h"
#include "SceneTypes.h"
#include "scenes/shared/DecorativePlanet.h"
#include "scenes/shared/SalalUi.h"
#include "scenes/shared/Starfield.h"
#include <theme/Theme.h>

#include <application/App.h>
#include <primitives/Primitives.h>
#include <ui/components/Control.h>
#include <ui/shapes/Icon.h>
#include <ui/shapes/Text.h>
#include <ui/theme/Tokens.h>
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

	// The title screen's row: the display face, the primary row's label in the bright accent, and a
	// disabled row faded whole by salal's disabled state. Under the pointer or a player's visible
	// focus it lights: the accent bar, the chevron, the icon in the accent and the label stepped right.
	class MenuRow : public ui::Control {
	  public:
		struct Args {
			UI_COMMON_ARGS
			std::string			  label;
			std::string			  icon;
			bool				  primary = false;
			std::function<void()> onPress;
		};

		explicit MenuRow(const Args& args)
			: label(args.label),
			  iconName(args.icon),
			  primary(args.primary),
			  onPress(args.onPress) {}

		[[nodiscard]] const char* typeName() const override { return "MenuRow"; }
		[[nodiscard]] ui::StyleId themeStyle() const override { return ui::styles::listRow.theme; }

	  protected:
		void onMount() override {
			Control::onMount();
			const ui::Theme& theme = getContext()->getTheme();
			bar = addChild<ui::Rectangle>({
				.size = {2.0F, ui::kAuto},
				.layout = {.position = ui::Position::Absolute, .inset = {.top = 0.0F, .bottom = 0.0F, .left = 0.0F}},
				.style = {.backgroundColor = theme.get(tokens::color::accent)},
				.pointerEvents = ui::PointerEvents::None,
			});
			bracket = addChild<ui::Icon>({.size = {14.0F, 14.0F}, .pointerEvents = ui::PointerEvents::None, .name = "chevronRight"});
			icon = addChild<ui::Icon>(
				{.size = {18.0F, 18.0F}, .margin = {0.0F, 0.0F, 0.0F, 4.0F}, .pointerEvents = ui::PointerEvents::None, .name = iconName}
			);
			text = addChild<ui::Text>({.margin = {0.0F, 0.0F, 0.0F, 8.0F}, .pointerEvents = ui::PointerEvents::None, .text = label});
			setOnFocus([this](ui::Component& /*self*/) { refresh(); });
			setOnBlur([this](ui::Component& /*self*/) { refresh(); });
			refresh();
		}

		void handlePointer(ui::PointerEvent& event) override {
			Control::handlePointer(event);
			if (event.type == ui::PointerEvent::Type::Enter || event.type == ui::PointerEvent::Type::Leave) {
				pointerInside = event.type == ui::PointerEvent::Type::Enter;
				refresh();
			}
		}

		void activate() override {
			if (onPress) {
				onPress();
			}
		}

	  private:
		void refresh() {
			const ui::Theme& theme = getContext()->getTheme();
			const bool		 lit = isEnabled() && (pointerInside || isFocusVisible());

			ui::Layout row = getLayout();
			row.display = ui::Display::Flex;
			row.flexDirection = ui::FlexDirection::Row;
			row.alignItems = ui::AlignItems::Center;
			row.padding = {0.0F, 0.0F, 0.0F, theme.get(lit ? tokens::spacing::space6 : tokens::spacing::space4)};
			setLayout(row);

			bar.get()->setVisible(lit);
			Foundation::Color chevron = theme.get(tokens::color::accent);
			chevron.a = lit ? chevron.a : 0.0F;
			bracket.get()->setStyle({.color = chevron});
			icon.get()->setStyle({.color = theme.get(lit ? tokens::color::accent : tokens::color::textFaint)});

			ui::ColorToken labelColor = tokens::color::textDim;
			if (isEnabled() && primary) {
				labelColor = tokens::color::accentBright;
			} else if (lit) {
				labelColor = tokens::color::textBright;
			}
			text.get()->setStyle(world_sim::text(
				theme,
				{.family = family::display, .size = tokens::fontSize::fsXl, .color = labelColor, .tracking = tokens::letterSpacing::lsWide}
			));
		}

		std::string			   label;
		std::string			   iconName;
		bool				   primary;
		std::function<void()>  onPress;
		bool				   pointerInside = false;
		ui::Ref<ui::Rectangle> bar;
		ui::Ref<ui::Icon>	   bracket;
		ui::Ref<ui::Icon>	   icon;
		ui::Ref<ui::Text>	   text;
	};

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
			item({.id = "menu_new_game", .label = "New Game", .icon = "play", .primary = true, .onPress = [this] {
					  world_sim::NewGameSetup::Reset();
					  owner->scenes().switchTo(world_sim::scenes::ScenarioSelect.name);
				  }});
			item({.id = "menu_continue", .enabled = false, .label = "Continue", .icon = "refresh"});
			item({.id = "menu_load_game", .enabled = false, .label = "Load Game", .icon = "save"});
			// Quick Start needs the prebuilt planet shipped next to the exe; if the build step that bakes
			// it hasn't run, the entry is disabled rather than dropping the player into a dead loading
			// screen.
			item({
				.id = "menu_quick_start",
				.enabled = Foundation::findResource(world_sim::kQuickstartPlanetResource).has_value(),
				.label = "Quick Start",
				.icon = "bolt",
				.onPress = [this] {
					auto config = std::make_unique<world_sim::GameStartConfig>();
					config->source = world_sim::GameStartConfig::Source::QuickStart;
					world_sim::GameStartConfig::SetPending(std::move(config));
					owner->scenes().switchTo(world_sim::scenes::GameLoading.name);
				},
			});
			item({.id = "menu_settings", .label = "Settings", .icon = "gear", .onPress = [this] {
					  owner->scenes().switchTo(world_sim::scenes::Settings.name);
				  }});
			item({.id = "menu_credits", .enabled = false, .label = "Credits", .icon = "users"});
			item({.id = "menu_exit", .label = "Exit", .icon = "close", .onPress = [this] { owner->requestExit(); }});
			const ui::Style footer =
				world_sim::text(theme, {.family = family::mono, .size = tokens::fontSize::fs2xs, .color = tokens::color::textFaint});
			root().addChild<ui::Text>({
				.id = "menu_version",
				.layout = {.position = ui::Position::Absolute, .inset = {.bottom = 16.0F, .left = kColX}},
				.style = footer,
				.pointerEvents = ui::PointerEvents::None,
				.text = "v0.1.0 - Development Build",
			});
			root().addChild<ui::Text>({
				.id = "menu_system",
				.layout = {.position = ui::Position::Absolute, .inset = {.right = kColX, .bottom = 16.0F}},
				.style = footer,
				.pointerEvents = ui::PointerEvents::None,
				.text = "Star system: Maed - Sector 28-B",
			});
		}

		void item(MenuRow::Args args) {
			args.size = {ui::percent(100.0F), kRowHeight};
			menu.addChild<MenuRow>(args);
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
