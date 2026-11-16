// Settings Scene - game options.
//
// Options screen: a title, a kicker and the drawing settings, one switch (or choice) each, read
// from and written to world_sim::UserSettings; changes apply at once for the
// session (no cross-launch persistence yet). ESC, or Back, returns to the main
// menu.

#include "SceneTypes.h"
#include "UserSettings.h"
#include "scenes/shared/SalalUi.h"
#include <theme/Theme.h>

#include <application/App.h>
#include <primitives/Primitives.h>
#include <theme/Keys.h>
#include <ui/components/Button.h>
#include <ui/components/SegmentedControl.h>
#include <ui/components/Toggle.h>
#include <ui/shapes/Text.h>
#include <utils/Log.h>

#include <cstdio>
#include <string>

namespace {

	constexpr const char* kSceneName = "settings";
	constexpr float		  kColX = 80.0F;
	constexpr float		  kRowWidth = 480.0F;

	namespace tokens = world_sim::tokens;
	namespace family = ui::tokens::font::family;

	std::string number(float value) {
		char text[32];
		std::snprintf(text, sizeof(text), "%g", static_cast<double>(value));
		return text;
	}

	class SettingsScene : public app::Scene {
	  public:
		void onEnter(app::App& app) override {
			owner = &app;
			LOG_INFO(Game, "SettingsScene - Entering");
			theme::apply(ui());
			build();
		}

		void update(const app::FrameContext& frame) override {
			if (theme::key(frame.input, foundation::Key::Escape).pressed()) {
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
			const ui::Style	 description = world_sim::text(theme, {.size = tokens::fontSize::fsSm, .color = tokens::color::textDim});
			const ui::Style	 kicker = world_sim::text(
				 theme,
				 {.family = family::mono,
				  .size = tokens::fontSize::fs2xs,
				  .color = tokens::color::textFaint,
				  .tracking = tokens::letterSpacing::lsWider}
			 );

			const float space2 = theme.get(tokens::spacing::space2);
			const float space4 = theme.get(tokens::spacing::space4);
			const float space8 = theme.get(tokens::spacing::space8);

			// The title and Back stay put; the settings between them scroll when the window is short.
			ui::Ref<ui::Component> column = root().addChild<ui::Component>({
				.id = "settings",
				.size = {ui::percent(100.0F), ui::percent(100.0F)},
				.layout = {
					.display = ui::Display::Flex,
					.flexDirection = ui::FlexDirection::Column,
					.gap = space4,
					.padding = {space8, 0.0F, space8, kColX},
					.alignItems = ui::AlignItems::Start,
				},
			});
			column.addChild<ui::Text>({
				.id = "settings_title",
				.layout = {.flexShrink = 0.0F},
				.style = world_sim::text(
					theme,
					{.family = family::display,
					 .size = tokens::fontSize::fs4xl,
					 .color = tokens::color::textBright,
					 .tracking = tokens::letterSpacing::lsWider}
				),
				.text = "Settings",
			});
			column.addChild<ui::Text>({.id = "settings_kicker", .layout = {.flexShrink = 0.0F}, .style = kicker, .text = "// DRAWING SETTINGS"});

			ui::Ref<ui::Component> list = column.addChild<ui::Component>({
				.id = "settings_drawing",
				.size = {kRowWidth, ui::kAuto},
				.layout =
					{.display = ui::Display::Flex,
					 .flexDirection = ui::FlexDirection::Column,
					 .gap = space4,
					 .flexGrow = 1.0F},
				.overflow = ui::Overflow::Scroll,
			});

			const assets::SnappingConfig& snapping = assets::ConstructionRegistry::Get().snapping();
			world_sim::DrawingSettings&	  drawing = world_sim::UserSettings::Get().drawing;

			const auto addSwitch = [&](const char* id, const std::string& label, const std::string& line, bool& field) {
				ui::Ref<ui::Component> row = list.addChild<ui::Component>({
					.id = id,
					.layout = {.display = ui::Display::Flex, .flexDirection = ui::FlexDirection::Column, .gap = space2},
				});
				row.addChild<ui::Toggle>({
					.id = std::string(id) + "_toggle",
					.size = {kRowWidth, ui::kAuto},
					.label = label,
					.on = field,
					.onChange = [&field, label](bool on) {
						field = on;
						LOG_INFO(Game, "Setting: %s -> %s", label.c_str(), on ? "on" : "off");
					},
				});
				row.addChild<ui::Text>({
					.id = std::string(id) + "_line",
					.style = description,
					.text = line,
					.wrap = true,
				});
			};

			addSwitch(
				"drawing_angle_steps",
				"Angle steps",
				"Turn segments in " + number(snapping.angleIncrementDegrees) + " degree steps.",
				drawing.angleSteps
			);
			addSwitch(
				"drawing_guides_from_shape",
				"Guides from the shape",
				"Line up with the shape's own edges, parallel or square.",
				drawing.guidesFromShape
			);
			addSwitch(
				"drawing_align_existing",
				"Align to existing buildings",
				"Line up with nearby buildings' edges and corners.",
				drawing.alignToExistingBuildings
			);
			addSwitch(
				"drawing_guides_from_world_axes",
				"Guides from the world axes",
				"Line up north-south and east-west with points and corners.",
				drawing.guidesFromWorldAxes
			);
			addSwitch("drawing_crossings", "Crossings", "Snap to where two guides meet.", drawing.guideCrossings);
			addSwitch(
				"drawing_common_lengths",
				"Common lengths",
				"Match the lengths of edges already built.",
				drawing.commonLengths
			);
			addSwitch(
				"drawing_guide_distances",
				"Guide distances",
				"Show how far the cursor is along each guide.",
				drawing.guideDistances
			);
			addSwitch(
				"drawing_angle_readout",
				"Angle readout",
				"Show the angle of a corner that is not square.",
				drawing.angleReadout
			);
			addSwitch(
				"drawing_round_lengths",
				"Round lengths to " + number(snapping.lengthIncrementMeters) + " m",
				"Segment lengths come out round.",
				drawing.roundLengths
			);
			addSwitch(
				"drawing_key_guide",
				"Show key guide while drawing",
				"A small panel with the keys and what the lines mean.",
				drawing.showKeyGuide
			);

			ui::Ref<ui::Component> grid = list.addChild<ui::Component>({
				.id = "drawing_grid",
				.layout = {.display = ui::Display::Flex, .flexDirection = ui::FlexDirection::Column, .gap = space2},
			});
			grid.addChild<ui::Text>({
				.id = "drawing_grid_label",
				.style = world_sim::text(theme, {.size = tokens::fontSize::fsBase, .color = tokens::color::text}),
				.text = "Grid spacing for the rectangle tool",
			});
			grid.addChild<ui::SegmentedControl>({
				.id = "drawing_grid_spacing",
				.options = {"1 m", "0.5 m"},
				.selected = drawing.rectangleGridSpacingMeters < 1.0F ? 1 : 0,
				.onChange =
					[&drawing](int index) {
						drawing.rectangleGridSpacingMeters = index == 1 ? 0.5F : 1.0F;
						LOG_INFO(Game, "Setting: rectangle grid spacing -> %g m", drawing.rectangleGridSpacingMeters);
					},
			});
			grid.addChild<ui::Text>({
				.id = "drawing_grid_line",
				.style = description,
				.text = "Rectangle corners snap to this grid.",
				.wrap = true,
			});

			column.addChild<ui::Button>({
				.id = "settings_back",
				.layout = {.flexShrink = 0.0F},
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
