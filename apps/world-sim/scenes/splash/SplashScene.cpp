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

	// Amber diamond mark, drawn at the top of a fixed-size box so the layout
	// engine can seat it above the title with a gap baked into the box height.
	class Diamond : public world_sim::Widget {
	  public:
		explicit Diamond(float radius, float boxHeight, const char* id = nullptr) : radius(radius), id(id) {
			size = {radius * 2.0F, boxHeight};
		}

		void render() override {
			drawDiamond(position.x + radius, position.y + radius, radius, UI::accent);
		}

		const char* debugTypeName() const override { return "Diamond"; }
		const char* debugId() const override { return id; }

	  private:
		float		radius;
		const char* id{nullptr};
	};

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
			m_progress = 0.0F;

			m_bootLog = std::make_unique<UI::LayoutContainer>(UI::LayoutContainer::Args{
				.direction = UI::Direction::Vertical,
				.gap = 3.0F,
				.id = "splash_bootlog"});

			buildIdentity();

			// Always shown; disabled until the asset library loads cleanly. The
			// boot-log lines are the only progress indicator (no pill bar).
			m_enterButton = std::make_unique<UI::Button>(UI::Button::Args{
				.label = "Enter Expedition",
				.size = {230.0F, 46.0F},
				.type = UI::Button::Type::Primary,
				.disabled = true,
				.onClick = [this]() { sceneManager->switchTo(world_sim::toKey(world_sim::SceneType::MainMenu)); },
				.id = "btn_enter_expedition",
				.iconGlyph = "play"});
		}

		bool handleInput(UI::InputEvent& event) override {
			// Button::handleEvent no-ops while disabled, so no guard needed.
			return m_enterButton && m_enterButton->handleEvent(event);
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
				// Assets already loaded synchronously (--scene jump): reveal the boot
				// log on a short timer, then enable the button.
				m_progress = std::min(1.0F, m_timer / kMinSplashSeconds);
				revealBootLines();
				if (m_timer > kMinSplashSeconds) {
					finishLoad();
				}
				return;
			}

			m_lastShownCount = progress.defsLoaded.load();

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

			LOG_INFO(Game, "SplashScene - assets ready (%d defs, %d warnings)", m_lastShownCount, report.warningCount());
			finishLoad();
		}

		void render() override {
			using namespace UI;
			using Renderer::Primitives::drawText;

			glClearColor(bg_void.r, bg_void.g, bg_void.b, 1.0F);
			glClear(GL_COLOR_BUFFER_BIT);

			const float screenW = Renderer::Primitives::PercentWidth(100.0F);
			const float screenH = Renderer::Primitives::PercentHeight(100.0F);
			world_sim::renderStarfield(static_cast<int>(screenW), static_cast<int>(screenH), 3U);
			const float cx = screenW * 0.5F;
			const float cy = screenH * 0.5F;

			// Identity block: vertical layout column (diamond + title + tagline +
			// flavor), centered on screen. Owned so the layout lint covers it.
			if (m_identity) {
				m_identity->setPosition(cx - m_identity->getWidth() * 0.5F, cy - m_identity->getHeight() * 0.5F);
				m_identity->render();
			}

			// Bottom cluster: boot-log text (the only progress indicator) above an
			// always-present Enter button that stays disabled until the load lands.
			const float buttonTop = screenH - space_12 - m_enterButton->size.y;

			// Boot log builds above the button as progress crosses each 1/6. The
			// block is centered on screen while its lines stay left-justified.
			if (m_bootLog) {
				m_bootLog->setPosition(cx - m_bootLog->getWidth() * 0.5F, buttonTop - space_5 - m_bootLog->getHeight());
				m_bootLog->render();
			}

			// Validation failure: error lines above the boot log; button stays disabled.
			if (m_failed) {
				float ey = buttonTop - space_5 - m_bootLog->getHeight() - space_4 -
						   18.0F * static_cast<float>(m_errorLines.size());
				for (const std::string& line : m_errorLines) {
					drawText(Renderer::Primitives::TextArgs{
						.text = line,
						.position = {cx - std::min(520.0F, screenW * 0.7F) * 0.5F, ey},
						.scale = textScale(fs_xs),
						.color = status_crit,
						.font = fontMono,
						.vAlign = Foundation::VerticalAlign::Top});
					ey += 18.0F;
				}
			}

			m_enterButton->setPosition(cx - m_enterButton->size.x * 0.5F, buttonTop);
			m_enterButton->render();

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
			m_identity.reset();
		}

		std::string exportState() override {
			return R"({"scene": "splash", "loaded": )" + std::to_string(m_lastShownCount) +
				   R"(, "failed": )" + (m_failed ? "true" : "false") +
				   R"(, "done": )" + (m_done ? "true" : "false") + "}";
		}

		const char* getName() const override { return kSceneName; }

		std::vector<const UI::IComponent*> getUiRoots() const override {
			std::vector<const UI::IComponent*> roots;
			if (m_identity) roots.push_back(m_identity.get());
			if (m_bootLog && m_shownBootLines > 0) roots.push_back(m_bootLog.get());
			if (m_enterButton) roots.push_back(m_enterButton.get());
			return roots;
		}

	  private:
		static constexpr float kMinSplashSeconds = 1.0F;

		void buildIdentity() {
			using namespace UI;
			m_identity = std::make_unique<LayoutContainer>(LayoutContainer::Args{
				.direction = Direction::Vertical,
				.gap = space_2,
				.crossAlign = CrossAlign::Center,
				.id = "splash_identity"});

			m_identity->addChild(Diamond(18.0F, 36.0F + space_5, "splash_diamond"));
			m_identity->addChild(world_sim::Label({.text = "WORLD-SIM",
												   .fontSize = fs_5xl,
												   .color = text_bright,
												   .font = fontDisplay,
												   .letterSpacingEm = ls_widest,
												   .hAlign = Foundation::HorizontalAlign::Center,
												   .id = "splash_title"}));
			m_identity->addChild(world_sim::Label({.text = "PROSPECTING EXPEDITION 28-B",
												   .fontSize = fs_sm,
												   .color = accent,
												   .font = fontMono,
												   .letterSpacingEm = ls_wider,
												   .hAlign = Foundation::HorizontalAlign::Center,
												   .id = "splash_tagline"}));
			m_identity->addChild(world_sim::Label({.text = "Everything you'll need is already here. It's just not yours yet.",
												   .fontSize = fs_md,
												   .color = text_dim,
												   .font = fontUi,
												   .hAlign = Foundation::HorizontalAlign::Center,
												   .id = "splash_flavor",
												   .margin = space_2}));
		}

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
			m_enterButton->setDisabled(false);
		}

		void buildErrorText(const engine::assets::ValidationReport& report) {
			m_errorLines.clear();
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
		std::vector<std::string>			 m_errorLines;
		std::unique_ptr<UI::LayoutContainer> m_bootLog;
		std::unique_ptr<UI::LayoutContainer> m_identity;
		std::unique_ptr<UI::Button>			 m_enterButton;
	};

} // namespace

// Export scene info for registry
namespace world_sim::scenes {
	extern const world_sim::SceneInfo Splash = {kSceneName, []() { return std::make_unique<SplashScene>(); }};
}
