// Settings Scene - Game settings and options
// Stub implementation - will be expanded with settings UI

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

constexpr const char* kSceneName = "settings";

class SettingsScene : public engine::IScene {
  public:
	void onEnter() override {
		LOG_INFO(Game, "SettingsScene - Entering");

		float centerX = Renderer::Primitives::PercentWidth(50.0F);
		float centerY = Renderer::Primitives::PercentHeight(50.0F);

		title = std::make_unique<UI::Text>(UI::Text::Args{
			.position = {centerX, centerY - 50.0F},
			.text = "Settings",
			.style =
				{
					.color = Foundation::Color::white(),
					.fontSize = 48.0F,
					.hAlign = Foundation::HorizontalAlign::Center,
					.vAlign = Foundation::VerticalAlign::Middle,
				},
			.id = "settings_title"
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
			.id = "settings_hint"
		});
	}

	void handleInput(float /*dt*/) override {
		if (engine::InputManager::Get().isKeyPressed(engine::Key::Escape)) {
			LOG_INFO(Game, "Returning to main menu");
			sceneManager->switchTo(world_sim::toKey(world_sim::SceneType::MainMenu));
		}
	}

	void update(float /*dt*/) override {
		// Will update settings UI
	}

	void render() override {
		// Dark gray background for settings
		glClearColor(0.1F, 0.1F, 0.1F, 1.0F);
		glClear(GL_COLOR_BUFFER_BIT);

		if (title) {
			title->render();
		}
		if (hint) {
			hint->render();
		}
	}

	void onExit() override {
		LOG_INFO(Game, "SettingsScene - Exiting");
		title.reset();
		hint.reset();
	}

	std::string exportState() override { return R"({"scene": "settings"})"; }

	const char* getName() const override { return kSceneName; }

  private:
	std::unique_ptr<UI::Text> title;
	std::unique_ptr<UI::Text> hint;
};

} // namespace

// Export factory function and name getter
namespace world_sim::scenes {
	std::unique_ptr<engine::IScene> createSettingsScene() { return std::make_unique<SettingsScene>(); }
	const char* getSettingsSceneName() { return kSceneName; }
} // namespace world_sim::scenes
