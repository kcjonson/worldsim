#include "CornerAngles.h"

#include <cmath>
#include <numbers>
#include <vector>

namespace construction {

	namespace {

		constexpr double kCoincidenceMeters = 0.001;
		constexpr double kRadiansPerDegree = std::numbers::pi / 180.0;
		// A path whose doubled area is within this of zero has no inside: its points are collinear.
		constexpr double kCollinearDoubleAreaSquareMeters = 1e-9;

		struct Point {
			double x = 0.0;
			double y = 0.0;
		};

		Point toPoint(::Foundation::Vec2 value) {
			return {static_cast<double>(value.x), static_cast<double>(value.y)};
		}
		Point operator-(Point lhs, Point rhs) {
			return {lhs.x - rhs.x, lhs.y - rhs.y};
		}
		double cross(Point lhs, Point rhs) {
			return lhs.x * rhs.y - lhs.y * rhs.x;
		}
		double dot(Point lhs, Point rhs) {
			return lhs.x * rhs.x + lhs.y * rhs.y;
		}
		double length(Point value) {
			return std::hypot(value.x, value.y);
		}

		::Foundation::Vec2 unitVector(Point value) {
			const double size = length(value);
			if (size <= 0.0) {
				return {0.0F, 0.0F};
			}
			return {static_cast<float>(value.x / size), static_cast<float>(value.y / size)};
		}

		// Whether the path's inside (even-odd) lies along `direction` right at path[index]: the
		// ray from the vertex that way crosses the path's other edges an odd number of times. The
		// ray starts at the vertex because a thin shape's far side can lie closer to it than any
		// fixed distance. An edge end on the ray's line counts as on its negative side, so a ray
		// through a vertex counts the two edges meeting there once between them, or not at all.
		bool insideAlong(const std::vector<Point>& path, std::size_t index, Point direction) {
			const std::size_t count = path.size();
			const Point		  vertex = path[index];
			const std::size_t previous = (index + count - 1) % count;
			bool			  inside = false;
			for (std::size_t start = 0; start < count; ++start) {
				if (start == index || start == previous) {
					continue; // the vertex's own edges meet the ray only at the vertex
				}
				const Point startOffset = path[start] - vertex;
				const Point endOffset = path[(start + 1) % count] - vertex;
				if ((cross(direction, startOffset) > 0.0) == (cross(direction, endOffset) > 0.0)) {
					continue;
				}
				const Point edge = endOffset - startOffset;
				if (cross(startOffset, edge) / cross(direction, edge) > 0.0) {
					inside = !inside;
				}
			}
			return inside;
		}

		// The corner at path[index]. `open` reads the plain angle between the two segments;
		// otherwise the interior angle of the closed path: the sweep from the edge toward the
		// previous vertex to the edge toward the next, the other way round when the inside does
		// not lie along its bisector. A collinear path has no inside and reads the plain angle.
		// False when an edge is under 1 mm.
		bool cornerAt(const std::vector<Point>& path, std::size_t index, bool open, double doubleArea, double squareTolerance, CornerAngle& out) {
			const std::size_t count = path.size();
			const Point		  vertex = path[index];
			const Point		  toPrevious = path[(index + count - 1) % count] - vertex;
			const Point		  toNext = path[(index + 1) % count] - vertex;
			if (length(toPrevious) < kCoincidenceMeters || length(toNext) < kCoincidenceMeters) {
				return false;
			}
			const double previousAngle = std::atan2(toPrevious.y, toPrevious.x);
			double		 sweep = std::atan2(cross(toPrevious, toNext), dot(toPrevious, toNext));
			if (!open && std::abs(doubleArea) > kCollinearDoubleAreaSquareMeters) {
				const double middle = previousAngle + sweep / 2.0;
				if (!insideAlong(path, index, {std::cos(middle), std::sin(middle)})) {
					sweep = sweep > 0.0 ? sweep - 2.0 * std::numbers::pi : sweep + 2.0 * std::numbers::pi;
				}
			}
			const double degrees = std::abs(sweep) / kRadiansPerDegree;
			out.vertexIndex = index;
			out.vertex = {static_cast<float>(vertex.x), static_cast<float>(vertex.y)};
			out.degrees = static_cast<float>(degrees);
			out.square = std::abs(degrees - 90.0) < squareTolerance;
			out.startRadians = static_cast<float>(previousAngle);
			out.sweepRadians = static_cast<float>(sweep);
			out.toPrevious = unitVector(toPrevious);
			out.toNext = unitVector(toNext);
			return true;
		}

	} // namespace

	CornerAngles cornerAngles(const CornerAngleInput& input) {
		CornerAngles result;
		const std::span<const ::Foundation::Vec2> points = input.points;
		if (points.size() < 2) {
			return result;
		}

		std::vector<Point> path;
		path.reserve(points.size() + 1);
		for (const ::Foundation::Vec2 point : points) {
			path.push_back(toPoint(point));
		}
		if (!input.closes) {
			path.push_back(toPoint(input.cursor));
		}

		const bool nearFirst = points.size() >= 3
			&& (input.closes || length(toPoint(input.cursor) - path.front()) <= static_cast<double>(input.closingReachMeters));
		const bool asPolygon = path.size() >= 3 && (input.tool == AngleReadoutTool::Foundation || input.closes);
		double	   doubleArea = 0.0;
		if (asPolygon) {
			for (std::size_t index = 0; index < path.size(); ++index) {
				doubleArea += cross(path[index], path[(index + 1) % path.size()]);
			}
		}
		const double squareTolerance = static_cast<double>(input.squareToleranceDegrees);

		CornerAngle previous;
		if (cornerAt(path, points.size() - 1, !asPolygon, doubleArea, squareTolerance, previous)) {
			previous.role = CornerRole::Previous;
			result.corners[result.count++] = previous;
		}
		if (nearFirst && asPolygon) {
			CornerAngle closing;
			if (cornerAt(path, 0, false, doubleArea, squareTolerance, closing)) {
				closing.role = CornerRole::Closing;
				result.corners[result.count++] = closing;
			}
		}
		return result;
	}

} // namespace construction
