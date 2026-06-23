#include "NavigationSystem.h"

#include "nav/NavCoords.h"
#include "nav/NavInputBuilder.h"

#include <assets/AssetRegistry.h>
#include <assets/ConstructionRegistry.h>
#include <assets/placement/PlacementExecutor.h>
#include <construction/ConstructionWorld.h>

#include <nav/PathQuery.h>

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
		if (m_future.valid()) {
			m_future.wait();
		}
	}

	std::unordered_set<engine::world::ChunkCoordinate> NavigationSystem::currentBuildableChunks() const {
		std::unordered_set<engine::world::ChunkCoordinate> out;
		if (m_chunkManager == nullptr) {
			return out;
		}
		// A chunk feeds the build only if it is loaded AND fully placed (its
		// entities exist), so obstacles aren't missing from a half-built region.
		for (const engine::world::Chunk* chunk : m_chunkManager->getLoadedChunks()) {
			if (chunk == nullptr || !chunk->isReady()) {
				continue;
			}
			const engine::world::ChunkCoordinate coord = chunk->coordinate();
			if (m_processedChunks != nullptr && m_processedChunks->find(coord) == m_processedChunks->end()) {
				continue;
			}
			out.insert(coord);
		}
		return out;
	}

	bool NavigationSystem::needsRebuild(const std::unordered_set<engine::world::ChunkCoordinate>& currentChunks) const {
		if (!m_haveBuiltOnce) {
			return true;
		}
		const std::uint64_t version = (m_constructionWorld != nullptr) ? m_constructionWorld->version() : 0;
		if (version != m_builtVersion) {
			return true;
		}
		return currentChunks != m_builtChunks;
	}

	void NavigationSystem::update(float /*deltaTime*/) {
		// Drain a finished build first so a mesh completed last frame is queryable
		// this frame (and a fresh rebuild can be launched on top of it below).
		if (m_future.valid()) {
			if (m_future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
				m_mesh = m_future.get(); // swap the new mesh in; old mesh is dropped
				++m_generation;			 // a new mesh: any path stamped to the prior generation is stale
				m_future = {};
			} else {
				return; // build still running: keep serving the old mesh, no new launch
			}
		}

		// buildInput needs the (non-thread-safe) ConstructionWorld for the walls and
		// the fallback border, so there is nothing meaningful to build without it.
		// Return WITHOUT latching m_haveBuiltOnce, so the first real build happens once
		// the world is wired -- otherwise an empty mesh would stick and no later
		// version/chunk change would trigger a rebuild.
		if (m_constructionWorld == nullptr) {
			return;
		}

		const std::unordered_set<engine::world::ChunkCoordinate> currentChunks = currentBuildableChunks();
		if (!needsRebuild(currentChunks)) {
			return;
		}

		// Snapshot the input ON THE MAIN THREAD: buildInput reads ConstructionWorld
		// (NOT thread-safe) and the per-chunk placement/tile data, so the extraction
		// must run here. The worker then owns a self-contained NavMeshInput by value.
		std::vector<const engine::world::Chunk*> chunks;
		if (m_chunkManager != nullptr) {
			for (const engine::world::Chunk* chunk : m_chunkManager->getLoadedChunks()) {
				if (chunk == nullptr || !chunk->isReady()) {
					continue;
				}
				if (m_processedChunks != nullptr &&
					m_processedChunks->find(chunk->coordinate()) == m_processedChunks->end()) {
					continue;
				}
				chunks.push_back(chunk);
			}
		}

		gnav::NavMeshInput input;
		if (m_constructionWorld != nullptr) {
			// buildInput needs a PlacementExecutor for per-chunk flora obstacles. When
			// none is wired (construction-only path, e.g. before placement streams in
			// or in a headless test) a default-constructed executor returns no chunk
			// index for any coord, so the build cleanly skips flora rather than crashing.
			engine::assets::PlacementExecutor  emptyPlacement(engine::assets::AssetRegistry::Get());
			engine::assets::PlacementExecutor& placement = (m_placement != nullptr) ? *m_placement : emptyPlacement;
			input = engine::nav::buildInput(chunks, placement, engine::assets::AssetRegistry::Get(),
											*m_constructionWorld, engine::assets::ConstructionRegistry::Get());

			// buildInput only emits the walkable border when at least one ready chunk
			// supplied bounds. With no chunks (construction-only, e.g. headless tests
			// or before terrain streams in) the input would have no unblocked polygon
			// and buildNavMesh would yield an empty mesh. Synthesize one walkable
			// border from the construction-world vertex bounds so the walls are still
			// navigable. This is the only border source when chunks are absent; with
			// chunks present buildInput's border already covers the region.
			bool haveBorder = false;
			for (const gnav::NavInputPolygon& p : input.polygons) {
				if (!p.blocked) {
					haveBorder = true;
					break;
				}
			}
			if (!haveBorder && !m_constructionWorld->vertices().empty()) {
				geometry::Vec2i64 minMm = m_constructionWorld->vertices().front().pos;
				geometry::Vec2i64 maxMm = minMm;
				for (const engine::construction::Vertex& v : m_constructionWorld->vertices()) {
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
		// against THIS input, not a later-mutated world.
		m_builtVersion = (m_constructionWorld != nullptr) ? m_constructionWorld->version() : 0;
		m_builtChunks = currentChunks;
		m_haveBuiltOnce = true;

		// Heavy triangulation off the main thread; the lambda owns `input` by value
		// (moved) so there's no dangling reference to main-thread state.
		m_future = std::async(std::launch::async,
							   [input = std::move(input)]() { return gnav::buildNavMesh(input); });
	}

	std::optional<std::vector<glm::vec2>>
	NavigationSystem::requestPath(glm::vec2 startMeters, glm::vec2 goalMeters, float agentRadiusMeters,
								  gnav::BeliefFilter belief) const {
		if (m_mesh.triangles.empty()) {
			return std::nullopt;
		}

		const geometry::Vec2i64 startMm = engine::nav::toMm(startMeters);
		const geometry::Vec2i64 goalMm = engine::nav::toMm(goalMeters);
		// Agent clearance is honored: pathThrough shrinks the funnel by this radius
		// and rejects corridors narrower than the disc.
		const std::int64_t radiusMm =
			static_cast<std::int64_t>(std::llround(static_cast<double>(agentRadiusMeters) * 1000.0));

		// belief is applied at query time, not via a second mesh: a default (empty)
		// filter reproduces the truth query; a colonist's known-structures filter routes
		// over its remembered geometry. Consumed synchronously, so the pointers it holds
		// stay valid for the call.
		const gnav::PathResult result = gnav::pathThrough(m_mesh, startMm, goalMm, radiusMm, belief);
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
		if (m_mesh.triangles.empty()) {
			return true;
		}

		const geometry::Vec2i64 startMm = engine::nav::toMm(startMeters);
		const geometry::Vec2i64 goalMm  = engine::nav::toMm(goalMeters);
		const std::int64_t radiusMm =
			static_cast<std::int64_t>(std::llround(static_cast<double>(agentRadiusMeters) * 1000.0));

		return gnav::reachable(m_mesh, startMm, goalMm, radiusMm, belief);
	}

} // namespace ecs
