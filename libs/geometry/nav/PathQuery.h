#pragma once

#include "../core/Vec2i64.h"
#include "NavMesh.h"

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
	// Terrain/junction blockers (faceBlocker < 0) ignore the filter and always block.
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
	PathResult pathThrough(const NavMesh& mesh, const Vec2i64& start, const Vec2i64& goal, std::int64_t agentRadiusMm,
						   BeliefFilter belief = {});

} // namespace geometry::nav
