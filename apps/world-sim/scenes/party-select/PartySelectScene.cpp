// Party Select Scene - New Game step 2 of 3.
//
// A 280px roster column of crew cards beside a raised, accented dossier panel
// for the selected member, under a header and over a footer. The roster
// scrolls when the party outgrows the column; the dossier scrolls its own
// content. The assembled party is stored in NewGameSetup and spawns at landing.

#include "NewGameSetup.h"
#include "SceneTypes.h"
#include "scenes/party-select/MockCrew.h"
#include "scenes/scenario-select/Scenarios.h"
#include "scenes/shared/SalalUi.h"
#include "scenes/shared/Starfield.h"

#include <app/Keys.h>
#include <application/App.h>
#include <primitives/Primitives.h>
#include <theme/Avatar.h>
#include <theme/Panel.h>
#include <ui/components/Badge.h>
#include <ui/components/Button.h>
#include <ui/components/Control.h>
#include <ui/components/Divider.h>
#include <ui/components/ProgressBar.h>
#include <ui/components/Stat.h>
#include <ui/shapes/Rectangle.h>
#include <ui/shapes/Text.h>
#include <utils/Log.h>

#include <algorithm>
#include <array>
#include <format>
#include <functional>
#include <random>
#include <string>
#include <vector>

namespace {

	constexpr const char* kSceneName = "party_select";
	constexpr float		  kRosterWidth = 280.0F;
	constexpr float		  kCardHeight = 64.0F;

	namespace tokens = world_sim::tokens;
	namespace family = ui::tokens::font::family;

	size_t partySizeForScenario(const std::string& scenarioId) {
		for (const world_sim::ScenarioDef& s : world_sim::kScenarios) {
			if (scenarioId == s.id) {
				return static_cast<size_t>(s.partyCount);
			}
		}
		return 3; // direct scene jumps arrive with no scenario chosen
	}

	std::string survivorLine(size_t n) {
		constexpr std::array<const char*, 8> kWords{"One", "Two", "Three", "Four", "Five", "Six", "Seven", "Eight"};
		const std::string					 count = (n >= 1 && n <= kWords.size()) ? kWords[n - 1] : std::to_string(n);
		return std::format("{} survivor{} walked away from the wreck. Learn who they are.", count, n == 1 ? "" : "s");
	}

	const char* moodLabel(float mood) {
		if (mood < 0.3F) {
			return "Distressed";
		}
		if (mood < 0.55F) {
			return "Uneasy";
		}
		return mood < 0.75F ? "Stable" : "Content";
	}

	ui::Tone moodTone(float mood) {
		if (mood < 0.3F) {
			return ui::Tone::Danger;
		}
		return mood < 0.55F ? ui::Tone::Warning : ui::Tone::Positive;
	}

	ui::Tone traitTone(world_sim::TraitTone tone) {
		switch (tone) {
			case world_sim::TraitTone::Good:
				return ui::Tone::Positive;
			case world_sim::TraitTone::Bad:
				return ui::Tone::Danger;
			case world_sim::TraitTone::Neutral:
				break;
		}
		return ui::Tone::Default;
	}

	struct RosterSlot {
		const world_sim::CrewDef* def; // template: origin/backstory/skills/traits
		std::string				  name;
		std::string				  role;
		int						  age;
		float					  mood;
	};

	// Mono, small and tracked: the roles, the mood labels and the counts.
	ui::Style label(const ui::Theme& theme, ui::ColorToken color, ui::DimensionToken size = tokens::fontSize::fs2xs) {
		return world_sim::text(
			theme,
			{.family = family::mono,
			 .size = size,
			 .color = color,
			 .tracking = tokens::letterSpacing::lsWide,
			 .transform = Foundation::TextTransform::Uppercase}
		);
	}

	// A crew member in the roster: the portrait, the name over the role, and the mood's meter over its
	// label. A control, so a click or a player's activate selects it.
	class RosterCard : public ui::Control {
	  public:
		struct Args {
			UI_COMMON_ARGS
			std::function<void()> onPress;
		};

