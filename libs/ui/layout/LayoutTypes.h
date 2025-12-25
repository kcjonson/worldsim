#pragma once

// Layout system types and enums for LayoutContainer.
// See: /docs/technical/ui-framework/layout-system.md

namespace UI {

// Direction for LayoutContainer stacking
enum class Direction {
	Vertical,	// Stack children top to bottom
	Horizontal	// Stack children left to right
};

// Horizontal alignment for children in a vertical layout
enum class HAlign {
	Left,
	Center,
	Right
};

// Vertical alignment for children in a horizontal layout
enum class VAlign {
	Top,
	Center,
	Bottom
};

} // namespace UI
