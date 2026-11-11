#include "PathQuery.h"

#include "../core/Vec2i64.h"
#include "../predicates/Predicates.h"
#include "NavMesh.h"
#include "RraCache.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <queue>
#include <vector>

namespace geometry::nav {

	namespace {

		struct Vec2d {
			double x = 0.0;
			double y = 0.0;
		};

		Vec2d toD(const Vec2i64& p) {
			return {static_cast<double>(p.x), static_cast<double>(p.y)};
		}

		Vec2d operator+(const Vec2d& a, const Vec2d& b) {
			return {a.x + b.x, a.y + b.y};
		}
		Vec2d operator-(const Vec2d& a, const Vec2d& b) {
			return {a.x - b.x, a.y - b.y};
		}
		Vec2d operator*(const Vec2d& a, double s) {
			return {a.x * s, a.y * s};
		}

		bool same(const Vec2d& a, const Vec2d& b) {
			return a.x == b.x && a.y == b.y;
		}

		double lengthD(const Vec2d& a) {
			return std::sqrt(a.x * a.x + a.y * a.y);
		}

		double distanceD(const Vec2d& a, const Vec2d& b) {
			return lengthD(b - a);
		}

		// Signed doubled area of triangle (a, b, c); >0 when CCW. Matches the
		// orientation predicate's sign convention but in double for the funnel's
		// shape math only.
		double area2(const Vec2d& a, const Vec2d& b, const Vec2d& c) {
			return (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
		}

		double distanceToSegment(const Vec2d& p, const Vec2d& a, const Vec2d& b) {
			const Vec2d	 d	  = b - a;
			const double len2 = d.x * d.x + d.y * d.y;
			if (len2 <= 0.0) {
				return distanceD(p, a);
			}
			const double u = std::clamp(((p.x - a.x) * d.x + (p.y - a.y) * d.y) / len2, 0.0, 1.0);
			return distanceD(p, a + d * u);
		}

		Vec2i64 cornerSum(const NavView& mesh, const NavTriangle& t) {
			return mesh.vertex(t.v[0]) + mesh.vertex(t.v[1]) + mesh.vertex(t.v[2]);
		}

		Vec2i64 edgeSum(const NavView& mesh, const NavTriangle& t, int e) {
			return mesh.vertex(t.v[e]) + mesh.vertex(t.v[(e + 1) % 3]);
		}

		// Locate which edge of `from` faces neighbor `to`.
		int sharedEdgeIndex(const NavTriangle& from, std::int32_t to) {
			for (int e = 0; e < 3; ++e) {
				if (from.neighbor[e] == to) {
					return e;
				}
			}
			return -1;
		}

		// The apex (local vertex index) shared by two edges of a triangle: the vertex
		// common to edge e1 = (v[e1],v[e1+1]) and e2 = (v[e2],v[e2+1]). For two distinct
		// edges of a triangle exactly one vertex is shared; that vertex keys the Demyen
		// edge-pair width for the passage that enters one edge and leaves the other.
		int apexVertexOfEdgePair(int e1, int e2) {
			const int a0 = e1, a1 = (e1 + 1) % 3;
			const int b0 = e2, b1 = (e2 + 1) % 3;
			if (a0 == b0 || a0 == b1) {
				return a0;
			}
			return a1; // a1 must be the shared vertex (distinct edges share one vertex)
		}

		// Does triangle `t` admit a disc of diameter `2*radius` passing between the edges
		// meeting at apex `apexLocal`? Demyen edge-pair width vs common-knowledge
		// obstacles. kUnconstrainedWidth always admits. Exact: width is the floored mm
		// clearance, and floor(width) >= 2r iff width >= 2r for the integer threshold.
		bool widthAdmits(const NavTriangle& t, int apexLocal, std::int64_t diameterMm) {
			return t.edgePairWidthMm[apexLocal] >= diameterMm;
		}

		Vec2i64 roundToMm(const Vec2d& p) {
			return {static_cast<std::int64_t>(std::llround(p.x)), static_cast<std::int64_t>(std::llround(p.y))};
		}

		// Is triangle `t` crossable for this belief? The truth-vs-belief predicate from
		// pathfinding-architecture section 5; the door-span trick (a pathable door's
		// footprint is a blocked face carrying faceOpening) is what makes a truth query
		// reproduce v1: solid wall blocks, door passes.
		bool traversable(const NavTriangle& t, const BeliefFilter& belief) {
			if (isFloorFace(t)) {
				return true; // real floor
			}
			if (isCommonKnowledgeTerrainFace(t)) {
				return false; // negative sentinel: common-knowledge terrain (or a junction
							   // with no incident-wall id) -- always blocks, filter or not
			}
			// faceBlocker > 0: a wall segment id (a junction tagged with an incident wall
			// lands here too, so it is belief-gated like that wall).
			const std::uint64_t segment = static_cast<std::uint64_t>(t.faceBlocker);
			if (belief.knownSegments == nullptr) {
				// TRUTH: the wall is known solid unless this face is a door span.
				return t.faceOpening != kNoOpening;
			}
			// BELIEF: an unseen wall is absent (walkable); a seen wall blocks unless the
			// agent also knows a door through it on this very face.
			if (belief.knownSegments->count(segment) == 0) {
				return true;
			}
			return t.faceOpening != kNoOpening && belief.knownOpenings != nullptr &&
				   belief.knownOpenings->count(static_cast<std::uint64_t>(t.faceOpening)) != 0;
		}

		// The reachability forest the belief selects: truth (AI goal validity) when the
		// agent knows everything, else terrain (the sound reject for every belief). A
		// belief query never uses the truth forest, because an agent optimistically
		// routing through unseen walls can reach triangles the truth forest walls off.
		const ReachabilityForest* forestFor(const NavView& mesh, const BeliefFilter& belief) {
			return mesh.forest(belief.knownSegments == nullptr);
		}

		// pathThrough()'s sound short-circuit, the same test reaches() makes. Both
		// triangles are already located and known traversable. Returns true when the
		// forest PROVES no disc of `diameterMm` can get from startTri to goalTri:
		// different component (any diameter), or the path bottleneck < the diameter.
		bool reachabilityRejects(
			const NavView& mesh, std::int32_t startTri, std::int32_t goalTri, std::int64_t diameterMm,
			const BeliefFilter& belief) {
			const ReachabilityForest* forest = forestFor(mesh, belief);
			return forest != nullptr && !inForestSubtree(*forest, reachRootInForest(*forest, startTri, diameterMm), goalTri);
		}

		[[nodiscard]] std::int64_t discDiameterMm(std::int64_t agentRadiusMm) {
			return (agentRadiusMm > 0) ? agentRadiusMm * 2 : 0;
		}

		// Collapse consecutive duplicate points and points collinear with their
		// neighbours (the middle of three collinear points carries no information).
		void appendTaut(std::vector<Vec2i64>& out, const Vec2i64& p) {
			if (!out.empty() && out.back() == p) {
				return;
			}
			if (out.size() >= 2) {
				const Vec2i64& a = out[out.size() - 2];
				const Vec2i64& b = out[out.size() - 1];
				if (orientation(a, b, p) == Orientation::Collinear) {
					// b is redundant: a, b, p are collinear; the funnel advances
					// monotonically, so p extends past b along the same ray.
					out.back() = p;
					return;
				}
			}
			out.push_back(p);
		}

		// Does a disc need to keep its radius from corner i of triangle t? Only if some
		// triangle around that vertex is one this belief cannot cross, or the vertex is
		// on the mesh boundary. A vertex in open ground (a seam's split vertex, a free
		// diagonal's end) is no obstacle, and pulling a portal in from it would bend
		// paths at nothing.
		bool isCorner(const NavView& mesh, std::int32_t t, int i, const BeliefFilter& belief) {
			std::int32_t cur = t;
			int			 li	 = i;
			for (int guard = 0; guard < 256; ++guard) {
				const NavTriangle& tri = mesh.triangle(cur);
				if (!traversable(tri, belief)) {
					return true;
				}
				const std::uint32_t v  = tri.v[li];
				const std::int32_t	nb = tri.neighbor[(li + 2) % 3]; // across the edge ending at v
				if (nb < 0) {
					return true;
				}
				if (nb == t) {
					return false;
				}
				const NavTriangle& next = mesh.triangle(nb);
				li						= next.v[0] == v ? 0 : (next.v[1] == v ? 1 : 2);
				cur						= nb;
			}
			return true;
		}

		// Edge e of triangle t as a portal, in the string-pull's terms: the edge runs
		// v[e] -> v[e+1] with t's interior on its left, so walking out through it the
		// tail is `left` and the head `right` under the funnel's sign convention. Each
		// corner end is pulled toward the other by the radius, at most half the edge, so
		// the taut path keeps that clearance from the walls.
		//
		// CLEARANCE CAVEAT: this per-endpoint inset along the portal edge approximates
		// the true radius-offset (Minkowski) shortest path. When a leg rounds an obstacle
		// vertex at an oblique angle, the emitted polyline can under-clear that vertex by
		// a few percent of the radius. The routing DECISION is sound (the width gates
		// never route a disc through a gap narrower than its diameter); only the emitted
		// geometry is approximate near oblique corners.
		struct PortalSpan {
			Vec2d left;
			Vec2d right;
		};

		PortalSpan portalSpan(const NavView& mesh, std::int32_t t, int e, double radius, const BeliefFilter& belief) {
			const NavTriangle& tri = mesh.triangle(t);
			PortalSpan		   span{toD(mesh.vertex(tri.v[e])), toD(mesh.vertex(tri.v[(e + 1) % 3]))};
			if (radius <= 0.0) {
				return span;
			}
			const Vec2d	 dir = span.right - span.left;
			const double len = lengthD(dir);
			if (len <= 0.0) {
				return span;
			}
			const Vec2d	 unit = dir * (1.0 / len);
			const double off  = std::min(radius, len * 0.5);
			const Vec2d	 tail = span.left;
			if (isCorner(mesh, t, e, belief)) {
				span.left = tail + unit * off;
			}
			if (isCorner(mesh, t, (e + 1) % 3, belief)) {
				span.right = span.right - unit * off;
			}
			return span;
		}

		// A portal as the string-pull reads it, with the search label that enters
		// through it (-1 where none does: the final string-pull).
		struct Portal {
			Vec2d		 left;
			Vec2d		 right;
			std::int32_t label = -1;
		};

		// A corridor's funnel: the last corner its taut path bends at (apex), the path
		// length to it, and the cone of directions from the apex that pass every portal
		// since, bounded by `left` and `right`. A bound equal to the apex is open. Each
		// point records the label whose portal it came from, so a bend can look back.
		struct Funnel {
			Vec2d		 apex;
			Vec2d		 left;
			Vec2d		 right;
			double		 length	   = 0.0;
			std::int32_t apexFrom  = -1;
			std::int32_t leftFrom  = -1;
			std::int32_t rightFrom = -1;
		};

		Funnel funnelAt(const Vec2d& p, std::int32_t state) {
			return {p, p, p, 0.0, state, state, state};
		}

		// Mononen's "simple stupid funnel" string-pull of `portals` into `f`: each
		// portal narrows the cone, and one wholly past a bound bends the path at that
		// bound, which becomes the apex; the pull then resumes from the portal after the
		// one that bound came from. A bound from before `portals` resumes at the start,
		// so the cone after it is only as narrow as the portals given; the result says
		// whether that happened. `bends` (when given) collects the bend points.
		bool pull(Funnel& f, const std::vector<Portal>& portals, std::vector<Vec2i64>* bends) {
			bool		 lostHistory = false;
			std::int64_t leftIdx	 = -1;
			std::int64_t rightIdx	 = -1;
			auto		 bendAt		 = [&](const Vec2d& p, std::int32_t from, std::int64_t idx) {
				if (bends != nullptr) {
					appendTaut(*bends, roundToMm(p));
				}
				f.length += distanceD(f.apex, p);
				f.apex		= p;
				f.apexFrom	= from;
				f.left		= p;
				f.right		= p;
				f.leftFrom	= from;
				f.rightFrom = from;
				leftIdx		= idx;
				rightIdx	= idx;
				lostHistory = lostHistory || idx < 0;
				return idx + 1;
			};
			for (std::int64_t i = 0; i < static_cast<std::int64_t>(portals.size()); ++i) {
				const Portal& p = portals[static_cast<std::size_t>(i)];
				// Tighten the right bound.
				if (area2(f.apex, f.right, p.right) <= 0.0) {
					if (same(f.apex, f.right) || area2(f.apex, f.left, p.right) > 0.0) {
						f.right		= p.right;
						f.rightFrom = p.label;
						rightIdx	= i;
					} else {
						// Right crosses left: the left bound becomes the apex.
						i = bendAt(f.left, f.leftFrom, leftIdx) - 1;
						continue;
					}
				}
				// Tighten the left bound.
				if (area2(f.apex, f.left, p.left) >= 0.0) {
					if (same(f.apex, f.left) || area2(f.apex, f.right, p.left) < 0.0) {
						f.left	   = p.left;
						f.leftFrom = p.label;
						leftIdx	   = i;
					} else {
						// Left crosses right: the right bound becomes the apex.
						i = bendAt(f.right, f.rightFrom, rightIdx) - 1;
						continue;
					}
				}
			}
			return lostHistory;
		}

		// The funnel's length on to p: through the cone straight when p is inside it,
		// else around the bound p lies past. Never more than the taut path to p through
		// the funnel's portals and then open ground.
		double lengthTo(const Funnel& f, const Vec2d& p) {
			if (!same(f.left, f.apex) && area2(f.apex, f.left, p) < 0.0) {
				return f.length + distanceD(f.apex, f.left) + distanceD(f.left, p);
			}
			if (!same(f.right, f.apex) && area2(f.apex, f.right, p) > 0.0) {
				return f.length + distanceD(f.apex, f.right) + distanceD(f.right, p);
			}
			return f.length + distanceD(f.apex, p);
		}

		// A search label: one corridor's funnel reaching a state (triangle, entry edge),
		// with the portal it entered by and the label it came from.
		struct Label {
			Funnel		 funnel;
			Portal		 entry;
			double		 best	  = 0.0;
			std::int32_t state	  = -1;
			std::int32_t cameFrom = -1;
			bool		 closed	  = false;
		};

		// A state holds up to kLabelsPerState corridors at once. Corridors that bend at
		// different corners can each be the better one depending on the portals still
		// to come, so one is dropped only when another is at least as short to every
		// point of the shared portal (its ends and middle), or, with the state full,
		// when it scores worst.
		constexpr int kLabelsPerState = 2;

		struct StateLabels {
			std::array<std::int32_t, kLabelsPerState> ids{};
			int										  count = 0;
		};

		// Labels and the states holding them, kept between queries on this thread so a
		// query costs what it expands, not the mesh's size. A state's entry is this
		// query's when its stamp is the current one.
		struct SearchScratch {
			std::vector<Label>		   labels;
			std::vector<StateLabels>   states;
			std::vector<std::uint32_t> stamp;
			std::uint32_t			   current = 0;
			std::vector<Portal>		   replay;

			StateLabels& held(std::int32_t state) {
				const std::size_t s = static_cast<std::size_t>(state);
				if (stamp[s] != current) {
					stamp[s]		= current;
					states[s].count = 0;
				}
				return states[s];
			}
		};

		SearchScratch& searchScratch(std::size_t stateCount) {
			thread_local SearchScratch scratch;
			scratch.labels.clear();
			if (scratch.stamp.size() < stateCount) {
				scratch.stamp.resize(stateCount, 0);
				scratch.states.resize(stateCount);
			}
			if (++scratch.current == 0) {
				std::fill(scratch.stamp.begin(), scratch.stamp.end(), 0);
				scratch.current = 1;
			}
			return scratch;
		}

		// Is `a` at least as short as `b` to each sample point of the portal they share?
		bool noLonger(const Funnel& a, const Funnel& b, const Portal& portal) {
			const Vec2d mid = (portal.left + portal.right) * 0.5;
			for (const Vec2d& p : {portal.left, portal.right, mid}) {
				if (lengthTo(a, p) > lengthTo(b, p)) {
					return false;
				}
			}
			return true;
		}

		// The distance between segment [a, b] and the (closed, CCW) triangle: 0 when they
		// meet, else the least of each end's distance to the triangle's edges and each
		// corner's distance to the segment.
		double segmentToTriangle(const Vec2d& a, const Vec2d& b, const std::array<Vec2d, 3>& tri) {
			auto inside = [&](const Vec2d& p) {
				return area2(tri[0], tri[1], p) >= 0.0 && area2(tri[1], tri[2], p) >= 0.0 && area2(tri[2], tri[0], p) >= 0.0;
			};
			if (inside(a) || inside(b)) {
				return 0.0;
			}
			double best = std::numeric_limits<double>::infinity();
			for (int i = 0; i < 3; ++i) {
				const Vec2d& p = tri[static_cast<std::size_t>(i)];
				const Vec2d& q = tri[static_cast<std::size_t>((i + 1) % 3)];
				const double ap = area2(a, b, p);
				const double aq = area2(a, b, q);
				const double pa = area2(p, q, a);
				const double pb = area2(p, q, b);
				if (((ap <= 0.0 && aq >= 0.0) || (ap >= 0.0 && aq <= 0.0)) && ((pa <= 0.0 && pb >= 0.0) || (pa >= 0.0 && pb <= 0.0))) {
					return 0.0; // an edge of the triangle crosses or touches the segment
				}
				best = std::min({best, distanceToSegment(p, a, b), distanceToSegment(a, p, q), distanceToSegment(b, p, q)});
			}
			return best;
		}

	} // namespace

