#pragma once

#include "math/Types.h"

#include <vector>

namespace renderer {

/// Cubic Bezier curve defined by 4 control points
struct CubicBezier {
	Foundation::Vec2 p0; ///< Start point
	Foundation::Vec2 p1; ///< First control point
	Foundation::Vec2 p2; ///< Second control point
	Foundation::Vec2 p3; ///< End point
};

/// Quadratic Bezier curve defined by 3 control points (for future SVG support)
struct QuadraticBezier {
	Foundation::Vec2 p0; ///< Start point
	Foundation::Vec2 p1; ///< Control point
	Foundation::Vec2 p2; ///< End point
};

/// Flatten a cubic Bezier curve to line segments using De Casteljau's algorithm
/// with adaptive subdivision based on flatness tolerance.
///
/// @param curve The cubic Bezier curve to flatten
/// @param tolerance Maximum allowed distance from the chord to the curve (in pixels).
///                  Smaller values produce smoother curves with more vertices.
///                  Recommended: 0.5-1.0 for screen rendering.
/// @param output Vector to receive the flattened points. The start point (p0) is NOT
///               included; endpoints of each segment ARE included.
/// @param maxDepth Maximum recursion depth to prevent stack overflow (default: 16)
void flattenCubicBezier(
	const CubicBezier& curve,
	float			   tolerance,
	std::vector<Foundation::Vec2>& output,
	int maxDepth = 16
);

/// Flatten a quadratic Bezier curve to line segments using De Casteljau's algorithm.
///
/// @param curve The quadratic Bezier curve to flatten
/// @param tolerance Maximum allowed distance from the chord to the curve (in pixels)
/// @param output Vector to receive the flattened points
/// @param maxDepth Maximum recursion depth (default: 16)
void flattenQuadraticBezier(
	const QuadraticBezier& curve,
	float				   tolerance,
	std::vector<Foundation::Vec2>& output,
	int maxDepth = 16
);

} // namespace renderer
