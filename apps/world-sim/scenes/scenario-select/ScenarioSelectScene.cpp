// Scenario Select Scene - New Game step 1 of 3.
//
// Stub of the prototype's ScenarioSelect screen: header, five selectable
// scenario rows (name, blurb, difficulty pips, party size, tags), and a
// Back/Confirm footer. Hand layout; the full card grid comes with the
// upgraded layout engine.

#include "NewGameSetup.h"
#include "SceneTypes.h"
#include "scenes/scenario-select/Scenarios.h"
#include "scenes/shared/Starfield.h"
#include <GL/glew.h>

#include <components/button/Button.h>
#include <graphics/PrimitiveStyles.h>
#include <input/InputEvent.h>
#include <input/InputManager.h>
#include <primitives/Primitives.h>
#include <scene/Scene.h>
#include <scene/SceneManager.h>
#include <shapes/Shapes.h>
#include <theme/Tokens.h>
#include <theme/Variants.h>
#include <utils/Log.h>

#include <algorithm>
#include <format>
#include <memory>
#include <string>
#include <vector>

namespace {

	constexpr const char* kSceneName = "scenario_select";

	constexpr float kColX = 80.0F;
	constexpr float kRowHeight = 80.0F;
	constexpr float kRowGap = 10.0F;
	constexpr float kMetaWidth = 210.0F; // right-side block: pips + party + tags

	float textScale(float px) { return px / 16.0F; }

	Foundation::Color pipColor(int pip, int difficulty) {
		if (pip > difficulty) return UI::text_faint;
		if (difficulty <= 2) return UI::status_ok;
		if (difficulty <= 3) return UI::status_warn;
		return UI::status_crit;
	}

	class ScenarioSelectScene : public engine::IScene {
	  public:
		void onEnter() override {
			LOG_INFO(Game, "ScenarioSelectScene - Entering");

			// Coming back from a later step restores the previous choice.
			selectedIndex = 0;
			const std::string& storedId = world_sim::NewGameSetup::Get().scenarioId;
			for (size_t i = 0; i < world_sim::kScenarios.size(); ++i) {
				if (storedId == world_sim::kScenarios[i].id) {
					selectedIndex = static_cast<int>(i);
					break;
				}
			}

			backButton = std::make_unique<UI::Button>(UI::Button::Args{
				.label = "Back",
				.size = {120.0F, 40.0F},
				.type = UI::Button::Type::Secondary,
				.onClick = [this]() { goBack(); },
				.id = "btn_scenario_back",
			});
			confirmButton = std::make_unique<UI::Button>(UI::Button::Args{
				.label = "Confirm Crew",
				.size = {200.0F, 40.0F},
				.type = UI::Button::Type::Primary,
				.onClick = [this]() { confirm(); },
				.id = "btn_scenario_confirm",
			});
			needsLayout = true;
		}

		void onExit() override {
			LOG_INFO(Game, "ScenarioSelectScene - Exiting");
			backButton.reset();
			confirmButton.reset();
			rowRects.clear();
		}

		bool handleInput(UI::InputEvent& event) override {
			if (backButton && backButton->handleEvent(event)) return true;
			if (confirmButton && confirmButton->handleEvent(event)) return true;

			using UI::InputEvent;
			if (event.type == InputEvent::Type::MouseMove) {
				hoveredIndex = rowAtPoint(event.position);
				return false;
			}
			if (event.type == InputEvent::Type::MouseUp && event.button == engine::MouseButton::Left) {
				const int idx = rowAtPoint(event.position);
				if (idx >= 0) {
					selectedIndex = idx;
					event.consume();
					return true;
				}
			}
			return false;
		}

		void update(float dt) override {
			if (engine::InputManager::Get().isKeyPressed(engine::Key::Escape)) {
				goBack();
				return;
			}
			if (backButton) backButton->update(dt);
			if (confirmButton) confirmButton->update(dt);
		}

		void render() override {
			using Renderer::Primitives::drawRect;
			using Renderer::Primitives::drawText;

			if (needsLayout) layout();

			glClearColor(UI::bg_void.r, UI::bg_void.g, UI::bg_void.b, 1.0F);
			glClear(GL_COLOR_BUFFER_BIT);

			const float screenW = Renderer::Primitives::PercentWidth(100.0F);
			const float screenH = Renderer::Primitives::PercentHeight(100.0F);
			world_sim::renderStarfield(static_cast<int>(screenW), static_cast<int>(screenH), 42U, true);

			// Header.
			drawText({.text = "// NEW GAME    STEP 01 / 03",
					  .position = {kColX, 40.0F},
					  .scale = textScale(UI::fs_2xs),
					  .color = UI::text_faint,
					  .font = UI::fontMono,
					  .vAlign = Foundation::VerticalAlign::Top,
					  .letterSpacing = UI::fs_2xs * UI::ls_wider});
			drawText({.text = "Select Scenario",
					  .position = {kColX, 56.0F},
					  .scale = textScale(UI::fs_3xl),
					  .color = UI::text_bright,
					  .font = UI::fontDisplay,
					  .vAlign = Foundation::VerticalAlign::Top,
					  .letterSpacing = UI::fs_3xl * UI::ls_wide});
			drawText({.text = "Each scenario reshapes your wreck site, salvage, and the world you'll fight to survive.",
					  .position = {kColX, 106.0F},
					  .scale = textScale(UI::fs_md),
					  .color = UI::text_dim,
					  .font = UI::fontUi,
					  .vAlign = Foundation::VerticalAlign::Top});

			for (size_t i = 0; i < world_sim::kScenarios.size(); ++i) {
				renderRow(i);
			}

			if (backButton) backButton->render();
			if (confirmButton) confirmButton->render();
		}

