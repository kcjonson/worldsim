#pragma once

// Buildings for visibility tests and benches: a grid of rectangular rooms of mixed size, walls
// 0.2 m thick on the grid lines with a 1 m door in every inner wall and in some outer ones, turned
// by an angle and moved to an origin. Wall faces are the occluders vision would pass in.

#include "../core/Vec2i64.h"
#include "../predicates/Predicates.h"
#include "Visibility.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

namespace geometry::buildings {

	constexpr std::int64_t kWallThicknessMm = 200;
	constexpr std::int64_t kDoorWidthMm		= 1000;
	constexpr std::size_t  kOutside			= static_cast<std::size_t>(-1);

	struct Door {
		std::size_t room  = 0;
		std::size_t other = kOutside; // the room through the door, or kOutside
		Vec2i64		centre;
		Vec2i64		outward; // for an outer door: a point 1 m outside it
	};

	struct Building {
		std::vector<OccluderSegment>		walls;
		std::vector<std::array<Vec2i64, 4>> pieces; // each wall piece's corners, counter-clockwise
		std::vector<Vec2i64>				roomCentres;
		std::vector<Door>					doors;
		std::vector<Vec2i64>				corners; // the building's outer corners, counter-clockwise
		double								angle = 0.0;
		Vec2i64								origin;

		Vec2i64 place(double x, double y) const {
			const double cosine = std::cos(angle);
			const double sine	= std::sin(angle);
			return {origin.x + std::llround(cosine * x - sine * y), origin.y + std::llround(sine * x + cosine * y)};
		}

		bool insideWall(const Vec2i64& point) const {
			for (const std::array<Vec2i64, 4>& piece : pieces) {
				if (pointInPolygon(point, std::vector<Vec2i64>(piece.begin(), piece.end())) != PointInPolygon::Outside) {
					return true;
				}
			}
			return false;
		}

		// The distance from a point to the building's outline, zero inside it.
		double distanceTo(const Vec2i64& point) const {
			if (pointInPolygon(point, corners) != PointInPolygon::Outside) {
				return 0.0;
			}
			double nearest = 1e300;
			for (std::size_t i = 0; i < corners.size(); ++i) {
				nearest = std::min(nearest, distanceToSegment(point, corners[i], corners[(i + 1) % corners.size()]));
			}
			return nearest;
		}
	};

	// Rooms columnWidths x rowHeights (millimetres, wall centre to wall centre), from local (0, 0).
	// Every inner wall gets a door at a random place along it; each outer wall of a room gets one
	// with probability outerDoorChance.
	inline Building makeBuilding(const std::vector<std::int64_t>& columnWidths, const std::vector<std::int64_t>& rowHeights, double angle, const Vec2i64& origin,
								 double outerDoorChance, std::mt19937& rng) {
		Building building;
		building.angle	= angle;
		building.origin = origin;
		std::vector<std::int64_t> columnAt{0};
		for (const std::int64_t width : columnWidths) {
			columnAt.push_back(columnAt.back() + width);
		}
		std::vector<std::int64_t> rowAt{0};
		for (const std::int64_t height : rowHeights) {
			rowAt.push_back(rowAt.back() + height);
		}
		const std::size_t columns = columnWidths.size();
		const std::size_t rows	  = rowHeights.size();
		const std::int64_t half	  = kWallThicknessMm / 2;

		auto addPiece = [&](double x0, double y0, double x1, double y1) {
			const std::array<Vec2i64, 4> corners{building.place(x0, y0), building.place(x1, y0), building.place(x1, y1), building.place(x0, y1)};
			building.pieces.push_back(corners);
			for (std::size_t i = 0; i < 4; ++i) {
				building.walls.push_back({corners[i], corners[(i + 1) % 4]});
			}
		};
		// A door's centre along a wall from `from` to `to`, at least 0.4 m clear of either end.
		auto doorAlong = [&](std::int64_t from, std::int64_t to) {
			const std::int64_t room = (to - from) - 2 * (half + 400) - kDoorWidthMm;
			const std::int64_t at	= from + half + 400 + kDoorWidthMm / 2 + (room > 0 ? static_cast<std::int64_t>(rng() % static_cast<std::uint32_t>(room + 1)) : 0);
			return at;
		};
		auto roomIndex = [&](std::size_t column, std::size_t row) { return row * columns + column; };
		for (std::size_t row = 0; row < rows; ++row) {
			for (std::size_t column = 0; column < columns; ++column) {
				building.roomCentres.push_back(
					building.place(static_cast<double>(columnAt[column] + columnAt[column + 1]) / 2.0, static_cast<double>(rowAt[row] + rowAt[row + 1]) / 2.0));
			}
		}

		// Vertical walls, x = columnAt[i], each room-high span with an optional door.
		for (std::size_t line = 0; line <= columns; ++line) {
			const double x = static_cast<double>(columnAt[line]);
			for (std::size_t row = 0; row < rows; ++row) {
				const bool outer	= line == 0 || line == columns;
				const bool withDoor = !outer || std::uniform_real_distribution<double>(0.0, 1.0)(rng) < outerDoorChance;
				const double y0		= static_cast<double>(rowAt[row] - half);
				const double y1		= static_cast<double>(rowAt[row + 1] + half);
				if (!withDoor) {
					addPiece(x - half, y0, x + half, y1);
					continue;
				}
				const std::int64_t at = doorAlong(rowAt[row], rowAt[row + 1]);
				addPiece(x - half, y0, x + half, static_cast<double>(at - kDoorWidthMm / 2));
				addPiece(x - half, static_cast<double>(at + kDoorWidthMm / 2), x + half, y1);
				const Vec2i64 centre = building.place(x, static_cast<double>(at));
				if (outer) {
					const double outwardX = line == 0 ? x - 1000.0 : x + 1000.0;
					building.doors.push_back({roomIndex(line == 0 ? 0 : columns - 1, row), kOutside, centre, building.place(outwardX, static_cast<double>(at))});
				} else {
					building.doors.push_back({roomIndex(line - 1, row), roomIndex(line, row), centre, centre});
				}
			}
		}
		// Horizontal walls, y = rowAt[j].
		for (std::size_t line = 0; line <= rows; ++line) {
			const double y = static_cast<double>(rowAt[line]);
			for (std::size_t column = 0; column < columns; ++column) {
				const bool outer	= line == 0 || line == rows;
				const bool withDoor = !outer || std::uniform_real_distribution<double>(0.0, 1.0)(rng) < outerDoorChance;
				const double x0		= static_cast<double>(columnAt[column] - half);
				const double x1		= static_cast<double>(columnAt[column + 1] + half);
				if (!withDoor) {
					addPiece(x0, y - half, x1, y + half);
					continue;
				}
				const std::int64_t at = doorAlong(columnAt[column], columnAt[column + 1]);
				addPiece(x0, y - half, static_cast<double>(at - kDoorWidthMm / 2), y + half);
				addPiece(static_cast<double>(at + kDoorWidthMm / 2), y - half, x1, y + half);
				const Vec2i64 centre = building.place(static_cast<double>(at), y);
				if (outer) {
					const double outwardY = line == 0 ? y - 1000.0 : y + 1000.0;
					building.doors.push_back({roomIndex(column, line == 0 ? 0 : rows - 1), kOutside, centre, building.place(static_cast<double>(at), outwardY)});
				} else {
					building.doors.push_back({roomIndex(column, line - 1), roomIndex(column, line), centre, centre});
				}
			}
		}
		const double right = static_cast<double>(columnAt.back() + half);
		const double top   = static_cast<double>(rowAt.back() + half);
		building.corners   = {building.place(-half, -half), building.place(right, -half), building.place(right, top), building.place(-half, top)};
		return building;
	}

