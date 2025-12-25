#pragma once

// Panel Style Factories
//
// Factory functions for common panel visual styles.
// "Panel" here refers to the visual style (dark bg, 1px border),
// not the structural UI element (which is called a "View").

#include "Theme.h"
#include "graphics/PrimitiveStyles.h"

namespace UI::PanelStyles {

	// Standard floating panel (EntityInfoView, TaskListView, etc.)
	// Dark semi-transparent background with subtle border
	inline Foundation::RectStyle floating() {
		return Foundation::RectStyle{
			.fill = Theme::Colors::panelBackground,
			.border = Foundation::BorderStyle{
				.color = Theme::Colors::panelBorder,
				.width = Theme::Borders::defaultWidth,
				.cornerRadius = Theme::Borders::defaultRadius,
				.position = Foundation::BorderPosition::Inside
			}
		};
	}

	// Sidebar panel (docked to screen edge)
	// Slightly darker and more opaque than floating
	inline Foundation::RectStyle sidebar() {
		return Foundation::RectStyle{
			.fill = Theme::Colors::sidebarBackground,
			.border = Foundation::BorderStyle{
				.color = Theme::Colors::panelBorder,
				.width = Theme::Borders::defaultWidth,
				.cornerRadius = 0.0F, // No rounded corners for docked panels
				.position = Foundation::BorderPosition::Inside
			}
		};
	}

	// Close button (destructive action styling)
	inline Foundation::RectStyle closeButton() {
		return Foundation::RectStyle{
			.fill = Theme::Colors::closeButtonBackground,
			.border = Foundation::BorderStyle{
				.color = Theme::Colors::closeButtonBorder,
				.width = Theme::Borders::defaultWidth,
				.cornerRadius = 0.0F,
				.position = Foundation::BorderPosition::Inside
			}
		};
	}

	// Action button (positive action styling)
	inline Foundation::RectStyle actionButton() {
		return Foundation::RectStyle{
			.fill = Theme::Colors::actionButtonBackground,
			.border = Foundation::BorderStyle{
				.color = Theme::Colors::actionButtonBorder,
				.width = Theme::Borders::defaultWidth,
				.cornerRadius = 0.0F,
				.position = Foundation::BorderPosition::Inside
			}
		};
	}

	// Card within panel (recipe cards, list items, etc.)
	inline Foundation::RectStyle card() {
		return Foundation::RectStyle{
			.fill = Theme::Colors::cardBackground,
			.border = Foundation::BorderStyle{
				.color = Theme::Colors::cardBorder,
				.width = Theme::Borders::defaultWidth,
				.cornerRadius = 0.0F,
				.position = Foundation::BorderPosition::Inside
			}
		};
	}

	// Selection highlight
	inline Foundation::RectStyle selection() {
		return Foundation::RectStyle{
			.fill = Theme::Colors::selectionBackground,
			.border = Foundation::BorderStyle{
				.color = Theme::Colors::selectionBorder,
				.width = Theme::Borders::defaultWidth,
				.cornerRadius = 0.0F,
				.position = Foundation::BorderPosition::Inside
			}
		};
	}

} // namespace UI::PanelStyles
