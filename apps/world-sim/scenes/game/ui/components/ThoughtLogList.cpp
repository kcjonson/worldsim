#include "ThoughtLogList.h"

#include "scenes/game/ui/dialogs/tabs/MeterDraw.h"

#include <ui/Context.h>

#include <string>

namespace world_sim {

	namespace {

		// In the order of ThoughtLogFilter.
		const std::vector<std::string> kFilterLabels{"Notable", "All"};

	} // namespace

	void ThoughtLogBlock::onMount() {
		const ui::Theme& theme = getContext()->getTheme();
		ui::Layout		 column = getLayout();
		column.display = ui::Display::Flex;
		column.flexDirection = ui::FlexDirection::Column;
		column.gap = theme.get(tokens::spacing::space05);
		setLayout(column);
		whenText = addChild<ui::Text>({
			.style = world_sim::text(
				theme, {.family = ui::tokens::font::family::mono, .size = tokens::fontSize::fs2xs, .color = tokens::color::textFaint}
			),
		});
		summaryText = addChild<ui::Text>({.style = tabs::labelText(theme), .wrap = true});
		lineText = addChild<ui::Text>({.style = tabs::mutedText(theme), .wrap = true});
		show();
	}

	void ThoughtLogBlock::setEntry(const ThoughtLogEntry& newEntry) {
		if (newEntry == entry) {
			return;
		}
		entry = newEntry;
		show();
	}

	void ThoughtLogBlock::show() {
		whenText.get()->setText(entry.when);
		summaryText.get()->setText(entry.summary);
		lineText.get()->setVisible(!entry.line.empty());
		lineText.get()->setText(entry.line.empty() ? std::string() : "\"" + entry.line + "\"");
	}

	void ThoughtLogList::onMount() {
		const ui::Theme& theme = getContext()->getTheme();
		ui::Layout		 column = getLayout();
		column.display = ui::Display::Flex;
		column.flexDirection = ui::FlexDirection::Column;
		column.gap = theme.get(tokens::spacing::space3);
		setLayout(column);
		filterChoice = addChild<ui::SegmentedControl>({
			.id = "thought_log_filter",
			.layout = {.flexShrink = 0.0F, .alignSelf = ui::AlignItems::Start},
			.options = kFilterLabels,
			.controlSize = ui::ControlSize::Sm,
			.onChange =
				[this](int index) {
					if (onFilter) {
						onFilter(static_cast<ThoughtLogFilter>(index));
					}
				},
		});
		empty = addChild<tabs::EmptyState>({
			.title = "No thoughts yet",
			.subtitle = "What this colonist notices, and how they take it, is kept here.",
		});
		nothingNotable = addChild<tabs::EmptyState>({
			.title = "Nothing notable yet",
			.subtitle = "All shows every thought, the passing ones too.",
		});
		blocks = addChild<ui::Component>({
			.layout = {.display = ui::Display::Flex, .flexDirection = ui::FlexDirection::Column, .gap = theme.get(tokens::spacing::space3)},
		});
		olderButton = addChild<ui::Button>({
			.id = "thought_log_older",
			.layout = {.flexShrink = 0.0F, .alignSelf = ui::AlignItems::Start},
			.label = "Show older",
			.variant = ui::ButtonVariant::Ghost,
			.controlSize = ui::ControlSize::Sm,
			.onPress =
				[this] {
					if (onShowOlder) {
						onShowOlder();
					}
				},
		});
	}

	void ThoughtLogList::show(const ThoughtLogModel& log) {
		if (shown && log.version() == shownVersion) {
			return;
		}
		shown = true;
		shownVersion = log.version();
		const std::vector<ThoughtLogEntry>& entries = log.entries();
		filterChoice.get()->setSelected(static_cast<int>(log.currentFilter()));
		empty.get()->setVisible(!log.hasThoughts());
		nothingNotable.get()->setVisible(log.hasThoughts() && entries.empty());
		blocks.get()->setVisible(!entries.empty());
		olderButton.get()->setVisible(log.hasOlder());
		ui::Component& column = *blocks.get();
		column.reconcileChildren(
			entries,
			[](const ThoughtLogEntry& entry) { return std::to_string(entry.id); },
			[&column](const ThoughtLogEntry& entry) -> ui::Ref<ui::Component> {
				return column.addChild<ThoughtLogBlock>({.id = "thought_log_block", .entry = entry});
			},
			[](ui::Component& shownBlock, const ThoughtLogEntry& entry) { static_cast<ThoughtLogBlock&>(shownBlock).setEntry(entry); }
		);
	}

} // namespace world_sim
