// Settings Scene - game options.
//
// Minimal options screen: a title, a kicker and a toggle for each option, read
// from and written to world_sim::UserSettings; changes apply at once for the
// session (no cross-launch persistence yet). ESC, or Back, returns to the main
// menu.

#include "SceneTypes.h"
#include "UserSettings.h"
#include "scenes/shared/SalalUi.h"
#include <theme/Theme.h>

#include <app/Keys.h>
#include <application/App.h>
#include <primitives/Primitives.h>
#include <ui/components/Button.h>
#include <ui/components/Toggle.h>
#include <ui/shapes/Text.h>
#include <utils/Log.h>

#include <string>

namespace {

	constexpr const char* kSceneName = "settings";
	constexpr float		  kColX = 80.0F;
	constexpr float		  kRowWidth = 480.0F;

	namespace tokens = world_sim::tokens;
	namespace family = ui::tokens::font::family;

	class SettingsScene : public app::Scene {
	  public:
		void onEnter(app::App& app) override {
			owner = &app;
			LOG_INFO(Game, "SettingsScene - Entering");
			theme::apply(ui());
			build();
		}

		void update(const app::FrameContext& frame) override {
			if (engine::key(frame.input, foundation::Key::Escape).pressed()) {
				back();
			}
		}

		void render(float /*alpha*/) override { Renderer::Primitives::clear(ui().getTheme().get(tokens::color::bgVoid)); }

		void onExit() override { LOG_INFO(Game, "SettingsScene - Exiting"); }

		std::string exportState() const override { return R"({"scene": "settings"})"; }
		const char* name() const override { return kSceneName; }

	  private:
		void back() { owner->scenes().switchTo(world_sim::scenes::MainMenu.name); }

		void build() {
			const ui::Theme& theme = ui().getTheme();
			const ui::Style	 kicker = world_sim::text(
				 theme,
				 {.family = family::mono,
				  .size = tokens::fontSize::fs2xs,
				  .color = tokens::color::textFaint,
				  .tracking = tokens::letterSpacing::lsWider}
			 );

			ui::Ref<ui::Component> column = root().addChild<ui::Component>({
				.id = "settings",
				.size = {ui::percent(100.0F), ui::percent(100.0F)},
				.layout = {
					.display = ui::Display::Flex,
					.flexDirection = ui::FlexDirection::Column,
					.gap = theme.get(tokens::spacing::space4),
					.padding = {0.0F, 0.0F, 0.0F, kColX},
					.alignItems = ui::AlignItems::Start,
					.justifyContent = ui::JustifyContent::Center
				},
			});
			column.addChild<ui::Text>({
				.id = "settings_title",
				.style = world_sim::text(
					theme,
					{.family = family::display,
					 .size = tokens::fontSize::fs4xl,
					 .color = tokens::color::textBright,
					 .tracking = tokens::letterSpacing::lsWider}
				),
				.text = "Settings",
			});
			column.addChild<ui::Text>({.id = "settings_kicker", .style = kicker, .text = "// CONSTRUCTION"});

			column.addChild<ui::Toggle>({
				.id = "settings_align_existing",
				.size = {kRowWidth, ui::kAuto},
				.label = "Align to existing foundations",
				.on = world_sim::UserSettings::Get().alignSnapToExistingFoundations,
				.onChange = [](bool on) {
					world_sim::UserSettings::Get().alignSnapToExistingFoundations = on;
					LOG_INFO(Game, "Setting: align snap to existing foundations -> %s", on ? "on" : "off");
				},
			});
			column.addChild<ui::Button>({
				.id = "settings_back",
				.margin = {theme.get(tokens::spacing::space4), 0.0F, 0.0F, 0.0F},
				.label = "Back",
				.icon = "chevronLeft",
				.variant = ui::ButtonVariant::Ghost,
				.onPress = [this] { back(); },
			});
			root().addChild<ui::Text>({
				.id = "settings_hint",
				.layout = {.position = ui::Position::Absolute, .inset = {.bottom = 16.0F, .left = kColX}},
				.style = kicker,
				.pointerEvents = ui::PointerEvents::None,
				.text = "Press ESC to return to menu",
			});
		}

		app::App* owner = nullptr;
	};

} // namespace

// Export scene info for registry
namespace world_sim::scenes {
	extern const world_sim::SceneInfo Settings = {kSceneName, []() { return std::make_unique<SettingsScene>(); }};
} // namespace world_sim::scenes
