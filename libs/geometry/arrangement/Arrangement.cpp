#include "Arrangement.h"

#include "../core/Int128.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <tuple>
#include <vector>

namespace geometry {

	namespace {

		// A segment still being resolved: a piece of one input segment, whose index
		// it carries. Coincident pieces union their indices when edges are built.
		struct WorkSegment {
			Vec2i64		 a;
			Vec2i64		 b;
			std::int64_t source = 0;
		};

		bool collinear(const Vec2i64& a, const Vec2i64& b, const Vec2i64& c) {
			return cross(b - a, c - a).sign() == 0;
		}

		// Is p strictly interior to segment [a,b] (collinear, between, not an
		// endpoint)? Used to decide whether a candidate point actually splits the
		// segment. Assumes a != b.
		bool strictlyInterior(const Vec2i64& p, const Vec2i64& a, const Vec2i64& b) {
			if (p == a || p == b) {
				return false;
			}
			if (!collinear(a, b, p)) {
				return false;
			}
			return p.x >= std::min(a.x, b.x) && p.x <= std::max(a.x, b.x) && p.y >= std::min(a.y, b.y) &&
				   p.y <= std::max(a.y, b.y);
		}

		// Clamp p into the axis-aligned bounding box of [a,b]. After a proper
		// crossing rounds to the nearest mm the point can land a hair outside the
		// box (or just off the exact line); clamping snaps it back onto the
		// segment's extent so it becomes a valid interior split vertex.
		Vec2i64 clampToBox(const Vec2i64& p, const Vec2i64& a, const Vec2i64& b) {
			return {std::clamp(p.x, std::min(a.x, b.x), std::max(a.x, b.x)),
					std::clamp(p.y, std::min(a.y, b.y), std::max(a.y, b.y))};
		}

		// Canonical rounded crossing point of two segments, independent of which is
		// passed first. intersectSegments parametrizes on its first segment, so the
		// rounded point can differ by a millimeter depending on argument order;
		// ordering the pair canonically guarantees both contributors split at the
		// SAME lattice vertex, which is what actually resolves the crossing.
		Vec2i64 canonicalCrossingPoint(const WorkSegment& s, const WorkSegment& other) {
			const bool sFirst =
				std::tie(s.a, s.b) < std::tie(other.a, other.b);
			const WorkSegment& p = sFirst ? s : other;
			const WorkSegment& q = sFirst ? other : s;
			return intersectSegments(p.a, p.b, q.a, q.b).point;
		}

		// Collect every point at which `other` forces a split of segment `s`'s
		// interior. Returns the interior split points (never s's own endpoints).
		// Handles all four SegmentRelation cases plus the collinear-endpoint
		// incidence the predicate reports as EndpointTouch.
		void interiorSplitPoints(const WorkSegment& s, const WorkSegment& other, std::vector<Vec2i64>& out) {
			const SegmentIntersection r = intersectSegments(s.a, s.b, other.a, other.b);
			switch (r.relation) {
				case SegmentRelation::Disjoint:
					return;
				case SegmentRelation::ProperCrossing: {
					// Snap-rounding: a proper crossing must become a shared vertex on
					// BOTH segments even when rounding moved it off either exact line.
					// Use the order-independent rounded point, clamped onto s, and
					// split there unless it coincides with an endpoint.
					const Vec2i64 cp = clampToBox(canonicalCrossingPoint(s, other), s.a, s.b);
					if (cp != s.a && cp != s.b) {
						out.push_back(cp);
					}
					return;
				}
				case SegmentRelation::EndpointTouch:
					if (strictlyInterior(r.point, s.a, s.b)) {
						out.push_back(r.point);
					}
					return;
				case SegmentRelation::CollinearOverlap:
					// The shared subsegment's endpoints split s wherever they fall
					// in its interior; the overlap body itself is handled by the
					// dedup pass once both segments share those vertices.
					if (strictlyInterior(r.overlapStart, s.a, s.b)) {
						out.push_back(r.overlapStart);
					}
					if (strictlyInterior(r.overlapEnd, s.a, s.b)) {
						out.push_back(r.overlapEnd);
					}
					return;
			}
		}

		struct Box {
			Vec2i64 lo;
			Vec2i64 hi;
		};

		Box boxOf(const WorkSegment& s) {
			return {{std::min(s.a.x, s.b.x), std::min(s.a.y, s.b.y)}, {std::max(s.a.x, s.b.x), std::max(s.a.y, s.b.y)}};
		}

		bool boxesOverlap(const Box& p, const Box& q) {
			return p.lo.x <= q.hi.x && q.lo.x <= p.hi.x && p.lo.y <= q.hi.y && q.lo.y <= p.hi.y;
		}

		// Broad phase for the split pass. Every non-Disjoint relation from
		// intersectSegments means the two segments share a point, so their closed
		// boxes overlap and both cover that point's cell. Each segment is registered
		// in every cell its closed box covers, so no such pair is missed.
		struct SegmentGrid {
			Vec2i64					 origin;
			std::int64_t			 cellSize = 1;
			std::int64_t			 cols = 0;
			std::int64_t			 rows = 0;
			std::vector<std::size_t> cellStart; // CSR: cell c holds entries[cellStart[c] .. cellStart[c+1])
			std::vector<std::size_t> entries;	// segment indices
		};

