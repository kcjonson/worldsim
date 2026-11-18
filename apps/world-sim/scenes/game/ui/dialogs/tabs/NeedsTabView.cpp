#include "NeedsTabView.h"
#include "MeterDraw.h"

#include <ecs/components/Needs.h>
#include <ui/Context.h>

#include <algorithm>
#include <string>

namespace world_sim {

	namespace {

		// Meters are kept by NeedType; the needs colonists act on are listed first, the comfort needs after.
		constexpr std::size_t kNeedCount = static_cast<std::size_t>(ecs::NeedType::Count);

		constexpr float kColumnGap = 24.0F;
		constexpr float kRowGap = 14.0F;
		constexpr float kSectionGap = 12.0F;
		constexpr float kComfortOpacity = 0.7F; // colonists don't act on comfort needs yet
		constexpr float kAilmentsHeight = 58.0F;

		bool actedOn(ecs::NeedType need) {
			return std::ranges::find(ecs::NeedsComponent::kActionableNeeds, need) != ecs::NeedsComponent::kActionableNeeds.end();
		}

		ui::Style amountText(const ui::Theme& theme) {
			return world_sim::text(theme, {.family = ui::tokens::font::family::mono, .size = tokens::fontSize::fsSm, .color = tokens::color::text});
		}

		struct BreakdownRowText {
			std::string label;
			std::string left;
			std::string amount;
		};

		BreakdownRowText rowText(const MoodNeedRow& row) {
			return {.label = row.label, .amount = row.amount};
		}

		BreakdownRowText rowText(const MoodThoughtRow& row) {
			return {.label = row.label, .left = row.left, .amount = row.amount};
		}

		// A label, or a label over its time left, with the amount at the right.
		class BreakdownRow : public ui::Component {
		  public:
			struct Args {
				UI_COMMON_ARGS
				BreakdownRowText row;
			};

			explicit BreakdownRow(const Args& args)
				: row(args.row) {}

			void setRow(const BreakdownRowText& newRow) {
				row = newRow;
				show();
			}

			[[nodiscard]] const char* typeName() const override { return "MoodBreakdownRow"; }

		  protected:
			void onMount() override {
				const ui::Theme& theme = getContext()->getTheme();
				ui::Layout		 layout = getLayout();
				layout.display = ui::Display::Flex;
				layout.gap = theme.get(tokens::spacing::space2);
				layout.alignItems = ui::AlignItems::Start;
				setLayout(layout);
				ui::Ref<ui::Component> words = addChild<ui::Component>({
					.size = {0.0F, ui::kAuto},
					.layout = {.display = ui::Display::Flex, .flexDirection = ui::FlexDirection::Column, .flexGrow = 1.0F},
				});
				label = words.addChild<ui::Text>({.style = tabs::labelText(theme), .wrap = true});
				left = words.addChild<ui::Text>({.style = tabs::smallText(theme), .wrap = true});
				amount = addChild<ui::Text>({.layout = {.flexShrink = 0.0F}, .style = amountText(theme)});
				show();
			}

		  private:
			void show() {
				label.get()->setText(row.label);
				left.get()->setVisible(!row.left.empty());
				left.get()->setText(row.left);
				amount.get()->setText(row.amount);
			}

			BreakdownRowText  row;
			ui::Ref<ui::Text> label;
			ui::Ref<ui::Text> left;
			ui::Ref<ui::Text> amount;
		};

		ui::Ref<ui::Component> column(ui::Component& parent, float gap) {
			return parent.addChild<ui::Component>({
				.size = {0.0F, ui::kAuto},
				.layout = {.display = ui::Display::Flex, .flexDirection = ui::FlexDirection::Column, .gap = gap, .flexGrow = 1.0F},
			});
		}

	} // namespace

