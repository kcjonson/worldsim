#pragma once

#include "ButtonStyle.h"
#include "theme/Theme.h"

// List Item Button Style
//
// Flat, minimal button appearance for use in selectable lists.
// Transparent background with bottom border only (avoids double borders).
// Selected items get a subtle darker background.

namespace UI {
namespace ButtonStyles {

// List item style - flat with bottom border only
inline ButtonAppearance listItem(bool isSelected = false) {
	ButtonAppearance appearance;

	// Colors for list items
	auto transparentBg = Foundation::Color{0.0F, 0.0F, 0.0F, 0.0F};
	auto hoverBg = Foundation::Color{1.0F, 1.0F, 1.0F, 0.05F};
	auto pressedBg = Foundation::Color{1.0F, 1.0F, 1.0F, 0.08F};
	auto selectedBg = Foundation::Color{0.0F, 0.0F, 0.0F, 0.15F};  // Subtle dark
	auto borderColor = Foundation::Color{1.0F, 1.0F, 1.0F, 0.1F};  // Subtle border
	auto textColor = Theme::Colors::textBody;

	auto baseBg = isSelected ? selectedBg : transparentBg;

	// Bottom border only (1px) - no corner radius for clean list look
	auto bottomBorder = Foundation::BorderStyle{
		.color = borderColor,
		.width = 1.0F,
		.cornerRadius = 0.0F,
		.position = Foundation::BorderPosition::Inside
	};

	// Normal
	appearance.normal.background.fill = baseBg;
	appearance.normal.background.border = bottomBorder;
	appearance.normal.textColor = textColor;
	appearance.normal.fontSize = 12.0F;
	appearance.normal.paddingX = 8.0F;
	appearance.normal.paddingY = 4.0F;

	// Hover - subtle highlight
	appearance.hover.background.fill = isSelected ? selectedBg : hoverBg;
	appearance.hover.background.border = bottomBorder;
	appearance.hover.textColor = textColor;
	appearance.hover.fontSize = 12.0F;
	appearance.hover.paddingX = 8.0F;
	appearance.hover.paddingY = 4.0F;

	// Pressed
	appearance.pressed.background.fill = isSelected ? selectedBg : pressedBg;
	appearance.pressed.background.border = bottomBorder;
	appearance.pressed.textColor = textColor;
	appearance.pressed.fontSize = 12.0F;
	appearance.pressed.paddingX = 8.0F;
	appearance.pressed.paddingY = 4.0F;

	// Disabled
	appearance.disabled.background.fill = transparentBg;
	appearance.disabled.background.border = bottomBorder;
	appearance.disabled.textColor = Theme::Colors::textMuted;
	appearance.disabled.fontSize = 12.0F;
	appearance.disabled.paddingX = 8.0F;
	appearance.disabled.paddingY = 4.0F;

	// Focused - slightly brighter border
	appearance.focused.background.fill = baseBg;
	appearance.focused.background.border = Foundation::BorderStyle{
		.color = Foundation::Color{1.0F, 1.0F, 1.0F, 0.3F},
		.width = 1.0F,
		.cornerRadius = 0.0F,
		.position = Foundation::BorderPosition::Inside
	};
	appearance.focused.textColor = textColor;
	appearance.focused.fontSize = 12.0F;
	appearance.focused.paddingX = 8.0F;
	appearance.focused.paddingY = 4.0F;

	return appearance;
}

// Cached static instances for pointer stability (Button::Args needs pointer)
inline ButtonAppearance& listItemNormal() {
	static ButtonAppearance style = listItem(false);
	return style;
}

inline ButtonAppearance& listItemSelected() {
	static ButtonAppearance style = listItem(true);
	return style;
}

} // namespace ButtonStyles
} // namespace UI
