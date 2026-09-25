#include "MarchingSquares.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace geometry {

	namespace {

		// Cell edges in counter-clockwise walk order around the cell. Edge k runs
		// from corner k to corner k+1, corners being 0 (x,y), 1 (x+1,y),
		// 2 (x+1,y+1), 3 (x,y+1).
		enum CellEdge : int {
			kBottom = 0,
			kRight	= 1,
			kTop	= 2,
			kLeft	= 3,
		};

		struct Segment {
			int startEdge = 0;
			int endEdge	  = 0;
		};

		struct CellSegments {
			int					   count = 0;
			std::array<Segment, 2> segments{};
		};

		// The directed contour pieces of one cell, inside on the left. Walking the
		// cell boundary counter-clockwise, a piece starts at an edge that goes from
		// inside to outside and ends at an edge that goes from outside to inside.
		// A separated saddle pairs each start with the preceding end (each inside
		// corner is cut off alone); a connected saddle pairs it with the following
		// end (each outside corner is cut off alone). With one start the two
		// searches agree.
		CellSegments cellSegments(const ScalarField& field, int x, int y, float iso) {
			const std::array<float, 4> v = {field.at(x, y), field.at(x + 1, y), field.at(x + 1, y + 1), field.at(x, y + 1)};
			std::array<bool, 4>		   in{};
			for (int k = 0; k < 4; ++k) {
				in[static_cast<std::size_t>(k)] = v[static_cast<std::size_t>(k)] >= iso;
			}
			auto inside		= [&](int k) { return in[static_cast<std::size_t>(k & 3)]; };
			auto startsHere = [&](int k) { return inside(k) && !inside(k + 1); };
			auto endsHere	= [&](int k) { return !inside(k) && inside(k + 1); };

			const bool saddle = in[0] == in[2] && in[1] == in[3] && in[0] != in[1];
			bool	   connected = false;
			if (saddle) {
				const double average = (static_cast<double>(v[0]) + static_cast<double>(v[1]) + static_cast<double>(v[2]) +
										static_cast<double>(v[3])) /
									   4.0;
				connected = average >= static_cast<double>(iso);
			}

			CellSegments out;
			for (int k = 0; k < 4; ++k) {
				if (!startsHere(k)) {
					continue;
				}
				for (int step = 1; step < 4; ++step) {
					const int candidate = connected ? (k + step) & 3 : (k - step) & 3;
					if (endsHere(candidate)) {
						out.segments[static_cast<std::size_t>(out.count++)] = {k, candidate};
						break;
					}
				}
			}
			return out;
		}

		// Interpolated crossing on the lattice edge between sample (x0,y0) and
		// (x1,y1), which differ by one step in +x or +y. The lower endpoint is
		// always the reference, so the result depends only on the edge's world
		// position and its two values, never on the field origin or on which cell
		// asks. The rounded offset is kept at least 1 mm from both endpoints: every
		// crossing then lies strictly inside its own edge, so no two crossings
		// coincide and no segment touches a sample, which is what keeps the output
		// simple when a sample sits within rounding distance of iso.
		Vec2i64 crossing(const ScalarField& field, int x0, int y0, int x1, int y1, float iso) {
			const double	   a	  = static_cast<double>(field.at(x0, y0));
			const double	   b	  = static_cast<double>(field.at(x1, y1));
			const double	   t	  = (static_cast<double>(iso) - a) / (b - a);
			const std::int64_t offset = std::clamp<std::int64_t>(
				std::llround(t * static_cast<double>(field.cellMm)), 1, field.cellMm - 1
			);
			const Vec2i64 p = field.samplePositionMm(x0, y0);
			if (x1 != x0) {
				return {p.x + offset, p.y};
			}
			return {p.x, p.y + offset};
		}

		Vec2i64 edgeCrossing(const ScalarField& field, int x, int y, int edge, float iso) {
			switch (edge) {
				case kBottom:
					return crossing(field, x, y, x + 1, y, iso);
				case kRight:
					return crossing(field, x + 1, y, x + 1, y + 1, iso);
				case kTop:
					return crossing(field, x, y + 1, x + 1, y + 1, iso);
				default:
					return crossing(field, x, y, x, y + 1, iso);
			}
		}

		bool borderBelowIso(const ScalarField& field, float iso) {
			for (int x = 0; x < field.width; ++x) {
				if (field.at(x, 0) >= iso || field.at(x, field.height - 1) >= iso) {
					return false;
				}
			}
			for (int y = 0; y < field.height; ++y) {
				if (field.at(0, y) >= iso || field.at(field.width - 1, y) >= iso) {
					return false;
				}
			}
			return true;
		}

	} // namespace

	std::vector<Ring> marchingSquares(const ScalarField& field, float iso) {
		std::vector<Ring> loops;
		if (field.width < 2 || field.height < 2) {
			return loops;
		}
		assert(field.cellMm >= 2);
		const bool closes = borderBelowIso(field, iso);
		assert(closes && "marchingSquares: every border sample must be below iso");
		if (!closes) {
			return loops;
		}

		const int cellsX = field.width - 1;
		const int cellsY = field.height - 1;
		// Per cell, bit s set once segment s has been walked.
		std::vector<std::uint8_t> visited(static_cast<std::size_t>(cellsX) * static_cast<std::size_t>(cellsY), 0);
		auto cellIndex = [cellsX](int x, int y) {
			return static_cast<std::size_t>(y) * static_cast<std::size_t>(cellsX) + static_cast<std::size_t>(x);
		};

		// Nearly every cell of a terrain field lies wholly on one side of iso; test
		// its corners straight off the samples before building segments.
		const float* samples = field.values.data();
		const auto	 width	 = static_cast<std::size_t>(field.width);
		for (int y = 0; y < cellsY; ++y) {
			const float* row0 = samples + static_cast<std::size_t>(y) * width;
			const float* row1 = row0 + width;
			for (int x = 0; x < cellsX; ++x) {
				const bool in00 = row0[x] >= iso;
				if (in00 == (row0[x + 1] >= iso) && in00 == (row1[x] >= iso) && in00 == (row1[x + 1] >= iso)) {
					continue;
				}
				const CellSegments startCell = cellSegments(field, x, y, iso);
				for (int s = 0; s < startCell.count; ++s) {
					if ((visited[cellIndex(x, y)] & (1U << s)) != 0) {
						continue;
					}

					Ring loop;
					int	 cx	 = x;
					int	 cy	 = y;
					int	 seg = s;
					CellSegments cur = startCell;
					while ((visited[cellIndex(cx, cy)] & (1U << seg)) == 0) {
						visited[cellIndex(cx, cy)] |= static_cast<std::uint8_t>(1U << seg);
						const Segment& piece = cur.segments[static_cast<std::size_t>(seg)];
						loop.push_back(edgeCrossing(field, cx, cy, piece.startEdge, iso));

						// Step across the exit edge; the neighbor's piece starts there.
						int entryEdge = 0;
						switch (piece.endEdge) {
							case kBottom:
								--cy;
								entryEdge = kTop;
								break;
							case kRight:
								++cx;
								entryEdge = kLeft;
								break;
							case kTop:
								++cy;
								entryEdge = kBottom;
								break;
							default:
								--cx;
								entryEdge = kRight;
								break;
						}
						cur = cellSegments(field, cx, cy, iso);
						seg = cur.segments[0].startEdge == entryEdge ? 0 : 1;
						assert(cur.segments[static_cast<std::size_t>(seg)].startEdge == entryEdge);
					}

					// The smallest loop circles one sample: four crossings.
					assert(loop.size() >= 4);
					loops.push_back(std::move(loop));
				}
			}
		}
		return loops;
	}

} // namespace geometry
