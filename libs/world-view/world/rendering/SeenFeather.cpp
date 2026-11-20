#include "SeenFeather.h"

#include <predicates/Predicates.h>

#include <primitives/Primitives.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <numbers>
#include <numeric>

namespace worldview {

	using geometry::Vec2i64;

	namespace {

		// How far off a piece's middle its outer side is read.
		constexpr double kProbeMm = 2.0;
		// The side of a cell of the cut's lookup grid.
		constexpr std::int64_t kCutGridMm = 4000;
		// A cell not drawn for this many frames is let go; it is built again when it comes back into view.
		constexpr std::uint64_t kCellKeptFrames = 240;
		constexpr double		kFanStep = kFeatherFanStepDegrees * std::numbers::pi / 180.0;

		glm::dvec2 toDouble(const Vec2i64& point) {
			return {static_cast<double>(point.x), static_cast<double>(point.y)};
		}

		Vec2i64 rounded(glm::dvec2 point) {
			return {std::llround(point.x), std::llround(point.y)};
		}

		std::int64_t cellOf(std::int64_t millimetres, std::int64_t size) {
			const std::int64_t cell = millimetres / size;
			return millimetres % size < 0 ? cell - 1 : cell;
		}

		bool boxesMeet(const Vec2i64& firstLow, const Vec2i64& firstHigh, const Vec2i64& secondLow, const Vec2i64& secondHigh) {
			return firstLow.x <= secondHigh.x && secondLow.x <= firstHigh.x && firstLow.y <= secondHigh.y && secondLow.y <= firstHigh.y;
		}

		void boxOf(const std::vector<Vec2i64>& ring, Vec2i64& low, Vec2i64& high) {
			low = ring.front();
			high = ring.front();
			for (const Vec2i64& point : ring) {
				low = {std::min(low.x, point.x), std::min(low.y, point.y)};
				high = {std::max(high.x, point.x), std::max(high.y, point.y)};
			}
		}

		bool counterClockwise(const std::vector<Vec2i64>& ring) {
			double doubled = 0.0;
			for (std::size_t k = 0; k < ring.size(); ++k) {
				doubled += geometry::cross(ring[k], ring[(k + 1) % ring.size()]).toDouble();
			}
			return doubled >= 0.0;
		}

		bool convexRing(const std::vector<Vec2i64>& ring) {
			for (std::size_t k = 0; k < ring.size(); ++k) {
				const Vec2i64& previous = ring[(k + ring.size() - 1) % ring.size()];
				const Vec2i64& next = ring[(k + 1) % ring.size()];
				if (geometry::cross(ring[k] - previous, next - ring[k]).sign() < 0) {
					return false;
				}
			}
			return true;
		}

		// A segment of the cut: its ends, whose it is (-1 the record's), and where along it it is split.
		struct Segment {
			Vec2i64				 start;
			Vec2i64				 end;
			int					 owner = -1;
			std::vector<double>	 splits;
			std::vector<Vec2i64> splitPoints;
		};

		// Whether `point` lies strictly inside the segment from `from` to `to`, on its line between its ends.
		bool strictlyWithin(const Vec2i64& point, const Vec2i64& from, const Vec2i64& to) {
			if (point == from || point == to || geometry::cross(to - from, point - from).sign() != 0) {
				return false;
			}
			const geometry::Int128 along = geometry::dot(point - from, to - from);
			return along.sign() > 0 && along < geometry::dot(to - from, to - from);
		}

		double alongOf(const Vec2i64& point, const Segment& segment) {
			const glm::dvec2 along = toDouble(segment.end) - toDouble(segment.start);
			return glm::dot(toDouble(point) - toDouble(segment.start), along) / glm::dot(along, along);
		}

		void split(Segment& segment, const Vec2i64& point) {
			if (point == segment.start || point == segment.end) {
				return;
			}
			segment.splits.push_back(alongOf(point, segment));
			segment.splitPoints.push_back(point);
		}