		explicit RosterCard(const Args& args)
			: onPress(args.onPress) {}

		void setSlot(const RosterSlot& value) {
			slot = value;
			rebuild();
		}

		void setSelected(bool value) {
			setActive(value);
			rebuild();
		}

		[[nodiscard]] const char* typeName() const override { return "RosterCard"; }

	  protected:
		void onMount() override {
			Control::onMount();
			const ui::Theme& theme = getContext()->getTheme();
			ui::Layout		 layout = getLayout();
			layout.display = ui::Display::Flex;
			layout.gap = theme.get(tokens::spacing::space3);
			layout.padding = {0.0F, theme.get(tokens::spacing::space3)};
			layout.alignItems = ui::AlignItems::Center;
			setLayout(layout);
			rebuild();
		}

		void activate() override {
			if (onPress) {
				onPress();
			}
		}

	  private:
		void rebuild() {
			const ui::Context* context = getContext();
			if (context == nullptr || slot.def == nullptr) {
				return;
			}
			for (const ui::ComponentId child : std::vector(getChildren())) {
				removeChild(context->find(child)->self());
			}
			const ui::Theme& theme = context->getTheme();
			const bool		 selected = isActive();
			setStyle({
				.backgroundColor = theme.get(selected ? tokens::color::bgActive : tokens::color::bgPanel),
				.borderColor = theme.get(selected ? tokens::color::accent : tokens::color::lineHairline),
				.borderWidth = theme.get(tokens::border::bw),
			});
			addChild<theme::Avatar>({.size = {40.0F, 40.0F}, .seed = slot.name, .mood = slot.mood, .selected = selected});
			ui::Ref<ui::Component> names = addChild<ui::Component>({
				.layout = {.display = ui::Display::Flex, .flexDirection = ui::FlexDirection::Column, .gap = 4.0F, .flexGrow = 1.0F},
			});
			names.addChild<ui::Text>({
				.style =
					world_sim::text(theme, {.family = family::display, .size = tokens::fontSize::fsMd, .color = tokens::color::textBright}),
				.text = slot.name,
			});
			names.addChild<ui::Text>({.style = label(theme, tokens::color::textFaint), .text = slot.role});
			ui::Ref<ui::Component> mood = addChild<ui::Component>({
				.size = {56.0F, ui::kAuto},
				.layout = {
					.display = ui::Display::Flex, .flexDirection = ui::FlexDirection::Column, .gap = 4.0F, .alignItems = ui::AlignItems::End
				},
			});
			mood.addChild<ui::ProgressBar>(
				{.size = {ui::percent(100.0F), ui::kAuto}, .value = slot.mood, .controlSize = ui::ControlSize::Sm}
			);
			mood.addChild<ui::Text>({.style = label(theme, tokens::color::textFaint), .text = moodLabel(slot.mood)});
		}

		std::function<void()> onPress;
		RosterSlot			  slot{nullptr, {}, {}, 0, 1.0F};
	};

	class PartySelectScene : public app::Scene {
	  public:
		void onEnter(app::App& app) override {
			owner = &app;
			LOG_INFO(Game, "PartySelectScene - Entering");
			world_sim::enterSalalUi(app, ui());

			const auto& setup = world_sim::NewGameSetup::Get();
			partySize = partySizeForScenario(setup.scenarioId);
			// Re-seed whenever the stored party doesn't match the scenario's size (first visit, or Back
			// and a different scenario pick).
			if (setup.party.size() == partySize) {
				restoreRoster(setup.party);
			} else {
				resetRoster();
			}
			selectedIndex = 0;
			build();
		}

		void onExit() override {
			LOG_INFO(Game, "PartySelectScene - Exiting");
			// Persist the crew so Back from the world creator (and the final land) sees the same roster.
			world_sim::NewGameSetup::Get().party = buildParty();
		}

