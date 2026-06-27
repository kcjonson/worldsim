#include "NavigationSystem.h"

#include "nav/NavCoords.h"
#include "nav/NavInputBuilder.h"

#include <assets/AssetRegistry.h>
#include <assets/ConstructionRegistry.h>
#include <assets/placement/PlacementExecutor.h>
#include <construction/ConstructionWorld.h>

#include <nav/PathQuery.h>

#include <utils/Log.h>

#include <world/chunk/Chunk.h>
#include <world/chunk/ChunkManager.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>

namespace ecs {

	namespace {
		namespace gnav = geometry::nav;

		// Pad (mm) around the construction-world bounds when synthesizing the
		// fallback walkable border. The border must enclose the wall bands with a
		// margin, otherwise an outside-the-room query point lands off-mesh.
		constexpr std::int64_t kConstructionBorderPadMm = 2000;

		// Squared distance and closest-point-on-segment, in meters. Used by
		// nearestPathablePoint; kept local and free of glm free functions.
		float dist2(glm::vec2 a, glm::vec2 b) {
			const float dx = a.x - b.x;
			const float dy = a.y - b.y;
			return dx * dx + dy * dy;
		}
		glm::vec2 closestOnSegment(glm::vec2 p, glm::vec2 a, glm::vec2 b) {
			const glm::vec2 ab	  = b - a;
			const float		denom = ab.x * ab.x + ab.y * ab.y;
			float			t	  = denom > 1e-9F ? ((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / denom : 0.0F;
			t					  = t < 0.0F ? 0.0F : (t > 1.0F ? 1.0F : t);
			return glm::vec2{a.x + t * ab.x, a.y + t * ab.y};
		}
	} // namespace

	NavigationSystem::~NavigationSystem() {
		// Block on any in-flight build so the worker can't outlive this object and
		// touch the moved-in (now-destroyed) input or write into a dead future.
		if (future.valid()) {
			future.wait();
		}
	}

	std::int64_t NavigationSystem::clampHalfExtent(std::int64_t requested) const {
		// Bound to [min, max] first.
		std::int64_t clamped = std::max(kMinSimHalfExtentMm, std::min(kMaxSimHalfExtentMm, requested));
		// Then to the loaded-chunk extent so we never sweep never-loaded tiles.
		if (chunkManager != nullptr) {
			const std::int64_t chunkMm	  = static_cast<std::int64_t>(engine::world::kChunkSize) * 1000;
			const std::int64_t loadRadius = static_cast<std::int64_t>(chunkManager->loadRadius());
			const std::int64_t maxRadius  = loadRadius * chunkMm - chunkMm / 2;
			clamped = std::max<std::int64_t>(0, std::min(clamped, maxRadius));
		}
		return clamped;
	}

	std::uint64_t NavigationSystem::areaChunkSignature(geometry::Vec2i64 center, std::int64_t halfExtent) const {
		if (processedChunks == nullptr) {
			return 0; // headless / construction-only: this trigger is inert
		}

		// Derive the chunk-coordinate range overlapping the area AABB the SAME way
		// buildInput does: floor/ceil the area bounds to tiles (+/-1 margin), then map the
		// corner tiles to chunk coords. The area covers at most ~2x2 chunks at 64 m, so the
		// loop below is trivially cheap.
		const geometry::Vec2i64 minMm{center.x - halfExtent, center.y - halfExtent};
		const geometry::Vec2i64 maxMm{center.x + halfExtent, center.y + halfExtent};
		const double	   tileMm	= static_cast<double>(engine::world::kTileSize) * 1000.0;
		const std::int64_t tileMinX = static_cast<std::int64_t>(std::floor(static_cast<double>(minMm.x) / tileMm)) - 1;
		const std::int64_t tileMinY = static_cast<std::int64_t>(std::floor(static_cast<double>(minMm.y) / tileMm)) - 1;
		const std::int64_t tileMaxX = static_cast<std::int64_t>(std::ceil(static_cast<double>(maxMm.x) / tileMm)) + 1;
		const std::int64_t tileMaxY = static_cast<std::int64_t>(std::ceil(static_cast<double>(maxMm.y) / tileMm)) + 1;
		const engine::world::ChunkCoordinate cMin =
			engine::world::worldToChunk({static_cast<float>(tileMinX), static_cast<float>(tileMinY)});
		const engine::world::ChunkCoordinate cMax =
			engine::world::worldToChunk({static_cast<float>(tileMaxX), static_cast<float>(tileMaxY)});

		// XOR-fold a per-coord hash over the in-area coords PRESENT in the processed set.
		// XOR is order-independent (the loop order can't perturb the result) and a coord
		// joining/leaving the set flips its term, so the signature changes iff the in-area
		// processed set changes.
		std::uint64_t sig = 0;
		const std::hash<engine::world::ChunkCoordinate> coordHash;
		for (std::int32_t cy = cMin.y; cy <= cMax.y; ++cy) {
			for (std::int32_t cx = cMin.x; cx <= cMax.x; ++cx) {
				const engine::world::ChunkCoordinate coord{cx, cy};
				if (processedChunks->find(coord) != processedChunks->end()) {
					sig ^= static_cast<std::uint64_t>(coordHash(coord));
				}
			}
		}
		return sig;
	}

	bool NavigationSystem::needsRebuild(std::int64_t clampedHalfExtent) const {
		if (!haveBuiltOnce) {
			return true;
		}
		const std::uint64_t version = (constructionWorld != nullptr) ? constructionWorld->version() : 0;
		if (version != builtVersion) {
			return true;
		}
		// Re-center: camera panned far enough from the built center.
		const std::int64_t dx = requestedCenter.x - builtCenter.x;
		const std::int64_t dy = requestedCenter.y - builtCenter.y;
		const std::int64_t distSq = dx * dx + dy * dy;
		const std::int64_t threshSq = kRecenterThresholdMm * kRecenterThresholdMm;
		if (distSq > threshSq) {
			return true;
		}
		// Resize: clamped half-extent changed by more than kSizeChangeThreshold.
		if (builtHalfExtent > 0) {
			const double ratio = static_cast<double>(clampedHalfExtent) / static_cast<double>(builtHalfExtent);
			const double delta = (ratio > 1.0) ? (ratio - 1.0) : (1.0 - ratio);
			if (delta > kSizeChangeThreshold) {
				return true;
			}
		} else if (clampedHalfExtent > 0) {
			return true; // first real extent after a zero-extent build
		}
		// In-area placement changed: a chunk overlapping the current clamped area joined or
		// left the processed set since the build. Without this, a stationary player whose
		// spawn-ring chunks finish placement after the first build would keep a mesh missing
		// every obstacle from those chunks until they panned/zoomed. Inert when headless.
		if (areaChunkSignature(requestedCenter, clampedHalfExtent) != builtAreaChunkSignature) {
			return true;
		}
		return false;
	}

	void NavigationSystem::update(float /*deltaTime*/) {
		// Drain a finished build first so a mesh completed last frame is queryable
		// this frame (and a fresh rebuild can be launched on top of it below).
		if (future.valid()) {
			if (future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
				navMesh = future.get(); // swap the new mesh in; old mesh is dropped
				++meshGeneration;		// a new mesh: any path stamped to the prior generation is stale
				// Triangle indices are stale after a rebuild, so the goal-triangle-keyed
				// RRA* caches must be dropped (a stale key would target the wrong triangle).
				rraCaches.clear();
				future = {};
			} else {
				return; // build still running: keep serving the old mesh, no new launch
			}
		}

		// buildInput needs the (non-thread-safe) ConstructionWorld for the walls and
		// the fallback border, so there is nothing meaningful to build without it.
		// Return WITHOUT latching haveBuiltOnce, so the first real build happens once
		// the world is wired -- otherwise an empty mesh would stick and no later
		// version change would trigger a rebuild.
		if (constructionWorld == nullptr) {
			return;
		}

		// The area path needs a ChunkManager AND a pushed area. With both, build over
		// the simulation area; without them, fall back to the construction-only path
		// (headless tests, before the area is wired) so walls are still navigable. We
		// must NOT latch haveBuiltOnce when we cannot produce a real build, or no later
		// change would retrigger it.
		const bool canBuildArea = (chunkManager != nullptr) && haveRequestedArea;

		// Clamp the requested half-extent now (used for both the rebuild check and the
		// actual build). In the fallback path this is unused but harmless.
		const std::int64_t clampedHalfExtent = canBuildArea ? clampHalfExtent(requestedHalfExtent) : 0;

		if (!needsRebuild(clampedHalfExtent)) {
			return;
		}

		// Snapshot the input ON THE MAIN THREAD: buildInput reads ConstructionWorld
		// (NOT thread-safe) and the per-chunk placement/tile data, so the extraction
		// must run here. The worker then owns a self-contained NavMeshInput by value.
		gnav::NavMeshInput input;

		if (canBuildArea) {
			engine::assets::PlacementExecutor  emptyPlacement(engine::assets::AssetRegistry::Get());
			engine::assets::PlacementExecutor& exec = (placement != nullptr) ? *placement : emptyPlacement;
			input = engine::nav::buildInput(requestedCenter, clampedHalfExtent, *chunkManager, exec,
											engine::assets::AssetRegistry::Get(), *constructionWorld,
											engine::assets::ConstructionRegistry::Get());
		} else {
			// Construction-only fallback: no chunks/area, so there is no terrain border.
			// extractWalls alone has no unblocked polygon and buildNavMesh would yield an
			// empty mesh, so synthesize one walkable border from the construction-world
			// vertex bounds and let the wall bands tile inside it.
			engine::nav::extractWalls(*constructionWorld, engine::assets::ConstructionRegistry::Get(), input.polygons,
									  input.doors);
			if (!constructionWorld->vertices().empty()) {
				geometry::Vec2i64 minMm = constructionWorld->vertices().front().pos;
				geometry::Vec2i64 maxMm = minMm;
				for (const engine::construction::Vertex& v : constructionWorld->vertices()) {
					minMm.x = std::min(minMm.x, v.pos.x);
					minMm.y = std::min(minMm.y, v.pos.y);
					maxMm.x = std::max(maxMm.x, v.pos.x);
					maxMm.y = std::max(maxMm.y, v.pos.y);
				}
				minMm.x -= kConstructionBorderPadMm;
				minMm.y -= kConstructionBorderPadMm;
				maxMm.x += kConstructionBorderPadMm;
				maxMm.y += kConstructionBorderPadMm;
				input.polygons.insert(input.polygons.begin(), engine::nav::borderRing(minMm, maxMm));
			}
		}

		// Record what we snapshotted so the next frame's needsRebuild compares
		// against THIS input's version, not a later-mutated world. The chunk signature is
		// computed over the SAME (center, clampedHalfExtent) the check uses, so an unchanged
		// world won't spuriously retrigger; a later in-area placement flips it and rebuilds.
		builtVersion = constructionWorld->version();
		builtCenter = requestedCenter;
		builtHalfExtent = clampedHalfExtent;
		builtAreaChunkSignature = areaChunkSignature(requestedCenter, clampedHalfExtent);
		haveBuiltOnce = true;

		// Heavy triangulation off the main thread; the lambda owns `input` by value
		// (moved) so there's no dangling reference to main-thread state.
		future = std::async(std::launch::async,
							[input = std::move(input)]() { return gnav::buildNavMesh(input); });
	}

	bool NavigationSystem::inSimArea(glm::vec2 meters) const {
		// No built area yet: return false so callers fall through to the no-mesh beeline.
		if (!haveBuiltOnce) {
			return false;
		}
		const geometry::Vec2i64 pMm = engine::nav::toMm(meters);
		return std::abs(pMm.x - builtCenter.x) <= builtHalfExtent
			&& std::abs(pMm.y - builtCenter.y) <= builtHalfExtent;
	}

	std::optional<std::vector<glm::vec2>>
	NavigationSystem::requestPath(glm::vec2 startMeters, glm::vec2 goalMeters, float agentRadiusMeters,
								  gnav::BeliefFilter belief) const {
		if (navMesh.triangles.empty()) {
			return std::nullopt;
		}

		const geometry::Vec2i64 startMm = engine::nav::toMm(startMeters);
		const geometry::Vec2i64 goalMm = engine::nav::toMm(goalMeters);
		// Agent clearance is honored: pathThrough shrinks the funnel by this radius
		// and rejects corridors narrower than the disc.
		const std::int64_t radiusMm =
			static_cast<std::int64_t>(std::llround(static_cast<double>(agentRadiusMeters) * 1000.0));

		// RRA* heuristic: locate the goal triangle and hand pathThrough the resumable
		// reverse-search cache for it (one per goal serves every agent; it resumes across
		// queries). When the goal is off-mesh, locate returns -1 and we pass no cache, so
		// pathThrough falls back to the straight-line heuristic and routes exactly as
		// before. The cache map is goal-triangle-keyed and is cleared on every mesh swap.
		gnav::RraCache*	   rra	   = nullptr;
		const std::int32_t goalTri = gnav::locateTriangle(navMesh, goalMm);
		if (goalTri >= 0) {
			// Bound the map: a churn of distinct goals must not grow it without limit.
			// When a NEW goal would push past the cap, drop the whole map (cheap to
			// rebuild lazily); an already-cached goal reuses its entry and never grows it.
			if (rraCaches.size() >= kMaxRraCaches && rraCaches.find(goalTri) == rraCaches.end()) {
				rraCaches.clear();
			}
			gnav::RraCache& cache = rraCaches[goalTri];
			cache.goalTri		  = goalTri; // freshly default-constructed entries start at -1
			rra					  = &cache;
		}

		// belief is applied at query time, not via a second mesh: a default (empty)
		// filter reproduces the truth query; a colonist's known-structures filter routes
		// over its remembered geometry. Consumed synchronously, so the pointers it holds
		// stay valid for the call.
		const gnav::PathResult result = gnav::pathThrough(navMesh, startMm, goalMm, radiusMm, belief, rra);

		// Aggregate A* instrumentation for a reachable solve (where the counts are
		// meaningful). LOG_DEBUG per query is fine; it compiles out in release.
		if (result.reachable) {
			++navStats.totalQueries;
			navStats.totalNodesExpanded += static_cast<std::uint64_t>(result.nodesExpanded);
			navStats.lastNodesExpanded = result.nodesExpanded;
			navStats.lastPeakOpenSet   = result.peakOpenSet;
			LOG_DEBUG(Engine, "[Nav] path query expanded=%lld peakOpen=%lld (cumulative queries=%llu nodes=%llu)",
					  static_cast<long long>(result.nodesExpanded), static_cast<long long>(result.peakOpenSet),
					  static_cast<unsigned long long>(navStats.totalQueries),
					  static_cast<unsigned long long>(navStats.totalNodesExpanded));
		}

		if (!result.reachable) {
			return std::nullopt;
		}

		std::vector<glm::vec2> waypoints;
		waypoints.reserve(result.points.size());
		for (const geometry::Vec2i64& p : result.points) {
			waypoints.push_back(engine::nav::toMeters(p));
		}
		return waypoints;
	}

	bool NavigationSystem::isReachable(glm::vec2 startMeters, glm::vec2 goalMeters, float agentRadiusMeters,
									   gnav::BeliefFilter belief) const {
		// No mesh yet: can't prove unreachable, so don't reject. Colonists beeline
		// legitimately during startup/outdoor play; blocking everything until the first
		// mesh lands would starve them of any movement. Callers that need certainty
		// can check hasMesh() first.
		if (navMesh.triangles.empty()) {
			return true;
		}

		// Off-area endpoint: the mesh covers only the simulation area. A goal (or start)
		// outside that area lies off-mesh, so the exact component/bottleneck check would
		// produce a false negative. Return true ("can't prove unreachable") -- the agent
		// will beeline toward the area, then switch to LOD0 routing once it enters.
		if (!inSimArea(startMeters) || !inSimArea(goalMeters)) {
			return true;
		}

		const geometry::Vec2i64 startMm = engine::nav::toMm(startMeters);
		const geometry::Vec2i64 goalMm  = engine::nav::toMm(goalMeters);
		const std::int64_t radiusMm =
			static_cast<std::int64_t>(std::llround(static_cast<double>(agentRadiusMeters) * 1000.0));

		return gnav::reachable(navMesh, startMm, goalMm, radiusMm, belief);
	}

	bool NavigationSystem::isOnMesh(glm::vec2 meters) const {
		if (navMesh.triangles.empty()) {
			return false;
		}
		return gnav::locateTriangle(navMesh, engine::nav::toMm(meters)) >= 0;
	}

	std::optional<glm::vec2> NavigationSystem::nearestPathablePoint(glm::vec2 meters) const {
		bool	  found	 = false;
		float	  bestD2 = 0.0F;
		glm::vec2 best{0.0F, 0.0F};
		for (const gnav::NavTriangle& t : navMesh.triangles) {
			if (!gnav::isFloorFace(t)) {
				continue; // never snap into water/wall interiors -- only real walkable floor
			}
			const glm::vec2 a = engine::nav::toMeters(navMesh.vertices[t.v[0]]);
			const glm::vec2 b = engine::nav::toMeters(navMesh.vertices[t.v[1]]);
			const glm::vec2 c = engine::nav::toMeters(navMesh.vertices[t.v[2]]);
			// The query point is off-mesh, so the closest point on this triangle lies on one of
			// its three edges. Take the nearest of the three.
			glm::vec2 cand = closestOnSegment(meters, a, b);
			float	  cd2  = dist2(meters, cand);
			const glm::vec2 p2 = closestOnSegment(meters, b, c);
			if (const float d2 = dist2(meters, p2); d2 < cd2) {
				cd2	 = d2;
				cand = p2;
			}
			const glm::vec2 p3 = closestOnSegment(meters, c, a);
			if (const float d2 = dist2(meters, p3); d2 < cd2) {
				cd2	 = d2;
				cand = p3;
			}
			if (!found || cd2 < bestD2) {
				// Nudge a hair toward the centroid so the snapped point sits unambiguously inside
				// this walkable triangle, not exactly on a boundary edge where locate is ambiguous.
				const glm::vec2 centroid = (a + b + c) / 3.0F;
				best					 = cand + (centroid - cand) * 0.05F;
				bestD2					 = cd2;
				found					 = true;
			}
		}
		return found ? std::optional<glm::vec2>(best) : std::nullopt;
	}

} // namespace ecs
