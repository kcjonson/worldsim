#pragma once

// UI Design Tokens
//
// Centralized design tokens for consistent UI styling.
// Single compile-time theme - values are inlined at compile time.
//
// Naming convention:
// - "Panel" = visual style (dark bg, 1px border)
// - "View" = structural UI element (EntityInfoView, TaskListView)
// - "Component" = base class for all UI elements

#include "graphics/Color.h"

namespace UI::Theme {

	// === Colors ===
	// Semantic color tokens for UI elements

	namespace Colors {

		// Panel backgrounds (90% opacity for floating panels)
		inline constexpr Foundation::Color panelBackground{0.10F, 0.10F, 0.12F, 0.90F};
		inline constexpr Foundation::Color panelBorder{0.30F, 0.30F, 0.40F, 1.0F};

		// Sidebar (docked panels - slightly darker, more opaque)
		inline constexpr Foundation::Color sidebarBackground{0.08F, 0.08F, 0.10F, 0.95F};

		// Text hierarchy
		inline constexpr Foundation::Color textTitle{0.90F, 0.90F, 0.95F, 1.0F};
		inline constexpr Foundation::Color textHeader{0.70F, 0.80F, 0.90F, 1.0F}; // Section headers
		inline constexpr Foundation::Color textBody{0.75F, 0.75F, 0.80F, 1.0F};
		inline constexpr Foundation::Color textSecondary{0.60F, 0.60F, 0.65F, 1.0F};
		inline constexpr Foundation::Color textMuted{0.45F, 0.45F, 0.50F, 1.0F};
		inline constexpr Foundation::Color textClickable{0.50F, 0.70F, 0.90F, 1.0F};

		// Close button (destructive action feel)
		inline constexpr Foundation::Color closeButtonBackground{0.30F, 0.20F, 0.20F, 0.90F};
		inline constexpr Foundation::Color closeButtonBorder{0.50F, 0.30F, 0.30F, 1.0F};
		inline constexpr Foundation::Color closeButtonText{0.90F, 0.60F, 0.60F, 1.0F};

		// Action button (positive action)
		inline constexpr Foundation::Color actionButtonBackground{0.20F, 0.40F, 0.30F, 0.90F};
		inline constexpr Foundation::Color actionButtonBorder{0.30F, 0.60F, 0.40F, 1.0F};
		inline constexpr Foundation::Color actionButtonText{0.70F, 0.95F, 0.80F, 1.0F};

		// Card/list item (within panels)
		inline constexpr Foundation::Color cardBackground{0.15F, 0.15F, 0.20F, 0.90F};
		inline constexpr Foundation::Color cardBorder{0.25F, 0.25F, 0.35F, 0.80F};

		// Selection highlight
		inline constexpr Foundation::Color selectionBackground{0.30F, 0.50F, 0.70F, 0.90F};
		inline constexpr Foundation::Color selectionBorder{0.50F, 0.70F, 1.0F, 1.0F};

		// Status indicators
		inline constexpr Foundation::Color statusActive{0.50F, 0.90F, 0.50F, 1.0F};
		inline constexpr Foundation::Color statusPending{0.90F, 0.90F, 0.50F, 1.0F};
		inline constexpr Foundation::Color statusBlocked{0.90F, 0.50F, 0.50F, 1.0F};
		inline constexpr Foundation::Color statusIdle{0.60F, 0.60F, 0.65F, 1.0F};

	} // namespace Colors

	// === Spacing ===
	// Consistent spacing values

	namespace Spacing {

		inline constexpr float panelPadding = 12.0F;
		inline constexpr float cardPadding = 8.0F;
		inline constexpr float itemSpacing = 4.0F;
		inline constexpr float sectionSpacing = 12.0F;

	} // namespace Spacing

	// === Typography ===
	// Font size hierarchy

	namespace Typography {

		inline constexpr float titleSize = 14.0F;
		inline constexpr float headerSize = 12.0F;
		inline constexpr float bodySize = 11.0F;
		inline constexpr float smallSize = 10.0F;

	} // namespace Typography

	// === Borders ===
	// Border dimensions

	namespace Borders {

		inline constexpr float defaultWidth = 1.0F;
		inline constexpr float defaultRadius = 0.0F; // No rounded corners yet

	} // namespace Borders

} // namespace UI::Theme
