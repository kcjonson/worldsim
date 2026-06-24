#pragma once

#include "../core/Vec2i64.h"
#include "NavMesh.h"
#include "RraCache.h"

#include <cstdint>
#include <unordered_set>
#include <vector>

// Path queries over a built NavMesh: locate the triangle containing a point,
// and find a taut, radius-clamped polyline from start to goal for a disc agent.
//
// EXACT / FLOAT BOUNDARY. Topology is exact via the integer predicates:
// locateTriangle uses orientation(), and the A* corridor only follows the
// mesh's integer adjacency, so reachability (start/goal on-mesh, corridor
// connectivity) is decided entirely by exact arithmetic. The path SHAPE is not:
// A* edge costs and heuristics are doubles (they only order the search), and the
// funnel does its apex/bound math in double, then rounds each emitted corner to
// the nearest millimeter. So whether a path exists is exact; where its bends sit
// is rounded-deterministic. The float work is platform-stable (no transcendental
// functions, fixed evaluation order) and the A* tie-break is by triangle index,
// so a given (mesh, start, goal, radius) yields identical points every run.

namespace geometry::nav {

	struct PathResult {
		bool				 reachable = false; // false => no path (off-mesh or disconnected)
		std::vector<Vec2i64> points;			// taut polyline start..goal in mm; empty when !reachable

		// A* instrumentation (P3.5): how hard the forward search worked. Lets a caller
		// VERIFY the RRA* heuristic cuts the explored fan vs the straight-line guide.
		// nodesExpanded counts states popped and closed (each expanded once); peakOpenSet
		// is the largest the open-set ever grew. Both stay 0 on the trivial/early-out
		// paths (off-mesh, same triangle, reachability reject) that never enter the loop.
		std::int64_t nodesExpanded = 0;
		std::int64_t peakOpenSet   = 0;
	};

	// Per-agent belief overlay on the shared truth mesh (see pathfinding-architecture
	// section 5). The mesh triangulates the whole region, wall interiors included, and
	// each triangle carries a faceBlocker/faceOpening tag; this filter decides, per
	// agent, which blocked faces that agent may cross:
	//   knownSegments == nullptr  -> TRUTH query: knows everything, reproduces v1
	//                                routing exactly (doors pass, solid walls block).
	//   knownSegments != nullptr  -> BELIEF query: a wall segment the agent has not
	//                                seen is treated as ABSENT (path goes straight
	//                                through it); a seen wall blocks unless the agent
	//                                also knows a door through it.
	// A negative faceBlocker is a common-knowledge terrain sentinel (water/tree, or a
	// junction with no incident-wall id) and always blocks, filter or not. A junction
	// tagged with a positive incident-wall segment id is belief-gated like that wall.
	struct BeliefFilter {
		const std::unordered_set<std::uint64_t>* knownSegments = nullptr;
		const std::unordered_set<std::uint64_t>* knownOpenings = nullptr;
	};

	// Triangle containing p (CCW point-in-triangle, on-edge counts as inside);
	// -1 if p is outside every triangle. Linear scan (walking-locate is a later
	// optimization behind the same signature).
	std::int32_t locateTriangle(const NavMesh& mesh, const Vec2i64& p);

	// Path for a disc of radius agentRadiusMm from start to goal. Triangle A* over
	// the dual graph, then a funnel (string-pulling) shrunk by the agent radius. The
	// A* expansion (and the start/goal triangles) are gated by `belief`: a default
	// (empty) BeliefFilter is a truth query that routes exactly as v1 did.
	//
	// `rra` is an optional Reverse Resumable A* cache (P3.5). When non-null AND its
	// goalTri matches the located goal triangle, the A* heuristic becomes the exact
	// terrain-graph distance-to-goal (rraHeuristic) -- a far tighter guide around big
	// concave obstacles that cuts the explored fan. The cache is belief- and
	// radius-agnostic (built on the width-unfiltered terrain graph) so one cache per
	// goal serves every agent; it resumes across calls. When `rra` is null (the default)
	// or its goal does not match, the heuristic falls back to the straight-line centroid
	// estimate, so the pure free function and every geometry-test keep working unchanged.
	// Cost, tie-break, width filter, reachability short-circuit, and funnel are identical
	// either way: only the heuristic VALUES change, never the routing rules.
	PathResult pathThrough(const NavMesh& mesh, const Vec2i64& start, const Vec2i64& goal, std::int64_t agentRadiusMm,
						   BeliefFilter belief = {}, RraCache* rra = nullptr);

	// Sound, cheap reachability test (P3.3): can a disc of radius agentRadiusMm
	// POSSIBLY get from start to goal under `belief`? Uses the precomputed
	// reachability forest (component + bottleneck via LCA), so it rejects a
	// provably-unreachable goal in O(log n) without running the full A*.
	//
	// Semantics are asymmetric and that asymmetry is the point:
	//   false => DEFINITELY unreachable (off-mesh, an untraversable endpoint, a
	//            different component, or the path's widest bottleneck < the disc
	//            diameter). A `false` never hides a real path -- it is sound.
	//   true  => MAYBE reachable. The forest over-approximates (bottleneck is an
	//            upper bound), so the caller must still run pathThrough for certainty.
	//
	// belief.knownSegments == nullptr selects the truth forest (AI goal validity);
	// otherwise the terrain forest, whose disconnect is sound for every belief.
	bool reachable(const NavMesh& mesh, const Vec2i64& start, const Vec2i64& goal, std::int64_t agentRadiusMm,
				   BeliefFilter belief = {});

} // namespace geometry::nav
