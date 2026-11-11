#pragma once

#include "../core/Vec2i64.h"
#include "NavMesh.h"
#include "RraCache.h"

#include <cstdint>
#include <unordered_map>
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
// functions, fixed evaluation order), and search ties and point location on a shared
// edge are settled by coordinates, never slot ids, so the same ground gives the same
// points whatever edit history built the mesh.

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

		// What the walk costs: its length, with each piece through a face crossed at a
		// price (BeliefFilter::passableAtCost) weighted by that face's multiplier. Equal to
		// the length when no such face is crossed.
		double cost = 0.0;
		// The walk runs through a face crossed at a price (flora it pushes through, a
		// waiting mover's footprint): the caller acts on it before the mover sets off.
		bool crossesPricedFaces = false;
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
	//
	// passableAtCost (optional) makes some blocked faces crossable at a price: a face this
	// filter would block, whose faceBlocker tag is a key, may be walked through, each mm
	// inside it costing its multiplier (>= 1) where open ground costs 1. The search then
	// takes the cheapest walk: round by free ground while that is less than the
	// multiplier times the way through, through otherwise. Flora that seals a goal, or a
	// mover waiting in a doorway, is passable this way rather than a wall. The stored
	// corridor widths and the reachability forests treat such faces as obstacles, so next
	// to one the width gates are skipped, and with any in play the forest pre-check and
	// the RRA heuristic are too (they would wall the goal off).
	struct BeliefFilter {
		const std::unordered_set<std::uint64_t>*		knownSegments  = nullptr;
		const std::unordered_set<std::uint64_t>*		knownOpenings  = nullptr;
		const std::unordered_map<std::int64_t, double>* passableAtCost = nullptr;
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
	// The search scores each corridor by a lower bound on its walk (its funnel so far,
	// then the best way on through the portal it last crossed) and accepts a goal only
	// at its exact string-pulled length (or cost), so it returns the shortest walk of the
	// corridors it keeps (see PathQuery.cpp's label cap).
	//
	// `rra` is an optional Reverse Resumable A* cache (P3.5). When non-null AND its
	// goalTri matches the located goal triangle, its bound (rraHeuristic) raises the
	// scores where it is the larger. The cache is belief- and radius-agnostic (built on
	// the width-unfiltered terrain graph) so one cache per goal serves every agent; it
	// resumes across calls. Width filter, reachability short-circuit and funnel are the
	// same either way.
	PathResult pathThrough(const NavMesh& mesh, const Vec2i64& start, const Vec2i64& goal, std::int64_t agentRadiusMm,
						   BeliefFilter belief = {}, RraCache* rra = nullptr);
	// Any mesh through its view (see NavView). Without forests in the view the
	// reachability pre-check is skipped and the A* decides alone.
	PathResult pathThrough(const NavView& mesh, const Vec2i64& start, const Vec2i64& goal, std::int64_t agentRadiusMm,
						   BeliefFilter belief = {}, RraCache* rra = nullptr);

	// Sound, cheap reachability test (P3.3): can a disc of radius agentRadiusMm POSSIBLY get from
	// start to goal under `belief`? Uses the precomputed reachability forest, so it rejects a
	// provably-unreachable goal without running A*. reachFrom does the start-side work once
	// (locate, endpoint gate, the disc's reach root in the forest); reaches() tests one goal against
	// it with a locate and an O(log n) subtree check, so many goals from one start cost one climb.
	// Pass reaches() the belief reachFrom took.
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
	struct ReachFrom {
		std::int32_t startTri = -1; // -1: off-mesh or untraversable start, reaches nothing
		std::int32_t root	  = -1; // forest node whose subtree the disc can reach; -1 for a non-node start
	};
	ReachFrom reachFrom(const NavMesh& mesh, const Vec2i64& start, std::int64_t agentRadiusMm, BeliefFilter belief = {});
	bool	  reaches(const NavMesh& mesh, const ReachFrom& from, const Vec2i64& goal, BeliefFilter belief = {});
	// The view must carry the forests.
	ReachFrom reachFrom(const NavView& mesh, const Vec2i64& start, std::int64_t agentRadiusMm, BeliefFilter belief = {});
	bool	  reaches(const NavView& mesh, const ReachFrom& from, const Vec2i64& goal, BeliefFilter belief = {});

} // namespace geometry::nav
