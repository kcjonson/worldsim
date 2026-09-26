#include "TerrainPolygonQuery.h"

#include <predicates/Predicates.h>

#include <algorithm>
#include <cmath>
#include <tuple>
#include <utility>

namespace engine::world {

	namespace {

		using geometry::Vec2i64;
		using Edge = TerrainEdgeIndex::Edge;

		constexpr int64_t kCellMm = TerrainPolygonQuery::kCellMm;
		// Slack on the box pruning bound, far above its rounding, so pruning never
		// drops an edge that could win or tie.
		constexpr double kPruneSlackMm = 1.0;

		int64_t floorDiv(int64_t a, int64_t b) {
			const int64_t q = a / b;
			return (a % b != 0 && a < 0) ? q - 1 : q;
		}

		// How far v lies outside [lo, hi]; zero inside.
		int64_t outside(int64_t v, int64_t lo, int64_t hi) {
			return v < lo ? lo - v : (v > hi ? v - hi : 0);
		}

		struct EdgeEnds {
			const Vec2i64& a;
			const Vec2i64& b;
		};

		EdgeEnds endsOf(const std::vector<TerrainRing>& rings, const Edge& edge) {
			const geometry::Ring& ring = rings[edge.ring].ring;
			return {ring[edge.vertex], ring[(edge.vertex + 1) % ring.size()]};
		}

		// Cell (x, y) of the index holding `point`, possibly outside the grid.
		std::pair<int64_t, int64_t> cellOf(const TerrainEdgeIndex& index, const Vec2i64& point) {
			return {floorDiv(point.x - index.originMm.x, kCellMm), floorDiv(point.y - index.originMm.y, kCellMm)};
		}

		bool inGrid(const TerrainEdgeIndex& index, int64_t x, int64_t y) {
			return x >= 0 && y >= 0 && x < index.cellsPerSide && y < index.cellsPerSide;
		}

		size_t cellIndex(const TerrainEdgeIndex& index, int64_t x, int64_t y) {
			return static_cast<size_t>(y) * static_cast<size_t>(index.cellsPerSide) + static_cast<size_t>(x);
		}

		std::pair<const Edge*, const Edge*> cellEdges(const TerrainEdgeIndex& index, size_t cell) {
			return {index.edges.data() + index.cellStart[cell], index.edges.data() + index.cellStart[cell + 1]};
		}

		// The ray from `point` toward +x, walked cell by cell along its row until it
		// enters a cell no edge touches (whose containment is stored) or leaves the
		// grid (outside every ring). Each edge counts in the first cell of the walk
		// its box touches, and the half-open rule (one end strictly above the ray,
		// the other at or below) counts each crossing once, as pointInPolygon does.
		TerrainEdgeIndex::CellWater castRay(const TerrainEdgeIndex& index, const std::vector<TerrainRing>& rings, const Vec2i64& point,
											int64_t startX, int64_t row) {
			TerrainEdgeIndex::CellWater water;
			for (int64_t x = std::max<int64_t>(startX, 0); x < index.cellsPerSide; ++x) {
				const size_t cell = cellIndex(index, x, row);
				const auto [first, last] = cellEdges(index, cell);
				if (first == last) {
					const TerrainEdgeIndex::CellWater& beyond = index.cellWater[cell];
					water.waterlineParity ^= beyond.waterlineParity;
					water.solidDepth = static_cast<int16_t>(water.solidDepth + beyond.solidDepth);
					return water;
				}
				for (const Edge* edge = first; edge != last; ++edge) {
					const auto [a, b] = endsOf(rings, *edge);
					if ((a.y > point.y) == (b.y > point.y)) {
						continue;
					}
					if (x > std::max<int64_t>(startX, floorDiv(std::min(a.x, b.x) - index.originMm.x, kCellMm))) {
						continue; // counted in an earlier cell of the walk
					}
					const geometry::Orientation side = geometry::orientation(a, b, point);
					const bool up = b.y > a.y;
					const bool rightOfPoint =
						up ? side == geometry::Orientation::CounterClockwise : side == geometry::Orientation::Clockwise;
					if (!rightOfPoint) {
						continue;
					}
					if (rings[edge->ring].holeCapable) {
						water.waterlineParity ^= 1U;
					} else {
						water.solidDepth = static_cast<int16_t>(water.solidDepth + (up ? 1 : -1));
					}
				}
			}
			return water;
		}

	} // namespace

