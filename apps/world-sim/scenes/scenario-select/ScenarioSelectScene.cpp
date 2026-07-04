// Scenario Select Scene - New Game step 1 of 3.
//
// Prototype-fidelity ScenarioSelect: header, a five-card grid (equal widths
// via Fill), and a Back/Confirm footer, all laid out by LayoutContainer.
// Cards carry difficulty pips, party count, tag badges, corner ticks, and a
// selected glow + check.

#include "NewGameSetup.h"
#include "SceneTypes.h"
#include "scenes/scenario-select/Scenarios.h"
#include "scenes/shared/Starfield.h"
#include "scenes/shared/UiStateDrain.h"
#include "scenes/shared/Widgets.h"
#include <GL/glew.h>

#include <components/badge/Badge.h>
#include <components/button/Button.h>
#include <components/icon/Icon.h>
#include <graphics/PrimitiveStyles.h>
#include <input/InputEvent.h>
#include <input/InputManager.h>
#include <layout/LayoutContainer.h>
#include <primitives/Primitives.h>
#include <scene/Scene.h>
#include <scene/SceneManager.h>
#include <shapes/Shapes.h>
#include <theme/Tokens.h>
#include <theme/Variants.h>
#include <utils/Log.h>

#include <cstring>
#include <format>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace {

	constexpr const char* kSceneName = "scenario_select";
	constexpr float		  kCardPad = UI::space_5;

	Foundation::Color pipColor(int pip, int difficulty) {
		if (pip > difficulty) return UI::text_faint;
		if (difficulty <= 2) return UI::status_ok;
		if (difficulty <= 3) return UI::status_warn;
		return UI::status_crit;
	}

	// Four L-bracket ticks at the corners of `r`, inset by `inset`, arm `len`.
	void drawCornerTicks(const Foundation::Rect& r, float inset, float len, Foundation::Color color) {
		using Renderer::Primitives::drawRect;
		const float x0 = r.x + inset;
		const float y0 = r.y + inset;
		const float x1 = r.x + r.width - inset;
		const float y1 = r.y + r.height - inset;
		// Top-left
		drawRect({.bounds = {x0, y0, len, 1.0F}, .style = {.fill = color}});
		drawRect({.bounds = {x0, y0, 1.0F, len}, .style = {.fill = color}});
		// Top-right
		drawRect({.bounds = {x1 - len, y0, len, 1.0F}, .style = {.fill = color}});
		drawRect({.bounds = {x1 - 1.0F, y0, 1.0F, len}, .style = {.fill = color}});
		// Bottom-left
		drawRect({.bounds = {x0, y1 - 1.0F, len, 1.0F}, .style = {.fill = color}});
		drawRect({.bounds = {x0, y1 - len, 1.0F, len}, .style = {.fill = color}});
		// Bottom-right
		drawRect({.bounds = {x1 - len, y1 - 1.0F, len, 1.0F}, .style = {.fill = color}});
		drawRect({.bounds = {x1 - 1.0F, y1 - len, 1.0F, len}, .style = {.fill = color}});
	}

	class ScenarioCard : public world_sim::Widget {
	  public:
		ScenarioCard(const world_sim::ScenarioDef& def, int index, std::function<void(int)> onSelect)
			: def(&def), index(index), onSelect(std::move(onSelect)) {
			widthMode = UI::SizeMode::Fill;
			heightMode = UI::SizeMode::Fill;
			checkIcon = std::make_unique<UI::Icon>(UI::Icon::Args{
				.size = 13.0F, .glyph = "check", .tint = UI::accent_contrast});
			scenarioIcon = std::make_unique<UI::Icon>(UI::Icon::Args{
				.size = 28.0F, .glyph = def.icon, .tint = UI::text_dim, .strokeWidth = 1.4F});
			for (const char* tag = def.tags;;) {
				const char* sep = std::strstr(tag, " / ");
				if (sep == nullptr) {
					tags.emplace_back(tag);
					break;
				}
				tags.emplace_back(tag, sep);
				tag = sep + 3;
			}
		}

		bool selected{false};

		bool handleEvent(UI::InputEvent& event) override {
			using UI::InputEvent;
			if (event.type == InputEvent::Type::MouseMove) {
				hovered = containsPoint(event.position);
				return false;
			}
			if (event.type == InputEvent::Type::MouseUp && event.button == engine::MouseButton::Left &&
			    containsPoint(event.position)) {
				onSelect(index);
				event.consume();
				return true;
			}
			return false;
		}

		bool containsPoint(Foundation::Vec2 p) const override {
			return p.x >= position.x && p.x <= position.x + size.x && p.y >= position.y &&
				   p.y <= position.y + size.y;
		}

		void render() override {
			using Renderer::Primitives::drawRect;
			using Renderer::Primitives::drawText;

			const Foundation::Rect r{position.x, position.y, size.x, size.y};

			if (selected) {
				// Faux outer glow behind the card.
				drawRect({.bounds = {r.x - 2.0F, r.y - 2.0F, r.width + 4.0F, r.height + 4.0F},
						  .style = {.fill = {0.0F, 0.0F, 0.0F, 0.0F},
									.border = Foundation::BorderStyle{.color = UI::accent_glow, .width = 2.0F}}});
			}
			drawRect({.bounds = r,
					  .style = {.fill = selected ? UI::bg_active : (hovered ? UI::bg_panel_raised : UI::bg_panel),
								.border = Foundation::BorderStyle{
									.color = selected ? UI::accent : (hovered ? UI::line_strong : UI::line_edge),
									.width = UI::bw}}});

			const Foundation::Color tickColor =
				selected ? UI::accent
						 : (hovered ? UI::accent_dim : UI::withAlpha(UI::line_edge, UI::line_edge.a * 0.5F));
			drawCornerTicks(r, 4.0F, 10.0F, tickColor);

			if (selected) {
				const Foundation::Vec2 c{r.x + r.width - UI::space_3 - 10.0F, r.y + UI::space_3 + 10.0F};
				Renderer::Primitives::drawCircle({.center = c, .radius = 10.0F, .style = {.fill = UI::accent}});
				checkIcon->setPosition(c.x - 6.5F, c.y - 6.5F);
				checkIcon->render();
			}

			scenarioIcon->setTint(selected ? UI::accent : (hovered ? UI::text : UI::text_dim));
			scenarioIcon->setPosition(r.x + kCardPad, r.y + kCardPad);
			scenarioIcon->render();

			drawText({.text = def->name,
					  .position = {r.x + kCardPad, r.y + kCardPad + 40.0F},
					  .scale = UI::fs_md / 16.0F,
					  .color = UI::text_bright,
					  .font = UI::fontDisplay,
					  .vAlign = Foundation::VerticalAlign::Top,
					  .letterSpacing = UI::fs_md * UI::ls_wide,
					  .transform = Foundation::TextTransform::Uppercase});

			UI::Text blurb(UI::Text::Args{
				.position = {r.x + kCardPad, r.y + kCardPad + 68.0F},
				.width = r.width - kCardPad * 2.0F,
				.text = def->blurb,
				.style = {.color = UI::text_dim, .fontSize = UI::fs_sm, .wordWrap = true},
			});
			blurb.render();

			// Meta row (pips left, party right) above the tag badges, both
			// anchored to the card bottom.
			const float tagsY = r.y + r.height - kCardPad - 20.0F;
			const float metaY = tagsY - 18.0F;
			constexpr float kPipW = 14.0F;
			constexpr float kPipGap = 3.0F;
			for (int pip = 1; pip <= 5; ++pip) {
				drawRect({.bounds = {r.x + kCardPad + static_cast<float>(pip - 1) * (kPipW + kPipGap),
									 metaY + 4.0F, kPipW, 4.0F},
						  .style = {.fill = pipColor(pip, def->difficulty)}});
			}
			drawText({.text = std::format("{} SURVIVOR{}", def->partyCount, def->partyCount == 1 ? "" : "S"),
					  .position = {r.x + kCardPad, metaY},
					  .scale = UI::fs_2xs / 16.0F,
					  .color = UI::text_faint,
					  .font = UI::fontMono,
					  .hAlign = Foundation::HorizontalAlign::Right,
					  .vAlign = Foundation::VerticalAlign::Top,
					  .boxWidth = r.width - kCardPad * 2.0F,
					  .letterSpacing = UI::fs_2xs * UI::ls_wide});

			float badgeX = r.x + kCardPad;
			for (const std::string& tag : tags) {
				const float badgeW = UI::Badge::MeasureWidth(tag);
				if (badgeX + badgeW > r.x + r.width - kCardPad && badgeX > r.x + kCardPad) break;
				UI::Badge({.position = {badgeX, tagsY}, .label = tag, .tone = UI::Tone::Default}).render();
				badgeX += badgeW + UI::space_1;
			}
		}

		const char* debugTypeName() const override { return "ScenarioCard"; }
		const char* debugId() const override { return def->id; }

	  private:
		const world_sim::ScenarioDef* def;
		int							  index;
		std::function<void(int)>	  onSelect;
		std::vector<std::string>	  tags;
		std::unique_ptr<UI::Icon>	  checkIcon;
		std::unique_ptr<UI::Icon>	  scenarioIcon;
		bool						  hovered{false};
	};

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

			lastViewport = {0.0F, 0.0F};
			buildUI();
		}

		void onExit() override {
			LOG_INFO(Game, "ScenarioSelectScene - Exiting");
			root.reset();
			cards.clear();
		}

		bool handleInput(UI::InputEvent& event) override {
			return root && root->handleEvent(event);
		}

		void update(float dt) override {
			if (engine::InputManager::Get().isKeyPressed(engine::Key::Escape)) {
				goBack();
				return;
			}
			if (root) root->update(dt);
		}

		void render() override {
			glClearColor(UI::bg_void.r, UI::bg_void.g, UI::bg_void.b, 1.0F);
			glClear(GL_COLOR_BUFFER_BIT);

			const float screenW = Renderer::Primitives::PercentWidth(100.0F);
			const float screenH = Renderer::Primitives::PercentHeight(100.0F);
			world_sim::renderStarfield(static_cast<int>(screenW), static_cast<int>(screenH), 42U, true);

			if (root != nullptr && (screenW != lastViewport.x || screenH != lastViewport.y) && screenW > 1.0F) {
				lastViewport = {screenW, screenH};
				root->layout({0.0F, 0.0F, screenW, screenH});
			}
			if (root) root->render();

			world_sim::serveUiStateRequests(*this);
		}

		std::string exportState() override {
			return std::format(R"({{"scene":"scenario_select","selected":"{}"}})",
							   world_sim::kScenarios[static_cast<size_t>(selectedIndex)].id);
		}
		const char* getName() const override { return kSceneName; }

		std::vector<const UI::IComponent*> getUiRoots() const override {
			if (!root) return {};
			return {root.get()};
		}

	  private:
		void buildUI() {
			using namespace UI;

			root = std::make_unique<LayoutContainer>(LayoutContainer::Args{
				.size = {1.0F, 1.0F}, // adopted from the viewport on first render
				.direction = Direction::Vertical,
				.gap = space_8,
				.padding = Insets{space_12, space_16, space_12, space_16},
				.crossAlign = CrossAlign::Stretch,
				.id = "scenario_root"});

			LayoutContainer header(LayoutContainer::Args{
				.direction = Direction::Vertical, .gap = space_2, .crossAlign = CrossAlign::Stretch,
				.id = "scenario_header"});
			header.addChild(world_sim::Label({
				.text = "// NEW GAME    STEP 01 / 03",
				.fontSize = fs_2xs,
				.color = accent,
				.font = fontMono,
				.letterSpacingEm = ls_wider,
				.transform = Foundation::TextTransform::Uppercase,
				.id = "kicker"}));
			header.addChild(world_sim::Label({
				.text = "Select Scenario",
				.fontSize = fs_3xl,
				.color = text_bright,
				.font = fontDisplay,
				.letterSpacingEm = ls_wide,
				.id = "title"}));
			header.addChild(Text(Text::Args{
				.text = "Each scenario reshapes your wreck site, salvage, and the world you'll fight to survive.",
				.style = {.color = text_dim, .fontSize = fs_md, .wordWrap = true},
				.id = "subtitle"}));
			root->addChild(std::move(header));

			LayoutContainer cardRow(LayoutContainer::Args{
				.direction = Direction::Horizontal, .gap = space_4, .crossAlign = CrossAlign::Stretch,
				.id = "scenario_cards"});
			cardRow.heightMode = SizeMode::Fill;
			std::vector<LayerHandle> cardHandles;
			for (size_t i = 0; i < world_sim::kScenarios.size(); ++i) {
				cardHandles.push_back(cardRow.addChild(ScenarioCard(
					world_sim::kScenarios[i], static_cast<int>(i), [this](int idx) { select(idx); })));
			}
			LayerHandle rowHandle = root->addChild(std::move(cardRow));

			LayoutContainer footer(LayoutContainer::Args{
				.direction = Direction::Vertical, .gap = space_4, .crossAlign = CrossAlign::Stretch,
				.id = "scenario_footer"});
			Rectangle hairline(Rectangle::Args{.size = {0.0F, 1.0F}, .style = {.fill = line_hairline}, .id = "footer_rule"});
			hairline.widthMode = SizeMode::Hug;
			footer.addChild(hairline);
			LayoutContainer footerRow(LayoutContainer::Args{
				.direction = Direction::Horizontal, .distribution = Distribution::SpaceBetween,
				.crossAlign = CrossAlign::Center, .id = "scenario_footer_row"});
			footerRow.addChild(Button(Button::Args{
				.label = "Back",
				.size = {120.0F, 40.0F},
				.type = Button::Type::Secondary,
				.onClick = [this]() { goBack(); },
				.id = "btn_scenario_back",
				.iconGlyph = "chevronLeft"}));
			footerRow.addChild(Button(Button::Args{
				.label = "Confirm Scenario",
				.size = {220.0F, 40.0F},
				.type = Button::Type::Primary,
				.onClick = [this]() { confirm(); },
				.id = "btn_scenario_confirm",
				.iconGlyph = "arrowRight"}));
			footer.addChild(std::move(footerRow));
			root->addChild(std::move(footer));

			// Resolve card pointers for selection state.
			auto* row = root->getChild<LayoutContainer>(rowHandle);
			cards.clear();
			for (LayerHandle handle : cardHandles) {
				cards.push_back(row->getChild<ScenarioCard>(handle));
			}
			select(selectedIndex);
		}

		void select(int idx) {
			selectedIndex = idx;
			for (size_t i = 0; i < cards.size(); ++i) {
				if (cards[i] != nullptr) cards[i]->selected = static_cast<int>(i) == idx;
			}
		}

		void goBack() {
			sceneManager->switchTo(world_sim::toKey(world_sim::SceneType::MainMenu));
		}

		void confirm() {
			world_sim::NewGameSetup::Get().scenarioId =
				world_sim::kScenarios[static_cast<size_t>(selectedIndex)].id;
			sceneManager->switchTo(world_sim::toKey(world_sim::SceneType::PartySelect));
		}

		std::unique_ptr<UI::LayoutContainer> root;
		std::vector<ScenarioCard*>			 cards;
		Foundation::Vec2					 lastViewport{0.0F, 0.0F};
		int									 selectedIndex = 0;
	};

} // namespace

// Export scene info for registry
namespace world_sim::scenes {
	extern const world_sim::SceneInfo ScenarioSelect = {kSceneName, []() {
		return std::make_unique<ScenarioSelectScene>();
	}};
} // namespace world_sim::scenes
