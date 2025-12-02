// Game Scene - Main gameplay with chunk-based world rendering
// Stub implementation - will be expanded with chunk system

#include "SceneTypes.h"
#include <GL/glew.h>

#include <graphics/Color.h>
#include <input/InputManager.h>
#include <primitives/Primitives.h>
#include <scene/Scene.h>
#include <scene/SceneManager.h>
#include <shapes/Shapes.h>
#include <utils/Log.h>

#include <memory>

namespace {

constexpr const char* kSceneName = "game";

class GameScene : public engine::IScene {
  public:
	void onEnter() override {
		LOG_INFO(Game, "GameScene - Entering");

		// Create placeholder text
		float centerX = Renderer::Primitives::PercentWidth(50.0F);
		float centerY = Renderer::Primitives::PercentHeight(50.0F);

		title = std::make_unique<UI::Text>(UI::Text::Args{
			.position = {centerX, centerY - 50.0F},
			.text = "Game Scene",
			.style =
				{
					.color = Foundation::Color::white(),
					.fontSize = 48.0F,
					.hAlign = Foundation::HorizontalAlign::Center,
					.vAlign = Foundation::VerticalAlign::Middle,
				},
			.id = "game_title"
		});

		hint = std::make_unique<UI::Text>(UI::Text::Args{
			.position = {centerX, centerY + 20.0F},
			.text = "Press ESC to return to menu",
			.style =
				{
					.color = Foundation::Color(0.6F, 0.6F, 0.6F, 1.0F),
					.fontSize = 20.0F,
					.hAlign = Foundation::HorizontalAlign::Center,
					.vAlign = Foundation::VerticalAlign::Middle,
				},
			.id = "game_hint"
		});
	}

	void handleInput(float /*dt*/) override {
		// ESC returns to main menu
		if (engine::InputManager::Get().isKeyPressed(engine::Key::Escape)) {
			LOG_INFO(Game, "Returning to main menu");
			sceneManager->switchTo(world_sim::toKey(world_sim::SceneType::MainMenu));
		}
	}

	void update(float /*dt*/) override {
		// Will update chunk loading, camera, etc.
	}

	void render() override {
		// Forest green background to indicate game is active
		glClearColor(0.15F, 0.25F, 0.15F, 1.0F);
		glClear(GL_COLOR_BUFFER_BIT);

		if (title) {
			title->render();
		}
		if (hint) {
			hint->render();
		}
	}

	void onExit() override {
		LOG_INFO(Game, "GameScene - Exiting");
		title.reset();
		hint.reset();
	}

	std::string exportState() override { return R"({"scene": "game"})"; }

	const char* getName() const override { return kSceneName; }

  private:
	std::unique_ptr<UI::Text> title;
	std::unique_ptr<UI::Text> hint;
};

} // namespace

// Export scene info for registry
namespace world_sim::scenes {
	extern const world_sim::SceneInfo Game = {kSceneName, []() { return std::make_unique<GameScene>(); }};
}