	TerrainEdgeIndex TerrainPolygonQuery::buildIndex(const std::vector<TerrainRing>& rings, const geometry::RectMm& area) {
		TerrainEdgeIndex index;
		if (rings.empty()) {
			return index;
		}
		const int64_t minX = floorDiv(area.min.x, kCellMm);
		const int64_t minY = floorDiv(area.min.y, kCellMm);
		const int64_t span = std::max(floorDiv(area.max.x, kCellMm) - minX, floorDiv(area.max.y, kCellMm) - minY) + 1;
		index.originMm	   = {minX * kCellMm, minY * kCellMm};
		index.cellsPerSide = static_cast<int32_t>(span);

		// Counting sort of (cell, edge) pairs; each cell keeps ring order.
		const size_t						  cells = static_cast<size_t>(span * span);
		std::vector<std::pair<uint32_t, Edge>> pairs;
		for (uint32_t r = 0; r < rings.size(); ++r) {
			const TerrainRing& ring = rings[r];
			const size_t	   n	= ring.ring.size();
			for (uint32_t i = 0; i < n; ++i) {
				const Edge	   edge{r, i, ring.isShoreEdge(i)};
				const auto [a, b] = endsOf(rings, edge);
				const auto [x0, y0] = cellOf(index, {std::min(a.x, b.x), std::min(a.y, b.y)});
				const auto [x1, y1] = cellOf(index, {std::max(a.x, b.x), std::max(a.y, b.y)});
				for (int64_t y = std::max<int64_t>(y0, 0); y <= std::min<int64_t>(y1, span - 1); ++y) {
					for (int64_t x = std::max<int64_t>(x0, 0); x <= std::min<int64_t>(x1, span - 1); ++x) {
						pairs.emplace_back(static_cast<uint32_t>(cellIndex(index, x, y)), edge);
					}
				}
			}
		}
		index.cellStart.assign(cells + 1, 0);
		for (const auto& [cell, edge] : pairs) {
			++index.cellStart[cell + 1];
		}
		for (size_t c = 0; c < cells; ++c) {
			index.cellStart[c + 1] += index.cellStart[c];
		}
		index.edges.resize(pairs.size());
		std::vector<uint32_t> cursor(index.cellStart.begin(), index.cellStart.end() - 1);
		for (const auto& [cell, edge] : pairs) {
			index.edges[cursor[cell]++] = edge;
		}

		// Edge-free cells right to left along each row, so the ray from a cell's
		// center stops at a cell already known.
		index.cellWater.resize(cells);
		for (int64_t y = 0; y < span; ++y) {
			for (int64_t x = span - 1; x >= 0; --x) {
				const size_t cell = cellIndex(index, x, y);
				if (index.cellStart[cell] == index.cellStart[cell + 1]) {
					const Vec2i64 center{index.originMm.x + x * kCellMm + kCellMm / 2, index.originMm.y + y * kCellMm + kCellMm / 2};
					index.cellWater[cell] = castRay(index, rings, center, x + 1, y);
				}
			}
		}
		return index;
	}

