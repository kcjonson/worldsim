#include "NavigationSystem.h"

#include "../World.h"
#include "../components/AgentRadius.h"
#include "../components/Colonist.h"
#include "../components/Memory.h"
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

		// Walkable for the most optimistic belief: only common-knowledge terrain (water, placed obstacles) blocks.
		bool terrainWalkable(const gnav::PatchableMesh& mesh, glm::vec2 meters) {
			return pointOnFace(mesh, meters, [](const gnav::NavTriangle& t) { return gnav::terrainTraversable(t); });
		}

		// The detail obstacle kinds, by the tag their rings carry: what walking a metre through one
		// costs. Each is in the nav mesh only (the structural mesh never holds detail), only where a
		// wanted rect brings it, and priced, never walled. One kind so far, every placed entity's
		// collider whatever the entity is.
		constexpr gnav::TagPrice kDetailKinds[] = {{.tag = nav::kProvenancePlaced, .multiplier = NavigationSystem::kDetailPassMultiplier}};

		bool isDetailTag(std::int64_t tag) {
			return std::any_of(std::begin(kDetailKinds), std::end(kDetailKinds), [tag](const gnav::TagPrice& kind) { return kind.tag == tag; });
		}

		std::vector<std::int64_t> detailTags() {
			std::vector<std::int64_t> tags;
			for (const gnav::TagPrice& kind : kDetailKinds) {
				tags.push_back(kind.tag);
			}
			return tags;
		}

		// Ground a mover may stand on: terrain-walkable, or detail, which movement prices.
		bool moverFace(const gnav::NavTriangle& t) {
			return gnav::terrainTraversable(t) || isDetailTag(t.faceBlocker);
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

	bool NavigationSystem::DetailKey::operator<(const DetailKey& o) const {
		if (chunk.y != o.chunk.y || chunk.x != o.chunk.x) {
			return ChunkLess{}(chunk, o.chunk);
		}
		return std::tie(cell, xBits, yBits, defName) < std::tie(o.cell, o.xBits, o.yBits, o.defName);
	}

	NavigationSystem::NavigationSystem()
		: detailMesh({.maxBoundsEdgeMm = kMaxBoundsEdgeMm, .detailTags = detailTags()}),
		  structural({.maxBoundsEdgeMm = kMaxBoundsEdgeMm}),
		  navWorker(std::make_unique<Worker>()),
		  structuralWorker(std::make_unique<Worker>()) {}

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
			wantDisc(position.value, range);
		}
		std::erase_if(moverDiscs, [this](const auto& entry) { return entry.second.seen != moverPass; });
	}

	void NavigationSystem::wantDisc(glm::vec2 centerMeters, float rangeMeters) {
		const geometry::Vec2i64 c = nav::toMm(centerMeters);
		const std::int64_t		r = radiusMm(rangeMeters);
		for (std::int64_t cy = geometry::floorDiv(c.y - r, kDetailCellMm); cy <= geometry::floorDiv(c.y + r, kDetailCellMm); ++cy) {
			for (std::int64_t cx = geometry::floorDiv(c.x - r, kDetailCellMm); cx <= geometry::floorDiv(c.x + r, kDetailCellMm); ++cx) {
				const std::int64_t dx = c.x - std::clamp(c.x, cx * kDetailCellMm, (cx + 1) * kDetailCellMm);
				const std::int64_t dy = c.y - std::clamp(c.y, cy * kDetailCellMm, (cy + 1) * kDetailCellMm);
				if (dx * dx + dy * dy > r * r) {
					continue;
				}
				const std::int64_t cell = cellKey(cx, cy);
				if (!wantedCells.insert(cell).second) {
					continue;
				}
				const world::ChunkCoordinate chunk = cellChunk(cell);
				if (wantedChunks.insert(chunk).second && !ground.contains(chunk)) {
					groundQueue.push_back(chunk);
				}
				// Behind any of its chunk's cells still waiting, so a chunk's cells keep their order.
				if (!cellsAwaitingChunk.contains(chunk) && chunkGroundMeshed(chunk) && placementReady(chunk)) {
					readyCells.push_back(cell);
				} else {
					cellsAwaitingChunk[chunk].push_back(cell);
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
		if (change == StructuralChange::Closes) {
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
			// Every chunk the box meets is within the colonist's anchor load range, so its ground is
			// on the way; one the chunk manager hasn't loaded either is noted once, in case it never is.
			const bool loaded = chunkManager != nullptr && std::as_const(*chunkManager).getChunk(coord) != nullptr;
			if (!loaded && unmeshedReported.insert(coord).second) {
				LOG_DEBUG(Engine, "[Nav] reach box meets chunk (%d, %d), not loaded yet: its colonists read unsettled until it is", coord.x, coord.y);
			}
		});
		return meshed;
	}

	// --- Ticks, jobs and the join -------------------------------------------------

	void NavigationSystem::update(float /*deltaTime*/) {
		settle();
		landedThisUpdate = std::move(groundLanded);
		groundLanded.clear();
		std::sort(landedThisUpdate.begin(), landedThisUpdate.end(), ChunkLess{});
		forgetDroppedRoutes();
		markCrossedRoutes();
		markStrayedRoutes();
		snapMovers();
		if (chunkManager != nullptr) {
			gather(kDetailRingsPerTick, kDetailRemovalsPerTick, kDetailCellsPerTick, kDetailScansPerTick, kGroundVerticesPerTick);
		}
	}

	void NavigationSystem::endTick() {
		submit();
	}

	void NavigationSystem::finishPendingBuilds() {
		do {
			settle();
			if (chunkManager != nullptr) {
				gather(kUnlimited, kUnlimited, kUnlimited, kUnlimited, kUnlimited);
			}
			settle();
		} while (chunkManager != nullptr && isBuilding());
	}

	void NavigationSystem::gather(std::size_t addBudget, std::size_t removeBudget, std::size_t cellBudget, std::size_t scanBudget,
								  std::size_t groundVertexBudget) {
		collectAnchors();
		trackMovers();
		queueRemovedDetail();
		gatherWalls();
		gatherGround(groundVertexBudget);
		releaseAwaitingCells();
		gatherDetail(addBudget, removeBudget, cellBudget, scanBudget);
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
			++structuralEpoch;
			for (const world::ChunkCoordinate coord : runningStructural.structuralChunks) {
				++chunkStructuralVersion[coord];
			}
			groundLanded.insert(groundLanded.end(), runningStructural.groundChunks.begin(), runningStructural.groundChunks.end());
			// Ground no anchor holds changes no colonist's route or wait. Routes are restamped
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
		detailLanded.insert(detailLanded.end(), runningNav.detailAddBoxes.begin(), runningNav.detailAddBoxes.end());
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
		readyLastCall = std::move(ready);
	}

	void NavigationSystem::gatherGround(std::size_t vertexBudget) {
		rederiveChangedWater();
		// Open chunks share a tick until their vertices would pass the budget; a chunk with water
		// is the costliest kind of job there is, so it goes alone. Chunks not ready yet keep their
		// place in the queue.
		const bool	unlimited = vertexBudget == kUnlimited;
		std::size_t taken	  = 0;
		bool		full	  = false;
		std::size_t kept	  = 0;
		groundPending		  = 0;
		for (const world::ChunkCoordinate coord : groundQueue) {
			if (ground.contains(coord)) {
				continue; // the landing meshed it
			}
			const world::Chunk* chunk = chunkManager->getChunk(coord);
			if (chunk == nullptr || !chunk->isReady() || full) {
				groundPending += chunk != nullptr && chunk->isReady() ? 1 : 0;
				groundQueue[kept++] = coord;
				continue;
			}
			std::vector<gnav::NavInputPolygon> water;
			nav::appendChunkWater(chunk->terrainPolygons(), water);
			const std::size_t cost = kSquareVertices + waterVertices(water);
			if (taken > 0 && !unlimited && (!water.empty() || cost > vertexBudget)) {
				full = true;
				++groundPending;
				groundQueue[kept++] = coord;
				continue;
			}
			vertexBudget -= std::min(cost, vertexBudget);
			++taken;
			full = !water.empty() && !unlimited;
			meshGround(coord, std::move(water), chunk->terrainPolygons().version);
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
		g.square			   = {kGatheredRing, kGatheredRing};
		g.water.assign(water.size(), {kGatheredRing, kGatheredRing});
		g.terrainVersion = terrainVersion;
		{
			const auto [lo, hi] = chunkBoxM(coord);
			noteStructural(lo, hi, StructuralChange::GroundArrives);
		}
		gatheredStructural.groundChunks.push_back(coord);
		gnav::PatchEdits navEdits;
		navEdits.adds.push_back(nav::chunkSquare(coord));
		navEdits.adds.insert(navEdits.adds.end(), std::make_move_iterator(water.begin()), std::make_move_iterator(water.end()));
		gnav::PatchEdits  structuralEdits = navEdits;
		const std::size_t rings			  = navEdits.adds.size();

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
		gatheredStructural.onPatched.emplace_back([&g, navMs, coord, rings, cost](const std::vector<RingId>& ids, double ms) {
			g.square.structural = ids[0];
			for (std::size_t k = 1; k < ids.size(); ++k) {
				g.water[k - 1].structural = ids[k];
			}
			// Permanent nav-mesh work diagnostic: one chunk's ground (square and water) entering
			// both meshes. DEBUG, dev-tools log server.
			LOG_DEBUG(Engine, "[NavBuild] ground (%d, %d) nav %.3f ms structural %.3f ms rings %zu vertices %zu", coord.x, coord.y, *navMs,
					  ms, rings, cost);
		});
	}

	bool NavigationSystem::chunkGroundMeshed(world::ChunkCoordinate coord) const {
		const auto it = ground.find(coord);
		return it != ground.end() && gathered(it->second.square.nav);
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
			} else if (addingCell.has_value()) {
				auto& rings = addingCell->rings;
				const auto pending = std::find_if(rings.begin() + static_cast<std::ptrdiff_t>(addingCell->next), rings.end(),
												  [&key](const auto& entry) { return !(entry.first < key) && !(key < entry.first); });
				if (pending != rings.end()) {
					rings.erase(pending);
				}
			}
		}
	}

	void NavigationSystem::releaseAwaitingCells() {
		for (auto it = cellsAwaitingChunk.begin(); it != cellsAwaitingChunk.end();) {
			if (!chunkGroundMeshed(it->first) || !placementReady(it->first)) {
				++it;
				continue;
			}
			readyCells.insert(readyCells.end(), it->second.begin(), it->second.end());
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

	void NavigationSystem::gatherDetail(std::size_t addBudget, std::size_t removeBudget, std::size_t cellBudget, std::size_t scanBudget) {
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
			gatheredNav.patches.push_back(std::move(edits));
			gatheredNav.onPatched.emplace_back([](const std::vector<RingId>&, double) {});
			first = last;
		}
		detailRemovals.erase(detailRemovals.begin(), detailRemovals.begin() + static_cast<std::ptrdiff_t>(removeCount));

		// Adds: the wanted cells first in, first out, one patch per cell a tick; a cell larger than
		// what is left of the ring budget goes in over several ticks.
		std::size_t rings = 0;
		std::size_t cells = 0;
		std::size_t scans = 0;
		while (rings < addBudget && cells < cellBudget) {
			if (!addingCell.has_value()) {
				if (readyCells.empty() || scans == scanBudget) {
					break;
				}
				const std::int64_t cell = readyCells.front();
				readyCells.pop_front();
				++scans;
				// Its chunk unloaded since the cell was ready: it waits for the chunk's placement again.
				if (!placementReady(cellChunk(cell))) {
					cellsAwaitingChunk[cellChunk(cell)].push_back(cell);
					continue;
				}
				addingCell = scanCell(cell);
			}
			ScannedCell&	  adding = *addingCell;
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
					gatheredNav.detailAddBoxes.emplace_back(nav::toMeters(lo), nav::toMeters(hi));
					edits.adds.push_back(std::move(adding.rings[k].second));
					keys.push_back(adding.rings[k].first);
				}
				gatheredNav.patches.push_back(std::move(edits));
				gatheredNav.onPatched.emplace_back([this, keys = std::move(keys), cell = adding.cell](const std::vector<RingId>& added, double) {
					for (std::size_t k = 0; k < keys.size(); ++k) {
						detailInMesh.emplace(keys[k], DetailRing{added[k], cell});
					}
				});
				adding.next += take;
				rings += take;
				++cells;
			}
			if (adding.next == adding.rings.size()) {
				addingCell.reset();
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

	bool NavigationSystem::detailOnAt(glm::vec2 /*meters*/) const {
		return true;
	}

	gnav::BeliefFilter NavigationSystem::pricedFor(gnav::BeliefFilter belief) {
		belief.prices = kDetailKinds;
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

	bool NavigationSystem::holds(const RouteSet& routes, const std::vector<glm::vec2>& waypoints) {
		const auto it = routes.find(routeKey(waypoints));
		return it != routes.end() && it->second == waypoints;
	}

	void NavigationSystem::note(RouteSet& routes, const std::vector<glm::vec2>& waypoints, bool in) {
		if (in) {
			routes[routeKey(waypoints)] = waypoints;
		} else if (holds(routes, waypoints)) {
			routes.erase(routeKey(waypoints));
		}
	}

	bool NavigationSystem::routeCrossesPricedFaces(const NavPath& path) const {
		return path.valid && holds(pricedRoutes, path.waypoints);
	}

	void NavigationSystem::forgetDroppedRoutes() {
		for (RouteSet* routes : {&pricedRoutes, &structuralRoutes}) {
			if (routes->empty()) {
				continue;
			}
			RouteSet live;
			if (world != nullptr) {
				for (auto [entity, path] : world->view<NavPath>()) {
					(void)entity;
					if (path.valid && holds(*routes, path.waypoints)) {
						live.emplace(routeKey(path.waypoints), path.waypoints);
					}
				}
			}
			*routes = std::move(live);
		}
	}

	bool NavigationSystem::reachSettledThisUpdate(glm::vec2 meters) const {
		if (landedThisUpdate.empty()) {
			return false;
		}
		join();
		const geometry::RectMm box	   = boxAround(nav::toMm(meters), anchorLoadRangeMm());
		bool				   touched = false;
		forChunksMeeting(box, [&](world::ChunkCoordinate coord) {
			touched = touched || std::binary_search(landedThisUpdate.begin(), landedThisUpdate.end(), coord, ChunkLess{});
		});
		return touched && groundMeshedThrough(box);
	}

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
		// obstacles that were already there are no news to a route planned past or through them.
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
			if (!crossed) {
				continue;
			}
			if (!holds(structuralRoutes, path.waypoints)) {
				path.builtNavVersion = kStaleRoute;
				continue;
			}
			std::vector<geometry::Vec2i64> rest{nav::toMm(position.value)};
			for (std::size_t next = path.current; next < path.waypoints.size(); ++next) {
				rest.push_back(nav::toMm(path.waypoints[next]));
			}
			note(pricedRoutes, path.waypoints, passesPlacedObstacle(rest, radiusMm(radius)));
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

	bool NavigationSystem::placedObstacleInNavMesh(world::ChunkCoordinate chunk, const assets::PlacedEntity& entity) const {
		return detailInMesh.contains(
			DetailKey{chunk, cellOfEntity(entity.position), std::bit_cast<std::uint32_t>(entity.position.x), std::bit_cast<std::uint32_t>(entity.position.y),
					 entity.defName});
	}

	void NavigationSystem::snapMovers() {
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

		// Only where detail is on is a mover pushed out of a placed obstacle, and so snapped out of one.
		// A mover whose route walks through detail on purpose, at its price, is left to it.
		const std::uint64_t current = generation();
		for (auto [entity, position, colonist] : world->view<Position, Colonist>()) {
			(void)colonist;
			if (!detailOnAt(position.value)) {
				continue;
			}
			const std::int32_t tri = detailMesh.locate(nav::toMm(position.value));
			if (tri < 0 || !isDetailTag(detailMesh.triangles()[static_cast<std::size_t>(tri)].faceBlocker)) {
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
		return groundPending > 0 || !readyCells.empty() || addingCell.has_value() || !detailRemovals.empty() || !gatheredNav.empty() ||
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

	NavigationSystem::PathAnswer
	NavigationSystem::requestPath(glm::vec2 startMeters, glm::vec2 goalMeters, float agentRadiusMeters, gnav::BeliefFilter belief) const {
		join();
		if (detailMesh.liveTriangleCount() == 0) {
			return {};
		}
		const geometry::Vec2i64 startMm = nav::toMm(startMeters);
		const geometry::Vec2i64 goalMm	= nav::toMm(goalMeters);
		const std::int64_t		radius	= radiusMm(agentRadiusMeters);

		// The structural mesh holds the nav mesh's ground without detail, so where it has no walk
		// under this belief (its known walls block) the nav mesh has none either: no nav search.
		const StructuralWalk structuralAnswer =
			structuralWalk(startMm, goalMm, radius, belief, std::numeric_limits<double>::infinity(), farRouteExpansions);
		const auto counted = [&](const gnav::PathResult& search) {
			++navStats.totalQueries;
			navStats.totalNodesExpanded += static_cast<std::uint64_t>(search.nodesExpanded);
			navStats.lastNodesExpanded = search.nodesExpanded;
			navStats.lastPeakOpenSet   = search.peakOpenSet;
			LOG_DEBUG(Engine, "[Nav] path query expanded=%lld peakOpen=%lld (cumulative queries=%llu nodes=%llu)",
					  static_cast<long long>(search.nodesExpanded), static_cast<long long>(search.peakOpenSet),
					  static_cast<unsigned long long>(navStats.totalQueries), static_cast<unsigned long long>(navStats.totalNodesExpanded));
		};
		if (structuralAnswer.gaveUp) {
			++navStats.farWalks;
			counted(structuralAnswer.walk);
			++navStats.routeSearchesGaveUp;
			LOG_DEBUG(Engine, "[Nav] path search gave up toward (%.2f, %.2f), %.0f m in a straight line from (%.2f, %.2f) after %u expansions",
					  goalMeters.x, goalMeters.y, std::sqrt(dist2(startMeters, goalMeters)), startMeters.x, startMeters.y, structuralAnswer.expansions);
			return {.route = std::nullopt, .gaveUp = true};
		}
		if (structuralAnswer.walk.stop != gnav::PathStop::Found) {
			return {};
		}

		gnav::PathResult result;
		bool			 fromStructural = true;
		if (!structuralAnswer.probeAnswered && structuralAnswer.walkExpansions > reachExpansionCap) {
			// A far goal: the structural walk is the route, through the detail at its price, as the
			// fallback below takes it. No nav search: scaled from a walk this long its budget would be
			// unbounded, and with a fixed one it gave up on every far goal measured
			// (nav-gate-result-2.md). The route is replanned as he goes.
			++navStats.farWalks;
			result = structuralAnswer.walk;
			counted(result);
		} else {
			// Detail only makes the nav search's corridors finer and its walk dearer, so it needs some
			// multiple of the structural search's expansions. Past that it is lost in priced ground, and
			// the route is the structural walk itself, through the detail at its price, as it would be off
			// camera: so a route exists on camera exactly when it does off it.
			const std::uint32_t cap = navExpansionCap(structuralAnswer.walkExpansions);
			result					= gnav::pathThrough(detailMesh.view(), startMm, goalMm, radius, pricedFor(belief), {.maxExpansions = cap});
			counted(result);
			++navStats.navSearches;
			navStats.navSearchExpansions += result.expansions;
			if (result.stop == gnav::PathStop::LimitReached) {
				++navStats.navSearchesCapped;
				LOG_DEBUG(Engine, "[Nav] path query gave up after %u expansions (structural %u) from (%.2f, %.2f) to (%.2f, %.2f): walking the structural route",
						  result.expansions, structuralAnswer.walkExpansions, startMeters.x, startMeters.y, goalMeters.x, goalMeters.y);
				result = structuralAnswer.walk;
			} else if (!result.reachable) {
				return {};
			} else {
				fromStructural = false;
			}
		}
		if (fromStructural) {
			result.crossesPricedFaces = passesPlacedObstacle(result.points, radius);
		}

		std::vector<glm::vec2> waypoints;
		waypoints.reserve(result.points.size());
		for (const geometry::Vec2i64& p : result.points) {
			waypoints.push_back(nav::toMeters(p));
		}
		note(pricedRoutes, waypoints, result.crossesPricedFaces);
		note(structuralRoutes, waypoints, fromStructural);
		return {.route = std::move(waypoints)};
	}

	std::uint32_t NavigationSystem::navExpansionCap(std::uint32_t walkSteps) const {
		const std::uint64_t scaled = static_cast<std::uint64_t>(navExpansionFloor) + static_cast<std::uint64_t>(navExpansionsPerStructural) * walkSteps;
		return static_cast<std::uint32_t>(std::min<std::uint64_t>(scaled, kNavExpansionCeiling));
	}

	NavigationSystem::StructuralWalk NavigationSystem::structuralWalk(const geometry::Vec2i64& startMm, const geometry::Vec2i64& goalMm,
																	   std::int64_t radius, const gnav::BeliefFilter& belief, double maxCost,
																	   std::uint32_t forwardCap) const {
		// LimitReached is either limit: one under its expansion cap stopped at maxCost, which is a
		// definite "no walk that short".
		const auto outOfSteps = [](const gnav::PathResult& r, std::uint32_t cap) {
			return r.stop == gnav::PathStop::LimitReached && r.expansions >= cap;
		};
		++navStats.structuralWalks;
		StructuralWalk answer;
		answer.walk = gnav::pathThrough(structural.view(), goalMm, startMm, radius, belief, {.maxCost = maxCost, .maxExpansions = reachProbeExpansions});
		answer.expansions	  = answer.walk.expansions;
		answer.walkExpansions = answer.walk.expansions;
		navStats.probeExpansions += answer.walk.expansions;
		if (!outOfSteps(answer.walk, reachProbeExpansions)) {
			++navStats.probeAnswered;
			answer.probeAnswered = true;
			std::reverse(answer.walk.points.begin(), answer.walk.points.end());
			return answer;
		}
		++navStats.forwardSearches;
		answer.walk = gnav::pathThrough(structural.view(), startMm, goalMm, radius, belief, {.maxCost = maxCost, .maxExpansions = forwardCap});
		answer.expansions += answer.walk.expansions;
		answer.walkExpansions = answer.walk.expansions;
		navStats.forwardExpansions += answer.walk.expansions;
		answer.gaveUp = outOfSteps(answer.walk, forwardCap);
		return answer;
	}

	float NavigationSystem::anchorLoadRangeMeters() const {
		return world::ChunkManager::kMinAnchorLoadRangeMeters;
	}

	geometry::RectMm NavigationSystem::boxAround(const geometry::Vec2i64& centerMm, std::int64_t halfMm) {
		return {{centerMm.x - halfMm, centerMm.y - halfMm}, {centerMm.x + halfMm, centerMm.y + halfMm}};
	}

	std::int64_t NavigationSystem::anchorLoadRangeMm() const {
		return std::llround(static_cast<double>(anchorLoadRangeMeters()) * static_cast<double>(geometry::kMillimetersPerMeter));
	}

	std::uint64_t NavigationSystem::chunkStamp(const geometry::RectMm& box) const {
		std::uint64_t stamp = 1469598103934665603ULL;
		forChunksMeeting(box, [&](world::ChunkCoordinate coord) {
			const auto			it		= chunkStructuralVersion.find(coord);
			const std::uint64_t version = it != chunkStructuralVersion.end() ? it->second : 0;
			stamp						= (stamp ^ version) * 1099511628211ULL;
		});
		return stamp;
	}

	NavigationSystem::Reach NavigationSystem::reachFrom(glm::vec2 startMeters, float agentRadiusMeters, gnav::BeliefFilter belief) const {
		join();
		Reach reach;
		reach.belief		  = belief;
		reach.startMm		  = nav::toMm(startMeters);
		reach.radiusMm		  = radiusMm(agentRadiusMeters);
		reach.onGround		  = structural.locate(reach.startMm) >= 0;
		reach.startCell		  = cellKey(geometry::floorDiv(reach.startMm.x, kReachCellMm), geometry::floorDiv(reach.startMm.y, kReachCellMm));
		reach.structuralEpoch = structuralEpoch;
		reach.settled		  = groundMeshedThrough(boxAround(reach.startMm, anchorLoadRangeMm()));
		return reach;
	}

	bool NavigationSystem::sharesStartRegion(const Reach& reach, glm::vec2 goalMeters) const {
		join();
		return reach.onGround && structural.locate(nav::toMm(goalMeters)) >= 0;
	}

	std::uint64_t NavigationSystem::verdictStamp(glm::vec2 anchor) const {
		join();
		return chunkStamp(boxAround(nav::toMm(anchor), 2 * anchorLoadRangeMm()));
	}

	std::uint64_t NavigationSystem::spanStamp(glm::vec2 from, glm::vec2 to) const {
		join();
		const geometry::Vec2i64 a	 = nav::toMm(from);
		const geometry::Vec2i64 b	 = nav::toMm(to);
		const std::int64_t		grow = anchorLoadRangeMm();
		return chunkStamp({{std::min(a.x, b.x) - grow, std::min(a.y, b.y) - grow}, {std::max(a.x, b.x) + grow, std::max(a.y, b.y) + grow}});
	}

	bool NavigationSystem::nearAnchor(glm::vec2 anchor, glm::vec2 here) const {
		const geometry::Vec2i64 a = nav::toMm(anchor);
		const geometry::Vec2i64 h = nav::toMm(here);
		return std::abs(h.x - a.x) <= anchorLoadRangeMm() && std::abs(h.y - a.y) <= anchorLoadRangeMm();
	}

	WalkAnswer NavigationSystem::walk(const Reach& reach, glm::vec2 goalMeters, float maxWalk) const {
		join();
		const geometry::Vec2i64 goal	 = nav::toMm(goalMeters);
		const float				straight = std::sqrt(dist2(nav::toMeters(reach.startMm), goalMeters));
		if (!reach.onGround) {
			return std::optional<float>(straight); // can't prove anything from off the ground
		}
		if (structural.locate(goal) < 0) {
			// Off meshed ground: maybe, while its ground is on the way.
			const bool coming = chunkManager == nullptr || std::as_const(*chunkManager).getChunk(world::worldToChunk({goalMeters.x, goalMeters.y})) != nullptr;
			return coming && straight <= maxWalk ? std::optional<float>(straight) : std::nullopt;
		}
		const StructuralWalk found = structuralWalk(reach.startMm, goal, reach.radiusMm, reach.belief,
													static_cast<double>(maxWalk) * static_cast<double>(geometry::kMillimetersPerMeter), reachExpansionCap);
		if (found.gaveUp) {
			++navStats.farWalks;
			LOG_DEBUG(Engine, "[Nav] reach gave up after %u expansions toward (%.2f, %.2f), %.0f m in a straight line: far", found.expansions,
					  goalMeters.x, goalMeters.y, straight);
			return WalkAnswer::farAway();
		}
		if (found.walk.stop != gnav::PathStop::Found) {
			return std::optional<float>{};
		}
		return std::optional<float>(static_cast<float>(found.walk.cost / static_cast<double>(geometry::kMillimetersPerMeter)));
	}

	bool NavigationSystem::reaches(const Reach& reach, glm::vec2 goalMeters) const {
		return walk(reach, goalMeters).reachable();
	}

	bool NavigationSystem::isOnMesh(glm::vec2 meters) const {
		join();
		return pointOnFace(detailMesh, meters, moverFace);
	}

	bool NavigationSystem::isValidPosition(glm::vec2 meters) const {
		join();
		return pointOnFace(detailMesh, meters, moverFace) && !placedObstacleAt(meters).has_value();
	}

	std::optional<NavigationSystem::PlacedObstacle> NavigationSystem::placedObstacleAt(glm::vec2 meters, float reachMeters) const {
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
		std::optional<PlacedObstacle>	 nearest;
		float						 nearestD2 = 0.0F;
		for (std::int32_t cy = lo.y; cy <= hi.y; ++cy) {
			for (std::int32_t cx = lo.x; cx <= hi.x; ++cx) {
				const assets::SpatialIndex* index = placement->getChunkIndex({cx, cy});
				if (index == nullptr) {
					continue;
				}
				for (const assets::PlacedEntity* e : index->queryRect(meters.x - reach, meters.y - reach, meters.x + reach, meters.y + reach)) {
					if (!nav::placedObstacleOutline(*e, registry, colliderScratch)) {
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
					nearest	  = PlacedObstacle{centre, std::sqrt(extent2)};
					nearestD2 = d2;
				}
			}
		}
		return nearest;
	}

	bool NavigationSystem::passesPlacedObstacle(const std::vector<geometry::Vec2i64>& points, std::int64_t radius) const {
		if (placement == nullptr) {
			return false;
		}
		// Each segment against the outline of every placed obstacle in the nav mesh near it: within the radius
		// when it crosses an edge or starts inside, or an edge comes that close. Exact, so no obstacle
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
						if (!placedObstacleInNavMesh({cx, cy}, *e) || !nav::placedObstacleOutline(*e, registry, colliderScratch)) {
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
		const auto clear	= [&](glm::vec2 p) { return walkable(p) && !placedObstacleAt(p).has_value(); };
		const std::optional<PlacedObstacle> collider = placedObstacleAt(target, 0.05F);
		if (!collider.has_value()) {
			if (clear(target)) {
				return target;
			}
			return nearestFaceOnMesh(structural, target, [](const gnav::NavTriangle& t) { return gnav::truthTraversable(t); });
		}
		// Round the collider at the disc's radius out from it, nearest the colonist first: a spot clear
		// of every collider, else (an obstacle among others) one that is at least ground a mover can cross,
		// inside a neighbour's collider, which a route reaches through the detail at its price.
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
		// A placed obstacle the mesh doesn't hold is still in the way: step out of its collider.
		if (!nearest.has_value() || !placedObstacleAt(*nearest).has_value()) {
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
