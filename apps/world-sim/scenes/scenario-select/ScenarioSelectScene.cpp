// Scenario Select Scene - New Game step 1 of 3.
//
// A header, a row of five cards of equal width, and a Back/Confirm footer.
// Each card carries its icon, name, blurb, difficulty pips, party count and
// tag badges, and a check while it is the one selected. Cards are controls:
// a click, or a player's activate on the focused one, selects it.

#include "NewGameSetup.h"
#include "SceneTypes.h"
#include "scenes/scenario-select/Scenarios.h"
#include "scenes/shared/SalalUi.h"
#include "scenes/shared/Starfield.h"
#include <theme/Theme.h>

#include <app/Keys.h>
#include <application/App.h>
#include <primitives/Primitives.h>
#include <ui/components/Badge.h>
#include <ui/components/Button.h>
#include <ui/components/Control.h>
#include <ui/shapes/Circle.h>
#include <ui/shapes/Icon.h>
#include <ui/shapes/Rectangle.h>
#include <ui/shapes/Text.h>
#include <utils/Log.h>

#include <cstring>
#include <format>
#include <functional>
#include <string>
#include <vector>

namespace {

	constexpr const char* kSceneName = "scenario_select";

	namespace tokens = world_sim::tokens;
	namespace family = ui::tokens::font::family;

	ui::ColorToken pipColor(int pip, int difficulty) {
		if (pip > difficulty) {
			return tokens::color::textFaint;
		}
		if (difficulty <= 2) {
			return tokens::color::statusOk;
		}
		return difficulty <= 3 ? tokens::color::statusWarn : tokens::color::statusCrit;
	}

	std::vector<std::string> splitTags(const char* tags) {
		std::vector<std::string> out;
		for (const char* tag = tags;;) {
			const char* sep = std::strstr(tag, " / ");
			if (sep == nullptr) {
				out.emplace_back(tag);
				return out;
			}
			out.emplace_back(tag, sep);
			tag = sep + 3;
		}
	}

	class ScenarioCard : public ui::Control {
	  public:
		struct Args {
			UI_COMMON_ARGS
			const world_sim::ScenarioDef* def = nullptr;
			std::function<void()>		  onPress;
		};

		explicit ScenarioCard(const Args& args)
			: def(args.def),
			  onPress(args.onPress) {}

		void setSelected(bool value) {
			setActive(value);
			restyle();
		}

		[[nodiscard]] const char* typeName() const override { return "ScenarioCard"; }

	  protected:
		void onMount() override {
			Control::onMount();
			const ui::Theme& theme = getContext()->getTheme();
			const float		 pad = theme.get(tokens::spacing::space5);
			// A column inside its padding, keeping what its parent set (its grow).
			ui::Layout layout = getLayout();
			layout.display = ui::Display::Flex;
			layout.flexDirection = ui::FlexDirection::Column;
			layout.gap = theme.get(tokens::spacing::space2);
			layout.padding = pad;
			layout.alignItems = ui::AlignItems::Start;
			setLayout(layout);

			check = addChild<ui::Circle>({
				.visible = false,
				.size = {20.0F, 20.0F},
				.layout =
					{.position = ui::Position::Absolute,
					 .inset = {.top = theme.get(tokens::spacing::space3), .right = theme.get(tokens::spacing::space3)}},
				.style = {.backgroundColor = theme.get(tokens::color::accent)},
			});
			check.addChild<ui::Icon>(
				{.position = {3.5F, 3.5F},
				 .size = {13.0F, 13.0F},
				 .style = {.color = theme.get(tokens::color::accentContrast)},
				 .name = "check"}
			);

			icon = addChild<ui::Icon>({.size = {28.0F, 28.0F}, .margin = {0.0F, 0.0F, 12.0F, 0.0F}, .name = def->icon});
			addChild<ui::Text>({
				.style = world_sim::text(
					theme,
					{.family = family::display,
					 .size = tokens::fontSize::fsMd,
					 .color = tokens::color::textBright,
					 .tracking = tokens::letterSpacing::lsWide,
					 .transform = Foundation::TextTransform::Uppercase}
				),
				.text = def->name,
			});
			addChild<ui::Text>({
				.size = {ui::percent(100.0F), ui::kAuto},
				.style = world_sim::text(theme, {.size = tokens::fontSize::fsSm, .color = tokens::color::textDim}),
				.text = def->blurb,
				.wrap = true,
			});
			addChild<ui::Component>({.size = {ui::percent(100.0F), ui::kAuto}, .layout = {.flexGrow = 1.0F}});

			// Difficulty pips at the left, the party at the right.
			ui::Ref<ui::Component> meta = addChild<ui::Component>({
				.size = {ui::percent(100.0F), ui::kAuto},
				.layout = {
					.display = ui::Display::Flex, .alignItems = ui::AlignItems::Center, .justifyContent = ui::JustifyContent::SpaceBetween
				},
			});
			ui::Ref<ui::Component> pips = meta.addChild<ui::Component>({.layout = {.display = ui::Display::Flex, .gap = 3.0F}});
			for (int pip = 1; pip <= 5; ++pip) {
				pips.addChild<ui::Rectangle>(
					{.size = {14.0F, 4.0F}, .style = {.backgroundColor = theme.get(pipColor(pip, def->difficulty))}}
				);
			}
			meta.addChild<ui::Text>({
				.style = world_sim::text(
					theme,
					{.family = family::mono,
					 .size = tokens::fontSize::fs2xs,
					 .color = tokens::color::textFaint,
					 .tracking = tokens::letterSpacing::lsWide}
				),
				.text = std::format("{} SURVIVOR{}", def->partyCount, def->partyCount == 1 ? "" : "S"),
			});

			ui::Ref<ui::Component> tags =
				addChild<ui::Component>({.layout = {.display = ui::Display::Flex, .gap = theme.get(tokens::spacing::space1)}});
			for (const std::string& tag : splitTags(def->tags)) {
				tags.addChild<ui::Badge>({.label = tag});
			}
			restyle();
		}