		struct CellRange {
			std::int64_t x0, y0, x1, y1;
		};

		CellRange cellRangeOf(const SegmentGrid& grid, const Box& b) {
			return {
				(b.lo.x - grid.origin.x) / grid.cellSize,
				(b.lo.y - grid.origin.y) / grid.cellSize,
				(b.hi.x - grid.origin.x) / grid.cellSize,
				(b.hi.y - grid.origin.y) / grid.cellSize
			};
		}

		void buildSegmentGrid(const std::vector<Box>& boxes, SegmentGrid& grid, std::vector<std::size_t>& fill) {
			Vec2i64 lo = boxes.front().lo;
			Vec2i64		hi = boxes.front().hi;
			for (const Box& b : boxes) {
				lo = {std::min(lo.x, b.lo.x), std::min(lo.y, b.lo.y)};
				hi = {std::max(hi.x, b.hi.x), std::max(hi.y, b.hi.y)};
			}
			const std::int64_t width = hi.x - lo.x;
			const std::int64_t height = hi.y - lo.y;
			const auto		   n = static_cast<double>(boxes.size());

			// About one cell per segment, then capped per axis so a degenerate
			// (collinear) or very lopsided extent can't allocate a huge grid.
			const double ideal = std::ceil(std::sqrt(static_cast<double>(width) * static_cast<double>(height) / n));
			const double limit = static_cast<double>(std::max(width, height)) + 1.0;
			std::int64_t cellSize = std::max<std::int64_t>(1, static_cast<std::int64_t>(std::min(ideal, limit)));
			const auto	 maxAxisCells = static_cast<std::int64_t>(4 * std::ceil(std::sqrt(n)));
			cellSize = std::max({cellSize, width / maxAxisCells + 1, height / maxAxisCells + 1});

			grid.origin = lo;
			grid.cellSize = cellSize;
			grid.cols = width / cellSize + 1;
			grid.rows = height / cellSize + 1;

			const auto cellCount = static_cast<std::size_t>(grid.cols * grid.rows);
			grid.cellStart.assign(cellCount + 1, 0);
			for (const Box& b : boxes) {
				const CellRange r = cellRangeOf(grid, b);
				for (std::int64_t cy = r.y0; cy <= r.y1; ++cy) {
					for (std::int64_t cx = r.x0; cx <= r.x1; ++cx) {
						++grid.cellStart[static_cast<std::size_t>(cy * grid.cols + cx) + 1];
					}
				}
			}
			for (std::size_t c = 0; c < cellCount; ++c) {
				grid.cellStart[c + 1] += grid.cellStart[c];
			}
			grid.entries.resize(grid.cellStart[cellCount]);
			fill.assign(grid.cellStart.begin(), grid.cellStart.end() - 1);
			for (std::size_t i = 0; i < boxes.size(); ++i) {
				const CellRange r = cellRangeOf(grid, boxes[i]);
				for (std::int64_t cy = r.y0; cy <= r.y1; ++cy) {
					for (std::int64_t cx = r.x0; cx <= r.x1; ++cx) {
						grid.entries[fill[static_cast<std::size_t>(cy * grid.cols + cx)]++] = i;
					}
				}
			}
		}

		struct Piece {
			std::size_t	 from;
			std::size_t	 to;
			std::int64_t source;
			bool		 operator<(const Piece& o) const { return std::tie(from, to, source) < std::tie(o.from, o.to, o.source); }
		};

	} // namespace

	struct ArrangementWorkspace::Storage {
		std::vector<WorkSegment> work;
		std::vector<WorkSegment> next;
		std::vector<Box>		 boxes;
		SegmentGrid				 grid;
		std::vector<std::size_t> fill;
		std::vector<std::size_t> lastSeen;
		std::vector<Vec2i64>	 cuts;
		std::vector<Piece>		 pieces;
	};

	ArrangementWorkspace::ArrangementWorkspace() : store(std::make_unique<Storage>()) {}
	ArrangementWorkspace::~ArrangementWorkspace()											  = default;
	ArrangementWorkspace::ArrangementWorkspace(ArrangementWorkspace&& other) noexcept			  = default;
	ArrangementWorkspace& ArrangementWorkspace::operator=(ArrangementWorkspace&& other) noexcept = default;

	Arrangement buildArrangement(const std::vector<InputSegment>& segments) {
		ArrangementWorkspace workspace;
		Arrangement			 out;
		buildArrangement(segments, workspace, out);
		return out;
	}