		std::string exportState() override {
			return std::format(R"({{"scene":"scenario_select","selected":"{}"}})",
							   world_sim::kScenarios[static_cast<size_t>(selectedIndex)].id);
		}
		const char* getName() const override { return kSceneName; }

	  private:
		void layout() {
			const float screenW = Renderer::Primitives::PercentWidth(100.0F);
			const float screenH = Renderer::Primitives::PercentHeight(100.0F);
			if (screenW < 1.0F || screenH < 1.0F) return; // viewport not ready

			rowWidth = std::min(760.0F, screenW - kColX * 2.0F);
			rowRects.clear();
			const float rowsTop = 150.0F;
			for (size_t i = 0; i < world_sim::kScenarios.size(); ++i) {
				rowRects.push_back({kColX, rowsTop + static_cast<float>(i) * (kRowHeight + kRowGap),
									rowWidth, kRowHeight});
			}

			const float footerY = screenH - 68.0F;
			backButton->setPosition(kColX, footerY);
			confirmButton->setPosition(kColX + rowWidth - 200.0F, footerY);
			needsLayout = false;
		}

		void renderRow(size_t i) {
			using Renderer::Primitives::drawRect;
			using Renderer::Primitives::drawText;

			const world_sim::ScenarioDef& s = world_sim::kScenarios[i];
			const Foundation::Rect&		  r = rowRects[i];
			const bool selected = static_cast<int>(i) == selectedIndex;
			const bool hovered = static_cast<int>(i) == hoveredIndex;

			drawRect({.bounds = r,
					  .style = {.fill = selected ? UI::bg_active : (hovered ? UI::bg_hover : UI::bg_panel),
								.border = Foundation::BorderStyle{
									.color = selected ? UI::accent : UI::line_edge,
									.width = UI::bw}}});
			if (selected) {
				drawRect({.bounds = {r.x, r.y, 2.0F, r.height}, .style = {.fill = UI::accent}});
			}

			drawText({.text = s.name,
					  .position = {r.x + UI::space_4, r.y + 10.0F},
					  .scale = textScale(UI::fs_lg),
					  .color = selected ? UI::accent_bright : UI::text_bright,
					  .font = UI::fontDisplay,
					  .vAlign = Foundation::VerticalAlign::Top});

			UI::Text blurb(UI::Text::Args{
				.position = {r.x + UI::space_4, r.y + 36.0F},
				.width = r.width - UI::space_4 * 2.0F - kMetaWidth,
				.text = s.blurb,
				.style = {.color = UI::text_dim,
						  .fontSize = UI::fs_sm,
						  .wordWrap = true},
			});
			blurb.render();

			// Right meta block: difficulty pips over party size over tags.
			const float metaRight = r.x + r.width - UI::space_4;
			constexpr float kPipW = 14.0F;
			constexpr float kPipGap = 4.0F;
			const float pipsW = 5.0F * kPipW + 4.0F * kPipGap;
			for (int pip = 1; pip <= 5; ++pip) {
				drawRect({.bounds = {metaRight - pipsW + static_cast<float>(pip - 1) * (kPipW + kPipGap),
									 r.y + 14.0F, kPipW, 4.0F},
						  .style = {.fill = pipColor(pip, s.difficulty)}});
			}
			const std::string party =
				std::format("{} survivor{}", s.partyCount, s.partyCount == 1 ? "" : "s");
			drawText({.text = party,
					  .position = {metaRight - kMetaWidth, r.y + 26.0F},
					  .scale = textScale(UI::fs_xs),
					  .color = UI::text_dim,
					  .font = UI::fontMono,
					  .hAlign = Foundation::HorizontalAlign::Right,
					  .vAlign = Foundation::VerticalAlign::Top,
					  .boxWidth = kMetaWidth});
			drawText({.text = s.tags,
					  .position = {metaRight - kMetaWidth, r.y + 46.0F},
					  .scale = textScale(UI::fs_2xs),
					  .color = UI::text_faint,
					  .font = UI::fontMono,
					  .hAlign = Foundation::HorizontalAlign::Right,
					  .vAlign = Foundation::VerticalAlign::Top,
					  .boxWidth = kMetaWidth,
					  .letterSpacing = UI::fs_2xs * UI::ls_wide,
					  .transform = Foundation::TextTransform::Uppercase});
		}

		int rowAtPoint(Foundation::Vec2 p) const {
			for (size_t i = 0; i < rowRects.size(); ++i) {
				if (rowRects[i].contains(p)) return static_cast<int>(i);
			}
			return -1;
		}

		void goBack() {
			sceneManager->switchTo(world_sim::toKey(world_sim::SceneType::MainMenu));
		}

		void confirm() {
			world_sim::NewGameSetup::Get().scenarioId =
				world_sim::kScenarios[static_cast<size_t>(selectedIndex)].id;
			sceneManager->switchTo(world_sim::toKey(world_sim::SceneType::PartySelect));
		}

		std::unique_ptr<UI::Button>	  backButton;
		std::unique_ptr<UI::Button>	  confirmButton;
		std::vector<Foundation::Rect> rowRects;
		float						  rowWidth = 760.0F;
		int							  selectedIndex = 0;
		int							  hoveredIndex = -1;
		bool						  needsLayout = true;
	};

} // namespace

// Export scene info for registry
namespace world_sim::scenes {
	extern const world_sim::SceneInfo ScenarioSelect = {kSceneName, []() {
		return std::make_unique<ScenarioSelectScene>();
	}};
} // namespace world_sim::scenes
