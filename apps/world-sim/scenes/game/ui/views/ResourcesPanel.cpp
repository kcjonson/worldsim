#include "ResourcesPanel.h"

#include <primitives/Primitives.h>
#include <theme/Theme.h>

#include <string>

namespace world_sim {

namespace {
// Chevron icon size and positioning
constexpr float kChevronSize = 12.0F;
constexpr float kChevronRightPadding = 8.0F;
} // namespace

ResourcesPanel::ResourcesPanel(const Args& args)
	: panelWidth(args.width) {

	// Create header button (chevron icon is separate)
	headerButtonHandle = addChild(UI::Button(UI::Button::Args{
		.label = "Storage",
		.position = {0.0F, 0.0F},
		.size = {panelWidth, kCollapsedHeight},
		.type = UI::Button::Type::Secondary,
		.onClick = [this]() { toggle(); },
		.id = "resources_header"
	}));

	// Create chevron icon (down arrow when collapsed, up when expanded)
	chevronHandle = addChild(UI::Icon(UI::Icon::Args{
		.position = {0.0F, 0.0F},  // Will be updated in updateLayout
		.size = kChevronSize,
		.svgPath = "assets/ui/icons/chevron_down.svg",
		.tint = Foundation::Color::white(),
		.id = "resources_chevron"
	}));

	// Create content background (only visible when expanded)
	contentBackgroundHandle = addChild(UI::Rectangle(UI::Rectangle::Args{
		.position = {0.0F, kHeaderHeight},
		.size = {panelWidth, kExpandedHeight - kHeaderHeight},
		.style = {
			.fill = UI::Theme::Colors::panelBackground,
			.border = Foundation::BorderStyle{
				.color = UI::Theme::Colors::panelBorder,
				.width = 1.0F
			}
		},
		.id = "resources_content_bg",
		.visible = false
	}));

	// Create empty message text
	emptyMessageHandle = addChild(UI::Text(UI::Text::Args{
		.position = {kPadding, kHeaderHeight + kPadding},
		.text = "No stockpiles built.\nCreate one to track\ncolony resources.",
		.style = {
			.color = UI::Theme::Colors::textMuted,
			.fontSize = 12.0F
		},
		.id = "resources_empty_msg",
		.visible = false
	}));

	// Start collapsed - updateLayout sets visibility
	updateLayout();
}

void ResourcesPanel::setAnchorPosition(float x, float y) {
	// Anchor is top-right, so offset by panel width
	anchorPosition = {x, y};
	position = {x - panelWidth, y};
	updateLayout();
}

void ResourcesPanel::toggle() {
	expanded = !expanded;
	updateChevron();
	updateLayout();
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

	// Show/hide expanded content
	auto* contentBg = getChild<UI::Rectangle>(contentBackgroundHandle);
	auto* emptyMsg = getChild<UI::Text>(emptyMessageHandle);

	if (contentBg) {
		contentBg->visible = expanded;
		contentBg->setPosition(position.x, position.y + kHeaderHeight);
	}

	if (emptyMsg) {
		emptyMsg->visible = expanded;
		emptyMsg->setPosition(position.x + kPadding, position.y + kHeaderHeight + kPadding);
	}

	// Update overall size
	size = {panelWidth, expanded ? kExpandedHeight : kCollapsedHeight};
}

Foundation::Rect ResourcesPanel::getBounds() const {
	return {position.x, position.y, panelWidth, expanded ? kExpandedHeight : kCollapsedHeight};
}

bool ResourcesPanel::handleEvent(UI::InputEvent& event) {
	// Use Component's dispatchEvent to properly handle children
	return dispatchEvent(event);
}

// render() inherited from Component - automatically renders all children

}  // namespace world_sim