		void update(const app::FrameContext& frame) override {
			if (engine::key(frame.input, foundation::Key::Escape).pressed()) {
				goBack();
			}
		}

		void render(float /*alpha*/) override {
			const ui::Theme& theme = ui().getTheme();
			Renderer::Primitives::clear(theme.get(tokens::color::bgVoid));
			const float screenW = Renderer::Primitives::PercentWidth(100.0F);
			const float screenH = Renderer::Primitives::PercentHeight(100.0F);
			world_sim::renderStarfield(theme, static_cast<int>(screenW), static_cast<int>(screenH), 57U, true);
		}

		std::string exportState() const override {
			std::string names;
			for (size_t i = 0; i < roster.size(); ++i) {
				names += std::format("{}\"{}\"", i > 0 ? "," : "", roster[i].name);
			}
			return std::format(
				R"({{"scene":"party_select","selected":"{}","roster":[{}]}})", roster[static_cast<size_t>(selectedIndex)].name, names
			);
		}
		const char* name() const override { return kSceneName; }

	  private:
		void build() {
			const ui::Theme& theme = ui().getTheme();
			const float		 space2 = theme.get(tokens::spacing::space2);
			const float		 space3 = theme.get(tokens::spacing::space3);
			const float		 space5 = theme.get(tokens::spacing::space5);
			const ui::Style	 rule{.backgroundColor = theme.get(tokens::color::lineHairline)};
			const auto		 column = [](float gap) {
				  return ui::Layout{.display = ui::Display::Flex, .flexDirection = ui::FlexDirection::Column, .gap = gap};
			};

			ui::Ref<ui::Component> page = root().addChild<ui::Component>({
				.id = "party_root",
				.size = {ui::percent(100.0F), ui::percent(100.0F)},
				.layout = {
					.display = ui::Display::Flex,
					.flexDirection = ui::FlexDirection::Column,
					.gap = space5,
					.padding = {theme.get(tokens::spacing::space8), theme.get(tokens::spacing::space10)}
				},
			});

			ui::Ref<ui::Component> header = page.addChild<ui::Component>({.id = "party_header", .layout = column(space2)});
			header.addChild<ui::Text>({
				.id = "kicker",
				.style = world_sim::text(
					theme,
					{.family = family::mono,
					 .size = tokens::fontSize::fs2xs,
					 .color = tokens::color::accent,
					 .tracking = tokens::letterSpacing::lsWider,
					 .transform = Foundation::TextTransform::Uppercase}
				),
				.text = "// NEW GAME    STEP 02 / 03",
			});
			header.addChild<ui::Text>({
				.id = "title",
				.style = world_sim::text(
					theme, {.family = family::display, .size = tokens::fontSize::fs3xl, .color = tokens::color::textBright}
				),
				.text = "Assemble the Crew",
			});
			header.addChild<ui::Text>({
				.id = "subtitle",
				.style = world_sim::text(theme, {.size = tokens::fontSize::fsBase, .color = tokens::color::textDim}),
				.text = survivorLine(partySize),
				.wrap = true,
			});

			// The two panes: the roster column at its width, the dossier taking the rest.
			ui::Ref<ui::Component> main = page.addChild<ui::Component>({
				.id = "party_main",
				.layout = {.display = ui::Display::Flex, .gap = space5, .flexGrow = 1.0F},
			});
			ui::Ref<ui::Component> rosterColumn = main.addChild<ui::Component>({
				.id = "party_roster",
				.size = {kRosterWidth, ui::kAuto},
				.layout = column(space3),
			});
			ui::Ref<ui::Component> rosterList = rosterColumn.addChild<ui::Component>({
				.id = "roster_list",
				.layout = {.display = ui::Display::Flex, .flexDirection = ui::FlexDirection::Column, .gap = space2, .flexGrow = 1.0F},
				.overflow = ui::Overflow::Scroll,
				.focusGroup = ui::FocusGroup{.orientation = ui::FocusGroup::Orientation::Vertical},
			});
			cards.clear();
			for (size_t i = 0; i < roster.size(); ++i) {
				const int			index = static_cast<int>(i);
				ui::Ref<RosterCard> card = rosterList.addChild<RosterCard>({
					.size = {ui::kAuto, kCardHeight},
					.onPress = [this, index] { select(index); },
				});
				if (RosterCard* made = card.get()) {
					made->setSlot(roster[i]);
				}
				cards.push_back(card);
			}

			ui::Ref<ui::Component> actions = rosterColumn.addChild<ui::Component>({.id = "roster_actions", .layout = column(space2)});
			actions.addChild<ui::Rectangle>({.size = {ui::kAuto, 1.0F}, .style = rule});
			ui::Ref<ui::Component> actionsRow = actions.addChild<ui::Component>({
				.id = "roster_actions_row",
				.layout = {
					.display = ui::Display::Flex, .alignItems = ui::AlignItems::Center, .justifyContent = ui::JustifyContent::SpaceBetween
				},
			});
			actionsRow.addChild<ui::Button>({
				.id = "btn_party_randomize",
				.size = {130.0F, 36.0F},
				.label = "Randomize",
				.icon = "dice",
				.onPress = [this] { randomize(); },
			});
			actionsRow.addChild<ui::Text>({
				.id = "slots_label",
				.style = label(theme, tokens::color::textFaint),
				.text = std::format("{} / {} SLOTS", roster.size(), partySize),
			});

			ui::Ref<theme::Panel> dossierPanel = main.addChild<theme::Panel>({
				.id = "party_dossier",
				.layout = {.flexGrow = 1.0F},
				.variant = ::theme::PanelVariant::Raised,
				.accent = ::theme::PanelAccent::Accent,
			});
			if (::theme::Panel* panel = dossierPanel.get()) {
				dossier = panel->body().addChild<ui::Component>({
					.id = "dossier_content",
					.layout =
						{.display = ui::Display::Flex,
						 .flexDirection = ui::FlexDirection::Column,
						 .gap = theme.get(tokens::spacing::space4),
						 .flexGrow = 1.0F},
					.overflow = ui::Overflow::Scroll,
				});
			}

			ui::Ref<ui::Component> footer = page.addChild<ui::Component>({.id = "party_footer", .layout = column(space3)});
			footer.addChild<ui::Rectangle>({.id = "footer_rule", .size = {ui::kAuto, 1.0F}, .style = rule});
			ui::Ref<ui::Component> footerRow = footer.addChild<ui::Component>({
				.id = "party_footer_row",
				.layout = {
					.display = ui::Display::Flex, .alignItems = ui::AlignItems::Center, .justifyContent = ui::JustifyContent::SpaceBetween
				},
			});
			footerRow.addChild<ui::Button>({
				.id = "btn_party_back",
				.size = {120.0F, 40.0F},
				.label = "Back",
				.icon = "chevronLeft",
				.onPress = [this] { goBack(); },
			});
			footerRow.addChild<ui::Button>({
				.id = "btn_party_generate",
				.size = {220.0F, 40.0F},
				.label = "Generate Planet",
				.icon = "arrowRight",
				.variant = ui::ButtonVariant::Primary,
				.onPress = [this] { confirm(); },
			});

			select(selectedIndex);
			if (RosterCard* first = cards.empty() ? nullptr : cards.front().get()) {
				first->focus();
			}
		}