		void meet(Segment& first, Segment& second) {
			const int startSide = geometry::cross(first.end - first.start, second.start - first.start).sign();
			const int endSide = geometry::cross(first.end - first.start, second.end - first.start).sign();
			const int otherStart = geometry::cross(second.end - second.start, first.start - second.start).sign();
			const int otherEnd = geometry::cross(second.end - second.start, first.end - second.start).sign();
			if (startSide * endSide < 0 && otherStart * otherEnd < 0) {
				const glm::dvec2 along = toDouble(first.end) - toDouble(first.start);
				const glm::dvec2 otherAlong = toDouble(second.end) - toDouble(second.start);
				const glm::dvec2 offset = toDouble(second.start) - toDouble(first.start);
				const double	 share =
					(offset.x * otherAlong.y - offset.y * otherAlong.x) / (along.x * otherAlong.y - along.y * otherAlong.x);
				const Vec2i64 crossing = rounded(toDouble(first.start) + share * along);
				split(first, crossing);
				split(second, crossing);
				return;
			}
			for (const Vec2i64& end : {second.start, second.end}) {
				if (strictlyWithin(end, first.start, first.end)) {
					split(first, end);
				}
			}
			for (const Vec2i64& end : {first.start, first.end}) {
				if (strictlyWithin(end, second.start, second.end)) {
					split(second, end);
				}
			}
		}

		// The point just off the middle of the piece on its right, its outer side.
		Vec2i64 outerProbe(const Vec2i64& start, const Vec2i64& end) {
			const glm::dvec2 from = toDouble(start);
			const glm::dvec2 along = toDouble(end) - from;
			const double	 length = glm::length(along);
			return rounded(from + along / 2.0 + glm::dvec2{along.y, -along.x} / length * kProbeMm);
		}

		// The box round what changed between a shape's last cut and now, the corners either has that the
		// other lacks and the sides at them, grown a little past the cut's probe.
		void
		appendChangedBox(const std::vector<Vec2i64>& before, const std::vector<Vec2i64>& after, std::vector<vision::SeenArea::Box>& out) {
			constexpr std::int64_t kChangedMarginMm = 10;
			bool				   any = false;
			Vec2i64				   low{0, 0};
			Vec2i64				   high{0, 0};
			const auto			   take = [&](const Vec2i64& point) {
				low = any ? Vec2i64{std::min(low.x, point.x), std::min(low.y, point.y)} : point;
				high = any ? Vec2i64{std::max(high.x, point.x), std::max(high.y, point.y)} : point;
				any = true;
			};
			for (const auto& [ring, other] : {std::pair{&before, &after}, std::pair{&after, &before}}) {
				for (std::size_t corner = 0; corner < ring->size(); ++corner) {
					if (std::ranges::find(*other, (*ring)[corner]) == other->end()) {
						take((*ring)[corner]);
						take((*ring)[(corner + 1) % ring->size()]);
						take((*ring)[(corner + ring->size() - 1) % ring->size()]);
					}
				}
			}
			if (any) {
				out.push_back({low - Vec2i64{kChangedMarginMm, kChangedMarginMm}, high + Vec2i64{kChangedMarginMm, kChangedMarginMm}});
			}
		}

	} // namespace

