// Main Menu Scene - Game entry point
// Menu with: New Game, Settings, Exit

#include "SceneTypes.h"
#include <GL/glew.h>

#include <components/button/Button.h>
#include <graphics/Color.h>
#include <primitives/Primitives.h>
#include <scene/Scene.h>
#include <scene/SceneManager.h>
#include <shapes/Shapes.h>
#include <utils/Log.h>

#include <memory>
#include <vector>

namespace {

	constexpr const char* kSceneName = "main_menu";

	class MainMenuScene : public engine::IScene {
	  public:
		void onEnter() override {
			LOG_INFO(Game, "MainMenuScene - Entering");

			using namespace UI;
			using namespace Foundation;

			// Get screen center
			float centerX = Renderer::Primitives::PercentWidth(50.0F);
			float centerY = Renderer::Primitives::PercentHeight(50.0F);

			// Title
			title = std::make_unique<Text>(Text::Args{
				.position = {centerX, centerY - 150.0F},
				.text = "World-Sim",
				.style =
					{
						.color = Color::white(),
						.fontSize = 64.0F,
						.hAlign = HorizontalAlign::Center,
						.vAlign = VerticalAlign::Middle,
					},
				.id = "menu_title"
			});

			// Subtitle
			subtitle = std::make_unique<Text>(Text::Args{
				.position = {centerX, centerY - 90.0F},
				.text = "A Colony Survival Game",
				.style =
					{
						.color = Color(0.6F, 0.6F, 0.7F, 1.0F),
						.fontSize = 20.0F,
						.hAlign = HorizontalAlign::Center,
						.vAlign = VerticalAlign::Middle,
					},
				.id = "menu_subtitle"
			});

			// Button dimensions
			constexpr float kButtonWidth = 200.0F;
			constexpr float kButtonHeight = 50.0F;
			constexpr float kButtonSpacing = 20.0F;
			float			buttonX = centerX - (kButtonWidth / 2.0F);
			float			buttonY = centerY - 20.0F;

			// New Game button - capture sceneManager for type-safe scene switch
			buttons.push_back(
				std::make_unique<Button>(Button::Args{
					.label = "New Game",
					.position = {buttonX, buttonY},
					.size = {kButtonWidth, kButtonHeight},
					.type = Button::Type::Primary,
					.onClick =
						[this]() {
							LOG_INFO(Game, "Starting new game...");
							sceneManager->switchTo(world_sim::toKey(world_sim::SceneType::GameLoading));
						},
					.id = "btn_new_game"
				})
			);
			buttonY += kButtonHeight + kButtonSpacing;

			// Settings button
			buttons.push_back(
				std::make_unique<Button>(Button::Args{
					.label = "Settings",
					.position = {buttonX, buttonY},
					.size = {kButtonWidth, kButtonHeight},
					.type = Button::Type::Secondary,
					.onClick =
						[this]() {
							LOG_INFO(Game, "Opening settings...");
							sceneManager->switchTo(world_sim::toKey(world_sim::SceneType::Settings));
						},
					.id = "btn_settings"
				})
			);
			buttonY += kButtonHeight + kButtonSpacing;

			// Exit button - use sceneManager->requestExit() instead of direct GLFW
			buttons.push_back(
				std::make_unique<Button>(Button::Args{
					.label = "Exit",
					.position = {buttonX, buttonY},
					.size = {kButtonWidth, kButtonHeight},
					.type = Button::Type::Secondary,
					.onClick =
						[this]() {
							LOG_INFO(Game, "Exit requested from main menu");
							sceneManager->requestExit();
						},
					.id = "btn_exit"
				})
			);

			// Version text at bottom
			version = std::make_unique<Text>(Text::Args{
				.position = {centerX, Renderer::Primitives::PercentHeight(95.0F)},
				.text = "v0.1.0 - Development Build",
				.style =
					{
						.color = Color(0.4F, 0.4F, 0.4F, 1.0F),
						.fontSize = 14.0F,
						.hAlign = HorizontalAlign::Center,
						.vAlign = VerticalAlign::Middle,
					},
				.id = "version"
			});
		}

		void handleInput(float /*dt*/) override {
			// Handle mouse input for buttons (keyboard input is routed via FocusManager)
			for (const auto& button : buttons) {
				button->handleInput();
			}
		}

		void update(float /*dt*/) override {
			// No dynamic updates needed
		}

		void render() override {
			// Dark background
			glClearColor(0.08F, 0.08F, 0.12F, 1.0F);
			glClear(GL_COLOR_BUFFER_BIT);

			// Render title and subtitle
			if (title) {
				title->render();
			}
			if (subtitle) {
				subtitle->render();
			}

			// Render all buttons
			for (const auto& button : buttons) {
				button->render();
			}

			// Render version
			if (version) {
				version->render();
			}
		}

		void onExit() override {
			LOG_INFO(Game, "MainMenuScene - Exiting");
			title.reset();
			subtitle.reset();
			version.reset();
			buttons.clear();
		}

		std::string exportState() override { return R"({"scene": "main_menu"})"; }

		const char* getName() const override { return kSceneName; }

	  private:
		std::unique_ptr<UI::Text>				 title;
		std::unique_ptr<UI::Text>				 subtitle;
		std::unique_ptr<UI::Text>				 version;
		std::vector<std::unique_ptr<UI::Button>> buttons;
	};

} // namespace

// Export scene info for registry
namespace world_sim::scenes {
	extern const world_sim::SceneInfo MainMenu = {kSceneName, []() { return std::make_unique<MainMenuScene>(); }};
} // namespace world_sim::scenes