		void activate() override {
			if (onPress) {
				onPress();
			}
		}

	  private:
		// The panel and its edge, the accent's while selected; the icon brightens with them.
		void restyle() {
			const ui::Context* context = getContext();
			if (context == nullptr) {
				return;
			}
			const ui::Theme& theme = context->getTheme();
			const bool		 selected = isActive();
			setStyle({
				.backgroundColor = theme.get(selected ? tokens::color::bgActive : tokens::color::bgPanel),
				.borderColor = theme.get(selected ? tokens::color::accent : tokens::color::lineEdge),
				.borderWidth = theme.get(tokens::border::bw),
			});
			if (ui::Component* mark = check.get()) {
				mark->setVisible(selected);
			}
			if (ui::Icon* glyph = icon.get()) {
				glyph->setStyle({.color = theme.get(selected ? tokens::color::accent : tokens::color::textDim)});
			}
		}

		const world_sim::ScenarioDef* def;
		std::function<void()>		  onPress;
		ui::Ref<ui::Circle>			  check;
		ui::Ref<ui::Icon>			  icon;
	};

	class ScenarioSelectScene : public app::Scene {
	  public:
		void onEnter(app::App& app) override {
			owner = &app;
			LOG_INFO(Game, "ScenarioSelectScene - Entering");
			theme::apply(ui());

			// Coming back from a later step restores the previous choice.
			selectedIndex = 0;
			const std::string& storedId = world_sim::NewGameSetup::Get().scenarioId;
			for (size_t i = 0; i < world_sim::kScenarios.size(); ++i) {
				if (storedId == world_sim::kScenarios[i].id) {
					selectedIndex = static_cast<int>(i);
					break;
				}
			}
			build();
		}

		void onExit() override { LOG_INFO(Game, "ScenarioSelectScene - Exiting"); }

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
			world_sim::renderStarfield(theme, static_cast<int>(screenW), static_cast<int>(screenH), 42U, true);
		}

		std::string exportState() const override {
			return std::format(
				R"({{"scene":"scenario_select","selected":"{}"}})", world_sim::kScenarios[static_cast<size_t>(selectedIndex)].id
			);
		}
		const char* name() const override { return kSceneName; }

