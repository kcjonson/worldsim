// Icon Scene - Demonstrates the Icon component for SVG rendering
// Shows icons at different sizes, with different tints, and in a layout

#include <GL/glew.h>

#include "SceneTypes.h"
#include <components/icon/Icon.h>
#include <graphics/Color.h>
#include <input/InputEvent.h>
#include <layout/LayoutContainer.h>
#include <layout/LayoutTypes.h>
#include <memory>
#include <primitives/Primitives.h>
#include <scene/Scene.h>
#include <scene/SceneManager.h>
#include <shapes/Shapes.h>
#include <theme/Theme.h>
#include <utils/Log.h>

namespace {

	constexpr const char* kSceneName = "icon";

	// SVG paths for demo - relative to working directory (build/apps/ui-sandbox)
	constexpr const char* kBerryPath = "../../../assets/world/misc/Berry/berry.svg";
	constexpr const char* kColonistPath = "../../../assets/world/colonists/Colonist/colonist.svg";
	constexpr const char* kBushPath = "../../../assets/world/flora/BerryBush/berry_bush.svg";
	constexpr const char* kStonePath = "../../../assets/world/misc/SmallStone/small_stone.svg";
	constexpr const char* kStickPath = "../../../assets/world/misc/Stick/stick.svg";

	class IconScene : public engine::IScene {
	  public:
		const char* getName() const override { return kSceneName; }
		std::string exportState() override { return "{}"; }

		void onEnter() override {
			using namespace UI;
			using namespace Foundation;

			// Create title
			title = std::make_unique<Text>(Text::Args{
				.position = {50.0F, 30.0F},
				.text = "Icon Component Demo",
				.style = {.color = Color::white(), .fontSize = 20.0F},
				.id = "title"});

			// ================================================================
			// Demo 1: Basic icons at default size
			// ================================================================
			label1 = std::make_unique<Text>(Text::Args{
				.position = {50.0F, 70.0F},
				.text = "1. Icons at Default Size (16px):",
				.style = {.color = Color::yellow(), .fontSize = 14.0F},
				.id = "label_1"});

			icon1 = std::make_unique<Icon>(Icon::Args{
				.position = {50.0F, 95.0F},
				.size = Theme::Icons::defaultSize,
				.svgPath = kBerryPath,
				.id = "icon_berry"});

			icon2 = std::make_unique<Icon>(Icon::Args{
				.position = {80.0F, 95.0F},
				.size = Theme::Icons::defaultSize,
				.svgPath = kStonePath,
				.id = "icon_stone"});

			icon3 = std::make_unique<Icon>(Icon::Args{
				.position = {110.0F, 95.0F},
				.size = Theme::Icons::defaultSize,
				.svgPath = kStickPath,
				.id = "icon_stick"});

			// ================================================================
			// Demo 2: Icons at different sizes
			// ================================================================
			label2 = std::make_unique<Text>(Text::Args{
				.position = {50.0F, 140.0F},
				.text = "2. Icons at Different Sizes:",
				.style = {.color = Color::yellow(), .fontSize = 14.0F},
				.id = "label_2"});

			iconSmall = std::make_unique<Icon>(Icon::Args{
				.position = {50.0F, 165.0F},
				.size = Theme::Icons::smallSize,
				.svgPath = kColonistPath,
				.id = "icon_small"});

			iconDefault = std::make_unique<Icon>(Icon::Args{
				.position = {80.0F, 165.0F},
				.size = Theme::Icons::defaultSize,
				.svgPath = kColonistPath,
				.id = "icon_default"});

			iconLarge = std::make_unique<Icon>(Icon::Args{
				.position = {110.0F, 165.0F},
				.size = Theme::Icons::largeSize,
				.svgPath = kColonistPath,
				.id = "icon_large"});

			iconXLarge = std::make_unique<Icon>(Icon::Args{
				.position = {150.0F, 165.0F},
				.size = 48.0F,
				.svgPath = kColonistPath,
				.id = "icon_xlarge"});

			// ================================================================
			// Demo 3: Icons with tinting
			// ================================================================
			label3 = std::make_unique<Text>(Text::Args{
				.position = {50.0F, 240.0F},
				.text = "3. Icons with Tinting:",
				.style = {.color = Color::yellow(), .fontSize = 14.0F},
				.id = "label_3"});

			iconRed = std::make_unique<Icon>(Icon::Args{
				.position = {50.0F, 265.0F},
				.size = 32.0F,
				.svgPath = kBushPath,
				.tint = Color(1.0F, 0.3F, 0.3F, 1.0F), // Red tint
				.id = "icon_red"});

			iconGreen = std::make_unique<Icon>(Icon::Args{
				.position = {100.0F, 265.0F},
				.size = 32.0F,
				.svgPath = kBushPath,
				.tint = Color(0.3F, 1.0F, 0.3F, 1.0F), // Green tint
				.id = "icon_green"});

			iconBlue = std::make_unique<Icon>(Icon::Args{
				.position = {150.0F, 265.0F},
				.size = 32.0F,
				.svgPath = kBushPath,
				.tint = Color(0.3F, 0.5F, 1.0F, 1.0F), // Blue tint
				.id = "icon_blue"});

			iconWhite = std::make_unique<Icon>(Icon::Args{
				.position = {200.0F, 265.0F},
				.size = 32.0F,
				.svgPath = kBushPath,
				.tint = Color::white(), // No tint (white)
				.id = "icon_white"});

			// ================================================================
			// Demo 4: Icons in a LayoutContainer
			// ================================================================
			label4 = std::make_unique<Text>(Text::Args{
				.position = {300.0F, 70.0F},
				.text = "4. Icons in Horizontal Layout:",
				.style = {.color = Color::yellow(), .fontSize = 14.0F},
				.id = "label_4"});

			layoutContainer = std::make_unique<LayoutContainer>(LayoutContainer::Args{
				.position = {300.0F, 95.0F},
				.size = {0.0F, 0.0F}, // Auto-size
				.direction = Direction::Horizontal,
				.vAlign = VAlign::Center,
				.id = "icon_layout"});

			layoutContainer->addChild(Icon(Icon::Args{
				.size = 24.0F,
				.svgPath = kBerryPath,
				.margin = 4.0F,
			}));

			layoutContainer->addChild(Icon(Icon::Args{
				.size = 24.0F,
				.svgPath = kStonePath,
				.margin = 4.0F,
			}));

			layoutContainer->addChild(Icon(Icon::Args{
				.size = 24.0F,
				.svgPath = kStickPath,
				.margin = 4.0F,
			}));

			layoutContainer->addChild(Icon(Icon::Args{
				.size = 24.0F,
				.svgPath = kColonistPath,
				.margin = 4.0F,
			}));

			layoutContainer->addChild(Icon(Icon::Args{
				.size = 24.0F,
				.svgPath = kBushPath,
				.margin = 4.0F,
			}));

			// Force layout calculation
			layoutContainer->layout(Rect{300.0F, 95.0F, 400.0F, 100.0F});

			// ================================================================
			// Instructions
			// ================================================================
			instructions = std::make_unique<Text>(Text::Args{
				.position = {50.0F, 350.0F},
				.text = "Icons render SVG assets via tessellation | Tinting modulates the fill color",
				.style = {.color = Color(0.6F, 0.6F, 0.7F, 1.0F), .fontSize = 12.0F},
				.id = "instructions"});

			LOG_INFO(UI, "Icon scene initialized");
		}

