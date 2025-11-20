#pragma once

#include "graphics/color.h"
#include "graphics/primitive_styles.h"

// Button Style Definitions
//
// Defines visual styles for UI buttons with support for multiple states.
// Each button has 5 style variants: Normal, Hover, Pressed, Disabled, Focused

namespace UI {

	// Button style - combines rectangle background with text properties
	struct ButtonStyle {
		// Background styling
		Foundation::RectStyle background;

		// Text styling
		Foundation::Color textColor = Foundation::Color::White();
		float			  fontSize = 16.0F;

		// Padding (text offset from background edges)
		float paddingX = 16.0F;
		float paddingY = 8.0F;
	};

	// Complete button appearance definition (all 5 states)
	struct ButtonAppearance {
		ButtonStyle normal;
		ButtonStyle hover;
		ButtonStyle pressed;
		ButtonStyle disabled;
		ButtonStyle focused;
	};

	// Predefined button type styles
	namespace ButtonStyles {

		// Primary button - Blue, high prominence
		inline ButtonAppearance Primary() {
			ButtonAppearance appearance;

			// Normal - Blue background
			appearance.normal.background.fill = Foundation::Color{0.2F, 0.4F, 0.8F, 1.0F};
			appearance.normal.background.border = Foundation::BorderStyle{
				.color = Foundation::Color{0.1F, 0.3F, 0.7F, 1.0F},
				.width = 2.0F,
				.cornerRadius = 4.0F,
				.position = Foundation::BorderPosition::Inside};
			appearance.normal.textColor = Foundation::Color::White();
			appearance.normal.fontSize = 16.0F;

			// Hover - Lighter blue
			appearance.hover.background.fill = Foundation::Color{0.3F, 0.5F, 0.9F, 1.0F};
			appearance.hover.background.border = Foundation::BorderStyle{
				.color = Foundation::Color{0.2F, 0.4F, 0.8F, 1.0F},
				.width = 2.0F,
				.cornerRadius = 4.0F,
				.position = Foundation::BorderPosition::Inside};
			appearance.hover.textColor = Foundation::Color::White();
			appearance.hover.fontSize = 16.0F;

			// Pressed - Darker blue
			appearance.pressed.background.fill = Foundation::Color{0.1F, 0.3F, 0.7F, 1.0F};
			appearance.pressed.background.border = Foundation::BorderStyle{
				.color = Foundation::Color{0.05F, 0.2F, 0.6F, 1.0F},
				.width = 2.0F,
				.cornerRadius = 4.0F,
				.position = Foundation::BorderPosition::Inside};
			appearance.pressed.textColor = Foundation::Color::White();
			appearance.pressed.fontSize = 16.0F;

			// Disabled - Grey
			appearance.disabled.background.fill = Foundation::Color{0.4F, 0.4F, 0.4F, 1.0F};
			appearance.disabled.background.border = Foundation::BorderStyle{
				.color = Foundation::Color{0.3F, 0.3F, 0.3F, 1.0F},
				.width = 2.0F,
				.cornerRadius = 4.0F,
				.position = Foundation::BorderPosition::Inside};
			appearance.disabled.textColor = Foundation::Color{0.6F, 0.6F, 0.6F, 1.0F};
			appearance.disabled.fontSize = 16.0F;

			// Focused - Blue with bright border (focus ring)
			appearance.focused.background.fill = Foundation::Color{0.2F, 0.4F, 0.8F, 1.0F};
			appearance.focused.background.border = Foundation::BorderStyle{
				.color = Foundation::Color{0.4F, 0.7F, 1.0F, 1.0F}, // Bright blue focus ring
				.width = 3.0F,
				.cornerRadius = 4.0F,
				.position = Foundation::BorderPosition::Outside}; // Outside for focus ring
			appearance.focused.textColor = Foundation::Color::White();
			appearance.focused.fontSize = 16.0F;

			return appearance;
		}

		// Secondary button - Light blue, lower prominence
		inline ButtonAppearance Secondary() {
			ButtonAppearance appearance;

			// Normal - Light blue background
			appearance.normal.background.fill = Foundation::Color{0.7F, 0.8F, 0.95F, 1.0F};
			appearance.normal.background.border = Foundation::BorderStyle{
				.color = Foundation::Color{0.5F, 0.6F, 0.8F, 1.0F},
				.width = 2.0F,
				.cornerRadius = 4.0F,
				.position = Foundation::BorderPosition::Inside};
			appearance.normal.textColor = Foundation::Color{0.1F, 0.2F, 0.4F, 1.0F}; // Dark blue text
			appearance.normal.fontSize = 16.0F;

			// Hover - Slightly lighter
			appearance.hover.background.fill = Foundation::Color{0.8F, 0.9F, 1.0F, 1.0F};
			appearance.hover.background.border = Foundation::BorderStyle{
				.color = Foundation::Color{0.6F, 0.7F, 0.9F, 1.0F},
				.width = 2.0F,
				.cornerRadius = 4.0F,
				.position = Foundation::BorderPosition::Inside};
			appearance.hover.textColor = Foundation::Color{0.1F, 0.2F, 0.4F, 1.0F};
			appearance.hover.fontSize = 16.0F;

			// Pressed - Slightly darker
			appearance.pressed.background.fill = Foundation::Color{0.6F, 0.7F, 0.9F, 1.0F};
			appearance.pressed.background.border = Foundation::BorderStyle{
				.color = Foundation::Color{0.4F, 0.5F, 0.7F, 1.0F},
				.width = 2.0F,
				.cornerRadius = 4.0F,
				.position = Foundation::BorderPosition::Inside};
			appearance.pressed.textColor = Foundation::Color{0.1F, 0.2F, 0.4F, 1.0F};
			appearance.pressed.fontSize = 16.0F;

			// Disabled - Grey
			appearance.disabled.background.fill = Foundation::Color{0.4F, 0.4F, 0.4F, 1.0F};
			appearance.disabled.background.border = Foundation::BorderStyle{
				.color = Foundation::Color{0.3F, 0.3F, 0.3F, 1.0F},
				.width = 2.0F,
				.cornerRadius = 4.0F,
				.position = Foundation::BorderPosition::Inside};
			appearance.disabled.textColor = Foundation::Color{0.6F, 0.6F, 0.6F, 1.0F};
			appearance.disabled.fontSize = 16.0F;

			// Focused - Light blue with bright border
			appearance.focused.background.fill = Foundation::Color{0.7F, 0.8F, 0.95F, 1.0F};
			appearance.focused.background.border = Foundation::BorderStyle{
				.color = Foundation::Color{0.4F, 0.7F, 1.0F, 1.0F}, // Bright blue focus ring
				.width = 3.0F,
				.cornerRadius = 4.0F,
				.position = Foundation::BorderPosition::Outside};
			appearance.focused.textColor = Foundation::Color{0.1F, 0.2F, 0.4F, 1.0F};
			appearance.focused.fontSize = 16.0F;

			return appearance;
		}

	} // namespace ButtonStyles

} // namespace UI
