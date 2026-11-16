#include "CornerAngles.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

namespace construction {

	namespace {

		constexpr double kCoincidenceMeters = 0.001;
		constexpr double kRadiansPerDegree = std::numbers::pi / 180.0;
		constexpr double kProbeFraction = 0.01;

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

		bool pointInPolygon(Point point, const std::vector<Point>& ring) {
			bool inside = false;
			for (std::size_t index = 0, previous = ring.size() - 1; index < ring.size(); previous = index++) {
				const Point current = ring[index];
				const Point before = ring[previous];
				const bool	crosses = (current.y > point.y) != (before.y > point.y) &&
					point.x < (before.x - current.x) * (point.y - current.y) / (before.y - current.y) + current.x;
				if (crosses) {
					inside = !inside;
				}
			}
			return inside;
		}

		::Foundation::Vec2 unitVector(Point value) {
			const double size = length(value);
			if (size <= 0.0) {
				return {0.0F, 0.0F};
			}
			return {static_cast<float>(value.x / size), static_cast<float>(value.y / size)};
		}

		// The corner at path[index]. `open` reads the plain angle between the two segments;
		// otherwise the interior angle of the closed path. False when an edge is under 1 mm.
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
			if (!open && std::abs(doubleArea) > 1e-9) {
				const double middle = previousAngle + sweep / 2.0;
				const double reach = std::min(length(toPrevious), length(toNext)) * kProbeFraction;
				const Point	 probe{vertex.x + std::cos(middle) * reach, vertex.y + std::sin(middle) * reach};
				if (!pointInPolygon(probe, path)) {
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
