#include "NeedsTabView.h"
#include "MeterDraw.h"

#include <ecs/components/Needs.h>
#include <ui/Context.h>

#include <string>

namespace world_sim {

	namespace {

		// By NeedType: 0 Hunger, 1 Thirst, 2 Energy, 3 Bladder, 4 Digestion are vital; 5 Hygiene,
		// 6 Recreation, 7 Temperature are comfort.
		constexpr std::size_t kVitalCount = 5;
		constexpr std::size_t kNeedCount = 8;

		constexpr float kColumnGap = 24.0F;
		constexpr float kRowGap = 14.0F;
		constexpr float kSectionGap = 12.0F;
		constexpr float kComfortOpacity = 0.7F; // colonists don't act on comfort needs yet
		constexpr float kAilmentsHeight = 58.0F;

		const char* needLabel(std::size_t index) {
			return index < ecs::kNeedLabels.size() ? ecs::kNeedLabels[index] : "Need";
		}

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
			.text = "Mood is computed from the needs below; it sinks as they go unmet.",
			.wrap = true,
		});

		ui::Ref<ui::Component> columns =
			addChild<ui::Component>({.layout = {.display = ui::Display::Flex, .gap = kColumnGap, .alignItems = ui::AlignItems::Start}});
		ui::Ref<ui::Component> left = column(*columns.get(), kRowGap);
		left.addChild<tabs::KickerDivider>({.label = "Vital needs"});
		for (std::size_t i = 0; i < kNeedCount; ++i) {
			if (i == kVitalCount) {
				left.addChild<tabs::KickerDivider>({.margin = {kSectionGap, 0.0F, 0.0F, 0.0F}, .label = "Comfort"});
			}
			needMeters[i] = left.addChild<ui::ProgressBar>({
				.style = {.opacity = i < kVitalCount ? 1.0F : kComfortOpacity},
				.label = needLabel(i),
				.controlSize = ui::ControlSize::Sm,
			});
		}
		left.addChild<ui::Text>({
			.style = world_sim::text(theme, {.size = tokens::fontSize::fs2xs, .color = tokens::color::textFaint}),
			.text = "Comfort needs are tracked, but colonists don't act on them yet.",
			.wrap = true,
		});

		ui::Ref<ui::Component> right = column(*columns.get(), theme.get(tokens::spacing::space2));
		right.addChild<tabs::KickerDivider>({.label = "Body & ailments"});
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
	}

} // namespace world_sim
