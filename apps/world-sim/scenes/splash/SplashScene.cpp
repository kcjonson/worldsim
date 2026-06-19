// Splash Scene - Salvage boot screen. Shows the identity block over a loading
// strip while assets load asynchronously, then transitions to the main menu when
// the library loads cleanly, or blocks with an error summary on validation errors.

#include "SceneTypes.h"
#include "scenes/shared/Starfield.h"
#include <assets/AssetRegistry.h>
#include <graphics/PrimitiveStyles.h>
#include <primitives/Primitives.h>
#include <scene/Scene.h>
#include <scene/SceneManager.h>
#include <theme/Tokens.h>
#include <theme/Variants.h>
#include <utils/Log.h>

#include <GL/glew.h>
#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <vector>

namespace {

	constexpr const char* kSceneName = "splash";

	float textScale(float px) { return px / 16.0F; }

	void drawDiamond(float cx, float cy, float r, Foundation::Color color) {
		const std::array<Foundation::Vec2, 4> v{{{cx, cy - r}, {cx + r, cy}, {cx, cy + r}, {cx - r, cy}}};
		const std::array<uint16_t, 6>		  idx{0, 1, 2, 0, 2, 3};
		Renderer::Primitives::drawTriangles(Renderer::Primitives::TrianglesArgs{
			.vertices = v.data(), .indices = idx.data(), .vertexCount = 4, .indexCount = 6, .color = color});
	}

	class SplashScene : public engine::IScene {
	  public:
		void onEnter() override {
			LOG_INFO(Game, "SplashScene - Entering");
			m_timer = 0.0F;
			m_lastShownCount = -1;
			m_failed = false;
			m_errorLines.clear();
			m_phase = "Initializing";
			m_progress = 0.0F;
		}

		void update(float dt) override {
			auto&		registry = engine::assets::AssetRegistry::Get();
			const auto& progress = registry.loadProgress();
			m_timer += dt;

			if (!progress.started.load()) {
				m_progress = std::min(1.0F, m_timer / kMinSplashSeconds);
				m_phase = "Ready";
				if (m_timer > kMinSplashSeconds) {
					sceneManager->switchTo(world_sim::toKey(world_sim::SceneType::MainMenu));
				}
				return;
			}

			const int loaded = progress.defsLoaded.load();
			if (loaded != m_lastShownCount) {
				m_lastShownCount = loaded;
				m_phase = "Mounting salvage manifest - " + std::to_string(loaded) + " defs";
			}

			if (!registry.isLoadComplete()) {
				// Cosmetic fill until the load reports complete, then snap to full.
				m_progress = std::min(0.92F, m_timer / 2.5F);
				return;
			}
			m_progress = 1.0F;

			const engine::assets::ValidationReport& report = registry.getValidationReport();
			if (report.hasErrors()) {
				if (!m_failed) {
					m_failed = true;
					buildErrorText(report);
					LOG_ERROR(Game, "Asset validation failed: %d error(s); blocking at splash", report.errorCount());
				}
				return;
			}

			LOG_INFO(Game, "SplashScene - assets ready (%d defs, %d warnings); -> MainMenu", loaded, report.warningCount());
			sceneManager->switchTo(world_sim::toKey(world_sim::SceneType::MainMenu));
		}

