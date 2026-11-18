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
		constexpr double	  kFanStep			  = kFeatherFanStepDegrees * std::numbers::pi / 180.0;

		glm::dvec2 toDouble(const Vec2i64& point) { return {static_cast<double>(point.x), static_cast<double>(point.y)}; }

		Vec2i64 rounded(glm::dvec2 point) { return {std::llround(point.x), std::llround(point.y)}; }

		std::int64_t cellOf(std::int64_t millimetres, std::int64_t size) {
			const std::int64_t cell = millimetres / size;
			return millimetres % size < 0 ? cell - 1 : cell;
		}

		bool boxesMeet(const Vec2i64& firstLow, const Vec2i64& firstHigh, const Vec2i64& secondLow, const Vec2i64& secondHigh) {
			return firstLow.x <= secondHigh.x && secondLow.x <= firstHigh.x && firstLow.y <= secondHigh.y && secondLow.y <= firstHigh.y;
		}

		void boxOf(const std::vector<Vec2i64>& ring, Vec2i64& low, Vec2i64& high) {
			low	 = ring.front();
			high = ring.front();
			for (const Vec2i64& point : ring) {
				low	 = {std::min(low.x, point.x), std::min(low.y, point.y)};
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
				const Vec2i64& next		= ring[(k + 1) % ring.size()];
				if (geometry::cross(ring[k] - previous, next - ring[k]).sign() < 0) {
					return false;
				}
			}
			return true;
		}

		std::uint64_t hashOf(const std::vector<Vec2i64>& ring) {
			std::uint64_t hash = 1469598103934665603ULL;
			for (const Vec2i64& point : ring) {
				for (const std::int64_t value : {point.x, point.y}) {
					hash ^= static_cast<std::uint64_t>(value);
					hash *= 1099511628211ULL;
				}
			}
			return hash;
		}

		// A segment of the cut: its ends, whose it is (-1 the record's), and where along it it is split.
		struct Segment {
			Vec2i64				start;
			Vec2i64				end;
			int					owner = -1;
			std::vector<double> splits;
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
			const int startSide	 = geometry::cross(first.end - first.start, second.start - first.start).sign();
			const int endSide	 = geometry::cross(first.end - first.start, second.end - first.start).sign();
			const int otherStart = geometry::cross(second.end - second.start, first.start - second.start).sign();
			const int otherEnd	 = geometry::cross(second.end - second.start, first.end - second.start).sign();
			if (startSide * endSide < 0 && otherStart * otherEnd < 0) {
				const glm::dvec2 along		= toDouble(first.end) - toDouble(first.start);
				const glm::dvec2 otherAlong = toDouble(second.end) - toDouble(second.start);
				const glm::dvec2 offset		= toDouble(second.start) - toDouble(first.start);
				const double	 t = (offset.x * otherAlong.y - offset.y * otherAlong.x) / (along.x * otherAlong.y - along.y * otherAlong.x);
				const Vec2i64	 crossing = rounded(toDouble(first.start) + t * along);
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
			const glm::dvec2 from	= toDouble(start);
			const glm::dvec2 along	= toDouble(end) - from;
			const double	 length = glm::length(along);
			return rounded(from + along / 2.0 + glm::dvec2{along.y, -along.x} / length * kProbeMm);
		}

	} // namespace

	void cutSeenBoundary(
		std::span<const std::vector<Vec2i64>* const>	shapes,
		std::span<const DirectedEdge>					recordEdges,
		const std::function<bool(const Vec2i64&)>&		inRecord,
		std::vector<DirectedEdge>&						out,
		SeenCutCounts&									counts
	) {
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

		// Every pair of segments of different owners that can meet, found through a grid of their boxes.
		std::unordered_map<std::uint64_t, std::vector<std::size_t>> grid;
		const auto keyOf = [](std::int64_t x, std::int64_t y) { return (static_cast<std::uint64_t>(x) << 32U) ^ (static_cast<std::uint64_t>(y) & 0xFFFFFFFFULL); };
		for (std::size_t index = 0; index < segments.size(); ++index) {
			const Segment& segment = segments[index];
			for (std::int64_t y = cellOf(std::min(segment.start.y, segment.end.y), kCutGridMm); y <= cellOf(std::max(segment.start.y, segment.end.y), kCutGridMm); ++y) {
				for (std::int64_t x = cellOf(std::min(segment.start.x, segment.end.x), kCutGridMm); x <= cellOf(std::max(segment.start.x, segment.end.x), kCutGridMm); ++x) {
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

		const auto insideShape = [&](const Vec2i64& point, int except) {
			for (std::size_t shape = 0; shape < rings.size(); ++shape) {
				if (static_cast<int>(shape) != except && point.x >= lows[shape].x && point.x <= highs[shape].x && point.y >= lows[shape].y &&
					point.y <= highs[shape].y && geometry::pointInPolygon(point, rings[shape]) != geometry::PointInPolygon::Outside) {
					return true;
				}
			}
			return false;
		};
		for (Segment& segment : segments) {
			std::vector<std::size_t> order(segment.splits.size());
			std::iota(order.begin(), order.end(), std::size_t{0});
			std::ranges::sort(order, [&segment](std::size_t first, std::size_t second) { return segment.splits[first] < segment.splits[second]; });
			Vec2i64 from = segment.start;
			for (std::size_t piece = 0; piece <= order.size(); ++piece) {
				const Vec2i64 to = piece < order.size() ? segment.splitPoints[order[piece]] : segment.end;
				if (to == from) {
					continue;
				}
				const Vec2i64 probe = outerProbe(from, to);
				const bool	  kept	= segment.owner < 0 ? !insideShape(probe, -1) : !inRecord(probe) && !insideShape(probe, segment.owner);
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
		const std::size_t count	  = ring.size();
		const auto		  outward = [&](std::size_t side) {
			   const glm::dvec2 along = toDouble(ring[(side + 1) % count]) - toDouble(ring[side]);
			   const double		length = glm::length(along);
			   return length > 0.0 ? glm::dvec2{along.y, -along.x} / length : glm::dvec2{0.0};
		};
		// The arc round corner k from the side before it's outward normal to the side after's.
		const auto appendArc = [&](std::size_t corner, std::vector<glm::dvec2>& into) {
			const glm::dvec2 from  = outward((corner + count - 1) % count);
			const glm::dvec2 to	   = outward(corner);
			const double	 sweep = std::atan2(from.x * to.y - from.y * to.x, glm::dot(from, to));
			const auto		 steps = std::max(1, static_cast<int>(std::ceil(std::abs(sweep) / kFanStep)));
			for (int step = 0; step <= steps; ++step) {
				const double	 angle = sweep * static_cast<double>(step) / static_cast<double>(steps);
				const glm::dvec2 turn{from.x * std::cos(angle) - from.y * std::sin(angle), from.x * std::sin(angle) + from.y * std::cos(angle)};
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
			const glm::dvec2 from	 = toDouble(ring[side]);
			const glm::dvec2 to		 = toDouble(ring[(side + 1) % count]);
			const glm::dvec2 offset	 = featherMm * outward(side);
			out.push_back({from, to, to + offset, from + offset});
			const Vec2i64& previous = ring[(side + count - 1) % count];
			if (geometry::cross(ring[side] - previous, ring[(side + 1) % count] - ring[side]).sign() > 0) {
				std::vector<glm::dvec2> fan{from};
				appendArc(side, fan);
				out.push_back(std::move(fan));
			}
		}
	}

	void SeenFeather::buildCell(const vision::SeenArea& seen, std::int64_t cellX, std::int64_t cellY, Cell& cell) const {
		const Vec2i64			  low{cellX * kSeenFeatherCellMm - feather, cellY * kSeenFeatherCellMm - feather};
		const Vec2i64			  high{(cellX + 1) * kSeenFeatherCellMm + feather, (cellY + 1) * kSeenFeatherCellMm + feather};
		std::vector<DirectedEdge> edges;
		seen.appendEdges(low, high, edges);
		cell.edges = edges.size();
		cell.low   = low;
		cell.high  = high;
		cell.pieces.clear();
		appendFeather(edges, static_cast<double>(feather), cell.pieces);
		cell.meshes.clear();
		for (const renderer::TessellatedMesh& mesh : valueMeshes(cell.pieces)) {
			cell.meshes.push_back(Renderer::Primitives::uploadInstancedMesh(mesh, 1));
		}
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
		frameCounts					 = Counts{.epochCellsLast = epochCells, .epochEdgesLast = epochEdges, .changedBoxes = changed.size()};
		if (featherMm != feather) {
			feather = featherMm;
			cells.clear();
			clusters.clear();
		}
		const Vec2i64 reach{feather, feather};
		for (const vision::SeenArea::Box& box : changed) {
			const Vec2i64 low  = box.low - reach;
			const Vec2i64 high = box.high + reach;
			for (auto it = cells.begin(); it != cells.end();) {
				const Vec2i64 cellLow{it->first.first * kSeenFeatherCellMm, it->first.second * kSeenFeatherCellMm};
				const Vec2i64 cellHigh{cellLow.x + kSeenFeatherCellMm, cellLow.y + kSeenFeatherCellMm};
				it = boxesMeet(low, high, cellLow, cellHigh) ? cells.erase(it) : std::next(it);
			}
			for (auto it = clusters.begin(); it != clusters.end();) {
				it = boxesMeet(box.low, box.high, it->second.low, it->second.high) ? clusters.erase(it) : std::next(it);
			}
		}

		// The cells whose pieces can reach the view: each draws edges up to the feather past it, a feather deep.
		drawnCells.clear();
		const Vec2i64 lowest  = viewLow - reach - reach;
		const Vec2i64 highest = viewHigh + reach + reach;
		bool		  rebuiltAny = false;
		std::size_t	  rebuiltEdges = 0;
		for (std::int64_t y = cellOf(lowest.y, kSeenFeatherCellMm); y <= cellOf(highest.y, kSeenFeatherCellMm); ++y) {
			for (std::int64_t x = cellOf(lowest.x, kSeenFeatherCellMm); x <= cellOf(highest.x, kSeenFeatherCellMm); ++x) {
				auto [found, fresh] = cells.try_emplace({x, y});
				if (fresh) {
					buildCell(seen, x, y, found->second);
					++frameCounts.cellsRebuilt;
					frameCounts.cellEdgesRead += found->second.edges;
					rebuiltAny = true;
					rebuiltEdges += found->second.edges;
				}
				found->second.frame = frameNumber;
				if (!found->second.meshes.empty()) {
					drawnCells.push_back(&found->second);
				}
			}
		}
		if (rebuiltAny && !changed.empty()) {
			frameCounts.epochCellsLast = frameCounts.cellsRebuilt;
			frameCounts.epochEdgesLast = rebuiltEdges;
		}
		frameCounts.cellsDrawn = drawnCells.size();
		for (auto it = cells.begin(); it != cells.end();) {
			it = frameNumber - it->second.frame > kCellKeptFrames ? cells.erase(it) : std::next(it);
		}

		// The pending shapes near the view, in clusters whose feathers can touch, each cut on its own and
		// kept until its shapes or the record there change.
		pendingNear.clear();
		std::vector<const std::vector<Vec2i64>*> pending;
		seen.appendPendingShapes(pending);
		std::vector<Vec2i64> lows;
		std::vector<Vec2i64> highs;
		const Vec2i64		 twice = reach + reach;
		for (const std::vector<Vec2i64>* shape : pending) {
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
		std::vector<std::size_t> parent(pendingNear.size());
		std::iota(parent.begin(), parent.end(), std::size_t{0});
		const auto root = [&parent](std::size_t index) {
			while (parent[index] != index) {
				parent[index] = parent[parent[index]];
				index		  = parent[index];
			}
			return index;
		};
		for (std::size_t first = 0; first < pendingNear.size(); ++first) {
			for (std::size_t second = first + 1; second < pendingNear.size(); ++second) {
				if (boxesMeet(lows[first], highs[first], lows[second], highs[second])) {
					parent[root(first)] = root(second);
				}
			}
		}
		std::map<std::size_t, std::vector<std::size_t>> members;
		for (std::size_t index = 0; index < pendingNear.size(); ++index) {
			members[root(index)].push_back(index);
		}
		pendingDrawn.clear();
		grown.clear();
		for (const auto& [clusterRoot, indices] : members) {
			std::vector<std::uint64_t> hashes;
			Vec2i64					   low	= lows[indices.front()];
			Vec2i64					   high = highs[indices.front()];
			for (const std::size_t index : indices) {
				hashes.push_back(hashOf(*pendingNear[index]));
				low	 = {std::min(low.x, lows[index].x), std::min(low.y, lows[index].y)};
				high = {std::max(high.x, highs[index].x), std::max(high.y, highs[index].y)};
				appendGrownByFeather(*pendingNear[index], static_cast<double>(feather), grown);
			}
			std::ranges::sort(hashes);
			std::uint64_t key = 1469598103934665603ULL;
			for (const std::uint64_t hash : hashes) {
				key ^= hash;
				key *= 1099511628211ULL;
			}
			++frameCounts.clusters;
			auto [found, fresh] = clusters.try_emplace(key);
			if (fresh) {
				std::vector<DirectedEdge> edges;
				seen.appendEdges(low, high, edges);
				std::erase_if(edges, [&](const DirectedEdge& edge) {
					const Vec2i64 edgeLow{std::min(edge.first.x, edge.second.x), std::min(edge.first.y, edge.second.y)};
					const Vec2i64 edgeHigh{std::max(edge.first.x, edge.second.x), std::max(edge.first.y, edge.second.y)};
					return std::ranges::none_of(indices, [&](std::size_t index) { return boxesMeet(edgeLow, edgeHigh, lows[index], highs[index]); });
				});
				std::vector<const std::vector<Vec2i64>*> shapes;
				for (const std::size_t index : indices) {
					shapes.push_back(pendingNear[index]);
				}
				std::vector<DirectedEdge> boundary;
				cutSeenBoundary(shapes, edges, [&seen](const Vec2i64& point) { return seen.inRecord(point); }, boundary, frameCounts.cut);
				found->second.low  = low;
				found->second.high = high;
				appendFeather(boundary, static_cast<double>(feather), found->second.pieces);
				for (const renderer::TessellatedMesh& mesh : valueMeshes(found->second.pieces)) {
					found->second.meshes.push_back(Renderer::Primitives::uploadInstancedMesh(mesh, 1));
				}
				++frameCounts.clustersCut;
			}
			found->second.frame = frameNumber;
			frameCounts.pendingPieces += found->second.pieces.size();
		}
		for (auto it = clusters.begin(); it != clusters.end();) {
			it = it->second.frame != frameNumber ? clusters.erase(it) : std::next(it);
		}
		for (const auto& [key, cluster] : clusters) {
			for (const Renderer::InstancedMeshHandle& mesh : cluster.meshes) {
				pendingDrawn.push_back(&mesh);
			}
		}
	}

	void SeenFeather::appendPendingPieces(std::vector<ValueTriangle>& out) const {
		for (const auto& [key, cluster] : clusters) {
			out.insert(out.end(), cluster.pieces.begin(), cluster.pieces.end());
		}
	}

} // namespace worldview
