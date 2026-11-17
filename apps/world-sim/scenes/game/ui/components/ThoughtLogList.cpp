#include "ThoughtLogList.h"

#include "scenes/game/ui/dialogs/tabs/MeterDraw.h"

#include <ui/Context.h>

#include <string>

namespace world_sim {

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
		setLayout(column);
		empty = addChild<tabs::EmptyState>({
			.title = "No thoughts yet",
			.subtitle = "What this colonist notices, and how they take it, is kept here.",
		});
		blocks = addChild<ui::Component>({
			.layout = {.display = ui::Display::Flex, .flexDirection = ui::FlexDirection::Column, .gap = theme.get(tokens::spacing::space3)},
		});
	}

	void ThoughtLogList::show(const std::vector<ThoughtLogEntry>& entries) {
		empty.get()->setVisible(entries.empty());
		blocks.get()->setVisible(!entries.empty());
		ui::Component& column = *blocks.get();
		column.reconcileChildren(
			entries,
			[](const ThoughtLogEntry& entry) { return std::to_string(entry.id); },
			[&column](const ThoughtLogEntry& entry) -> ui::Ref<ui::Component> {
				return column.addChild<ThoughtLogBlock>({.id = "thought_log_block", .entry = entry});
			},
			[](ui::Component& shown, const ThoughtLogEntry& entry) { static_cast<ThoughtLogBlock&>(shown).setEntry(entry); }
		);
	}

} // namespace world_sim
