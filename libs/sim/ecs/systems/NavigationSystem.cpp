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
#include <nav/LimitPolygon.h>
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
#include <cassert>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <deque>
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

		constexpr std::size_t  kUnlimited = std::numeric_limits<std::size_t>::max();
		constexpr std::int64_t kChunkMm	  = static_cast<std::int64_t>(world::kChunkSize) * geometry::kMillimetersPerMeter;

		double msSince(Clock::time_point start) {
			return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
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

		// The first nudge from a face's edge toward its centroid that locates in the face, else the
		// centroid when it does: a long face is not walked a fixed share of its size away from where it
		// was asked. nullopt when neither locates there (a face under the nudge's size across).
		constexpr float			 kIntoFaceMeters = 0.01F;
		std::optional<glm::vec2> intoFace(const gnav::PatchableMesh& mesh, std::int32_t t, glm::vec2 onEdge, glm::vec2 centroid) {
			const glm::vec2 inward = centroid - onEdge;
			const float		length = std::sqrt(inward.x * inward.x + inward.y * inward.y);
			for (float step = kIntoFaceMeters; step < length; step *= 2.0F) {
				const glm::vec2 p = onEdge + inward * (step / length);
				if (mesh.locate(nav::toMm(p)) == t) {
					return p;
				}
			}
			return mesh.locate(nav::toMm(centroid)) == t ? std::optional<glm::vec2>(centroid) : std::nullopt;
		}

		// Nearest point on `mesh` to `meters` whose face passes `faceOk` (given the triangle id), moved
		// just inside that face so it locates there, or nullopt when no face does. A face no point
		// locates in is passed over for the next.
		template <typename FaceOk>
		std::optional<glm::vec2> nearestFaceOnMesh(const gnav::PatchableMesh& mesh, glm::vec2 meters, const FaceOk& faceOk) {
			std::optional<glm::vec2> best;
			float					 bestD2 = 0.0F;
			constexpr float			 kNone	= std::numeric_limits<float>::infinity();
			nearestFirst(mesh, meters, [&](std::int32_t t, const TriangleMeters& tri) {
				if (!faceOk(t)) {
					return best.has_value() ? bestD2 : kNone;
				}
				const ClosestPoint cp = closestPointOnTriangle(meters, tri.a, tri.b, tri.c);
				if (!best.has_value() || cp.dist2 < bestD2) {
					if (const std::optional<glm::vec2> in = intoFace(mesh, t, cp.point, (tri.a + tri.b + tri.c) / 3.0F)) {
						best   = in;
						bestD2 = cp.dist2;
					}
				}
				return best.has_value() ? bestD2 : kNone;
			});
			return best;
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
				if (r == gnav::kGroundToken) {
					continue;
				}
				const gnav::RegisteredRing& ring = mesh.ring(r);
				if (!isDetailTag(ring.polygon.provenanceId)) {
					containing.push_back(&ring.sample);
				}
			}
			const gnav::FaceTags tags = gnav::classifyCover(containing);
			face.faceBlocker		  = tags.blocker;
			face.faceOpening		  = tags.opening;
			return face;
		}
		bool buildableFace(const gnav::PatchableMesh& mesh, std::int32_t tri) {
			return gnav::terrainTraversable(groundBeneath(mesh, tri)) && !nav::faceInFord(mesh, tri);
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

		constexpr float kClearMarginMeters = 0.02F; // 2 cm past clearance so a re-locate isn't borderline

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
					const float		clearance = band.halfThicknessMeters + r + kClearMarginMeters;
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

		bool crossableFace(const gnav::PatchableMesh& mesh, std::int32_t t) {
			return t >= 0 && gnav::terrainTraversable(mesh.triangles()[static_cast<std::size_t>(t)]);
		}

		// The nearest point closer than `r` to `p` on an edge between a face a route crosses (water and
		// placed obstacles block, walls read open) and one that blocks or no face at all (the mesh's
		// edge), walked to over crossable faces from p's; nullopt when there is none. `p` itself when it
		// stands on no crossable face.
		std::optional<glm::vec2> blockedWithin(const gnav::PatchableMesh& mesh, glm::vec2 p, float r) {
			const std::int32_t start = mesh.locate(nav::toMm(p));
			if (!crossableFace(mesh, start)) {
				return p;
			}
			std::vector<std::int32_t>		 open{start};
			std::unordered_set<std::int32_t> seen{start};
			std::optional<glm::vec2>		 nearest;
			float							 nearestD2 = r * r;
			while (!open.empty()) {
				const std::int32_t t = open.back();
				open.pop_back();
				const gnav::NavTriangle&	   tri = mesh.triangles()[static_cast<std::size_t>(t)];
				const TriangleMeters		   m   = trianglePoints(mesh, tri);
				const std::array<glm::vec2, 3> corner{m.a, m.b, m.c};
				for (std::size_t e = 0; e < 3; ++e) {
					const glm::vec2 q  = closestOnSegment(p, corner[e], corner[(e + 1) % 3]);
					const float		d2 = dist2(p, q);
					if (d2 >= r * r) {
						continue;
					}
					const std::int32_t across = tri.neighbor[e];
					if (!crossableFace(mesh, across)) {
						if (d2 < nearestD2) {
							nearest	  = q;
							nearestD2 = d2;
						}
					} else if (seen.insert(across).second) {
						open.push_back(across);
					}
				}
			}
			return nearest;
		}

		// `p` moved until a disc of `r` round it is clear of every blocked edge (blockedWithin) and every
		// built band, each push straight away from the nearest thing it overlaps; nullopt when a few
		// pushes don't clear it (a pocket narrower than the disc) or it leaves crossable ground.
		constexpr int			 kClearPushes = 4;
		std::optional<glm::vec2> pushedClear(const gnav::PatchableMesh& mesh, glm::vec2 p, float r, const std::vector<WallBandM>& bands) {
			for (int push = 0; push < kClearPushes; ++push) {
				p									 = ejectFromBands(p, r, bands);
				const std::optional<glm::vec2> block = blockedWithin(mesh, p, r);
				if (!block.has_value()) {
					return clearOfBands(p, r, bands) ? std::optional<glm::vec2>(p) : std::nullopt;
				}
				const glm::vec2 away = p - *block;
				const float		d	 = std::sqrt(dist2(p, *block));
				if (d <= 0.0F) {
					return std::nullopt;
				}
				p = *block + away * ((r + kClearMarginMeters) / d);
			}
			return std::nullopt;
		}

		// Nearest point to `meters` where a disc of `r` stands clear (pushedClear): per crossable face,
		// `meters` itself when it lies there, else its nearest point nudged into the face, and the
		// face's centroid, each pushed clear.
		std::optional<glm::vec2> nearestStandable(const gnav::PatchableMesh& mesh, glm::vec2 meters, float r, const std::vector<WallBandM>& bands) {
			std::optional<glm::vec2> best;
			float					 bestD2	  = 0.0F;
			const auto				 consider = [&](std::optional<glm::vec2> p) {
				  if (!p.has_value()) {
					  return;
				  }
				  const std::optional<glm::vec2> clear = pushedClear(mesh, *p, r, bands);
				  if (clear.has_value() && (!best.has_value() || dist2(meters, *clear) < bestD2)) {
					  best	 = clear;
					  bestD2 = dist2(meters, *clear);
				  }
			};
			nearestFirst(mesh, meters, [&](std::int32_t t, const TriangleMeters& tri) {
				if (crossableFace(mesh, t)) {
					const glm::vec2 centroid = (tri.a + tri.b + tri.c) / 3.0F;
					consider(mesh.locate(nav::toMm(meters)) == t
								 ? std::optional<glm::vec2>(meters)
								 : intoFace(mesh, t, closestPointOnTriangle(meters, tri.a, tri.b, tri.c).point, centroid));
					consider(intoFace(mesh, t, centroid, centroid));
				}
				return best.has_value() ? bestD2 : std::numeric_limits<float>::infinity();
			});
			return best;
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
			const auto	 steps = static_cast<std::int64_t>(std::ceil(lenMm / static_cast<double>(NavigationSystem::kFootprintSampleStepMm)));
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

		// Whether segment (a, b) and the closed polygon share a point.
		bool segmentMeetsPolygon(const geometry::Vec2i64& a, const geometry::Vec2i64& b, const std::vector<geometry::Vec2i64>& polygon) {
			// With `a` outside, `b` inside means the segment crosses an edge, so the edges settle it.
			if (geometry::pointInPolygon(a, polygon) != geometry::PointInPolygon::Outside) {
				return true;
			}
			for (std::size_t i = 0; i < polygon.size(); ++i) {
				if (geometry::intersectSegments(a, b, polygon[i], polygon[(i + 1) % polygon.size()]).relation != geometry::SegmentRelation::Disjoint) {
					return true;
				}
			}
			return false;
		}

		std::array<geometry::Vec2i64, 3> triangleCorners(const gnav::PatchableMesh& mesh, std::int32_t tri) {
			const gnav::NavTriangle& t = mesh.triangles()[static_cast<std::size_t>(tri)];
			return {mesh.vertices()[t.v[0]], mesh.vertices()[t.v[1]], mesh.vertices()[t.v[2]]};
		}

		// Whether the closed triangle (counter-clockwise, as the mesh holds them) and the closed
		// polygon share a point.
		bool triangleMeetsPolygon(const std::array<geometry::Vec2i64, 3>& corners, const std::vector<geometry::Vec2i64>& polygon) {
			for (std::size_t e = 0; e < 3; ++e) {
				if (segmentMeetsPolygon(corners[e], corners[(e + 1) % 3], polygon)) {
					return true;
				}
			}
			// No side of the triangle touches the polygon, so the polygon is wholly inside the triangle
			// or wholly outside it, and one of its points tells which.
			for (std::size_t e = 0; e < 3; ++e) {
				if (geometry::orientation(corners[e], corners[(e + 1) % 3], polygon.front()) == geometry::Orientation::Clockwise) {
					return false;
				}
			}
			return true;
		}

		// Whole-footprint validity, read exactly from the mesh: whether the closed polygon lies wholly
		// on the mesh and `face` accepts every triangle it meets, a triangle it only touches included.
		// The triangles are found by walking out from the one under a corner, so nothing narrower than
		// a sampling pitch can pass between probes. A footprint that reaches the mesh's edge is off it.
		// A ring of fewer than 3 points validates its points and the samples between them.
		template <typename Face> bool areaOnFaces(const gnav::PatchableMesh& mesh, const std::vector<glm::vec2>& polygonMeters, const Face& face) {
			if (polygonMeters.size() < 3) {
				return polylineWalkableUnder([&](glm::vec2 p) { return pointOnGround(mesh, p, face); }, polygonMeters);
			}
			std::vector<geometry::Vec2i64> footprint;
			footprint.reserve(polygonMeters.size());
			for (const glm::vec2 p : polygonMeters) {
				footprint.push_back(nav::toMm(p));
			}
			const std::int32_t first = mesh.locate(footprint.front());
			if (first < 0) {
				return false;
			}
			std::vector<std::int32_t>		 pending{first};
			std::unordered_set<std::int32_t> reached{first};
			while (!pending.empty()) {
				const std::int32_t tri = pending.back();
				pending.pop_back();
				if (!face(mesh, tri)) {
					return false;
				}
				const gnav::NavTriangle&			   triangle = mesh.triangles()[static_cast<std::size_t>(tri)];
				const std::array<geometry::Vec2i64, 3> corners	= triangleCorners(mesh, tri);
				for (std::size_t e = 0; e < 3; ++e) {
					const std::int32_t across = triangle.neighbor[e];
					if (across < 0) {
						if (segmentMeetsPolygon(corners[e], corners[(e + 1) % 3], footprint)) {
							return false;
						}
					} else if (reached.insert(across).second && triangleMeetsPolygon(triangleCorners(mesh, across), footprint)) {
						pending.push_back(across);
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
			job.reports.push_back(mesh.applyPatch(edits));
			job.patchMs.push_back(msSince(patchStart));
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

	bool NavigationSystem::ObstacleKey::operator<(const ObstacleKey& o) const {
		if (chunk.y != o.chunk.y || chunk.x != o.chunk.x) {
			return ChunkLess{}(chunk, o.chunk);
		}
		return std::tie(xBits, yBits, defName) < std::tie(o.xBits, o.yBits, o.defName);
	}

	NavigationSystem::NavigationSystem()
		: patchWorker(std::make_unique<Worker>()),
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
			const float circumradius = static_cast<float>(gnav::limitCircumradius(radiusMm(meshingRange(entity)))) /
									   static_cast<float>(geometry::kMillimetersPerMeter);
			out.push_back({{position.value.x, position.value.y},
						   circumradius + kFootprintReachMeters + static_cast<float>(kLimitRetakeStepMm) / static_cast<float>(geometry::kMillimetersPerMeter)});
		}
	}

	glm::vec2 NavigationSystem::stepWithinSight(EntityID mover, glm::vec2 from, glm::vec2 to) const {
		const auto disc = moverDiscs.find(mover);
		if (disc == moverDiscs.end() || !disc->second.served.has_value()) {
			return from;
		}
		const glm::vec2 centre = nav::toMeters(disc->second.served->centre);
		const float		reach  = static_cast<float>(std::max<std::int64_t>(0, disc->second.served->rangeMm - disc->second.sightMm)) /
							static_cast<float>(geometry::kMillimetersPerMeter);
		const glm::vec2 out	 = to - centre;
		const float		outD = glm::dot(out, out);
		if (outD <= reach * reach) {
			return to;
		}
		const glm::vec2 in	= from - centre;
		const float		inD = glm::dot(in, in);
		if (inD >= reach * reach) {
			return outD <= inD ? to : from;
		}
		// Where from + t (to - from) meets the circle, t in (0, 1).
		const glm::vec2 d	 = to - from;
		const float		a	 = glm::dot(d, d);
		const float		half = glm::dot(in, d);
		const float		t	 = (-half + std::sqrt(half * half - a * (inD - reach * reach))) / a;
		return from + d * t;
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
			const std::int64_t xMm	 = radiusMm(position.value.x);
			const std::int64_t yMm	 = radiusMm(position.value.y);
			const float		   range = meshingRange(entity);
			auto [it, fresh]		 = moverDiscs.try_emplace(entity);
			MoverDisc& disc			 = it->second;
			disc.seen				 = moverPass;
			disc.sightMm			 = radiusMm(range - kSightBufferMeters);
			const std::int64_t dx	 = xMm - disc.xMm;
			const std::int64_t dy	 = yMm - disc.yMm;
			if (fresh || dx * dx + dy * dy >= kLimitRetakeStepMm * kLimitRetakeStepMm || disc.rangeMeters != range) {
				disc.xMm		 = xMm;
				disc.yMm		 = yMm;
				disc.rangeMeters = range;
				SeenLimit limit	 = makeLimit(entity, {xMm, yMm}, range);
				limit.taken		 = ++disc.taken;
				if (detailMesh.groundCovers(limit.outline)) {
					noteServed(disc, limit);
				} else {
					limitQueue.push_back(std::move(limit));
				}
			}
			disc.urgent = true;
			if (disc.served.has_value()) {
				const std::int64_t margin = disc.served->rangeMm - disc.sightMm - kLimitRetakeStepMm;
				const std::int64_t sx	  = xMm - disc.served->centre.x;
				const std::int64_t sy	  = yMm - disc.served->centre.y;
				disc.urgent				  = margin <= 0 || sx * sx + sy * sy >= margin * margin;
			}
		}
		std::erase_if(moverDiscs, [this](const auto& entry) { return entry.second.seen != moverPass; });
	}

	void NavigationSystem::noteServed(MoverDisc& disc, const SeenLimit& limit) {
		if (!disc.served.has_value() || limit.taken > disc.served->taken) {
			disc.served = ServedLimit{.centre = limit.centre, .rangeMm = limit.rangeMm, .taken = limit.taken};
		}
	}

	void NavigationSystem::noteServed(const SeenLimit& limit) {
		if (const auto disc = moverDiscs.find(limit.mover); disc != moverDiscs.end()) {
			noteServed(disc->second, limit);
		}
	}

	NavigationSystem::SeenLimit NavigationSystem::makeLimit(EntityID mover, geometry::Vec2i64 centre, float rangeMeters) const {
		SeenLimit limit{.mover			= mover,
						.centre			= centre,
						.rangeMm		= radiusMm(rangeMeters),
						.circumradiusMm = gnav::limitCircumradius(radiusMm(rangeMeters))};
		limit.outline			= nav::visibilityLimit(centre, radiusMm(rangeMeters)).ring;
		const std::int64_t reach = limit.circumradiusMm + radiusMm(kFootprintReachMeters);
		limit.query				= {{centre.x - reach, centre.y - reach}, {centre.x + reach, centre.y + reach}};
		return limit;
	}

	bool NavigationSystem::anchorHolds(world::ChunkCoordinate coord) const {
		return anchorChunks.contains(coord);
	}

	namespace {
		// Calls `visit` for each chunk the box meets, row by row.
		template <typename Visit> bool everyChunkIn(geometry::Vec2i64 lo, geometry::Vec2i64 hi, const Visit& visit) {
			const std::int64_t x0 = geometry::floorDiv(lo.x, kChunkMm);
			const std::int64_t x1 = geometry::floorDiv(hi.x, kChunkMm);
			const std::int64_t y0 = geometry::floorDiv(lo.y, kChunkMm);
			const std::int64_t y1 = geometry::floorDiv(hi.y, kChunkMm);
			for (std::int64_t cy = y0; cy <= y1; ++cy) {
				for (std::int64_t cx = x0; cx <= x1; ++cx) {
					if (!visit(world::ChunkCoordinate{static_cast<std::int32_t>(cx), static_cast<std::int32_t>(cy)})) {
						return false;
					}
				}
			}
			return true;
		}
	} // namespace

	bool NavigationSystem::limitServable(const SeenLimit& limit) const {
		return everyChunkIn(limit.query.lo, limit.query.hi, [this, &limit](world::ChunkCoordinate coord) {
			const world::Chunk* chunk = chunkManager->getChunk(coord);
			return chunk != nullptr && chunk->isReady() && placementReady(coord) && (limit.mover == kInvalidEntity || anchorHolds(coord));
		});
	}

	bool NavigationSystem::limitStranded(const SeenLimit& limit) const {
		return limit.mover != kInvalidEntity &&
			   !everyChunkIn(limit.query.lo, limit.query.hi, [this](world::ChunkCoordinate coord) { return anchorHolds(coord); });
	}

	bool NavigationSystem::groundComing(geometry::Vec2i64 pointMm) const {
		const auto holds = [&pointMm](const std::vector<geometry::Vec2i64>& outline) {
			return geometry::pointInPolygon(pointMm, outline) != geometry::PointInPolygon::Outside;
		};
		const auto queuedHolds = [&](const SeenLimit& limit) {
			return pointMm.x >= limit.query.lo.x && pointMm.x <= limit.query.hi.x && pointMm.y >= limit.query.lo.y && pointMm.y <= limit.query.hi.y &&
				   holds(limit.outline);
		};
		if (landingLimit.has_value() && queuedHolds(*landingLimit)) {
			return true;
		}
		if (std::any_of(limitQueue.begin(), limitQueue.end(), [&](const SeenLimit& limit) { return !limitStranded(limit) && queuedHolds(limit); })) {
			return true;
		}
		return std::any_of(gatheredJob.groundOutlines.begin(), gatheredJob.groundOutlines.end(), holds);
	}

	bool NavigationSystem::limitUrgent(const SeenLimit& limit) const {
		const auto disc = moverDiscs.find(limit.mover);
		return disc != moverDiscs.end() && disc->second.urgent && limit.taken == disc->second.taken;
	}

	bool NavigationSystem::limitHoldable(const SeenLimit& limit) const {
		const auto disc = moverDiscs.find(limit.mover);
		return disc != moverDiscs.end() && disc->second.served.has_value() && !limitUrgent(limit);
	}

	// --- Ticks, jobs and the join -------------------------------------------------

	void NavigationSystem::update(float /*deltaTime*/) {
		settle();
		startRouteSlice();
		markStrayedRoutes();
		snapMovers();
		if (chunkManager != nullptr) {
			gather(kDetailRingsPerTick, kDetailRemovalsPerTick, kLimitsPerTick, holdingLimits);
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
		holdingLimits  = false;
		for (const auto& [ticket, route] : queuedRoutes) {
			if (!routes.waiting(route.queued)) {
				continue;
			}
			searching	  = true;
			holdingLimits = holdingLimits || (route.holdFrom != 0 && routeTick - route.holdFrom < kMaxHeldAdditionTicks);
		}
		if (holdingLimits) {
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
				gather(kUnlimited, kUnlimited, kUnlimited, false);
			}
			settle();
		} while (chunkManager != nullptr && isBuilding());
	}

	void NavigationSystem::gather(std::size_t addBudget, std::size_t removeBudget, std::size_t limitBudget, bool holdLimits) {
		assert(placement != nullptr);
		collectAnchors();
		trackMovers();
		queueRemovedDetail();
		gatherWalls();
		rederiveChangedWater();
		gatherRemovals(removeBudget);
		gatherLimits(addBudget, limitBudget, holdLimits);
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
		for (std::size_t i = 0; i < runningJob.onPatched.size(); ++i) {
			runningJob.onPatched[i](runningJob.reports[i], runningJob.patchMs[i]);
		}
		routeReports.insert(routeReports.end(), std::make_move_iterator(runningJob.reports.begin()), std::make_move_iterator(runningJob.reports.end()));
		runningJob.reports.clear();
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

		std::string pointName(glm::vec2 meters) {
			return "(" + std::to_string(meters.x) + ", " + std::to_string(meters.y) + ")";
		}

		std::string chunkName(world::ChunkCoordinate coord) {
			return "chunk (" + std::to_string(coord.x) + ", " + std::to_string(coord.y) + ")";
		}

		// A patch the mesh could not take as given (PatchReport::unreadCovers, refusedRings) is unsupported
		// input, so the mesh may be wrong there: an error, logged once as its job is collected, naming the
		// patch (`what()`) and each refused add (`describe(k)`, k its index among the patch's adds).
		template <typename What, typename Describe> void logUnsupported(const gnav::PatchReport& report, const What& what, const Describe& describe) {
			if (report.unreadCovers == 0 && report.refusedRings == 0) {
				return;
			}
			LOG_ERROR(Engine, "[Nav] patch of %s, changing %s to %s: %d covers read no sample, %d rings refused", what().c_str(),
					  pointName(nav::toMeters(report.lo)).c_str(), pointName(nav::toMeters(report.hi)).c_str(), report.unreadCovers, report.refusedRings);
			for (std::size_t k = 0; k < report.added.size(); ++k) {
				if (report.added[k] == gnav::kNoRing) {
					LOG_ERROR(Engine, "[Nav] refused: %s", describe(k).c_str());
				}
			}
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
			gatheredJob.onPatched.emplace_back([this, firstAdded](const gnav::PatchReport& report, double) {
				for (std::size_t k = 0; k < report.added.size(); ++k) {
					walls[firstAdded + k].ring = report.added[k];
				}
				logUnsupported(
					report, [] { return std::string("walls"); },
					[this, firstAdded](std::size_t k) { return "wall ring at " + pointName(nav::toMeters(walls[firstAdded + k].polygon.ring.front())); });
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


		std::pair<glm::vec2, glm::vec2> chunkBoxM(world::ChunkCoordinate coord) {
			const geometry::Vec2i64 lo = nav::chunkOriginMm(coord);
			// Just inside the far sides, so the box meets this chunk and not its neighbours.
			return {nav::toMeters(lo), nav::toMeters({lo.x + kChunkMm - 1, lo.y + kChunkMm - 1})};
		}

	} // namespace

	void NavigationSystem::rederiveChangedWater() {
		std::unordered_set<world::ChunkCoordinate> ready;
		for (const world::Chunk* chunk : std::as_const(*chunkManager).getLoadedChunks()) {
			if (!chunk->isReady()) {
				continue;
			}
			ready.insert(chunk->coordinate());
			const auto it = water.find(chunk->coordinate());
			if (it == water.end()) {
				continue;
			}
			const world::ChunkTerrainPolygons& polys = chunk->terrainPolygons();
			ChunkWater&						   g	 = it->second;
			if (!readyLastCall.contains(chunk->coordinate())) {
				g.terrainVersion = polys.version;
				continue;
			}
			if (g.terrainVersion == polys.version) {
				continue;
			}

			// Terrain changed under meshed water: its old water comes out and the new water goes in,
			// in one patch.
			gnav::PatchEdits edits;
			edits.removes = g.rings;
			nav::appendChunkWater(polys, edits.adds);
			g.rings.assign(edits.adds.size(), kGatheredRing);
			g.terrainVersion = polys.version;
			gatheredJob.blockingBoxes.push_back(chunkBoxM(chunk->coordinate()));
			gatheredJob.patches.push_back(std::move(edits));
			++gatheredJob.routePatches;
			gatheredJob.onPatched.emplace_back([&g, coord = chunk->coordinate()](const gnav::PatchReport& report, double) {
				for (std::size_t k = 0; k < report.added.size(); ++k) {
					g.rings[k] = report.added[k];
				}
				const auto what = [coord] { return "water re-derived in " + chunkName(coord); };
				logUnsupported(report, what, [&what](std::size_t) { return what(); });
			});
		}
		readyLastCall = std::move(ready);
	}


	// --- Detail ---------------------------------------------------------------------

	bool NavigationSystem::placementReady(world::ChunkCoordinate coord) const {
		return placement->getChunkIndex(coord) != nullptr && (processedChunks == nullptr || processedChunks->contains(coord));
	}

	void NavigationSystem::queueRemovedDetail() {
		if (placement == nullptr) {
			return;
		}
		for (const assets::PlacementExecutor::RemovedEntity& entry : placement->takeRemovals()) {
			const ObstacleKey key{entry.coord, std::bit_cast<std::uint32_t>(entry.position.x), std::bit_cast<std::uint32_t>(entry.position.y),
								  entry.defName};
			if (const auto it = obstacleInMesh.find(key); it != obstacleInMesh.end()) {
				// Removals are taken after the joined jobs are collected, so every gathered ring has its id.
				assert(it->second >= 0);
				detailRemovals.push_back(it->second);
				obstacleInMesh.erase(it);
			}
		}
	}

	void NavigationSystem::gatherRemovals(std::size_t removeBudget) {
		const std::size_t count = std::min(removeBudget, detailRemovals.size());
		if (count == 0) {
			return;
		}
		gnav::PatchEdits edits;
		edits.removes.assign(detailRemovals.begin(), detailRemovals.begin() + static_cast<std::ptrdiff_t>(count));
		gatheredJob.patches.push_back(std::move(edits));
		gatheredJob.onPatched.emplace_back([count](const gnav::PatchReport& report, double) {
			const auto what = [count] { return std::to_string(count) + " placed obstacles removed"; };
			logUnsupported(report, what, [&what](std::size_t) { return what(); });
		});
		detailRemovals.erase(detailRemovals.begin(), detailRemovals.begin() + static_cast<std::ptrdiff_t>(count));
	}

	void NavigationSystem::gatherLimits(std::size_t addBudget, std::size_t limitBudget, bool holdLimits) {
		if (landingLimit.has_value() && limitServable(*landingLimit) && serveLimit(*landingLimit, addBudget)) {
			landingLimit.reset();
		}
		limitsPending = landingLimit.has_value() && limitServable(*landingLimit) ? 1 : 0;
		std::size_t served = 0;
		for (auto it = limitQueue.begin(); it != limitQueue.end();) {
			if (limitStranded(*it)) {
				++limitsDropped;
				it = limitQueue.erase(it);
				continue;
			}
			if (it->coverChecked != detailMesh.epoch()) {
				it->coverChecked = detailMesh.epoch();
				if (detailMesh.groundCovers(it->outline)) {
					noteServed(*it);
					it = limitQueue.erase(it);
					continue;
				}
			}
			if (!limitServable(*it)) {
				++it;
				continue;
			}
			if (limitUrgent(*it)) {
				std::size_t whole = kUnlimited;
				serveLimit(*it, whole);
				++navStats.urgentLimits;
				it = limitQueue.erase(it);
				continue;
			}
			if (served == limitBudget || addBudget == 0 || (holdLimits && limitHoldable(*it))) {
				++limitsPending;
				++it;
				continue;
			}
			if (!serveLimit(*it, addBudget)) {
				++limitsPending;
				++it;
				continue;
			}
			++served;
			it = limitQueue.erase(it);
		}
	}

	bool NavigationSystem::serveLimit(const SeenLimit& limit, std::size_t& budget) {
		struct Found {
			ObstacleKey			  key;
			gnav::NavInputPolygon ring;
			geometry::Vec2i64	  lo;
			geometry::Vec2i64	  hi;
		};
		std::vector<Found> found;
		const assets::AssetRegistry& registry = assets::AssetRegistry::Get();
		const glm::vec2				 lo		  = nav::toMeters(limit.query.lo);
		const glm::vec2				 hi		  = nav::toMeters(limit.query.hi);
		const std::int64_t			 reachMm  = radiusMm(kFootprintReachMeters);
		everyChunkIn(limit.query.lo, limit.query.hi, [&](world::ChunkCoordinate coord) {
			for (const assets::PlacedEntity* e : placement->getChunkIndex(coord)->queryRect(lo.x, lo.y, hi.x, hi.y)) {
				ObstacleKey key{coord, std::bit_cast<std::uint32_t>(e->position.x), std::bit_cast<std::uint32_t>(e->position.y), e->defName};
				if (obstacleInMesh.contains(key)) {
					continue;
				}
				std::optional<gnav::NavInputPolygon> ring = nav::placedObstacleRing(*e, registry);
				if (!ring.has_value()) {
					continue;
				}
				// The footprint's box against the disc the limit circumscribes.
				const geometry::Vec2i64 at = nav::toMm(e->position);
				geometry::Vec2i64		boxLo = ring->ring.front();
				geometry::Vec2i64		boxHi = boxLo;
				std::int64_t			reach = 0;
				for (const geometry::Vec2i64& p : ring->ring) {
					boxLo = {std::min(boxLo.x, p.x), std::min(boxLo.y, p.y)};
					boxHi = {std::max(boxHi.x, p.x), std::max(boxHi.y, p.y)};
					reach = std::max({reach, std::abs(p.x - at.x), std::abs(p.y - at.y)});
				}
				if (reach > reachMm && overreachingDefs.insert(e->defName).second) {
					LOG_ERROR(Engine, "[Nav] %s's footprint reaches %lld mm from its position, past kFootprintReachMeters: where it pokes into a limit "
									  "from outside the limit's query box, seen ground goes in without it",
							  e->defName.c_str(), static_cast<long long>(reach));
				}
				const std::int64_t gx = std::max<std::int64_t>({0, boxLo.x - limit.centre.x, limit.centre.x - boxHi.x});
				const std::int64_t gy = std::max<std::int64_t>({0, boxLo.y - limit.centre.y, limit.centre.y - boxHi.y});
				if (gx * gx + gy * gy <= limit.circumradiusMm * limit.circumradiusMm) {
					found.push_back({std::move(key), std::move(*ring), boxLo, boxHi});
				}
			}
			return true;
		});
		// Key order, so a limit's obstacles go in the same order however the index returns them.
		std::sort(found.begin(), found.end(), [](const Found& a, const Found& b) { return a.key < b.key; });

		const std::size_t take = std::min(found.size(), budget);
		budget -= take;
		const bool		  withGround = take == found.size();
		gnav::PatchEdits  edits;
		std::vector<ObstacleKey> keys;
		// A placed obstacle over ground already meshed (one back in the world after it left the mesh)
		// can land across a route: its box closes ground, as a wall's does.
		const auto onMeshedGround = [this](const Found& f) {
			const auto meshed = [this](geometry::Vec2i64 p) { return detailMesh.locate(p) >= 0; };
			return meshed(f.lo) || meshed(f.hi) || meshed({f.lo.x, f.hi.y}) || meshed({f.hi.x, f.lo.y}) ||
				   std::any_of(f.ring.ring.begin(), f.ring.ring.end(), meshed);
		};
		bool closesGround = false;
		for (std::size_t k = 0; k < take; ++k) {
			if (onMeshedGround(found[k])) {
				gatheredJob.blockingBoxes.emplace_back(nav::toMeters(found[k].lo), nav::toMeters(found[k].hi));
				closesGround = true;
			}
			edits.adds.push_back(std::move(found[k].ring));
			obstacleInMesh.emplace(found[k].key, kGatheredRing);
			keys.push_back(std::move(found[k].key));
		}
		std::vector<world::ChunkCoordinate> waterChunks;
		std::vector<std::size_t>			waterRings; // per chunk in waterChunks, how many rings
		if (withGround) {
			everyChunkIn(limit.query.lo, limit.query.hi, [&](world::ChunkCoordinate coord) {
				if (water.contains(coord)) {
					return true;
				}
				const world::Chunk*	   chunk = chunkManager->getChunk(coord);
				const std::size_t	   first = edits.adds.size();
				nav::appendChunkWater(chunk->terrainPolygons(), edits.adds);
				ChunkWater& w	  = water[coord];
				w.terrainVersion = chunk->terrainPolygons().version;
				w.rings.assign(edits.adds.size() - first, kGatheredRing);
				waterChunks.push_back(coord);
				waterRings.push_back(edits.adds.size() - first);
				return true;
			});
			edits.adds.push_back({limit.outline, false, nav::kProvenanceBorder});
			gatheredJob.groundOutlines.push_back(limit.outline);
		}
		if (withGround || closesGround) {
			++gatheredJob.routePatches;
		}
		if (edits.adds.empty()) {
			return withGround;
		}
		const std::size_t rings = edits.adds.size();
		gatheredJob.patches.push_back(std::move(edits));
		// Its mover's served limit only once joined: until then what he sees is bounded by the last.
		std::optional<SeenLimit> ground;
		if (withGround) {
			ground = SeenLimit{.mover = limit.mover, .centre = limit.centre, .rangeMm = limit.rangeMm, .taken = limit.taken};
		}
		gatheredJob.onPatched.emplace_back([this, keys = std::move(keys), waterChunks = std::move(waterChunks), waterRings = std::move(waterRings),
											ground = std::move(ground), withGround, rings, mover = limit.mover, centre = limit.centre,
											rangeMm = limit.rangeMm](const gnav::PatchReport& report, double ms) {
			const std::vector<RingId>& added = report.added;
			if (ground.has_value()) {
				noteServed(*ground);
			}
			std::size_t k = 0;
			for (; k < keys.size(); ++k) {
				if (added[k] == gnav::kNoRing) {
					obstacleInMesh.erase(keys[k]);
				} else {
					obstacleInMesh.at(keys[k]) = added[k];
				}
			}
			for (std::size_t c = 0; c < waterChunks.size(); ++c) {
				std::vector<RingId>& ids = water.at(waterChunks[c]).rings;
				for (std::size_t r = 0; r < waterRings[c]; ++r) {
					ids[r] = added[k++];
				}
			}
			const auto what = [&] {
				return std::string(withGround ? "the visibility limit" : "placed obstacles of the visibility limit") + " of mover " +
					   std::to_string(static_cast<unsigned long long>(mover)) + " round " + pointName(nav::toMeters(centre)) + " R " + std::to_string(rangeMm) +
					   " mm";
			};
			const auto describe = [&](std::size_t i) {
				if (i < keys.size()) {
					const ObstacleKey& key = keys[i];
					return "placed obstacle " + key.defName + " at " +
						   pointName({std::bit_cast<float>(key.xBits), std::bit_cast<float>(key.yBits)}) + " in " + chunkName(key.chunk);
				}
				std::size_t at = keys.size();
				for (std::size_t c = 0; c < waterChunks.size(); ++c) {
					if (i < at + waterRings[c]) {
						return "water of " + chunkName(waterChunks[c]);
					}
					at += waterRings[c];
				}
				return what();
			};
			logUnsupported(report, what, describe);
			// Permanent nav-mesh work diagnostic: a limit's placed obstacles, and its water and ground
			// once they go. DEBUG, dev-tools log server.
			LOG_DEBUG(Engine, "[NavBuild] limit %s %.3f ms rings %zu obstacles %zu", withGround ? "ground" : "obstacles", ms, rings, keys.size());
		});
		return withGround;
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
		const auto it = obstacleInMesh.find(
			ObstacleKey{chunk, std::bit_cast<std::uint32_t>(entity.position.x), std::bit_cast<std::uint32_t>(entity.position.y), entity.defName});
		return it != obstacleInMesh.end() && it->second >= 0;
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
																	float minSeparationMeters, float clearanceMeters,
																	const std::function<void(const GroupSpawn&)>& clearLanding) {
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

		// The landing runs before the first update: bring in walls and the landing limit, and wait for
		// it. The searches below reach 2 x reachM + ringRadiusMeters + clearanceMeters from the drop,
		// well inside it.
		settle();
		collectAnchors();
		gatherWalls();
		landingLimit = makeLimit(kInvalidEntity, nav::toMm(drop), kLandingSightMeters);
		gatherLimits(kUnlimited, 0, false);
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
		if (clearLanding) {
			clearLanding(out);
			queueRemovedDetail();
			gatherRemovals(kUnlimited);
			settle();
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
		return limitsPending > 0 || !detailRemovals.empty() || !gatheredJob.empty();
	}

	std::uint64_t NavigationSystem::generation() const {
		join();
		return routeGeneration;
	}

	std::uint64_t NavigationSystem::awaitedGroundEpoch() const {
		join();
		return routeGeneration + limitsDropped;
	}

	const geometry::nav::PatchableMesh& NavigationSystem::navMesh() const {
		join();
		return detailMesh;
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
		bool					  goalComing = false;
		const gnav::NavView		  view		 = detailMesh.view();
		const std::int32_t		  startTri	 = startOnMesh ? gnav::standingTriangle(view, startMm, belief) : -1;
		if (startTri >= 0) {
			const gnav::MeshComponents& labels = detailMesh.components();
			for (std::size_t i = 0; i < goalsMeters.size(); ++i) {
				const geometry::Vec2i64 goalMm	= nav::toMm(goalsMeters[i]);
				const std::int32_t		goalTri = gnav::standingTriangle(view, goalMm, belief);
				if (goalTri >= 0 && labels.sameComponent(startTri, goalTri)) {
					args.goals.push_back(goalMm);
					goalIndices.push_back(static_cast<std::int32_t>(i));
				} else if (!goalComing && detailMesh.locate(goalMm) < 0) {
					goalComing = groundComing(goalMm);
				}
			}
		}
		if (args.goals.empty()) {
			const bool waits = startOnMesh ? goalComing : groundComing(startMm);
			++routeStatsTotal.answeredAtOnce;
			answeredAtOnce.emplace(ticket, RouteAnswer{.outcome	   = waits ? nav::RouteOutcome::AwaitingMesh : nav::RouteOutcome::NoKnownWay,
													   .labelEpoch = detailMesh.components().epoch(),
													   .patchEpoch = detailMesh.epoch()});
			takeableSince.emplace_back(routeTick, ticket);
			countOutstandingRoutes();
			return ticket;
		}
		const nav::RouteTicket queued = routes.request(args);
		queuedRoutes.emplace(ticket, QueuedRoute{.queued = queued, .goalIndices = std::move(goalIndices), .goalComing = goalComing});
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
		const bool	waits = found->outcome == nav::RouteOutcome::NoKnownWay && queued->second.goalComing;
		RouteAnswer answer{.outcome	   = waits ? nav::RouteOutcome::AwaitingMesh : found->outcome,
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
		const ObstacleKey at{world::worldToChunk({meters.x, meters.y}), std::bit_cast<std::uint32_t>(meters.x), std::bit_cast<std::uint32_t>(meters.y), {}};
		if (const auto it = obstacleInMesh.lower_bound(at); it != obstacleInMesh.end() && it->first.chunk == at.chunk && it->first.xBits == at.xBits &&
															 it->first.yBits == at.yBits && it->second >= 0) {
			return obstacleOf(it->second);
		}
		const std::int32_t tri = detailMesh.locate(nav::toMm(meters));
		if (tri < 0) {
			return std::nullopt;
		}
		std::optional<PlacedObstacle> nearest;
		for (const gnav::RingId id : detailMesh.cover(tri)) {
			if (id == gnav::kGroundToken || !isDetailTag(detailMesh.ring(id).polygon.provenanceId)) {
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
		join();
		return areaOnFaces(detailMesh, polygonMeters, [](const gnav::PatchableMesh& mesh, std::int32_t tri) {
			return gnav::terrainTraversable(mesh.triangles()[static_cast<std::size_t>(tri)]);
		});
	}

	bool NavigationSystem::isAreaBuildable(const std::vector<glm::vec2>& polygonMeters) const {
		join();
		return areaOnFaces(detailMesh, polygonMeters, buildableFace);
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
		join();
		if (detailMesh.locate(nav::toMm(meters)) < 0) {
			return std::nullopt;
		}
		const std::vector<WallBandM> bands = resolveBuiltBands(constructionWorld);
		return nearestStandable(detailMesh, meters, agentRadiusMeters, bands);
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
