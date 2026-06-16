// Splash Scene - shows the title while assets load asynchronously, then gates on
// validation: transitions to the main menu when the library loads cleanly, or
// blocks with an error summary when load-time validation found errors.

#include "SceneTypes.h"
#include <assets/AssetRegistry.h>
#include <graphics/Color.h>
#include <primitives/Primitives.h>
#include <scene/Scene.h>
#include <scene/SceneManager.h>
#include <shapes/Shapes.h>
#include <utils/Log.h>

#include <GL/glew.h>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr const char* kSceneName = "splash";

class SplashScene : public engine::IScene {
  public:
	void onEnter() override {
		LOG_INFO(Game, "SplashScene - Entering");
		m_timer = 0.0F;
		m_lastShownCount = -1;
		m_failed = false;
		m_errorLines.clear();

		m_centerX = Renderer::Primitives::PercentWidth(50.0F);
		m_centerY = Renderer::Primitives::PercentHeight(45.0F);

		m_title = makeText(m_centerX, m_centerY, "World-Sim", 72.0F, Foundation::Color::white(), "splash_title");
		setSubtitle("Loading...");
	}

	void update(float dt) override {
		auto&		registry = engine::assets::AssetRegistry::Get();
		const auto& progress = registry.loadProgress();
		m_timer += dt;

		// No async load running (synchronous path or assets missing): keep a brief
		// splash, then proceed.
		if (!progress.started.load()) {
			if (m_timer > kMinSplashSeconds) {
				sceneManager->switchTo(world_sim::toKey(world_sim::SceneType::MainMenu));
			}
			return;
		}

		// Reflect the running definition count in the subtitle.
		const int loaded = progress.defsLoaded.load();
		if (loaded != m_lastShownCount) {
			m_lastShownCount = loaded;
			setSubtitle("Loading assets... " + std::to_string(loaded));
		}

		if (!registry.isLoadComplete()) {
			return;
		}

		const engine::assets::ValidationReport& report = registry.getValidationReport();
		if (report.hasErrors()) {
			if (!m_failed) {
				m_failed = true;
				buildErrorText(report);
				LOG_ERROR(Game, "Asset validation failed: %d error(s); blocking at splash", report.errorCount());
			}
			return; // block: do not enter the game with invalid assets
		}

		LOG_INFO(Game, "SplashScene - assets ready (%d defs, %d warnings); -> MainMenu", loaded, report.warningCount());
		sceneManager->switchTo(world_sim::toKey(world_sim::SceneType::MainMenu));
	}

	void render() override {
		// Dark blue-gray background
		glClearColor(0.05F, 0.05F, 0.1F, 1.0F);
		glClear(GL_COLOR_BUFFER_BIT);

		if (m_title) {
			m_title->render();
		}
		if (m_subtitle) {
			m_subtitle->render();
		}
		for (const auto& line : m_errorLines) {
			line->render();
		}
	}

	void onExit() override {
		LOG_INFO(Game, "SplashScene - Exiting");
		m_title.reset();
		m_subtitle.reset();
		m_errorLines.clear();
	}

	std::string exportState() override {
		return R"({"scene": "splash", "loaded": )" + std::to_string(m_lastShownCount) + R"(, "failed": )" + (m_failed ? "true" : "false") + "}";
	}

	const char* getName() const override { return kSceneName; }

  private:
	static constexpr float kMinSplashSeconds = 1.0F;

	std::unique_ptr<UI::Text> makeText(float x, float y, const std::string& text, float size, Foundation::Color color, const char* id) {
		return std::make_unique<UI::Text>(UI::Text::Args{
			.position = {x, y},
			.text = text,
			.style =
				{
					.color = color,
					.fontSize = size,
					.hAlign = Foundation::HorizontalAlign::Center,
					.vAlign = Foundation::VerticalAlign::Middle,
				},
			.id = id
		});
	}

	void setSubtitle(const std::string& text) {
		if (m_subtitle) {
			m_subtitle->text = text; // UI::Text recomputes its cache when text changes; no realloc
		} else {
			m_subtitle = makeText(m_centerX, m_centerY + 80.0F, text, 24.0F, Foundation::Color(0.6F, 0.6F, 0.6F, 1.0F), "splash_subtitle");
		}
	}

	void buildErrorText(const engine::assets::ValidationReport& report) {
		m_errorLines.clear();
		setSubtitle("Asset validation failed: " + std::to_string(report.errorCount()) + " error(s) (see log / asset-cli validate)");

		float		  y = m_centerY + 140.0F;
		constexpr int kMaxLines = 8;
		int			  shown = 0;
		for (const auto& issue : report.issues) {
			if (issue.severity != engine::assets::Severity::Error) {
				continue;
			}
			if (shown >= kMaxLines) {
				break;
			}
			const std::string text = (issue.defName.empty() ? std::string("-") : issue.defName) + ": " + issue.message;
			m_errorLines.push_back(makeText(m_centerX, y, text, 16.0F, Foundation::Color(0.9F, 0.4F, 0.4F, 1.0F), "splash_error"));
			y += 24.0F;
			++shown;
		}
	}

	float									   m_timer = 0.0F;
	float									   m_centerX = 0.0F;
	float									   m_centerY = 0.0F;
	int										   m_lastShownCount = -1;
	bool									   m_failed = false;
	std::unique_ptr<UI::Text>				   m_title;
	std::unique_ptr<UI::Text>				   m_subtitle;
	std::vector<std::unique_ptr<UI::Text>> m_errorLines;
};

} // namespace

// Export scene info for registry
namespace world_sim::scenes {
	extern const world_sim::SceneInfo Splash = {kSceneName, []() { return std::make_unique<SplashScene>(); }};
}
