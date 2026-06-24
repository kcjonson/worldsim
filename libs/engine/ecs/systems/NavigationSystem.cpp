#include "NavigationSystem.h"

#include "nav/NavCoords.h"
#include "nav/NavInputBuilder.h"

#include <assets/AssetRegistry.h>
#include <assets/ConstructionRegistry.h>
#include <assets/placement/PlacementExecutor.h>
#include <construction/ConstructionWorld.h>

#include <nav/PathQuery.h>

#include <utils/Log.h>

#include <world/camera/WorldCamera.h>
#include <world/chunk/Chunk.h>
#include <world/chunk/ChunkManager.h>

#include <algorithm>
#include <chrono>
#include <cmath>

namespace ecs {

	namespace {
		namespace gnav = geometry::nav;

		// Pad (mm) around the construction-world bounds when synthesizing the
		// fallback walkable border. The border must enclose the wall bands with a
		// margin, otherwise an outside-the-room query point lands off-mesh.
		constexpr std::int64_t kConstructionBorderPadMm = 2000;
	} // namespace

	NavigationSystem::~NavigationSystem() {
		// Block on any in-flight build so the worker can't outlive this object and
		// touch the moved-in (now-destroyed) input or write into a dead future.
		if (future.valid()) {
			future.wait();
		}
	}

	bool NavigationSystem::needsRebuild() const {
		if (!haveBuiltOnce) {
			return true;
		}
		const std::uint64_t version = (constructionWorld != nullptr) ? constructionWorld->version() : 0;
		return version != builtVersion;
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

		if (!needsRebuild()) {
			return;
		}

		// Snapshot the input ON THE MAIN THREAD: buildInput reads ConstructionWorld
		// (NOT thread-safe) and the per-chunk placement/tile data, so the extraction
		// must run here. The worker then owns a self-contained NavMeshInput by value.
		gnav::NavMeshInput input;

		// The area path needs a ChunkManager AND a camera to anchor the area. With a
		// camera, build over the simulation area; without one, fall back to the
		// construction-only path (headless tests, before the camera is wired) so the
		// walls are still navigable. We must NOT latch haveBuiltOnce when we cannot
		// produce a real build, or no later change would retrigger it.
		const bool canBuildArea = (chunkManager != nullptr) && (camera != nullptr);

		if (canBuildArea) {
			// Anchor the area at the camera on the first build and hold it there (Phase A;
			// snap-on-scroll re-centers it later).
			if (!haveSimArea) {
				const engine::world::WorldPosition pos = camera->position();
				simAreaCenter = engine::nav::toMm(glm::vec2{pos.x, pos.y});
				haveSimArea = true;
			}

			// Clamp the half-extent so the area never reaches past the loaded-chunk
			// extent: chunks load in a radius around the camera, so the farthest in-bounds
			// point is loadRadius full chunks out plus the half-chunk the camera sits in.
			// A request beyond that would sweep tiles in never-loaded chunks (read as land
			// -- harmless, but wasted work).
			const std::int64_t chunkMm	  = static_cast<std::int64_t>(engine::world::kChunkSize) * 1000;
			const std::int64_t loadRadius = static_cast<std::int64_t>(chunkManager->loadRadius());
			const std::int64_t maxRadius  = loadRadius * chunkMm - chunkMm / 2;
			const std::int64_t radius	  = std::max<std::int64_t>(0, std::min(kSimAreaRadiusMm, maxRadius));

			engine::assets::PlacementExecutor  emptyPlacement(engine::assets::AssetRegistry::Get());
			engine::assets::PlacementExecutor& exec = (placement != nullptr) ? *placement : emptyPlacement;
			input = engine::nav::buildInput(simAreaCenter, radius, *chunkManager, exec, engine::assets::AssetRegistry::Get(),
											*constructionWorld, engine::assets::ConstructionRegistry::Get());
		} else {
			// Construction-only fallback: no chunks/camera, so there is no terrain border.
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
		// against THIS input's version, not a later-mutated world.
		builtVersion = constructionWorld->version();
		haveBuiltOnce = true;

		// Heavy triangulation off the main thread; the lambda owns `input` by value
		// (moved) so there's no dangling reference to main-thread state.
		future = std::async(std::launch::async,
							[input = std::move(input)]() { return gnav::buildNavMesh(input); });
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

		const geometry::Vec2i64 startMm = engine::nav::toMm(startMeters);
		const geometry::Vec2i64 goalMm  = engine::nav::toMm(goalMeters);
		const std::int64_t radiusMm =
			static_cast<std::int64_t>(std::llround(static_cast<double>(agentRadiusMeters) * 1000.0));

		return gnav::reachable(navMesh, startMm, goalMm, radiusMm, belief);
	}

} // namespace ecs
