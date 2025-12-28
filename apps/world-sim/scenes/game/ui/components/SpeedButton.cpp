#include "SpeedButton.h"

#include <theme/Theme.h>

namespace world_sim {

SpeedButton::SpeedButton(const Args& args)
	: onClick(args.onClick)
	, id(args.id) {
	position = args.position;
	size = {kButtonSize, kButtonSize};

	// Create background rectangle
	backgroundHandle = addChild(UI::Rectangle(UI::Rectangle::Args{
		.position = {0.0F, 0.0F},
		.size = {kButtonSize, kButtonSize},
		.style =
			{
				.fill = UI::Theme::Colors::cardBackground,
				.border =
					Foundation::BorderStyle{
						.color = UI::Theme::Colors::cardBorder,
						.width = 1.0F,
						.cornerRadius = 4.0F,
						.position = Foundation::BorderPosition::Inside},
			},
		.id = id.c_str(),
		.zIndex = 501}));

	// Create icon centered in button
	iconHandle = addChild(UI::Icon(UI::Icon::Args{
		.position = {0.0F, 0.0F},
		.size = kIconSize,
		.svgPath = "assets/" + args.iconPath,
		.tint = UI::Theme::Colors::textBody,
		.id = (id + "_icon").c_str()}));

	if (auto* iconPtr = getChild<UI::Icon>(iconHandle)) {
		iconPtr->zIndex = 502;
	}

	positionElements();
}

void SpeedButton::setActive(bool newActive) {
	if (active == newActive) {
		return;
	}
	active = newActive;
	updateAppearance();
}

void SpeedButton::setPosition(float x, float y) {
	Component::setPosition(x, y);
	positionElements();
}

void SpeedButton::positionElements() {
	if (auto* bg = getChild<UI::Rectangle>(backgroundHandle)) {
		bg->setPosition(position.x, position.y);
	}
	if (auto* iconPtr = getChild<UI::Icon>(iconHandle)) {
		float iconOffset = (kButtonSize - kIconSize) / 2.0F;
		iconPtr->setPosition(position.x + iconOffset, position.y + iconOffset);
	}
}

bool SpeedButton::handleEvent(UI::InputEvent& event) {
	if (event.type == UI::InputEvent::Type::MouseMove) {
		bool wasHovered = hovered;
		hovered = containsPoint(event.position);
		if (wasHovered != hovered) {
			updateAppearance();
		}
		return false;  // Don't consume mouse move
	}

	if (event.type == UI::InputEvent::Type::MouseDown) {
		if (containsPoint(event.position)) {
			pressed = true;
			updateAppearance();
			return true;
		}
	}

	if (event.type == UI::InputEvent::Type::MouseUp) {
		if (pressed) {
			pressed = false;
			updateAppearance();
			if (containsPoint(event.position) && onClick) {
				onClick();
			}
			return true;
		}
	}

	return false;
}

void SpeedButton::updateAppearance() {
	auto* bg = getChild<UI::Rectangle>(backgroundHandle);
	auto* iconPtr = getChild<UI::Icon>(iconHandle);
	if (!bg || !iconPtr) {
		return;
	}

	Foundation::Color bgColor;
	Foundation::Color borderColor;
	Foundation::Color iconColor;

	if (active) {
		// Active state - highlighted
		bgColor = UI::Theme::Colors::selectionBackground;
		borderColor = UI::Theme::Colors::selectionBorder;
		iconColor = UI::Theme::Colors::textTitle;
	} else if (pressed) {
		// Pressed state - darker
		bgColor = Foundation::Color{
			UI::Theme::Colors::cardBackground.r - 0.05F,
			UI::Theme::Colors::cardBackground.g - 0.05F,
			UI::Theme::Colors::cardBackground.b - 0.05F,
			UI::Theme::Colors::cardBackground.a};
		borderColor = UI::Theme::Colors::cardBorder;
		iconColor = UI::Theme::Colors::textBody;
	} else if (hovered) {
		// Hover state - lighter
		bgColor = Foundation::Color{
			UI::Theme::Colors::cardBackground.r + 0.1F,
			UI::Theme::Colors::cardBackground.g + 0.1F,
			UI::Theme::Colors::cardBackground.b + 0.1F,
			UI::Theme::Colors::cardBackground.a};
		borderColor = UI::Theme::Colors::cardBorder;
		iconColor = UI::Theme::Colors::textBody;
	} else {
		// Normal state
		bgColor = UI::Theme::Colors::cardBackground;
		borderColor = UI::Theme::Colors::cardBorder;
		iconColor = UI::Theme::Colors::textBody;
	}

	bg->style.fill = bgColor;
	bg->style.border = Foundation::BorderStyle{
		.color = borderColor,
		.width = 1.0F,
		.cornerRadius = 4.0F,
		.position = Foundation::BorderPosition::Inside};

	iconPtr->setTint(iconColor);
}

bool SpeedButton::containsPoint(Foundation::Vec2 point) const {
	return point.x >= position.x && point.x < position.x + kButtonSize &&
		   point.y >= position.y && point.y < position.y + kButtonSize;
}

// render() inherited from Component - automatically renders all children

}  // namespace world_sim
