#pragma once

// Clipping Types
//
// Types for the clipping and scrolling system. Follows Flutter/Unity pattern
// where clipping and content offset (scrolling) are independent concepts.
//
// See /docs/technical/ui-framework/clipping.md for design documentation.

#include "graphics/Rect.h"
#include "math/Types.h"

#include <optional>
#include <variant>
#include <vector>

namespace Foundation { // NOLINT(readability-identifier-naming)

	// ============================================================================
	// Clip Shape Types
	// ============================================================================

	// Axis-aligned rectangle clip (FAST PATH - shader-based, zero GL state changes)
	struct ClipRect {
		std::optional<Rect> bounds = std::nullopt; // nullopt = use layer bounds
	};

	// Rectangle with rounded corners (requires stencil buffer - NOT YET IMPLEMENTED)
	struct ClipRoundedRect {
		std::optional<Rect> bounds = std::nullopt;
		float				cornerRadius = 8.0F;
	};

	// Circular clip (requires stencil buffer - NOT YET IMPLEMENTED)
	struct ClipCircle {
		Vec2  center{0.0F, 0.0F};
		float radius = 50.0F;
	};

	// Arbitrary polygon clip (requires stencil buffer - NOT YET IMPLEMENTED)
	struct ClipPath {
		std::vector<Vec2> vertices; // Closed polygon
	};

	// Variant type for all clip shapes
	using ClipShape = std::variant<ClipRect, ClipRoundedRect, ClipCircle, ClipPath>;

	// ============================================================================
	// Clip Mode
	// ============================================================================

	enum class ClipMode {
		Inside, // Standard overflow clipping - content visible INSIDE the shape
		Outside // Punch holes - content visible OUTSIDE the shape (e.g., spotlight effect)
	};

	// ============================================================================
	// Clip Settings
	// ============================================================================

	struct ClipSettings {
		ClipShape shape;
		ClipMode  mode = ClipMode::Inside;
	};

} // namespace Foundation
