#include "ResourcesPanel.h"

#include <primitives/Primitives.h>
#include <theme/Theme.h>

namespace world_sim {

ResourcesPanel::ResourcesPanel(const Args& args)
	: panelWidth(args.width) {

	// Create header button (shows "Storage ▼" or "Storage ▲")
	headerButtonHandle = addChild(UI::Button(UI::Button::Args{
		.label = "Storage \xE2\x96\xBC", // ▼ (UTF-8)
		.position = {0.0F, 0.0F},
		.size = {panelWidth, kCollapsedHeight},
		.type = UI::Button::Type::Secondary,
		.onClick = [this]() { toggle(); },
		.id = "resources_header"
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

	// Update button label to show arrow direction
	auto* button = getChild<UI::Button>(headerButtonHandle);
	if (button) {
		button->label = expanded ? "Storage \xE2\x96\xB2" : "Storage \xE2\x96\xBC"; // ▲ or ▼
	}

	updateLayout();
}

void ResourcesPanel::updateLayout() {
	// Position header at top
	auto* header = getChild<UI::Button>(headerButtonHandle);
	if (header) {
		header->setPosition(position.x, position.y);
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
	if (!visible) {
		return false;
	}

	// Dispatch to header button
	auto* header = getChild<UI::Button>(headerButtonHandle);
	if (header && header->handleEvent(event)) {
		return true;
	}

	// If expanded, consume clicks in content area
	if (expanded && event.type == UI::InputEvent::Type::MouseDown) {
		Foundation::Rect contentBounds{
			position.x,
			position.y + kHeaderHeight,
			panelWidth,
			kExpandedHeight - kHeaderHeight
		};
		if (event.position.x >= contentBounds.x &&
			event.position.x < contentBounds.x + contentBounds.width &&
			event.position.y >= contentBounds.y &&
			event.position.y < contentBounds.y + contentBounds.height) {
			event.consume();
			return true;
		}
	}

	return event.isConsumed();
}

void ResourcesPanel::render() {
	if (!visible) {
		return;
	}

	// Render header button
	auto* header = getChild<UI::Button>(headerButtonHandle);
	if (header) {
		header->render();
	}

	// Render expanded content if visible
	if (expanded) {
		auto* contentBg = getChild<UI::Rectangle>(contentBackgroundHandle);
		if (contentBg) {
			contentBg->render();
		}

		auto* emptyMsg = getChild<UI::Text>(emptyMessageHandle);
		if (emptyMsg) {
			emptyMsg->render();
		}
	}
}

}  // namespace world_sim
