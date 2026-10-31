// gallery: worldsim's theme on salal's ui, to eyeball against the React prototype's component gallery.
// salal's components in each variant under worldsim's tokens; worldsim's own Panel and Avatar; the
// palette; and every icon in worldsim's set.

#include "SceneTypes.h"

#include <theme/Avatar.h>
#include <theme/Icons.h>
#include <theme/Panel.h>
#include <theme/Theme.h>
#include <theme/Tokens.h>

#include <primitives/Primitives.h>
#include <scene/Scene.h>
#include <ui/components/Badge.h>
#include <ui/components/Button.h>
#include <ui/components/Divider.h>
#include <ui/components/KeyCap.h>
#include <ui/components/ProgressBar.h>
#include <ui/components/SegmentedControl.h>
#include <ui/components/Slider.h>
#include <ui/components/Stat.h>
#include <ui/components/TabBar.h>
#include <ui/components/Tooltip.h>
#include <ui/shapes/Icon.h>
#include <ui/shapes/Rectangle.h>
#include <ui/shapes/Text.h>
#include <ui/theme/Tokens.h>

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>

namespace {

	constexpr const char* kSceneName = "gallery";
	constexpr std::size_t kIconColumns = 18;

	namespace t = theme::tokens::theme;

	ui::Layout flex(ui::FlexDirection direction, float gap) {
		return {.display = ui::Display::Flex, .flexDirection = direction, .gap = gap, .alignItems = ui::AlignItems::Start};
	}

	ui::Ref<ui::Component> section(ui::Ref<ui::Component> parent, const char* label, ui::FlexDirection direction, float gap) {
		ui::Ref<ui::Component> column = parent.addChild<ui::Component>({.layout = flex(ui::FlexDirection::Column, 8.0F)});
		column.addChild<ui::Text>({.style = ui::styles::label, .text = label});
		return column.addChild<ui::Component>({.layout = flex(direction, gap)});
	}

	class GalleryScene : public app::Scene {
	  public:
		const char* name() const override { return kSceneName; }

		void onEnter(app::App& /*app*/) override {
			theme::apply(ui());
			ui::Layout stage = flex(ui::FlexDirection::Column, 20.0F);
			stage.padding = 24.0F;
			root().get()->setLayout(stage);
			root().addChild<ui::Text>({.style = ui::styles::text::display, .text = "Component library"});

			ui::Ref<ui::Component> columns = root().addChild<ui::Component>({.layout = flex(ui::FlexDirection::Row, 40.0F)});
			ui::Ref<ui::Component> left = columns.addChild<ui::Component>({.layout = flex(ui::FlexDirection::Column, 18.0F)});
			buttons(section(left, "BUTTONS", ui::FlexDirection::Row, 12.0F));
			stats(section(left, "STATS", ui::FlexDirection::Row, 32.0F));
			bars(section(left, "BARS", ui::FlexDirection::Column, 12.0F));
			badges(section(left, "BADGES", ui::FlexDirection::Row, 8.0F));
			controls(section(left, "CONTROLS", ui::FlexDirection::Column, 12.0F));

			ui::Ref<ui::Component> middle = columns.addChild<ui::Component>({.layout = flex(ui::FlexDirection::Column, 18.0F)});
			people(section(middle, "PEOPLE", ui::FlexDirection::Row, 10.0F));
			sliders(section(middle, "SLIDERS", ui::FlexDirection::Column, 8.0F));
			tooltip(section(middle, "TOOLTIP", ui::FlexDirection::Row, 0.0F));

			panels(section(columns, "PANELS", ui::FlexDirection::Column, 20.0F));

			palette(section(root(), "PALETTE", ui::FlexDirection::Row, 8.0F));
			icons(section(root(), "ICONS", ui::FlexDirection::Column, 18.0F));
		}

		void render(float /*alpha*/) override { Renderer::Primitives::clear(ui().getTheme().get(t::color::bgVoid)); }

