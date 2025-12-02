// Splash Scene - Fast-loading splash screen
// Shows game title while resources load, auto-transitions to main menu

#include "SceneTypes.h"
#include <graphics/Color.h>
#include <primitives/Primitives.h>
#include <scene/Scene.h>
#include <scene/SceneManager.h>
#include <shapes/Shapes.h>
#include <utils/Log.h>

#include <GL/glew.h>
#include <memory>

namespace {

constexpr const char* kSceneName = "splash";

class SplashScene : public engine::IScene {
  public:
	void onEnter() override {
		LOG_INFO(Game, "SplashScene - Entering");
		m_timer = 0.0F;

		// Create title text centered on screen
		float centerX = Renderer::Primitives::PercentWidth(50.0F);
		float centerY = Renderer::Primitives::PercentHeight(45.0F);

		m_title = std::make_unique<UI::Text>(UI::Text::Args{
			.position = {centerX, centerY},
			.text = "World-Sim",
			.style =
				{
					.color = Foundation::Color::white(),
					.fontSize = 72.0F,
					.hAlign = Foundation::HorizontalAlign::Center,
					.vAlign = Foundation::VerticalAlign::Middle,
				},
			.id = "splash_title"
		});

		// Subtitle
		m_subtitle = std::make_unique<UI::Text>(UI::Text::Args{
			.position = {centerX, centerY + 80.0F},
			.text = "Loading...",
			.style =
				{
					.color = Foundation::Color(0.6F, 0.6F, 0.6F, 1.0F),
					.fontSize = 24.0F,
					.hAlign = Foundation::HorizontalAlign::Center,
					.vAlign = Foundation::VerticalAlign::Middle,
				},
			.id = "splash_subtitle"
		});
	}

	void handleInput(float /*dt*/) override {
		// No input handling - auto transitions
	}

	void update(float dt) override {
		m_timer += dt;

		// Auto-transition to main menu after splash duration
		if (m_timer > kSplashDuration) {
			LOG_INFO(Game, "SplashScene - Transitioning to MainMenu");
			sceneManager->switchTo(world_sim::toKey(world_sim::SceneType::MainMenu));
		}
	}

	void render() override {
		// Dark blue-gray background
		glClearColor(0.05F, 0.05F, 0.1F, 1.0F);
		glClear(GL_COLOR_BUFFER_BIT);

		// Render title and subtitle
		if (m_title) {
			m_title->render();
		}
		if (m_subtitle) {
			m_subtitle->render();
		}
	}

	void onExit() override {
		LOG_INFO(Game, "SplashScene - Exiting");
		m_title.reset();
		m_subtitle.reset();
	}

	std::string exportState() override {
		return R"({"scene": "splash", "timer": )" + std::to_string(m_timer) + "}";
	}

	const char* getName() const override { return kSceneName; }

  private:
	static constexpr float kSplashDuration = 1.5F; // seconds

	float					  m_timer = 0.0F;
	std::unique_ptr<UI::Text> m_title;
	std::unique_ptr<UI::Text> m_subtitle;
};

} // namespace

// Export scene info for registry
namespace world_sim::scenes {
	extern const world_sim::SceneInfo Splash = {kSceneName, []() { return std::make_unique<SplashScene>(); }};
}
