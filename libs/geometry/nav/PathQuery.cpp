#include "PathQuery.h"

#include "../core/Vec2i64.h"
#include "../predicates/Predicates.h"
#include "NavMesh.h"

#include <algorithm>
#include <array>
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

		Vec2i64 centroidI(const NavMesh& mesh, const NavTriangle& t) {
			const Vec2i64& a = mesh.vertices[t.v[0]];
			const Vec2i64& b = mesh.vertices[t.v[1]];
			const Vec2i64& c = mesh.vertices[t.v[2]];
			return {(a.x + b.x + c.x) / 3, (a.y + b.y + c.y) / 3};
		}

		// The two vertex indices shared by edge `e` of triangle `t` (the portal to
		// neighbor t.neighbor[e]): vertices v[e] and v[(e+1)%3].
		struct Portal {
			std::uint32_t a = 0;
			std::uint32_t b = 0;
		};

		// Locate which edge of `from` faces neighbor `to`.
		int sharedEdgeIndex(const NavTriangle& from, std::int32_t to) {
			for (int e = 0; e < 3; ++e) {
				if (from.neighbor[e] == to) {
					return e;
				}
			}
			return -1;
		}

		Vec2i64 roundToMm(const Vec2d& p) {
			return {static_cast<std::int64_t>(std::llround(p.x)), static_cast<std::int64_t>(std::llround(p.y))};
		}

		// Is triangle `t` crossable for this belief? The truth-vs-belief predicate from
		// pathfinding-architecture section 5; the door-span trick (a pathable door's
		// footprint is a blocked face carrying faceOpening) is what makes a truth query
		// reproduce v1: solid wall blocks, door passes.
		bool traversable(const NavTriangle& t, const BeliefFilter& belief) {
			if (t.faceBlocker == kNoBlocker) {
				return true; // real floor
			}
			if (t.faceBlocker < 0) {
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
					// b is redundant: a, b, p are collinear. Replace b with p only if p
					// extends past b along the same ray (it always does here, the
					// funnel advances monotonically), so just drop b.
					out.back() = p;
					return;
				}
			}
			out.push_back(p);
		}

	} // namespace

	std::int32_t locateTriangle(const NavMesh& mesh, const Vec2i64& p) {
		for (std::int32_t ti = 0; ti < static_cast<std::int32_t>(mesh.triangles.size()); ++ti) {
			const NavTriangle& t  = mesh.triangles[ti];
			const Vec2i64&	   v0 = mesh.vertices[t.v[0]];
			const Vec2i64&	   v1 = mesh.vertices[t.v[1]];
			const Vec2i64&	   v2 = mesh.vertices[t.v[2]];
			if (orientation(v0, v1, p) != Orientation::Clockwise &&
				orientation(v1, v2, p) != Orientation::Clockwise &&
				orientation(v2, v0, p) != Orientation::Clockwise) {
				return ti;
			}
		}
		return -1;
	}

	PathResult pathThrough(const NavMesh& mesh, const Vec2i64& start, const Vec2i64& goal, std::int64_t agentRadiusMm,
						   BeliefFilter belief) {
		PathResult result;

		const std::int32_t startTri = locateTriangle(mesh, start);
		const std::int32_t goalTri	= locateTriangle(mesh, goal);
		if (startTri < 0 || goalTri < 0) {
			return result; // off-mesh
		}

		// Reject a query whose endpoints sit on an untraversable face for this belief
		// (e.g. start inside a wall the agent knows blocks): no path. Without this, A*
		// could exit a blocked start through a floor neighbor and "escape" a wall.
		if (!traversable(mesh.triangles[startTri], belief) || !traversable(mesh.triangles[goalTri], belief)) {
			return result;
		}

		if (startTri == goalTri) {
			result.reachable = true;
			result.points	 = {start, goal};
			return result;
		}

		// --- Triangle A* over the dual graph ----------------------------------
		const std::int32_t n = static_cast<std::int32_t>(mesh.triangles.size());
		const double	   kInf = std::numeric_limits<double>::infinity();

		std::vector<double>		  g(n, kInf);
		std::vector<std::int32_t> cameFrom(n, -1);
		std::vector<char>		  closed(n, 0);

		const Vec2d goalC = toD(centroidI(mesh, mesh.triangles[goalTri]));

		auto heuristic = [&](std::int32_t ti) {
			return distanceD(toD(centroidI(mesh, mesh.triangles[ti])), goalC);
		};

		// Open-set node: f-score plus triangle id. The comparator orders by f, then
		// by triangle id, so equal f-scores resolve deterministically (smaller id
		// first); std::priority_queue is a max-heap, hence the reversed comparisons.
		struct Node {
			double		 f;
			std::int32_t tri;
		};
		struct NodeWorse {
			bool operator()(const Node& a, const Node& b) const {
				if (a.f != b.f) {
					return a.f > b.f; // larger f is "worse" -> lower priority
				}
				return a.tri > b.tri; // tie-break: larger id is "worse"
			}
		};
		std::priority_queue<Node, std::vector<Node>, NodeWorse> open;

		g[startTri] = 0.0;
		open.push({heuristic(startTri), startTri});

		bool found = false;
		while (!open.empty()) {
			const Node cur = open.top();
			open.pop();
			const std::int32_t ti = cur.tri;
			if (closed[ti]) {
				continue; // stale entry (we push duplicates instead of decrease-key)
			}
			closed[ti] = 1;
			if (ti == goalTri) {
				found = true;
				break;
			}

			const Vec2d ci = toD(centroidI(mesh, mesh.triangles[ti]));
			for (std::int32_t nb : mesh.triangles[ti].neighbor) {
				if (nb < 0 || closed[nb]) {
					continue;
				}
				// Belief gate: skip a neighbor this agent may not cross. This is the
				// only place truth/belief changes routing; the funnel below is untouched.
				if (!traversable(mesh.triangles[nb], belief)) {
					continue;
				}
				// Cost: centroid-to-centroid distance. Integer mesh, double cost; the
				// metric only orders the search, the funnel decides the real shape.
				const double tentative = g[ti] + distanceD(ci, toD(centroidI(mesh, mesh.triangles[nb])));
				if (tentative < g[nb]) {
					g[nb]		= tentative;
					cameFrom[nb] = ti;
					open.push({tentative + heuristic(nb), nb});
				}
			}
		}

		if (!found) {
			return result; // disconnected
		}

		// Reconstruct the triangle corridor start..goal.
		std::vector<std::int32_t> corridor;
		for (std::int32_t t = goalTri; t != -1; t = cameFrom[t]) {
			corridor.push_back(t);
		}
		std::reverse(corridor.begin(), corridor.end());

		// --- Build the portal sequence (left, right) per crossed edge ----------
		// For each adjacent pair (corridor[i], corridor[i+1]) the shared edge has two
		// vertices. Order them so `left` is CCW and `right` is CW of the travel
		// direction, the convention the Mononen funnel expects. We seed travel from
		// the start point: a portal endpoint is "left" when the segment
		// start->endpoint turns left (CCW) relative to start->otherEndpoint, decided
		// per portal by orientation against the apex of the previous portal.
		struct PortalPts {
			Vec2d left;
			Vec2d right;
		};
		std::vector<PortalPts> portals;
		portals.reserve(corridor.size());

		// First funnel point is the start.
		portals.push_back({toD(start), toD(start)});

		for (std::size_t i = 0; i + 1 < corridor.size(); ++i) {
			const NavTriangle& from = mesh.triangles[corridor[i]];
			const int		   e	= sharedEdgeIndex(from, corridor[i + 1]);
			if (e < 0) {
				// Corridor adjacency is built from neighbor[], so this is unreachable;
				// bail safely rather than indexing past the edge.
				return result;
			}
			const std::uint32_t ia = from.v[e];
			const std::uint32_t ib = from.v[(e + 1) % 3];
			Vec2i64				pa = mesh.vertices[ia];
			Vec2i64				pb = mesh.vertices[ib];

			// Directed edge pa->pb (= v[e]->v[(e+1)%3]) in a CCW triangle keeps the
			// interior on its left, so the agent exits to its RIGHT. Facing the exit
			// direction, the edge's tail `pa` is on the left and its head `pb` on the
			// right, the orientation the Mononen funnel expects.
			Vec2d leftP	 = toD(pa);
			Vec2d rightP = toD(pb);

			// Radius shrink: pull each endpoint inward (toward the portal centre) by
			// agentRadiusMm so the taut path keeps that clearance from the walls. The
			// inward direction for the left point is toward right and vice versa.
			if (agentRadiusMm > 0) {
				const Vec2d  l	   = leftP;
				const Vec2d  r	   = rightP;
				const Vec2d  dir   = r - l; // from left toward right
				const double len   = lengthD(dir);
				if (len > 0.0) {
					const Vec2d unit = dir * (1.0 / len);
					double		off	 = static_cast<double>(agentRadiusMm);
					// Clamp so the two inward offsets do not cross: each may move at
					// most half the portal width. A portal narrower than 2*radius
					// degrades to both points meeting at the midpoint (path threads the
					// middle) rather than inverting.
					if (off > len * 0.5) {
						off = len * 0.5;
					}
					leftP  = l + unit * off;	   // left moves toward right
					rightP = r - unit * off;	   // right moves toward left
				}
			}

			portals.push_back({leftP, rightP});
		}

		// Last funnel point is the goal.
		portals.push_back({toD(goal), toD(goal)});

		// --- Mononen "simple stupid funnel" string-pull ------------------------
		std::vector<Vec2i64> pts;
		pts.push_back(start);

		Vec2d		apex	  = portals[0].left; // == start
		Vec2d		portalL	  = portals[0].left;
		Vec2d		portalR	  = portals[0].right;
		std::size_t apexIdx	  = 0;
		std::size_t leftIdx	  = 0;
		std::size_t rightIdx  = 0;

		for (std::size_t i = 1; i < portals.size(); ++i) {
			const Vec2d left  = portals[i].left;
			const Vec2d right = portals[i].right;

			// Tighten the right bound.
			if (area2(apex, portalR, right) <= 0.0) {
				if (apex.x == portalR.x && apex.y == portalR.y) {
					portalR	 = right;
					rightIdx = i;
				} else if (area2(apex, portalL, right) > 0.0) {
					// Right does not cross left: tighten.
					portalR	 = right;
					rightIdx = i;
				} else {
					// Right crosses left: left bound becomes a new apex corner.
					appendTaut(pts, roundToMm(portalL));
					apex	 = portalL;
					apexIdx	 = leftIdx;
					// Reset the funnel from the new apex.
					portalL	 = apex;
					portalR	 = apex;
					leftIdx	 = apexIdx;
					rightIdx = apexIdx;
					i		 = apexIdx; // restart scan after the new apex
					continue;
				}
			}

			// Tighten the left bound.
			if (area2(apex, portalL, left) >= 0.0) {
				if (apex.x == portalL.x && apex.y == portalL.y) {
					portalL	= left;
					leftIdx	= i;
				} else if (area2(apex, portalR, left) < 0.0) {
					portalL	= left;
					leftIdx	= i;
				} else {
					// Left crosses right: right bound becomes a new apex corner.
					appendTaut(pts, roundToMm(portalR));
					apex	 = portalR;
					apexIdx	 = rightIdx;
					portalL	 = apex;
					portalR	 = apex;
					leftIdx	 = apexIdx;
					rightIdx = apexIdx;
					i		 = apexIdx;
					continue;
				}
			}
		}

		appendTaut(pts, goal);

		result.reachable = true;
		result.points	 = std::move(pts);
		return result;
	}

} // namespace geometry::nav
