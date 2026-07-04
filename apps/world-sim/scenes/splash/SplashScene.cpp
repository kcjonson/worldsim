// Splash Scene - Salvage boot screen. Shows the identity block over a boot
// log and loading strip while assets load asynchronously, then swaps the
// loader for an "Enter Expedition" button when the library loads cleanly, or
// blocks with an error summary on validation errors.

#include "SceneTypes.h"
#include "scenes/shared/Starfield.h"
#include "scenes/shared/UiStateDrain.h"
#include "scenes/shared/Widgets.h"
#include <assets/AssetRegistry.h>
#include <components/button/Button.h>
#include <graphics/PrimitiveStyles.h>
#include <layout/LayoutContainer.h>
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

	constexpr std::array<const char*, 6> kBootLines{
		"NAV-CORE online",
		"Mounting salvage manifest",
		"Calibrating sensor grain",
		"Loading vector atlas",
		"Spinning up world cache",
		"Crew vitals nominal",
	};

	float textScale(float px) { return px / 16.0F; }

	void drawDiamond(float cx, float cy, float r, Foundation::Color color) {
		const std::array<Foundation::Vec2, 4> v{{{cx, cy - r}, {cx + r, cy}, {cx, cy + r}, {cx - r, cy}}};
		const std::array<uint16_t, 6>		  idx{0, 1, 2, 0, 2, 3};
		Renderer::Primitives::drawTriangles(Renderer::Primitives::TrianglesArgs{
			.vertices = v.data(), .indices = idx.data(), .vertexCount = 4, .indexCount = 6, .color = color});
	}

	// One "[ OK ] <text>" boot log line: green stamp, faint body, mono.
	class BootLine : public world_sim::Widget {
	  public:
		explicit BootLine(const char* text) : text(text) {
			widthMode = UI::SizeMode::Hug;
			size.y = 17.0F;
		}

		[[nodiscard]] float getWidth() const override {
			if (size.x > 0.0F) {
				return size.x;
			}
			auto* fontRenderer = Renderer::Primitives::getFontRenderer();
			const float bodyW =
				fontRenderer != nullptr ? fontRenderer->MeasureText(text, textScale(UI::fs_xs), UI::fontMono).x : 200.0F;
			return kBodyX + bodyW;
		}

		void render() override {
			using Renderer::Primitives::drawText;
			drawText({.text = "[ OK ]",
					  .position = position,
					  .scale = textScale(UI::fs_xs),
					  .color = UI::status_ok,
					  .font = UI::fontMono,
					  .vAlign = Foundation::VerticalAlign::Top});
			drawText({.text = text,
					  .position = {position.x + kBodyX, position.y},
					  .scale = textScale(UI::fs_xs),
					  .color = UI::text_faint,
					  .font = UI::fontMono,
					  .vAlign = Foundation::VerticalAlign::Top});
		}

		const char* debugTypeName() const override { return "BootLine"; }

	  private:
		static constexpr float kBodyX = 52.0F;
		std::string			   text;
	};

	class SplashScene : public engine::IScene {
	  public:
		void onEnter() override {
			LOG_INFO(Game, "SplashScene - Entering");
			m_timer = 0.0F;
			m_lastShownCount = -1;
			m_failed = false;
			m_done = false;
			m_shownBootLines = 0;
			m_errorLines.clear();
			m_phase = "Initializing";
			m_progress = 0.0F;

			m_bootLog = std::make_unique<UI::LayoutContainer>(UI::LayoutContainer::Args{
				.direction = UI::Direction::Vertical,
				.gap = 3.0F,
				.id = "splash_bootlog"});

			m_enterButton = std::make_unique<UI::Button>(UI::Button::Args{
				.label = "Enter Expedition",
				.size = {230.0F, 46.0F},
				.type = UI::Button::Type::Primary,
				.onClick = [this]() { sceneManager->switchTo(world_sim::toKey(world_sim::SceneType::MainMenu)); },
				.id = "btn_enter_expedition",
				.iconGlyph = "play"});
			m_enterButton->visible = false;
		}

		bool handleInput(UI::InputEvent& event) override {
			return m_enterButton && m_enterButton->visible && m_enterButton->handleEvent(event);
		}

		void update(float dt) override {
			auto&		registry = engine::assets::AssetRegistry::Get();
			const auto& progress = registry.loadProgress();
			m_timer += dt;
			if (m_enterButton) m_enterButton->update(dt);

			if (m_done || m_failed) {
				return;
			}

			if (!progress.started.load()) {
				// Assets already loaded synchronously (--scene jump): play the
				// strip on a short timer, then offer the button.
				m_progress = std::min(1.0F, m_timer / kMinSplashSeconds);
				m_phase = "Ready";
				revealBootLines();
				if (m_timer > kMinSplashSeconds) {
					finishLoad();
				}
				return;
			}

			const int loaded = progress.defsLoaded.load();
			if (loaded != m_lastShownCount) {
				m_lastShownCount = loaded;
				m_phase = "Mounting salvage manifest";
			}

			if (!registry.isLoadComplete()) {
				// Cosmetic fill until the load reports complete, then snap to full.
				m_progress = std::min(0.92F, m_timer / 2.5F);
				revealBootLines();
				return;
			}

			const engine::assets::ValidationReport& report = registry.getValidationReport();
			if (report.hasErrors()) {
				m_failed = true;
				buildErrorText(report);
				LOG_ERROR(Game, "Asset validation failed: %d error(s); blocking at splash", report.errorCount());
				return;
			}

			LOG_INFO(Game, "SplashScene - assets ready (%d defs, %d warnings)", loaded, report.warningCount());
			finishLoad();
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
			drawDiamond(cx, cy - 93.0F, 18.0F, accent);
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
				.position = {0.0F, cy + 58.0F},
				.scale = textScale(fs_sm),
				.color = accent,
				.font = fontMono,
				.hAlign = Foundation::HorizontalAlign::Center,
				.vAlign = Foundation::VerticalAlign::Middle,
				.boxWidth = screenW,
				.letterSpacing = fs_sm * ls_wider});
			// Flavor quote: body font, dim, centered, max 460px.
			drawText(Renderer::Primitives::TextArgs{
				.text = "Everything you'll need is already here. It's just not yours yet.",
				.position = {cx - 230.0F, cy + 104.0F},
				.scale = textScale(fs_md),
				.color = text_dim,
				.font = fontUi,
				.hAlign = Foundation::HorizontalAlign::Center,
				.vAlign = Foundation::VerticalAlign::Middle,
				.boxWidth = 460.0F});

			// Bottom loading strip.
			const float stripW = std::min(520.0F, screenW * 0.7F);
			const float stripX = cx - stripW * 0.5F;
			const float barY = screenH - space_12;

			if (m_failed) {
				float ey = barY - 8.0F - 18.0F * static_cast<float>(m_errorLines.size());
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
				// Boot log builds above the loader (or the Enter button once done)
				// as progress crosses each 1/6.
				if (m_bootLog) {
					const float reserve = m_done ? m_enterButton->size.y + 20.0F : 40.0F;
					m_bootLog->setPosition(stripX, barY - reserve - m_bootLog->getHeight());
					m_bootLog->render();
				}

				if (m_done) {
					m_enterButton->setPosition(cx - m_enterButton->size.x * 0.5F, barY - m_enterButton->size.y);
					m_enterButton->render();
				} else {
					drawText(Renderer::Primitives::TextArgs{
						.text = m_phase,
						.position = {stripX, barY - 20.0F},
						.scale = textScale(fs_xs),
						.color = text,
						.font = fontMono,
						.vAlign = Foundation::VerticalAlign::Top,
						.letterSpacing = fs_xs * ls_wide,
						.transform = Foundation::TextTransform::Uppercase});
					drawText(Renderer::Primitives::TextArgs{
						.text = std::to_string(static_cast<int>(m_progress * 100.0F)) + "%",
						.position = {stripX, barY - 20.0F},
						.scale = textScale(fs_xs),
						.color = accent_bright,
						.font = fontMono,
						.hAlign = Foundation::HorizontalAlign::Right,
						.vAlign = Foundation::VerticalAlign::Top,
						.boxWidth = stripW});
					// Pill track + amber fill.
					const float radius = std::min(r_pill, 1.5F);
					drawRect(Renderer::Primitives::RectArgs{
						.bounds = {stripX, barY, stripW, 3.0F},
						.style = {.fill = bg_inset,
								  .border = Foundation::BorderStyle{
									  .color = bg_inset, .width = 0.0F, .cornerRadius = radius,
									  .position = Foundation::BorderPosition::Inside}}});
					const float fillW = stripW * std::clamp(m_progress, 0.0F, 1.0F);
					if (fillW > 3.0F) {
						drawRect(Renderer::Primitives::RectArgs{
							.bounds = {stripX, barY, fillW, 3.0F},
							.style = {.fill = accent,
									  .border = Foundation::BorderStyle{
										  .color = accent, .width = 0.0F, .cornerRadius = radius,
										  .position = Foundation::BorderPosition::Inside}}});
					}
				}
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

			world_sim::serveUiStateRequests(*this);
		}

		void onExit() override {
			LOG_INFO(Game, "SplashScene - Exiting");
			m_errorLines.clear();
			m_bootLog.reset();
			m_enterButton.reset();
		}

		std::string exportState() override {
			return R"({"scene": "splash", "loaded": )" + std::to_string(m_lastShownCount) +
				   R"(, "failed": )" + (m_failed ? "true" : "false") +
				   R"(, "done": )" + (m_done ? "true" : "false") + "}";
		}

		const char* getName() const override { return kSceneName; }

		std::vector<const UI::IComponent*> getUiRoots() const override {
			std::vector<const UI::IComponent*> roots;
			if (m_bootLog && m_shownBootLines > 0) roots.push_back(m_bootLog.get());
			if (m_enterButton && m_enterButton->visible) roots.push_back(m_enterButton.get());
			return roots;
		}

	  private:
		static constexpr float kMinSplashSeconds = 1.0F;

		void revealBootLines() {
			const int target = std::min(static_cast<int>(kBootLines.size()),
										static_cast<int>(m_progress * static_cast<float>(kBootLines.size())) + 1);
			while (m_shownBootLines < target) {
				m_bootLog->addChild(BootLine(kBootLines[static_cast<size_t>(m_shownBootLines)]));
				++m_shownBootLines;
			}
		}

		void finishLoad() {
			m_progress = 1.0F;
			while (m_shownBootLines < static_cast<int>(kBootLines.size())) {
				m_bootLog->addChild(BootLine(kBootLines[static_cast<size_t>(m_shownBootLines)]));
				++m_shownBootLines;
			}
			m_done = true;
			m_phase = "Ready";
			m_enterButton->visible = true;
		}

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

		float								 m_timer = 0.0F;
		int									 m_lastShownCount = -1;
		bool								 m_failed = false;
		bool								 m_done = false;
		int									 m_shownBootLines = 0;
		float								 m_progress = 0.0F;
		std::string							 m_phase;
		std::vector<std::string>			 m_errorLines;
		std::unique_ptr<UI::LayoutContainer> m_bootLog;
		std::unique_ptr<UI::Button>			 m_enterButton;
	};

} // namespace

// Export scene info for registry
namespace world_sim::scenes {
	extern const world_sim::SceneInfo Splash = {kSceneName, []() { return std::make_unique<SplashScene>(); }};
}