	double rraHeuristic(RraCache& cache, const NavView& mesh, std::int32_t tri) {
		const std::int32_t n	= static_cast<std::int32_t>(mesh.triangleSlots());
		const double	   kInf = std::numeric_limits<double>::infinity();

		if (tri < 0 || tri >= n) {
			return kInf; // out of range: not a node
		}

		// First touch for this goal, or the mesh changed since: size the tables and seed
		// the search with the goal triangle at 0. A patch can grow the slots and reuse
		// ids, so values from another epoch are neither in range nor sound; the cache
		// only steers the search, so starting over is always correct.
		if (!cache.initialized || cache.epoch != mesh.epoch()) {
			cache.epoch = mesh.epoch();
			cache.g.assign(static_cast<std::size_t>(n), kInf);
			cache.settled.assign(static_cast<std::size_t>(n), 0);
			cache.open = {};
			if (cache.goalTri >= 0 && cache.goalTri < n) {
				const Vec2i64 sum = cornerSum(mesh, mesh.triangle(cache.goalTri));
				cache.g[static_cast<std::size_t>(cache.goalTri)] = 0.0;
				cache.open.push({0.0, sum, sum, cache.goalTri});
			}
			cache.initialized = true;
		}

		if (cache.settled[static_cast<std::size_t>(tri)]) {
			return cache.g[static_cast<std::size_t>(tri)];
		}

		// Resume the bottleneck search until `tri` settles or the open-set drains: a
		// triangle's value is the least, over the routes reaching it, of the farthest the
		// route's portals come from the centroid, so values only grow along a route and
		// the first pop of a triangle is final.
		const NavTriangle&		   goal = mesh.triangle(cache.goalTri);
		const std::array<Vec2d, 3> goalCorners{toD(mesh.vertex(goal.v[0])), toD(mesh.vertex(goal.v[1])), toD(mesh.vertex(goal.v[2]))};
		while (!cache.open.empty()) {
			const RraCache::Node cur = cache.open.top();
			cache.open.pop();
			const std::int32_t ct = cur.tri;
			if (cache.settled[static_cast<std::size_t>(ct)]) {
				continue;
			}
			cache.settled[static_cast<std::size_t>(ct)] = 1;

			const NavTriangle& t = mesh.triangle(ct);
			for (int e = 0; e < 3; ++e) {
				const std::int32_t nb = t.neighbor[e];
				if (nb < 0 || !terrainTraversable(mesh.triangle(nb)) || cache.settled[static_cast<std::size_t>(nb)]) {
					continue;
				}
				const double reach =
					std::max(cur.dist, segmentToTriangle(toD(mesh.vertex(t.v[e])), toD(mesh.vertex(t.v[(e + 1) % 3])), goalCorners));
				if (reach < cache.g[static_cast<std::size_t>(nb)]) {
					cache.g[static_cast<std::size_t>(nb)] = reach;
					cache.open.push({reach, cornerSum(mesh, mesh.triangle(nb)), edgeSum(mesh, t, e), nb});
				}
			}

			if (ct == tri) {
				return cur.dist;
			}
		}

		// Open-set drained without settling `tri`: terrain-disconnected from the goal.
		return cache.g[static_cast<std::size_t>(tri)];
	}

