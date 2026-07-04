#include "ResourcesPanel.h"

#include <components/list/ListRow.h>
#include <layout/LayoutContainer.h>
#include <primitives/Primitives.h>
#include <theme/Tokens.h>

#include <algorithm>
#include <format>
#include <string>

namespace world_sim {

namespace {
// Chevron icon size and positioning
constexpr float kChevronSize = 12.0F;
constexpr float kChevronRightPadding = 8.0F;
// Right-side reserve so row counts never sit under the scrollbar
constexpr float kScrollbarReserve = 16.0F;
} // namespace

ResourcesPanel::ResourcesPanel(const Args& args)
	: panelWidth(args.width),
	  onToggle(args.onToggle) {

	// Create header button (chevron icon is separate)
	headerButtonHandle = addChild(UI::Button(UI::Button::Args{
		.label = "Storage",
		.position = {0.0F, 0.0F},
		.size = {panelWidth, kCollapsedHeight},
		.type = UI::Button::Type::Secondary,
		.onClick = [this]() { toggle(); },
		.id = "resources_header"
	}));

	// Create chevron icon (down arrow when collapsed, up when expanded). It sits
	// on the header button by design; the higher zIndex marks the overlap as
	// intentional layering for the layout lint.
	auto chevron = UI::Icon(UI::Icon::Args{
		.position = {0.0F, 0.0F},  // Will be updated in updateLayout
		.size = kChevronSize,
		.svgPath = "assets/ui/icons/chevron_down.svg",
		.tint = Foundation::Color::white(),
		.id = "resources_chevron"
	});
	chevron.zIndex = 1;
	chevronHandle = addChild(std::move(chevron));

	// Create content background (only visible when expanded)
	contentBackgroundHandle = addChild(UI::Rectangle(UI::Rectangle::Args{
		.position = {0.0F, kHeaderHeight},
		.size = {panelWidth, kEmptyExpandedHeight - kHeaderHeight},
		.style = {
			.fill = UI::bg_panel,
			.border = Foundation::BorderStyle{
				.color = UI::line_edge,
				.width = UI::bw,
				.cornerRadius = UI::r_md
			}
		},
		.id = "resources_content_bg",
		.visible = false
	}));

	// Create empty message text (shown when no storage containers exist)
	emptyMessageHandle = addChild(UI::Text(UI::Text::Args{
		.position = {UI::space_2, kHeaderHeight + UI::space_2},
		.width = panelWidth - UI::space_2 * 2.0F,
		.text = "No stockpiles built. Create one to track colony resources.",
		.style = {
			.color = UI::text_dim,
			.fontSize = UI::fs_sm,
			.wordWrap = true
		},
		.id = "resources_empty_msg",
		.visible = false
	}));

	// Create scroll container with a vertical layout of resource rows inside
	auto scrollContainer = UI::ScrollContainer(UI::ScrollContainer::Args{
		.position = {UI::space_2, kHeaderHeight + UI::space_2},
		.size = {panelWidth - UI::space_2 * 2.0F, kMaxExpandedHeight - kHeaderHeight - UI::space_2 * 2.0F},
		.id = "resources_scroll"
	});
	scrollContainer.visible = false;

	auto layout = UI::LayoutContainer(UI::LayoutContainer::Args{
		.position = {0.0F, 0.0F},
		.size = {panelWidth - UI::space_2 * 2.0F - kScrollbarReserve, 0.0F},
		.direction = UI::Direction::Vertical,
		.id = "resources_layout"
	});
	layoutHandle = scrollContainer.addChild(std::move(layout));
	scrollContainerHandle = addChild(std::move(scrollContainer));

	// Start collapsed - updateLayout sets visibility
	updateLayout();
}

void ResourcesPanel::setAnchorPosition(float x, float y) {
	// Anchor is top-right, so offset by panel width
	position = {x - panelWidth, y};
	updateLayout();
}

void ResourcesPanel::setResources(const std::vector<adapters::ResourceRowData>& rows, size_t containers) {
	rowCount = rows.size();
	containerCount = containers;

	auto* scroll = getChild<UI::ScrollContainer>(scrollContainerHandle);
	auto* layout = scroll != nullptr ? scroll->getChild<UI::LayoutContainer>(layoutHandle) : nullptr;
	if (layout != nullptr) {
		layout->clearChildren();
		const float rowWidth = panelWidth - UI::space_2 * 2.0F - kScrollbarReserve;
		for (size_t i = 0; i < rows.size(); ++i) {
			layout->addChild(UI::ListRow(UI::ListRow::Args{
				.label = rows[i].displayName,
				.trailing = std::to_string(rows[i].count),
				.size = {rowWidth, kRowHeight},
				.id = std::format("resource_row_{}", i)
			}));
		}
	}
	if (scroll != nullptr) {
		scroll->setContentHeight(static_cast<float>(rows.size()) * kRowHeight);
		scroll->setViewportSize({panelWidth - UI::space_2 * 2.0F, expandedHeight() - kHeaderHeight - UI::space_2 * 2.0F});
	}

	updateLayout();
}

void ResourcesPanel::toggle() {
	expanded = !expanded;
	updateChevron();
	updateLayout();
	if (onToggle) {
		onToggle();
	}
}

float ResourcesPanel::expandedHeight() const {
	if (containerCount == 0) {
		return kEmptyExpandedHeight;
	}
	const float contentHeight = static_cast<float>(std::max<size_t>(rowCount, 1)) * kRowHeight + UI::space_2 * 2.0F;
	return std::min(kHeaderHeight + contentHeight, kMaxExpandedHeight);
}

void ResourcesPanel::updateChevron() {
	auto* chevron = getChild<UI::Icon>(chevronHandle);
	if (chevron) {
		// Up arrow when expanded (click to collapse), down arrow when collapsed (click to expand)
		std::string path = expanded ? "assets/ui/icons/chevron_up.svg" : "assets/ui/icons/chevron_down.svg";
		chevron->setSvgPath(path);
	}
}

void ResourcesPanel::updateLayout() {
	// Position header at top
	auto* header = getChild<UI::Button>(headerButtonHandle);
	if (header) {
		header->setPosition(position.x, position.y);
	}

	// Position chevron on the right side of the header button, vertically centered
	auto* chevron = getChild<UI::Icon>(chevronHandle);
	if (chevron) {
		float chevronX = position.x + panelWidth - kChevronSize - kChevronRightPadding;
		float chevronY = position.y + (kCollapsedHeight - kChevronSize) / 2.0F;
		chevron->setPosition(chevronX, chevronY);
	}

	const bool showEmpty = expanded && containerCount == 0;
	const bool showRows = expanded && containerCount > 0;

	auto* contentBg = getChild<UI::Rectangle>(contentBackgroundHandle);
	if (contentBg) {
		contentBg->visible = expanded;
		contentBg->size = {panelWidth, expandedHeight() - kHeaderHeight};
		contentBg->setPosition(position.x, position.y + kHeaderHeight);
	}

	auto* emptyMsg = getChild<UI::Text>(emptyMessageHandle);
	if (emptyMsg) {
		emptyMsg->visible = showEmpty;
		emptyMsg->setPosition(position.x + UI::space_2, position.y + kHeaderHeight + UI::space_2);
	}

	auto* scroll = getChild<UI::ScrollContainer>(scrollContainerHandle);
	if (scroll) {
		scroll->visible = showRows;
		scroll->setPosition(position.x + UI::space_2, position.y + kHeaderHeight + UI::space_2);
	}

	// Update overall size
	size = {panelWidth, expanded ? expandedHeight() : kCollapsedHeight};
}

Foundation::Rect ResourcesPanel::getBounds() const {
	return {position.x, position.y, panelWidth, expanded ? expandedHeight() : kCollapsedHeight};
}

void ResourcesPanel::update(float deltaTime) {
	auto* scroll = getChild<UI::ScrollContainer>(scrollContainerHandle);
	if (scroll) {
		scroll->update(deltaTime);
	}
}

bool ResourcesPanel::handleEvent(UI::InputEvent& event) {
	// Use Component's dispatchEvent to properly handle children
	return dispatchEvent(event);
}

// render() inherited from Component - automatically renders all children

}  // namespace world_sim