		void onExit() override {
			title.reset();
			label1.reset();
			label2.reset();
			label3.reset();
			label4.reset();
			instructions.reset();
			icon1.reset();
			icon2.reset();
			icon3.reset();
			iconSmall.reset();
			iconDefault.reset();
			iconLarge.reset();
			iconXLarge.reset();
			iconRed.reset();
			iconGreen.reset();
			iconBlue.reset();
			iconWhite.reset();
			layoutContainer.reset();
			LOG_INFO(UI, "Icon scene exited");
		}

		bool handleInput(UI::InputEvent& /*event*/) override {
			return false;
		}

		void update(float /*deltaTime*/) override {
			// No animation for now
		}

		void render() override {
			// Clear background
			glClearColor(0.10F, 0.10F, 0.13F, 1.0F);
			glClear(GL_COLOR_BUFFER_BIT);

			// Render labels
			if (title) {
				title->render();
			}
			if (label1) {
				label1->render();
			}
			if (label2) {
				label2->render();
			}
			if (label3) {
				label3->render();
			}
			if (label4) {
				label4->render();
			}
			if (instructions) {
				instructions->render();
			}

			// Render standalone icons
			if (icon1) {
				icon1->render();
			}
			if (icon2) {
				icon2->render();
			}
			if (icon3) {
				icon3->render();
			}
			if (iconSmall) {
				iconSmall->render();
			}
			if (iconDefault) {
				iconDefault->render();
			}
			if (iconLarge) {
				iconLarge->render();
			}
			if (iconXLarge) {
				iconXLarge->render();
			}
			if (iconRed) {
				iconRed->render();
			}
			if (iconGreen) {
				iconGreen->render();
			}
			if (iconBlue) {
				iconBlue->render();
			}
			if (iconWhite) {
				iconWhite->render();
			}

			// Render layout container with icons
			if (layoutContainer) {
				layoutContainer->render();
			}
		}

	  private:
		// Labels
		std::unique_ptr<UI::Text> title;
		std::unique_ptr<UI::Text> label1;
		std::unique_ptr<UI::Text> label2;
		std::unique_ptr<UI::Text> label3;
		std::unique_ptr<UI::Text> label4;
		std::unique_ptr<UI::Text> instructions;

		// Standalone icons
		std::unique_ptr<UI::Icon> icon1;
		std::unique_ptr<UI::Icon> icon2;
		std::unique_ptr<UI::Icon> icon3;
		std::unique_ptr<UI::Icon> iconSmall;
		std::unique_ptr<UI::Icon> iconDefault;
		std::unique_ptr<UI::Icon> iconLarge;
		std::unique_ptr<UI::Icon> iconXLarge;
		std::unique_ptr<UI::Icon> iconRed;
		std::unique_ptr<UI::Icon> iconGreen;
		std::unique_ptr<UI::Icon> iconBlue;
		std::unique_ptr<UI::Icon> iconWhite;

		// Layout container
		std::unique_ptr<UI::LayoutContainer> layoutContainer;
	};

} // anonymous namespace

// Export scene info for registry
namespace ui_sandbox::scenes {
	extern const ui_sandbox::SceneInfo Icon = {kSceneName, []() { return std::make_unique<IconScene>(); }};
} // namespace ui_sandbox::scenes