	void cutSeenBoundary(
		std::span<const std::vector<Vec2i64>* const> shapes,
		std::span<const DirectedEdge>				 recordEdges,
		const std::function<bool(const Vec2i64&)>&	 inRecord,
		std::span<const vision::SeenArea::Box>		 region,
		std::vector<DirectedEdge>&					 out,
		SeenCutCounts&								 counts
	) {
		const auto inRegion = [&region](const Vec2i64& from, const Vec2i64& to) {
			if (region.empty()) {
				return true;
			}
			const Vec2i64 low{std::min(from.x, to.x), std::min(from.y, to.y)};
			const Vec2i64 high{std::max(from.x, to.x), std::max(from.y, to.y)};
			return std::ranges::any_of(region, [&](const vision::SeenArea::Box& box) { return boxesMeet(low, high, box.low, box.high); });
		};
		std::vector<std::vector<Vec2i64>> rings;
		std::vector<Vec2i64>			  lows;
		std::vector<Vec2i64>			  highs;
		rings.reserve(shapes.size());
		std::vector<Segment> segments;
		for (const DirectedEdge& edge : recordEdges) {
			if (edge.first != edge.second) {
				segments.push_back({.start = edge.first, .end = edge.second, .owner = -1});
			}
		}
		counts.recordEdges += segments.size();
		for (const std::vector<Vec2i64>* shape : shapes) {
			if (shape->size() < 3) {
				continue;
			}
			std::vector<Vec2i64> ring = *shape;
			if (!counterClockwise(ring)) {
				std::ranges::reverse(ring);
			}
			const int owner = static_cast<int>(rings.size());
			for (std::size_t k = 0; k < ring.size(); ++k) {
				const Vec2i64& to = ring[(k + 1) % ring.size()];
				if (ring[k] != to) {
					segments.push_back({.start = ring[k], .end = to, .owner = owner});
					++counts.shapeSides;
				}
			}
			Vec2i64 low;
			Vec2i64 high;
			boxOf(ring, low, high);
			lows.push_back(low);
			highs.push_back(high);
			rings.push_back(std::move(ring));
		}
		counts.shapes += rings.size();
		if (rings.empty()) {
			return;
		}

		// With a region, only the segments that can split a piece meeting it are cut: those meeting it, and
		// those meeting the box round them, where their crossings lie.
		std::vector<bool> wanted(segments.size(), region.empty());
		if (!region.empty()) {
			bool	any = false;
			Vec2i64 around{0, 0};
			Vec2i64 aroundHigh{0, 0};
			for (const Segment& segment : segments) {
				if (inRegion(segment.start, segment.end)) {
					const Vec2i64 low{std::min(segment.start.x, segment.end.x), std::min(segment.start.y, segment.end.y)};
					const Vec2i64 high{std::max(segment.start.x, segment.end.x), std::max(segment.start.y, segment.end.y)};
					around = any ? Vec2i64{std::min(around.x, low.x), std::min(around.y, low.y)} : low;
					aroundHigh = any ? Vec2i64{std::max(aroundHigh.x, high.x), std::max(aroundHigh.y, high.y)} : high;
					any = true;
				}
			}
			for (std::size_t index = 0; any && index < segments.size(); ++index) {
				const Segment& segment = segments[index];
				const Vec2i64  low{std::min(segment.start.x, segment.end.x), std::min(segment.start.y, segment.end.y)};
				const Vec2i64  high{std::max(segment.start.x, segment.end.x), std::max(segment.start.y, segment.end.y)};
				wanted[index] = boxesMeet(low, high, around, aroundHigh);
			}
		}

		// Every pair of segments of different owners that can meet, found through a grid of their boxes.
		std::unordered_map<std::uint64_t, std::vector<std::size_t>> grid;
		const auto													keyOf = [](std::int64_t x, std::int64_t y) {
			 return (static_cast<std::uint64_t>(x) << 32U) ^ (static_cast<std::uint64_t>(y) & 0xFFFFFFFFULL);
		};
		for (std::size_t index = 0; index < segments.size(); ++index) {
			if (!wanted[index]) {
				continue;
			}
			const Segment& segment = segments[index];
			for (std::int64_t y = cellOf(std::min(segment.start.y, segment.end.y), kCutGridMm);
				 y <= cellOf(std::max(segment.start.y, segment.end.y), kCutGridMm);
				 ++y) {
				for (std::int64_t x = cellOf(std::min(segment.start.x, segment.end.x), kCutGridMm);
					 x <= cellOf(std::max(segment.start.x, segment.end.x), kCutGridMm);
					 ++x) {
					grid[keyOf(x, y)].push_back(index);
				}
			}
		}
		std::vector<std::pair<std::size_t, std::size_t>> pairs;
		for (const auto& [cell, members] : grid) {
			for (std::size_t first = 0; first < members.size(); ++first) {
				for (std::size_t second = first + 1; second < members.size(); ++second) {
					const std::size_t one = std::min(members[first], members[second]);
					const std::size_t two = std::max(members[first], members[second]);
					if (segments[one].owner != segments[two].owner) {
						pairs.emplace_back(one, two);
					}
				}
			}
		}
		std::ranges::sort(pairs);
		pairs.erase(std::unique(pairs.begin(), pairs.end()), pairs.end());
		for (const auto& [one, two] : pairs) {
			meet(segments[one], segments[two]);
		}

		// Whether a point lies in a shape other than `except`, a side counting as in: by the sides a ray
		// from it toward +x crosses, read from rows of kCutGridMm, so a probe reads the sides of its row
		// and not every side of every shape in the cluster.
		std::unordered_map<std::int64_t, std::vector<std::size_t>> rows;
		for (std::size_t index = 0; index < segments.size(); ++index) {
			const Segment& segment = segments[index];
			if (segment.owner < 0) {
				continue;
			}
			for (std::int64_t row = cellOf(std::min(segment.start.y, segment.end.y), kCutGridMm);
				 row <= cellOf(std::max(segment.start.y, segment.end.y), kCutGridMm);
				 ++row) {
				rows[row].push_back(index);
			}
		}
		std::vector<std::uint8_t> parity(rings.size(), 0);
		const auto				  insideShape = [&](const Vec2i64& point, int except) {
			   const auto found = rows.find(cellOf(point.y, kCutGridMm));
			   if (found == rows.end()) {
				   return false;
			   }
			   std::ranges::fill(parity, std::uint8_t{0});
			   for (const std::size_t index : found->second) {
				   const Segment& segment = segments[index];
				   if (segment.owner == except) {
					   continue;
				   }
				   const int side = geometry::cross(segment.end - segment.start, point - segment.start).sign();
				   if (side == 0 && (point == segment.start || point == segment.end || strictlyWithin(point, segment.start, segment.end))) {
					   return true;
				   }
				   const bool upward = segment.start.y <= point.y && segment.end.y > point.y;
				   const bool downward = segment.end.y <= point.y && segment.start.y > point.y;
				   if ((upward && side > 0) || (downward && side < 0)) {
					   parity[static_cast<std::size_t>(segment.owner)] ^= 1U;
				   }
			   }
			   return std::ranges::any_of(parity, [](std::uint8_t odd) { return odd != 0U; });
		};
		for (std::size_t index = 0; index < segments.size(); ++index) {
			if (!wanted[index]) {
				continue;
			}
			Segment&				 segment = segments[index];
			std::vector<std::size_t> order(segment.splits.size());
			std::iota(order.begin(), order.end(), std::size_t{0});
			std::ranges::sort(order, [&segment](std::size_t first, std::size_t second) {
				return segment.splits[first] < segment.splits[second];
			});
			Vec2i64 from = segment.start;
			for (std::size_t piece = 0; piece <= order.size(); ++piece) {
				const Vec2i64 to = piece < order.size() ? segment.splitPoints[order[piece]] : segment.end;
				if (to == from || !inRegion(from, to)) {
					from = to;
					continue;
				}
				const Vec2i64 probe = outerProbe(from, to);
				const bool	  kept = segment.owner < 0 ? !insideShape(probe, -1) : !inRecord(probe) && !insideShape(probe, segment.owner);
				if (kept) {
					out.emplace_back(from, to);
					++counts.kept;
				}
				from = to;
			}
		}
	}