	std::int32_t locateTriangle(const NavMesh& mesh, const Vec2i64& p) {
		const NavGrid& g = mesh.grid;
		// Empty mesh or grid not built (no triangles): off-mesh.
		if (g.cols == 0 || g.rows == 0) {
			return -1;
		}
		// Reject p outside the mesh AABB immediately.
		if (p.x < g.minPt.x || p.y < g.minPt.y || p.x > g.maxPt.x || p.y > g.maxPt.y) {
			return -1;
		}
		// Map p to its grid cell (safe: p is within [minPt, maxPt]).
		const std::int32_t col = static_cast<std::int32_t>((p.x - g.minPt.x) / g.cellSize);
		const std::int32_t row = static_cast<std::int32_t>((p.y - g.minPt.y) / g.cellSize);
		// Clamp to valid cell range (can happen when p == maxPt exactly).
		const std::int32_t c	   = col < g.cols ? col : g.cols - 1;
		const std::int32_t r	   = row < g.rows ? row : g.rows - 1;
		const std::int32_t cellIdx = r * g.cols + c;
		const std::int32_t begin   = g.cellStart[static_cast<std::size_t>(cellIdx)];
		const std::int32_t end	   = g.cellStart[static_cast<std::size_t>(cellIdx) + 1];
		// A point on an edge or vertex is in several triangles: the one whose sorted
		// corners come first answers, so the answer is the geometry's, as
		// PatchableMesh::locate's is.
		std::int32_t		   best = -1;
		std::array<Vec2i64, 3> bestCorners;
		for (std::int32_t i = begin; i < end; ++i) {
			const std::int32_t ti = g.candidates[static_cast<std::size_t>(i)];
			const NavTriangle& t  = mesh.triangles[static_cast<std::size_t>(ti)];
			const Vec2i64&	   v0 = mesh.vertices[t.v[0]];
			const Vec2i64&	   v1 = mesh.vertices[t.v[1]];
			const Vec2i64&	   v2 = mesh.vertices[t.v[2]];
			const Orientation  o0 = orientation(v0, v1, p);
			const Orientation  o1 = orientation(v1, v2, p);
			const Orientation  o2 = orientation(v2, v0, p);
			if (o0 == Orientation::Clockwise || o1 == Orientation::Clockwise || o2 == Orientation::Clockwise) {
				continue;
			}
			if (o0 == Orientation::CounterClockwise && o1 == Orientation::CounterClockwise && o2 == Orientation::CounterClockwise) {
				return ti;
			}
			std::array<Vec2i64, 3> corners{v0, v1, v2};
			std::sort(corners.begin(), corners.end());
			if (best < 0 || corners < bestCorners) {
				best		= ti;
				bestCorners = corners;
			}
		}
		return best;
	}