		// The dossier for `slot`: the hero line, the stats, the backstory, the skills and the traits.
		void showDossier(const RosterSlot& slot) {
			ui::Component* content = dossier.get();
			if (content == nullptr) {
				return;
			}
			for (const ui::ComponentId child : std::vector(content->getChildren())) {
				content->removeChild(ui().find(child)->self());
			}
			content->scrollTo({0.0F, 0.0F});
			const ui::Theme& theme = ui().getTheme();

			ui::Ref<ui::Component> hero = dossier.addChild<ui::Component>({
				.layout = {.display = ui::Display::Flex, .gap = 16.0F, .alignItems = ui::AlignItems::Center},
			});
			hero.addChild<theme::Avatar>({.size = {72.0F, 72.0F}, .seed = slot.name, .mood = slot.mood});
			ui::Ref<ui::Component> heroNames = hero.addChild<ui::Component>({
				.layout = {.display = ui::Display::Flex, .flexDirection = ui::FlexDirection::Column, .gap = 8.0F},
			});
			heroNames.addChild<ui::Text>({
				.style = world_sim::text(
					theme, {.family = family::display, .size = tokens::fontSize::fs2xl, .color = tokens::color::textBright}
				),
				.text = slot.name,
			});
			heroNames.addChild<ui::Text>({.style = label(theme, tokens::color::accent, tokens::fontSize::fsXs), .text = slot.role});
			dossier.addChild<ui::Rectangle>(
				{.size = {ui::kAuto, 1.0F}, .style = {.backgroundColor = theme.get(tokens::color::lineHairline)}}
			);

			ui::Ref<ui::Component> stats = dossier.addChild<ui::Component>({.layout = {.display = ui::Display::Flex, .gap = 32.0F}});
			stats.addChild<ui::Stat>({.label = "Origin", .value = slot.def->origin, .controlSize = ui::ControlSize::Sm});
			stats.addChild<ui::Stat>(
				{.label = "Age", .value = std::to_string(slot.age), .unit = " yrs", .controlSize = ui::ControlSize::Sm}
			);
			stats.addChild<ui::Stat>(
				{.label = "Mood", .value = moodLabel(slot.mood), .tone = moodTone(slot.mood), .controlSize = ui::ControlSize::Sm}
			);

			dossier.addChild<ui::Divider>({.label = "Background"});
			dossier.addChild<ui::Text>({
				.id = "dossier_backstory",
				.size = {ui::percent(100.0F), ui::kAuto},
				.style = world_sim::text(theme, {.size = tokens::fontSize::fsBase, .color = tokens::color::text}),
				.text = slot.def->backstory,
				.wrap = true,
			});

			dossier.addChild<ui::Divider>({.label = "Skills"});
			for (const world_sim::CrewSkillDef& skill : slot.def->skills) {
				ui::Ref<ui::Component> row = dossier.addChild<ui::Component>(
					{.layout = {
						 .display = ui::Display::Flex, .gap = theme.get(tokens::spacing::space2), .alignItems = ui::AlignItems::Center
					 }}
				);
				row.addChild<ui::Text>(
					{.size = {140.0F, ui::kAuto}, .style = label(theme, tokens::color::textDim, tokens::fontSize::fsXs), .text = skill.name}
				);
				row.addChild<ui::ProgressBar>({
					.size = {0.0F, ui::kAuto},
					.layout = {.flexGrow = 1.0F},
					.value = skill.level / 20.0F,
					.tone = ui::Tone::Accent,
					.controlSize = ui::ControlSize::Sm,
				});
				ui::Style value = world_sim::text(
					theme,
					{.family = family::mono,
					 .size = tokens::fontSize::fsXs,
					 .color = tokens::color::accentBright,
					 .align = Foundation::HorizontalAlign::Right}
				);
				row.addChild<ui::Text>({.size = {32.0F, ui::kAuto}, .style = value, .text = std::format("{:.0f}", skill.level)});
			}

			dossier.addChild<ui::Divider>({.label = "Traits"});
			ui::Ref<ui::Component> traits =
				dossier.addChild<ui::Component>({.layout = {.display = ui::Display::Flex, .gap = theme.get(tokens::spacing::space2)}});
			for (const world_sim::CrewTraitDef& trait : slot.def->traits) {
				if (trait.name != nullptr) {
					traits.addChild<ui::Badge>({.label = trait.name, .tone = traitTone(trait.tone)});
				}
			}
		}