	void appendGrownByFeather(const std::vector<Vec2i64>& shape, double featherMm, std::vector<std::vector<glm::dvec2>>& out) {
		if (shape.size() < 3) {
			return;
		}
		std::vector<Vec2i64> ring = shape;
		if (!counterClockwise(ring)) {
			std::ranges::reverse(ring);
		}
		const std::size_t count = ring.size();
		const auto		  outward = [&](std::size_t side) {
			   const glm::dvec2 along = toDouble(ring[(side + 1) % count]) - toDouble(ring[side]);
			   const double		length = glm::length(along);
			   return length > 0.0 ? glm::dvec2{along.y, -along.x} / length : glm::dvec2{0.0};
		};
		// The arc round corner k from the side before it's outward normal to the side after's.
		const auto appendArc = [&](std::size_t corner, std::vector<glm::dvec2>& into) {
			const glm::dvec2 from = outward((corner + count - 1) % count);
			const glm::dvec2 to = outward(corner);
			const double	 sweep = std::atan2(from.x * to.y - from.y * to.x, glm::dot(from, to));
			const auto		 steps = std::max(1, static_cast<int>(std::ceil(std::abs(sweep) / kFanStep)));
			for (int step = 0; step <= steps; ++step) {
				const double	 angle = sweep * static_cast<double>(step) / static_cast<double>(steps);
				const glm::dvec2 turn{
					from.x * std::cos(angle) - from.y * std::sin(angle), from.x * std::sin(angle) + from.y * std::cos(angle)
				};
				into.push_back(toDouble(ring[corner]) + featherMm * turn);
			}
		};
		if (convexRing(ring)) {
			std::vector<glm::dvec2> grownRing;
			for (std::size_t corner = 0; corner < count; ++corner) {
				appendArc(corner, grownRing);
			}
			out.push_back(std::move(grownRing));
			return;
		}
		std::vector<glm::dvec2> outline;
		for (const Vec2i64& point : ring) {
			outline.push_back(toDouble(point));
		}
		out.push_back(std::move(outline));
		for (std::size_t side = 0; side < count; ++side) {
			const glm::dvec2 from = toDouble(ring[side]);
			const glm::dvec2 to = toDouble(ring[(side + 1) % count]);
			const glm::dvec2 offset = featherMm * outward(side);
			out.push_back({from, to, to + offset, from + offset});
			const Vec2i64& previous = ring[(side + count - 1) % count];
			if (geometry::cross(ring[side] - previous, ring[(side + 1) % count] - ring[side]).sign() > 0) {
				std::vector<glm::dvec2> fan{from};
				appendArc(side, fan);
				out.push_back(std::move(fan));
			}
		}
	}