	  private:
		void build() {
			const ui::Theme& theme = ui().getTheme();
			const float		 space12 = theme.get(tokens::spacing::space12);
			const float		 space16 = theme.get(tokens::spacing::space16);

			ui::Ref<ui::Component> page = root().addChild<ui::Component>({
				.id = "scenario_root",
				.size = {ui::percent(100.0F), ui::percent(100.0F)},
				.layout = {
					.display = ui::Display::Flex,
					.flexDirection = ui::FlexDirection::Column,
					.gap = theme.get(tokens::spacing::space8),
					.padding = {space12, space16, space12, space16}
				},
			});

			ui::Ref<ui::Component> header = page.addChild<ui::Component>({
				.id = "scenario_header",
				.layout = {
					.display = ui::Display::Flex, .flexDirection = ui::FlexDirection::Column, .gap = theme.get(tokens::spacing::space2)
				},
			});
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
				.text = "// NEW GAME    STEP 01 / 03",
			});
			header.addChild<ui::Text>({
				.id = "title",
				.style = world_sim::text(
					theme,
					{.family = family::display,
					 .size = tokens::fontSize::fs3xl,
					 .color = tokens::color::textBright,
					 .tracking = tokens::letterSpacing::lsWide}
				),
				.text = "Select Scenario",
			});
			header.addChild<ui::Text>({
				.id = "subtitle",
				.style = world_sim::text(theme, {.size = tokens::fontSize::fsMd, .color = tokens::color::textDim}),
				.text = "Each scenario reshapes your wreck site, salvage, and the world you'll fight to survive.",
				.wrap = true,
			});

			ui::Ref<ui::Component> row = page.addChild<ui::Component>({
				.id = "scenario_cards",
				.layout = {.display = ui::Display::Flex, .gap = theme.get(tokens::spacing::space4), .flexGrow = 1.0F},
			});
			cards.clear();
			for (size_t i = 0; i < world_sim::kScenarios.size(); ++i) {
				const int index = static_cast<int>(i);
				cards.push_back(row.addChild<ScenarioCard>({
					.id = world_sim::kScenarios[i].id,
					.size = {0.0F, ui::kAuto},
					.layout = {.flexGrow = 1.0F},
					.def = &world_sim::kScenarios[i],
					.onPress = [this, index] { select(index); },
				}));
			}

			ui::Ref<ui::Component> footer = page.addChild<ui::Component>({
				.id = "scenario_footer",
				.layout = {
					.display = ui::Display::Flex, .flexDirection = ui::FlexDirection::Column, .gap = theme.get(tokens::spacing::space4)
				},
			});
			footer.addChild<ui::Rectangle>(
				{.id = "footer_rule", .size = {ui::kAuto, 1.0F}, .style = {.backgroundColor = theme.get(tokens::color::lineHairline)}}
			);
			ui::Ref<ui::Component> footerRow = footer.addChild<ui::Component>({
				.id = "scenario_footer_row",
				.layout = {
					.display = ui::Display::Flex, .alignItems = ui::AlignItems::Center, .justifyContent = ui::JustifyContent::SpaceBetween
				},
			});
			footerRow.addChild<ui::Button>({
				.id = "btn_scenario_back",
				.size = {120.0F, 40.0F},
				.label = "Back",
				.icon = "chevronLeft",
				.onPress = [this] { goBack(); },
			});
			footerRow.addChild<ui::Button>({
				.id = "btn_scenario_confirm",
				.size = {220.0F, 40.0F},
				.label = "Confirm Scenario",
				.icon = "arrowRight",
				.variant = ui::ButtonVariant::Primary,
				.onPress = [this] { confirm(); },
			});

			select(selectedIndex);
		}

		void select(int idx) {
			selectedIndex = idx;
			for (size_t i = 0; i < cards.size(); ++i) {
				if (ScenarioCard* card = cards[i].get()) {
					card->setSelected(static_cast<int>(i) == idx);
				}
			}
		}

		void goBack() { owner->scenes().switchTo(world_sim::scenes::MainMenu.name); }

		void confirm() {
			world_sim::NewGameSetup::Get().scenarioId = world_sim::kScenarios[static_cast<size_t>(selectedIndex)].id;
			owner->scenes().switchTo(world_sim::scenes::PartySelect.name);
		}

		app::App*						   owner = nullptr;
		std::vector<ui::Ref<ScenarioCard>> cards;
		int								   selectedIndex = 0;
	};

} // namespace

// Export scene info for registry
namespace world_sim::scenes {
	extern const world_sim::SceneInfo ScenarioSelect = {kSceneName, []() { return std::make_unique<ScenarioSelectScene>(); }};
} // namespace world_sim::scenes