		void select(int idx) {
			selectedIndex = idx;
			for (size_t i = 0; i < cards.size(); ++i) {
				if (RosterCard* card = cards[i].get()) {
					card->setSelected(static_cast<int>(i) == idx);
				}
			}
			showDossier(roster[static_cast<size_t>(idx)]);
		}

		void refreshCards() {
			for (size_t i = 0; i < cards.size() && i < roster.size(); ++i) {
				if (RosterCard* card = cards[i].get()) {
					card->setSlot(roster[i]);
				}
			}
		}

		void resetRoster() {
			roster.clear();
			for (size_t i = 0; i < partySize; ++i) {
				const world_sim::CrewDef& def = world_sim::kCrewPool[i % world_sim::kCrewPool.size()];
				RosterSlot				  slot{&def, def.name, def.role, def.age, def.mood};
				// Slots past the pool reuse a template but get a distinct identity from the reroll pools.
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
				// Origin is unique per template, so it recovers the CrewDef the member was rolled from.
				const world_sim::CrewDef* def = &world_sim::kCrewPool[i % world_sim::kCrewPool.size()];
				for (const world_sim::CrewDef& candidate : world_sim::kCrewPool) {
					if (member.origin == candidate.origin) {
						def = &candidate;
						break;
					}
				}
				roster.push_back({def, member.name, member.role, member.age, member.mood});
			}
			if (roster.empty()) {
				resetRoster();
			}
		}