	void SeenFeather::buildCell(const vision::SeenArea& seen, std::int64_t cellX, std::int64_t cellY, Cell& cell) {
		cell.anchor = {cellX * kSeenFeatherCellMm, cellY * kSeenFeatherCellMm};
		cell.low = {cell.anchor.x - feather, cell.anchor.y - feather};
		cell.high = {cell.anchor.x + kSeenFeatherCellMm + feather, cell.anchor.y + kSeenFeatherCellMm + feather};
		edges.clear();
		seen.appendEdges(cell.low, cell.high, edges);
		cell.edges = edges.size();
		cell.pieces.clear();
		appendFeather(edges, static_cast<double>(feather), cell.pieces);
		cell.meshes.clear();
		const glm::dvec2 anchor{static_cast<double>(cell.anchor.x), static_cast<double>(cell.anchor.y)};
		for (const renderer::TessellatedMesh& mesh : valueMeshes(cell.pieces, anchor)) {
			cell.meshes.push_back(Renderer::Primitives::uploadInstancedMesh(mesh, 1));
		}
		cell.built = true;
		cell.stale = false;
	}

	void SeenFeather::update(
		const vision::SeenArea&				   seen,
		std::span<const vision::SeenArea::Box> changed,
		const Vec2i64&						   viewLow,
		const Vec2i64&						   viewHigh,
		std::int64_t						   featherMm
	) {
		++frameNumber;
		const std::size_t epochCells = frameCounts.epochCellsLast;
		const std::size_t epochEdges = frameCounts.epochEdgesLast;
		frameCounts = Counts{.epochCellsLast = epochCells, .epochEdgesLast = epochEdges, .changedBoxes = changed.size()};
		if (featherMm != feather) {
			feather = featherMm;
			cells.clear();
			clusters.clear();
		}
		const Vec2i64 reach{feather, feather};
		for (const vision::SeenArea::Box& box : changed) {
			const Vec2i64 low = box.low - reach;
			const Vec2i64 high = box.high + reach;
			for (std::int64_t cellY = cellOf(low.y, kSeenFeatherCellMm); cellY <= cellOf(high.y, kSeenFeatherCellMm); ++cellY) {
				for (std::int64_t cellX = cellOf(low.x, kSeenFeatherCellMm); cellX <= cellOf(high.x, kSeenFeatherCellMm); ++cellX) {
					const auto found = cells.find({cellX, cellY});
					if (found != cells.end()) {
						found->second.stale = true;
					}
				}
			}
			for (auto it = clusters.begin(); it != clusters.end();) {
				it = boxesMeet(box.low, box.high, it->second.low, it->second.high) ? clusters.erase(it) : std::next(it);
			}
		}

		// The cells the seen ground near the view reaches, read from the record's seen boxes, so a view over
		// ground nobody has seen touches no cell. Each draws edges up to the feather past it, a feather deep.
		drawnCells.clear();
		dueCells.clear();
		const Vec2i64 lowest = viewLow - reach - reach;
		const Vec2i64 highest = viewHigh + reach + reach;
		seenBoxes.clear();
		seen.appendSeenBoxes(lowest, highest, seenBoxes);
		for (const vision::SeenArea::Box& box : seenBoxes) {
			const Vec2i64 low{std::max(box.low.x, lowest.x) - feather, std::max(box.low.y, lowest.y) - feather};
			const Vec2i64 high{std::min(box.high.x, highest.x) + feather, std::min(box.high.y, highest.y) + feather};
			for (std::int64_t cellY = cellOf(low.y, kSeenFeatherCellMm); cellY <= cellOf(high.y, kSeenFeatherCellMm); ++cellY) {
				for (std::int64_t cellX = cellOf(low.x, kSeenFeatherCellMm); cellX <= cellOf(high.x, kSeenFeatherCellMm); ++cellX) {
					Cell& cell = cells[{cellX, cellY}];
					if (cell.frame == frameNumber) {
						continue;
					}
					cell.frame = frameNumber;
					++frameCounts.cellsVisited;
					if (!cell.built || cell.stale) {
						dueCells.emplace_back(cellX, cellY);
					}
				}
			}
		}
		// Cells with nothing yet first, nearest the view's centre first; stale cells, which draw as they
		// were, wait behind them.
		const Vec2i64 middle{(viewLow.x + viewHigh.x) / 2, (viewLow.y + viewHigh.y) / 2};
		const auto	  fromMiddle = [&middle](const std::pair<std::int64_t, std::int64_t>& cell) {
			   const double x = static_cast<double>(cell.first * kSeenFeatherCellMm + kSeenFeatherCellMm / 2 - middle.x);
			   const double y = static_cast<double>(cell.second * kSeenFeatherCellMm + kSeenFeatherCellMm / 2 - middle.y);
			   return x * x + y * y;
		};
		std::ranges::sort(
			dueCells, [&](const std::pair<std::int64_t, std::int64_t>& first, const std::pair<std::int64_t, std::int64_t>& second) {
				const bool firstBuilt = cells[first].built;
				const bool secondBuilt = cells[second].built;
				return firstBuilt != secondBuilt ? !firstBuilt : fromMiddle(first) < fromMiddle(second);
			}
		);
		bool		rebuiltAny = false;
		std::size_t rebuiltEdges = 0;
		for (std::size_t index = 0; index < dueCells.size(); ++index) {
			if (index >= kSeenFeatherCellBuildsPerFrame) {
				++frameCounts.cellsWaiting;
				continue;
			}
			Cell& cell = cells[dueCells[index]];
			buildCell(seen, dueCells[index].first, dueCells[index].second, cell);
			++frameCounts.cellsRebuilt;
			frameCounts.cellEdgesRead += cell.edges;
			rebuiltAny = true;
			rebuiltEdges += cell.edges;
		}
		for (auto it = cells.begin(); it != cells.end();) {
			if (frameNumber - it->second.frame > kCellKeptFrames) {
				it = cells.erase(it);
				continue;
			}
			if (it->second.frame == frameNumber && !it->second.meshes.empty()) {
				drawnCells.push_back(&it->second);
			}
			++it;
		}
		if (rebuiltAny && !changed.empty()) {
			frameCounts.epochCellsLast = frameCounts.cellsRebuilt;
			frameCounts.epochEdgesLast = rebuiltEdges;
		}
		frameCounts.cellsDrawn = drawnCells.size();

		// The pending shapes near the view, in clusters whose feathers can touch, each cut on its own and
		// kept until its shapes or the record there change.
		pendingNear.clear();
		pendingAll.clear();
		seen.appendPendingShapes(pendingAll);
		lows.clear();
		highs.clear();
		const Vec2i64 twice = reach + reach;
		for (const std::vector<Vec2i64>* shape : pendingAll) {
			if (shape->size() < 3) {
				continue;
			}
			Vec2i64 low;
			Vec2i64 high;
			boxOf(*shape, low, high);
			if (boxesMeet(low - twice - reach, high + twice + reach, viewLow, viewHigh)) {
				pendingNear.push_back(shape);
				lows.push_back(low - twice);
				highs.push_back(high + twice);
			}
		}
		frameCounts.pendingShapes = pendingNear.size();
		parents.resize(pendingNear.size());
		std::iota(parents.begin(), parents.end(), std::size_t{0});
		const auto root = [this](std::size_t index) {
			while (parents[index] != index) {
				parents[index] = parents[parents[index]];
				index = parents[index];
			}
			return index;
		};
		for (std::size_t first = 0; first < pendingNear.size(); ++first) {
			for (std::size_t second = first + 1; second < pendingNear.size(); ++second) {
				if (boxesMeet(lows[first], highs[first], lows[second], highs[second])) {
					parents[root(first)] = root(second);
				}
			}
		}
		for (std::vector<std::size_t>& group : members) {
			group.clear();
		}
		members.resize(std::max(members.size(), pendingNear.size()));
		for (std::size_t index = 0; index < pendingNear.size(); ++index) {
			members[root(index)].push_back(index);
		}
		drawnClusters.clear();
		for (const std::vector<std::size_t>& indices : members) {
			if (indices.empty()) {
				continue;
			}
			// A cluster is known by which shapes it holds, so a leg that moves keeps its cluster and its
			// cut is redone only where its shapes changed (7.2).
			hashes.clear();
			Vec2i64 low = lows[indices.front()];
			Vec2i64 high = highs[indices.front()];
			for (const std::size_t index : indices) {
				hashes.push_back(static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(pendingNear[index])));
				low = {std::min(low.x, lows[index].x), std::min(low.y, lows[index].y)};
				high = {std::max(high.x, highs[index].x), std::max(high.y, highs[index].y)};
			}
			shapes.clear();
			for (const std::size_t index : indices) {
				shapes.push_back(pendingNear[index]);
			}
			std::ranges::sort(shapes);
			std::ranges::sort(hashes);
			std::uint64_t key = 1469598103934665603ULL;
			for (const std::uint64_t hash : hashes) {
				key ^= hash;
				key *= 1099511628211ULL;
			}
			++frameCounts.clusters;
			Cluster& cluster = clusters[key];
			region.clear();
			bool whole = !cluster.built || cluster.cut.size() != shapes.size();
			for (std::size_t shape = 0; !whole && shape < shapes.size(); ++shape) {
				if (*shapes[shape] != cluster.cut[shape]) {
					appendChangedBox(cluster.cut[shape], *shapes[shape], region);
				}
			}
			if (whole || !region.empty()) {
				edges.clear();
				seen.appendEdges(low, high, edges);
				std::erase_if(edges, [&](const DirectedEdge& edge) {
					const Vec2i64 edgeLow{std::min(edge.first.x, edge.second.x), std::min(edge.first.y, edge.second.y)};
					const Vec2i64 edgeHigh{std::max(edge.first.x, edge.second.x), std::max(edge.first.y, edge.second.y)};
					if (!whole) {
						return std::ranges::none_of(region, [&](const vision::SeenArea::Box& box) {
							return boxesMeet(edgeLow, edgeHigh, box.low, box.high);
						});
					}
					return std::ranges::none_of(indices, [&](std::size_t index) {
						return boxesMeet(edgeLow, edgeHigh, lows[index], highs[index]);
					});
				});
				if (whole) {
					cluster.boundary.clear();
				} else {
					std::erase_if(cluster.boundary, [&](const DirectedEdge& edge) {
						const Vec2i64 edgeLow{std::min(edge.first.x, edge.second.x), std::min(edge.first.y, edge.second.y)};
						const Vec2i64 edgeHigh{std::max(edge.first.x, edge.second.x), std::max(edge.first.y, edge.second.y)};
						return std::ranges::any_of(region, [&](const vision::SeenArea::Box& box) {
							return boxesMeet(edgeLow, edgeHigh, box.low, box.high);
						});
					});
				}
				cutSeenBoundary(
					shapes, edges, [&seen](const Vec2i64& point) { return seen.inRecord(point); }, region, cluster.boundary, frameCounts.cut
				);
				cluster.grown.clear();
				cluster.cut.resize(shapes.size());
				for (std::size_t shape = 0; shape < shapes.size(); ++shape) {
					appendGrownByFeather(*shapes[shape], static_cast<double>(feather), cluster.grown);
					cluster.cut[shape] = *shapes[shape];
				}
				cluster.built = true;
				cluster.low = low;
				cluster.high = high;
				cluster.anchor = low;
				cluster.pieces.clear();
				appendFeather(cluster.boundary, static_cast<double>(feather), cluster.pieces);
				cluster.meshes.clear();
				const glm::dvec2 anchor{static_cast<double>(low.x), static_cast<double>(low.y)};
				for (const renderer::TessellatedMesh& mesh : valueMeshes(cluster.pieces, anchor)) {
					cluster.meshes.push_back(Renderer::Primitives::uploadInstancedMesh(mesh, 1));
				}
				++frameCounts.clustersCut;
			}
			cluster.frame = frameNumber;
			frameCounts.pendingPieces += cluster.pieces.size();
			drawnClusters.push_back(&cluster);
		}
		for (auto it = clusters.begin(); it != clusters.end();) {
			it = it->second.frame != frameNumber ? clusters.erase(it) : std::next(it);
		}
	}

} // namespace worldview
