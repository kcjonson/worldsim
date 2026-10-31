// Splash Scene - Salvage boot screen. Shows the identity block over a boot
// log while assets load asynchronously, then enables the "Enter Expedition"
// button when the library loads cleanly, or blocks with an error summary on
// validation errors.

#include "SceneTypes.h"
#include "scenes/shared/SalvageUi.h"
#include "scenes/shared/Starfield.h"
#include <application/App.h>
#include <assets/AssetRegistry.h>
#include <primitives/Primitives.h>
#include <ui/components/Button.h>
#include <ui/shapes/Text.h>
#include <utils/Log.h>

#include <algorithm>
#include <array>
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

	namespace tokens = world_sim::tokens;
	namespace family = ui::tokens::font::family;

	class SplashScene : public app::Scene {
	  public:
		void onEnter(app::App& app) override {
			owner = &app;
			LOG_INFO(Game, "SplashScene - Entering");
			world_sim::enterSalvageUi(app, ui());
			m_timer = 0.0F;
			m_lastShownCount = -1;
			m_failed = false;
			m_done = false;
			m_shownBootLines = 0;
			m_progress = 0.0F;
			build();
		}

		void update(const app::FrameContext& frame) override {
			const auto	dt = static_cast<float>(frame.dt);
			auto&		registry = engine::assets::AssetRegistry::Get();
			const auto& progress = registry.loadProgress();
			m_timer += dt;

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
				showErrors(report);
				LOG_ERROR(Game, "Asset validation failed: %d error(s); blocking at splash", report.errorCount());
				return;
			}

			LOG_INFO(Game, "SplashScene - assets ready (%d defs, %d warnings)", m_lastShownCount, report.warningCount());
			finishLoad();
		}

		void render(float /*alpha*/) override {
			const ui::Theme& theme = ui().getTheme();
			Renderer::Primitives::clear(theme.get(tokens::color::bgVoid));
			const float screenW = Renderer::Primitives::PercentWidth(100.0F);
			const float screenH = Renderer::Primitives::PercentHeight(100.0F);
			world_sim::renderStarfield(theme, static_cast<int>(screenW), static_cast<int>(screenH), 3U);
		}

		void onExit() override { LOG_INFO(Game, "SplashScene - Exiting"); }

		std::string exportState() const override {
			return R"({"scene": "splash", "loaded": )" + std::to_string(m_lastShownCount) + R"(, "failed": )" +
				   (m_failed ? "true" : "false") + R"(, "done": )" + (m_done ? "true" : "false") + "}";
		}

		const char* name() const override { return kSceneName; }

	  private:
		static constexpr float kMinSplashSeconds = 1.0F;

		ui::Style mono(ui::DimensionToken size, ui::ColorToken color) {
			return world_sim::text(ui().getTheme(), {.family = family::mono, .size = size, .color = color});
		}

		// The identity block centred in the viewport; the error lines, the boot log and the button in a
		// column centred at the foot; the version at the bottom right.
		void build() {
			const ui::Theme&	   theme = ui().getTheme();
			ui::Ref<ui::Component> top = root();
			const float			   space2 = theme.get(tokens::spacing::space2);

			ui::Ref<ui::Component> centre = top.addChild<ui::Component>({
				.id = "splash_centre",
				.size = {ui::percent(100.0F), ui::percent(100.0F)},
				.layout = {
					.display = ui::Display::Flex, .alignItems = ui::AlignItems::Center, .justifyContent = ui::JustifyContent::Center
				},
			});
			ui::Ref<ui::Component> identity = centre.addChild<ui::Component>({
				.id = "splash_identity",
				.layout = {
					.display = ui::Display::Flex,
					.flexDirection = ui::FlexDirection::Column,
					.gap = space2,
					.alignItems = ui::AlignItems::Center
				},
			});
			identity.addChild<world_sim::Diamond>(
				{.id = "splash_diamond", .margin = {0.0F, 0.0F, theme.get(tokens::spacing::space5), 0.0F}, .radius = 18.0F}
			);
			identity.addChild<ui::Text>({
				.id = "splash_title",
				.style = world_sim::text(
					theme,
					{.family = family::display,
					 .size = tokens::fontSize::fs5xl,
					 .color = tokens::color::textBright,
					 .tracking = tokens::letterSpacing::lsWidest}
				),
				.text = "WORLD-SIM",
			});
			identity.addChild<ui::Text>({
				.id = "splash_tagline",
				.style = world_sim::text(
					theme,
					{.family = family::mono,
					 .size = tokens::fontSize::fsSm,
					 .color = tokens::color::accent,
					 .tracking = tokens::letterSpacing::lsWider}
				),
				.text = "PROSPECTING EXPEDITION 28-B",
			});
			identity.addChild<ui::Text>({
				.id = "splash_flavor",
				.margin = space2,
				.style = world_sim::text(theme, {.size = tokens::fontSize::fsMd, .color = tokens::color::textDim}),
				.text = "Everything you'll need is already here. It's just not yours yet.",
			});

			ui::Ref<ui::Component> foot = top.addChild<ui::Component>({
				.id = "splash_foot",
				.size = {ui::percent(100.0F), ui::kAuto},
				.layout = {
					.display = ui::Display::Flex,
					.flexDirection = ui::FlexDirection::Column,
					.gap = theme.get(tokens::spacing::space5),
					.alignItems = ui::AlignItems::Center,
					.position = ui::Position::Absolute,
					.inset = {.bottom = theme.get(tokens::spacing::space12)}
				},
			});
			m_errors = foot.addChild<ui::Component>({
				.id = "splash_errors",
				.visible = false,
				.layout = {.display = ui::Display::Flex, .flexDirection = ui::FlexDirection::Column, .gap = 4.0F},
			});
			m_bootLog = foot.addChild<ui::Component>({
				.id = "splash_bootlog",
				.layout = {.display = ui::Display::Flex, .flexDirection = ui::FlexDirection::Column, .gap = 3.0F},
			});
			// Always shown; disabled until the asset library loads cleanly. The boot log is the only
			// progress indicator.
			m_enterButton = foot.addChild<ui::Button>({
				.id = "btn_enter_expedition",
				.enabled = false,
				.size = {230.0F, 46.0F},
				.label = "Enter Expedition",
				.icon = "play",
				.variant = ui::ButtonVariant::Primary,
				.onPress = [this] { owner->scenes().switchTo(world_sim::scenes::MainMenu.name); },
			});

			top.addChild<ui::Text>({
				.id = "splash_version",
				.layout = {.position = ui::Position::Absolute, .inset = {.right = 24.0F, .bottom = 16.0F}},
				.style = mono(tokens::fontSize::fs2xs, tokens::color::textFaint),
				.text = "v0.1.0-proto",
			});
		}

		// One "[ OK ] <text>" line: a green stamp, then the faint body, in mono.
		void addBootLine(const char* line) {
			ui::Ref<ui::Component> row = m_bootLog.addChild<ui::Component>({.layout = {.display = ui::Display::Flex}});
			row.addChild<ui::Text>(
				{.size = {52.0F, ui::kAuto}, .style = mono(tokens::fontSize::fsXs, tokens::color::statusOk), .text = "[ OK ]"}
			);
			row.addChild<ui::Text>({.style = mono(tokens::fontSize::fsXs, tokens::color::textFaint), .text = line});
		}

		void revealBootLines() {
			const int target =
				std::min(static_cast<int>(kBootLines.size()), static_cast<int>(m_progress * static_cast<float>(kBootLines.size())) + 1);
			while (m_shownBootLines < target) {
				addBootLine(kBootLines[static_cast<size_t>(m_shownBootLines)]);
				++m_shownBootLines;
			}
		}

		void finishLoad() {
			m_progress = 1.0F;
			while (m_shownBootLines < static_cast<int>(kBootLines.size())) {
				addBootLine(kBootLines[static_cast<size_t>(m_shownBootLines)]);
				++m_shownBootLines;
			}
			m_done = true;
			if (ui::Button* button = m_enterButton.get()) {
				button->setEnabled(true);
			}
		}

		// Up to eight error lines over the boot log; the button stays disabled.
		void showErrors(const engine::assets::ValidationReport& report) {
			ui::Component* errors = m_errors.get();
			if (errors == nullptr) {
				return;
			}
			constexpr int kMaxLines = 8;
			int			  shown = 0;
			for (const auto& issue : report.issues) {
				if (issue.severity != engine::assets::Severity::Error) {
					continue;
				}
				if (shown >= kMaxLines) {
					break;
				}
				m_errors.addChild<ui::Text>({
					.style = mono(tokens::fontSize::fsXs, tokens::color::statusCrit),
					.text = (issue.defName.empty() ? std::string("-") : issue.defName) + ": " + issue.message,
				});
				++shown;
			}
			errors->setVisible(true);
		}

		app::App*			   owner = nullptr;
		float				   m_timer = 0.0F;
		int					   m_lastShownCount = -1;
		bool				   m_failed = false;
		bool				   m_done = false;
		int					   m_shownBootLines = 0;
		float				   m_progress = 0.0F;
		ui::Ref<ui::Component> m_bootLog;
		ui::Ref<ui::Component> m_errors;
		ui::Ref<ui::Button>	   m_enterButton;
	};

} // namespace

// Export scene info for registry
namespace world_sim::scenes {
	extern const world_sim::SceneInfo Splash = {kSceneName, []() { return std::make_unique<SplashScene>(); }};
} // namespace world_sim::scenes
