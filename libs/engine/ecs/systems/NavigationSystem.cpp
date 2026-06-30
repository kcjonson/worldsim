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
#include <utility>
#include <vector>

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

		// Closest point to `p` on the triangle (a,b,c) -- the nearer of its three edge
		// projections -- with that squared distance. One source of closest-point-on-triangle
		// math: both nearestPathableOnMesh and findValidPositionNear's fallback use it. A point
		// already inside the triangle still resolves to its nearest edge, which is what both
		// callers want (they then bias toward the centroid for an unambiguous in-face locate).
		struct ClosestPoint {
			glm::vec2 point;
			float	  dist2;
		};
		ClosestPoint closestPointOnTriangle(glm::vec2 p, glm::vec2 a, glm::vec2 b, glm::vec2 c) {
			glm::vec2 cand = closestOnSegment(p, a, b);
			float	  cd2  = dist2(p, cand);
			if (const glm::vec2 p2 = closestOnSegment(p, b, c); dist2(p, p2) < cd2) {
				cand = p2;
				cd2	 = dist2(p, p2);
			}
			if (const glm::vec2 p3 = closestOnSegment(p, c, a); dist2(p, p3) < cd2) {
				cand = p3;
				cd2	 = dist2(p, p3);
			}
			return {cand, cd2};
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
				const ClosestPoint cp = closestPointOnTriangle(meters, a, b, c);
				const glm::vec2	   cand = cp.point;
				const float		   cd2	= cp.dist2;
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

		// --- Built-wall collision-band clearance (recovery-snap side) ----------------
		// The recovery snap must land OUTSIDE the same band WallCollisionSystem pushes
		// agents out of, or the two oscillate. These mirror WallCollisionSystem's band
		// model exactly (centerline endpoints in meters, halfThickness + r clearance,
		// pathable door-gap spans exempt) so a point this code calls "clear" is a point
		// that system will never move.

		constexpr float kBandClearMarginMeters = 0.02F; // 2 cm past clearance so a re-locate isn't borderline

		struct WallBandM {
			glm::vec2							 v0;
			glm::vec2							 v1;
			float								 halfThicknessMeters;
			std::vector<std::pair<float, float>> doorSpans; // centerline [t0,t1] of pathable openings
		};

		// Closest point on segment param t in [0,1], plus that point, in meters.
		float closestParam(glm::vec2 p, glm::vec2 a, glm::vec2 b) {
			const glm::vec2 ab	  = b - a;
			const float		denom = ab.x * ab.x + ab.y * ab.y;
			if (denom <= 1e-12F) {
				return 0.0F;
			}
			const float t = ((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / denom;
			return t < 0.0F ? 0.0F : (t > 1.0F ? 1.0F : t);
		}

		// Resolve the BUILT wall bands once (same Built-only gate, thickness preset, and
		// door-span precompute as WallCollisionSystem). Empty when no construction world
		// or no built walls.
		std::vector<WallBandM> resolveBuiltBands(const engine::construction::ConstructionWorld* cw) {
			std::vector<WallBandM> bands;
			if (cw == nullptr) {
				return bands;
			}
			namespace cons	  = engine::construction;
			const auto& registry = engine::assets::ConstructionRegistry::Get();
			constexpr float kMmPerM = 1000.0F;
			bands.reserve(cw->segments().size());
			for (const cons::WallSegment& seg : cw->segments()) {
				if (seg.state != cons::FoundationState::Built) {
					continue;
				}
				const cons::Vertex* v0 = cw->getVertex(seg.v0);
				const cons::Vertex* v1 = cw->getVertex(seg.v1);
				if (v0 == nullptr || v1 == nullptr) {
					continue;
				}
				const auto* preset = registry.getThicknessPreset(seg.material, seg.thicknessPreset);
				if (preset == nullptr || preset->halfThicknessMm <= 0) {
					continue;
				}
				WallBandM band;
				band.v0					 = {static_cast<float>(v0->pos.x) / kMmPerM, static_cast<float>(v0->pos.y) / kMmPerM};
				band.v1					 = {static_cast<float>(v1->pos.x) / kMmPerM, static_cast<float>(v1->pos.y) / kMmPerM};
				band.halfThicknessMeters = static_cast<float>(preset->halfThicknessMm) / kMmPerM;

				const float dx			= (band.v1.x - band.v0.x) * kMmPerM;
				const float dy			= (band.v1.y - band.v0.y) * kMmPerM;
				const float segLengthMm = std::sqrt(dx * dx + dy * dy);
				if (segLengthMm > 0.0F) {
					for (const cons::Opening& op : cw->openings()) {
						if (op.segment != seg.id || op.state != cons::FoundationState::Built) {
							continue;
						}
						const auto* type = registry.getOpeningType(op.type);
						if (type == nullptr || !type->pathable) {
							continue;
						}
						const float halfExtent = (static_cast<float>(type->widthMm) * 0.5F) / segLengthMm;
						band.doorSpans.emplace_back(std::clamp(op.t - halfExtent, 0.0F, 1.0F),
													std::clamp(op.t + halfExtent, 0.0F, 1.0F));
					}
				}
				bands.push_back(std::move(band));
			}
			return bands;
		}

		// True when `p` is clear of every built band for an agent of radius r (outside
		// halfThickness + r, or inside a pathable door gap). A point with no bands is clear.
		bool clearOfBands(glm::vec2 p, float r, const std::vector<WallBandM>& bands) {
			for (const WallBandM& band : bands) {
				const float t	  = closestParam(p, band.v0, band.v1);
				const glm::vec2 c = band.v0 + (band.v1 - band.v0) * t;
				const float dx	  = p.x - c.x;
				const float dy	  = p.y - c.y;
				const float dist  = std::sqrt(dx * dx + dy * dy);
				if (dist >= band.halfThicknessMeters + r) {
					continue;
				}
				bool inDoor = false;
				for (const std::pair<float, float>& span : band.doorSpans) {
					if (t >= span.first && t <= span.second) {
						inDoor = true;
						break;
					}
				}
				if (!inDoor) {
					return false;
				}
			}
			return true;
		}

		// Push `p` out of every band it penetrates, to exactly halfThickness + r + margin,
		// with the same 2-iteration relaxation WallCollisionSystem uses so a corner between
		// two walls settles against both. Door gaps are exempt (no push there).
		glm::vec2 ejectFromBands(glm::vec2 p, float r, const std::vector<WallBandM>& bands) {
			for (int iter = 0; iter < 2; ++iter) {
				for (const WallBandM& band : bands) {
					const float t	  = closestParam(p, band.v0, band.v1);
					const glm::vec2 c = band.v0 + (band.v1 - band.v0) * t;
					glm::vec2		toA{p.x - c.x, p.y - c.y};
					const float		dist	  = std::sqrt(toA.x * toA.x + toA.y * toA.y);
					const float		clearance = band.halfThicknessMeters + r + kBandClearMarginMeters;
					if (dist >= clearance) {
						continue;
					}
					bool inDoor = false;
					for (const std::pair<float, float>& span : band.doorSpans) {
						if (t >= span.first && t <= span.second) {
							inDoor = true;
							break;
						}
					}
					if (inDoor) {
						continue;
					}
					glm::vec2 normal;
					if (dist > 1e-4F) {
						normal = {toA.x / dist, toA.y / dist};
					} else {
						const glm::vec2 d = band.v1 - band.v0;
						const float		l = std::sqrt(d.x * d.x + d.y * d.y);
						normal			  = (l > 1e-6F) ? glm::vec2{-d.y / l, d.x / l} : glm::vec2{1.0F, 0.0F};
					}
					p.x += normal.x * (clearance - dist);
					p.y += normal.y * (clearance - dist);
				}
			}
			return p;
		}

		// Nearest point to `meters` that is on a walkable face AND clears every built band.
		// Scans walkable triangles; for each, takes the closest interior point to `meters`
		// (clamped to the centroid-nudged region) and, if still in-band, the centroid; keeps
		// the nearest candidate that passes isOnMeshFn + clearOfBands. The centroid is a deep
		// interior point that any triangle wider than ~2r clears, so a sub-clearance sliver
		// next to the agent is skipped in favour of the nearest roomy face (the wall's open
		// side). nullopt when no walkable face yields a clear point.
		template <typename OnMeshFn>
		std::optional<glm::vec2> nearestClearOnMesh(const gnav::NavMesh& navMesh, glm::vec2 meters, float r,
													const std::vector<WallBandM>& bands, const OnMeshFn& isOnMeshFn) {
			bool	  found = false;
			float	  bestD2 = 0.0F;
			glm::vec2 best{0.0F, 0.0F};
			auto consider = [&](glm::vec2 p) {
				if (!clearOfBands(p, r, bands) || !isOnMeshFn(p)) {
					return;
				}
				const float d2 = dist2(meters, p);
				if (!found || d2 < bestD2) {
					best   = p;
					bestD2 = d2;
					found  = true;
				}
			};
			for (const gnav::NavTriangle& t : navMesh.triangles) {
				if (!gnav::terrainTraversable(t)) {
					continue;
				}
				const glm::vec2 a = engine::nav::toMeters(navMesh.vertices[t.v[0]]);
				const glm::vec2 b = engine::nav::toMeters(navMesh.vertices[t.v[1]]);
				const glm::vec2 c = engine::nav::toMeters(navMesh.vertices[t.v[2]]);
				const glm::vec2 centroid = (a + b + c) / 3.0F;
				// Closest edge point to `meters`, then biased a third toward the centroid so
				// the probe sits inside the face rather than on the edge that locate excludes.
				glm::vec2	cand = closestOnSegment(meters, a, b);
				float		cd2	 = dist2(meters, cand);
				if (const glm::vec2 p2 = closestOnSegment(meters, b, c); dist2(meters, p2) < cd2) {
					cand = p2;
					cd2	 = dist2(meters, p2);
				}
				if (const glm::vec2 p3 = closestOnSegment(meters, c, a); dist2(meters, p3) < cd2) {
					cand = p3;
				}
				consider(cand + (centroid - cand) * 0.34F);
				consider(centroid);
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
		// A felled tree / depleted node is removed from the live chunk index but leaves no
		// chunk-membership or construction-version change, so neither check above sees it.
		// The placement removal epoch does: any removal forces the covering region to rebuild
		// and reclaim the trunk hole as walkable ground (the mesh re-gathers flora rings from
		// the live index). Region-wide, not per-position -- a removal anywhere rebuilds, which
		// is fine: harvests are infrequent relative to the gate.
		const std::uint64_t epoch = (placement != nullptr) ? placement->removalEpoch() : 0;
		if (epoch != region.builtRemovalEpoch) {
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
		// Snapshot the removal epoch from the SAME live index buildInput just read, so the
		// gate only refires on a removal that lands after this build.
		region.builtRemovalEpoch		= (placement != nullptr) ? placement->removalEpoch() : 0;

		const double inputMs =
			std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - inputStart).count();
		std::size_t walkableBorders = 0;
		for (const gnav::NavInputPolygon& p : input.polygons) {
			if (!p.blocked) {
				++walkableBorders;
			}
		}
		// Permanent nav-build diagnostic (per-region input-extraction timing + walkable/blocked
		// ring balance). At DEBUG: visible via the dev-tools log server, compiled out in release.
		LOG_DEBUG(Engine, "[NavBuild] region %d buildInput %.2f ms: polys=%zu walkableBorders=%zu blockedRings=%zu",
				 region.id, inputMs, input.polygons.size(), walkableBorders, input.polygons.size() - walkableBorders);

		region.future = std::async(std::launch::async, [input = std::move(input), id = region.id]() {
			const auto			   buildStart = std::chrono::steady_clock::now();
			geometry::nav::NavMesh m		  = gnav::buildNavMesh(input);
			const double		   buildMs =
				std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - buildStart).count();
			// Permanent worker-thread build-timing diagnostic. DEBUG keeps it off the default stream.
			LOG_DEBUG(Engine, "[NavBuild] region %d buildNavMesh %.2f ms: tris=%zu verts=%zu", id, buildMs,
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
			// Permanent mesh-swap face-count diagnostic: the per-rebuild walkable/floor/blocked
			// verdict (walkable=0 is the zero-walkable-navmesh signature). DEBUG, dev-tools log server.
			LOG_DEBUG(Engine,
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
		// region's built rect AND whose size matches that region (within kSizeChangeThreshold)
		// keeps that region as-is (no recenter, no rebuild unless its obstacle inputs changed).
		// This shuts the gate for a stationary/slow driver and for a camera pan that stays well
		// inside the built viewport region; a large zoom in/out fails the size check and
		// resizes the region in Pass 2.
		for (std::size_t d = 0; d < desired.size(); ++d) {
			const std::int64_t wantHalf =
				clampHalfExtent(std::max(desired[d].rect.halfExtentX(), desired[d].rect.halfExtentY()));
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
				// Size match: the desired half-extent is within kSizeChangeThreshold of the
				// built one. A big zoom (>20%) fails this and forces a resize.
				bool sizeMatch = false;
				if (regions[r].halfExtent > 0) {
					const double ratio = static_cast<double>(wantHalf) / static_cast<double>(regions[r].halfExtent);
					const double delta = (ratio > 1.0) ? (ratio - 1.0) : (1.0 - ratio);
					sizeMatch		   = delta <= kSizeChangeThreshold;
				}
				if (allInside && sizeMatch) {
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
				// Permanent region-lifecycle diagnostic (recenter onto a moved driver/viewport).
				LOG_DEBUG(Engine, "[NavBuild] region %d recenter -> (%lld, %lld) half=%lld", region.id,
						 static_cast<long long>(wantCenter.x), static_cast<long long>(wantCenter.y),
						 static_cast<long long>(wantHalf));
			} else {
				SimulationRegion region;
				region.id		  = nextRegionId++;
				region.center	  = wantCenter;
				region.halfExtent = wantHalf;
				launchBuild(region);
				// Permanent region-lifecycle diagnostic (new simulation region created).
				LOG_DEBUG(Engine, "[NavBuild] region %d NEW -> (%lld, %lld) half=%lld", region.id,
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
			// Permanent region-lifecycle diagnostic (region torn down, no longer wanted).
			LOG_DEBUG(Engine, "[NavBuild] region %d dropped", goneId);
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

	bool NavigationSystem::isSegmentWalkable(glm::vec2 aMeters, glm::vec2 bMeters) const {
		// The segment is walkable IFF both endpoints and every interior sample at the
		// footprint pitch are on walkable mesh (isOnMesh is the per-point authority). The
		// interior samples catch a water sliver the endpoints straddle. Step in mm for
		// resolution-stable sampling regardless of segment orientation/length.
		if (!isOnMesh(aMeters) || !isOnMesh(bMeters)) {
			return false;
		}
		const double dx	   = static_cast<double>(bMeters.x) - aMeters.x;
		const double dy	   = static_cast<double>(bMeters.y) - aMeters.y;
		const double lenMm = std::sqrt(dx * dx + dy * dy) * static_cast<double>(geometry::kMillimetersPerMeter);
		const auto	 steps = static_cast<std::int64_t>(lenMm / static_cast<double>(kFootprintSampleStepMm));
		for (std::int64_t i = 1; i < steps; ++i) {
			const double t = static_cast<double>(i) / static_cast<double>(steps);
			const glm::vec2 p{static_cast<float>(aMeters.x + dx * t), static_cast<float>(aMeters.y + dy * t)};
			if (!isOnMesh(p)) {
				return false;
			}
		}
		return true;
	}

	bool NavigationSystem::isPolylineWalkable(const std::vector<glm::vec2>& ptsMeters) const {
		if (ptsMeters.empty()) {
			return true;
		}
		if (ptsMeters.size() == 1) {
			return isOnMesh(ptsMeters.front());
		}
		for (std::size_t i = 0; i + 1 < ptsMeters.size(); ++i) {
			if (!isSegmentWalkable(ptsMeters[i], ptsMeters[i + 1])) {
				return false;
			}
		}
		return true;
	}

	bool NavigationSystem::isAreaWalkable(const std::vector<glm::vec2>& polygonMeters) const {
		// Fewer than 3 points is not an area: fall back to validating the point(s) we have.
		if (polygonMeters.size() < 3) {
			return isPolylineWalkable(polygonMeters);
		}

		// 1) Every edge (closing edge included) entirely on walkable mesh. This also
		//    covers all vertices and any water sliver an edge crosses between two on-land
		//    corners (e.g. a foundation spanning a river).
		const std::size_t n = polygonMeters.size();
		for (std::size_t i = 0; i < n; ++i) {
			if (!isSegmentWalkable(polygonMeters[i], polygonMeters[(i + 1) % n])) {
				return false;
			}
		}

		// 2) Interior on a grid at the same pitch. The edge pass alone misses a hole
		//    fully inside the ring (a water pond an outer boundary encloses), so probe
		//    every grid point that lies inside the polygon and require it walkable.
		//    Foundations are small, so this is a few hundred O(1)-ish probes at most.
		const double stepMeters = static_cast<double>(kFootprintSampleStepMm) / static_cast<double>(geometry::kMillimetersPerMeter);
		float minX = polygonMeters[0].x;
		float maxX = polygonMeters[0].x;
		float minY = polygonMeters[0].y;
		float maxY = polygonMeters[0].y;
		for (const auto& p : polygonMeters) {
			minX = std::min(minX, p.x);
			maxX = std::max(maxX, p.x);
			minY = std::min(minY, p.y);
			maxY = std::max(maxY, p.y);
		}

		// Even-odd point-in-polygon (ring is implicitly closed). Float test is fine here:
		// it only decides WHICH grid points to probe; the walkability decision per probe is
		// the exact integer locateTriangle. A point misclassified right on an edge is still
		// covered by the edge pass above.
		auto inside = [&](float px, float py) -> bool {
			bool in = false;
			for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
				const float xi = polygonMeters[i].x;
				const float yi = polygonMeters[i].y;
				const float xj = polygonMeters[j].x;
				const float yj = polygonMeters[j].y;
				if (((yi > py) != (yj > py)) && (px < (xj - xi) * (py - yi) / (yj - yi) + xi)) {
					in = !in;
				}
			}
			return in;
		};

		for (double y = static_cast<double>(minY) + stepMeters; y < static_cast<double>(maxY); y += stepMeters) {
			for (double x = static_cast<double>(minX) + stepMeters; x < static_cast<double>(maxX); x += stepMeters) {
				const auto fx = static_cast<float>(x);
				const auto fy = static_cast<float>(y);
				if (inside(fx, fy) && !isOnMesh({fx, fy})) {
					return false;
				}
			}
		}
		return true;
	}

	std::optional<glm::vec2> NavigationSystem::nearestPathablePoint(glm::vec2 meters) const {
		const int r = regionContaining(meters);
		if (r < 0) {
			return std::nullopt; // off every region: caller leaves the colonist put
		}
		return nearestPathableOnMesh(regions[static_cast<std::size_t>(r)].navMesh, meters);
	}

	std::optional<glm::vec2> NavigationSystem::nearestPathablePoint(glm::vec2 meters, float agentRadiusMeters) const {
		const int region = regionContaining(meters);
		if (region < 0) {
			return std::nullopt; // off every region: caller leaves the colonist put
		}
		const gnav::NavMesh&		 navMesh = regions[static_cast<std::size_t>(region)].navMesh;
		const std::vector<WallBandM> bands	 = resolveBuiltBands(constructionWorld);

		// No built walls -> no band to clear; the plain nearest-walkable point is already safe.
		if (bands.empty()) {
			return nearestPathableOnMesh(navMesh, meters);
		}

		auto onMesh = [&](glm::vec2 p) { return isOnMesh(p); };

		// Fast path: take the bare nearest-walkable point and push it out of the bands. In
		// the common case (a roomy face beside the wall) the ejected point stays on that
		// face and clears the band -- snap done, no broad scan.
		if (const std::optional<glm::vec2> bare = nearestPathableOnMesh(navMesh, meters)) {
			const glm::vec2 ejected = ejectFromBands(*bare, agentRadiusMeters, bands);
			if (onMesh(ejected) && clearOfBands(ejected, agentRadiusMeters, bands)) {
				return ejected;
			}
		}

		// Slow path: the closest pocket is too thin to hold the agent clear of the wall
		// (the ejected point fell off-mesh). Find the nearest walkable face that DOES have
		// room -- typically the wall's open side. This is what frees a colonist sealed into
		// a sub-clearance gap instead of letting it oscillate there forever.
		if (const std::optional<glm::vec2> clear = nearestClearOnMesh(navMesh, meters, agentRadiusMeters, bands, onMesh)) {
			return clear;
		}

		// Last resort: every walkable face is inside some band (degenerate local mesh). Return
		// the bare nearest point so the caller still moves the colonist onto the mesh; the
		// AIDecisionSystem recovery then falls through to the colony-origin teleport if this
		// point is still off-mesh next frame.
		return nearestPathableOnMesh(navMesh, meters);
	}

	std::optional<glm::vec2>
	NavigationSystem::findValidPositionNear(glm::vec2 origin, float minDistMeters, glm::vec2 preferredDir) const {
		// Tier 1: must start inside a region (otherwise there is no mesh to place against, same
		// gate as nearestPathablePoint). Candidate validity below is the per-point isOnMesh, which
		// locates against the containing region's mesh itself, so no separate locate is needed here.
		const int region = regionContaining(origin);
		if (region < 0) {
			return std::nullopt;
		}
		const gnav::NavMesh& navMesh = regions[static_cast<std::size_t>(region)].navMesh;

		const float minDist = std::max(minDistMeters, 0.0F);

		// Canonical direction: the preferred bias if non-zero, else +X. Normalizing a near-zero
		// vector would be undefined, so fall back to the canonical axis for determinism.
		glm::vec2	dir{1.0F, 0.0F};
		const float dirLen2 = preferredDir.x * preferredDir.x + preferredDir.y * preferredDir.y;
		if (dirLen2 > 1e-12F) {
			const float invLen = 1.0F / std::sqrt(dirLen2);
			dir					= {preferredDir.x * invLen, preferredDir.y * invLen};
		}

		// Tier 2: the preferred candidate, then a fixed ring at the same radius. The ring starts
		// at `dir` and steps through kRingSamples evenly spaced angles, so the angle set and its
		// order are fixed -- deterministic, no RNG. First on-mesh sample wins.
		const glm::vec2 preferred = origin + dir * minDist;
		if (isOnMesh(preferred)) {
			return preferred;
		}
		constexpr int kRingSamples	 = 32;
		const float	  baseAngle		 = std::atan2(dir.y, dir.x);
		constexpr float kTwoPi		 = 6.28318530717958647692F;
		for (int i = 1; i < kRingSamples; ++i) {
			const float a = baseAngle + kTwoPi * (static_cast<float>(i) / static_cast<float>(kRingSamples));
			const glm::vec2 cand{origin.x + minDist * std::cos(a), origin.y + minDist * std::sin(a)};
			if (isOnMesh(cand)) {
				return cand;
			}
		}

		// Tier 3 (fallback): no ring sample is on mesh (origin wedged in a sub-minDist pocket
		// near a hole/edge). Take the nearest walkable point and, if it is closer than minDist,
		// push it radially out to minDist so the result still clears the requested gap. Guarantees
		// a non-null result whenever any walkable floor exists.
		// TODO(tier-3): best-first adjacency walk
		const std::optional<glm::vec2> nearest = nearestPathableOnMesh(navMesh, origin);
		if (!nearest) {
			return std::nullopt; // region has no walkable floor at all
		}
		glm::vec2	away	= *nearest - origin;
		const float awayLen = std::sqrt(away.x * away.x + away.y * away.y);
		if (awayLen >= minDist) {
			return *nearest; // already at/beyond the requested distance
		}
		// Pushing radially to minDist can cross a hole/water edge in a tight pocket, so re-validate.
		// If the pushed point is off-mesh, fall back to *nearest (on-mesh by construction -- it snaps
		// to a walkable triangle and nudges toward the centroid). This holds the "never return an
		// off-mesh point" guarantee; a sub-minDist but valid point beats a far invalid one, which a
		// requestPath to an off-mesh target would reject, stalling the consumer.
		const glm::vec2 outDir = (awayLen > 1e-6F) ? glm::vec2{away.x / awayLen, away.y / awayLen} : dir;
		const glm::vec2 pushed = origin + outDir * minDist;
		return isOnMesh(pushed) ? pushed : *nearest;
	}

} // namespace ecs