	std::optional<TerrainPolygonQuery::ShoreHit> TerrainPolygonQuery::nearestShore(const Vec2i64& point, double searchMm) const {
		const TerrainEdgeIndex& index = polygons.edgeIndex;
		if (index.cellsPerSide == 0) {
			return std::nullopt;
		}
		const Vec2i64 gridMax{
			index.originMm.x + static_cast<int64_t>(index.cellsPerSide) * kCellMm, index.originMm.y + static_cast<int64_t>(index.cellsPerSide) * kCellMm
		};

		// Every shore edge within `reach` of the point lies in a cell the square of
		// half-side `reach` around it touches; the nearest of them within `reach`.
		auto searchSquare = [&](double reach) {
			std::optional<ShoreHit> best;
			double					bound = reach;
			auto					tryEdge = [&](const Edge& edge) {
				   const auto [a, b] = endsOf(polygons.rings, edge);
				   const auto	ox	  = static_cast<double>(outside(point.x, std::min(a.x, b.x), std::max(a.x, b.x)));
				   const auto	oy	  = static_cast<double>(outside(point.y, std::min(a.y, b.y), std::max(a.y, b.y)));
				   const double prune = bound + kPruneSlackMm;
				   if (ox * ox + oy * oy > prune * prune) {
					   return;
				   }
				   const geometry::SegmentPoint closest = geometry::closestOnSegment(point, a, b);
				   if (closest.distanceMm > bound) {
					   return;
				   }
				   if (best && closest.distanceMm == best->distanceMm) {
					   // Ties go to the lexicographically smaller edge, whatever order the rings are in.
					   const auto [bestA, bestB] = endsOf(polygons.rings, *best->edge);
					   if (!(std::tie(std::min(a, b), std::max(a, b)) < std::tie(std::min(bestA, bestB), std::max(bestA, bestB)))) {
						   return;
					   }
				   }
				   best	 = ShoreHit{closest.distanceMm, &edge, closest.t};
				   bound = closest.distanceMm;
			};
			const auto reachMm = static_cast<int64_t>(std::ceil(reach));
			const auto [x0, y0] = cellOf(index, {point.x - reachMm, point.y - reachMm});
			const auto [x1, y1]	  = cellOf(index, {point.x + reachMm, point.y + reachMm});
			for (int64_t y = std::max<int64_t>(y0, 0); y <= std::min<int64_t>(y1, index.cellsPerSide - 1); ++y) {
				for (int64_t x = std::max<int64_t>(x0, 0); x <= std::min<int64_t>(x1, index.cellsPerSide - 1); ++x) {
					const auto [first, last] = cellEdges(index, cellIndex(index, x, y));
					for (const Edge* edge = first; edge != last; ++edge) {
						if (edge->shore) {
							tryEdge(*edge);
						}
					}
				}
			}
			return best;
		};

		// Widen the square until it finds the shore or holds the whole grid: a hit
		// within the square is the nearest anywhere.
		double reach = std::min(searchMm, static_cast<double>(kCellMm));
		while (true) {
			std::optional<ShoreHit> best = searchSquare(reach);
			const bool				wholeGrid = static_cast<double>(point.x) - reach <= static_cast<double>(index.originMm.x) &&
								static_cast<double>(point.y) - reach <= static_cast<double>(index.originMm.y) &&
								static_cast<double>(point.x) + reach >= static_cast<double>(gridMax.x) &&
								static_cast<double>(point.y) + reach >= static_cast<double>(gridMax.y);
			if (best || reach >= searchMm || wholeGrid) {
				return best;
			}
			reach = std::min(searchMm, 2.0 * reach);
		}
	}

	double TerrainPolygonQuery::distanceToWaterMm(const Vec2i64& point, double searchMm) const {
		if (isInsideWater(point)) {
			return 0.0;
		}
		const std::optional<ShoreHit> hit = nearestShore(point, searchMm);
		return hit ? hit->distanceMm : kUnbounded;
	}

	std::optional<Vec2i64> TerrainPolygonQuery::nearestShorePoint(const Vec2i64& point) const {
		const std::optional<ShoreHit> hit = nearestShore(point, kUnbounded);
		if (!hit) {
			return std::nullopt;
		}
		const auto [a, b] = endsOf(polygons.rings, *hit->edge);
		return Vec2i64{
			a.x + std::llround(static_cast<double>(b.x - a.x) * hit->t), a.y + std::llround(static_cast<double>(b.y - a.y) * hit->t)
		};
	}

	bool TerrainPolygonQuery::isInsideWater(const Vec2i64& point) const {
		const TerrainEdgeIndex& index = polygons.edgeIndex;
		const auto [x, y]			  = cellOf(index, point);
		if (!inGrid(index, x, y)) {
			return false;
		}
		const size_t cell = cellIndex(index, x, y);
		const TerrainEdgeIndex::CellWater water =
			index.cellStart[cell] == index.cellStart[cell + 1] ? index.cellWater[cell] : castRay(index, polygons.rings, point, x, y);
		return water.waterlineParity != 0 || water.solidDepth > 0;
	}

} // namespace engine::world