	ReachFrom reachFrom(const NavView& mesh, const Vec2i64& start, std::int64_t agentRadiusMm, BeliefFilter belief) {
		assert(forestFor(mesh, belief) != nullptr && "reachFrom: the mesh has no reachability forests");
		ReachFrom from;
		if (forestFor(mesh, belief) == nullptr) {
			return from; // reaches() answers "maybe" without forests
		}
		const std::int32_t startTri = mesh.locate(start);
		// A start on a face this belief cannot stand on reaches nothing, same as pathThrough's
		// endpoint gate (e.g. start inside a wall the agent knows blocks).
		if (startTri < 0 || !traversable(mesh.triangle(startTri), belief)) {
			return from;
		}
		from.startTri = startTri;
		from.root	  = reachRootInForest(*forestFor(mesh, belief), startTri, discDiameterMm(agentRadiusMm));
		return from;
	}

	bool reaches(const NavView& mesh, const ReachFrom& from, const Vec2i64& goal, BeliefFilter belief) {
		assert(forestFor(mesh, belief) != nullptr && "reaches: the mesh has no reachability forests");
		if (forestFor(mesh, belief) == nullptr) {
			return true; // nothing proves the goal unreachable
		}
		if (from.startTri < 0) {
			return false; // off-mesh or untraversable start
		}
		const std::int32_t goalTri = mesh.locate(goal);
		if (goalTri < 0 || !traversable(mesh.triangle(goalTri), belief)) {
			return false;
		}
		if (goalTri == from.startTri) {
			return true; // same triangle: trivially reachable
		}
		// `false` = the forest proves unreachability (sound); otherwise "maybe".
		return inForestSubtree(*forestFor(mesh, belief), from.root, goalTri);
	}

