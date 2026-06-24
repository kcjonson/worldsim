#pragma once

#include "NavMesh.h"

#include <cstdint>
#include <limits>
#include <queue>
#include <vector>

// Reverse Resumable A* (RRA*) heuristic for the width-filtered triangle A* in
// PathQuery. The forward search wants h(t) = the exact remaining graph-distance
// from triangle t to the goal; a straight-line centroid estimate badly
// under-guides around big concave obstacles (lakes, C-walls), so A* explores a
// large fan. RRA* instead runs ONE backward Dijkstra FROM the goal and reads off
// exact distances, expanded lazily and resumed across queries.
//
// ADMISSIBILITY (why one reverse search serves EVERY forward query): the reverse
// search runs on the TERRAIN graph (terrainTraversable() triangles -- floor or
// ANY wall face open, only common-knowledge terrain blocks), WIDTH-UNFILTERED
// (every terrain adjacency is an edge, edge widths ignored), with the SAME
// centroid-to-centroid `distanceD` cost the forward search uses. Any agent's true
// remaining cost is a shortest path on a SUBGRAPH of that terrain graph (belief-
// and width-filtered) with identical edge costs, and a subgraph shortest path is
// >= the supergraph shortest path, so h never overestimates -- admissible for
// every belief and every radius. It is consistent too (an exact graph distance),
// so A* stays optimal and never re-expands.
//
// BELIEF- AND RADIUS-AGNOSTIC: built width-unfiltered on the terrain graph, the
// reverse distances depend only on the GOAL triangle, so a single cache keyed by
// the goal triangle serves every agent regardless of belief or disc size. A node
// genuinely terrain-disconnected from the goal gets h = +inf (the forward search
// can never productively reach it, since forward-reachable is a subset of
// terrain-reachable).

namespace geometry::nav {

	// Resumable reverse-search state for ONE goal triangle. `g` holds the exact
	// terrain-graph distance from each triangle to the goal, filled lazily as
	// rraHeuristic() expands the backward Dijkstra; `settled` marks a triangle whose
	// distance is final (popped from the reverse open-set). The open-set persists
	// across queries so a second query to the same goal RESUMES rather than restarts.
	// The cache is keyed (by the engine) on goalTri alone; rebuild the cache when the
	// mesh changes (triangle indices go stale).
	//
	// NOT thread-safe: rraHeuristic() mutates the cache. NavigationSystem drives all
	// queries from the single-threaded main loop, so no locking is needed.
	struct RraCache {
		std::int32_t goalTri = -1; // the goal triangle this reverse search targets

		std::vector<double> g;		 // distance-to-goal per triangle (+inf until reached)
		std::vector<char>	settled; // 1 once a triangle's g is final (popped)

		// Reverse open-set node: (tentative distance-to-goal, triangle id). Ordered by
		// distance, then by triangle id, mirroring the forward search's deterministic
		// (smaller-id-first) tie-break; std::priority_queue is a max-heap, so the
		// comparisons are reversed.
		struct Node {
			double		 dist;
			std::int32_t tri;
		};
		struct NodeWorse {
			bool operator()(const Node& a, const Node& b) const {
				if (a.dist != b.dist) {
					return a.dist > b.dist; // larger distance is "worse" -> lower priority
				}
				return a.tri > b.tri; // tie-break: larger id is "worse"
			}
		};
		std::priority_queue<Node, std::vector<Node>, NodeWorse> open;

		bool initialized = false; // g/settled sized and goal seeded into the open-set
	};

	// Exact terrain-graph distance from triangle `tri` to cache.goalTri, expanding the
	// resumable reverse search on demand. Returns +inf if `tri` is terrain-disconnected
	// from the goal (or out of range). The cache must already target the located goal
	// triangle (cache.goalTri == that goal); the caller (pathThrough / NavigationSystem)
	// guarantees this. Cheap when `tri` is already settled (a table read); otherwise pops
	// and expands reverse nodes until `tri` settles or the open-set empties.
	double rraHeuristic(RraCache& cache, const NavMesh& mesh, std::int32_t tri);

} // namespace geometry::nav
