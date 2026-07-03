// Party Select Scene - New Game step 2 of 3.
//
// Stub of the prototype's PartySelect screen: a left roster of three crew
// cards, a right dossier panel for the selected member (identity, stats,
// backstory, skills, traits), a Randomize reroll, and a Back/Generate footer.
// Hand layout; full fidelity comes with the upgraded layout engine. The
// assembled party is stored in NewGameSetup and spawns at landing.

#include "NewGameSetup.h"
#include "SceneTypes.h"
#include "scenes/party-select/MockCrew.h"
#include "scenes/shared/Starfield.h"
#include <GL/glew.h>

#include <components/avatar/Avatar.h>
#include <components/badge/Badge.h>
#include <components/button/Button.h>
#include <components/divider/Divider.h>
#include <components/panel/Panel.h>
#include <components/progress/ProgressBar.h>
#include <components/stat/Stat.h>
#include <font/FontRenderer.h>
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
#include <array>
#include <format>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace {

	constexpr const char* kSceneName = "party_select";

	constexpr float kColX = 80.0F;
	constexpr float kContentTop = 150.0F;
	constexpr float kRosterWidth = 360.0F;
	constexpr float kCardHeight = 64.0F;
	constexpr float kCardGap = 8.0F;
	constexpr float kDetailGap = 24.0F;
	constexpr float kPad = 20.0F;

	float textScale(float px) { return px / 16.0F; }

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

	class PartySelectScene : public engine::IScene {
	  public:
		void onEnter() override {
			LOG_INFO(Game, "PartySelectScene - Entering");

			const auto& setup = world_sim::NewGameSetup::Get();
			if (setup.party.empty()) {
				resetRoster();
			} else {
				restoreRoster(setup.party);
			}
			selectedIndex = 0;
			hoveredIndex = -1;

			backButton = std::make_unique<UI::Button>(UI::Button::Args{
				.label = "Back",
				.size = {120.0F, 40.0F},
				.type = UI::Button::Type::Secondary,
				.onClick = [this]() { goBack(); },
				.id = "btn_party_back",
			});
			generateButton = std::make_unique<UI::Button>(UI::Button::Args{
				.label = "Generate Planet",
				.size = {220.0F, 40.0F},
				.type = UI::Button::Type::Primary,
				.onClick = [this]() { confirm(); },
				.id = "btn_party_generate",
			});
			randomizeButton = std::make_unique<UI::Button>(UI::Button::Args{
				.label = "Randomize",
				.size = {150.0F, 36.0F},
				.type = UI::Button::Type::Data,
				.onClick = [this]() { randomize(); },
				.id = "btn_party_randomize",
			});
			needsLayout = true;
		}

		void onExit() override {
			LOG_INFO(Game, "PartySelectScene - Exiting");
			// Persist the crew so Back from the world creator (and the final
			// land) sees the same roster.
			world_sim::NewGameSetup::Get().party = buildParty();
			backButton.reset();
			generateButton.reset();
			randomizeButton.reset();
			cardRects.clear();
		}

		bool handleInput(UI::InputEvent& event) override {
			if (backButton && backButton->handleEvent(event)) return true;
			if (generateButton && generateButton->handleEvent(event)) return true;
			if (randomizeButton && randomizeButton->handleEvent(event)) return true;

			using UI::InputEvent;
			if (event.type == InputEvent::Type::MouseMove) {
				hoveredIndex = cardAtPoint(event.position);
				return false;
			}
			if (event.type == InputEvent::Type::MouseUp && event.button == engine::MouseButton::Left) {
				const int idx = cardAtPoint(event.position);
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
			if (generateButton) generateButton->update(dt);
			if (randomizeButton) randomizeButton->update(dt);
		}

		void render() override {
			using Renderer::Primitives::drawText;

			if (needsLayout) layout();

			glClearColor(UI::bg_void.r, UI::bg_void.g, UI::bg_void.b, 1.0F);
			glClear(GL_COLOR_BUFFER_BIT);

			const float screenW = Renderer::Primitives::PercentWidth(100.0F);
			const float screenH = Renderer::Primitives::PercentHeight(100.0F);
			world_sim::renderStarfield(static_cast<int>(screenW), static_cast<int>(screenH), 57U, true);

			// Header.
			drawText({.text = "// NEW GAME    STEP 02 / 03",
					  .position = {kColX, 40.0F},
					  .scale = textScale(UI::fs_2xs),
					  .color = UI::text_faint,
					  .font = UI::fontMono,
					  .vAlign = Foundation::VerticalAlign::Top,
					  .letterSpacing = UI::fs_2xs * UI::ls_wider});
			drawText({.text = "Assemble the Crew",
					  .position = {kColX, 56.0F},
					  .scale = textScale(UI::fs_3xl),
					  .color = UI::text_bright,
					  .font = UI::fontDisplay,
					  .vAlign = Foundation::VerticalAlign::Top,
					  .letterSpacing = UI::fs_3xl * UI::ls_wide});
			drawText({.text = "Three survivors walked away from the wreck. Learn who they are.",
					  .position = {kColX, 106.0F},
					  .scale = textScale(UI::fs_md),
					  .color = UI::text_dim,
					  .font = UI::fontUi,
					  .vAlign = Foundation::VerticalAlign::Top});

			for (size_t i = 0; i < roster.size(); ++i) {
				renderCard(i);
			}

			// Roster actions row.
			if (randomizeButton) randomizeButton->render();
			drawText({.text = std::format("{} / {} SLOTS", roster.size(), world_sim::kPartySize),
					  .position = {kColX + 162.0F, rosterActionsY + 12.0F},
					  .scale = textScale(UI::fs_2xs),
					  .color = UI::text_faint,
					  .font = UI::fontMono,
					  .vAlign = Foundation::VerticalAlign::Top,
					  .letterSpacing = UI::fs_2xs * UI::ls_wide});

			renderDetail();

			if (backButton) backButton->render();
			if (generateButton) generateButton->render();
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

	  private:
		struct RosterSlot {
			const world_sim::CrewDef* def; // template: origin/backstory/skills/traits
			std::string				  name;
			std::string				  role;
			int						  age;
			float					  mood;
		};

		void layout() {
			const float screenW = Renderer::Primitives::PercentWidth(100.0F);
			const float screenH = Renderer::Primitives::PercentHeight(100.0F);
			if (screenW < 1.0F || screenH < 1.0F) return; // viewport not ready

			cardRects.clear();
			for (size_t i = 0; i < world_sim::kPartySize; ++i) {
				cardRects.push_back({kColX, kContentTop + static_cast<float>(i) * (kCardHeight + kCardGap),
									 kRosterWidth, kCardHeight});
			}
			rosterActionsY = kContentTop + static_cast<float>(world_sim::kPartySize) * (kCardHeight + kCardGap) + 8.0F;
			randomizeButton->setPosition(kColX, rosterActionsY);

			detailX = kColX + kRosterWidth + kDetailGap;
			detailW = std::min(640.0F, screenW - detailX - kColX);
			detailH = 494.0F;

			const float footerY = screenH - 68.0F;
			backButton->setPosition(kColX, footerY);
			generateButton->setPosition(detailX + detailW - 220.0F, footerY);
			needsLayout = false;
		}

		void renderCard(size_t i) {
			using Renderer::Primitives::drawRect;
			using Renderer::Primitives::drawText;

			const RosterSlot&		slot = roster[i];
			const Foundation::Rect& r = cardRects[i];
			const bool selected = static_cast<int>(i) == selectedIndex;
			const bool hovered = static_cast<int>(i) == hoveredIndex;

			drawRect({.bounds = r,
					  .style = {.fill = selected ? UI::bg_active : (hovered ? UI::bg_hover : UI::bg_panel),
								.border = Foundation::BorderStyle{
									.color = selected ? UI::accent : UI::line_edge,
									.width = UI::bw}}});

			UI::Avatar avatar({.position = {r.x + 12.0F, r.y + 12.0F},
							   .size = 40.0F,
							   .seed = slot.name,
							   .mood = slot.mood,
							   .selected = selected});
			avatar.render();

			drawText({.text = slot.name,
					  .position = {r.x + 64.0F, r.y + 12.0F},
					  .scale = textScale(UI::fs_md),
					  .color = selected ? UI::accent_bright : UI::text_bright,
					  .font = UI::fontDisplay,
					  .vAlign = Foundation::VerticalAlign::Top});
			drawText({.text = slot.role,
					  .position = {r.x + 64.0F, r.y + 34.0F},
					  .scale = textScale(UI::fs_xs),
					  .color = UI::text_dim,
					  .font = UI::fontMono,
					  .vAlign = Foundation::VerticalAlign::Top});

			UI::ProgressBar moodBar({.position = {r.x + 250.0F, r.y + 18.0F},
									 .width = 96.0F,
									 .value = slot.mood,
									 .tone = UI::Tone::Auto,
									 .size = UI::Size::Sm});
			moodBar.render();
			drawText({.text = moodLabel(slot.mood),
					  .position = {r.x + 250.0F, r.y + 32.0F},
					  .scale = textScale(UI::fs_2xs),
					  .color = UI::toneColor(moodTone(slot.mood)),
					  .font = UI::fontMono,
					  .hAlign = Foundation::HorizontalAlign::Right,
					  .vAlign = Foundation::VerticalAlign::Top,
					  .boxWidth = 96.0F});
		}

		void renderDetail() {
			using Renderer::Primitives::drawText;

			const RosterSlot&		  slot = roster[static_cast<size_t>(selectedIndex)];
			const world_sim::CrewDef& def = *slot.def;
			const float px = detailX;
			const float py = kContentTop;
			const float innerW = detailW - kPad * 2.0F;

			UI::Panel panel({.position = {px, py},
							 .size = {detailW, detailH},
							 .variant = UI::PanelVariant::Raised,
							 .accent = UI::PanelAccent::Accent});
			panel.render();

			// Hero: portrait + identity.
			UI::Avatar avatar({.position = {px + kPad, py + kPad},
							   .size = 72.0F,
							   .seed = slot.name,
							   .mood = slot.mood});
			avatar.render();
			drawText({.text = slot.name,
					  .position = {px + kPad + 88.0F, py + 24.0F},
					  .scale = textScale(UI::fs_2xl),
					  .color = UI::text_bright,
					  .font = UI::fontDisplay,
					  .vAlign = Foundation::VerticalAlign::Top});
			drawText({.text = slot.role,
					  .position = {px + kPad + 88.0F, py + 62.0F},
					  .scale = textScale(UI::fs_sm),
					  .color = UI::accent,
					  .font = UI::fontMono,
					  .vAlign = Foundation::VerticalAlign::Top,
					  .letterSpacing = UI::fs_sm * UI::ls_wide,
					  .transform = Foundation::TextTransform::Uppercase});

			// Stats row.
			const float statsY = py + 112.0F;
			UI::Stat({.position = {px + kPad, statsY},
					  .label = "Origin",
					  .value = def.origin,
					  .size = UI::Size::Sm})
				.render();
			UI::Stat({.position = {px + kPad + 280.0F, statsY},
					  .label = "Age",
					  .value = std::to_string(slot.age),
					  .unit = " yrs",
					  .size = UI::Size::Sm})
				.render();
			UI::Stat({.position = {px + kPad + 400.0F, statsY},
					  .label = "Mood",
					  .value = moodLabel(slot.mood),
					  .tone = moodTone(slot.mood),
					  .size = UI::Size::Sm})
				.render();

			// Background.
			UI::Divider({.position = {px + kPad, py + 168.0F}, .width = innerW, .label = "Background"}).render();
			UI::Text backstory(UI::Text::Args{
				.position = {px + kPad, py + 184.0F},
				.width = innerW,
				.text = def.backstory,
				.style = {.color = UI::text, .fontSize = UI::fs_base, .wordWrap = true},
			});
			backstory.render();

			// Skills.
			UI::Divider({.position = {px + kPad, py + 254.0F}, .width = innerW, .label = "Skills"}).render();
			float skillY = py + 272.0F;
			for (const world_sim::CrewSkillDef& skill : def.skills) {
				drawText({.text = skill.name,
						  .position = {px + kPad, skillY},
						  .scale = textScale(UI::fs_sm),
						  .color = UI::text,
						  .font = UI::fontUi,
						  .vAlign = Foundation::VerticalAlign::Top});
				UI::ProgressBar bar({.position = {px + kPad + 150.0F, skillY + 4.0F},
									 .width = innerW - 150.0F - 40.0F,
									 .value = skill.level / 20.0F,
									 .tone = UI::Tone::Accent,
									 .size = UI::Size::Sm});
				bar.render();
				drawText({.text = std::format("{:.0f}", skill.level),
						  .position = {px + kPad + innerW - 32.0F, skillY},
						  .scale = textScale(UI::fs_xs),
						  .color = UI::accent_bright,
						  .font = UI::fontMono,
						  .hAlign = Foundation::HorizontalAlign::Right,
						  .vAlign = Foundation::VerticalAlign::Top,
						  .boxWidth = 32.0F});
				skillY += 26.0F;
			}

			// Traits.
			UI::Divider({.position = {px + kPad, py + 438.0F}, .width = innerW, .label = "Traits"}).render();
			float badgeX = px + kPad;
			for (const world_sim::CrewTraitDef& trait : def.traits) {
				if (trait.name == nullptr) continue;
				UI::Badge({.position = {badgeX, py + 454.0F}, .label = trait.name, .tone = traitTone(trait.tone)}).render();
				badgeX += badgeWidth(trait.name) + UI::space_2;
			}
		}

		// Mirror of Badge's internal width so a row of badges can be advanced
		// without overlap (Badge doesn't expose measurement).
		static float badgeWidth(const std::string& label) {
			float labelWidth = 60.0F;
			if (const ui::FontRenderer* font = Renderer::Primitives::getFontRenderer(); font != nullptr) {
				labelWidth = font->MeasureText(label, textScale(UI::fs_2xs), UI::fontMono, UI::fs_2xs * UI::ls_wider).x;
			}
			return UI::space_2 * 2.0F + labelWidth;
		}

		int cardAtPoint(Foundation::Vec2 p) const {
			for (size_t i = 0; i < cardRects.size(); ++i) {
				if (cardRects[i].contains(p)) return static_cast<int>(i);
			}
			return -1;
		}

		void resetRoster() {
			roster.clear();
			for (size_t i = 0; i < world_sim::kPartySize; ++i) {
				const world_sim::CrewDef& def = world_sim::kCrewPool[i];
				roster.push_back({&def, def.name, def.role, def.age, def.mood});
			}
		}

		void restoreRoster(const std::vector<world_sim::PartyMember>& party) {
			roster.clear();
			for (size_t i = 0; i < party.size() && i < world_sim::kPartySize; ++i) {
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
			std::array<size_t, world_sim::kCrewPool.size()> order{};
			for (size_t i = 0; i < order.size(); ++i) order[i] = i;
			std::shuffle(order.begin(), order.end(), rng);

			std::vector<std::string> usedNames;
			roster.clear();
			for (size_t i = 0; i < world_sim::kPartySize; ++i) {
				const world_sim::CrewDef& def = world_sim::kCrewPool[order[i]];
				RosterSlot slot{&def, def.name, def.role, def.age, def.mood};
				if (std::bernoulli_distribution{0.5}(rng)) {
					slot.name = pickUnusedName(usedNames);
					slot.role = world_sim::kRerollRoles[std::uniform_int_distribution<size_t>{
						0, world_sim::kRerollRoles.size() - 1}(rng)];
					slot.age = std::uniform_int_distribution<int>{23, 45}(rng);
					slot.mood = std::uniform_real_distribution<float>{0.35F, 0.9F}(rng);
				}
				usedNames.push_back(slot.name);
				roster.push_back(std::move(slot));
			}
			selectedIndex = 0;
			LOG_INFO(Game, "PartySelectScene - Randomized crew");
		}

		std::string pickUnusedName(const std::vector<std::string>& usedNames) {
			std::uniform_int_distribution<size_t> dist{0, world_sim::kRerollNames.size() - 1};
			std::string candidate = world_sim::kRerollNames[dist(rng)];
			while (std::find(usedNames.begin(), usedNames.end(), candidate) != usedNames.end()) {
				candidate = world_sim::kRerollNames[dist(rng)];
			}
			return candidate;
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

		std::vector<RosterSlot>		  roster;
		std::unique_ptr<UI::Button>	  backButton;
		std::unique_ptr<UI::Button>	  generateButton;
		std::unique_ptr<UI::Button>	  randomizeButton;
		std::vector<Foundation::Rect> cardRects;
		std::mt19937				  rng{std::random_device{}()};
		float						  rosterActionsY = 0.0F;
		float						  detailX = 0.0F;
		float						  detailW = 0.0F;
		float						  detailH = 0.0F;
		int							  selectedIndex = 0;
		int							  hoveredIndex = -1;
		bool						  needsLayout = true;
	};

} // namespace

// Export scene info for registry
namespace world_sim::scenes {
	extern const world_sim::SceneInfo PartySelect = {kSceneName, []() {
		return std::make_unique<PartySelectScene>();
	}};
} // namespace world_sim::scenes
