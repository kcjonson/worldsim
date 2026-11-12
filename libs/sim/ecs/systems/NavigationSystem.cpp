#include "NavigationSystem.h"

#include "../World.h"
#include "../components/AgentRadius.h"
#include "../components/Memory.h"
#include "../components/Movement.h"
#include "../components/NavPath.h"
#include "../components/Transform.h"

#include "nav/NavCoords.h"
#include "nav/NavInputBuilder.h"

#include <assets/AssetRegistry.h>
#include <assets/ConstructionRegistry.h>
#include <assets/placement/PlacementExecutor.h>
#include <construction/ConstructionWorld.h>

#include <core/IntegerDivision.h>
#include <nav/PathQuery.h>
#include <predicates/Predicates.h>

#include <utils/Log.h>

#include <world/chunk/Chunk.h>
#include <world/chunk/ChunkManager.h>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <stop_token>
#include <thread>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ecs {

	namespace {
		namespace gnav = geometry::nav;

		using Clock = std::chrono::steady_clock;

		constexpr std::size_t  kUnlimited	  = std::numeric_limits<std::size_t>::max();
		constexpr std::int64_t kChunkMm		  = static_cast<std::int64_t>(world::kChunkSize) * geometry::kMillimetersPerMeter;
		constexpr std::int64_t kCellsPerChunk = kChunkMm / NavigationSystem::kDetailCellMm;
		static_assert(kCellsPerChunk * NavigationSystem::kDetailCellMm == kChunkMm, "detail cells tile a chunk");

		double msSince(Clock::time_point start) {
			return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
		}

		std::int64_t cellKey(std::int64_t cx, std::int64_t cy) {
			return static_cast<std::int64_t>((static_cast<std::uint64_t>(cx) << 32U) | static_cast<std::uint32_t>(cy));
		}
		std::int64_t cellX(std::int64_t key) { return key >> 32; }
		std::int64_t cellY(std::int64_t key) { return static_cast<std::int32_t>(static_cast<std::uint32_t>(key & 0xFFFFFFFF)); }
		world::ChunkCoordinate cellChunk(std::int64_t key) {
			return {static_cast<std::int32_t>(geometry::floorDiv(cellX(key), kCellsPerChunk)),
					static_cast<std::int32_t>(geometry::floorDiv(cellY(key), kCellsPerChunk))};
		}
		// The scan cell that owns a placed entity: [lo, lo + side) on each axis, as scanDetail tests
		// it (the side is a power of two in metres, so the division is exact).
		std::int64_t cellOfEntity(glm::vec2 position) {
			constexpr float kSide = static_cast<float>(NavigationSystem::kDetailCellMm / geometry::kMillimetersPerMeter);
			return cellKey(static_cast<std::int64_t>(std::floor(position.x / kSide)), static_cast<std::int64_t>(std::floor(position.y / kSide)));
		}
		std::int64_t radiusMm(float meters) { return static_cast<std::int64_t>(std::llround(static_cast<double>(meters) * 1000.0)); }

		// Squared distance and closest-point-on-segment, in meters.
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

		// Closest point to `p` on the triangle (a,b,c) -- the nearer of its three edge projections --
		// with that squared distance. A point inside still resolves to its nearest edge, which is what
		// the callers want (they then bias toward the centroid for an unambiguous in-face locate).
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

		struct TriangleMeters {
			glm::vec2 a;
			glm::vec2 b;
			glm::vec2 c;
		};
		TriangleMeters trianglePoints(const gnav::PatchableMesh& mesh, const gnav::NavTriangle& t) {
			return {nav::toMeters(mesh.vertices()[t.v[0]]), nav::toMeters(mesh.vertices()[t.v[1]]), nav::toMeters(mesh.vertices()[t.v[2]])};
		}

		// Squared distance from p to the triangle: 0 inside it.
		float triangleDist2(glm::vec2 p, const TriangleMeters& t) {
			const auto side = [](glm::vec2 a, glm::vec2 b, glm::vec2 q) { return (b.x - a.x) * (q.y - a.y) - (b.y - a.y) * (q.x - a.x); };
			const float s0 = side(t.a, t.b, p);
			const float s1 = side(t.b, t.c, p);
			const float s2 = side(t.c, t.a, p);
			if ((s0 >= 0.0F && s1 >= 0.0F && s2 >= 0.0F) || (s0 <= 0.0F && s1 <= 0.0F && s2 <= 0.0F)) {
				return 0.0F;
			}
			return closestPointOnTriangle(p, t.a, t.b, t.c).dist2;
		}

		// Visits the triangles of `mesh` nearest `p` first, from the one holding it outward over
		// adjacency, so a search touches only the triangles nearer than its answer. `visit` takes the
		// triangle's id and corners and returns
		// the squared distance the search has settled for so far, and the walk stops once every
		// triangle left is farther. Off the mesh it visits nothing.
		template <typename Visit> void nearestFirst(const gnav::PatchableMesh& mesh, glm::vec2 p, Visit visit) {
			const std::int32_t start = mesh.locate(nav::toMm(p));
			if (start < 0) {
				return;
			}
			using Entry = std::pair<float, std::int32_t>;
			std::priority_queue<Entry, std::vector<Entry>, std::greater<>> open;
			std::unordered_set<std::int32_t>								 seen{start};
			open.push({0.0F, start});
			float settled = std::numeric_limits<float>::infinity();
			while (!open.empty() && open.top().first <= settled) {
				const std::int32_t t = open.top().second;
				open.pop();
				const gnav::NavTriangle& tri = mesh.triangles()[static_cast<std::size_t>(t)];
				settled						 = std::min(settled, visit(t, trianglePoints(mesh, tri)));
				for (const std::int32_t n : tri.neighbor) {
					if (n >= 0 && seen.insert(n).second) {
						open.push({triangleDist2(p, trianglePoints(mesh, mesh.triangles()[static_cast<std::size_t>(n)])), n});
					}
				}
			}
		}

		// Nearest point on `mesh` to `meters` whose face passes `faceOk` (given the triangle id), nudged
		// a hair toward that face's centroid so it locates inside it, or nullopt when no face does.
		template <typename FaceOk>
		std::optional<glm::vec2> nearestFaceOnMesh(const gnav::PatchableMesh& mesh, glm::vec2 meters, const FaceOk& faceOk) {
			bool	  found	 = false;
			float	  bestD2 = 0.0F;
			glm::vec2 best{0.0F, 0.0F};
			constexpr float kNone = std::numeric_limits<float>::infinity();
			nearestFirst(mesh, meters, [&](std::int32_t t, const TriangleMeters& tri) {
				if (!faceOk(t)) {
					return found ? bestD2 : kNone;
				}
				const ClosestPoint	 cp	 = closestPointOnTriangle(meters, tri.a, tri.b, tri.c);
				if (!found || cp.dist2 < bestD2) {
					const glm::vec2 centroid = (tri.a + tri.b + tri.c) / 3.0F;
					best					 = cp.point + (centroid - cp.point) * 0.05F;
					bestD2					 = cp.dist2;
					found					 = true;
				}
				return bestD2;
			});
			return found ? std::optional<glm::vec2>(best) : std::nullopt;
		}

		template <typename FaceOk> bool pointOnFace(const gnav::PatchableMesh& mesh, glm::vec2 meters, const FaceOk& faceOk) {
			const std::int32_t tri = mesh.locate(nav::toMm(meters));
			return tri >= 0 && faceOk(mesh.triangles()[static_cast<std::size_t>(tri)]);
		}

		// Walkable for the most optimistic belief: only common-knowledge terrain (water, placed obstacles) blocks.
		bool terrainWalkable(const gnav::PatchableMesh& mesh, glm::vec2 meters) {
			return pointOnFace(mesh, meters, [](const gnav::NavTriangle& t) { return gnav::terrainTraversable(t); });
		}

		// The tag a placed obstacle's ring carries. Its faces block routes.
		bool isDetailTag(std::int64_t tag) {
			return tag == nav::kProvenancePlaced;
		}

		// Ground a mover may stand on: terrain-walkable, or inside a placed obstacle, which the snap
		// moves it out of.
		bool moverFace(const gnav::NavTriangle& t) {
			return gnav::terrainTraversable(t) || isDetailTag(t.faceBlocker);
		}

		// Walkable in truth: a solid wall face blocks, a door's span passes.
		bool truthWalkable(const gnav::PatchableMesh& mesh, glm::vec2 meters) {
			return pointOnFace(mesh, meters, [](const gnav::NavTriangle& t) { return gnav::truthTraversable(t); });
		}

		// Triangle `tri`'s face with the placed obstacles over it taken away, classified from the rest
		// of its cover as the mesh classifies it: the ground beneath, which building and the landing
		// read (a placed obstacle is a clear task to them, not a blocker), water under it included.
		gnav::NavTriangle groundBeneath(const gnav::PatchableMesh& mesh, std::int32_t tri) {
			gnav::NavTriangle face = mesh.triangles()[static_cast<std::size_t>(tri)];
			if (!isDetailTag(face.faceBlocker)) {
				return face;
			}
			std::vector<const gnav::SampleRing*> containing;
			for (const gnav::RingId r : mesh.cover(tri)) {
				const gnav::RegisteredRing& ring = mesh.ring(r);
				if (ring.polygon.blocked && !isDetailTag(ring.polygon.provenanceId)) {
					containing.push_back(&ring.sample);
				}
			}
			const gnav::FaceTags tags = gnav::classifyCover(containing);
			face.faceBlocker		  = tags.blocker;
			face.faceOpening		  = tags.opening;
			return face;
		}
		bool buildableFace(const gnav::PatchableMesh& mesh, std::int32_t tri) {
			return gnav::terrainTraversable(groundBeneath(mesh, tri));
		}
		bool landingFace(const gnav::PatchableMesh& mesh, std::int32_t tri) {
			return gnav::truthTraversable(groundBeneath(mesh, tri));
		}
		template <typename Face> bool pointOnGround(const gnav::PatchableMesh& mesh, glm::vec2 meters, const Face& face) {
			const std::int32_t tri = mesh.locate(nav::toMm(meters));
			return tri >= 0 && face(mesh, tri);
		}

		// --- Built-wall collision-band clearance (recovery-snap side) ----------------
		// The recovery snap must land OUTSIDE the band WallCollisionSystem pushes agents out of,
		// or the two oscillate. These mirror WallCollisionSystem's band model exactly (centerline
		// endpoints in meters, halfThickness + r clearance, pathable door-gap spans exempt).

		constexpr float kBandClearMarginMeters = 0.02F; // 2 cm past clearance so a re-locate isn't borderline

		struct WallBandM {
			glm::vec2							 v0;
			glm::vec2							 v1;
			float								 halfThicknessMeters;
			std::vector<std::pair<float, float>> doorSpans; // centerline [t0,t1] of pathable openings
		};

		float closestParam(glm::vec2 p, glm::vec2 a, glm::vec2 b) {
			const glm::vec2 ab	  = b - a;
			const float		denom = ab.x * ab.x + ab.y * ab.y;
			if (denom <= 1e-12F) {
				return 0.0F;
			}
			const float t = ((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / denom;
			return t < 0.0F ? 0.0F : (t > 1.0F ? 1.0F : t);
		}

		std::vector<WallBandM> resolveBuiltBands(const construction::ConstructionWorld* cw) {
			std::vector<WallBandM> bands;
			if (cw == nullptr) {
				return bands;
			}
			namespace cons			 = construction;
			const auto&		registry = assets::ConstructionRegistry::Get();
			constexpr float kMmPerM	 = 1000.0F;
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
						band.doorSpans.emplace_back(std::clamp(op.t - halfExtent, 0.0F, 1.0F), std::clamp(op.t + halfExtent, 0.0F, 1.0F));
					}
				}
				bands.push_back(std::move(band));
			}
			return bands;
		}

		bool inDoorSpan(const WallBandM& band, float t) {
			return std::any_of(band.doorSpans.begin(), band.doorSpans.end(),
							   [t](const std::pair<float, float>& span) { return t >= span.first && t <= span.second; });
		}

		// True when `p` is clear of every built band for an agent of radius r.
		bool clearOfBands(glm::vec2 p, float r, const std::vector<WallBandM>& bands) {
			for (const WallBandM& band : bands) {
				const float		t	 = closestParam(p, band.v0, band.v1);
				const glm::vec2 c	 = band.v0 + (band.v1 - band.v0) * t;
				const float		dist = std::sqrt(dist2(p, c));
				if (dist < band.halfThicknessMeters + r && !inDoorSpan(band, t)) {
					return false;
				}
			}
			return true;
		}

		// Push `p` out of every band it penetrates, to halfThickness + r + margin, with the same
		// 2-iteration relaxation WallCollisionSystem uses so a corner settles against both walls.
		glm::vec2 ejectFromBands(glm::vec2 p, float r, const std::vector<WallBandM>& bands) {
			for (int iter = 0; iter < 2; ++iter) {
				for (const WallBandM& band : bands) {
					const float		t = closestParam(p, band.v0, band.v1);
					const glm::vec2 c = band.v0 + (band.v1 - band.v0) * t;
					const glm::vec2 toA{p.x - c.x, p.y - c.y};
					const float		dist	  = std::sqrt(toA.x * toA.x + toA.y * toA.y);
					const float		clearance = band.halfThicknessMeters + r + kBandClearMarginMeters;
					if (dist >= clearance || inDoorSpan(band, t)) {
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
					p += normal * (clearance - dist);
				}
			}
			return p;
		}

		// Nearest point to `meters` on a walkable face that clears every built band: per walkable
		// triangle, the closest point biased a third toward the centroid, and the centroid itself,
		// so a sub-clearance sliver gives way to the nearest roomy face.
		template <typename OnMeshFn>
		std::optional<glm::vec2> nearestClearOnMesh(const gnav::PatchableMesh& mesh, glm::vec2 meters, float r,
													const std::vector<WallBandM>& bands, const OnMeshFn& isOnMeshFn) {
			bool	  found	 = false;
			float	  bestD2 = 0.0F;
			glm::vec2 best{0.0F, 0.0F};
			auto	  consider = [&](glm::vec2 p) {
				 if (!clearOfBands(p, r, bands) || !isOnMeshFn(p)) {
					 return;
				 }
				 const float d2 = dist2(meters, p);
				 if (!found || d2 < bestD2) {
					 best	= p;
					 bestD2 = d2;
					 found	= true;
				 }
			};
			nearestFirst(mesh, meters, [&](std::int32_t t, const TriangleMeters& tri) {
				if (gnav::terrainTraversable(mesh.triangles()[static_cast<std::size_t>(t)])) {
					const glm::vec2 centroid = (tri.a + tri.b + tri.c) / 3.0F;
					const glm::vec2 cand	 = closestPointOnTriangle(meters, tri.a, tri.b, tri.c).point;
					consider(cand + (centroid - cand) * 0.34F);
					consider(centroid);
				}
				return found ? bestD2 : std::numeric_limits<float>::infinity();
			});
			return found ? std::optional<glm::vec2>(best) : std::nullopt;
		}

		// Whole-segment walkability under a per-point predicate: both endpoints plus every interior
		// sample at the footprint pitch.
		template <typename OnMesh> bool segmentWalkableUnder(const OnMesh& onMesh, glm::vec2 a, glm::vec2 b) {
			if (!onMesh(a) || !onMesh(b)) {
				return false;
			}
			const double dx	   = static_cast<double>(b.x) - a.x;
			const double dy	   = static_cast<double>(b.y) - a.y;
			const double lenMm = std::sqrt(dx * dx + dy * dy) * static_cast<double>(geometry::kMillimetersPerMeter);
			const auto	 steps = static_cast<std::int64_t>(lenMm / static_cast<double>(NavigationSystem::kFootprintSampleStepMm));
			for (std::int64_t i = 1; i < steps; ++i) {
				const double	t = static_cast<double>(i) / static_cast<double>(steps);
				const glm::vec2 p{static_cast<float>(a.x + dx * t), static_cast<float>(a.y + dy * t)};
				if (!onMesh(p)) {
					return false;
				}
			}
			return true;
		}

		template <typename OnMesh> bool polylineWalkableUnder(const OnMesh& onMesh, const std::vector<glm::vec2>& pts) {
			if (pts.empty()) {
				return true;
			}
			if (pts.size() == 1) {
				return onMesh(pts.front());
			}
			for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
				if (!segmentWalkableUnder(onMesh, pts[i], pts[i + 1])) {
					return false;
				}
			}
			return true;
		}

		// Whole-footprint walkability: every edge (closing edge included) plus the interior on a grid
		// at the footprint pitch, which catches a hole wholly inside the ring (an enclosed pond).
		template <typename OnMesh> bool areaWalkableUnder(const OnMesh& onMesh, const std::vector<glm::vec2>& poly) {
			if (poly.size() < 3) {
				return polylineWalkableUnder(onMesh, poly);
			}
			const std::size_t n = poly.size();
			for (std::size_t i = 0; i < n; ++i) {
				if (!segmentWalkableUnder(onMesh, poly[i], poly[(i + 1) % n])) {
					return false;
				}
			}
			const double stepMeters =
				static_cast<double>(NavigationSystem::kFootprintSampleStepMm) / static_cast<double>(geometry::kMillimetersPerMeter);
			float minX = poly[0].x;
			float maxX = poly[0].x;
			float minY = poly[0].y;
			float maxY = poly[0].y;
			for (const auto& p : poly) {
				minX = std::min(minX, p.x);
				maxX = std::max(maxX, p.x);
				minY = std::min(minY, p.y);
				maxY = std::max(maxY, p.y);
			}
			// The float even-odd test only picks which grid points to probe; each probe is the exact
			// locate, and a point misclassified on an edge is covered by the edge pass above.
			auto inside = [&](float px, float py) {
				bool in = false;
				for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
					const float xi = poly[i].x;
					const float yi = poly[i].y;
					const float xj = poly[j].x;
					const float yj = poly[j].y;
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
					if (inside(fx, fy) && !onMesh({fx, fy})) {
						return false;
					}
				}
			}
			return true;
		}

		using PolygonKey = std::tuple<std::int64_t, std::int64_t, bool, std::vector<geometry::Vec2i64>>;
		PolygonKey polygonKey(const gnav::NavInputPolygon& p) { return {p.provenanceId, p.openingId, p.holeCapable, p.ring}; }

		bool samePortal(const gnav::DoorPortal& a, const gnav::DoorPortal& b) {
			return a.openingId == b.openingId && a.a == b.a && a.b == b.b && a.clearWidthMm == b.clearWidthMm;
		}

		// A door edit retags the triangles on its edge (a, b) where that is a mesh edge, and makes no
		// PatchReport of its own: this is one for them. The edge's midpoint locates to the lower id of
		// the two, which has the edge; its neighbour across it is the other.
		gnav::PatchReport doorEdgeReport(const gnav::PatchableMesh& mesh, const std::vector<gnav::DoorPortal>& portals) {
			gnav::PatchReport report;
			bool			  boxed = false;
			const auto		  take	= [&](std::int32_t t) {
				   report.changes.push_back({.id = t, .why = gnav::kTriangleBorder});
				   for (const std::uint32_t v : mesh.triangles()[static_cast<std::size_t>(t)].v) {
					   const geometry::Vec2i64& p = mesh.vertices()[v];
					   report.lo				  = boxed ? geometry::Vec2i64{std::min(report.lo.x, p.x), std::min(report.lo.y, p.y)} : p;
					   report.hi				  = boxed ? geometry::Vec2i64{std::max(report.hi.x, p.x), std::max(report.hi.y, p.y)} : p;
					   boxed					  = true;
				   }
			};
			for (const gnav::DoorPortal& portal : portals) {
				const std::int32_t t = mesh.locate({(portal.a.x + portal.b.x) / 2, (portal.a.y + portal.b.y) / 2});
				if (t < 0) {
					continue;
				}
				const gnav::NavTriangle& tri = mesh.triangles()[static_cast<std::size_t>(t)];
				for (std::size_t e = 0; e < 3; ++e) {
					const geometry::Vec2i64& p = mesh.vertices()[tri.v[e]];
					const geometry::Vec2i64& q = mesh.vertices()[tri.v[(e + 1) % 3]];
					if ((p == portal.a && q == portal.b) || (p == portal.b && q == portal.a)) {
						take(t);
						if (tri.neighbor[e] >= 0) {
							take(tri.neighbor[e]);
						}
						break;
					}
				}
			}
			std::sort(report.changes.begin(), report.changes.end(), [](const gnav::ChangedTriangle& x, const gnav::ChangedTriangle& y) { return x.id < y.id; });
			report.changes.erase(std::unique(report.changes.begin(), report.changes.end()), report.changes.end());
			return report;
		}

	} // namespace

	// A thread that runs one job at a time, handed over by start() and waited for by wait().
	class NavigationSystem::Worker {
	  public:
		Worker()
			: thread([this](const std::stop_token& stop) { loop(stop); }) {}

		void start(std::function<void()> work) {
			{
				const std::lock_guard lock(mutex);
				job	 = std::move(work);
				busy = true;
			}
			changed.notify_all();
		}

		void wait() {
			std::unique_lock lock(mutex);
			changed.wait(lock, [this] { return !busy; });
		}

	  private:
		void loop(const std::stop_token& stop) {
			while (true) {
				std::function<void()> work;
				{
					std::unique_lock lock(mutex);
					if (!changed.wait(lock, stop, [this] { return job != nullptr; })) {
						return;
					}
					work = std::move(job);
					job	 = nullptr;
				}
				work();
				{
					const std::lock_guard lock(mutex);
					busy = false;
				}
				changed.notify_all();
			}
		}

		std::mutex					mutex;
		std::condition_variable_any changed;
		std::function<void()>		job;
		bool						busy = false;
		std::jthread				thread; // last: it starts running the loop over the members above
	};

	// A tick's patch job, on the patch worker.
	void NavigationSystem::runJob(gnav::PatchableMesh& mesh, MeshJob& job) {
		const Clock::time_point start = Clock::now();
		for (const gnav::PatchEdits& edits : job.patches) {
			const Clock::time_point patchStart = Clock::now();
			gnav::PatchReport		report	   = mesh.applyPatch(edits);
			job.added.push_back(std::move(report.added));
			job.patchMs.push_back(msSince(patchStart));
			job.reports.push_back(std::move(report));
		}
		for (const gnav::DoorId door : job.doorRemoves) {
			mesh.removeDoor(door);
		}
		for (const gnav::DoorPortal& portal : job.doorAdds) {
			job.doorsAdded.push_back(mesh.addDoor(portal));
		}
		if (!job.doorEdges.empty()) {
			job.reports.push_back(doorEdgeReport(mesh, job.doorEdges));
		}
		const Clock::time_point widthsStart = Clock::now();
		job.reports.push_back(mesh.refreshWidths());
		job.widthsMs = msSince(widthsStart);
		job.ms		 = msSince(start);
	}

	bool NavigationSystem::DetailKey::operator<(const DetailKey& o) const {
		if (chunk.y != o.chunk.y || chunk.x != o.chunk.x) {
			return ChunkLess{}(chunk, o.chunk);
		}
		return std::tie(cell, xBits, yBits, defName) < std::tie(o.cell, o.xBits, o.yBits, o.defName);
	}

	NavigationSystem::NavigationSystem()
		: detailMesh({.maxBoundsEdgeMm = kMaxBoundsEdgeMm}),
		  patchWorker(std::make_unique<Worker>()),
		  routeWorker(std::make_unique<Worker>()) {}

	NavigationSystem::~NavigationSystem() {
		join();
		routeWorker->wait();
	}

	void NavigationSystem::setPlacementData(assets::PlacementExecutor* executor, const std::unordered_set<world::ChunkCoordinate>* processed) {
		placement		= executor;
		processedChunks = processed;
		if (executor != nullptr) {
			static_cast<void>(executor->takeRemovals());
		}
	}

	void NavigationSystem::setConstructionWorld(const construction::ConstructionWorld* world) {
		constructionWorld = world;
		wallsVersion	  = UINT64_MAX;
	}

	void NavigationSystem::appendAnchors(std::vector<world::ChunkAnchor>& out) const {
		// Null only in headless tests with no colonists.
		if (world == nullptr) {
			return;
		}
		for (auto [entity, position, agent] : world->view<Position, AgentRadius>()) {
			(void)agent;
			out.push_back({{position.value.x, position.value.y}, meshingRange(entity)});
		}
	}

	float NavigationSystem::meshingRange(EntityID mover) const {
		const Memory* memory = world->getComponent<Memory>(mover);
		return (memory != nullptr ? memory->sightRadius : kDefaultSightRadius) + kSightBufferMeters;
	}

	void NavigationSystem::collectAnchors() {
		std::vector<world::ChunkAnchor> held;
		appendAnchors(held);
		anchorChunks.clear();
		for (const world::ChunkAnchor& anchor : held) {
			const world::WorldPosition	 p	   = anchor.position;
			const float					 range = world::ChunkManager::anchorLoadRange(anchor.rangeMeters);
			const world::ChunkCoordinate lo	   = world::worldToChunk({p.x - range, p.y - range});
			const world::ChunkCoordinate hi	   = world::worldToChunk({p.x + range, p.y + range});
			for (std::int32_t cy = lo.y; cy <= hi.y; ++cy) {
				for (std::int32_t cx = lo.x; cx <= hi.x; ++cx) {
					anchorChunks.insert({cx, cy});
				}
			}
		}
	}

	void NavigationSystem::trackMovers() {
		if (world == nullptr) {
			return;
		}
		++moverPass;
		for (auto [entity, position, agent] : world->view<Position, AgentRadius>()) {
			(void)agent;
			const std::int64_t cell	 = cellOfEntity(position.value);
			const float		   range = meshingRange(entity);
			auto [it, fresh]		 = moverDiscs.try_emplace(entity);
			MoverDisc& disc			 = it->second;
			disc.seen				 = moverPass;
			if (!fresh && disc.cell == cell && disc.rangeMeters == range) {
				continue;
			}
			disc.cell		 = cell;
			disc.rangeMeters = range;
			wantDisc(cell, range);
			wantNear(cell);
		}
		std::erase_if(moverDiscs, [this](const auto& entry) { return entry.second.seen != moverPass; });
	}

	void NavigationSystem::wantDisc(std::int64_t centre, float rangeMeters) {
		// Measured from the centre cell's square, not a point in it, so wherever the mover stands in
		// that cell everything within R of it is in a wanted cell.
		const std::int64_t r	 = radiusMm(rangeMeters);
		const std::int64_t reach = r / kDetailCellMm + 1;
		const std::int64_t ox	 = cellX(centre);
		const std::int64_t oy	 = cellY(centre);
		const auto		   gapMm = [](std::int64_t d) { return std::max<std::int64_t>(0, std::abs(d) - 1) * kDetailCellMm; };
		struct Wanted {
			std::int64_t gap2;
			std::int64_t centre2;
			std::int64_t dy;
			std::int64_t dx;
		};
		std::vector<Wanted> fresh;
		for (std::int64_t dy = -reach; dy <= reach; ++dy) {
			for (std::int64_t dx = -reach; dx <= reach; ++dx) {
				const std::int64_t gap2 = gapMm(dx) * gapMm(dx) + gapMm(dy) * gapMm(dy);
				if (gap2 <= r * r && !wantedCells.contains(cellKey(ox + dx, oy + dy))) {
					fresh.push_back({gap2, dx * dx + dy * dy, dy, dx});
				}
			}
		}
		std::sort(fresh.begin(), fresh.end(), [](const Wanted& a, const Wanted& b) {
			return std::tie(a.gap2, a.centre2, a.dy, a.dx) < std::tie(b.gap2, b.centre2, b.dy, b.dx);
		});
		for (const Wanted& w : fresh) {
			const std::int64_t cell = cellKey(ox + w.dx, oy + w.dy);
			wantedCells.insert(cell);
			pendingCells.insert(cell);
			const world::ChunkCoordinate chunk = cellChunk(cell);
			if (wantedChunks.insert(chunk).second && !ground.contains(chunk)) {
				groundQueue.push_back(chunk);
			}
			// Behind any of its chunk's cells still waiting, so a chunk's cells keep their order.
			if (!cellsAwaitingChunk.contains(chunk) && cellServable(cell)) {
				readyCells.push_back(cell);
			} else {
				cellsAwaitingChunk[chunk].push_back(cell);
			}
		}
	}

	void NavigationSystem::wantNear(std::int64_t centre) {
		static constexpr std::array<std::pair<std::int64_t, std::int64_t>, 9> kOwnThenTouching{
			{{0, 0}, {-1, -1}, {0, -1}, {1, -1}, {-1, 0}, {1, 0}, {-1, 1}, {0, 1}, {1, 1}}};
		for (const auto [dx, dy] : kOwnThenTouching) {
			const std::int64_t cell = cellKey(cellX(centre) + dx, cellY(centre) + dy);
			if (!pendingCells.contains(cell) || !nearCells.insert(cell).second) {
				continue;
			}
			// Not servable yet: it is popped back to its chunk's wait, which releases it to nearReady.
			nearReady.push_back(cell);
			const world::ChunkCoordinate chunk = cellChunk(cell);
			if (!ground.contains(chunk) && std::find(nearGround.begin(), nearGround.end(), chunk) == nearGround.end()) {
				nearGround.push_back(chunk);
			}
		}
	}

	bool NavigationSystem::cellServable(std::int64_t cell) const {
		const world::ChunkCoordinate chunk = cellChunk(cell);
		return chunkGroundMeshed(chunk) && placementReady(chunk) && anchorHolds(chunk);
	}

	bool NavigationSystem::anchorHolds(world::ChunkCoordinate coord) const {
		return anchorChunks.contains(coord);
	}

	// --- Ticks, jobs and the join -------------------------------------------------

	void NavigationSystem::update(float /*deltaTime*/) {
		settle();
		startRouteSlice();
		markCrossedRoutes();
		markStrayedRoutes();
		snapMovers();
		if (chunkManager != nullptr) {
			gather(kDetailRingsPerTick, kDetailRemovalsPerTick, kDetailCellsPerTick, kDetailScansPerTick, kGroundVerticesPerTick, holdingFar);
		}
	}

	void NavigationSystem::startRouteSlice() {
		// settle() waited for the nav mesh's job, which waited for the slice; with no job, the slice
		// may still run.
		routeWorker->wait();
		const bool sliceRan = sliceStarted;
		if (sliceStarted) {
			sliceStarted		= false;
			std::uint32_t spent = 0;
			for (const nav::RouteTurn& turn : routes.lastSlice()) {
				spent += turn.spent;
			}
			RouteStats& s = routeStatsTotal;
			++s.slices;
			s.sliceExpansions += spent;
			s.lastSliceExpansions = spent;
			s.maxSliceExpansions  = std::max(s.maxSliceExpansions, spent);
			s.lastSliceMs		  = sliceMs;
			s.maxSliceMs		  = std::max(s.maxSliceMs, sliceMs);
		}
		for (const gnav::PatchReport& report : routeReports) {
			routes.notePatches(report);
		}
		routeReports.clear();
		routes.admit();
		++routeTick;
		for (const nav::RouteTicket queued : routes.answeredTickets()) {
			const auto ours = byQueueTicket.find(queued);
			if (ours == byQueueTicket.end()) {
				continue;
			}
			QueuedRoute& route = queuedRoutes.at(ours->second);
			if (!route.takeable) {
				route.takeable = true;
				takeableSince.emplace_back(routeTick, ours->second);
			}
		}
		dropUntakenAnswers();

		// A search still waiting after a slice it had a turn in spans ticks: its window opens now,
		// once, and a restart leaves it where it is.
		if (sliceRan) {
			for (const nav::RouteTurn& turn : routes.lastSlice()) {
				const auto ours = byQueueTicket.find(turn.ticket);
				if (ours == byQueueTicket.end() || !routes.waiting(turn.ticket)) {
					continue;
				}
				QueuedRoute& route = queuedRoutes.at(ours->second);
				if (route.holdFrom == 0) {
					route.holdFrom = routeTick;
				}
			}
		}
		bool searching = false;
		holdingFar	   = false;
		for (const auto& [ticket, route] : queuedRoutes) {
			if (!routes.waiting(route.queued)) {
				continue;
			}
			searching  = true;
			holdingFar = holdingFar || (route.holdFrom != 0 && routeTick - route.holdFrom < kMaxHeldAdditionTicks);
		}
		if (holdingFar) {
			++heldInARow;
			++routeStatsTotal.ticksHeld;
			routeStatsTotal.longestHold = std::max(routeStatsTotal.longestHold, heldInARow);
		} else {
			heldInARow = 0;
		}
		if (!searching) {
			return;
		}
		const nav::RouteMeshEpochs epochs{.labels = detailMesh.components().epoch(), .patches = detailMesh.epoch()};
		sliceStarted = true;
		routeWorker->start([this, epochs] {
			const Clock::time_point start = Clock::now();
			routes.runSlice(detailMesh.view(), epochs);
			sliceMs = msSince(start);
		});
	}

	void NavigationSystem::dropUntakenAnswers() {
		while (!takeableSince.empty()) {
			const auto [tick, ticket] = takeableSince.front();
			const bool untaken		  = answeredAtOnce.contains(ticket) || queuedRoutes.contains(ticket);
			if (untaken && routeTick - tick < kUntakenAnswerTicks) {
				return;
			}
			takeableSince.pop_front();
			if (untaken) {
				LOG_DEBUG(Engine, "[Nav] route %llu answered %llu ticks ago and never taken: dropped", static_cast<unsigned long long>(ticket),
						  static_cast<unsigned long long>(routeTick - tick));
				cancelRoute(ticket);
				++routeStatsTotal.dropped;
			}
		}
	}

	void NavigationSystem::countOutstandingRoutes() {
		routeStatsTotal.outstanding = queuedRoutes.size() + answeredAtOnce.size();
	}

	void NavigationSystem::endTick() {
		submit();
	}

	void NavigationSystem::finishPendingBuilds() {
		do {
			settle();
			if (chunkManager != nullptr) {
				gather(kUnlimited, kUnlimited, kUnlimited, kUnlimited, kUnlimited, false);
			}
			settle();
		} while (chunkManager != nullptr && isBuilding());
	}

	void NavigationSystem::gather(std::size_t addBudget, std::size_t removeBudget, std::size_t cellBudget, std::size_t scanBudget,
								  std::size_t groundVertexBudget, bool holdFar) {
		collectAnchors();
		trackMovers();
		queueRemovedDetail();
		gatherWalls();
		gatherGround(groundVertexBudget);
		releaseAwaitingCells();
		gatherDetail(addBudget, removeBudget, cellBudget, scanBudget, holdFar);
	}

	void NavigationSystem::submit() {
		if (gatheredJob.empty()) {
			return;
		}
		collect();
		runningJob	= std::move(gatheredJob);
		gatheredJob = {};
		running		= true;
		published	= false;
		collected	= false;
		// The slice reads the mesh: it ends before the job writes.
		patchWorker->start([this] {
			routeWorker->wait();
			runJob(detailMesh, runningJob);
		});
	}

	void NavigationSystem::join() const {
		double waitMs = 0.0;
		if (running) {
			const Clock::time_point start = Clock::now();
			patchWorker->wait();
			waitMs	= msSince(start);
			running = false;
		}
		if (published) {
			return;
		}
		published = true;
		navStats.navPatches += runningJob.patches.size();
		navStats.groundWaterWallPatches += runningJob.routePatches;
		// Routes are restamped here, not on the next tick, since a query may join mid-frame and the
		// AI reads them next.
		if (runningJob.movesRoutes()) {
			++routeGeneration;
			restampRoutes(runningJob.blockingBoxes);
		}
		// Permanent nav-mesh work diagnostic: one tick's job on the patch worker (of which the width
		// refresh), how long the main thread waited for it, the mesh's triangle slots then, and how
		// many patches the job ran. DEBUG, dev-tools log server.
		LOG_DEBUG(Engine, "[NavBuild] job %.3f ms (widths %.3f ms) wait %.3f ms slots %zu patches %zu%s", runningJob.ms, runningJob.widthsMs, waitMs,
				  detailMesh.triangles().size(), runningJob.patches.size(), runningJob.ms > kJobBudgetMs ? " over budget" : "");
		// Permanent diagnostic for a job over budget: what each of its patches was and took.
		if (runningJob.ms > kJobBudgetMs) {
			for (std::size_t i = 0; i < runningJob.patches.size(); ++i) {
				LOG_DEBUG(Engine, "[NavBuild] patch %zu adds %zu removes %zu %.3f ms", i, runningJob.patches[i].adds.size(), runningJob.patches[i].removes.size(),
						  runningJob.patchMs[i]);
			}
		}
	}

	void NavigationSystem::collect() {
		if (collected) {
			return;
		}
		join();
		collected = true;
		detailLanded.insert(detailLanded.end(), runningJob.detailAddBoxes.begin(), runningJob.detailAddBoxes.end());
		routeReports.insert(routeReports.end(), std::make_move_iterator(runningJob.reports.begin()), std::make_move_iterator(runningJob.reports.end()));
		runningJob.reports.clear();
		for (std::size_t i = 0; i < runningJob.onPatched.size(); ++i) {
			runningJob.onPatched[i](runningJob.added[i], runningJob.patchMs[i]);
		}
		if (runningJob.onDoorsAdded) {
			runningJob.onDoorsAdded(runningJob.doorsAdded);
		}
	}

	void NavigationSystem::settle() {
		submit();
		collect();
	}

	namespace {
		std::pair<glm::vec2, glm::vec2> ringBoxM(const gnav::NavInputPolygon& polygon) {
			geometry::Vec2i64 lo = polygon.ring.front();
			geometry::Vec2i64 hi = polygon.ring.front();
			for (const geometry::Vec2i64& p : polygon.ring) {
				lo = {std::min(lo.x, p.x), std::min(lo.y, p.y)};
				hi = {std::max(hi.x, p.x), std::max(hi.y, p.y)};
			}
			return {nav::toMeters(lo), nav::toMeters(hi)};
		}

		std::pair<glm::vec2, glm::vec2> portalBoxM(const gnav::DoorPortal& portal) {
			return {nav::toMeters({std::min(portal.a.x, portal.b.x), std::min(portal.a.y, portal.b.y)}),
					nav::toMeters({std::max(portal.a.x, portal.b.x), std::max(portal.a.y, portal.b.y)})};
		}
	} // namespace

	// --- Walls and doors -------------------------------------------------------

	void NavigationSystem::gatherWalls() {
		const std::uint64_t version = constructionWorld != nullptr ? constructionWorld->version() : 0;
		if (version == wallsVersion) {
			return;
		}
		wallsVersion = version;

		std::vector<gnav::NavInputPolygon> polys;
		std::vector<gnav::DoorPortal>	   portals;
		if (constructionWorld != nullptr) {
			nav::extractWalls(*constructionWorld, assets::ConstructionRegistry::Get(), polys, portals);
		}

		// Rings that are still there keep their ids; the rest come out, and new ones go in.
		std::map<PolygonKey, std::vector<std::size_t>> fresh;
		for (std::size_t i = polys.size(); i-- > 0;) {
			fresh[polygonKey(polys[i])].push_back(i);
		}
		gnav::PatchEdits	  edits;
		std::vector<WallRing> next;
		for (WallRing& wall : walls) {
			auto it = fresh.find(polygonKey(wall.polygon));
			if (it != fresh.end() && !it->second.empty()) {
				it->second.pop_back();
				next.push_back(std::move(wall));
				continue;
			}
			edits.removes.push_back(wall.ring);
		}
		const std::size_t firstAdded = next.size();
		for (std::size_t i = 0; i < polys.size(); ++i) {
			std::vector<std::size_t>& unused = fresh[polygonKey(polys[i])];
			if (unused.empty() || unused.back() != i) {
				continue;
			}
			unused.pop_back();
			edits.adds.push_back(polys[i]);
			next.push_back({std::move(polys[i]), gnav::kNoRing});
		}
		walls = std::move(next);

		for (std::size_t k = firstAdded; k < walls.size(); ++k) {
			gatheredJob.blockingBoxes.push_back(ringBoxM(walls[k].polygon));
		}
		if (!edits.adds.empty() || !edits.removes.empty()) {
			gatheredJob.patches.push_back(std::move(edits));
			++gatheredJob.routePatches;
			gatheredJob.onPatched.emplace_back([this, firstAdded](const std::vector<RingId>& added, double) {
				for (std::size_t k = 0; k < added.size(); ++k) {
					walls[firstAdded + k].ring = added[k];
				}
			});
		}

		std::vector<WallDoor> nextDoors;
		std::vector<bool>	  portalKept(portals.size(), false);
		for (WallDoor& door : doors) {
			bool kept = false;
			for (std::size_t i = 0; i < portals.size() && !kept; ++i) {
				if (!portalKept[i] && samePortal(portals[i], door.portal)) {
					portalKept[i] = true;
					kept		  = true;
				}
			}
			if (kept) {
				nextDoors.push_back(door);
				continue;
			}
			gatheredJob.doorRemoves.push_back(door.door);
			gatheredJob.doorEdges.push_back(door.portal);
			gatheredJob.blockingBoxes.push_back(portalBoxM(door.portal));
		}
		const std::size_t firstDoor = nextDoors.size();
		for (std::size_t i = 0; i < portals.size(); ++i) {
			if (portalKept[i]) {
				continue;
			}
			gatheredJob.doorAdds.push_back(portals[i]);
			gatheredJob.doorEdges.push_back(portals[i]);
			nextDoors.push_back({portals[i], -1});
		}
		doors = std::move(nextDoors);
		if (firstDoor < doors.size()) {
			gatheredJob.onDoorsAdded = [this, firstDoor](const std::vector<DoorId>& added) {
				for (std::size_t k = 0; k < added.size(); ++k) {
					doors[firstDoor + k].door = added[k];
				}
			};
		}
	}

	// --- Ground ------------------------------------------------------------------

	namespace {
		constexpr gnav::RingId kGatheredRing = -2; // in a gathered patch, its id not yet known

		bool gathered(gnav::RingId id) { return id != gnav::kNoRing; }

		std::pair<glm::vec2, glm::vec2> chunkBoxM(world::ChunkCoordinate coord) {
			const geometry::Vec2i64 lo = nav::chunkOriginMm(coord);
			// Just inside the far sides, so the box meets this chunk and not its neighbours.
			return {nav::toMeters(lo), nav::toMeters({lo.x + kChunkMm - 1, lo.y + kChunkMm - 1})};
		}

		// A chunk's square enters the mesh with its sides cut to kMaxBoundsEdgeMm, so that is how
		// many vertices it brings.
		constexpr std::size_t kSquareVertices = static_cast<std::size_t>(4 * kChunkMm / NavigationSystem::kMaxBoundsEdgeMm);

		std::size_t waterVertices(const std::vector<gnav::NavInputPolygon>& water) {
			std::size_t vertices = 0;
			for (const gnav::NavInputPolygon& ring : water) {
				vertices += ring.ring.size();
			}
			return vertices;
		}
	} // namespace

	void NavigationSystem::rederiveChangedWater() {
		std::unordered_set<world::ChunkCoordinate> ready;
		for (const world::Chunk* chunk : std::as_const(*chunkManager).getLoadedChunks()) {
			if (!chunk->isReady()) {
				continue;
			}
			ready.insert(chunk->coordinate());
			const auto it = ground.find(chunk->coordinate());
			if (it == ground.end()) {
				continue;
			}
			const world::ChunkTerrainPolygons& polys = chunk->terrainPolygons();
			ChunkGround&					   g	 = it->second;
			if (!readyLastCall.contains(chunk->coordinate())) {
				g.terrainVersion = polys.version;
				continue;
			}
			if (g.terrainVersion == polys.version) {
				continue;
			}

			// Terrain changed under meshed ground: its old water comes out and the new water goes in,
			// in one patch.
			gnav::PatchEdits edits;
			edits.removes = g.water;
			nav::appendChunkWater(polys, edits.adds);
			g.water.assign(edits.adds.size(), kGatheredRing);
			g.terrainVersion = polys.version;
			gatheredJob.blockingBoxes.push_back(chunkBoxM(chunk->coordinate()));
			gatheredJob.patches.push_back(std::move(edits));
			++gatheredJob.routePatches;
			gatheredJob.onPatched.emplace_back([&g](const std::vector<RingId>& ids, double) {
				for (std::size_t k = 0; k < ids.size(); ++k) {
					g.water[k] = ids[k];
				}
			});
		}
		readyLastCall = std::move(ready);
	}

	void NavigationSystem::gatherGround(std::size_t vertexBudget) {
		rederiveChangedWater();
		// Open chunks share a tick until their vertices would pass the budget; a chunk with water
		// is the costliest kind of job there is, so it goes alone. Chunks not ready yet, or that no
		// mover holds (whatever loaded them), keep their place in the queue.
		const bool	unlimited = vertexBudget == kUnlimited;
		std::size_t taken	  = 0;
		bool		full	  = false;
		// True when it was meshed; `waiting` when it is held and ready but over the budget.
		const auto serve = [&](world::ChunkCoordinate coord, bool& waiting) {
			waiting					  = false;
			const world::Chunk* chunk = anchorHolds(coord) ? chunkManager->getChunk(coord) : nullptr;
			if (chunk == nullptr || !chunk->isReady()) {
				return false;
			}
			if (full) {
				waiting = true;
				return false;
			}
			std::vector<gnav::NavInputPolygon> water;
			nav::appendChunkWater(chunk->terrainPolygons(), water);
			const std::size_t cost = kSquareVertices + waterVertices(water);
			if (taken > 0 && !unlimited && (!water.empty() || cost > vertexBudget)) {
				full	= true;
				waiting = true;
				return false;
			}
			vertexBudget -= std::min(cost, vertexBudget);
			++taken;
			full = !water.empty() && !unlimited;
			meshGround(coord, std::move(water), chunk->terrainPolygons().version);
			return true;
		};
		bool waiting = false;
		std::erase_if(nearGround, [&](world::ChunkCoordinate coord) { return ground.contains(coord) || !anchorHolds(coord) || serve(coord, waiting); });
		std::size_t kept = 0;
		groundPending	 = 0;
		for (const world::ChunkCoordinate coord : groundQueue) {
			if (ground.contains(coord)) {
				continue; // the landing or the near lane meshed it
			}
			if (!serve(coord, waiting)) {
				groundPending += waiting ? 1 : 0;
				groundQueue[kept++] = coord;
			}
		}
		groundQueue.resize(kept);
	}

	void NavigationSystem::gatherGroundWithin(const Rect& within) {
		for (const world::Chunk* chunk : std::as_const(*chunkManager).getLoadedChunks()) {
			const world::ChunkCoordinate coord = chunk->coordinate();
			const geometry::Vec2i64		 lo	   = nav::chunkOriginMm(coord);
			const geometry::Vec2i64		 hi{lo.x + kChunkMm, lo.y + kChunkMm};
			if (!chunk->isReady() || ground.contains(coord) || hi.x < within.lo.x || lo.x > within.hi.x || hi.y < within.lo.y ||
				lo.y > within.hi.y) {
				continue;
			}
			std::vector<gnav::NavInputPolygon> water;
			nav::appendChunkWater(chunk->terrainPolygons(), water);
			meshGround(coord, std::move(water), chunk->terrainPolygons().version);
		}
	}

	void NavigationSystem::meshGround(world::ChunkCoordinate coord, std::vector<gnav::NavInputPolygon> water, std::uint32_t terrainVersion) {
		const std::size_t cost = kSquareVertices + waterVertices(water);
		ChunkGround&	  g	   = ground[coord];
		g.square			   = kGatheredRing;
		g.water.assign(water.size(), kGatheredRing);
		g.terrainVersion = terrainVersion;
		gnav::PatchEdits edits;
		edits.adds.push_back(nav::chunkSquare(coord));
		edits.adds.insert(edits.adds.end(), std::make_move_iterator(water.begin()), std::make_move_iterator(water.end()));
		const std::size_t rings = edits.adds.size();
		gatheredJob.patches.push_back(std::move(edits));
		++gatheredJob.routePatches;
		gatheredJob.onPatched.emplace_back([&g, coord, rings, cost](const std::vector<RingId>& ids, double ms) {
			g.square = ids[0];
			for (std::size_t k = 1; k < ids.size(); ++k) {
				g.water[k - 1] = ids[k];
			}
			// Permanent nav-mesh work diagnostic: one chunk's ground (square and water) entering the
			// mesh. DEBUG, dev-tools log server.
			LOG_DEBUG(Engine, "[NavBuild] ground (%d, %d) %.3f ms rings %zu vertices %zu", coord.x, coord.y, ms, rings, cost);
		});
	}

	bool NavigationSystem::chunkGroundMeshed(world::ChunkCoordinate coord) const {
		const auto it = ground.find(coord);
		return it != ground.end() && gathered(it->second.square);
	}

	// --- Detail ---------------------------------------------------------------------

	bool NavigationSystem::placementReady(world::ChunkCoordinate coord) const {
		return placement != nullptr && placement->getChunkIndex(coord) != nullptr && (processedChunks == nullptr || processedChunks->contains(coord));
	}

	void NavigationSystem::queueRemovedDetail() {
		if (placement == nullptr) {
			return;
		}
		for (const assets::PlacementExecutor::RemovedEntity& entry : placement->takeRemovals()) {
			const DetailKey key{entry.coord, cellOfEntity(entry.position), std::bit_cast<std::uint32_t>(entry.position.x),
							   std::bit_cast<std::uint32_t>(entry.position.y), entry.defName};
			if (const auto it = detailInMesh.find(key); it != detailInMesh.end()) {
				detailRemovals.push_back(it->second);
				detailInMesh.erase(it);
			} else {
				for (std::optional<ScannedCell>* adding : {&addingNear, &addingFar}) {
					if (!adding->has_value() || (*adding)->cell != key.cell) {
						continue;
					}
					auto&	   rings   = (*adding)->rings;
					const auto pending = std::find_if(rings.begin() + static_cast<std::ptrdiff_t>((*adding)->next), rings.end(),
													  [&key](const auto& entry) { return !(entry.first < key) && !(key < entry.first); });
					if (pending != rings.end()) {
						rings.erase(pending);
					}
				}
			}
		}
	}

	void NavigationSystem::releaseAwaitingCells() {
		for (auto it = cellsAwaitingChunk.begin(); it != cellsAwaitingChunk.end();) {
			if (!chunkGroundMeshed(it->first) || !placementReady(it->first) || !anchorHolds(it->first)) {
				++it;
				continue;
			}
			for (const std::int64_t cell : it->second) {
				(nearCells.contains(cell) ? nearReady : readyCells).push_back(cell);
			}
			it = cellsAwaitingChunk.erase(it);
		}
	}

	NavigationSystem::ScannedCell NavigationSystem::scanCell(std::int64_t cell) const {
		ScannedCell					 out{.cell = cell};
		const world::ChunkCoordinate chunk	  = cellChunk(cell);
		const assets::AssetRegistry& registry = assets::AssetRegistry::Get();
		// One cell owns each entity, by its position in [lo, lo + cell).
		const float lox	 = static_cast<float>(cellX(cell) * kDetailCellMm / geometry::kMillimetersPerMeter);
		const float loy	 = static_cast<float>(cellY(cell) * kDetailCellMm / geometry::kMillimetersPerMeter);
		const float side = static_cast<float>(kDetailCellMm / geometry::kMillimetersPerMeter);
		for (const assets::PlacedEntity* e : placement->getChunkIndex(chunk)->queryRect(lox, loy, lox + side, loy + side)) {
			if (e->position.x < lox || e->position.x >= lox + side || e->position.y < loy || e->position.y >= loy + side) {
				continue;
			}
			DetailKey key{chunk, cell, std::bit_cast<std::uint32_t>(e->position.x), std::bit_cast<std::uint32_t>(e->position.y), e->defName};
			if (detailInMesh.contains(key)) {
				continue;
			}
			std::optional<gnav::NavInputPolygon> ring = nav::placedObstacleRing(*e, registry);
			if (ring.has_value()) {
				out.rings.emplace_back(std::move(key), std::move(*ring));
			}
		}
		// Key order, so a cell's rings go in the same order however the index returns them.
		std::sort(out.rings.begin(), out.rings.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
		return out;
	}

	void NavigationSystem::gatherDetail(std::size_t addBudget, std::size_t removeBudget, std::size_t cellBudget, std::size_t scanBudget, bool holdFar) {
		// Removals, oldest first, one patch per run of one cell: a patch's width refresh covers the
		// box around everything it changed, so a patch spread over a chunk would refresh the chunk.
		std::size_t removeCount = std::min(removeBudget, detailRemovals.size());
		for (std::size_t i = 0, cells = 0; i < removeCount; ++i) {
			if (i == 0 || detailRemovals[i].cell != detailRemovals[i - 1].cell) {
				if (++cells > cellBudget) {
					removeCount = i;
				}
			}
		}
		for (std::size_t first = 0; first < removeCount;) {
			std::size_t		 last = first;
			gnav::PatchEdits edits;
			for (; last < removeCount && detailRemovals[last].cell == detailRemovals[first].cell; ++last) {
				edits.removes.push_back(detailRemovals[last].ring);
			}
			gatheredJob.patches.push_back(std::move(edits));
			gatheredJob.onPatched.emplace_back([](const std::vector<RingId>&, double) {});
			first = last;
		}
		detailRemovals.erase(detailRemovals.begin(), detailRemovals.begin() + static_cast<std::ptrdiff_t>(removeCount));

		// Adds: the cells round movers first, then, unless `holdFar`, the wanted cells first in, first
		// out, one patch per cell a tick; a cell larger than what is left of the ring budget goes in
		// over several ticks. Only a scan counts against scanBudget: a cell already scanned is dropped,
		// and one whose chunk unloaded or that no mover holds any more goes back to wait for its chunk.
		std::size_t scans	 = 0;
		const auto	nextCell = [this](std::deque<std::int64_t>& queue) -> std::optional<std::int64_t> {
			 while (!queue.empty()) {
				 const std::int64_t cell = queue.front();
				 queue.pop_front();
				 if (!pendingCells.contains(cell)) {
					 continue;
				 }
				 if (!cellServable(cell)) {
					 cellsAwaitingChunk[cellChunk(cell)].push_back(cell);
					 continue;
				 }
				 return cell;
			 }
			 return std::nullopt;
		};
		const auto fill = [&](std::optional<ScannedCell>& slot, std::deque<std::int64_t>& queue) {
			if (slot.has_value() || scans == scanBudget) {
				return slot.has_value();
			}
			const std::optional<std::int64_t> cell = nextCell(queue);
			if (!cell.has_value()) {
				return false;
			}
			++scans;
			pendingCells.erase(*cell);
			nearCells.erase(*cell);
			slot = scanCell(*cell);
			return true;
		};
		std::size_t rings = 0;
		std::size_t cells = 0;
		while (rings < addBudget && cells < cellBudget) {
			std::optional<ScannedCell>* slot = nullptr;
			if (fill(addingNear, nearReady)) {
				slot = &addingNear;
			} else if (!holdFar && fill(addingFar, readyCells)) {
				slot = &addingFar;
			} else {
				break;
			}
			ScannedCell&	  adding = **slot;
			const std::size_t take	 = std::min(adding.rings.size() - adding.next, addBudget - rings);
			if (take > 0) {
				gnav::PatchEdits	   edits;
				std::vector<DetailKey> keys;
				for (std::size_t k = adding.next; k < adding.next + take; ++k) {
					const std::vector<geometry::Vec2i64>& ring = adding.rings[k].second.ring;
					geometry::Vec2i64					  lo   = ring.front();
					geometry::Vec2i64					  hi   = ring.front();
					for (const geometry::Vec2i64& p : ring) {
						lo = {std::min(lo.x, p.x), std::min(lo.y, p.y)};
						hi = {std::max(hi.x, p.x), std::max(hi.y, p.y)};
					}
					gatheredJob.detailAddBoxes.emplace_back(nav::toMeters(lo), nav::toMeters(hi));
					edits.adds.push_back(std::move(adding.rings[k].second));
					keys.push_back(adding.rings[k].first);
				}
				gatheredJob.patches.push_back(std::move(edits));
				gatheredJob.onPatched.emplace_back([this, keys = std::move(keys), cell = adding.cell](const std::vector<RingId>& added, double) {
					for (std::size_t k = 0; k < keys.size(); ++k) {
						detailInMesh.emplace(keys[k], DetailRing{added[k], cell});
					}
				});
				adding.next += take;
				rings += take;
				++cells;
			}
			if (adding.next == adding.rings.size()) {
				slot->reset();
			}
		}
	}

	// --- The forward snap -------------------------------------------------------------

	namespace {
		// The part of a..b inside lo..hi, as parameters along it, or nothing.
		std::optional<std::pair<float, float>> clipToRect(glm::vec2 a, glm::vec2 b, glm::vec2 lo, glm::vec2 hi) {
			float t0 = 0.0F;
			float t1 = 1.0F;
			for (int axis = 0; axis < 2; ++axis) {
				const float d = b[axis] - a[axis];
				if (d == 0.0F) {
					if (a[axis] < lo[axis] || a[axis] > hi[axis]) {
						return std::nullopt;
					}
					continue;
				}
				float enter = (lo[axis] - a[axis]) / d;
				float leave = (hi[axis] - a[axis]) / d;
				if (enter > leave) {
					std::swap(enter, leave);
				}
				t0 = std::max(t0, enter);
				t1 = std::min(t1, leave);
				if (t0 > t1) {
					return std::nullopt;
				}
			}
			return std::pair{t0, t1};
		}
	} // namespace

	void NavigationSystem::markCrossedRoutes() {
		if (detailLanded.empty()) {
			return;
		}
		const std::vector<std::pair<glm::vec2, glm::vec2>> landed = std::move(detailLanded);
		detailLanded.clear();
		if (world == nullptr) {
			return;
		}
		// A piece of route that comes within the mover's radius of a new obstacle's box crosses it:
		// obstacles that were already there are no news to a route planned past them.
		const auto crosses = [&](glm::vec2 a, glm::vec2 b, float radius) {
			return std::any_of(landed.begin(), landed.end(), [&](const std::pair<glm::vec2, glm::vec2>& box) {
				return clipToRect(a, b, box.first - glm::vec2{radius}, box.second + glm::vec2{radius}).has_value();
			});
		};
		const std::uint64_t current = generation();
		for (auto [entity, position, path] : world->view<Position, NavPath>()) {
			if (path.done() || path.builtNavVersion != current) {
				continue;
			}
			const AgentRadius* agent  = world->getComponent<AgentRadius>(entity);
			const float		   radius = agent != nullptr ? agent->radiusMeters : AgentRadius{}.radiusMeters;
			glm::vec2		   from	  = position.value;
			bool			   crossed = false;
			for (std::size_t next = path.current; next < path.waypoints.size() && !crossed; ++next) {
				crossed = crosses(from, path.waypoints[next], radius);
				from	= path.waypoints[next];
			}
			if (crossed) {
				path.builtNavVersion = kStaleRoute;
			}
		}
	}

	void NavigationSystem::restampRoutes(const std::vector<std::pair<glm::vec2, glm::vec2>>& closed) const {
		if (world == nullptr) {
			return;
		}
		const std::uint64_t previous = routeGeneration - 1;
		// A route current before the change stays current unless ground closed across its rest.
		for (auto [entity, position, path] : world->view<Position, NavPath>()) {
			if (!path.valid || path.builtNavVersion != previous) {
				continue;
			}
			const AgentRadius* agent  = world->getComponent<AgentRadius>(entity);
			const float		   radius = agent != nullptr ? agent->radiusMeters : AgentRadius{}.radiusMeters;
			bool			   crossed = false;
			glm::vec2		   from	   = position.value;
			for (std::size_t next = path.current; next < path.waypoints.size() && !crossed; ++next) {
				for (const auto& [lo, hi] : closed) {
					if (clipToRect(from, path.waypoints[next], lo - glm::vec2{radius}, hi + glm::vec2{radius}).has_value()) {
						crossed = true;
						break;
					}
				}
				from = path.waypoints[next];
			}
			if (!crossed) {
				path.builtNavVersion = routeGeneration;
			}
		}
	}

	void NavigationSystem::markStrayedRoutes() {
		if (world == nullptr) {
			return;
		}
		const float			kOff2	= static_cast<float>(kOffRouteMm * kOffRouteMm) / 1.0e6F;
		const std::uint64_t current = generation();
		for (auto [entity, position, path] : world->view<Position, NavPath>()) {
			(void)entity;
			if (path.done() || path.builtNavVersion != current) {
				continue;
			}
			// The leg it is on runs from the waypoint it last passed to the one it steers for.
			const glm::vec2 to	 = path.waypoints[path.current];
			const glm::vec2 from = path.current > 0 ? path.waypoints[path.current - 1] : to;
			const glm::vec2 leg	 = to - from;
			const float		len2 = dist2(from, to);
			const float		t	 = len2 > 0.0F ? std::clamp(glm::dot(position.value - from, leg) / len2, 0.0F, 1.0F) : 0.0F;
			if (dist2(position.value, from + leg * t) > kOff2) {
				path.builtNavVersion = kStaleRoute;
			}
		}
	}

	bool NavigationSystem::placedObstacleInNavMesh(world::ChunkCoordinate chunk, const assets::PlacedEntity& entity) const {
		return detailInMesh.contains(
			DetailKey{chunk, cellOfEntity(entity.position), std::bit_cast<std::uint32_t>(entity.position.x), std::bit_cast<std::uint32_t>(entity.position.y),
					 entity.defName});
	}

	bool NavigationSystem::snapHolds(EntityID mover) const {
		return std::binary_search(snapHeld.begin(), snapHeld.end(), mover);
	}

	void NavigationSystem::snapMovers() {
		const std::vector<EntityID> heldBefore = std::move(snapHeld);
		snapHeld.clear();
		if (world == nullptr || detailMesh.liveTriangleCount() == 0) {
			return;
		}
		// A route is planned over what the mover remembers, so it can run through a wall it hasn't
		// seen: a candidate has to be walkable in truth, its disc clear on all sides, and the walk
		// stops at the first face that blocks in truth for a reason other than detail (a wall, water,
		// unmeshed ground). Nothing past that is a candidate. The disc is sampled out to its rim at
		// the snap step along 8 rays, so a wall band thinner than the radius can't fall between.
		const float step = static_cast<float>(kSnapStepMm) / static_cast<float>(geometry::kMillimetersPerMeter);

		const auto clearFor = [this, step](glm::vec2 p, float radius) {
			if (!truthWalkable(detailMesh, p)) {
				return false;
			}
			const int rings = std::max(1, static_cast<int>(std::ceil(radius / step)));
			for (int k = 0; k < 8; ++k) {
				const float	 angle = 6.2831853F * static_cast<float>(k) / 8.0F;
				const glm::vec2 ray{std::cos(angle), std::sin(angle)};
				for (int r = 1; r <= rings; ++r) {
					if (!truthWalkable(detailMesh, p + ray * (radius * static_cast<float>(r) / static_cast<float>(rings)))) {
						return false;
					}
				}
			}
			return true;
		};
		const auto stopsTheWalk = [this](glm::vec2 p) {
			const std::int32_t tri = detailMesh.locate(nav::toMm(p));
			if (tri < 0) {
				return true;
			}
			const gnav::NavTriangle& t = detailMesh.triangles()[static_cast<std::size_t>(tri)];
			return !gnav::truthTraversable(t) && !isDetailTag(t.faceBlocker);
		};

		for (auto [entity, position, agent] : world->view<Position, AgentRadius>()) {
			const std::int32_t tri = detailMesh.locate(nav::toMm(position.value));
			if (tri < 0 || !isDetailTag(detailMesh.triangles()[static_cast<std::size_t>(tri)].faceBlocker)) {
				continue;
			}
			NavPath*	path   = world->getComponent<NavPath>(entity);
			const float radius = agent.radiusMeters;

			// With a route: forward along what remains of it only, from where it stands to the
			// waypoint it is steering for, then on through the rest. Points behind it are never
			// candidates; with none ahead it stays put and its route goes stale, so the AI replans
			// from where it stands. Held last tick and its route still stale (no new one came), it has
			// no route.
			const bool stuck  = path != nullptr && path->builtNavVersion == kStaleRoute && std::binary_search(heldBefore.begin(), heldBefore.end(), entity);
			const bool routed = path != nullptr && !path->done() && !stuck;
			if (!routed) {
				// Without a route: the nearest point, out in rings of the snap step, that the route case
				// would take: walkable in truth with its clearance, reached in a straight line that
				// crosses no wall or water, so it stays on its own side of a wall. Never behind its
				// heading: the leg of a stale route it steers along, else its velocity.
				glm::vec2 heading{0.0F, 0.0F};
				if (path != nullptr && !path->done()) {
					const glm::vec2 to = path->waypoints[path->current];
					heading			   = to - (path->current > 0 ? path->waypoints[path->current - 1] : position.value);
				} else if (const Velocity* velocity = world->getComponent<Velocity>(entity); velocity != nullptr) {
					heading = velocity->value;
				}
				const auto reachable = [&](glm::vec2 to) {
					if (glm::dot(to - position.value, heading) < 0.0F) {
						return false;
					}
					const int steps = std::max(1, static_cast<int>(std::ceil(std::sqrt(dist2(position.value, to)) / step)));
					for (int i = 1; i <= steps; ++i) {
						if (stopsTheWalk(position.value + (to - position.value) * (static_cast<float>(i) / static_cast<float>(steps)))) {
							return false;
						}
					}
					return true;
				};
				constexpr int	kSpokes	  = 16;
				constexpr float kMaxReach = 4.0F;
				bool			snapped	  = false;
				for (float r = step; r <= kMaxReach && !snapped; r += step) {
					for (int k = 0; k < kSpokes && !snapped; ++k) {
						const float		angle = 6.2831853F * static_cast<float>(k) / static_cast<float>(kSpokes);
						const glm::vec2 spot  = position.value + r * glm::vec2{std::cos(angle), std::sin(angle)};
						if (clearFor(spot, radius) && reachable(spot)) {
							position.value = spot;
							snapped		   = true;
						}
					}
				}
				if (!snapped) {
					snapHeld.push_back(entity);
				}
				continue;
			}
			bool	  moved	  = false;
			bool	  stopped = false;
			glm::vec2 from	  = position.value;
			for (std::size_t next = path->current; next < path->waypoints.size() && !moved && !stopped; ++next) {
				const glm::vec2 to	   = path->waypoints[next];
				const float		length = std::sqrt(dist2(from, to));
				const int		steps  = std::max(1, static_cast<int>(std::ceil(length / step)));
				for (int i = 1; i <= steps && !moved && !stopped; ++i) {
					const glm::vec2 p = from + (to - from) * (static_cast<float>(i) / static_cast<float>(steps));
					if (stopsTheWalk(p)) {
						stopped = true;
					} else if (clearFor(p, radius)) {
						position.value = p;
						path->current  = next;
						moved		   = true;
					}
				}
				from = to;
			}
			if (!moved) {
				path->builtNavVersion = kStaleRoute;
				snapHeld.push_back(entity);
			}
		}
		std::sort(snapHeld.begin(), snapHeld.end());
	}

	// --- Landing -------------------------------------------------------------------

	NavigationSystem::GroupSpawn NavigationSystem::groupSpawnPoints(glm::vec2 drop, std::size_t count, float ringRadiusMeters,
																	float minSeparationMeters, float clearanceMeters) {
		// Candidate search around a seed: the seed, then rings kSearchStepM apart, kSearchAngles
		// points each, out to kSearchRings -- nearest first, a fixed order so a landing is
		// deterministic.
		static constexpr float kSearchStepM	 = 0.5F;
		static constexpr int   kSearchRings	 = 40; // 20 m
		static constexpr int   kSearchAngles = 16;
		static constexpr float kTwoPi		 = 6.2831853F;
		const float			   reachM		 = kSearchStepM * static_cast<float>(kSearchRings);

		auto ringPoints = [count, ringRadiusMeters](glm::vec2 center) {
			std::vector<glm::vec2> pts;
			pts.reserve(count);
			for (std::size_t i = 0; i < count; ++i) {
				if (count == 1) {
					pts.push_back(center);
					break;
				}
				const float angle = kTwoPi * static_cast<float>(i) / static_cast<float>(count);
				pts.push_back(center + glm::vec2{std::cos(angle), std::sin(angle)} * ringRadiusMeters);
			}
			return pts;
		};

		GroupSpawn out{drop, ringPoints(drop)};
		if (chunkManager == nullptr || count == 0) {
			return out;
		}

		// The landing runs before the first update: bring in walls and the ground every point the
		// searches can touch stands on, and wait for it.
		const float padM = 2.0F * reachM + ringRadiusMeters + clearanceMeters + 1.0F;
		const Rect	within{nav::toMm(drop - glm::vec2{padM, padM}), nav::toMm(drop + glm::vec2{padM, padM})};
		settle();
		gatherWalls();
		gatherGroundWithin(within);
		settle();

		auto clear = [this, clearanceMeters](glm::vec2 p) {
			if (!pointOnGround(detailMesh, p, landingFace)) {
				return false;
			}
			for (int k = 0; k < 8; ++k) {
				const float angle = kTwoPi * static_cast<float>(k) / 8.0F;
				if (!pointOnGround(detailMesh, p + glm::vec2{std::cos(angle), std::sin(angle)} * clearanceMeters, landingFace)) {
					return false;
				}
			}
			return true;
		};
		auto firstAround = [&](glm::vec2 seed, const auto& accept) -> std::optional<glm::vec2> {
			if (accept(seed)) {
				return seed;
			}
			for (int ring = 1; ring <= kSearchRings; ++ring) {
				const float r = kSearchStepM * static_cast<float>(ring);
				for (int a = 0; a < kSearchAngles; ++a) {
					const float		angle = kTwoPi * static_cast<float>(a) / static_cast<float>(kSearchAngles);
					const glm::vec2 c	  = seed + glm::vec2{std::cos(angle), std::sin(angle)} * r;
					if (accept(c)) {
						return c;
					}
				}
			}
			return std::nullopt;
		};

		// Bias the center inland: the nearest spot whose whole ring stands on clear ground.
		const glm::vec2 snapped		   = nearestTerrainWalkablePoint(drop).value_or(drop);
		const auto		wholeRingClear = [&](glm::vec2 c) {
			 const std::vector<glm::vec2> pts = ringPoints(c);
			 return std::all_of(pts.begin(), pts.end(), clear);
		};
		out.center = firstAround(snapped, wholeRingClear).value_or(snapped);

		const float minSep2 = minSeparationMeters * minSeparationMeters;
		out.points.clear();
		for (const glm::vec2& seed : ringPoints(out.center)) {
			const auto spaced = [&](glm::vec2 p) {
				for (const glm::vec2& q : out.points) {
					if (dist2(p, q) < minSep2) {
						return false;
					}
				}
				return clear(p);
			};
			out.points.push_back(firstAround(seed, spaced).value_or(nearestTerrainWalkablePoint(seed).value_or(seed)));
		}
		return out;
	}

	// --- State ---------------------------------------------------------------------

	bool NavigationSystem::hasMesh() const {
		join();
		return detailMesh.liveTriangleCount() > 0;
	}

	bool NavigationSystem::isBuilding() const {
		join();
		return groundPending > 0 || !nearReady.empty() || !readyCells.empty() || addingNear.has_value() || addingFar.has_value() || !detailRemovals.empty() || !gatheredJob.empty();
	}

	std::uint64_t NavigationSystem::generation() const {
		join();
		return routeGeneration;
	}

	const geometry::nav::PatchableMesh& NavigationSystem::navMesh() const {
		join();
		return detailMesh;
	}

	std::vector<NavigationSystem::RegionView> NavigationSystem::builtRegions() const {
		join();
		if (ground.empty()) {
			return {};
		}
		geometry::Vec2i64 lo = nav::chunkOriginMm(ground.begin()->first);
		geometry::Vec2i64 hi = lo;
		for (const auto& [coord, chunkGround] : ground) {
			const geometry::Vec2i64 o = nav::chunkOriginMm(coord);
			lo						  = {std::min(lo.x, o.x), std::min(lo.y, o.y)};
			hi						  = {std::max(hi.x, o.x + kChunkMm), std::max(hi.y, o.y + kChunkMm)};
		}
		return {{{(lo.x + hi.x) / 2, (lo.y + hi.y) / 2}, std::max(hi.x - lo.x, hi.y - lo.y) / 2}};
	}

	// --- Queries -------------------------------------------------------------------

	bool NavigationSystem::inSimArea(glm::vec2 meters) const {
		join();
		return detailMesh.locate(nav::toMm(meters)) >= 0;
	}

	nav::RouteTicket NavigationSystem::requestRoute(glm::vec2 startMeters, std::span<const glm::vec2> goalsMeters, float agentRadiusMeters,
												   const gnav::BeliefFilter& belief) {
		join();
		const nav::RouteTicket ticket = nextRouteTicket++;
		++routeStatsTotal.requests;
		const geometry::Vec2i64 startMm = nav::toMm(startMeters);
		const bool				startOnMesh = detailMesh.locate(startMm) >= 0;
		nav::RouteRequestArgs	args{.start = startMm, .agentRadiusMm = radiusMm(agentRadiusMeters), .belief = belief};
		std::vector<std::int32_t> goalIndices;
		const gnav::NavView		  view	   = detailMesh.view();
		const std::int32_t		  startTri = startOnMesh ? gnav::standingTriangle(view, startMm, belief) : -1;
		if (startTri >= 0) {
			const gnav::MeshComponents& labels = detailMesh.components();
			for (std::size_t i = 0; i < goalsMeters.size(); ++i) {
				const geometry::Vec2i64 goalMm	= nav::toMm(goalsMeters[i]);
				const std::int32_t		goalTri = gnav::standingTriangle(view, goalMm, belief);
				if (goalTri >= 0 && labels.sameComponent(startTri, goalTri)) {
					args.goals.push_back(goalMm);
					goalIndices.push_back(static_cast<std::int32_t>(i));
				}
			}
		}
		if (args.goals.empty()) {
			++routeStatsTotal.answeredAtOnce;
			answeredAtOnce.emplace(ticket, RouteAnswer{.outcome	   = startOnMesh ? nav::RouteOutcome::NoKnownWay : nav::RouteOutcome::StartOffMesh,
													   .labelEpoch = detailMesh.components().epoch(),
													   .patchEpoch = detailMesh.epoch()});
			takeableSince.emplace_back(routeTick, ticket);
			countOutstandingRoutes();
			return ticket;
		}
		const nav::RouteTicket queued = routes.request(args);
		queuedRoutes.emplace(ticket, QueuedRoute{.queued = queued, .goalIndices = std::move(goalIndices)});
		byQueueTicket.emplace(queued, ticket);
		countOutstandingRoutes();
		return ticket;
	}

	std::optional<NavigationSystem::RouteAnswer> NavigationSystem::takeRouteAnswer(nav::RouteTicket ticket) {
		if (const auto at = answeredAtOnce.find(ticket); at != answeredAtOnce.end()) {
			RouteAnswer answer = std::move(at->second);
			answeredAtOnce.erase(at);
			++routeStatsTotal.answered;
			countOutstandingRoutes();
			return answer;
		}
		const auto queued = queuedRoutes.find(ticket);
		if (queued == queuedRoutes.end()) {
			return std::nullopt;
		}
		std::optional<nav::RouteAnswer> found = routes.takeAnswer(queued->second.queued);
		if (!found.has_value()) {
			return std::nullopt;
		}
		RouteAnswer answer{.outcome	   = found->outcome,
						   .labelEpoch = found->epochs.labels,
						   .patchEpoch = found->epochs.patches,
						   .restarts   = found->restarts,
						   .expansions = found->path.nodesExpanded};
		if (found->outcome == nav::RouteOutcome::Routed) {
			answer.goalIndex = queued->second.goalIndices[static_cast<std::size_t>(found->goalIndex)];
			answer.waypoints.reserve(found->path.points.size());
			double lengthMm = 0.0;
			for (std::size_t i = 0; i < found->path.points.size(); ++i) {
				answer.waypoints.push_back(nav::toMeters(found->path.points[i]));
				if (i > 0) {
					const geometry::Vec2i64& a = found->path.points[i - 1];
					const geometry::Vec2i64& b = found->path.points[i];
					lengthMm += std::hypot(static_cast<double>(b.x - a.x), static_cast<double>(b.y - a.y));
				}
			}
			answer.lengthMeters = static_cast<float>(lengthMm / static_cast<double>(geometry::kMillimetersPerMeter));
		}
		byQueueTicket.erase(queued->second.queued);
		queuedRoutes.erase(queued);
		++routeStatsTotal.answered;
		routeStatsTotal.restarts += answer.restarts;
		countOutstandingRoutes();
		return answer;
	}

	bool NavigationSystem::routeTicketLive(nav::RouteTicket ticket) const {
		return answeredAtOnce.contains(ticket) || queuedRoutes.contains(ticket);
	}

	void NavigationSystem::cancelRoute(nav::RouteTicket ticket) {
		answeredAtOnce.erase(ticket);
		if (const auto queued = queuedRoutes.find(ticket); queued != queuedRoutes.end()) {
			routes.cancel(queued->second.queued);
			byQueueTicket.erase(queued->second.queued);
			queuedRoutes.erase(queued);
		}
		countOutstandingRoutes();
	}

	bool NavigationSystem::isOnMesh(glm::vec2 meters) const {
		join();
		return pointOnFace(detailMesh, meters, moverFace);
	}

	bool NavigationSystem::isValidPosition(glm::vec2 meters) const {
		join();
		return terrainWalkable(detailMesh, meters);
	}

	std::optional<NavigationSystem::PlacedObstacle> NavigationSystem::placedObstacleAt(glm::vec2 meters) const {
		const auto obstacleOf = [this](gnav::RingId id) {
			const std::vector<geometry::Vec2i64>& outline = detailMesh.ring(id).polygon.ring;
			glm::vec2							  centre{0.0F, 0.0F};
			for (const geometry::Vec2i64& v : outline) {
				centre += nav::toMeters(v);
			}
			centre /= static_cast<float>(outline.size());
			float extent2 = 0.0F;
			for (const geometry::Vec2i64& v : outline) {
				extent2 = std::max(extent2, dist2(centre, nav::toMeters(v)));
			}
			return PlacedObstacle{centre, std::sqrt(extent2)};
		};
		// The entity standing exactly here: its key sorts first among the keys of its position.
		const std::int64_t cell = cellOfEntity(meters);
		const DetailKey	   at{cellChunk(cell), cell, std::bit_cast<std::uint32_t>(meters.x), std::bit_cast<std::uint32_t>(meters.y), {}};
		if (const auto it = detailInMesh.lower_bound(at);
			it != detailInMesh.end() && it->first.chunk == at.chunk && it->first.cell == at.cell && it->first.xBits == at.xBits && it->first.yBits == at.yBits &&
			it->second.ring >= 0) {
			return obstacleOf(it->second.ring);
		}
		const std::int32_t tri = detailMesh.locate(nav::toMm(meters));
		if (tri < 0) {
			return std::nullopt;
		}
		std::optional<PlacedObstacle> nearest;
		for (const gnav::RingId id : detailMesh.cover(tri)) {
			if (!isDetailTag(detailMesh.ring(id).polygon.provenanceId)) {
				continue;
			}
			const PlacedObstacle obstacle = obstacleOf(id);
			if (!nearest.has_value() || dist2(meters, obstacle.centre) < dist2(meters, nearest->centre)) {
				nearest = obstacle;
			}
		}
		return nearest;
	}

	std::optional<glm::vec2> NavigationSystem::standPointBeside(glm::vec2 target, glm::vec2 from, float agentRadiusMeters) const {
		join();
		if (detailMesh.locate(nav::toMm(target)) < 0) {
			return std::nullopt;
		}
		// Walkable in truth on the nav mesh, so in no placed obstacle it holds.
		const auto standable = [&](glm::vec2 p) { return truthWalkable(detailMesh, p); };
		const std::optional<PlacedObstacle> collider = placedObstacleAt(target);
		if (!collider.has_value()) {
			if (standable(target)) {
				return target;
			}
			return nearestFaceOnMesh(detailMesh, target,
									 [this](std::int32_t t) { return gnav::truthTraversable(detailMesh.triangles()[static_cast<std::size_t>(t)]); });
		}
		// Round the collider at the disc's radius out from it, nearest the colonist first.
		const float								 standOff = collider->extent + agentRadiusMeters + 0.05F;
		constexpr int							 kSpots	  = 32;
		std::array<std::pair<float, glm::vec2>, kSpots> spots;
		for (int k = 0; k < kSpots; ++k) {
			const float		angle = 6.2831853F * static_cast<float>(k) / static_cast<float>(kSpots);
			const glm::vec2 spot  = collider->centre + standOff * glm::vec2{std::cos(angle), std::sin(angle)};
			spots[static_cast<std::size_t>(k)] = {dist2(spot, from), spot};
		}
		std::sort(spots.begin(), spots.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
		for (const auto& [d2, spot] : spots) {
			if (standable(spot)) {
				return spot;
			}
		}
		return std::nullopt;
	}

	bool NavigationSystem::isSegmentWalkable(glm::vec2 aMeters, glm::vec2 bMeters) const {
		return segmentWalkableUnder([this](glm::vec2 p) { return isValidPosition(p); }, aMeters, bMeters);
	}

	bool NavigationSystem::isPolylineWalkable(const std::vector<glm::vec2>& ptsMeters) const {
		return polylineWalkableUnder([this](glm::vec2 p) { return isValidPosition(p); }, ptsMeters);
	}

	bool NavigationSystem::isAreaWalkable(const std::vector<glm::vec2>& polygonMeters) const {
		return areaWalkableUnder([this](glm::vec2 p) { return isValidPosition(p); }, polygonMeters);
	}

	bool NavigationSystem::isAreaBuildable(const std::vector<glm::vec2>& polygonMeters) const {
		join();
		return areaWalkableUnder([this](glm::vec2 p) { return pointOnGround(detailMesh, p, buildableFace); }, polygonMeters);
	}

	bool NavigationSystem::isPointBuildable(glm::vec2 meters) const {
		join();
		return pointOnGround(detailMesh, meters, buildableFace);
	}

	std::optional<glm::vec2> NavigationSystem::nearestTerrainWalkablePoint(glm::vec2 meters) const {
		join();
		if (pointOnGround(detailMesh, meters, landingFace)) {
			return meters;
		}
		return nearestFaceOnMesh(detailMesh, meters, [this](std::int32_t t) { return landingFace(detailMesh, t); });
	}

	std::optional<glm::vec2> NavigationSystem::nearestPathablePoint(glm::vec2 meters) const {
		join();
		if (terrainWalkable(detailMesh, meters)) {
			return meters;
		}
		return nearestFaceOnMesh(detailMesh, meters,
								 [this](std::int32_t t) { return gnav::terrainTraversable(detailMesh.triangles()[static_cast<std::size_t>(t)]); });
	}

	bool NavigationSystem::connected(glm::vec2 aMeters, glm::vec2 bMeters) const {
		join();
		const std::int32_t a = detailMesh.locate(nav::toMm(aMeters));
		return a >= 0 && detailMesh.components().sameComponent(a, detailMesh.locate(nav::toMm(bMeters)));
	}

	geometry::nav::ComponentLabel NavigationSystem::componentAt(glm::vec2 meters) const {
		join();
		return detailMesh.components().label(detailMesh.locate(nav::toMm(meters)));
	}

	std::uint64_t NavigationSystem::connectivityEpoch() const {
		join();
		return detailMesh.components().epoch();
	}

	std::optional<glm::vec2> NavigationSystem::nearestPathablePoint(glm::vec2 meters, float agentRadiusMeters) const {
		const std::optional<glm::vec2> bare	 = nearestPathablePoint(meters);
		const std::vector<WallBandM>   bands = resolveBuiltBands(constructionWorld);
		if (!bare.has_value() || bands.empty()) {
			return bare;
		}
		auto onMesh = [this](glm::vec2 p) { return isValidPosition(p); };

		// Fast path: push the bare nearest-walkable point out of the bands; in the common case (a
		// roomy face beside the wall) it stays on that face and clears the band.
		const glm::vec2 ejected = ejectFromBands(*bare, agentRadiusMeters, bands);
		if (onMesh(ejected) && clearOfBands(ejected, agentRadiusMeters, bands)) {
			return ejected;
		}
		// The closest pocket is too thin to hold the agent clear of the wall: take the nearest
		// walkable face that has room, typically the wall's open side.
		if (const std::optional<glm::vec2> clear = nearestClearOnMesh(detailMesh, meters, agentRadiusMeters, bands, onMesh)) {
			return clear;
		}
		// Every walkable face is inside some band: the bare point still moves the colonist onto the
		// mesh, and the AI's recovery falls through to the colony-origin teleport if that fails.
		return bare;
	}

	std::optional<glm::vec2> NavigationSystem::findValidPositionNear(glm::vec2 origin, float minDistMeters, glm::vec2 preferredDir) const {
		if (!inSimArea(origin)) {
			return std::nullopt;
		}
		const float minDist = std::max(minDistMeters, 0.0F);

		// Canonical direction: the preferred bias if non-zero, else +X.
		glm::vec2	dir{1.0F, 0.0F};
		const float dirLen2 = preferredDir.x * preferredDir.x + preferredDir.y * preferredDir.y;
		if (dirLen2 > 1e-12F) {
			dir = preferredDir / std::sqrt(dirLen2);
		}

		// The preferred candidate, then a fixed ring at the same radius starting at `dir`.
		const glm::vec2 preferred = origin + dir * minDist;
		if (isValidPosition(preferred)) {
			return preferred;
		}
		constexpr int	kRingSamples = 32;
		constexpr float kTwoPi		 = 6.28318530717958647692F;
		const float		baseAngle	 = std::atan2(dir.y, dir.x);
		for (int i = 1; i < kRingSamples; ++i) {
			const float		a = baseAngle + kTwoPi * (static_cast<float>(i) / static_cast<float>(kRingSamples));
			const glm::vec2 cand{origin.x + minDist * std::cos(a), origin.y + minDist * std::sin(a)};
			if (isValidPosition(cand)) {
				return cand;
			}
		}

		// No ring sample is on mesh (origin wedged in a pocket): the nearest walkable point, pushed
		// radially out to minDist when that push stays on mesh.
		const std::optional<glm::vec2> nearest = nearestPathablePoint(origin);
		if (!nearest) {
			return std::nullopt;
		}
		const glm::vec2 away	= *nearest - origin;
		const float		awayLen = std::sqrt(away.x * away.x + away.y * away.y);
		if (awayLen >= minDist) {
			return *nearest;
		}
		const glm::vec2 outDir = (awayLen > 1e-6F) ? away / awayLen : dir;
		const glm::vec2 pushed = origin + outDir * minDist;
		return isValidPosition(pushed) ? pushed : *nearest;
	}

} // namespace ecs
