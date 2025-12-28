#include "SpeedButton.h"

#include <core/RenderContext.h>
#include <theme/Theme.h>

namespace world_sim {

SpeedButton::SpeedButton(const Args& args)
	: position(args.position)
	, onClick(args.onClick)
	, id(args.id) {
	// Create background rectangle
	background = std::make_unique<UI::Rectangle>(UI::Rectangle::Args{
		.position = position,
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
		.zIndex = 501});

	// Create icon centered in button
	float iconOffset = (kButtonSize - kIconSize) / 2.0F;
	icon = std::make_unique<UI::Icon>(UI::Icon::Args{
		.position = {position.x + iconOffset, position.y + iconOffset},
		.size = kIconSize,
		.svgPath = "assets/" + args.iconPath,
		.tint = UI::Theme::Colors::textBody,
		.id = (id + "_icon").c_str()});
	icon->zIndex = 502;
}

void SpeedButton::setActive(bool newActive) {
	if (active == newActive) {
		return;
	}
	active = newActive;
	updateAppearance();
}

void SpeedButton::setPosition(Foundation::Vec2 pos) {
	position = pos;
	if (background) {
		background->setPosition(pos.x, pos.y);
	}
	if (icon) {
		float iconOffset = (kButtonSize - kIconSize) / 2.0F;
		icon->setPosition(pos.x + iconOffset, pos.y + iconOffset);
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

void SpeedButton::render() {
	if (background) {
		UI::RenderContext::setZIndex(background->zIndex);
		background->render();
	}
	if (icon) {
		UI::RenderContext::setZIndex(icon->zIndex);
		icon->render();
	}
}

float SpeedButton::getWidth() const {
	return kButtonSize;
}

float SpeedButton::getHeight() const {
	return kButtonSize;
}

void SpeedButton::updateAppearance() {
	if (!background || !icon) {
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

	background->style.fill = bgColor;
	background->style.border = Foundation::BorderStyle{
		.color = borderColor,
		.width = 1.0F,
		.cornerRadius = 4.0F,
		.position = Foundation::BorderPosition::Inside};

	icon->setTint(iconColor);
}

bool SpeedButton::containsPoint(Foundation::Vec2 point) const {
	return point.x >= position.x && point.x < position.x + kButtonSize &&
		   point.y >= position.y && point.y < position.y + kButtonSize;
}

}  // namespace world_sim
