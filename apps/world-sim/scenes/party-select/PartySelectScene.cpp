// Party Select Scene - New Game step 2 of 3.
//
// Prototype-fidelity PartySelect: a 280px roster column of crew cards beside
// a raised, accented dossier panel for the selected member, over a shared
// header/footer column laid out by LayoutContainer. The roster scrolls when
// the party outgrows the column; the dossier scrolls its own content. The
// assembled party is stored in NewGameSetup and spawns at landing.

#include "NewGameSetup.h"
#include "SceneTypes.h"
#include "scenes/party-select/MockCrew.h"
#include "scenes/scenario-select/Scenarios.h"
#include "scenes/shared/Starfield.h"
#include "scenes/shared/UiStateDrain.h"
#include "scenes/shared/Widgets.h"
#include <GL/glew.h>

#include <components/avatar/Avatar.h>
#include <components/badge/Badge.h>
#include <components/button/Button.h>
#include <components/panel/Panel.h>
#include <components/progress/ProgressBar.h>
#include <components/stat/Stat.h>
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

#include <algorithm>
#include <array>
#include <format>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace {

	constexpr const char* kSceneName = "party_select";
	constexpr float		  kRosterWidth = 280.0F;
	constexpr float		  kCardHeight = 64.0F;

	size_t partySizeForScenario(const std::string& scenarioId) {
		for (const world_sim::ScenarioDef& s : world_sim::kScenarios) {
			if (scenarioId == s.id) return static_cast<size_t>(s.partyCount);
		}
		return 3; // direct scene jumps arrive with no scenario chosen
	}

	std::string survivorLine(size_t n) {
		constexpr std::array<const char*, 8> kWords{"One", "Two", "Three", "Four",
													"Five", "Six", "Seven", "Eight"};
		const std::string count = (n >= 1 && n <= kWords.size()) ? kWords[n - 1] : std::to_string(n);
		return std::format("{} survivor{} walked away from the wreck. Learn who they are.",
						   count, n == 1 ? "" : "s");
	}

	const char* moodLabel(float mood) {
		if (mood < 0.3F) return "Distressed";
		if (mood < 0.55F) return "Uneasy";
		if (mood < 0.75F) return "Stable";
		return "Content";
	}

	UI::Tone moodTone(float mood) {
		if (mood < 0.3F) return UI::Tone::Crit;
		if (mood < 0.55F) return UI::Tone::Warn;
		return UI::Tone::Ok;
	}

	UI::Tone traitTone(world_sim::TraitTone tone) {
		switch (tone) {
			case world_sim::TraitTone::Good: return UI::Tone::Ok;
			case world_sim::TraitTone::Bad: return UI::Tone::Crit;
			case world_sim::TraitTone::Neutral: break;
		}
		return UI::Tone::Default;
	}

	struct RosterSlot {
		const world_sim::CrewDef* def; // template: origin/backstory/skills/traits
		std::string				  name;
		std::string				  role;
		int						  age;
		float					  mood;
	};

	void drawCardTicks(const Foundation::Rect& r, Foundation::Color color) {
		using Renderer::Primitives::drawRect;
		constexpr float kInset = 3.0F;
		constexpr float kLen = 6.0F;
		const float x0 = r.x + kInset;
		const float y0 = r.y + kInset;
		const float x1 = r.x + r.width - kInset;
		const float y1 = r.y + r.height - kInset;
		drawRect({.bounds = {x0, y0, kLen, 1.0F}, .style = {.fill = color}});
		drawRect({.bounds = {x0, y0, 1.0F, kLen}, .style = {.fill = color}});
		drawRect({.bounds = {x1 - kLen, y0, kLen, 1.0F}, .style = {.fill = color}});
		drawRect({.bounds = {x1 - 1.0F, y0, 1.0F, kLen}, .style = {.fill = color}});
		drawRect({.bounds = {x0, y1 - 1.0F, kLen, 1.0F}, .style = {.fill = color}});
		drawRect({.bounds = {x0, y1 - kLen, 1.0F, kLen}, .style = {.fill = color}});
		drawRect({.bounds = {x1 - kLen, y1 - 1.0F, kLen, 1.0F}, .style = {.fill = color}});
		drawRect({.bounds = {x1 - 1.0F, y1 - kLen, 1.0F, kLen}, .style = {.fill = color}});
	}

	class RosterCard : public world_sim::Widget {
	  public:
		RosterCard(int index, std::function<void(int)> onSelect)
			: index(index), onSelect(std::move(onSelect)) {
			widthMode = UI::SizeMode::Hug;
			size.y = kCardHeight;
		}

		void setSlot(const RosterSlot& slot) {
			name = slot.name;
			role = slot.role;
			mood = slot.mood;
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
			drawRect({.bounds = r,
					  .style = {.fill = selected ? UI::bg_active : (hovered ? UI::bg_hover : UI::bg_panel),
								.border = Foundation::BorderStyle{
									.color = selected ? UI::accent : (hovered ? UI::line_edge : UI::line_hairline),
									.width = UI::bw}}});
			if (selected || hovered) {
				drawCardTicks(r, selected ? UI::accent : UI::line_edge);
			}

			UI::Avatar({.position = {r.x + UI::space_3, r.y + (r.height - 40.0F) * 0.5F},
						.size = 40.0F,
						.seed = name,
						.mood = mood,
						.selected = selected})
				.render();

			const float textX = r.x + UI::space_3 + 40.0F + UI::space_3;
			drawText({.text = name,
					  .position = {textX, r.y + 14.0F},
					  .scale = UI::fs_md / 16.0F,
					  .color = UI::text_bright,
					  .font = UI::fontDisplay,
					  .vAlign = Foundation::VerticalAlign::Top});
			drawText({.text = role,
					  .position = {textX, r.y + 36.0F},
					  .scale = UI::fs_2xs / 16.0F,
					  .color = UI::text_faint,
					  .font = UI::fontMono,
					  .vAlign = Foundation::VerticalAlign::Top,
					  .letterSpacing = UI::fs_2xs * UI::ls_wide,
					  .transform = Foundation::TextTransform::Uppercase});

			// Mood column, right-aligned: meter over its label.
			constexpr float kMoodW = 56.0F;
			const float		moodX = r.x + r.width - UI::space_3 - kMoodW;
			UI::ProgressBar moodBar({.position = {moodX, r.y + 22.0F},
									 .width = kMoodW,
									 .value = mood,
									 .tone = UI::Tone::Auto,
									 .size = UI::Size::Sm});
			moodBar.render();
			drawText({.text = moodLabel(mood),
					  .position = {moodX, r.y + 32.0F},
					  .scale = UI::fs_2xs / 16.0F,
					  .color = UI::text_faint,
					  .font = UI::fontMono,
					  .hAlign = Foundation::HorizontalAlign::Right,
					  .vAlign = Foundation::VerticalAlign::Top,
					  .boxWidth = kMoodW,
					  .letterSpacing = UI::fs_2xs * UI::ls_wide,
					  .transform = Foundation::TextTransform::Uppercase});
		}

		const char* debugTypeName() const override { return "RosterCard"; }

	  private:
		int						 index;
		std::function<void(int)> onSelect;
		std::string				 name;
		std::string				 role;
		float					 mood{1.0F};
		bool					 hovered{false};
	};

	// --- Dossier content rows (local coords inside the dossier scroll) ---

	class HeroRow : public world_sim::Widget {
	  public:
		HeroRow() {
			widthMode = UI::SizeMode::Hug;
			size.y = 92.0F;
		}
		std::string name;
		std::string role;
		float		mood{1.0F};

		void render() override {
			using Renderer::Primitives::drawRect;
			using Renderer::Primitives::drawText;
			UI::Avatar({.position = {position.x, position.y}, .size = 72.0F, .seed = name, .mood = mood}).render();
			drawText({.text = name,
					  .position = {position.x + 88.0F, position.y + 6.0F},
					  .scale = UI::fs_2xl / 16.0F,
					  .color = UI::text_bright,
					  .font = UI::fontDisplay,
					  .vAlign = Foundation::VerticalAlign::Top});
			drawText({.text = role,
					  .position = {position.x + 88.0F, position.y + 44.0F},
					  .scale = UI::fs_xs / 16.0F,
					  .color = UI::accent,
					  .font = UI::fontMono,
					  .vAlign = Foundation::VerticalAlign::Top,
					  .letterSpacing = UI::fs_xs * UI::ls_wide,
					  .transform = Foundation::TextTransform::Uppercase});
			drawRect({.bounds = {position.x, position.y + size.y - 1.0F, size.x, 1.0F},
					  .style = {.fill = UI::line_hairline}});
		}
		const char* debugTypeName() const override { return "HeroRow"; }
	};

	class StatsRow : public world_sim::Widget {
	  public:
		StatsRow() {
			widthMode = UI::SizeMode::Hug;
			size.y = 44.0F;
		}
		std::string origin;
		int			age{0};
		float		mood{1.0F};

		void render() override {
			UI::Stat({.position = {position.x, position.y},
					  .label = "Origin",
					  .value = origin,
					  .size = UI::Size::Sm})
				.render();
			UI::Stat({.position = {position.x + 280.0F, position.y},
					  .label = "Age",
					  .value = std::to_string(age),
					  .unit = " yrs",
					  .size = UI::Size::Sm})
				.render();
			UI::Stat({.position = {position.x + 420.0F, position.y},
					  .label = "Mood",
					  .value = moodLabel(mood),
					  .tone = moodTone(mood),
					  .size = UI::Size::Sm})
				.render();
		}
		const char* debugTypeName() const override { return "StatsRow"; }
	};

	class SkillRow : public world_sim::Widget {
	  public:
		SkillRow(const world_sim::CrewSkillDef& skill) : skill(&skill) {
			widthMode = UI::SizeMode::Hug;
			size.y = 20.0F;
		}

		void render() override {
			using Renderer::Primitives::drawText;
			constexpr float kNameW = 140.0F;
			constexpr float kValueW = 32.0F;
			drawText({.text = skill->name,
					  .position = {position.x, position.y + 2.0F},
					  .scale = UI::fs_xs / 16.0F,
					  .color = UI::text_dim,
					  .font = UI::fontMono,
					  .vAlign = Foundation::VerticalAlign::Top,
					  .letterSpacing = UI::fs_xs * UI::ls_wide,
					  .transform = Foundation::TextTransform::Uppercase});
			UI::ProgressBar bar({.position = {position.x + kNameW, position.y + 6.0F},
								 .width = std::max(40.0F, size.x - kNameW - kValueW - UI::space_2),
								 .value = skill->level / 20.0F,
								 .tone = UI::Tone::Accent,
								 .size = UI::Size::Sm});
			bar.render();
			drawText({.text = std::format("{:.0f}", skill->level),
					  .position = {position.x + size.x - kValueW, position.y + 2.0F},
					  .scale = UI::fs_xs / 16.0F,
					  .color = UI::accent_bright,
					  .font = UI::fontMono,
					  .hAlign = Foundation::HorizontalAlign::Right,
					  .vAlign = Foundation::VerticalAlign::Top,
					  .boxWidth = kValueW});
		}
		const char* debugTypeName() const override { return "SkillRow"; }

	  private:
		const world_sim::CrewSkillDef* skill;
	};

	class TraitsRow : public world_sim::Widget {
	  public:
		explicit TraitsRow(const world_sim::CrewDef& def) : def(&def) {
			widthMode = UI::SizeMode::Hug;
			size.y = 24.0F;
		}

		void render() override {
			float badgeX = position.x;
			for (const world_sim::CrewTraitDef& trait : def->traits) {
				if (trait.name == nullptr) continue;
				const float badgeW = UI::Badge::MeasureWidth(trait.name);
				if (badgeX + badgeW > position.x + size.x && badgeX > position.x) break;
				UI::Badge({.position = {badgeX, position.y}, .label = trait.name, .tone = traitTone(trait.tone)})
					.render();
				badgeX += badgeW + UI::space_2;
			}
		}
		const char* debugTypeName() const override { return "TraitsRow"; }

	  private:
		const world_sim::CrewDef* def;
	};

	// Raised accent panel hosting the scrollable dossier for the active member.
	class DossierView : public world_sim::Widget {
	  public:
		DossierView() {
			widthMode = UI::SizeMode::Fill;
			heightMode = UI::SizeMode::Fill;
			scroll = std::make_unique<UI::ScrollContainer>(UI::ScrollContainer::Args{.size = {1.0F, 1.0F}});
		}

		void setMember(const RosterSlot& slot) {
			scroll->clearChildren();
			scroll->scrollToTop();

			UI::LayoutContainer content(UI::LayoutContainer::Args{
				.direction = UI::Direction::Vertical,
				.gap = UI::space_4,
				.crossAlign = UI::CrossAlign::Stretch,
				.id = "dossier_content"});

			HeroRow hero;
			hero.name = slot.name;
			hero.role = slot.role;
			hero.mood = slot.mood;
			content.addChild(std::move(hero));

			StatsRow stats;
			stats.origin = slot.def->origin;
			stats.age = slot.age;
			stats.mood = slot.mood;
			content.addChild(std::move(stats));

			content.addChild(world_sim::DividerRow("Background"));
			content.addChild(UI::Text(UI::Text::Args{
				.text = slot.def->backstory,
				.style = {.color = UI::text, .fontSize = UI::fs_base, .wordWrap = true},
				.id = "dossier_backstory"}));

			content.addChild(world_sim::DividerRow("Skills"));
			for (const world_sim::CrewSkillDef& skill : slot.def->skills) {
				content.addChild(SkillRow(skill));
			}

			content.addChild(world_sim::DividerRow("Traits"));
			content.addChild(TraitsRow(*slot.def));

			scroll->addChild(std::move(content));
		}

		void render() override {
			UI::Panel panel({.position = position,
							 .size = size,
							 .variant = UI::PanelVariant::Raised,
							 .accent = UI::PanelAccent::Accent});
			panel.render();

			const Foundation::Rect body = panel.bodyBounds();
			const Foundation::Vec2 scrollPos = scroll->getPosition();
			if (scrollPos.x != body.x || scrollPos.y != body.y) {
				scroll->setPosition(body.x, body.y);
			}
			if (scroll->size.x != body.width || scroll->size.y != body.height) {
				scroll->setViewportSize({body.width, body.height});
			}
			if (!scroll->getChildren().empty()) {
				scroll->getChildren().front()->setLayoutSize(body.width - 12.0F, UI::kSizeKeep);
			}
			scroll->render();
		}

		bool handleEvent(UI::InputEvent& event) override { return scroll->handleEvent(event); }
		bool containsPoint(Foundation::Vec2 p) const override { return scroll->containsPoint(p); }

		const char* debugTypeName() const override { return "DossierView"; }
		const char* debugId() const override { return "party_dossier"; }

	  private:
		std::unique_ptr<UI::ScrollContainer> scroll;
	};

	class PartySelectScene : public engine::IScene {
	  public:
		void onEnter() override {
			LOG_INFO(Game, "PartySelectScene - Entering");

			const auto& setup = world_sim::NewGameSetup::Get();
			partySize = partySizeForScenario(setup.scenarioId);
			// Re-seed whenever the stored party doesn't match the scenario's
			// size (first visit, or Back + a different scenario pick).
			if (setup.party.size() == partySize) {
				restoreRoster(setup.party);
			} else {
				resetRoster();
			}
			selectedIndex = 0;
			lastViewport = {0.0F, 0.0F};
			buildUI();
		}

		void onExit() override {
			LOG_INFO(Game, "PartySelectScene - Exiting");
			// Persist the crew so Back from the world creator (and the final
			// land) sees the same roster.
			world_sim::NewGameSetup::Get().party = buildParty();
			root.reset();
			cards.clear();
			dossier = nullptr;
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
			world_sim::renderStarfield(static_cast<int>(screenW), static_cast<int>(screenH), 57U, true);

			if (root != nullptr && (screenW != lastViewport.x || screenH != lastViewport.y) && screenW > 1.0F) {
				lastViewport = {screenW, screenH};
				root->layout({0.0F, 0.0F, screenW, screenH});
			}
			if (root) root->render();

			world_sim::serveUiStateRequests(*this);
		}

		std::string exportState() override {
			std::string names;
			for (size_t i = 0; i < roster.size(); ++i) {
				names += std::format("{}\"{}\"", i > 0 ? "," : "", roster[i].name);
			}
			return std::format(R"({{"scene":"party_select","selected":"{}","roster":[{}]}})",
							   roster[static_cast<size_t>(selectedIndex)].name, names);
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
				.size = {1.0F, 1.0F},
				.direction = Direction::Vertical,
				.gap = space_5,
				.padding = Insets{space_8, space_10, space_8, space_10},
				.crossAlign = CrossAlign::Stretch,
				.id = "party_root"});

			LayoutContainer header(LayoutContainer::Args{
				.direction = Direction::Vertical, .gap = space_2, .crossAlign = CrossAlign::Stretch,
				.id = "party_header"});
			header.addChild(world_sim::Label({
				.text = "// NEW GAME    STEP 02 / 03",
				.fontSize = fs_2xs,
				.color = accent,
				.font = fontMono,
				.letterSpacingEm = ls_wider,
				.transform = Foundation::TextTransform::Uppercase,
				.id = "kicker"}));
			header.addChild(world_sim::Label({
				.text = "Assemble the Crew",
				.fontSize = fs_3xl,
				.color = text_bright,
				.font = fontDisplay,
				.id = "title"}));
			header.addChild(Text(Text::Args{
				.text = survivorLine(partySize),
				.style = {.color = text_dim, .fontSize = fs_base, .wordWrap = true},
				.id = "subtitle"}));
			root->addChild(std::move(header));

			// Main two-pane row: fixed roster column | dossier fills the rest.
			LayoutContainer main(LayoutContainer::Args{
				.direction = Direction::Horizontal, .gap = space_5, .crossAlign = CrossAlign::Stretch,
				.id = "party_main"});
			main.heightMode = SizeMode::Fill;

			LayoutContainer rosterCol(LayoutContainer::Args{
				.size = {kRosterWidth, 0.0F},
				.direction = Direction::Vertical,
				.gap = space_3,
				.crossAlign = CrossAlign::Stretch,
				.id = "party_roster"});

			world_sim::ScrollRegion rosterScroll("roster_scroll");
			rosterScroll.widthMode = SizeMode::Hug;
			rosterScroll.heightMode = SizeMode::Fill;
			LayoutContainer rosterList(LayoutContainer::Args{
				.direction = Direction::Vertical, .gap = space_2, .crossAlign = CrossAlign::Stretch,
				.id = "roster_list"});
			std::vector<LayerHandle> cardHandles;
			for (size_t i = 0; i < roster.size(); ++i) {
				RosterCard card(static_cast<int>(i), [this](int idx) { select(idx); });
				card.setSlot(roster[i]);
				cardHandles.push_back(rosterList.addChild(std::move(card)));
			}
			auto* listPtr = &rosterScroll.container();
			LayerHandle listHandle = rosterScroll.container().addChild(std::move(rosterList));
			rosterCol.addChild(std::move(rosterScroll));

			LayoutContainer actions(LayoutContainer::Args{
				.direction = Direction::Vertical, .gap = space_2, .crossAlign = CrossAlign::Stretch,
				.id = "roster_actions"});
			Rectangle actionsRule(Rectangle::Args{.size = {0.0F, 1.0F}, .style = {.fill = line_hairline}});
			actionsRule.widthMode = SizeMode::Hug;
			actions.addChild(actionsRule);
			LayoutContainer actionsRow(LayoutContainer::Args{
				.direction = Direction::Horizontal, .distribution = Distribution::SpaceBetween,
				.crossAlign = CrossAlign::Center, .id = "roster_actions_row"});
			actionsRow.addChild(Button(Button::Args{
				.label = "Randomize",
				.size = {130.0F, 36.0F},
				.type = Button::Type::Data,
				.onClick = [this]() { randomize(); },
				.id = "btn_party_randomize",
				.iconGlyph = "dice"}));
			actionsRow.addChild(world_sim::Label({
				.text = std::format("{} / {} SLOTS", roster.size(), partySize),
				.fontSize = fs_2xs,
				.color = text_faint,
				.font = fontMono,
				.letterSpacingEm = ls_wide,
				.id = "slots_label"}));
			actions.addChild(std::move(actionsRow));
			rosterCol.addChild(std::move(actions));
			main.addChild(std::move(rosterCol));

			LayerHandle dossierHandle = main.addChild(DossierView());
			LayerHandle mainHandle = root->addChild(std::move(main));

			LayoutContainer footer(LayoutContainer::Args{
				.direction = Direction::Vertical, .gap = space_3, .crossAlign = CrossAlign::Stretch,
				.id = "party_footer"});
			Rectangle hairline(Rectangle::Args{.size = {0.0F, 1.0F}, .style = {.fill = line_hairline}, .id = "footer_rule"});
			hairline.widthMode = SizeMode::Hug;
			footer.addChild(hairline);
			LayoutContainer footerRow(LayoutContainer::Args{
				.direction = Direction::Horizontal, .distribution = Distribution::SpaceBetween,
				.crossAlign = CrossAlign::Center, .id = "party_footer_row"});
			footerRow.addChild(Button(Button::Args{
				.label = "Back",
				.size = {120.0F, 40.0F},
				.type = Button::Type::Secondary,
				.onClick = [this]() { goBack(); },
				.id = "btn_party_back",
				.iconGlyph = "chevronLeft"}));
			footerRow.addChild(Button(Button::Args{
				.label = "Generate Planet",
				.size = {220.0F, 40.0F},
				.type = Button::Type::Primary,
				.onClick = [this]() { confirm(); },
				.id = "btn_party_generate",
				.iconGlyph = "arrowRight"}));
			footer.addChild(std::move(footerRow));
			root->addChild(std::move(footer));

			// Resolve stable pointers now that everything is arena-placed.
			auto* mainPtr = root->getChild<LayoutContainer>(mainHandle);
			dossier = mainPtr->getChild<DossierView>(dossierHandle);
			auto* list = listPtr->getChild<LayoutContainer>(listHandle);
			cards.clear();
			for (LayerHandle handle : cardHandles) {
				cards.push_back(list->getChild<RosterCard>(handle));
			}
			select(selectedIndex);
		}

		void select(int idx) {
			selectedIndex = idx;
			for (size_t i = 0; i < cards.size(); ++i) {
				if (cards[i] != nullptr) cards[i]->selected = static_cast<int>(i) == idx;
			}
			if (dossier != nullptr) dossier->setMember(roster[static_cast<size_t>(idx)]);
		}

		void refreshCards() {
			for (size_t i = 0; i < cards.size() && i < roster.size(); ++i) {
				if (cards[i] != nullptr) cards[i]->setSlot(roster[i]);
			}
		}

		void resetRoster() {
			roster.clear();
			for (size_t i = 0; i < partySize; ++i) {
				const world_sim::CrewDef& def = world_sim::kCrewPool[i % world_sim::kCrewPool.size()];
				RosterSlot slot{&def, def.name, def.role, def.age, def.mood};
				// Slots past the pool reuse a template but get a distinct
				// identity from the reroll pools.
				if (i >= world_sim::kCrewPool.size()) {
					const size_t extra = i - world_sim::kCrewPool.size();
					slot.name = world_sim::kRerollNames[extra % world_sim::kRerollNames.size()];
					slot.role = world_sim::kRerollRoles[extra % world_sim::kRerollRoles.size()];
					slot.age = 26 + static_cast<int>(extra) * 4;
					slot.mood = 0.55F + 0.08F * static_cast<float>(extra % 4);
				}
				roster.push_back(std::move(slot));
			}
		}

		void restoreRoster(const std::vector<world_sim::PartyMember>& party) {
			roster.clear();
			for (size_t i = 0; i < party.size(); ++i) {
				const world_sim::PartyMember& member = party[i];
				// Origin is unique per template, so it recovers the CrewDef the
				// member was rolled from.
				const world_sim::CrewDef* def = &world_sim::kCrewPool[i % world_sim::kCrewPool.size()];
				for (const world_sim::CrewDef& candidate : world_sim::kCrewPool) {
					if (member.origin == candidate.origin) {
						def = &candidate;
						break;
					}
				}
				roster.push_back({def, member.name, member.role, member.age, member.mood});
			}
			if (roster.empty()) resetRoster();
		}

		void randomize() {
			std::array<size_t, world_sim::kCrewPool.size()> defOrder{};
			for (size_t i = 0; i < defOrder.size(); ++i) defOrder[i] = i;
			std::shuffle(defOrder.begin(), defOrder.end(), rng);
			// Drawing reroll names from a shuffled order keeps them unique
			// without rejection sampling.
			std::array<size_t, world_sim::kRerollNames.size()> nameOrder{};
			for (size_t i = 0; i < nameOrder.size(); ++i) nameOrder[i] = i;
			std::shuffle(nameOrder.begin(), nameOrder.end(), rng);
			size_t nextName = 0;

			roster.clear();
			for (size_t i = 0; i < partySize; ++i) {
				const world_sim::CrewDef& def = world_sim::kCrewPool[defOrder[i % defOrder.size()]];
				RosterSlot slot{&def, def.name, def.role, def.age, def.mood};
				// Slots past the pool repeat a template, so they must reroll to
				// stay distinct; pool slots reroll half the time for variety.
				if (i >= defOrder.size() || std::bernoulli_distribution{0.5}(rng)) {
					slot.name = world_sim::kRerollNames[nameOrder[nextName++]];
					slot.role = world_sim::kRerollRoles[std::uniform_int_distribution<size_t>{
						0, world_sim::kRerollRoles.size() - 1}(rng)];
					slot.age = std::uniform_int_distribution<int>{23, 45}(rng);
					slot.mood = std::uniform_real_distribution<float>{0.35F, 0.9F}(rng);
				}
				roster.push_back(std::move(slot));
			}
			refreshCards();
			select(0);
			LOG_INFO(Game, "PartySelectScene - Randomized crew");
		}

		std::vector<world_sim::PartyMember> buildParty() const {
			std::vector<world_sim::PartyMember> party;
			for (const RosterSlot& slot : roster) {
				world_sim::PartyMember member;
				member.name = slot.name;
				member.role = slot.role;
				member.origin = slot.def->origin;
				member.age = slot.age;
				member.mood = slot.mood;
				for (const world_sim::CrewSkillDef& skill : slot.def->skills) {
					member.skills.emplace(skill.name, skill.level);
				}
				for (const world_sim::CrewTraitDef& trait : slot.def->traits) {
					if (trait.name != nullptr) member.traits.emplace_back(trait.name);
				}
				member.backstory = slot.def->backstory;
				party.push_back(std::move(member));
			}
			return party;
		}

		void goBack() {
			sceneManager->switchTo(world_sim::toKey(world_sim::SceneType::ScenarioSelect));
		}

		void confirm() {
			sceneManager->switchTo(world_sim::toKey(world_sim::SceneType::WorldCreator));
		}

		std::vector<RosterSlot>				 roster;
		std::unique_ptr<UI::LayoutContainer> root;
		std::vector<RosterCard*>			 cards;
		DossierView*						 dossier{nullptr};
		std::mt19937						 rng{std::random_device{}()};
		size_t								 partySize = 3;
		Foundation::Vec2					 lastViewport{0.0F, 0.0F};
		int									 selectedIndex = 0;
	};

} // namespace

// Export scene info for registry
namespace world_sim::scenes {
	extern const world_sim::SceneInfo PartySelect = {kSceneName, []() {
		return std::make_unique<PartySelectScene>();
	}};
} // namespace world_sim::scenes