	void NeedsTabView::onMount() {
		const ui::Theme& theme = getContext()->getTheme();
		ui::Layout		 layout = getLayout();
		layout.display = ui::Display::Flex;
		layout.flexDirection = ui::FlexDirection::Column;
		layout.gap = theme.get(tokens::spacing::space2);
		setLayout(layout);

		moodMeter = addChild<ui::ProgressBar>({.label = "Mood", .controlSize = ui::ControlSize::Sm});
		addChild<ui::Text>({
			.margin = {0.0F, 0.0F, theme.get(tokens::spacing::space1), 0.0F},
			.style = world_sim::text(theme, {.size = tokens::fontSize::fsXs, .color = tokens::color::textFaint}),
			.text = "Mood eases toward a target made of the needs below and the thoughts this colonist holds.",
			.wrap = true,
		});

		ui::Ref<ui::Component> columns =
			addChild<ui::Component>({.layout = {.display = ui::Display::Flex, .gap = kColumnGap, .alignItems = ui::AlignItems::Start}});
		ui::Ref<ui::Component> left = column(*columns.get(), kRowGap);
		const auto addMeter = [&](ecs::NeedType need) {
			needMeters[static_cast<std::size_t>(need)] = left.addChild<ui::ProgressBar>({
				.style = {.opacity = actedOn(need) ? 1.0F : kComfortOpacity},
				.label = ecs::needLabel(need),
				.controlSize = ui::ControlSize::Sm,
			});
		};
		left.addChild<tabs::KickerDivider>({.label = "Vital needs"});
		for (const ecs::NeedType need : ecs::NeedsComponent::kActionableNeeds) {
			addMeter(need);
		}
		left.addChild<tabs::KickerDivider>({.margin = {kSectionGap, 0.0F, 0.0F, 0.0F}, .label = "Comfort"});
		for (std::size_t i = 0; i < kNeedCount; ++i) {
			if (!actedOn(static_cast<ecs::NeedType>(i))) {
				addMeter(static_cast<ecs::NeedType>(i));
			}
		}
		left.addChild<ui::Text>({
			.style = world_sim::text(theme, {.size = tokens::fontSize::fs2xs, .color = tokens::color::textFaint}),
			.text = "Comfort needs are tracked, but colonists don't act on them yet.",
			.wrap = true,
		});

		ui::Ref<ui::Component> right = column(*columns.get(), theme.get(tokens::spacing::space2));
		right.addChild<tabs::KickerDivider>({.label = "Mood breakdown"});
		const ui::Style label = tabs::labelText(theme);
		const auto		sumRow = [&](const char* id, const char* name) {
			 ui::Ref<ui::Component> row = right.addChild<ui::Component>(
				 {.id = id, .layout = {.display = ui::Display::Flex, .gap = theme.get(tokens::spacing::space2)}}
			 );
			 row.addChild<ui::Text>({.layout = {.flexGrow = 1.0F}, .style = label, .text = name});
			 return row.addChild<ui::Text>({.layout = {.flexShrink = 0.0F}, .style = amountText(theme)});
		};
		needRows = right.addChild<ui::Component>({
			.id = "mood_breakdown_needs",
			.layout = {.display = ui::Display::Flex, .flexDirection = ui::FlexDirection::Column, .gap = theme.get(tokens::spacing::space2)},
		});
		thoughtRows = right.addChild<ui::Component>({
			.id = "mood_breakdown_thoughts",
			.layout = {.display = ui::Display::Flex, .flexDirection = ui::FlexDirection::Column, .gap = theme.get(tokens::spacing::space2)},
		});
		noThoughts = right.addChild<ui::Text>({
			.id = "mood_breakdown_no_thoughts",
			.style = tabs::smallText(theme),
			.text = "No thought is moving this colonist's mood.",
			.wrap = true,
		});
		moodTarget = sumRow("mood_breakdown_target", "Target");
		clampedNote = right.addChild<ui::Text>({.id = "mood_breakdown_clamped", .visible = false, .style = tabs::smallText(theme), .wrap = true});
		right.addChild<tabs::KickerDivider>({.margin = {kSectionGap, 0.0F, 0.0F, 0.0F}, .label = "Body & ailments"});
		right.addChild<tabs::EmptyState>({
			.minSize = {ui::kAuto, kAilmentsHeight},
			.title = "No injuries or ailments",
			.subtitle = "Wounds, illness, and treatment arrive with the medical update.",
		});
		update({});
	}

	void NeedsTabView::update(const NeedsData& data) {
		ui::ProgressBar* mood = moodMeter.get();
		if (mood == nullptr) {
			return;
		}
		const ui::Tone moodTone = data.mood < 25.0F ? ui::Tone::Danger : (data.mood < 50.0F ? ui::Tone::Warning : ui::Tone::Positive);
		mood->setValue(data.mood / 100.0F);
		mood->setTone(moodTone);
		// An ASCII separator: the font atlas has no middot.
		mood->setValueText(std::to_string(static_cast<int>(data.mood)) + "% - " + (data.moodLabel.empty() ? "Mood" : data.moodLabel));

		for (std::size_t i = 0; i < kNeedCount; ++i) {
			ui::ProgressBar* meter = needMeters[i].get();
			const ui::Tone tone = data.isCritical[i] ? ui::Tone::Danger : (data.needsAttention[i] ? ui::Tone::Warning : ui::Tone::Positive);
			meter->setValue(data.needValues[i] / 100.0F);
			meter->setTone(tone);
			meter->setValueText(std::to_string(static_cast<int>(data.needValues[i])) + "%");
		}
		if (!breakdownShown || data.moodBreakdown != shownBreakdown) {
			showBreakdown(data.moodBreakdown);
		}
	}

	void NeedsTabView::showBreakdown(const MoodBreakdown& breakdown) {
		breakdownShown = true;
		shownBreakdown = breakdown;
		moodTarget.get()->setText(breakdown.target);
		clampedNote.get()->setVisible(!breakdown.clampedFrom.empty());
		clampedNote.get()->setText("Clamped: the parts add to " + breakdown.clampedFrom);
		noThoughts.get()->setVisible(breakdown.thoughts.empty());
		thoughtRows.get()->setVisible(!breakdown.thoughts.empty());
		needRows.get()->setVisible(!breakdown.needs.empty());
		ui::Component& needs = *needRows.get();
		needs.reconcileChildren(
			breakdown.needs,
			[](const MoodNeedRow& row) { return row.label; },
			[&needs](const MoodNeedRow& row) -> ui::Ref<ui::Component> {
				return needs.addChild<BreakdownRow>({.id = "mood_breakdown_need", .row = rowText(row)});
			},
			[](ui::Component& shown, const MoodNeedRow& row) { static_cast<BreakdownRow&>(shown).setRow(rowText(row)); }
		);
		ui::Component& rows = *thoughtRows.get();
		rows.reconcileChildren(
			breakdown.thoughts,
			[](const MoodThoughtRow& row) { return std::to_string(row.id); },
			[&rows](const MoodThoughtRow& row) -> ui::Ref<ui::Component> {
				return rows.addChild<BreakdownRow>({.id = "mood_breakdown_row", .row = rowText(row)});
			},
			[](ui::Component& shown, const MoodThoughtRow& row) { static_cast<BreakdownRow&>(shown).setRow(rowText(row)); }
		);
	}

} // namespace world_sim
