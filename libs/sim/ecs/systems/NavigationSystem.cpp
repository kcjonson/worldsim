#include "NavigationSystem.h"

#include "../World.h"
#include "../components/AgentRadius.h"
#include "../components/Colonist.h"
#include "../components/NavPath.h"
#include "../components/Task.h"
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
		constexpr std::int64_t kCellsPerChunk = kChunkMm / NavigationSystem::kFloraCellMm;
		static_assert(kCellsPerChunk * NavigationSystem::kFloraCellMm == kChunkMm, "flora cells tile a chunk");

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
		// The scan cell that owns a placed entity: [lo, lo + side) on each axis, as scanFlora tests
		// it (the side is a power of two in metres, so the division is exact).
		std::int64_t cellOfEntity(glm::vec2 position) {
			constexpr float kSide = static_cast<float>(NavigationSystem::kFloraCellMm / geometry::kMillimetersPerMeter);
			return cellKey(static_cast<std::int64_t>(std::floor(position.x / kSide)), static_cast<std::int64_t>(std::floor(position.y / kSide)));
		}
		constexpr std::int64_t kFirstCell = std::numeric_limits<std::int64_t>::min();

		std::int64_t radiusMm(float meters) { return static_cast<std::int64_t>(std::llround(static_cast<double>(meters) * 1000.0)); }

		std::int64_t dist2Mm(const geometry::Vec2i64& a, const geometry::Vec2i64& b) {
			const std::int64_t dx = a.x - b.x;
			const std::int64_t dy = a.y - b.y;
			return dx * dx + dy * dy;
		}

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
		// adjacency, so a search touches only the triangles nearer than its answer. `visit` returns
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
				settled						 = std::min(settled, visit(tri, trianglePoints(mesh, tri)));
				for (const std::int32_t n : tri.neighbor) {
					if (n >= 0 && seen.insert(n).second) {
						open.push({triangleDist2(p, trianglePoints(mesh, mesh.triangles()[static_cast<std::size_t>(n)])), n});
					}
				}
			}
		}

		// Nearest point on `mesh` to `meters` whose face passes `faceOk`, nudged a hair toward that
		// face's centroid so it locates inside it, or nullopt when no face does.
		template <typename FaceOk>
		std::optional<glm::vec2> nearestFaceOnMesh(const gnav::PatchableMesh& mesh, glm::vec2 meters, const FaceOk& faceOk) {
			bool	  found	 = false;
			float	  bestD2 = 0.0F;
			glm::vec2 best{0.0F, 0.0F};
			constexpr float kNone = std::numeric_limits<float>::infinity();
			nearestFirst(mesh, meters, [&](const gnav::NavTriangle& t, const TriangleMeters& tri) {
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

		// Walkable for the most optimistic belief: only common-knowledge terrain (water, trees) blocks.
		bool terrainWalkable(const gnav::PatchableMesh& mesh, glm::vec2 meters) {
			return pointOnFace(mesh, meters, [](const gnav::NavTriangle& t) { return gnav::terrainTraversable(t); });
		}

		// Ground a mover may stand on: terrain-walkable, or a tree, which movement prices.
		bool moverFace(const gnav::NavTriangle& t) {
			return gnav::terrainTraversable(t) || t.faceBlocker == nav::kProvenanceTree;
		}

		// Walkable in truth: a solid wall face blocks, a door's span passes.
		bool truthWalkable(const gnav::PatchableMesh& mesh, glm::vec2 meters) {
			return pointOnFace(mesh, meters, [](const gnav::NavTriangle& t) { return gnav::truthTraversable(t); });
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
			nearestFirst(mesh, meters, [&](const gnav::NavTriangle& t, const TriangleMeters& tri) {
				if (gnav::terrainTraversable(t)) {
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

	// One mesh's share of a tick, on that mesh's worker.
	void NavigationSystem::runJob(gnav::PatchableMesh& mesh, MeshJob& job, bool forests) {
		const Clock::time_point start = Clock::now();
		for (const gnav::PatchEdits& edits : job.patches) {
			const Clock::time_point patchStart = Clock::now();
			job.added.push_back(mesh.applyPatch(edits).added);
			job.patchMs.push_back(msSince(patchStart));
		}
		for (const gnav::DoorId door : job.doorRemoves) {
			mesh.removeDoor(door);
		}
		for (const gnav::DoorPortal& portal : job.doorAdds) {
			job.doorsAdded.push_back(mesh.addDoor(portal));
		}
		const Clock::time_point finishStart = Clock::now();
		if (forests) {
			mesh.rebuildForests();
		} else {
			mesh.refreshWidths();
		}
		job.finishMs = msSince(finishStart);
		job.ms		 = msSince(start);
	}

	bool NavigationSystem::FloraKey::operator<(const FloraKey& o) const {
		if (chunk.y != o.chunk.y || chunk.x != o.chunk.x) {
			return ChunkLess{}(chunk, o.chunk);
		}
		return std::tie(cell, xBits, yBits, defName) < std::tie(o.cell, o.xBits, o.yBits, o.defName);
	}

	NavigationSystem::NavigationSystem(std::size_t floraRingBudget)
		: detailMesh({.maxBoundsEdgeMm = kMaxBoundsEdgeMm, .detailTags = {nav::kProvenanceTree}}),
		  structural({.maxBoundsEdgeMm = kMaxBoundsEdgeMm}),
		  navWorker(std::make_unique<Worker>()),
		  structuralWorker(std::make_unique<Worker>()),
		  floraRingBudget(floraRingBudget) {}

	NavigationSystem::~NavigationSystem() {
		join();
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

	void NavigationSystem::appendAnchors(std::vector<world::WorldPosition>& out) const {
		// Null only in headless tests with no colonists.
		if (world == nullptr) {
			return;
		}
		// Every mover the AI drives (a colonist), and the goal of one waiting for a mesh: its ground
		// has to come for the wait to end.
		for (auto [entity, position, task] : world->view<Position, Task>()) {
			(void)entity;
			out.emplace_back(position.value.x, position.value.y);
			if (task.state == TaskState::Moving && task.navState == NavState::AwaitingMesh) {
				out.emplace_back(task.targetPosition.x, task.targetPosition.y);
			}
		}
		// A route's rest, so the ground under it stays loaded and meshed: no two anchors along it
		// farther apart than half the load range, so every chunk it crosses is held.
		const float spacing = anchorLoadRangeMeters() / 2.0F;
		for (auto [entity, position, path] : world->view<Position, NavPath>()) {
			(void)entity;
			if (path.done()) {
				continue;
			}
			glm::vec2 from = position.value;
			for (std::size_t next = path.current; next < path.waypoints.size(); ++next) {
				const glm::vec2 to	   = path.waypoints[next];
				const float		length = std::sqrt(dist2(from, to));
				const int		pieces = std::max(1, static_cast<int>(std::ceil(length / spacing)));
				for (int i = 1; i <= pieces; ++i) {
					const glm::vec2 p = from + (to - from) * (static_cast<float>(i) / static_cast<float>(pieces));
					out.emplace_back(p.x, p.y);
				}
				from = to;
			}
		}
	}

	std::vector<NavigationSystem::Rect> NavigationSystem::wantedRects() const {
		if (!haveViewport) {
			return {};
		}
		return {viewportRect()};
	}

	void NavigationSystem::collectAnchors() {
		std::vector<world::WorldPosition> held;
		appendAnchors(held);
		anchors.clear();
		anchorChunks.clear();
		const float range = anchorLoadRangeMeters();
		for (const world::WorldPosition& p : held) {
			anchors.emplace_back(p.x, p.y);
			const world::ChunkCoordinate lo = world::worldToChunk({p.x - range, p.y - range});
			const world::ChunkCoordinate hi = world::worldToChunk({p.x + range, p.y + range});
			for (std::int32_t cy = lo.y; cy <= hi.y; ++cy) {
				for (std::int32_t cx = lo.x; cx <= hi.x; ++cx) {
					anchorChunks.insert({cx, cy});
				}
			}
		}
	}

	bool NavigationSystem::anchorHolds(world::ChunkCoordinate coord) const {
		return anchorChunks.contains(coord);
	}

	void NavigationSystem::noteStructural(glm::vec2 lo, glm::vec2 hi, StructuralChange change) {
		const bool					 content = change == StructuralChange::Opens || change == StructuralChange::Closes;
		const world::ChunkCoordinate a		 = world::worldToChunk({lo.x, lo.y});
		const world::ChunkCoordinate b		 = world::worldToChunk({hi.x, hi.y});
		for (std::int32_t cy = a.y; cy <= b.y; ++cy) {
			for (std::int32_t cx = a.x; cx <= b.x; ++cx) {
				if (content) {
					gatheredStructural.structuralChunks.push_back({cx, cy});
				}
				gatheredStructural.anchored = gatheredStructural.anchored || anchorHolds({cx, cy});
			}
		}
		if (change == StructuralChange::Closes || change == StructuralChange::GroundLeaves) {
			gatheredStructural.blockingBoxes.emplace_back(lo, hi);
		}
	}

	bool NavigationSystem::groundMeshed(world::ChunkCoordinate coord) const {
		const auto it = ground.find(coord);
		return it != ground.end() && it->second.square.structural >= 0; // a gathered patch has no ring id yet
	}

	void NavigationSystem::forChunksMeeting(const geometry::RectMm& box, const std::function<void(world::ChunkCoordinate)>& visit) {
		static_assert(world::kTileSize == 1.0F, "a chunk's side in mm assumes 1 m tiles");
		constexpr std::int64_t kChunkMm = static_cast<std::int64_t>(world::kChunkSize) * geometry::kMillimetersPerMeter;
		// Chunk c's interior (c * side, (c + 1) * side) meets (min, max) when c * side < max and
		// (c + 1) * side > min.
		const auto first = [](std::int64_t min) { return static_cast<std::int32_t>(geometry::floorDiv(min, kChunkMm)); };
		const auto last	 = [](std::int64_t max) { return static_cast<std::int32_t>(geometry::floorDiv(max - 1, kChunkMm)); };
		for (std::int32_t cy = first(box.min.y); cy <= last(box.max.y); ++cy) {
			for (std::int32_t cx = first(box.min.x); cx <= last(box.max.x); ++cx) {
				visit({cx, cy});
			}
		}
	}

	bool NavigationSystem::groundMeshedThrough(const geometry::RectMm& box) const {
		bool meshed = true;
		forChunksMeeting(box, [&](world::ChunkCoordinate coord) {
			if (groundMeshed(coord)) {
				return;
			}
			meshed = false;
			// Every chunk the square meets is within the colonist's anchor load range, so its ground is
			// on the way; one the chunk manager hasn't loaded either is noted once, in case it never is.
			const bool loaded = chunkManager != nullptr && std::as_const(*chunkManager).getChunk(coord) != nullptr;
			if (!loaded && unmeshedReported.insert(coord).second) {
				LOG_DEBUG(Engine, "[Nav] reach square meets chunk (%d, %d), not loaded yet: its colonists read unsettled until it is", coord.x, coord.y);
			}
		});
		return meshed;
	}

	NavigationSystem::Rect NavigationSystem::viewportRect() const {
		const geometry::Vec2i64 c  = nav::toMm(viewportCenterM);
		const std::int64_t		hx = std::clamp(radiusMm(viewportHalfM.x), kMinSimHalfExtentMm, kMaxSimHalfExtentMm);
		const std::int64_t		hy = std::clamp(radiusMm(viewportHalfM.y), kMinSimHalfExtentMm, kMaxSimHalfExtentMm);
		return {{c.x - hx, c.y - hy}, {c.x + hx, c.y + hy}};
	}

	// --- Ticks, jobs and the join -------------------------------------------------

	void NavigationSystem::update(float /*deltaTime*/) {
		settle();
		++tickCount;
		updateDetail();
		forgetDroppedRoutes();
		markCrossedRoutes();
		markStrayedRoutes();
		snapMovers();
		if (chunkManager != nullptr) {
			gather(kFloraRingsPerTick, kFloraRemovalsPerTick, kFloraCellsPerTick, kGroundVerticesPerTick);
		}
	}

	void NavigationSystem::endTick() {
		submit();
	}

	void NavigationSystem::finishPendingBuilds() {
		do {
			settle();
			if (chunkManager != nullptr) {
				gather(kUnlimited, kUnlimited, kUnlimited, kUnlimited);
			}
			settle();
		} while (chunkManager != nullptr && isBuilding());
	}

	void NavigationSystem::gather(std::size_t addBudget, std::size_t removeBudget, std::size_t cellBudget, std::size_t groundVertexBudget) {
		collectAnchors();
		const std::vector<Rect> rects = wantedRects();
		queueRemovedFlora();
		dropFloraOfLostChunks();
		gatherWalls();
		gatherGround(groundVertexBudget, rects, nullptr);
		scanFlora(rects);
		trimFlora(rects, removeBudget);
		gatherFlora(addBudget, removeBudget, cellBudget, rects);
	}

	void NavigationSystem::submit() {
		if (gatheredNav.empty() && gatheredStructural.empty()) {
			return;
		}
		collect();
		runningNav		   = std::move(gatheredNav);
		runningStructural  = std::move(gatheredStructural);
		gatheredNav		   = {};
		gatheredStructural = {};
		running			   = true;
		published		   = false;
		collected		   = false;
		if (!runningNav.empty()) {
			navWorker->start([this] { runJob(detailMesh, runningNav, false); });
		}
		if (!runningStructural.empty()) {
			structuralWorker->start([this] { runJob(structural, runningStructural, true); });
		}
	}

	void NavigationSystem::join() const {
		double waitMs = 0.0;
		if (running) {
			const Clock::time_point start = Clock::now();
			navWorker->wait();
			structuralWorker->wait();
			waitMs	= msSince(start);
			running = false;
		}
		if (published) {
			return;
		}
		published = true;
		navStats.navPatches += runningNav.patches.size();
		navStats.structuralPatches += runningStructural.patches.size();
		if (!runningStructural.empty()) {
			for (const world::ChunkCoordinate coord : runningStructural.structuralChunks) {
				++chunkStructuralVersion[coord];
			}
			// Ground the camera alone holds changes no colonist's route or wait. Routes are restamped
			// here, not on the next tick, since a query may join mid-frame and the AI reads them next.
			if (runningStructural.anchored) {
				++structuralGeneration;
				restampRoutes(runningStructural.blockingBoxes);
			}
		}
		const double longest = std::max(runningNav.ms, runningStructural.ms);
		// Permanent nav-mesh work diagnostic: one tick's job on each mesh's worker (of which the
		// width refresh and the forest rebuild), how long the main thread waited for it, the meshes'
		// triangle slots then, and how many patches each job ran. DEBUG, dev-tools log server.
		LOG_DEBUG(Engine,
				  "[NavBuild] job nav %.3f ms structural %.3f ms (widths %.3f ms forests %.3f ms) wait %.3f ms slots %zu structuralSlots %zu "
				  "patches %zu structuralPatches %zu%s",
				  runningNav.ms, runningStructural.ms, runningNav.finishMs, runningStructural.finishMs, waitMs, detailMesh.triangles().size(),
				  structural.triangles().size(), runningNav.patches.size(), runningStructural.patches.size(),
				  longest > kJobBudgetMs ? " over budget" : "");
		// Permanent diagnostic for a job over budget: what each of its patches was and took.
		if (longest > kJobBudgetMs) {
			for (const auto& [mesh, job] : {std::pair{"nav", &runningNav}, std::pair{"structural", &runningStructural}}) {
				for (std::size_t i = 0; i < job->patches.size(); ++i) {
					LOG_DEBUG(Engine, "[NavBuild] patch %s %zu adds %zu removes %zu %.3f ms", mesh, i, job->patches[i].adds.size(),
							  job->patches[i].removes.size(), job->patchMs[i]);
				}
			}
		}
	}

	void NavigationSystem::collect() {
		if (collected) {
			return;
		}
		join();
		collected = true;
		floraLanded.insert(floraLanded.end(), runningNav.floraAddBoxes.begin(), runningNav.floraAddBoxes.end());
		for (MeshJob* job : {&runningNav, &runningStructural}) {
			for (std::size_t i = 0; i < job->onPatched.size(); ++i) {
				job->onPatched[i](job->added[i], job->patchMs[i]);
			}
			if (job->onDoorsAdded) {
				job->onDoorsAdded(job->doorsAdded);
			}
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
		gnav::PatchEdits	  navEdits;
		gnav::PatchEdits	  structuralEdits;
		std::vector<WallRing> next;
		for (WallRing& wall : walls) {
			auto it = fresh.find(polygonKey(wall.polygon));
			if (it != fresh.end() && !it->second.empty()) {
				it->second.pop_back();
				next.push_back(std::move(wall));
				continue;
			}
			navEdits.removes.push_back(wall.ids.nav);
			structuralEdits.removes.push_back(wall.ids.structural);
			const auto [lo, hi] = ringBoxM(wall.polygon);
			noteStructural(lo, hi, StructuralChange::Opens);
		}
		const std::size_t firstAdded = next.size();
		for (std::size_t i = 0; i < polys.size(); ++i) {
			std::vector<std::size_t>& unused = fresh[polygonKey(polys[i])];
			if (unused.empty() || unused.back() != i) {
				continue;
			}
			unused.pop_back();
			navEdits.adds.push_back(polys[i]);
			structuralEdits.adds.push_back(polys[i]);
			next.push_back({std::move(polys[i]), {}});
		}
		walls = std::move(next);

		for (std::size_t k = firstAdded; k < walls.size(); ++k) {
			const auto [lo, hi] = ringBoxM(walls[k].polygon);
			noteStructural(lo, hi, StructuralChange::Closes);
		}
		if (!navEdits.adds.empty() || !navEdits.removes.empty()) {
			gatheredNav.patches.push_back(std::move(navEdits));
			gatheredNav.onPatched.emplace_back([this, firstAdded](const std::vector<RingId>& added, double) {
				for (std::size_t k = 0; k < added.size(); ++k) {
					walls[firstAdded + k].ids.nav = added[k];
				}
			});
			gatheredStructural.patches.push_back(std::move(structuralEdits));
			gatheredStructural.onPatched.emplace_back([this, firstAdded](const std::vector<RingId>& added, double) {
				for (std::size_t k = 0; k < added.size(); ++k) {
					walls[firstAdded + k].ids.structural = added[k];
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
			gatheredNav.doorRemoves.push_back(door.nav);
			gatheredStructural.doorRemoves.push_back(door.structural);
			const auto [lo, hi] = portalBoxM(door.portal);
			noteStructural(lo, hi, StructuralChange::Closes);
		}
		const std::size_t firstDoor = nextDoors.size();
		for (std::size_t i = 0; i < portals.size(); ++i) {
			if (portalKept[i]) {
				continue;
			}
			gatheredNav.doorAdds.push_back(portals[i]);
			gatheredStructural.doorAdds.push_back(portals[i]);
			const auto [lo, hi] = portalBoxM(portals[i]);
			noteStructural(lo, hi, StructuralChange::Opens);
			nextDoors.push_back({portals[i], -1, -1});
		}
		doors = std::move(nextDoors);
		if (firstDoor < doors.size()) {
			gatheredNav.onDoorsAdded = [this, firstDoor](const std::vector<DoorId>& added) {
				for (std::size_t k = 0; k < added.size(); ++k) {
					doors[firstDoor + k].nav = added[k];
				}
			};
			gatheredStructural.onDoorsAdded = [this, firstDoor](const std::vector<DoorId>& added) {
				for (std::size_t k = 0; k < added.size(); ++k) {
					doors[firstDoor + k].structural = added[k];
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

		// A chunk's square enters the meshes with its sides cut to kMaxBoundsEdgeMm, so that is how
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

	void NavigationSystem::noteLoadedChunks() {
		std::unordered_set<world::ChunkCoordinate> loaded;
		for (const world::Chunk* chunk : std::as_const(*chunkManager).getLoadedChunks()) {
			loaded.insert(chunk->coordinate());
			if (!chunk->isReady()) {
				continue;
			}
			const world::ChunkTerrainPolygons& polys = chunk->terrainPolygons();
			auto [it, added]						 = ground.try_emplace(chunk->coordinate());
			ChunkGround&					   g	 = it->second;
			g.loaded								 = true;
			if (added || !gathered(g.square.nav)) {
				g.terrainVersion = polys.version;
				continue;
			}
			if (g.terrainVersion == polys.version) {
				continue;
			}

			// Terrain changed under meshed ground: its old water comes out and the new water goes in,
			// in one patch per mesh.
			gnav::PatchEdits navEdits;
			gnav::PatchEdits structuralEdits;
			for (const RingPair& water : g.water) {
				navEdits.removes.push_back(water.nav);
				structuralEdits.removes.push_back(water.structural);
			}
			nav::appendChunkWater(polys, navEdits.adds);
			structuralEdits.adds = navEdits.adds;
			g.water.assign(navEdits.adds.size(), {kGatheredRing, kGatheredRing});
			g.terrainVersion = polys.version;
			++groundVersion;
			{
				const auto [lo, hi] = chunkBoxM(chunk->coordinate());
				noteStructural(lo, hi, StructuralChange::Closes);
			}
			gatheredNav.patches.push_back(std::move(navEdits));
			gatheredNav.onPatched.emplace_back([&g](const std::vector<RingId>& ids, double) {
				for (std::size_t k = 0; k < ids.size(); ++k) {
					g.water[k].nav = ids[k];
				}
			});
			gatheredStructural.patches.push_back(std::move(structuralEdits));
			gatheredStructural.onPatched.emplace_back([&g](const std::vector<RingId>& ids, double) {
				for (std::size_t k = 0; k < ids.size(); ++k) {
					g.water[k].structural = ids[k];
				}
			});
		}
		for (auto& [coord, g] : ground) {
			if (g.loaded && !loaded.contains(coord)) {
				g.loaded	 = false;
				g.unloadedAt = ++unloadTicker;
			}
		}
	}

	void NavigationSystem::gatherGround(std::size_t vertexBudget, const std::vector<Rect>& rects, const Rect* within) {
		noteLoadedChunks();

		struct Candidate {
			std::int64_t		   priority;
			world::ChunkCoordinate coord;
		};
		std::vector<Candidate> candidates;
		for (const auto& [coord, g] : ground) {
			if (!g.loaded || gathered(g.square.nav)) {
				continue;
			}
			const geometry::Vec2i64 lo = nav::chunkOriginMm(coord);
			const geometry::Vec2i64 hi{lo.x + kChunkMm, lo.y + kChunkMm};
			if (within != nullptr && (hi.x < within->lo.x || lo.x > within->hi.x || hi.y < within->lo.y || lo.y > within->hi.y)) {
				continue;
			}
			// Ground an anchor holds first, nearest an anchor first, so panning the camera never holds
			// a colonist's ground back; then the camera's, nearest the view first. Distances are
			// Chebyshev, in chunks.
			const auto chunksTo = [&coord](glm::vec2 at) {
				const world::ChunkCoordinate c = world::worldToChunk({at.x, at.y});
				return static_cast<std::int64_t>(std::max(std::abs(c.x - coord.x), std::abs(c.y - coord.y)));
			};
			constexpr std::int64_t kCameraOnly = std::int64_t{1} << 32;
			std::int64_t		   priority	   = kCameraOnly;
			if (anchorHolds(coord)) {
				for (const glm::vec2 anchor : anchors) {
					priority = std::min(priority, chunksTo(anchor));
				}
			} else {
				std::int64_t nearest = rects.empty() ? 0 : std::numeric_limits<std::int64_t>::max();
				for (const Rect& r : rects) {
					nearest = std::min(nearest, chunksTo(nav::toMeters({(r.lo.x + r.hi.x) / 2, (r.lo.y + r.hi.y) / 2})));
				}
				priority = kCameraOnly + nearest;
			}
			candidates.push_back({priority, coord});
		}
		std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
			return a.priority != b.priority ? a.priority < b.priority : ChunkLess{}(a.coord, b.coord);
		});

		// Open chunks share a tick until their vertices would pass the budget; a chunk with water
		// is the costliest kind of job there is, so it goes alone.
		const bool	unlimited = vertexBudget == kUnlimited;
		std::size_t taken	  = 0;
		for (const Candidate& c : candidates) {
			// Reloaded since the last tick and still generating: its polygons are being written.
			const world::Chunk* chunk = chunkManager->getChunk(c.coord);
			if (chunk == nullptr || !chunk->isReady()) {
				continue;
			}
			std::vector<gnav::NavInputPolygon> water;
			nav::appendChunkWater(chunk->terrainPolygons(), water);
			const std::size_t cost = kSquareVertices + waterVertices(water);
			if (taken > 0 && !unlimited && (!water.empty() || cost > vertexBudget)) {
				break;
			}
			vertexBudget -= std::min(cost, vertexBudget);
			++taken;

			ChunkGround& g = ground[c.coord];
			g.square	   = {kGatheredRing, kGatheredRing};
			g.water.assign(water.size(), {kGatheredRing, kGatheredRing});
			++groundVersion;
			{
				const auto [lo, hi] = chunkBoxM(c.coord);
				noteStructural(lo, hi, StructuralChange::GroundArrives);
			}
			gnav::PatchEdits navEdits;
			navEdits.adds.push_back(nav::chunkSquare(c.coord));
			navEdits.adds.insert(navEdits.adds.end(), water.begin(), water.end());
			gnav::PatchEdits structuralEdits = navEdits;
			const std::size_t rings			 = navEdits.adds.size();

			// The nav mesh's time is held for the structural mesh's sink, which collects second and
			// logs both.
			auto navMs = std::make_shared<double>(0.0);
			gatheredNav.patches.push_back(std::move(navEdits));
			gatheredNav.onPatched.emplace_back([&g, navMs](const std::vector<RingId>& ids, double ms) {
				g.square.nav = ids[0];
				for (std::size_t k = 1; k < ids.size(); ++k) {
					g.water[k - 1].nav = ids[k];
				}
				*navMs = ms;
			});
			gatheredStructural.patches.push_back(std::move(structuralEdits));
			gatheredStructural.onPatched.emplace_back([&g, navMs, coord = c.coord, rings, cost](const std::vector<RingId>& ids, double ms) {
				g.square.structural = ids[0];
				for (std::size_t k = 1; k < ids.size(); ++k) {
					g.water[k - 1].structural = ids[k];
				}
				// Permanent nav-mesh work diagnostic: one chunk's ground (square and water) entering
				// both meshes. DEBUG, dev-tools log server.
				LOG_DEBUG(Engine, "[NavBuild] ground (%d, %d) nav %.3f ms structural %.3f ms rings %zu vertices %zu", coord.x, coord.y,
						  *navMs, ms, rings, cost);
			});
			if (!water.empty() && !unlimited) {
				break;
			}
		}
		if (within != nullptr) {
			return;
		}
		// Ground leaves a chunk a tick, on a tick no ground arrived.
		groundPending = candidates.size() - taken + dropUnloadedGround(unlimited ? kUnlimited : (taken > 0 ? 0 : 1));
	}

	bool NavigationSystem::holdsConstruction(world::ChunkCoordinate coord) const {
		if (constructionWorld == nullptr) {
			return false;
		}
		// The chunk grown past any wall band's half thickness, so a band whose centreline runs just
		// outside it still holds it. Openings sit on their segments.
		constexpr std::int64_t	kConstructionReachMm = 1000;
		const geometry::Vec2i64 origin = nav::chunkOriginMm(coord);
		const geometry::Vec2i64 lo{origin.x - kConstructionReachMm, origin.y - kConstructionReachMm};
		const geometry::Vec2i64 hi{origin.x + kChunkMm + kConstructionReachMm, origin.y + kChunkMm + kConstructionReachMm};
		const auto				inside = [&](const geometry::Vec2i64& p) { return p.x >= lo.x && p.x <= hi.x && p.y >= lo.y && p.y <= hi.y; };
		// Liang-Barsky: does a..b meet the box?
		const auto crosses = [&](const geometry::Vec2i64& a, const geometry::Vec2i64& b) {
			double		 t0 = 0.0;
			double		 t1 = 1.0;
			const double d[2]	 = {static_cast<double>(b.x - a.x), static_cast<double>(b.y - a.y)};
			const double from[2] = {static_cast<double>(a.x), static_cast<double>(a.y)};
			const double mins[2] = {static_cast<double>(lo.x), static_cast<double>(lo.y)};
			const double maxs[2] = {static_cast<double>(hi.x), static_cast<double>(hi.y)};
			for (int axis = 0; axis < 2; ++axis) {
				if (d[axis] == 0.0) {
					if (from[axis] < mins[axis] || from[axis] > maxs[axis]) {
						return false;
					}
					continue;
				}
				double enter = (mins[axis] - from[axis]) / d[axis];
				double leave = (maxs[axis] - from[axis]) / d[axis];
				if (enter > leave) {
					std::swap(enter, leave);
				}
				t0 = std::max(t0, enter);
				t1 = std::min(t1, leave);
				if (t0 > t1) {
					return false;
				}
			}
			return true;
		};
		for (const construction::WallSegment& segment : constructionWorld->segments()) {
			const construction::Vertex* v0 = constructionWorld->getVertex(segment.v0);
			const construction::Vertex* v1 = constructionWorld->getVertex(segment.v1);
			if (v0 != nullptr && v1 != nullptr && crosses(v0->pos, v1->pos)) {
				return true;
			}
		}
		for (const construction::Foundation& foundation : constructionWorld->foundations()) {
			const geometry::Ring& ring = foundation.ring;
			for (std::size_t i = 0; i < ring.size(); ++i) {
				if (crosses(ring[i], ring[(i + 1) % ring.size()])) {
					return true;
				}
			}
			if (!ring.empty() && geometry::pointInPolygon(origin, ring) != geometry::PointInPolygon::Outside) {
				return true; // the chunk lies wholly inside the foundation
			}
		}
		return false;
	}

	std::size_t NavigationSystem::dropUnloadedGround(std::size_t maxChunks) {
		// Past the budget, the chunks unloaded longest ago that hold no construction lose their
		// ground, up to maxChunks of them, one patch per chunk, oldest first. Returns how many more
		// are still to go.
		std::vector<std::pair<std::uint64_t, world::ChunkCoordinate>> unloaded;
		for (const auto& [coord, g] : ground) {
			if (!g.loaded) {
				unloaded.emplace_back(g.unloadedAt, coord);
			}
		}
		std::sort(unloaded.begin(), unloaded.end(), [](const auto& a, const auto& b) { return a.first < b.first; }); // unloadedAt is unique
		std::size_t kept	 = unloaded.size();
		std::size_t taken	 = 0;
		std::size_t leftOver = 0;
		for (const auto& [at, coord] : unloaded) {
			if (kept <= kKeptUnloadedChunks) {
				break;
			}
			if (holdsConstruction(coord) || anchorHolds(coord)) {
				continue;
			}
			--kept;
			ChunkGround& g = ground[coord];
			if (!gathered(g.square.nav)) {
				ground.erase(coord);
				continue;
			}
			if (taken == maxChunks) {
				++leftOver;
				continue;
			}
			++taken;
			gnav::PatchEdits navEdits;
			gnav::PatchEdits structuralEdits;
			navEdits.removes.push_back(g.square.nav);
			structuralEdits.removes.push_back(g.square.structural);
			for (const RingPair& water : g.water) {
				navEdits.removes.push_back(water.nav);
				structuralEdits.removes.push_back(water.structural);
			}
			ground.erase(coord);
			++groundVersion;
			{
				const auto [lo, hi] = chunkBoxM(coord);
				noteStructural(lo, hi, StructuralChange::GroundLeaves);
			}
			gatheredNav.patches.push_back(std::move(navEdits));
			gatheredNav.onPatched.emplace_back([](const std::vector<RingId>&, double) {});
			gatheredStructural.patches.push_back(std::move(structuralEdits));
			gatheredStructural.onPatched.emplace_back([](const std::vector<RingId>&, double) {});
		}
		return leftOver;
	}

	bool NavigationSystem::chunkGroundMeshed(world::ChunkCoordinate coord) const {
		const auto it = ground.find(coord);
		return it != ground.end() && gathered(it->second.square.nav);
	}

	// --- Flora ---------------------------------------------------------------------

	bool NavigationSystem::floraChunkUsable(world::ChunkCoordinate coord) const {
		return placement != nullptr && chunkManager->getChunk(coord) != nullptr && placement->getChunkIndex(coord) != nullptr &&
			   (processedChunks == nullptr || processedChunks->contains(coord));
	}

	void NavigationSystem::queueRemovedFlora() {
		if (placement == nullptr) {
			return;
		}
		for (const assets::PlacementExecutor::RemovedEntity& entry : placement->takeRemovals()) {
			const FloraKey key{entry.coord, cellOfEntity(entry.position), std::bit_cast<std::uint32_t>(entry.position.x),
							   std::bit_cast<std::uint32_t>(entry.position.y), entry.defName};
			if (const auto it = floraInMesh.find(key); it != floraInMesh.end()) {
				floraRemovals.push_back(it->second);
				floraInMesh.erase(it);
			} else {
				floraAdds.erase(key);
			}
		}
	}

	void NavigationSystem::dropFloraOfLostChunks() {
		for (auto chunkIt = floraChunks.begin(); chunkIt != floraChunks.end();) {
			const world::ChunkCoordinate coord = *chunkIt;
			if (floraChunkUsable(coord)) {
				++chunkIt;
				continue;
			}
			// In key order, which is by cell, so the removals batch into one patch per cell.
			const FloraKey first{coord, kFirstCell, 0, 0, {}};
			for (auto it = floraInMesh.lower_bound(first); it != floraInMesh.end() && it->first.chunk == coord;) {
				floraRemovals.push_back(it->second);
				it = floraInMesh.erase(it);
			}
			for (auto it = floraAdds.lower_bound(first); it != floraAdds.end() && it->first.chunk == coord;) {
				it = floraAdds.erase(it);
			}
			std::erase_if(scannedCells, [coord](std::int64_t cell) { return cellChunk(cell) == coord; });
			std::erase_if(cellLastWanted, [coord](const auto& entry) { return cellChunk(entry.first) == coord; });
			chunkIt = floraChunks.erase(chunkIt);
		}
	}

	void NavigationSystem::scanFlora(const std::vector<Rect>& rects) {
		if (placement == nullptr) {
			return;
		}
		std::vector<CellRange> ranges;
		ranges.reserve(rects.size());
		for (const Rect& r : rects) {
			ranges.push_back({geometry::floorDiv(r.lo.x, kFloraCellMm), geometry::floorDiv(r.lo.y, kFloraCellMm),
							  geometry::floorDiv(r.hi.x, kFloraCellMm), geometry::floorDiv(r.hi.y, kFloraCellMm)});
		}
		// groundVersion moves whenever ground comes in, changes or goes out, so it can't alias; the
		// processed set can (one chunk leaves as another arrives), so it is compared whole.
		const std::unordered_set<world::ChunkCoordinate> noneProcessed;
		const std::unordered_set<world::ChunkCoordinate>& processed = processedChunks != nullptr ? *processedChunks : noneProcessed;
		if (ranges == scannedRanges && groundVersion == scannedGround && processed == scannedProcessed) {
			return;
		}
		scannedRanges	 = ranges;
		scannedGround	 = groundVersion;
		scannedProcessed = processed;

		std::unordered_set<std::int64_t> wanted;
		const assets::AssetRegistry&	 registry = assets::AssetRegistry::Get();
		for (const CellRange& range : ranges) {
			for (std::int64_t cy = range.minY; cy <= range.maxY; ++cy) {
				for (std::int64_t cx = range.minX; cx <= range.maxX; ++cx) {
					const std::int64_t cell = cellKey(cx, cy);
					wanted.insert(cell);
					if (scannedCells.contains(cell)) {
						cellLastWanted[cell] = tickCount;
						continue;
					}
					const world::ChunkCoordinate chunk = cellChunk(cell);
					if (!chunkGroundMeshed(chunk) || !floraChunkUsable(chunk)) {
						continue;
					}
					// One cell owns each entity, by its position in [lo, lo + cell).
					const float lox	 = static_cast<float>(cx * kFloraCellMm / geometry::kMillimetersPerMeter);
					const float loy	 = static_cast<float>(cy * kFloraCellMm / geometry::kMillimetersPerMeter);
					const float side = static_cast<float>(kFloraCellMm / geometry::kMillimetersPerMeter);
					for (const assets::PlacedEntity* e : placement->getChunkIndex(chunk)->queryRect(lox, loy, lox + side, loy + side)) {
						if (e->position.x < lox || e->position.x >= lox + side || e->position.y < loy || e->position.y >= loy + side) {
							continue;
						}
						FloraKey key{chunk, cell, std::bit_cast<std::uint32_t>(e->position.x), std::bit_cast<std::uint32_t>(e->position.y), e->defName};
						if (floraInMesh.contains(key) || floraAdds.contains(key)) {
							continue;
						}
						std::optional<gnav::NavInputPolygon> ring = nav::floraRingFor(*e, registry);
						if (!ring.has_value()) {
							continue;
						}
						floraAdds.emplace(std::move(key), PendingFlora{std::move(*ring), cell});
					}
					scannedCells.insert(cell);
					cellLastWanted[cell] = tickCount;
					floraChunks.insert(chunk);
				}
			}
		}
		// Flora still queued in a cell nothing wants any more is dropped, and the cell scanned
		// again if it is wanted later.
		for (auto it = floraAdds.begin(); it != floraAdds.end();) {
			if (wanted.contains(it->second.cell)) {
				++it;
				continue;
			}
			scannedCells.erase(it->second.cell);
			cellLastWanted.erase(it->second.cell);
			it = floraAdds.erase(it);
		}
	}

	void NavigationSystem::gatherFlora(std::size_t addBudget, std::size_t removeBudget, std::size_t cellBudget, const std::vector<Rect>& rects) {
		// Removals, oldest first, one patch per run of one cell: a patch's width refresh covers the
		// box around everything it changed, so a patch spread over a chunk would refresh the chunk.
		std::size_t removeCount = std::min(removeBudget, floraRemovals.size());
		for (std::size_t i = 0, cells = 0; i < removeCount; ++i) {
			if (i == 0 || floraRemovals[i].cell != floraRemovals[i - 1].cell) {
				if (++cells > cellBudget) {
					removeCount = i;
				}
			}
		}
		for (std::size_t first = 0; first < removeCount;) {
			std::size_t		 last = first;
			gnav::PatchEdits edits;
			for (; last < removeCount && floraRemovals[last].cell == floraRemovals[first].cell; ++last) {
				edits.removes.push_back(floraRemovals[last].ring);
			}
			gatheredNav.patches.push_back(std::move(edits));
			gatheredNav.onPatched.emplace_back([](const std::vector<RingId>&, double) {});
			first = last;
		}
		floraRemovals.erase(floraRemovals.begin(), floraRemovals.begin() + static_cast<std::ptrdiff_t>(removeCount));

		if (addBudget == 0 || floraAdds.empty()) {
			return;
		}
		// Cells nearest a rect centre first, a cell's flora together, map order within a cell.
		struct Ranked {
			std::int64_t							   priority;
			std::int64_t							   cell;
			std::size_t								   order;
			std::map<FloraKey, PendingFlora>::iterator it;
		};
		std::vector<Ranked> ranked;
		ranked.reserve(floraAdds.size());
		std::size_t order = 0;
		for (auto it = floraAdds.begin(); it != floraAdds.end(); ++it) {
			const std::int64_t		cell = it->second.cell;
			const geometry::Vec2i64 cellCenter{cellX(cell) * kFloraCellMm + kFloraCellMm / 2, cellY(cell) * kFloraCellMm + kFloraCellMm / 2};
			std::int64_t			priority = std::numeric_limits<std::int64_t>::max();
			for (const Rect& r : rects) {
				priority = std::min(priority, dist2Mm(cellCenter, {(r.lo.x + r.hi.x) / 2, (r.lo.y + r.hi.y) / 2}));
			}
			ranked.push_back({priority, cell, order++, it});
		}
		const std::size_t addCount = std::min(addBudget, ranked.size());
		const auto		  before   = [](const Ranked& a, const Ranked& b) {
			   return std::tie(a.priority, a.cell, a.order) < std::tie(b.priority, b.cell, b.order);
		};
		if (addCount < ranked.size()) {
			std::nth_element(ranked.begin(), ranked.begin() + static_cast<std::ptrdiff_t>(addCount), ranked.end(), before);
			ranked.resize(addCount);
		}
		std::sort(ranked.begin(), ranked.end(), before);
		for (std::size_t i = 0, cells = 0; i < ranked.size(); ++i) {
			if ((i == 0 || ranked[i].cell != ranked[i - 1].cell) && ++cells > cellBudget) {
				ranked.resize(i);
			}
		}

		for (std::size_t first = 0; first < ranked.size();) {
			std::size_t			  last = first;
			gnav::PatchEdits	  edits;
			std::vector<FloraKey> keys;
			for (; last < ranked.size() && ranked[last].cell == ranked[first].cell; ++last) {
				const std::vector<geometry::Vec2i64>& ring = ranked[last].it->second.ring.ring;
				geometry::Vec2i64					  lo   = ring.front();
				geometry::Vec2i64					  hi   = ring.front();
				for (const geometry::Vec2i64& p : ring) {
					lo = {std::min(lo.x, p.x), std::min(lo.y, p.y)};
					hi = {std::max(hi.x, p.x), std::max(hi.y, p.y)};
				}
				gatheredNav.floraAddBoxes.emplace_back(nav::toMeters(lo), nav::toMeters(hi));
				edits.adds.push_back(std::move(ranked[last].it->second.ring));
				keys.push_back(ranked[last].it->first);
				floraAdds.erase(ranked[last].it);
			}
			gatheredNav.patches.push_back(std::move(edits));
			gatheredNav.onPatched.emplace_back(
				[this, keys = std::move(keys), cell = ranked[first].cell](const std::vector<RingId>& added, double) {
					for (std::size_t k = 0; k < keys.size(); ++k) {
						floraInMesh.emplace(keys[k], FloraRing{added[k], cell});
					}
				});
			first = last;
		}
	}

	// --- Detail, the forward snap and the flora budget ---------------------------------

	namespace {
		std::int64_t cellAt(const geometry::Vec2i64& p) {
			return cellKey(geometry::floorDiv(p.x, NavigationSystem::kFloraCellMm), geometry::floorDiv(p.y, NavigationSystem::kFloraCellMm));
		}

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

		bool cellMeetsRect(std::int64_t cell, const geometry::Vec2i64& lo, const geometry::Vec2i64& hi) {
			const std::int64_t x = cellX(cell) * NavigationSystem::kFloraCellMm;
			const std::int64_t y = cellY(cell) * NavigationSystem::kFloraCellMm;
			return x <= hi.x && x + NavigationSystem::kFloraCellMm >= lo.x && y <= hi.y && y + NavigationSystem::kFloraCellMm >= lo.y;
		}
	} // namespace

	void NavigationSystem::updateDetail() {
		if (!haveViewport) {
			detailCells.clear();
			detailFor.reset();
			return;
		}
		const Rect view = viewportRect();
		if (detailFor.has_value() && detailFor->lo == view.lo && detailFor->hi == view.hi) {
			return;
		}
		detailFor = view;
		const geometry::Vec2i64 keepLo{view.lo.x - kDetailKeepMarginMm, view.lo.y - kDetailKeepMarginMm};
		const geometry::Vec2i64 keepHi{view.hi.x + kDetailKeepMarginMm, view.hi.y + kDetailKeepMarginMm};
		std::erase_if(detailCells, [&](std::int64_t cell) { return !cellMeetsRect(cell, keepLo, keepHi); });
		const geometry::Vec2i64 onLo{view.lo.x - kDetailOnMarginMm, view.lo.y - kDetailOnMarginMm};
		const geometry::Vec2i64 onHi{view.hi.x + kDetailOnMarginMm, view.hi.y + kDetailOnMarginMm};
		for (std::int64_t cy = geometry::floorDiv(onLo.y, kFloraCellMm); cy <= geometry::floorDiv(onHi.y, kFloraCellMm); ++cy) {
			for (std::int64_t cx = geometry::floorDiv(onLo.x, kFloraCellMm); cx <= geometry::floorDiv(onHi.x, kFloraCellMm); ++cx) {
				detailCells.insert(cellKey(cx, cy));
			}
		}
	}

	bool NavigationSystem::detailOnAt(glm::vec2 meters) const {
		return detailCells.contains(cellAt(nav::toMm(meters)));
	}

	gnav::BeliefFilter NavigationSystem::pricedFor(gnav::BeliefFilter belief) {
		static constexpr gnav::TagPrice kFloraPrices[] = {{.tag = nav::kProvenanceTree, .multiplier = kFloraPassMultiplier}};
		belief.prices								   = kFloraPrices;
		return belief;
	}

	std::uint64_t NavigationSystem::routeKey(const std::vector<glm::vec2>& waypoints) {
		std::uint64_t hash = 1469598103934665603ULL ^ waypoints.size();
		for (const glm::vec2 p : waypoints) {
			for (const float v : {p.x, p.y}) {
				hash = (hash ^ std::bit_cast<std::uint32_t>(v)) * 1099511628211ULL;
			}
		}
		return hash;
	}

	bool NavigationSystem::routeCrossesPricedFaces(const NavPath& path) const {
		if (!path.valid) {
			return false;
		}
		const auto it = pricedRoutes.find(routeKey(path.waypoints));
		return it != pricedRoutes.end() && it->second == path.waypoints;
	}

	void NavigationSystem::forgetDroppedRoutes() {
		// A requester's confinement lasts while its task does, or with no task instance while its route
		// does; then it's forgotten. A scope planned before its task had an instance id is the task's
		// first route, and is that instance's from here on.
		for (auto it = routeScopes.begin(); it != routeScopes.end();) {
			const bool	   alive = world != nullptr && world->isAlive(it->first);
			const Task*	   task	 = alive ? world->getComponent<Task>(it->first) : nullptr;
			const NavPath* path	 = alive ? world->getComponent<NavPath>(it->first) : nullptr;
			if (it->second.instance == 0 && task != nullptr && task->instanceId != 0) {
				it->second.instance = task->instanceId;
			}
			const bool held = it->second.instance != 0 ? task != nullptr && task->instanceId == it->second.instance
													   : path != nullptr && !path->done();
			it				= held ? std::next(it) : routeScopes.erase(it);
		}
		if (pricedRoutes.empty()) {
			return;
		}
		std::unordered_map<std::uint64_t, std::vector<glm::vec2>> live;
		if (world != nullptr) {
			for (auto [entity, path] : world->view<NavPath>()) {
				(void)entity;
				if (routeCrossesPricedFaces(path)) {
					live.emplace(routeKey(path.waypoints), path.waypoints);
				}
			}
		}
		pricedRoutes = std::move(live);
	}

	void NavigationSystem::markCrossedRoutes() {
		if (floraLanded.empty()) {
			return;
		}
		const std::vector<std::pair<glm::vec2, glm::vec2>> landed = std::move(floraLanded);
		floraLanded.clear();
		if (world == nullptr) {
			return;
		}
		// A piece of route that comes within the mover's radius of a new tree's box crosses it:
		// trees that were already there are no news to a route planned past or through them.
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
			for (std::size_t next = path.current; next < path.waypoints.size(); ++next) {
				if (crosses(from, path.waypoints[next], radius)) {
					path.builtNavVersion = kStaleRoute;
					break;
				}
				from = path.waypoints[next];
			}
		}
	}

	void NavigationSystem::restampRoutes(const std::vector<std::pair<glm::vec2, glm::vec2>>& closed) const {
		if (world == nullptr) {
			return;
		}
		const std::uint64_t previous = structuralGeneration - 1;
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
				path.builtNavVersion = structuralGeneration;
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

	bool NavigationSystem::floraInNavMesh(world::ChunkCoordinate chunk, const assets::PlacedEntity& entity) const {
		return floraInMesh.contains(
			FloraKey{chunk, cellOfEntity(entity.position), std::bit_cast<std::uint32_t>(entity.position.x), std::bit_cast<std::uint32_t>(entity.position.y),
					 entity.defName});
	}

	void NavigationSystem::snapMovers() {
		if (world == nullptr || detailMesh.liveTriangleCount() == 0) {
			return;
		}
		// A route is planned over what the mover remembers, so it can run through a wall it hasn't
		// seen: a candidate has to be walkable in truth, its disc clear on all sides, and the walk
		// stops at the first face that blocks in truth for a reason other than flora (a wall, water,
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
			return !gnav::truthTraversable(t) && t.faceBlocker != nav::kProvenanceTree;
		};

		// Only where detail is on is a mover pushed out of a tree, and so snapped out of one. A
		// mover whose route walks through flora on purpose, at its price, is left to it.
		const std::uint64_t current = generation();
		for (auto [entity, position, colonist] : world->view<Position, Colonist>()) {
			(void)colonist;
			if (!detailOnAt(position.value)) {
				continue;
			}
			const std::int32_t tri = detailMesh.locate(nav::toMm(position.value));
			if (tri < 0 || detailMesh.triangles()[static_cast<std::size_t>(tri)].faceBlocker != nav::kProvenanceTree) {
				continue;
			}
			NavPath* path = world->getComponent<NavPath>(entity);
			if (path != nullptr && !path->done() && routeCrossesPricedFaces(*path)) {
				continue;
			}
			const AgentRadius* agent  = world->getComponent<AgentRadius>(entity);
			const float		   radius = agent != nullptr ? agent->radiusMeters : AgentRadius{}.radiusMeters;

			// With a route: forward along what remains of it only, from where it stands to the
			// waypoint it is steering for, then on through the rest. Points behind it are never
			// candidates; with none ahead it stays put and its route goes stale, so the AI replans.
			if (path == nullptr || path->done()) {
				// Without a route: the nearest point, out in rings of the snap step, that the route case
				// would take: walkable in truth with its clearance, reached in a straight line that
				// crosses no wall or water, so it stays on its own side of a wall.
				const auto reachable = [&](glm::vec2 to) {
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
			}
		}
	}

	void NavigationSystem::trimFlora(const std::vector<Rect>& rects, std::size_t removeBudget) {
		if (trimQueue.empty() && floraInMesh.size() <= floraRingBudget) {
			return;
		}
		// Cells that may lose their flora: out of every wanted rect, out of the detail keep rect,
		// and not detail-on. The choice is over scanned cells, never the rings, and each cell's
		// rings are one range of floraInMesh, so what the main thread spends is the cells once per
		// trim and the rings that leave, a removal budget's worth a tick.
		std::optional<Rect> keep;
		if (haveViewport) {
			const Rect view = viewportRect();
			keep			= Rect{{view.lo.x - kDetailKeepMarginMm, view.lo.y - kDetailKeepMarginMm}, {view.hi.x + kDetailKeepMarginMm, view.hi.y + kDetailKeepMarginMm}};
		}
		const auto spared = [&](std::int64_t cell) {
			if (detailCells.contains(cell) || (keep.has_value() && cellMeetsRect(cell, keep->lo, keep->hi))) {
				return true;
			}
			return std::any_of(rects.begin(), rects.end(), [cell](const Rect& r) { return cellMeetsRect(cell, r.lo, r.hi); });
		};
		if (trimQueue.empty()) {
			std::vector<std::pair<std::uint64_t, std::int64_t>> oldest;
			for (const auto& [cell, wanted] : cellLastWanted) {
				if (!spared(cell)) {
					oldest.emplace_back(wanted, cell);
				}
			}
			std::sort(oldest.begin(), oldest.end(), std::greater<>());
			for (const auto& [wanted, cell] : oldest) {
				trimQueue.push_back(cell);
			}
		}
		const std::size_t target  = floraRingBudget - floraRingBudget / 8;
		std::size_t		  removed = 0;
		while (!trimQueue.empty() && floraInMesh.size() > target && removed < removeBudget) {
			const std::int64_t cell = trimQueue.back();
			trimQueue.pop_back();
			if (spared(cell) || !cellLastWanted.contains(cell)) {
				continue; // wanted again, or its chunk went, since it was queued
			}
			const world::ChunkCoordinate chunk = cellChunk(cell);
			for (auto it = floraInMesh.lower_bound(FloraKey{chunk, cell, 0, 0, {}});
				 it != floraInMesh.end() && it->first.chunk == chunk && it->first.cell == cell;) {
				floraRemovals.push_back(it->second);
				it = floraInMesh.erase(it);
				++removed;
			}
			scannedCells.erase(cell);
			cellLastWanted.erase(cell);
		}
		if (floraInMesh.size() <= target) {
			trimQueue.clear();
		}
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
		gatherGround(kUnlimited, {}, &within);
		settle();

		auto clear = [this, clearanceMeters](glm::vec2 p) {
			if (!truthWalkable(structural, p)) {
				return false;
			}
			for (int k = 0; k < 8; ++k) {
				const float angle = kTwoPi * static_cast<float>(k) / 8.0F;
				if (!truthWalkable(structural, p + glm::vec2{std::cos(angle), std::sin(angle)} * clearanceMeters)) {
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
		return groundPending > 0 || !floraAdds.empty() || !floraRemovals.empty() || !trimQueue.empty() || !gatheredNav.empty() ||
			   !gatheredStructural.empty();
	}

	std::uint64_t NavigationSystem::generation() const {
		join();
		return structuralGeneration;
	}

	const geometry::nav::PatchableMesh& NavigationSystem::navMesh() const {
		join();
		return detailMesh;
	}

	const geometry::nav::PatchableMesh& NavigationSystem::structuralMesh() const {
		join();
		return structural;
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

	std::optional<std::vector<glm::vec2>>
	NavigationSystem::requestPath(glm::vec2 startMeters, glm::vec2 goalMeters, float agentRadiusMeters, gnav::BeliefFilter belief,
								  RouteKind kind, EntityID requester) const {
		join();
		if (detailMesh.liveTriangleCount() == 0) {
			return std::nullopt;
		}
		const geometry::Vec2i64 startMm = nav::toMm(startMeters);
		const geometry::Vec2i64 goalMm	= nav::toMm(goalMeters);
		const std::int64_t		radius	= radiusMm(agentRadiusMeters);

		// An autonomous route (a colonist's own choice) is always confined, to the square round where
		// its task's first route started, the ground its anchor held when it chose: a goal outside it
		// has no route. A player's order isn't confined. Every later route of the same task (its next
		// leg, a replan) keeps the kind and the square the first had, so a detour or a second leg that
		// choice approved never fails because the square moved with the colonist, nor turns into
		// another kind of route when control changes hands. A task's first route is planned before its
		// instance id is given (the AI routes, then takes its claims), so the first instance to ask
		// takes up a scope that has none. A requester with no task instance (an order, a test) is held
		// to one goal instead. A start pushed out of the square by no more than
		// the mover's radius keeps it; one farther out (a teleport) takes the square round where it
		// now stands.
		const auto inside = [](const geometry::RectMm& r, const geometry::Vec2i64& p) {
			return p.x >= r.min.x && p.x <= r.max.x && p.y >= r.min.y && p.y <= r.max.y;
		};
		const auto grown = [](const geometry::RectMm& r, std::int64_t by) {
			return geometry::RectMm{{r.min.x - by, r.min.y - by}, {r.max.x + by, r.max.y + by}};
		};
		const Task*	   task		= requester != kInvalidEntity && world != nullptr ? world->getComponent<Task>(requester) : nullptr;
		const uint64_t instance = task != nullptr ? task->instanceId : 0;
		RouteScope	   scope{instance, goalMm, kind, kind == RouteKind::Autonomous ? std::optional(squareAround(startMm)) : std::nullopt};
		if (requester != kInvalidEntity) {
			const auto held = routeScopes.find(requester);
			if (held != routeScopes.end() &&
				(instance != 0 ? held->second.instance == instance || held->second.instance == 0
							   : held->second.instance == 0 && held->second.goalMm == goalMm)) {
				scope.kind	 = held->second.kind;
				scope.within = held->second.within;
				if (scope.within.has_value() && !inside(grown(*scope.within, radius), startMm)) {
					scope.within = squareAround(startMm);
				}
			}
			routeScopes[requester] = scope;
		}
		std::optional<geometry::RectMm> within = scope.within;
		if (within.has_value() && !inside(*within, startMm)) {
			within = grown(*within, radius);
		}
		const geometry::RectMm* confine = within.has_value() ? &*within : nullptr;

		// The structural mesh holds the nav mesh's ground without flora, so where it has no walk
		// under this belief (its known walls block) the nav mesh has none either: no nav search.
		const gnav::PathResult walk = gnav::pathThrough(structural.view(), startMm, goalMm, radius, belief, {.within = confine});
		if (walk.stop != gnav::PathStop::Found) {
			return std::nullopt;
		}

		// Flora only makes the nav search's corridors finer and its walk dearer, so it needs some
		// multiple of the structural search's expansions. Past that it is lost in priced ground, and
		// the route is the structural walk itself, through the flora at its price, as it would be off
		// camera: so a route exists on camera exactly when it does off it.
		const std::uint32_t	   cap	  = navExpansionFloor + navExpansionsPerStructural * walk.expansions;
		gnav::PathResult	   result = gnav::pathThrough(detailMesh.view(), startMm, goalMm, radius, pricedFor(belief),
														  {.within = confine, .maxExpansions = cap});
		if (result.stop == gnav::PathStop::LimitReached) {
			LOG_DEBUG(Engine, "[Nav] path query gave up after %u expansions (structural %u) from (%.2f, %.2f) to (%.2f, %.2f): walking the structural route",
					  result.expansions, walk.expansions, startMeters.x, startMeters.y, goalMeters.x, goalMeters.y);
			result					  = walk;
			result.crossesPricedFaces = passesTree(result.points, radius);
		} else if (!result.reachable) {
			return std::nullopt;
		}
		++navStats.totalQueries;
		navStats.totalNodesExpanded += static_cast<std::uint64_t>(result.nodesExpanded);
		navStats.lastNodesExpanded = result.nodesExpanded;
		navStats.lastPeakOpenSet   = result.peakOpenSet;
		LOG_DEBUG(Engine, "[Nav] path query expanded=%lld peakOpen=%lld (cumulative queries=%llu nodes=%llu)",
				  static_cast<long long>(result.nodesExpanded), static_cast<long long>(result.peakOpenSet),
				  static_cast<unsigned long long>(navStats.totalQueries), static_cast<unsigned long long>(navStats.totalNodesExpanded));

		std::vector<glm::vec2> waypoints;
		waypoints.reserve(result.points.size());
		for (const geometry::Vec2i64& p : result.points) {
			waypoints.push_back(nav::toMeters(p));
		}
		if (result.crossesPricedFaces) {
			pricedRoutes[routeKey(waypoints)] = waypoints;
		} else if (const auto it = pricedRoutes.find(routeKey(waypoints)); it != pricedRoutes.end() && it->second == waypoints) {
			pricedRoutes.erase(it);
		}
		return waypoints;
	}

	float NavigationSystem::anchorLoadRangeMeters() const {
		return chunkManager != nullptr ? chunkManager->anchorLoadRangeMeters() : world::ChunkManager::kDefaultAnchorLoadRangeMeters;
	}

	std::int64_t NavigationSystem::autonomousReachMm() const {
		return std::llround(static_cast<double>(anchorLoadRangeMeters()) * static_cast<double>(geometry::kMillimetersPerMeter));
	}

	geometry::RectMm NavigationSystem::squareAround(const geometry::Vec2i64& centerMm) const {
		const std::int64_t half = autonomousReachMm();
		return {{centerMm.x - half, centerMm.y - half}, {centerMm.x + half, centerMm.y + half}};
	}

	std::uint64_t NavigationSystem::chunkStamp(const geometry::RectMm& box, StampGround withGround) const {
		std::uint64_t stamp = 1469598103934665603ULL;
		forChunksMeeting(box, [&](world::ChunkCoordinate coord) {
			const auto			it		= chunkStructuralVersion.find(coord);
			const std::uint64_t version = it != chunkStructuralVersion.end() ? it->second : 0;
			stamp						= (stamp ^ version) * 1099511628211ULL;
			if (withGround == StampGround::With) {
				stamp = (stamp ^ (groundMeshed(coord) ? 1U : 0U)) * 1099511628211ULL;
			}
		});
		return stamp;
	}

	NavigationSystem::Reach NavigationSystem::reachFrom(glm::vec2 startMeters, float agentRadiusMeters, gnav::BeliefFilter belief) const {
		join();
		Reach reach;
		reach.belief				= belief;
		reach.startMm				= nav::toMm(startMeters);
		reach.radiusMm				= radiusMm(agentRadiusMeters);
		reach.onGround				= structural.locate(reach.startMm) >= 0;
		reach.startCell				= cellKey(geometry::floorDiv(reach.startMm.x, kReachCellMm), geometry::floorDiv(reach.startMm.y, kReachCellMm));
		reach.startRegionGeneration = chunkStamp(squareAround(reach.startMm), StampGround::With);
		reach.settled				= groundMeshedThrough(squareAround(reach.startMm));
		return reach;
	}

	bool NavigationSystem::sharesStartRegion(const Reach& reach, glm::vec2 goalMeters) const {
		return reach.onGround && inSquareOf(nav::toMeters(reach.startMm), goalMeters);
	}

	std::uint64_t NavigationSystem::verdictStamp(glm::vec2 anchor) const {
		join();
		const geometry::Vec2i64 c = nav::toMm(anchor);
		const std::int64_t		kHalf = 2 * autonomousReachMm();
		return chunkStamp({{c.x - kHalf, c.y - kHalf}, {c.x + kHalf, c.y + kHalf}}, StampGround::Without);
	}

	bool NavigationSystem::inSquareOf(glm::vec2 anchor, glm::vec2 here) const {
		const geometry::Vec2i64 a = nav::toMm(anchor);
		const geometry::Vec2i64 h = nav::toMm(here);
		return std::abs(h.x - a.x) <= autonomousReachMm() && std::abs(h.y - a.y) <= autonomousReachMm();
	}

	std::optional<float> NavigationSystem::walkDistance(const Reach& reach, glm::vec2 goalMeters, float maxWalk) const {
		join();
		const geometry::Vec2i64 goal	 = nav::toMm(goalMeters);
		const float				straight = std::sqrt(dist2(nav::toMeters(reach.startMm), goalMeters));
		if (!reach.onGround) {
			return straight; // can't prove anything from off the ground
		}
		const geometry::RectMm square = squareAround(reach.startMm);
		if (goal.x < square.min.x || goal.x > square.max.x || goal.y < square.min.y || goal.y > square.max.y) {
			return std::nullopt;
		}
		if (structural.locate(goal) < 0) {
			// Off meshed ground: maybe, while its ground is on the way.
			const bool coming = chunkManager == nullptr || std::as_const(*chunkManager).getChunk(world::worldToChunk({goalMeters.x, goalMeters.y})) != nullptr;
			return coming && straight <= maxWalk ? std::optional<float>(straight) : std::nullopt;
		}
		const gnav::PathResult walk = gnav::pathThrough(
			structural.view(), reach.startMm, goal, reach.radiusMm, reach.belief,
			{.maxCost = static_cast<double>(maxWalk) * static_cast<double>(geometry::kMillimetersPerMeter), .within = &square});
		if (walk.stop != gnav::PathStop::Found) {
			return std::nullopt;
		}
		return static_cast<float>(walk.cost / static_cast<double>(geometry::kMillimetersPerMeter));
	}

	bool NavigationSystem::reaches(const Reach& reach, glm::vec2 goalMeters) const {
		return walkDistance(reach, goalMeters).has_value();
	}

	bool NavigationSystem::isOnMesh(glm::vec2 meters) const {
		join();
		return pointOnFace(detailMesh, meters, moverFace);
	}

	bool NavigationSystem::isValidPosition(glm::vec2 meters) const {
		join();
		return pointOnFace(detailMesh, meters, moverFace) && !treeColliderAt(meters).has_value();
	}

	std::optional<NavigationSystem::TreeCollider> NavigationSystem::treeColliderAt(glm::vec2 meters, float reachMeters) const {
		if (placement == nullptr) {
			return std::nullopt;
		}
		// Colliders are found by their entity's centre, so the box reaches past the largest one.
		constexpr float				 kColliderReach = 4.0F;
		const float					 reach			= kColliderReach + reachMeters;
		const world::ChunkCoordinate lo				= world::worldToChunk({meters.x - reach, meters.y - reach});
		const world::ChunkCoordinate hi				= world::worldToChunk({meters.x + reach, meters.y + reach});
		const assets::AssetRegistry& registry		= assets::AssetRegistry::Get();
		const geometry::Vec2i64		 p				= nav::toMm(meters);
		std::optional<TreeCollider>	 nearest;
		float						 nearestD2 = 0.0F;
		for (std::int32_t cy = lo.y; cy <= hi.y; ++cy) {
			for (std::int32_t cx = lo.x; cx <= hi.x; ++cx) {
				const assets::SpatialIndex* index = placement->getChunkIndex({cx, cy});
				if (index == nullptr) {
					continue;
				}
				for (const assets::PlacedEntity* e : index->queryRect(meters.x - reach, meters.y - reach, meters.x + reach, meters.y + reach)) {
					if (!nav::floraOutline(*e, registry, colliderScratch)) {
						continue;
					}
					const bool holds = geometry::pointInPolygon(p, colliderScratch) != geometry::PointInPolygon::Outside;
					if (reachMeters <= 0.0F && !holds) {
						continue;
					}
					const float d2 = dist2(meters, e->position);
					if (nearest.has_value() && d2 >= nearestD2) {
						continue;
					}
					glm::vec2 centre{0.0F, 0.0F};
					for (const geometry::Vec2i64& v : colliderScratch) {
						centre += nav::toMeters(v);
					}
					centre /= static_cast<float>(colliderScratch.size());
					float extent2 = 0.0F;
					for (const geometry::Vec2i64& v : colliderScratch) {
						extent2 = std::max(extent2, dist2(centre, nav::toMeters(v)));
					}
					nearest	  = TreeCollider{centre, std::sqrt(extent2)};
					nearestD2 = d2;
				}
			}
		}
		return nearest;
	}

	bool NavigationSystem::passesTree(const std::vector<geometry::Vec2i64>& points, std::int64_t radius) const {
		if (placement == nullptr) {
			return false;
		}
		// Each segment against the outline of every tree in the nav mesh near it: within the radius
		// when it crosses an edge or starts inside, or an edge comes that close. Exact, so no trunk
		// however narrow is missed between samples.
		using P						  = glm::dvec2;
		const auto pointToSegment2	  = [](P p, P a, P b) {
			   const P		d	 = b - a;
			   const double len2 = glm::dot(d, d);
			   const double t	 = len2 > 0.0 ? std::clamp(glm::dot(p - a, d) / len2, 0.0, 1.0) : 0.0;
			   const P		q	 = a + d * t - p;
			   return glm::dot(q, q);
		};
		const auto cross = [](P o, P a, P b) { return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x); };
		const auto segmentsWithin = [&](P a, P b, P c, P d, double r2) {
			const double d1 = cross(c, d, a);
			const double d2 = cross(c, d, b);
			const double d3 = cross(a, b, c);
			const double d4 = cross(a, b, d);
			if (((d1 > 0.0 && d2 < 0.0) || (d1 < 0.0 && d2 > 0.0)) && ((d3 > 0.0 && d4 < 0.0) || (d3 < 0.0 && d4 > 0.0))) {
				return true;
			}
			return std::min({pointToSegment2(a, c, d), pointToSegment2(b, c, d), pointToSegment2(c, a, b), pointToSegment2(d, a, b)}) <= r2;
		};
		const assets::AssetRegistry& registry = assets::AssetRegistry::Get();
		const double				 r2		  = static_cast<double>(radius) * static_cast<double>(radius);
		// Placed entities are found by their centre, so the box reaches past the largest collider.
		constexpr float kColliderReach = 4.0F;
		for (std::size_t k = 1; k < points.size(); ++k) {
			const P			a{static_cast<double>(points[k - 1].x), static_cast<double>(points[k - 1].y)};
			const P			b{static_cast<double>(points[k].x), static_cast<double>(points[k].y)};
			const glm::vec2 lo = glm::min(nav::toMeters(points[k - 1]), nav::toMeters(points[k])) -
								 glm::vec2{kColliderReach + static_cast<float>(radius) / 1000.0F};
			const glm::vec2 hi = glm::max(nav::toMeters(points[k - 1]), nav::toMeters(points[k])) +
								 glm::vec2{kColliderReach + static_cast<float>(radius) / 1000.0F};
			const world::ChunkCoordinate cLo = world::worldToChunk({lo.x, lo.y});
			const world::ChunkCoordinate cHi = world::worldToChunk({hi.x, hi.y});
			for (std::int32_t cy = cLo.y; cy <= cHi.y; ++cy) {
				for (std::int32_t cx = cLo.x; cx <= cHi.x; ++cx) {
					const assets::SpatialIndex* index = placement->getChunkIndex({cx, cy});
					if (index == nullptr) {
						continue;
					}
					for (const assets::PlacedEntity* e : index->queryRect(lo.x, lo.y, hi.x, hi.y)) {
						if (!floraInNavMesh({cx, cy}, *e) || !nav::floraOutline(*e, registry, colliderScratch)) {
							continue;
						}
						if (geometry::pointInPolygon(points[k - 1], colliderScratch) != geometry::PointInPolygon::Outside) {
							return true;
						}
						for (std::size_t v = 0; v < colliderScratch.size(); ++v) {
							const geometry::Vec2i64& c = colliderScratch[v];
							const geometry::Vec2i64& d = colliderScratch[(v + 1) % colliderScratch.size()];
							if (segmentsWithin(a, b, {static_cast<double>(c.x), static_cast<double>(c.y)},
											   {static_cast<double>(d.x), static_cast<double>(d.y)}, r2)) {
								return true;
							}
						}
					}
				}
			}
		}
		return false;
	}

	std::optional<glm::vec2> NavigationSystem::standPointBeside(glm::vec2 target, glm::vec2 from, float agentRadiusMeters) const {
		join();
		if (structural.locate(nav::toMm(target)) < 0) {
			return std::nullopt;
		}
		const auto walkable = [&](glm::vec2 p) { return truthWalkable(structural, p); };
		const auto clear	= [&](glm::vec2 p) { return walkable(p) && !treeColliderAt(p).has_value(); };
		const std::optional<TreeCollider> collider = treeColliderAt(target, 0.05F);
		if (!collider.has_value()) {
			if (clear(target)) {
				return target;
			}
			return nearestFaceOnMesh(structural, target, [](const gnav::NavTriangle& t) { return gnav::truthTraversable(t); });
		}
		// Round the collider at the disc's radius out from it, nearest the colonist first: a spot clear
		// of every collider, else (a tree in a thicket) one that is at least ground a mover can cross,
		// inside a neighbour's trunk, which a route reaches through the flora at its price.
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
			if (clear(spot)) {
				return spot;
			}
		}
		for (const auto& [d2, spot] : spots) {
			if (walkable(spot)) {
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
		return areaWalkableUnder([this](glm::vec2 p) { return terrainWalkable(structural, p); }, polygonMeters);
	}

	bool NavigationSystem::isPointBuildable(glm::vec2 meters) const {
		join();
		return terrainWalkable(structural, meters);
	}

	std::optional<glm::vec2> NavigationSystem::nearestTerrainWalkablePoint(glm::vec2 meters) const {
		join();
		if (truthWalkable(structural, meters)) {
			return meters;
		}
		return nearestFaceOnMesh(structural, meters, [](const gnav::NavTriangle& t) { return gnav::truthTraversable(t); });
	}

	std::optional<glm::vec2> NavigationSystem::nearestPathablePoint(glm::vec2 meters) const {
		join();
		const std::optional<glm::vec2> nearest =
			terrainWalkable(detailMesh, meters)
				? std::optional<glm::vec2>(meters)
				: nearestFaceOnMesh(detailMesh, meters, [](const gnav::NavTriangle& t) { return gnav::terrainTraversable(t); });
		// A tree the mesh doesn't hold is still in the way: step out of its collider.
		if (!nearest.has_value() || !treeColliderAt(*nearest).has_value()) {
			return nearest;
		}
		return standPointBeside(*nearest, meters, 0.0F);
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