	void buildArrangement(const std::vector<InputSegment>& segments, ArrangementWorkspace& workspace, Arrangement& out) {
		ArrangementWorkspace::Storage& ws	= workspace.storage();
		std::vector<WorkSegment>&	   work = ws.work;
		work.clear();
		for (const InputSegment& in : segments) {
			if (in.a == in.b) {
				continue; // reject zero-length input
			}
			work.push_back({in.a, in.b, in.index});
		}

		// Split to a fixpoint. Each pass gathers every interior incidence on each
		// segment (from the exact predicate) and splits there. A rounded crossing
		// point added this pass may newly land on a third segment or coincide with
		// another rounded point; the next pass sees that incidence and splits
		// again. The loop terminates because every split strictly shortens a
		// segment and points live on the finite integer-mm grid, so the supply of
		// distinct interior points is bounded.
		bool					  changed  = !work.empty();
		std::vector<Vec2i64>&	  cuts	   = ws.cuts;
		std::vector<WorkSegment>& next	   = ws.next;
		std::vector<Box>&		  boxes	   = ws.boxes;
		SegmentGrid&			  grid	   = ws.grid;
		std::vector<std::size_t>& lastSeen = ws.lastSeen;
		while (changed) {
			changed = false;
			next.clear();
			boxes.clear();
			for (const WorkSegment& s : work) {
				boxes.push_back(boxOf(s));
			}
			buildSegmentGrid(boxes, grid, ws.fill);
			lastSeen.assign(work.size(), SIZE_MAX);

			for (std::size_t i = 0; i < work.size(); ++i) {
				const WorkSegment& s = work[i];

				cuts.clear();
				const CellRange r = cellRangeOf(grid, boxes[i]);
				for (std::int64_t cy = r.y0; cy <= r.y1; ++cy) {
					for (std::int64_t cx = r.x0; cx <= r.x1; ++cx) {
						const auto cell = static_cast<std::size_t>(cy * grid.cols + cx);
						for (std::size_t k = grid.cellStart[cell]; k < grid.cellStart[cell + 1]; ++k) {
							const std::size_t j = grid.entries[k];
							if (j == i || lastSeen[j] == i) {
								continue;
							}
							lastSeen[j] = i;
							if (boxesOverlap(boxes[i], boxes[j])) {
								interiorSplitPoints(s, work[j], cuts);
							}
						}
					}
				}

				if (cuts.empty()) {
					next.push_back(s);
					continue;
				}

				// Order the cut points along the segment and emit the chain of
				// sub-segments. Dedup identical cuts so a point hit by several
				// neighbors splits only once. A rounded crossing sits off the line, so
				// two cuts can share an x on a segment that is not vertical: order by
				// exact projection onto the segment, not by coordinates.
				const Vec2i64 dir = s.b - s.a;
				std::sort(cuts.begin(), cuts.end(), [&](const Vec2i64& p, const Vec2i64& q) {
					const Int128 tp = dot(p - s.a, dir);
					const Int128 tq = dot(q - s.a, dir);
					return tp != tq ? tp < tq : p < q;
				});
				cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());

				Vec2i64 prev = s.a;
				for (const Vec2i64& c : cuts) {
					if (c != prev) {
						next.push_back({prev, c, s.source});
						prev = c;
					}
				}
				if (s.b != prev) {
					next.push_back({prev, s.b, s.source});
				}
				changed = true;
			}

			work.swap(next);
		}

		// Canonicalize vertices: the unique points, sorted, indexed by position.
		out.vertices.clear();
		for (const WorkSegment& s : work) {
			out.vertices.push_back(s.a);
			out.vertices.push_back(s.b);
		}
		std::sort(out.vertices.begin(), out.vertices.end());
		out.vertices.erase(std::unique(out.vertices.begin(), out.vertices.end()), out.vertices.end());
		auto indexOf = [&](const Vec2i64& p) {
			return static_cast<std::size_t>(std::lower_bound(out.vertices.begin(), out.vertices.end(), p) - out.vertices.begin());
		};

		// Edges keyed by canonical {from,to} (from < to). Coincident pieces land on the
		// same key, so sorting gathers them and their sources merge into one edge.
		std::vector<Piece>& pieces = ws.pieces;
		pieces.clear();
		for (const WorkSegment& s : work) {
			if (s.a == s.b) {
				continue; // defensive: never emit zero-length
			}
			const std::size_t u = indexOf(s.a);
			const std::size_t v = indexOf(s.b);
			pieces.push_back({std::min(u, v), std::max(u, v), s.source});
		}
		std::sort(pieces.begin(), pieces.end());
		// Edges left from the last build keep their provenance storage.
		std::size_t edgeCount = 0;
		for (std::size_t i = 0; i < pieces.size();) {
			if (out.edges.size() == edgeCount) {
				out.edges.emplace_back();
			}
			ArrangementEdge& edge = out.edges[edgeCount++];
			edge.from			  = pieces[i].from;
			edge.to				  = pieces[i].to;
			edge.provenance.clear();
			for (; i < pieces.size() && pieces[i].from == edge.from && pieces[i].to == edge.to; ++i) {
				if (edge.provenance.empty() || edge.provenance.back() != pieces[i].source) {
					edge.provenance.push_back(pieces[i].source);
				}
			}
		}
		out.edges.resize(edgeCount);
	}

} // namespace geometry