	PathResult pathThrough(const NavView& mesh, const Vec2i64& start, const Vec2i64& goal, std::int64_t agentRadiusMm,
						   BeliefFilter belief, RraCache* rra) {
		PathResult result;
		assert(mesh.widthsCurrent() && "pathThrough: corridor widths lag behind a patch");
		if (!mesh.widthsCurrent()) {
			return result; // stale widths could route a disc through a gap it does not fit
		}

		const std::int32_t startTri = mesh.locate(start);
		const std::int32_t goalTri	= mesh.locate(goal);
		if (startTri < 0 || goalTri < 0) {
			return result; // off-mesh
		}

		// Reject a query whose endpoints sit on an untraversable face for this belief
		// (e.g. start inside a wall the agent knows blocks): no path. Without this, A*
		// could exit a blocked start through a floor neighbor and "escape" a wall.
		if (!traversable(mesh.triangle(startTri), belief) || !traversable(mesh.triangle(goalTri), belief)) {
			return result;
		}

		if (startTri == goalTri) {
			result.reachable = true;
			result.points	 = {start, goal};
			return result;
		}

		// Disc diameter the width gates compare against. Computed once; both the
		// reachability short-circuit and the A* width filter use this exact value, so
		// they never disagree on the threshold.
		const std::int64_t diameterMm = discDiameterMm(agentRadiusMm);

		// Width-aware reachability short-circuit (P3.3): reject a provably-unreachable
		// goal before paying for the search. Sound: a `true` from the forest is "maybe"
		// and falls through to the A* (which decides for real); a reject is a proof no
		// path admits this disc.
		if (reachabilityRejects(mesh, startTri, goalTri, diameterMm, belief)) {
			return result; // empty PathResult: definitely unreachable
		}

		// --- Triangle A* over the dual graph, width-filtered -------------------
		// Search state is (triangle, entry-edge): the squeeze the agent must clear to
		// LEAVE a triangle depends on which edge it ENTERED by (Demyen edge-pair width
		// keyed by the apex shared by the entry and exit edges), so the same triangle is
		// distinct per entry edge. entry == 3 is the start triangle (no entry edge, no
		// squeeze). At most 4 states per triangle.
		//
		// A label is one corridor reaching a state, carrying the corridor's funnel (see
		// Funnel), scored by the funnel's length on to the goal: what the corridor's taut
		// path would be if the rest were open ground, and never more than it. A corridor's
		// score depends on where its walls and corners are, not on how its open ground is
		// cut into triangles, so seams and free diagonals do not steer the search. A state
		// keeps up to kLabelsPerState labels (see there); the first label to pop in the
		// goal triangle is the corridor the string-pull walks.
		const int kStartEntry = 3;
		auto	  stateId	  = [](std::int32_t tri, int entry) { return tri * 4 + entry; };

		SearchScratch& scratch = searchScratch(static_cast<std::size_t>(mesh.triangleSlots()) * 4);

		const Vec2d	 goalD	= toD(goal);
		const double radius = static_cast<double>(agentRadiusMm);

		// RRA* applies only when a cache is supplied AND it targets THIS goal triangle.
		const bool useRra	 = (rra != nullptr) && (rra->goalTri == goalTri);
		auto	   scoreFrom = [&](const Funnel& f, std::int32_t tri) {
			const double through = lengthTo(f, goalD);
			if (!useRra) {
				return through;
			}
			// The rest of the walk is a walk from `tri` to the goal triangle.
			return std::max(through, f.length + rraHeuristic(*rra, mesh, tri));
		};

		// Open-set node: score, then geometry, so equal scores resolve the same way
		// whatever slots the triangles sit in: the triangle's corners, then the entry
		// edge's. std::priority_queue is a max-heap, hence the reversed comparisons.
		struct Node {
			double		 f;
			Vec2i64		 corners; // sum of the triangle's corners
			Vec2i64		 entry;	  // sum of the entry edge's ends (the start point twice for the start)
			Vec2d		 apex;
			std::int32_t label;
		};
		struct NodeWorse {
			bool operator()(const Node& a, const Node& b) const {
				if (a.f != b.f) {
					return a.f > b.f;
				}
				if (a.corners != b.corners) {
					return b.corners < a.corners;
				}
				if (a.entry != b.entry) {
					return b.entry < a.entry;
				}
				if (a.apex.x != b.apex.x) {
					return a.apex.x > b.apex.x;
				}
				return a.apex.y > b.apex.y;
			}
		};
		std::priority_queue<Node, std::vector<Node>, NodeWorse> open;

		const std::int32_t startState = stateId(startTri, kStartEntry);
		const Vec2d		   startD	  = toD(start);
		{
			Label first;
			first.funnel = funnelAt(startD, 0);
			first.entry	 = {startD, startD, 0};
			first.best	 = scoreFrom(first.funnel, startTri);
			first.state	 = startState;
			scratch.labels.push_back(first);
			StateLabels& held = scratch.held(startState);
			held.ids[0]		  = 0;
			held.count		  = 1;
			open.push({first.best, cornerSum(mesh, mesh.triangle(startTri)), start + start, startD, 0});
		}
		result.peakOpenSet = static_cast<std::int64_t>(open.size());
		std::vector<Portal>& replay = scratch.replay;

		std::int32_t goalLabel = -1;
		while (!open.empty()) {
			const Node cur = open.top();
			open.pop();
			const std::int32_t li = cur.label;
			if (scratch.labels[static_cast<std::size_t>(li)].closed || cur.f > scratch.labels[static_cast<std::size_t>(li)].best) {
				continue; // stale entry (we push duplicates instead of decrease-key)
			}
			scratch.labels[static_cast<std::size_t>(li)].closed = true;
			++result.nodesExpanded; // a label closed exactly once: one real expansion
			const std::int32_t s	 = scratch.labels[static_cast<std::size_t>(li)].state;
			const std::int32_t ti	 = s / 4;
			const int		   entry = s % 4;
			if (ti == goalTri) {
				goalLabel = li;
				break;
			}

			const NavTriangle& tri	  = mesh.triangle(ti);
			const Funnel	   funnel = scratch.labels[static_cast<std::size_t>(li)].funnel;
			for (int e = 0; e < 3; ++e) {
				if (e == entry) {
					continue; // U-turn back through the entry edge: never on a taut path
				}
				const std::int32_t nb = tri.neighbor[e];
				if (nb < 0) {
					continue;
				}
				// Belief gate: skip a neighbor this agent may not cross. The only place
				// truth/belief changes routing; the funnel below is untouched.
				if (!traversable(mesh.triangle(nb), belief)) {
					continue;
				}
				// Width gate (intra-triangle squeeze): to leave `ti` via edge e having
				// entered by `entry`, the disc must clear the Demyen width of the edge
				// pair meeting at their shared apex. The start state has no entry edge,
				// so nothing to clear inside the start triangle.
				if (entry != kStartEntry && !widthAdmits(tri, apexVertexOfEdgePair(entry, e), diameterMm)) {
					continue;
				}
				// Width gate (door portal): a door edge is gated by its clear width.
				if (tri.edgeOpening[e] != kNoOpening && tri.edgeClearWidthMm[e] < diameterMm) {
					continue;
				}

				// The edge of `nb` that faces `ti` is nb's entry edge for this step.
				const int back = sharedEdgeIndex(mesh.triangle(nb), ti);
				if (back < 0) {
					continue; // adjacency inconsistency; skip safely
				}
				const std::int32_t ns	 = stateId(nb, back);
				const std::int32_t label = static_cast<std::int32_t>(scratch.labels.size()); // the label a new one would take
				const PortalSpan   span	 = portalSpan(mesh, ti, e, radius, belief);
				const Portal	   step{span.left, span.right, label};
				Funnel			   next = funnel;
				replay.assign(1, step);
				if (pull(next, replay, nullptr)) {
					// The path bends at a corner from an earlier portal: pull again from
					// that corner through every portal since, as the string-pull would.
					replay.clear();
					for (std::int32_t at = li; at != next.apexFrom && at >= 0; at = scratch.labels[static_cast<std::size_t>(at)].cameFrom) {
						replay.push_back(scratch.labels[static_cast<std::size_t>(at)].entry);
					}
					std::reverse(replay.begin(), replay.end());
					replay.push_back(step);
					Funnel restart = funnelAt(next.apex, next.apexFrom);
					restart.length = next.length;
					pull(restart, replay, nullptr);
					next = restart;
				}
				const double score = scoreFrom(next, nb);
				StateLabels& held = scratch.held(ns);
				std::int32_t slot = -1; // the label this corridor takes over, if any
				bool		 beaten = false;
				for (int k = 0; k < held.count && !beaten; ++k) {
					Label& other = scratch.labels[static_cast<std::size_t>(held.ids[static_cast<std::size_t>(k)])];
					if (noLonger(other.funnel, next, step)) {
						beaten = true;
					} else if (!other.closed && noLonger(next, other.funnel, step)) {
						if (slot < 0) {
							slot = held.ids[static_cast<std::size_t>(k)];
						} else {
							other.closed = true; // dominated twice over: retire it
						}
					}
				}
				if (beaten) {
					continue;
				}
				if (slot < 0 && held.count == kLabelsPerState) {
					// Full: take over the worst-scoring open corridor if this one scores better.
					for (int k = 0; k < held.count; ++k) {
						const std::int32_t id = held.ids[static_cast<std::size_t>(k)];
						const Label&	   other = scratch.labels[static_cast<std::size_t>(id)];
						if (!other.closed && other.best > score && (slot < 0 || other.best > scratch.labels[static_cast<std::size_t>(slot)].best)) {
							slot = id;
						}
					}
					if (slot < 0) {
						continue;
					}
				}
				Label fresh;
				fresh.funnel   = next;
				fresh.entry	   = step;
				fresh.best	   = score;
				fresh.state	   = ns;
				fresh.cameFrom = li;
				if (slot < 0) {
					slot = label;
					held.ids[static_cast<std::size_t>(held.count++)] = label;
					scratch.labels.push_back(fresh);
				} else {
					// The new corridor takes over an open label; the portal it records as
					// its own must name the label it now is.
					fresh.entry.label = slot;
					if (fresh.funnel.leftFrom == label) {
						fresh.funnel.leftFrom = slot;
					}
					if (fresh.funnel.rightFrom == label) {
						fresh.funnel.rightFrom = slot;
					}
					if (fresh.funnel.apexFrom == label) {
						fresh.funnel.apexFrom = slot;
					}
					scratch.labels[static_cast<std::size_t>(slot)] = fresh;
				}
				open.push({score, cornerSum(mesh, mesh.triangle(nb)), edgeSum(mesh, tri, e), next.apex, slot});
				const std::int64_t sz = static_cast<std::int64_t>(open.size());
				if (sz > result.peakOpenSet) {
					result.peakOpenSet = sz;
				}
			}
		}

		if (goalLabel < 0) {
			return result; // disconnected (or every corridor too narrow for the agent)
		}

		// Reconstruct the triangle corridor start..goal.
		std::vector<std::int32_t> corridor;
		for (std::int32_t at = goalLabel; at != -1; at = scratch.labels[static_cast<std::size_t>(at)].cameFrom) {
			corridor.push_back(scratch.labels[static_cast<std::size_t>(at)].state / 4);
		}
		std::reverse(corridor.begin(), corridor.end());

		// --- String-pull the corridor ------------------------------------------
		std::vector<Portal> portals;
		portals.reserve(corridor.size());
		for (std::size_t i = 0; i + 1 < corridor.size(); ++i) {
			const int e = sharedEdgeIndex(mesh.triangle(corridor[i]), corridor[i + 1]);
			if (e < 0) {
				// Corridor adjacency is built from neighbor[], so this is unreachable;
				// bail safely rather than indexing past the edge.
				return result;
			}
			const PortalSpan span = portalSpan(mesh, corridor[i], e, radius, belief);
			portals.push_back({span.left, span.right, -1});
		}
		portals.push_back({goalD, goalD, -1});
		std::vector<Vec2i64> pts{start};
		Funnel				 taut = funnelAt(toD(start), -1);
		pull(taut, portals, &pts);
		appendTaut(pts, goal);

		result.reachable = true;
		result.points	 = std::move(pts);
		return result;
	}

	ReachFrom reachFrom(const NavMesh& mesh, const Vec2i64& start, std::int64_t agentRadiusMm, BeliefFilter belief) {
		return reachFrom(navView(mesh), start, agentRadiusMm, belief);
	}

	bool reaches(const NavMesh& mesh, const ReachFrom& from, const Vec2i64& goal, BeliefFilter belief) {
		return reaches(navView(mesh), from, goal, belief);
	}

	PathResult pathThrough(const NavMesh& mesh, const Vec2i64& start, const Vec2i64& goal, std::int64_t agentRadiusMm,
						   BeliefFilter belief, RraCache* rra) {
		return pathThrough(navView(mesh), start, goal, agentRadiusMm, belief, rra);
	}

} // namespace geometry::nav