	  private:
		static void buttons(ui::Ref<ui::Component> row) {
			constexpr std::array<std::pair<const char*, ui::ButtonVariant>, 4> kVariants{{
				{"PRIMARY", ui::ButtonVariant::Primary},
				{"SECONDARY", ui::ButtonVariant::Secondary},
				{"GHOST", ui::ButtonVariant::Ghost},
				{"DANGER", ui::ButtonVariant::Danger},
			}};
			for (const auto& [label, variant] : kVariants) {
				row.addChild<ui::Button>({.size = {150.0F, ui::kAuto}, .label = label, .variant = variant});
			}
		}

		static void stats(ui::Ref<ui::Component> row) {
			row.addChild<ui::Stat>({.label = "POPULATION", .value = "12"});
			row.addChild<ui::Stat>({.label = "FOOD", .value = "86", .unit = "%", .tone = ui::Tone::Positive});
			row.addChild<ui::Stat>({.label = "THREAT", .value = "2", .tone = ui::Tone::Danger});
			row.addChild<ui::Stat>({.label = "POWER", .value = "1.8", .unit = "kW", .tone = ui::Tone::Info});
		}

		static void bars(ui::Ref<ui::Component> column) {
			const ui::Size width{360.0F, ui::kAuto};
			column.addChild<ui::ProgressBar>({.size = width, .value = 0.72F, .label = "MOOD", .valueText = "72%"});
			column.addChild<ui::ProgressBar>({.size = width, .value = 0.30F, .label = "HULL", .valueText = "30%"});
			column.addChild<ui::ProgressBar>({.size = width, .value = 0.85F, .tone = ui::Tone::Info, .label = "POWER", .valueText = "85%"});
			column.addChild<ui::ProgressBar>(
				{.size = width,
				 .value = 0.62F,
				 .tone = ui::Tone::Accent,
				 .label = "BUILD FOUNDATION",
				 .valueText = "62%",
				 .inlineLabel = true}
			);
		}

		static void badges(ui::Ref<ui::Component> row) {
			constexpr std::array<std::pair<const char*, ui::Tone>, 6> kTones{{
				{"DEFAULT", ui::Tone::Default},
				{"ACCENT", ui::Tone::Accent},
				{"DATA", ui::Tone::Info},
				{"OK", ui::Tone::Positive},
				{"WARN", ui::Tone::Warning},
				{"CRIT", ui::Tone::Danger},
			}};
			for (const auto& [label, tone] : kTones) {
				row.addChild<ui::Badge>({.label = label, .tone = tone});
			}
			row.addChild<ui::Badge>({.label = "ONLINE", .tone = ui::Tone::Positive, .dot = true});
		}

		static void controls(ui::Ref<ui::Component> column) {
			column.addChild<ui::SegmentedControl>(
				{.size = {380.0F, ui::kAuto}, .options = {"TERRAIN", "BIOMES", "TEMP", "RAIN"}, .tone = ui::Tone::Info}
			);
			column.addChild<ui::TabBar>(
				{.size = {320.0F, ui::kAuto},
				 .tabs =
					 {{.id = "bio", .label = "BIO"},
					  {.id = "needs", .label = "NEEDS"},
					  {.id = "skills", .label = "SKILLS"},
					  {.id = "gear", .label = "GEAR"}},
				 .selectedId = "needs"}
			);
			ui::Ref<ui::Component> keys = column.addChild<ui::Component>({.layout = flex(ui::FlexDirection::Row, 8.0F)});
			for (const char* key : {"B", "M", "ESC", "SPACE"}) {
				keys.addChild<ui::KeyCap>({.label = key});
			}
			column.addChild<ui::Divider>({.size = {380.0F, ui::kAuto}, .label = "FIELD REPORT"});
		}

		static void people(ui::Ref<ui::Component> row) {
			row.addChild<theme::Avatar>({.seed = "Mara Vance", .mood = 0.72F});
			row.addChild<theme::Avatar>({.seed = "Idris Okonkwo", .mood = 0.45F});
			row.addChild<theme::Avatar>({.seed = "Rin Calloway", .mood = 0.2F});
			row.addChild<theme::Avatar>({.seed = "Vale", .mood = 0.9F, .selected = true});
		}

