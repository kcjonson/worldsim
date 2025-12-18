#pragma once

#include "graphics/Color.h"
#include "graphics/PrimitiveStyles.h"

// TabBar Style Definitions
//
// Defines visual styles for tab bar UI component with support for multiple states.
// Each tab has 5 style variants: Normal, Hover, Active (selected), Disabled, Focused
// Similar pattern to ButtonStyle for consistency.

namespace UI {

	// Individual tab style - combines rectangle background with text properties
	struct TabStyle {
		// Background styling
		Foundation::RectStyle background;

		// Text styling
		Foundation::Color textColor = Foundation::Color::white();
		float			  fontSize = 12.0F;

		// Padding (text offset from background edges)
		float paddingX = 12.0F;
		float paddingY = 6.0F;
	};

	// Complete tab bar appearance definition
	struct TabBarAppearance {
		// Tab states
		TabStyle normal;	// Default unselected tab
		TabStyle hover;		// Mouse over unselected tab
		TabStyle active;	// Currently selected tab
		TabStyle disabled;	// Disabled tab
		TabStyle focused;	// Keyboard focus on tab bar (shows on active tab)

		// Bar container styling
		Foundation::RectStyle barBackground;

		// Layout
		float tabSpacing = 2.0F;  // Gap between tabs
		float barPadding = 4.0F;  // Padding inside bar container
	};

	// Predefined tab bar styles
	namespace TabBarStyles {

		// Default style - dark theme matching game UI
		inline TabBarAppearance defaultStyle() {
			TabBarAppearance appearance;

			// Normal tab - subtle dark background
			appearance.normal.background.fill = Foundation::Color{0.15F, 0.15F, 0.2F, 0.9F};
			appearance.normal.background.border = Foundation::BorderStyle{
				.color = Foundation::Color{0.25F, 0.25F, 0.3F, 1.0F},
				.width = 1.0F,
				.cornerRadius = 3.0F,
				.position = Foundation::BorderPosition::Inside};
			appearance.normal.textColor = Foundation::Color{0.6F, 0.6F, 0.65F, 1.0F};
			appearance.normal.fontSize = 12.0F;
			appearance.normal.paddingX = 12.0F;
			appearance.normal.paddingY = 6.0F;

			// Hover tab - lighter background
			appearance.hover.background.fill = Foundation::Color{0.2F, 0.2F, 0.25F, 0.95F};
			appearance.hover.background.border = Foundation::BorderStyle{
				.color = Foundation::Color{0.3F, 0.3F, 0.35F, 1.0F},
				.width = 1.0F,
				.cornerRadius = 3.0F,
				.position = Foundation::BorderPosition::Inside};
			appearance.hover.textColor = Foundation::Color{0.8F, 0.8F, 0.85F, 1.0F};
			appearance.hover.fontSize = 12.0F;
			appearance.hover.paddingX = 12.0F;
			appearance.hover.paddingY = 6.0F;

			// Active tab - prominent, stands out
			appearance.active.background.fill = Foundation::Color{0.25F, 0.3F, 0.4F, 1.0F};
			appearance.active.background.border = Foundation::BorderStyle{
				.color = Foundation::Color{0.35F, 0.4F, 0.5F, 1.0F},
				.width = 1.0F,
				.cornerRadius = 3.0F,
				.position = Foundation::BorderPosition::Inside};
			appearance.active.textColor = Foundation::Color{0.95F, 0.95F, 1.0F, 1.0F};
			appearance.active.fontSize = 12.0F;
			appearance.active.paddingX = 12.0F;
			appearance.active.paddingY = 6.0F;

			// Disabled tab - greyed out
			appearance.disabled.background.fill = Foundation::Color{0.12F, 0.12F, 0.15F, 0.7F};
			appearance.disabled.background.border = Foundation::BorderStyle{
				.color = Foundation::Color{0.2F, 0.2F, 0.22F, 0.5F},
				.width = 1.0F,
				.cornerRadius = 3.0F,
				.position = Foundation::BorderPosition::Inside};
			appearance.disabled.textColor = Foundation::Color{0.4F, 0.4F, 0.42F, 0.8F};
			appearance.disabled.fontSize = 12.0F;
			appearance.disabled.paddingX = 12.0F;
			appearance.disabled.paddingY = 6.0F;

			// Focused tab - active with focus ring
			appearance.focused.background.fill = Foundation::Color{0.25F, 0.3F, 0.4F, 1.0F};
			appearance.focused.background.border = Foundation::BorderStyle{
				.color = Foundation::Color{0.4F, 0.6F, 0.9F, 1.0F},  // Blue focus ring
				.width = 2.0F,
				.cornerRadius = 3.0F,
				.position = Foundation::BorderPosition::Outside};
			appearance.focused.textColor = Foundation::Color{0.95F, 0.95F, 1.0F, 1.0F};
			appearance.focused.fontSize = 12.0F;
			appearance.focused.paddingX = 12.0F;
			appearance.focused.paddingY = 6.0F;

			// Bar container - semi-transparent dark
			appearance.barBackground.fill = Foundation::Color{0.1F, 0.1F, 0.12F, 0.85F};
			appearance.barBackground.border = Foundation::BorderStyle{
				.color = Foundation::Color{0.2F, 0.2F, 0.25F, 1.0F},
				.width = 1.0F,
				.cornerRadius = 4.0F,
				.position = Foundation::BorderPosition::Inside};

			appearance.tabSpacing = 2.0F;
			appearance.barPadding = 4.0F;

			return appearance;
		}

	}  // namespace TabBarStyles

}  // namespace UI