	// A random building: 2 to 4 columns and 1 to 3 rows of rooms 3 to 9 m across, at a random
	// angle and an origin up to 40 km out.
	inline Building randomBuilding(std::mt19937& rng) {
		std::vector<std::int64_t> widths(2 + rng() % 3);
		std::vector<std::int64_t> heights(1 + rng() % 3);
		for (std::int64_t& width : widths) {
			width = 3000 + static_cast<std::int64_t>(rng() % 6001);
		}
		for (std::int64_t& height : heights) {
			height = 3000 + static_cast<std::int64_t>(rng() % 6001);
		}
		const double	   angle   = rng() % 4 == 0 ? 0.0 : std::uniform_real_distribution<double>(0.0, 6.283185307179586)(rng);
		const std::int64_t originX = static_cast<std::int64_t>(rng() % 80000001) - 40000000;
		const std::int64_t originY = static_cast<std::int64_t>(rng() % 80000001) - 40000000;
		return makeBuilding(widths, heights, angle, {originX, originY}, 0.5, rng);
	}

	// Observers a colonist could stand at: anywhere in or round the building, near a wall corner
	// (1 to 300 mm off it), or in or by a doorway; never inside a wall.
	inline std::vector<Vec2i64> observersFor(const Building& building, std::mt19937& rng, std::size_t count) {
		std::vector<Vec2i64> out;
		Vec2i64				 low  = building.corners.front();
		Vec2i64				 high = low;
		for (const Vec2i64& corner : building.corners) {
			low	 = {std::min(low.x, corner.x), std::min(low.y, corner.y)};
			high = {std::max(high.x, corner.x), std::max(high.y, corner.y)};
		}
		std::size_t attempts = 0;
		while (out.size() < count && attempts < count * 100) {
			++attempts;
			Vec2i64			   candidate;
			const std::uint32_t kind = rng() % 3;
			if (kind == 0) {
				const std::int64_t spanX = high.x - low.x + 20000;
				const std::int64_t spanY = high.y - low.y + 20000;
				const std::int64_t x	 = low.x - 10000 + static_cast<std::int64_t>(rng() % static_cast<std::uint32_t>(spanX));
				const std::int64_t y	 = low.y - 10000 + static_cast<std::int64_t>(rng() % static_cast<std::uint32_t>(spanY));
				candidate				 = {x, y};
			} else if (kind == 1) {
				const std::array<Vec2i64, 4>& piece	 = building.pieces[rng() % building.pieces.size()];
				const Vec2i64&				  corner = piece[rng() % 4];
				const std::int64_t			  x		 = static_cast<std::int64_t>(rng() % 601) - 300;
				const std::int64_t			  y		 = static_cast<std::int64_t>(rng() % 601) - 300;
				candidate							 = corner + Vec2i64{x, y};
			} else {
				const Door&		   door = building.doors[rng() % building.doors.size()];
				const std::int64_t x	= static_cast<std::int64_t>(rng() % 2001) - 1000;
				const std::int64_t y	= static_cast<std::int64_t>(rng() % 2001) - 1000;
				candidate				= door.centre + Vec2i64{x, y};
			}
			if (!building.insideWall(candidate)) {
				out.push_back(candidate);
			}
		}
		return out;
	}

} // namespace geometry::buildings