		void render() override {
			using namespace UI;
			using Renderer::Primitives::drawRect;
			using Renderer::Primitives::drawText;

			glClearColor(bg_void.r, bg_void.g, bg_void.b, 1.0F);
			glClear(GL_COLOR_BUFFER_BIT);

			const float screenW = Renderer::Primitives::PercentWidth(100.0F);
			const float screenH = Renderer::Primitives::PercentHeight(100.0F);
			world_sim::renderStarfield(static_cast<int>(screenW), static_cast<int>(screenH), 3U);
			const float cx = screenW * 0.5F;
			const float cy = screenH * 0.5F;

			// Identity block, centered.
			drawDiamond(cx, cy - 66.0F, 18.0F, accent);
			drawText(Renderer::Primitives::TextArgs{
				.text = "WORLD-SIM",
				.position = {0.0F, cy},
				.scale = textScale(fs_5xl),
				.color = text_bright,
				.font = fontDisplay,
				.hAlign = Foundation::HorizontalAlign::Center,
				.vAlign = Foundation::VerticalAlign::Middle,
				.boxWidth = screenW,
				.letterSpacing = fs_5xl * ls_widest});
			drawText(Renderer::Primitives::TextArgs{
				.text = "PROSPECTING EXPEDITION 28-B",
				.position = {0.0F, cy + 70.0F},
				.scale = textScale(fs_sm),
				.color = accent,
				.font = fontMono,
				.hAlign = Foundation::HorizontalAlign::Center,
				.vAlign = Foundation::VerticalAlign::Middle,
				.boxWidth = screenW,
				.letterSpacing = fs_sm * ls_wider});
			drawText(Renderer::Primitives::TextArgs{
				.text = "Everything you'll need is already here. It's just not yours yet.",
				.position = {0.0F, cy + 96.0F},
				.scale = textScale(fs_sm),
				.color = text_dim,
				.font = fontMono,
				.hAlign = Foundation::HorizontalAlign::Center,
				.vAlign = Foundation::VerticalAlign::Middle,
				.boxWidth = screenW});

			// Bottom loading strip.
			const float stripW = std::min(520.0F, screenW * 0.7F);
			const float stripX = cx - stripW * 0.5F;
			const float barY = screenH - 96.0F;

			if (m_failed) {
				float ey = barY - 8.0F;
				for (const std::string& line : m_errorLines) {
					drawText(Renderer::Primitives::TextArgs{
						.text = line,
						.position = {stripX, ey},
						.scale = textScale(fs_xs),
						.color = status_crit,
						.font = fontMono,
						.vAlign = Foundation::VerticalAlign::Top});
					ey += 18.0F;
				}
			} else {
				drawText(Renderer::Primitives::TextArgs{
					.text = m_phase,
					.position = {stripX, barY - 18.0F},
					.scale = textScale(fs_2xs),
					.color = text_dim,
					.font = fontMono,
					.vAlign = Foundation::VerticalAlign::Top});
				drawText(Renderer::Primitives::TextArgs{
					.text = std::to_string(static_cast<int>(m_progress * 100.0F)) + "%",
					.position = {stripX, barY - 18.0F},
					.scale = textScale(fs_2xs),
					.color = accent,
					.font = fontMono,
					.hAlign = Foundation::HorizontalAlign::Right,
					.vAlign = Foundation::VerticalAlign::Top,
					.boxWidth = stripW});
				// Track + amber fill.
				drawRect(Renderer::Primitives::RectArgs{
					.bounds = {stripX, barY, stripW, 3.0F},
					.style = {.fill = bg_inset}});
				drawRect(Renderer::Primitives::RectArgs{
					.bounds = {stripX, barY, stripW * std::clamp(m_progress, 0.0F, 1.0F), 3.0F},
					.style = {.fill = accent}});
			}

			// Version, bottom-right.
			drawText(Renderer::Primitives::TextArgs{
				.text = "v0.1.0-proto",
				.position = {0.0F, screenH - 28.0F},
				.scale = textScale(fs_2xs),
				.color = text_faint,
				.font = fontMono,
				.hAlign = Foundation::HorizontalAlign::Right,
				.vAlign = Foundation::VerticalAlign::Top,
				.boxWidth = screenW - 24.0F});
		}

		void onExit() override {
			LOG_INFO(Game, "SplashScene - Exiting");
			m_errorLines.clear();
		}

		std::string exportState() override {
			return R"({"scene": "splash", "loaded": )" + std::to_string(m_lastShownCount) + R"(, "failed": )" + (m_failed ? "true" : "false") + "}";
		}

		const char* getName() const override { return kSceneName; }

	  private:
		static constexpr float kMinSplashSeconds = 1.0F;

		void buildErrorText(const engine::assets::ValidationReport& report) {
			m_errorLines.clear();
			m_phase = "Asset validation failed: " + std::to_string(report.errorCount()) + " error(s)";
			constexpr int kMaxLines = 8;
			int			  shown = 0;
			for (const auto& issue : report.issues) {
				if (issue.severity != engine::assets::Severity::Error) {
					continue;
				}
				if (shown >= kMaxLines) {
					break;
				}
				m_errorLines.push_back((issue.defName.empty() ? std::string("-") : issue.defName) + ": " + issue.message);
				++shown;
			}
		}

		float					 m_timer = 0.0F;
		int						 m_lastShownCount = -1;
		bool					 m_failed = false;
		float					 m_progress = 0.0F;
		std::string				 m_phase;
		std::vector<std::string> m_errorLines;
	};

} // namespace

// Export scene info for registry
namespace world_sim::scenes {
	extern const world_sim::SceneInfo Splash = {kSceneName, []() { return std::make_unique<SplashScene>(); }};
}
