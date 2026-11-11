#pragma once

#include "NavMesh.h"

#include <cstdint>
#include <queue>
#include <vector>

// Reverse Resumable A* (RRA*) heuristic for the width-filtered triangle A* in
// PathQuery. A straight line to the goal under-guides the forward search around big
// concave obstacles (lakes, C-walls), so RRA* runs ONE backward search FROM the goal
// triangle and reads off a bound per triangle, expanded lazily and resumed across
// queries.
//
// The bound is a bottleneck, not a sum: a triangle's value is the least, over the
// routes from the goal triangle to it, of the farthest any portal on the route lies
// from the goal triangle's centroid. Every walk from the centroid to a point in the
// triangle crosses some route's portals, and is at least as long as its straight
// distance to each of them, so the value never exceeds the walk. Around a lake it is
// how far out the walk has to swing, which is what the straight line misses.
//
// ADMISSIBILITY: a walk from a point in triangle t to the goal point is at least the
// walk from t to the centroid less the goal point's distance from the centroid, so
// the forward search uses g(t) less the goal triangle's centroid radius; that never
// overestimates the funnelled score it is compared with. The search walks the
// TERRAIN graph (terrainTraversable() triangles: floor or ANY wall face open, only
// common-knowledge terrain blocks), WIDTH-UNFILTERED, so every agent's walk is one of
// its routes and one cache keyed by the goal triangle serves every belief and disc
// size. A node terrain-disconnected from the goal gets +inf.

namespace geometry::nav {

	// Resumable backward-search state for ONE goal triangle. `g` holds each triangle's
	// bound, final once `settled`. The open-set persists across queries so a second
	// query to the same goal RESUMES rather than restarts. Rebuilt when the mesh epoch
	// changes (triangle ids go stale).
	//
	// NOT thread-safe: rraHeuristic() mutates the cache. NavigationSystem drives all
	// queries from the single-threaded main loop, so no locking is needed.
	struct RraCache {
		std::int32_t  goalTri = -1; // the goal triangle this backward search targets
		std::uint64_t epoch	  = 0;	// the mesh epoch g and settled were built against

		std::vector<double> g;		 // bound per triangle (+inf until reached)
		std::vector<char>	settled; // 1 once a triangle's g is final (popped)

		// Open-set node: bound, then the triangle's and its entry edge's coordinates, so
		// ties resolve on geometry and not on slot ids; std::priority_queue is a max-heap,
		// so the comparisons are reversed.
		struct Node {
			double		 dist;
			Vec2i64		 corners; // sum of the triangle's corners
			Vec2i64		 entry;	  // sum of the entry edge's ends
			std::int32_t tri;
		};
		struct NodeWorse {
			bool operator()(const Node& a, const Node& b) const {
				if (a.dist != b.dist) {
					return a.dist > b.dist;
				}
				if (a.corners != b.corners) {
					return b.corners < a.corners;
				}
				return b.entry < a.entry;
			}
		};
		std::priority_queue<Node, std::vector<Node>, NodeWorse> open;

		bool initialized = false; // g/settled sized and goal seeded into the open-set
	};

	// The bound from cache.goalTri to triangle `tri` (see above), expanding the
	// resumable search on demand. +inf if `tri` is terrain-disconnected from the goal
	// (or out of range). The cache must already target the located goal triangle
	// (cache.goalTri == that goal); the caller (pathThrough / NavigationSystem)
	// guarantees this. Cheap when `tri` is already settled (a table read).
	double rraHeuristic(RraCache& cache, const NavView& mesh, std::int32_t tri);

} // namespace geometry::nav