		static void sliders(ui::Ref<ui::Component> column) {
			column.addChild<ui::Slider>(
				{.size = {220.0F, ui::kAuto}, .value = 0.62F, .label = "WATER", .showValue = true, .format = [](float) {
					 return std::string("62%");
				 }}
			);
			column.addChild<ui::Slider>(
				{.size = {220.0F, ui::kAuto}, .value = 0.31F, .label = "ATMOSPHERE", .showValue = true, .format = [](float) {
					 return std::string("0.7 atm");
				 }}
			);
		}

		static void tooltip(ui::Ref<ui::Component> row) {
			ui::Ref<ui::Button> dossier = row.addChild<ui::Button>({.label = "MARA VANCE", .variant = ui::ButtonVariant::Ghost});
			dossier.get()->setTooltip(ui::tooltip(ui::TooltipText{.title = "Double-click for full dossier"}));
		}

		static void panels(ui::Ref<ui::Component> column) {
			const ui::Size		   wide{300.0F, 180.0F};
			const ui::Size		   low{300.0F, 150.0F};
			ui::Ref<ui::Component> top = column.addChild<ui::Component>({.layout = flex(ui::FlexDirection::Row, 20.0F)});
			top.addChild<theme::Panel>({.size = wide, .title = "REGION", .kicker = "SURVEY"});
			top.addChild<theme::Panel>(
				{.size = wide,
				 .title = "TELEMETRY",
				 .kicker = "FEED",
				 .variant = theme::PanelVariant::Raised,
				 .accent = theme::PanelAccent::Data}
			);
			ui::Ref<ui::Component> bottom = column.addChild<ui::Component>({.layout = flex(ui::FlexDirection::Row, 20.0F)});
			bottom.addChild<theme::Panel>(
				{.size = low,
				 .title = "READOUT",
				 .variant = theme::PanelVariant::Inset,
				 .accent = theme::PanelAccent::None,
				 .corners = false}
			);
			bottom.addChild<theme::Panel>(
				{.size = low, .title = "HUD", .kicker = "COMPACT", .variant = theme::PanelVariant::Raised, .compact = true}
			);
		}

		void palette(ui::Ref<ui::Component> row) {
			const ui::Theme&					 tokens = ui().getTheme();
			const std::array<ui::ColorToken, 11> kSwatches{
				t::color::accent,
				t::color::accentBright,
				t::color::data,
				t::color::dataBright,
				t::color::statusOk,
				t::color::statusWarn,
				t::color::statusCrit,
				t::color::textBright,
				t::color::bgPanel,
				t::color::bgPanelRaised,
				t::color::bgInset,
			};
			for (const ui::ColorToken swatch : kSwatches) {
				row.addChild<ui::Rectangle>(
					{.size = {36.0F, 36.0F},
					 .style = {
						 .backgroundColor = tokens.get(swatch),
						 .borderColor = tokens.get(t::color::lineEdge),
						 .borderWidth = tokens.get(t::border::bw)
					 }}
				);
			}
		}

		static void icons(ui::Ref<ui::Component> grid) {
			const auto&			   names = theme::iconAtlas().names;
			ui::Ref<ui::Component> row;
			for (std::size_t i = 0; i < names.size(); ++i) {
				if (i % kIconColumns == 0) {
					row = grid.addChild<ui::Component>({.layout = flex(ui::FlexDirection::Row, 18.0F)});
				}
				row.addChild<ui::Icon>({.size = {24.0F, 24.0F}, .name = std::string(names[i].name)});
			}
		}
	};

} // namespace

namespace ui_sandbox::scenes {
	extern const ui_sandbox::SceneInfo Gallery = {kSceneName, []() { return std::make_unique<GalleryScene>(); }};
} // namespace ui_sandbox::scenes
