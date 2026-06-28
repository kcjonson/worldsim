#include "NavigationSystem.h"

#include "../World.h"
#include "../components/Colonist.h"
#include "../components/Transform.h"

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

		// Nearest walkable point on a single mesh to `meters`, or nullopt when the mesh
		// has no walkable floor. Shared by nearestPathablePoint after region dispatch.
		std::optional<glm::vec2> nearestPathableOnMesh(const gnav::NavMesh& navMesh, glm::vec2 meters) {
			bool	  found	 = false;
			float	  bestD2 = 0.0F;
			glm::vec2 best{0.0F, 0.0F};
			for (const gnav::NavTriangle& t : navMesh.triangles) {
				if (!gnav::terrainTraversable(t)) {
					continue; // skip common-knowledge blockers (water/tree); snap only onto walkable ground
				}
				const glm::vec2 a = engine::nav::toMeters(navMesh.vertices[t.v[0]]);
				const glm::vec2 b = engine::nav::toMeters(navMesh.vertices[t.v[1]]);
				const glm::vec2 c = engine::nav::toMeters(navMesh.vertices[t.v[2]]);
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
					// Nudge a hair toward the centroid so the snapped point sits unambiguously
					// inside this walkable triangle, not on a boundary edge where locate is ambiguous.
					const glm::vec2 centroid = (a + b + c) / 3.0F;
					best					 = cand + (centroid - cand) * 0.05F;
					bestD2					 = cd2;
					found					 = true;
				}
			}
			return found ? std::optional<glm::vec2>(best) : std::nullopt;
		}
	} // namespace

	std::vector<SimAabb> clusterAabbs(const std::vector<SimAabb>& boxes) {
		// Union-find over overlap. Small n (a handful of colonists + one viewport), so an
		// O(n^2) merge with a fixed-point sweep is plenty and stays order-independent.
		std::vector<SimAabb> clusters;
		clusters.reserve(boxes.size());
		for (const SimAabb& b : boxes) {
			SimAabb merged	  = b;
			bool	mergedAny = true;
			// Repeatedly absorb any existing cluster the (growing) box overlaps, removing
			// it from the list, until no more overlaps -- so a box bridging two clusters
			// collapses all three into one.
			while (mergedAny) {
				mergedAny = false;
				for (std::size_t i = 0; i < clusters.size();) {
					if (merged.overlaps(clusters[i])) {
						merged.unionWith(clusters[i]);
						clusters[i] = clusters.back();
						clusters.pop_back();
						mergedAny = true;
					} else {
						++i;
					}
				}
			}
			clusters.push_back(merged);
		}
		return clusters;
	}

	NavigationSystem::~NavigationSystem() {
		// Block on any in-flight builds so no worker outlives this object and touches the
		// moved-in (now-destroyed) input or writes into a dead future.
		for (SimulationRegion& r : regions) {
			if (r.future.valid()) {
				r.future.wait();
			}
		}
	}

	std::int64_t NavigationSystem::clampHalfExtent(std::int64_t requested) const {
		std::int64_t clamped = std::max(kMinSimHalfExtentMm, std::min(kMaxSimHalfExtentMm, requested));
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

	bool NavigationSystem::regionObstaclesChanged(const SimulationRegion& region) const {
		const std::uint64_t version = (constructionWorld != nullptr) ? constructionWorld->version() : 0;
		if (version != region.builtVersion) {
			return true;
		}
		if (areaChunkSignature(region.center, region.halfExtent) != region.builtAreaChunkSignature) {
			return true;
		}
		return false;
	}

	std::vector<NavigationSystem::DesiredRegion> NavigationSystem::computeDesiredRegions() const {
		// A driver is a point (colonist position or viewport center) and its own square.
		struct Driver {
			geometry::Vec2i64 center;
			SimAabb			  box;
		};
		std::vector<Driver> drivers;

		// One square per colonist: center +/- kSimRadiusMm. ECS view is colonist-only; the
		// world is always wired when this runs in-game (null only in headless tests with no
		// colonists, where the viewport drives the regions instead).
		if (world != nullptr) {
			for (auto [entity, position, colonist] : world->view<Position, Colonist>()) {
				(void)entity;
				(void)colonist;
				const geometry::Vec2i64 c = engine::nav::toMm(position.value);
				drivers.push_back(
					{c, {c.x - kSimRadiusMm, c.y - kSimRadiusMm, c.x + kSimRadiusMm, c.y + kSimRadiusMm}});
			}
		}

		// One square for the viewport, half-extents clamped so a zoomed-out viewport doesn't
		// ask for an unbuildable region. The viewport square is independent of any colonist
		// square; clustering merges them only when they actually overlap.
		if (haveViewport) {
			const geometry::Vec2i64 c  = engine::nav::toMm(viewportCenterM);
			const std::int64_t		hx = clampHalfExtent(
				 static_cast<std::int64_t>(std::llround(static_cast<double>(viewportHalfM.x) * 1000.0)));
			const std::int64_t hy = clampHalfExtent(
				static_cast<std::int64_t>(std::llround(static_cast<double>(viewportHalfM.y) * 1000.0)));
			drivers.push_back({c, {c.x - hx, c.y - hy, c.x + hx, c.y + hy}});
		}

		// Cluster the driver squares (transitive overlap -> one bounding rect), carrying each
		// driver's center into its cluster. Same union-find sweep as clusterAabbs, but on
		// DesiredRegion so the driver points ride along for the self-gate.
		std::vector<DesiredRegion> clusters;
		for (const Driver& dr : drivers) {
			DesiredRegion merged;
			merged.rect = dr.box;
			merged.drivers.push_back(dr.center);
			bool mergedAny = true;
			while (mergedAny) {
				mergedAny = false;
				for (std::size_t i = 0; i < clusters.size();) {
					if (merged.rect.overlaps(clusters[i].rect)) {
						merged.rect.unionWith(clusters[i].rect);
						merged.drivers.insert(merged.drivers.end(), clusters[i].drivers.begin(),
											  clusters[i].drivers.end());
						clusters[i] = std::move(clusters.back());
						clusters.pop_back();
						mergedAny = true;
					} else {
						++i;
					}
				}
			}
			clusters.push_back(std::move(merged));
		}
		return clusters;
	}

	void NavigationSystem::launchBuild(SimulationRegion& region) {
		// Snapshot the input ON THE MAIN THREAD: buildInput reads ConstructionWorld (NOT
		// thread-safe) and per-chunk placement/tile data, so the extraction must run here.
		// The worker then owns a self-contained NavMeshInput by value.
		gnav::NavMeshInput input;
		const auto		   inputStart = std::chrono::steady_clock::now();

		engine::assets::PlacementExecutor  emptyPlacement(engine::assets::AssetRegistry::Get());
		engine::assets::PlacementExecutor& exec = (placement != nullptr) ? *placement : emptyPlacement;
		input = engine::nav::buildInput(region.center, region.halfExtent, *chunkManager, exec,
										engine::assets::AssetRegistry::Get(), *constructionWorld,
										engine::assets::ConstructionRegistry::Get());

		region.builtVersion				= constructionWorld->version();
		region.builtAreaChunkSignature	= areaChunkSignature(region.center, region.halfExtent);

		const double inputMs =
			std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - inputStart).count();
		std::size_t walkableBorders = 0;
		for (const gnav::NavInputPolygon& p : input.polygons) {
			if (!p.blocked) {
				++walkableBorders;
			}
		}
		LOG_INFO(Engine, "[NavBuild] region %d buildInput %.2f ms: polys=%zu walkableBorders=%zu blockedRings=%zu",
				 region.id, inputMs, input.polygons.size(), walkableBorders, input.polygons.size() - walkableBorders);

		region.future = std::async(std::launch::async, [input = std::move(input), id = region.id]() {
			const auto			   buildStart = std::chrono::steady_clock::now();
			geometry::nav::NavMesh m		  = gnav::buildNavMesh(input);
			const double		   buildMs =
				std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - buildStart).count();
			LOG_INFO(Engine, "[NavBuild] region %d buildNavMesh %.2f ms: tris=%zu verts=%zu", id, buildMs,
					 m.triangles.size(), m.vertices.size());
			return m;
		});
	}

	void NavigationSystem::drainFinishedBuilds() {
		for (SimulationRegion& region : regions) {
			if (!region.future.valid()) {
				continue;
			}
			if (region.future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
				continue; // still building: keep serving the old mesh
			}
			region.navMesh = region.future.get(); // swap the new mesh in; old mesh is dropped
			region.future	= {};
			++region.meshGeneration;
			meshGeneration = std::max(meshGeneration, region.meshGeneration);

			std::size_t walkable = 0;
			std::size_t floor	 = 0;
			for (const gnav::NavTriangle& t : region.navMesh.triangles) {
				if (gnav::isFloorFace(t)) {
					++floor;
				}
				if (gnav::terrainTraversable(t)) {
					++walkable;
				}
			}
			LOG_INFO(Engine,
					 "[NavBuild] region %d mesh swapped gen=%llu tris=%zu walkable=%zu floor=%zu blocked=%zu",
					 region.id, static_cast<unsigned long long>(region.meshGeneration), region.navMesh.triangles.size(),
					 walkable, floor, region.navMesh.triangles.size() - walkable);

			// Triangle indices are stale after this region's rebuild, so drop its RRA caches.
			for (auto it = rraCaches.begin(); it != rraCaches.end();) {
				if (it->first.region == region.id) {
					it = rraCaches.erase(it);
				} else {
					++it;
				}
			}
		}
	}

	void NavigationSystem::reconcileRegions(const std::vector<DesiredRegion>& desired) {
		std::vector<bool> regionMatched(regions.size(), false);
		std::vector<bool> desiredHandled(desired.size(), false);

		// A driver point is "comfortably inside" a built rect when it sits >= kEdgeMarginMm
		// from every edge. This is the movement-tied self-gate: a region is reused untouched
		// only while none of its drivers nears its edge.
		auto driverInsideMargin = [](geometry::Vec2i64 p, const SimAabb& have) {
			return p.x >= have.minX + kEdgeMarginMm && p.x <= have.maxX - kEdgeMarginMm
				&& p.y >= have.minY + kEdgeMarginMm && p.y <= have.maxY - kEdgeMarginMm;
		};

		// Pass 1: a desired region whose EVERY driver sits comfortably inside an existing
		// region's built rect keeps that region as-is (no recenter, no rebuild unless its
		// obstacle inputs changed). This shuts the gate for a stationary/slow driver and for
		// a camera pan that stays well inside the built viewport region.
		for (std::size_t d = 0; d < desired.size(); ++d) {
			for (std::size_t r = 0; r < regions.size(); ++r) {
				if (regionMatched[r]) {
					continue;
				}
				const SimAabb have	  = regions[r].rect();
				bool		  allInside = true;
				for (const geometry::Vec2i64& p : desired[d].drivers) {
					if (!driverInsideMargin(p, have)) {
						allInside = false;
						break;
					}
				}
				if (allInside) {
					regionMatched[r]  = true;
					desiredHandled[d] = true;
					// Rect unchanged, but an obstacle-input change (a wall edit, a
					// late-finishing chunk) still forces an in-place rebuild.
					if (!regions[r].future.valid() && regionObstaclesChanged(regions[r])) {
						launchBuild(regions[r]);
					}
					break;
				}
			}
		}

		// Pass 2: each remaining desired region recenters the best-overlapping unmatched
		// region (a driver crossed the margin -- same region, moved) or spawns a new one (a
		// disjoint cluster: the camera panned far, or a second colonist). Recentering reuses
		// the region slot+id so a colonist's region stays "his" across moves.
		for (std::size_t d = 0; d < desired.size(); ++d) {
			if (desiredHandled[d]) {
				continue;
			}
			const SimAabb&			want	   = desired[d].rect;
			const geometry::Vec2i64 wantCenter = want.center();
			const std::int64_t		wantHalf   = clampHalfExtent(std::max(want.halfExtentX(), want.halfExtentY()));

			int			 best		= -1;
			std::int64_t bestDistSq = 0;
			for (std::size_t r = 0; r < regions.size(); ++r) {
				if (regionMatched[r] || !regions[r].rect().overlaps(want)) {
					continue;
				}
				const std::int64_t dx	  = regions[r].center.x - wantCenter.x;
				const std::int64_t dy	  = regions[r].center.y - wantCenter.y;
				const std::int64_t distSq = dx * dx + dy * dy;
				if (best < 0 || distSq < bestDistSq) {
					best	   = static_cast<int>(r);
					bestDistSq = distSq;
				}
			}

			if (best >= 0) {
				SimulationRegion& region = regions[static_cast<std::size_t>(best)];
				region.center			 = wantCenter;
				region.halfExtent		 = wantHalf;
				regionMatched[static_cast<std::size_t>(best)] = true;
				launchBuild(region);
				LOG_INFO(Engine, "[NavBuild] region %d recenter -> (%lld, %lld) half=%lld", region.id,
						 static_cast<long long>(wantCenter.x), static_cast<long long>(wantCenter.y),
						 static_cast<long long>(wantHalf));
			} else {
				SimulationRegion region;
				region.id		  = nextRegionId++;
				region.center	  = wantCenter;
				region.halfExtent = wantHalf;
				launchBuild(region);
				LOG_INFO(Engine, "[NavBuild] region %d NEW -> (%lld, %lld) half=%lld", region.id,
						 static_cast<long long>(wantCenter.x), static_cast<long long>(wantCenter.y),
						 static_cast<long long>(wantHalf));
				regions.push_back(std::move(region));
				regionMatched.push_back(true);
			}
		}

		// Drop regions no longer wanted. Block on any in-flight build first so its worker
		// can't write into a future we're about to destroy. Also purge their RRA caches.
		for (std::size_t r = regions.size(); r-- > 0;) {
			if (regionMatched[r]) {
				continue;
			}
			if (regions[r].future.valid()) {
				regions[r].future.wait();
			}
			const std::int32_t goneId = regions[r].id;
			for (auto it = rraCaches.begin(); it != rraCaches.end();) {
				if (it->first.region == goneId) {
					it = rraCaches.erase(it);
				} else {
					++it;
				}
			}
			LOG_INFO(Engine, "[NavBuild] region %d dropped", goneId);
			regions.erase(regions.begin() + static_cast<std::ptrdiff_t>(r));
		}
	}

	void NavigationSystem::update(float /*deltaTime*/) {
		// Drain finished builds first so a mesh completed last frame is queryable this
		// frame (and a fresh rebuild can be launched on top of it below).
		drainFinishedBuilds();

		// buildInput needs the (non-thread-safe) ConstructionWorld for walls and the
		// fallback border, and a ChunkManager for terrain. Without both there is nothing
		// to build -- the very early frames before the scene wires them. Return without
		// touching regions so the first real build happens once everything is wired.
		if (constructionWorld == nullptr || chunkManager == nullptr) {
			return;
		}

		// Compute the desired regions from colonists + viewport, then reconcile. The
		// reconcile self-gates: a region whose drivers stay comfortably inside it and whose
		// obstacles are unchanged is left untouched (no rebuild) -- nav generation is off
		// the render clock.
		const std::vector<DesiredRegion> desired = computeDesiredRegions();
		reconcileRegions(desired);
	}

	int NavigationSystem::regionContaining(glm::vec2 meters) const {
		const geometry::Vec2i64 pMm = engine::nav::toMm(meters);
		for (std::size_t r = 0; r < regions.size(); ++r) {
			if (regions[r].hasMesh() && regions[r].rect().contains(pMm)) {
				return static_cast<int>(r);
			}
		}
		return -1;
	}

	bool NavigationSystem::hasMesh() const {
		for (const SimulationRegion& r : regions) {
			if (r.hasMesh()) {
				return true;
			}
		}
		return false;
	}

	std::vector<NavigationSystem::RegionView> NavigationSystem::builtRegions() const {
		std::vector<RegionView> out;
		out.reserve(regions.size());
		for (const SimulationRegion& r : regions) {
			if (r.hasMesh()) {
				out.push_back({&r.navMesh, r.center, r.halfExtent});
			}
		}
		return out;
	}

	bool NavigationSystem::inSimArea(glm::vec2 meters) const {
		return regionContaining(meters) >= 0;
	}

	std::optional<std::vector<glm::vec2>>
	NavigationSystem::requestPath(glm::vec2 startMeters, glm::vec2 goalMeters, float agentRadiusMeters,
								  gnav::BeliefFilter belief) const {
		// Dispatch by position: both endpoints must lie in the SAME built region. Different
		// regions (or a goal outside every region) -> hold the task; long-range routing
		// across unsimulated space is Phase 2.
		const int startRegion = regionContaining(startMeters);
		const int goalRegion  = regionContaining(goalMeters);
		if (startRegion < 0 || startRegion != goalRegion) {
			return std::nullopt;
		}
		const SimulationRegion& region	= regions[static_cast<std::size_t>(startRegion)];
		const gnav::NavMesh&	navMesh = region.navMesh;

		const geometry::Vec2i64 startMm = engine::nav::toMm(startMeters);
		const geometry::Vec2i64 goalMm	= engine::nav::toMm(goalMeters);
		const std::int64_t radiusMm =
			static_cast<std::int64_t>(std::llround(static_cast<double>(agentRadiusMeters) * 1000.0));

		// RRA* heuristic: locate the goal triangle in THIS region and hand pathThrough the
		// resumable reverse-search cache for it (keyed by region id + goal triangle).
		gnav::RraCache*	   rra	   = nullptr;
		const std::int32_t goalTri = gnav::locateTriangle(navMesh, goalMm);
		if (goalTri >= 0) {
			const RraKey key{region.id, goalTri};
			if (rraCaches.size() >= kMaxRraCaches && rraCaches.find(key) == rraCaches.end()) {
				rraCaches.clear();
			}
			gnav::RraCache& cache = rraCaches[key];
			cache.goalTri		  = goalTri;
			rra					  = &cache;
		}

		const gnav::PathResult result = gnav::pathThrough(navMesh, startMm, goalMm, radiusMm, belief, rra);

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
		// No single region covers both endpoints (no mesh yet, off-area endpoint, or a
		// cross-region pair): can't prove unreachable, so don't reject. Colonists beeline
		// legitimately during startup/outdoor play; the agent moves toward the area and
		// switches to in-region routing once both endpoints share a region.
		const int startRegion = regionContaining(startMeters);
		const int goalRegion  = regionContaining(goalMeters);
		if (startRegion < 0 || startRegion != goalRegion) {
			return true;
		}
		const gnav::NavMesh& navMesh = regions[static_cast<std::size_t>(startRegion)].navMesh;

		const geometry::Vec2i64 startMm = engine::nav::toMm(startMeters);
		const geometry::Vec2i64 goalMm	= engine::nav::toMm(goalMeters);
		const std::int64_t radiusMm =
			static_cast<std::int64_t>(std::llround(static_cast<double>(agentRadiusMeters) * 1000.0));

		return gnav::reachable(navMesh, startMm, goalMm, radiusMm, belief);
	}

	bool NavigationSystem::isOnMesh(glm::vec2 meters) const {
		const int r = regionContaining(meters);
		if (r < 0) {
			return false;
		}
		const gnav::NavMesh& navMesh = regions[static_cast<std::size_t>(r)].navMesh;
		// "On the mesh" means standing on WALKABLE ground, not merely inside some triangle.
		// Use terrainTraversable: outdoor ground is NOT a kNoBlocker "floor" face (that's
		// constructed indoor floor), so isFloorFace would reject all open terrain.
		const std::int32_t tri = gnav::locateTriangle(navMesh, engine::nav::toMm(meters));
		return tri >= 0 && gnav::terrainTraversable(navMesh.triangles[static_cast<std::size_t>(tri)]);
	}

	std::optional<glm::vec2> NavigationSystem::nearestPathablePoint(glm::vec2 meters) const {
		const int r = regionContaining(meters);
		if (r < 0) {
			return std::nullopt; // off every region: caller leaves the colonist put
		}
		return nearestPathableOnMesh(regions[static_cast<std::size_t>(r)].navMesh, meters);
	}

} // namespace ecs