		void randomize() {
			std::array<size_t, world_sim::kCrewPool.size()> defOrder{};
			for (size_t i = 0; i < defOrder.size(); ++i) {
				defOrder[i] = i;
			}
			std::shuffle(defOrder.begin(), defOrder.end(), rng);
			// Drawing reroll names from a shuffled order keeps them unique without rejection sampling.
			std::array<size_t, world_sim::kRerollNames.size()> nameOrder{};
			for (size_t i = 0; i < nameOrder.size(); ++i) {
				nameOrder[i] = i;
			}
			std::shuffle(nameOrder.begin(), nameOrder.end(), rng);
			size_t nextName = 0;

			roster.clear();
			for (size_t i = 0; i < partySize; ++i) {
				const world_sim::CrewDef& def = world_sim::kCrewPool[defOrder[i % defOrder.size()]];
				RosterSlot				  slot{&def, def.name, def.role, def.age, def.mood};
				// Slots past the pool repeat a template, so they must reroll to stay distinct; pool slots
				// reroll half the time for variety.
				if (i >= defOrder.size() || std::bernoulli_distribution{0.5}(rng)) {
					slot.name = world_sim::kRerollNames[nameOrder[nextName++]];
					slot.role = world_sim::kRerollRoles[std::uniform_int_distribution<size_t>{0, world_sim::kRerollRoles.size() - 1}(rng)];
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
					if (trait.name != nullptr) {
						member.traits.emplace_back(trait.name);
					}
				}
				member.backstory = slot.def->backstory;
				party.push_back(std::move(member));
			}
			return party;
		}

		void goBack() { owner->scenes().switchTo(world_sim::scenes::ScenarioSelect.name); }

		void confirm() { owner->scenes().switchTo(world_sim::scenes::WorldCreator.name); }

		app::App*						 owner = nullptr;
		std::vector<RosterSlot>			 roster;
		std::vector<ui::Ref<RosterCard>> cards;
		ui::Ref<ui::Component>			 dossier;
		std::mt19937					 rng{std::random_device{}()};
		size_t							 partySize = 3;
		int								 selectedIndex = 0;
	};

} // namespace

// Export scene info for registry
namespace world_sim::scenes {
	extern const world_sim::SceneInfo PartySelect = {kSceneName, []() { return std::make_unique<PartySelectScene>(); }};
} // namespace world_sim::scenes
