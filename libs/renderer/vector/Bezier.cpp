// Bezier curve flattening using De Casteljau's algorithm
// Converts smooth curves to line segments for tessellation

#include "Bezier.h"

#include <cmath>

namespace renderer {

namespace {

	/// Calculate the perpendicular distance from a point to a line segment.
	/// Used to determine if a curve is "flat enough" to approximate with a line.
	float pointToLineDistance(
		const Foundation::Vec2& point,
		const Foundation::Vec2& lineStart,
		const Foundation::Vec2& lineEnd
	) {
		Foundation::Vec2 line = lineEnd - lineStart;
		float			 lineLengthSq = line.x * line.x + line.y * line.y;

		if (lineLengthSq < 1e-10F) {
			// Degenerate case: line is a point
			Foundation::Vec2 diff = point - lineStart;
			return std::sqrt(diff.x * diff.x + diff.y * diff.y);
		}

		// Calculate perpendicular distance using cross product
		// |AB × AP| / |AB| = distance from P to line AB
		Foundation::Vec2 toPoint = point - lineStart;
		float cross = std::abs(line.x * toPoint.y - line.y * toPoint.x);
		return cross / std::sqrt(lineLengthSq);
	}

	/// Check if a cubic Bezier curve is flat enough to approximate with a straight line.
	/// Uses the maximum distance from control points to the chord (p0 to p3).
	bool isCubicFlatEnough(const CubicBezier& curve, float tolerance) {
		// Calculate distance from both control points to the chord
		float dist1 = pointToLineDistance(curve.p1, curve.p0, curve.p3);
		float dist2 = pointToLineDistance(curve.p2, curve.p0, curve.p3);

		// Curve is flat if both control points are within tolerance of the chord
		return std::max(dist1, dist2) <= tolerance;
	}

	/// Check if a quadratic Bezier curve is flat enough.
	bool isQuadraticFlatEnough(const QuadraticBezier& curve, float tolerance) {
		float dist = pointToLineDistance(curve.p1, curve.p0, curve.p2);
		return dist <= tolerance;
	}

	/// Internal recursive implementation for cubic Bezier flattening
	void flattenCubicRecursive(
		const CubicBezier& curve,
		float			   tolerance,
		std::vector<Foundation::Vec2>& output,
		int depth,
		int maxDepth
	) {
		// Base case: max depth reached or curve is flat enough
		if (depth >= maxDepth || isCubicFlatEnough(curve, tolerance)) {
			output.push_back(curve.p3);
			return;
		}

		// De Casteljau subdivision at t=0.5
		// This splits the curve into two smaller curves that together
		// represent the same shape as the original

		// First level: midpoints of each segment
		Foundation::Vec2 m01 = (curve.p0 + curve.p1) * 0.5F;
		Foundation::Vec2 m12 = (curve.p1 + curve.p2) * 0.5F;
		Foundation::Vec2 m23 = (curve.p2 + curve.p3) * 0.5F;

		// Second level: midpoints of the first level
		Foundation::Vec2 m012 = (m01 + m12) * 0.5F;
		Foundation::Vec2 m123 = (m12 + m23) * 0.5F;

		// Third level: the point on the curve at t=0.5
		Foundation::Vec2 mid = (m012 + m123) * 0.5F;

		// Recurse on both halves
		// Left half: p0 → m01 → m012 → mid
		flattenCubicRecursive({curve.p0, m01, m012, mid}, tolerance, output, depth + 1, maxDepth);

		// Right half: mid → m123 → m23 → p3
		flattenCubicRecursive({mid, m123, m23, curve.p3}, tolerance, output, depth + 1, maxDepth);
	}

	/// Internal recursive implementation for quadratic Bezier flattening
	void flattenQuadraticRecursive(
		const QuadraticBezier& curve,
		float				   tolerance,
		std::vector<Foundation::Vec2>& output,
		int depth,
		int maxDepth
	) {
		// Base case: max depth reached or curve is flat enough
		if (depth >= maxDepth || isQuadraticFlatEnough(curve, tolerance)) {
			output.push_back(curve.p2);
			return;
		}

		// De Casteljau subdivision at t=0.5
		Foundation::Vec2 m01 = (curve.p0 + curve.p1) * 0.5F;
		Foundation::Vec2 m12 = (curve.p1 + curve.p2) * 0.5F;
		Foundation::Vec2 mid = (m01 + m12) * 0.5F;

		// Recurse on both halves
		flattenQuadraticRecursive({curve.p0, m01, mid}, tolerance, output, depth + 1, maxDepth);
		flattenQuadraticRecursive({mid, m12, curve.p2}, tolerance, output, depth + 1, maxDepth);
	}

} // anonymous namespace

void flattenCubicBezier(
	const CubicBezier& curve,
	float			   tolerance,
	std::vector<Foundation::Vec2>& output,
	int maxDepth
) {
	flattenCubicRecursive(curve, tolerance, output, 0, maxDepth);
}

void flattenQuadraticBezier(
	const QuadraticBezier& curve,
	float				   tolerance,
	std::vector<Foundation::Vec2>& output,
	int maxDepth
) {
	flattenQuadraticRecursive(curve, tolerance, output, 0, maxDepth);
}

} // namespace renderer
