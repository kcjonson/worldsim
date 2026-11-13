// Player visibility's seen record (player-visibility.md 3.3) as a second, standalone
// PatchableMesh holding only sight outlines as ground rings: legs walked in the open and the
// landing polygon, each tested with groundCovers and patched in only when it extends the union.
// The rows measure what that record costs: rings registered and bytes held per kilometre,
// patch and covered-test time, point location, the nav upkeep every patch runs, and a walk of
// the union's edge. Every row runs once and prints its figures on stderr; the counters carry
// the headline numbers.

#include "nav/MeshComponents.h"
#include "nav/NavMesh.h"
#include "nav/PatchableMesh.h"
#include "predicates/Predicates.h"
#include "visibility/Visibility.h"

#include <benchmark/benchmark.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

	using geometry::Vec2i64;
	using namespace geometry::nav;

	constexpr int		   kSightSides	   = 64;
	constexpr std::int64_t kSightMm		   = 30000;
	constexpr std::int64_t kSlackMm		   = 50;
	constexpr std::int64_t kLegRadiusMm	   = kSightMm - kSlackMm;
	constexpr std::int64_t kLandingMm	   = 150000;
	constexpr std::int64_t kSeenProvenance = -3;
	constexpr double	   kPi			   = 3.14159265358979323846;
	constexpr double	   kWalkMps		   = 2.0; // Movement::speed's default

	// PatchableMesh's private storage and door tagging, reached through explicit
	// instantiations, which the language exempts from access checking. Nothing here writes
	// except applyDoorTags, which sets the tags a patch already set.
	using RingsMember	   = geometry::PagedVector<RegisteredRing> PatchableMesh::*;
	using PiecesMember	   = geometry::PagedVector<std::vector<std::int32_t>> PatchableMesh::*;
	using EdgeOfMember	   = geometry::PagedVector<EdgeSource> PatchableMesh::*;
	using FreshWidthsMember = std::vector<std::int32_t> PatchableMesh::*;
	using DoorTagsMember   = void (PatchableMesh::*)(std::int32_t);
	struct RingsKey {
		friend RingsMember memberOf(RingsKey);
	};
	struct PiecesKey {
		friend PiecesMember memberOf(PiecesKey);
	};
	struct EdgeOfKey {
		friend EdgeOfMember memberOf(EdgeOfKey);
	};
	struct FreshWidthsKey {
		friend FreshWidthsMember memberOf(FreshWidthsKey);
	};
	struct DoorTagsKey {
		friend DoorTagsMember memberOf(DoorTagsKey);
	};
	template <typename Key, typename Member, Member Value> struct Reach {
		friend Member memberOf(Key) { return Value; }
	};
	template struct Reach<RingsKey, RingsMember, &PatchableMesh::rings>;
	template struct Reach<PiecesKey, PiecesMember, &PatchableMesh::pieces>;
	template struct Reach<EdgeOfKey, EdgeOfMember, &PatchableMesh::edgeOf>;
	template struct Reach<FreshWidthsKey, FreshWidthsMember, &PatchableMesh::freshWidthTriangles>;
	template struct Reach<DoorTagsKey, DoorTagsMember, &PatchableMesh::applyDoorTags>;

	// The width seed type is private, so its list is measured by a function the explicit
	// instantiation sets.
	struct ListSize {
		std::size_t count = 0;
		std::size_t bytes = 0;
	};
	using ListSizeOf = ListSize (*)(const PatchableMesh&);
	ListSizeOf widthSeedsOf = nullptr;
	template <auto Member> struct ReachList {
		static inline const bool set = (widthSeedsOf = [](const PatchableMesh& mesh) {
			const auto& list = mesh.*Member;
			return ListSize{list.size(), list.capacity() * sizeof(*list.data())};
		}, true);
	};
	template struct ReachList<&PatchableMesh::pendingWidthSeeds>;

	template <typename Work> double secondsOf(Work work) {
		const auto start = std::chrono::steady_clock::now();
		work();
		return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
	}

	double meanOf(const std::vector<double>& values) {
		double sum = 0.0;
		for (const double x : values) {
			sum += x;
		}
		return values.empty() ? 0.0 : sum / static_cast<double>(values.size());
	}

	double percentileOf(std::vector<double> values, double quantile) {
		if (values.empty()) {
			return 0.0;
		}
		std::sort(values.begin(), values.end());
		return values[std::min(values.size() - 1, static_cast<std::size_t>(quantile * static_cast<double>(values.size() - 1) + 0.5))];
	}

	double maxOf(const std::vector<double>& values) { return values.empty() ? 0.0 : *std::max_element(values.begin(), values.end()); }

	void boxOf(const std::vector<Vec2i64>& ring, Vec2i64& low, Vec2i64& high) {
		low = ring.front();
		high = ring.front();
		for (const Vec2i64& point : ring) {
			low = {std::min(low.x, point.x), std::min(low.y, point.y)};
			high = {std::max(high.x, point.x), std::max(high.y, point.y)};
		}
	}

	// A regular kSightSides-gon inscribed in the circle of `radius` round `centre`, each vertex
	// truncated toward the centre.
	std::vector<Vec2i64> sightPolygon(const Vec2i64& centre, std::int64_t radius) {
		std::vector<Vec2i64> out;
		out.reserve(kSightSides);
		for (int k = 0; k < kSightSides; ++k) {
			const double angle = 2.0 * kPi * static_cast<double>(k) / static_cast<double>(kSightSides);
			out.push_back({centre.x + static_cast<std::int64_t>(std::cos(angle) * static_cast<double>(radius)),
						   centre.y + static_cast<std::int64_t>(std::sin(angle) * static_cast<double>(radius))});
		}
		return out;
	}

	std::int64_t cross3(const Vec2i64& origin, const Vec2i64& first, const Vec2i64& second) {
		return (first.x - origin.x) * (second.y - origin.y) - (first.y - origin.y) * (second.x - origin.x);
	}

	// Convex hull, counter-clockwise, collinear points dropped.
	std::vector<Vec2i64> hullOf(std::vector<Vec2i64> pts) {
		std::sort(pts.begin(), pts.end());
		pts.erase(std::unique(pts.begin(), pts.end()), pts.end());
		std::vector<Vec2i64> hull(2 * pts.size());
		std::size_t			 size = 0;
		for (const Vec2i64& point : pts) {
			while (size >= 2 && cross3(hull[size - 2], hull[size - 1], point) <= 0) {
				--size;
			}
			hull[size++] = point;
		}
		const std::size_t lower = size + 1;
		for (std::size_t i = pts.size() - 1; i-- > 0;) {
			while (size >= lower && cross3(hull[size - 2], hull[size - 1], pts[i]) <= 0) {
				--size;
			}
			hull[size++] = pts[i];
		}
		hull.resize(size - 1);
		return hull;
	}

	// A leg's capsule: the hull of the leg-radius sight polygons at its two ends.
	std::vector<Vec2i64> legRing(const Vec2i64& start, const Vec2i64& end) {
		std::vector<Vec2i64> pts = sightPolygon(start, kLegRadiusMm);
		const std::vector<Vec2i64> atEnd = sightPolygon(end, kLegRadiusMm);
		pts.insert(pts.end(), atEnd.begin(), atEnd.end());
		return hullOf(std::move(pts));
	}

	Vec2i64 stepFrom(const Vec2i64& point, double heading, double lengthMm) {
		return {point.x + std::llround(std::cos(heading) * lengthMm), point.y + std::llround(std::sin(heading) * lengthMm)};
	}

	// Bytes the mesh holds, from its containers' sizes. The registry: each ring slot with its
	// outline, sample and edge ids, and the edge index (per ring edge its source, flag and
	// half-edge list). The triangles: each triangle slot with its edge sources, cover and
	// live flag, and each vertex slot with its use count. Box grids, hash tables and
	// interned lists are left out of both.
	struct Bytes {
		double registry  = 0.0;
		double triangles = 0.0;
	};

	Bytes bytesOf(const PatchableMesh& mesh) {
		Bytes		out;
		const auto& rings = mesh.*memberOf(RingsKey{});
		for (std::size_t i = 0; i < rings.size(); ++i) {
			const RegisteredRing& registeredRing = rings[i];
			out.registry += static_cast<double>(sizeof(RegisteredRing) + registeredRing.polygon.ring.capacity() * sizeof(Vec2i64) +
												registeredRing.sample.ring3.capacity() * sizeof(Vec2i64) + registeredRing.edgeIds.capacity() * sizeof(std::int32_t));
		}
		const auto& edgeOf = mesh.*memberOf(EdgeOfKey{});
		const auto& pieces = mesh.*memberOf(PiecesKey{});
		out.registry += static_cast<double>(edgeOf.size() * (sizeof(EdgeSource) + 1));
		for (std::size_t i = 0; i < pieces.size(); ++i) {
			out.registry += static_cast<double>(sizeof(std::vector<std::int32_t>) + pieces[i].capacity() * sizeof(std::int32_t));
		}
		out.triangles = static_cast<double>(mesh.triangles().size() * (sizeof(NavTriangle) + 3 * sizeof(std::int32_t) + sizeof(std::int32_t) + 1) +
											mesh.vertices().size() * (sizeof(Vec2i64) + sizeof(std::int32_t)));
		return out;
	}

	std::int64_t liveVertexCount(const PatchableMesh& mesh) {
		std::vector<std::uint8_t> used(mesh.vertices().size(), 0);
		std::int64_t			  count = 0;
		for (std::int32_t t = 0; t < static_cast<std::int32_t>(mesh.triangles().size()); ++t) {
			if (!mesh.triangleLive(t)) {
				continue;
			}
			for (const std::uint32_t vertex : mesh.triangles()[static_cast<std::size_t>(t)].v) {
				count += used[vertex] == 0 ? 1 : 0;
				used[vertex] = 1;
			}
		}
		return count;
	}

	// The union's boundary: every live triangle edge with nothing across it, collected in
	// one pass over the triangle slots. Best of five.
	struct EdgeWalk {
		double		micros	  = 0.0;
		std::size_t edges = 0;
	};

	EdgeWalk walkBoundary(const PatchableMesh& mesh) {
		EdgeWalk								 out;
		std::vector<std::pair<Vec2i64, Vec2i64>> edges;
		out.micros = 1e30;
		for (int run = 0; run < 5; ++run) {
			edges.clear();
			const double seconds = secondsOf([&] {
				for (std::int32_t t = 0; t < static_cast<std::int32_t>(mesh.triangles().size()); ++t) {
					if (!mesh.triangleLive(t)) {
						continue;
					}
					const NavTriangle& tri = mesh.triangles()[static_cast<std::size_t>(t)];
					for (std::size_t e = 0; e < 3; ++e) {
						if (tri.neighbor[e] < 0) {
							edges.push_back({mesh.vertices()[tri.v[e]], mesh.vertices()[tri.v[(e + 1) % 3]]});
						}
					}
				}
			});
			benchmark::DoNotOptimize(edges.data());
			out.micros = std::min(out.micros, seconds * 1e6);
		}
		out.edges = edges.size();
		return out;
	}

	// A covered test that reads the union's edge instead of the rings: the outline lies in
	// the union when every vertex of it is on the mesh and no boundary edge (a live triangle
	// edge with nothing across it) meets its closed area. A boundary edge that comes near is
	// not judged here; the test then answers "unsure" and the caller asks groundCovers. The
	// boundary edges sit in a grid of 16 m cells, which each patch report updates where the
	// patch changed the mesh.
	class EdgeCover {
	  public:
		enum class Answer { Covered, NotCovered, Unsure };

		Answer test(const PatchableMesh& mesh, const std::vector<Vec2i64>& outline) {
			for (const Vec2i64& point : outline) {
				if (mesh.locate(point) < 0) {
					return Answer::NotCovered;
				}
			}
			Vec2i64 low;
			Vec2i64 high;
			boxOf(outline, low, high);
			++stamp;
			for (std::int64_t cy = cellOf(low.y); cy <= cellOf(high.y); ++cy) {
				for (std::int64_t cx = cellOf(low.x); cx <= cellOf(high.x); ++cx) {
					const auto cell = cells.find(keyOf(cx, cy));
					if (cell == cells.end()) {
						continue;
					}
					for (const std::int32_t halfEdge : cell->second) {
						if (seen[static_cast<std::size_t>(halfEdge)] == stamp) {
							continue;
						}
						seen[static_cast<std::size_t>(halfEdge)] = stamp;
						const auto& [first, second]				  = edges[static_cast<std::size_t>(halfEdge)];
						if (std::max(first.x, second.x) < low.x || high.x < std::min(first.x, second.x) || std::max(first.y, second.y) < low.y || high.y < std::min(first.y, second.y)) {
							continue;
						}
						if (inside(first, outline)) {
							return Answer::Unsure;
						}
						for (std::size_t i = 0; i < outline.size(); ++i) {
							if (geometry::intersectSegments(first, second, outline[i], outline[(i + 1) % outline.size()]).relation != geometry::SegmentRelation::Disjoint) {
								return Answer::Unsure;
							}
						}
					}
				}
			}
			return Answer::Covered;
		}

		// Brings the grid up to date with a patch: every half-edge of a triangle the report
		// names leaves the grid, and goes back when its triangle is live with nothing across it.
		void update(const PatchableMesh& mesh, const PatchReport& report) {
			updateUs.push_back(secondsOf([&] {
				const std::size_t slots = mesh.triangles().size() * 3;
				if (edges.size() < slots) {
					edges.resize(slots);
					present.resize(slots, 0);
					seen.resize(slots, 0);
				}
				for (const ChangedTriangle& changed : report.changes) {
					for (std::int32_t halfEdge = changed.id * 3; halfEdge < changed.id * 3 + 3; ++halfEdge) {
						if (present[static_cast<std::size_t>(halfEdge)] != 0) {
							forEachCell(edges[static_cast<std::size_t>(halfEdge)], [&](std::uint64_t key) {
								std::vector<std::int32_t>& list = cells[key];
								list.erase(std::find(list.begin(), list.end(), halfEdge));
							});
							present[static_cast<std::size_t>(halfEdge)] = 0;
						}
					}
				}
				for (const ChangedTriangle& changed : report.changes) {
					if (!mesh.triangleLive(changed.id)) {
						continue;
					}
					const NavTriangle& tri = mesh.triangles()[static_cast<std::size_t>(changed.id)];
					for (std::size_t e = 0; e < 3; ++e) {
						if (tri.neighbor[e] >= 0) {
							continue;
						}
						const auto halfEdge						 = static_cast<std::int32_t>(changed.id * 3 + static_cast<std::int32_t>(e));
						edges[static_cast<std::size_t>(halfEdge)]	 = {mesh.vertices()[tri.v[e]], mesh.vertices()[tri.v[(e + 1) % 3]]};
						present[static_cast<std::size_t>(halfEdge)] = 1;
						forEachCell(edges[static_cast<std::size_t>(halfEdge)], [&](std::uint64_t key) { cells[key].push_back(halfEdge); });
					}
				}
			}) * 1e6);
		}

		std::vector<double> updateUs;

	  private:
		static constexpr std::int64_t kCellMm = 16000;

		static std::int64_t	 cellOf(std::int64_t values) { return values >= 0 ? values / kCellMm : -((-values + kCellMm - 1) / kCellMm); }
		static std::uint64_t keyOf(std::int64_t cx, std::int64_t cy) {
			return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(cx)) << 32) | static_cast<std::uint32_t>(cy);
		}

		template <typename Visit> static void forEachCell(const std::pair<Vec2i64, Vec2i64>& edge, Visit visit) {
			const auto& [first, second] = edge;
			for (std::int64_t cy = cellOf(std::min(first.y, second.y)); cy <= cellOf(std::max(first.y, second.y)); ++cy) {
				for (std::int64_t cx = cellOf(std::min(first.x, second.x)); cx <= cellOf(std::max(first.x, second.x)); ++cx) {
					visit(keyOf(cx, cy));
				}
			}
		}

		// Even-odd, for a point on no outline edge (one on an edge meets it, and is caught there).
		static bool inside(const Vec2i64& point, const std::vector<Vec2i64>& ring) {
			bool isInside = false;
			for (std::size_t i = 0; i < ring.size(); ++i) {
				const Vec2i64& first = ring[i];
				const Vec2i64& second = ring[(i + 1) % ring.size()];
				if ((first.y > point.y) != (second.y > point.y)) {
					const std::int64_t side = cross3(first, second, point);
					if (second.y > first.y ? side > 0 : side < 0) {
						isInside = !isInside;
					}
				}
			}
			return isInside;
		}

		std::vector<std::pair<Vec2i64, Vec2i64>>					 edges; // per half-edge t * 3 + e
		std::vector<std::uint8_t>									 present;
		std::unordered_map<std::uint64_t, std::vector<std::int32_t>> cells;
		std::vector<std::uint32_t>									 seen;
		std::uint32_t												 stamp = 0;
	};

	// The record: each shape tested with groundCovers and patched in only when it is not
	// covered, as player-visibility.md 3.3 has it. K for a shape is the registered rings
	// whose boxes meet its box: what the covered test and the patch read there. The edge
	// test runs beside it on every shape, timed with its fallback to groundCovers, and must
	// never call covered what groundCovers does not.
	struct SeenRecord {
		PatchableMesh							   mesh;
		std::vector<std::pair<Vec2i64, Vec2i64>> boxes;
		std::vector<double>						   patchMs;
		std::vector<double>						   coversUs;
		std::vector<double>						   coversK;
		std::vector<double>						   passes;
		EdgeCover								   edgeCover;
		std::vector<double>						   edgeTestUs; // with the fallback's groundCovers when unsure
		int										   unsure	 = 0;
		int										   edgeWrong = 0;
		PatchReport								   last;
		int										   dropped = 0;
		std::int64_t							   unread  = 0;

		double ringsOver(const Vec2i64& low, const Vec2i64& high) const {
			double count = 0.0;
			for (const auto& [otherLow, otherHigh] : boxes) {
				count += otherLow.x <= high.x && low.x <= otherHigh.x && otherLow.y <= high.y && low.y <= otherHigh.y ? 1.0 : 0.0;
			}
			return count;
		}

		bool commit(const std::vector<Vec2i64>& ring) {
			Vec2i64 low;
			Vec2i64 high;
			boxOf(ring, low, high);
			coversK.push_back(ringsOver(low, high));
			bool		 covered = false;
			const double seconds		 = secondsOf([&] { covered = mesh.groundCovers(ring); });
			coversUs.push_back(seconds * 1e6);
			EdgeCover::Answer answer = EdgeCover::Answer::Unsure;
			const double	  edgeSeconds = secondsOf([&] { answer = edgeCover.test(mesh, ring); });
			unsure += answer == EdgeCover::Answer::Unsure ? 1 : 0;
			edgeWrong += (answer == EdgeCover::Answer::Covered) != covered && answer != EdgeCover::Answer::Unsure ? 1 : 0;
			edgeTestUs.push_back((edgeSeconds + (answer == EdgeCover::Answer::Unsure ? seconds : 0.0)) * 1e6);
			if (covered) {
				++dropped;
				return false;
			}
			PatchEdits edits;
			edits.adds.push_back(NavInputPolygon{ring, false, kSeenProvenance});
			patchMs.push_back(secondsOf([&] { last = mesh.applyPatch(edits); }) * 1e3);
			passes.push_back(last.passes);
			unread += last.unreadCovers;
			edgeCover.update(mesh, last);
			boxes.push_back({low, high});
			return true;
		}

		std::size_t registered() const { return boxes.size(); }
	};

	struct LocateCost {
		double meanNs  = 0.0;
		double p99Ns   = 0.0;
		double offMesh = 0.0;
	};

	LocateCost locateCost(const PatchableMesh& mesh, const std::vector<Vec2i64>& points) {
		constexpr int		kRepeats = 16;
		std::vector<double> nanos;
		LocateCost			out;
		for (const Vec2i64& point : points) {
			std::int32_t triangle = -1;
			const double seconds = secondsOf([&] {
				for (int repeat = 0; repeat < kRepeats; ++repeat) {
					triangle = mesh.locate(point);
					benchmark::DoNotOptimize(triangle);
				}
			});
			out.offMesh += triangle < 0 ? 1.0 : 0.0;
			nanos.push_back(seconds * 1e9 / kRepeats);
		}
		out.meanNs = meanOf(nanos);
		out.p99Ns  = percentileOf(nanos, 0.99);
		return out;
	}

	// Points within 25 m of a walked path, on a random leg.
	std::vector<Vec2i64> pointsNear(const std::vector<Vec2i64>& path, std::mt19937& rng, std::size_t count) {
		std::vector<Vec2i64> out;
		while (out.size() < count) {
			const std::size_t  leg = rng() % (path.size() - 1);
			const double	   fraction = static_cast<double>(rng() % 1001) / 1000.0;
			const std::int64_t offsetX  = static_cast<std::int64_t>(rng() % 50001) - 25000;
			const std::int64_t offsetY  = static_cast<std::int64_t>(rng() % 50001) - 25000;
			const Vec2i64&	   first   = path[leg];
			const Vec2i64&	   second   = path[leg + 1];
			out.push_back({first.x + std::llround(fraction * static_cast<double>(second.x - first.x)) + offsetX, first.y + std::llround(fraction * static_cast<double>(second.y - first.y)) + offsetY});
		}
		return out;
	}

	// An explorer's walk eastward: legs of `legMm`, each turning up to 30 degrees from the
	// last and kept within 60 degrees of east, so the walk keeps reaching new ground.
	std::vector<Vec2i64> exploreWalk(std::int64_t legMm, std::int64_t totalMm, std::uint32_t seed) {
		std::mt19937		 rng(seed);
		std::vector<Vec2i64> out{{0, 0}};
		double				 heading = 0.0;
		for (std::int64_t distance = 0; distance < totalMm; distance += legMm) {
			const double turn = (static_cast<double>(rng() % 1201) - 600.0) / 600.0 * kPi / 6.0;
			heading			  = std::clamp(heading + turn, -kPi / 3.0, kPi / 3.0);
			out.push_back(stepFrom(out.back(), heading, static_cast<double>(legMm)));
		}
		return out;
	}

	void printTimes(const char* what, const std::vector<double>& values, const char* unit) {
		std::fprintf(stderr, "  %s %s mean %.3f p99 %.3f max %.3f (n %zu)\n", what, unit, meanOf(values), percentileOf(values, 0.99), maxOf(values), values.size());
	}

	void printEdgeTest(const SeenRecord& rec) {
		std::fprintf(stderr, "  edge test (with its fallback) us mean %.2f p99 %.2f max %.2f; unsure %d of %zu; wrong %d; grid update us mean %.1f max %.1f\n",
					 meanOf(rec.edgeTestUs), percentileOf(rec.edgeTestUs, 0.99), maxOf(rec.edgeTestUs), rec.unsure, rec.edgeTestUs.size(), rec.edgeWrong,
					 meanOf(rec.edgeCover.updateUs), maxOf(rec.edgeCover.updateUs));
	}

	// --- Exploration: legs of state.range(0) m over state.range(1) km --------------------

	void BM_SeenExplore(benchmark::State& state) {
		const std::int64_t legMm   = state.range(0) * 1000;
		const std::int64_t totalMm = state.range(1) * 1000000;
		for (auto _ : state) {
			const std::vector<Vec2i64> path = exploreWalk(legMm, totalMm, 7);
			SeenRecord				   rec;
			for (std::size_t i = 1; i < path.size(); ++i) {
				rec.commit(legRing(path[i - 1], path[i]));
			}
			const double	   kilometres	  = static_cast<double>(totalMm) / 1e6;
			const Bytes		   bytes  = bytesOf(rec.mesh);
			const std::int64_t verts  = liveVertexCount(rec.mesh);
			std::mt19937	   rng(11);
			const LocateCost   locate = locateCost(rec.mesh, pointsNear(path, rng, 10000));
			const EdgeWalk	   edge	  = walkBoundary(rec.mesh);
			const std::string  valid  = rec.mesh.validate();
			std::fprintf(stderr,
						 "[seen explore] legs %lld m, %.0f km: %zu legs, %zu rings registered, %d dropped as covered\n"
						 "  registry %.0f KB (%.1f KB/km), triangles %.0f KB (%.1f KB/km); %d live triangles, %lld live vertices\n",
						 static_cast<long long>(legMm / 1000), kilometres, path.size() - 1, rec.registered(), rec.dropped, bytes.registry / 1024.0,
						 bytes.registry / 1024.0 / kilometres, bytes.triangles / 1024.0, bytes.triangles / 1024.0 / kilometres, rec.mesh.liveTriangleCount(),
						 static_cast<long long>(verts));
			printTimes("patch", rec.patchMs, "ms");
			printTimes("groundCovers", rec.coversUs, "us");
			printEdgeTest(rec);
			std::fprintf(stderr, "  K mean %.1f max %.0f; passes mean %.2f max %.0f; unreadCovers %lld\n", meanOf(rec.coversK), maxOf(rec.coversK),
						 meanOf(rec.passes), maxOf(rec.passes), static_cast<long long>(rec.unread));
			std::fprintf(stderr, "  locate ns mean %.0f p99 %.0f, off mesh %.0f of 10000; boundary walk %.0f us for %zu edges; validate %s\n", locate.meanNs,
						 locate.p99Ns, locate.offMesh, edge.micros, edge.edges, valid.empty() ? "clean" : valid.c_str());
			state.SetIterationTime(meanOf(rec.patchMs) * static_cast<double>(rec.patchMs.size()) / 1e3);
			state.counters["rings"]		  = static_cast<double>(rec.registered());
			state.counters["regKBPerKm"]  = bytes.registry / 1024.0 / kilometres;
			state.counters["triKBPerKm"]  = bytes.triangles / 1024.0 / kilometres;
			state.counters["patchMeanMs"] = meanOf(rec.patchMs);
			state.counters["patchP99Ms"]  = percentileOf(rec.patchMs, 0.99);
			state.counters["patchMaxMs"]  = maxOf(rec.patchMs);
			state.counters["tris"]		  = rec.mesh.liveTriangleCount();
			state.counters["verts"]		  = static_cast<double>(verts);
			state.counters["locateNs"]	  = locate.meanNs;
			state.counters["locateP99Ns"] = locate.p99Ns;
			state.counters["edgeWalkUs"]  = edge.micros;
		}
	}

	// --- Covered test against K ------------------------------------------------------
	//
	// state.range(0) legs of 500 m run out from one hub in directions spread round the
	// circle in bit-reversed order, so every one reaches new ground and all overlap at the
	// hub. Then 1,000 short legs (2 to 15 m) within 5 m of the hub, every one covered.

	std::uint32_t bitReversed(std::uint32_t i, int bits) {
		std::uint32_t out = 0;
		for (int bit = 0; bit < bits; ++bit) {
			out = (out << 1) | ((i >> bit) & 1U);
		}
		return out;
	}

	void BM_SeenCoveredVsK(benchmark::State& state) {
		const auto count	= static_cast<std::uint32_t>(state.range(0));
		int		   bits = 0;
		while ((1U << bits) < count) {
			++bits;
		}
		for (auto _ : state) {
			SeenRecord rec;
			for (std::uint32_t i = 0; i < count; ++i) {
				const double heading = 2.0 * kPi * static_cast<double>(bitReversed(i, bits)) / static_cast<double>(1U << bits);
				rec.commit(legRing({0, 0}, stepFrom({0, 0}, heading, 500000.0)));
			}
			std::mt19937		rng(5);
			std::vector<double> micros;
			std::vector<double> ringCounts;
			int					covered = 0;
			std::vector<double> edgeUs;
			int					probeUnsure = 0;
			int					probeWrong	= 0;
			for (int i = 0; i < 1000; ++i) {
				const std::int64_t x	   = static_cast<std::int64_t>(rng() % 10001) - 5000;
				const std::int64_t y	   = static_cast<std::int64_t>(rng() % 10001) - 5000;
				const Vec2i64	   first{x, y};
				const double	   heading = static_cast<double>(rng() % 6284) / 1000.0;
				const double	   length  = 2000.0 + static_cast<double>(rng() % 13001);
				const std::vector<Vec2i64> ring = legRing(first, stepFrom(first, heading, length));
				Vec2i64			   low;
				Vec2i64			   high;
				boxOf(ring, low, high);
				ringCounts.push_back(rec.ringsOver(low, high));
				bool		 probeIsCovered = false;
				const double seconds = secondsOf([&] { probeIsCovered = rec.mesh.groundCovers(ring); });
				covered += probeIsCovered ? 1 : 0;
				micros.push_back(seconds * 1e6);
				EdgeCover::Answer answer = EdgeCover::Answer::Unsure;
				const double	  edgeSeconds = secondsOf([&] { answer = rec.edgeCover.test(rec.mesh, ring); });
				probeUnsure += answer == EdgeCover::Answer::Unsure ? 1 : 0;
				probeWrong += (answer == EdgeCover::Answer::Covered) != probeIsCovered && answer != EdgeCover::Answer::Unsure ? 1 : 0;
				edgeUs.push_back((edgeSeconds + (answer == EdgeCover::Answer::Unsure ? seconds : 0.0)) * 1e6);
			}
			// Whether applyPatch registers a ring the union already covers.
			const std::vector<Vec2i64> probe = legRing({0, 0}, {3000, 0});
			const bool				   probeCovered = rec.mesh.groundCovers(probe);
			PatchEdits				   edits;
			edits.adds.push_back(NavInputPolygon{probe, false, kSeenProvenance});
			const PatchReport report = rec.mesh.applyPatch(edits);
			std::fprintf(stderr,
						 "[seen covered vs K] %u spokes of 500 m: %zu registered; probes covered %d of 1000, K mean %.1f; groundCovers us mean %.2f p99 %.2f max %.2f\n"
						 "  a covered leg patched anyway (covered %s): registered as ring %d, %zu triangles touched\n",
						 count, rec.registered(), covered, meanOf(ringCounts), meanOf(micros), percentileOf(micros, 0.99), maxOf(micros), probeCovered ? "yes" : "no",
						 report.added.front(), report.changes.size());
			std::fprintf(stderr, "  probes: edge test (with its fallback) us mean %.2f p99 %.2f; unsure %d; wrong %d\n", meanOf(edgeUs), percentileOf(edgeUs, 0.99),
						 probeUnsure, probeWrong);
			state.SetIterationTime(meanOf(micros) / 1e6);
			state.counters["edgeTestUs"]  = meanOf(edgeUs);
			state.counters["edgeWrong"]	  = probeWrong;
			state.counters["K"]			  = meanOf(ringCounts);
			state.counters["coversUs"]	  = meanOf(micros);
			state.counters["coversP99Us"] = percentileOf(micros, 0.99);
		}
	}

	// --- Wandering inside seen ground ------------------------------------------------
	//
	// A region about 300 m across seen by a sweep (7 rows 40 m apart, 240 m long), then
	// 5,000 legs of 2 to 15 m at random headings by a walker kept in the sweep's box.

	void BM_SeenWander(benchmark::State& state) {
		constexpr std::int64_t kHalf = 120000;
		for (auto _ : state) {
			SeenRecord			 rec;
			std::vector<Vec2i64> sweep;
			bool				 east = true;
			for (std::int64_t y = -kHalf; y <= kHalf; y += 40000) {
				sweep.push_back({east ? -kHalf : kHalf, y});
				sweep.push_back({east ? kHalf : -kHalf, y});
				east = !east;
			}
			for (std::size_t i = 1; i < sweep.size(); ++i) {
				rec.commit(legRing(sweep[i - 1], sweep[i]));
			}
			const std::size_t seedRings = rec.registered();
			rec.patchMs.clear();
			rec.coversUs.clear();
			rec.coversK.clear();
			rec.dropped = 0;

			std::mt19937			rng(3);
			Vec2i64					position{0, 0};
			std::vector<std::size_t> registeredAt; // after each 1,000 legs
			for (int i = 0; i < 5000; ++i) {
				const double heading = static_cast<double>(rng() % 6284) / 1000.0;
				const double length	 = 2000.0 + static_cast<double>(rng() % 13001);
				Vec2i64		 next		 = stepFrom(position, heading, length);
				next.x				 = next.x > kHalf ? 2 * kHalf - next.x : (next.x < -kHalf ? -2 * kHalf - next.x : next.x);
				next.y				 = next.y > kHalf ? 2 * kHalf - next.y : (next.y < -kHalf ? -2 * kHalf - next.y : next.y);
				rec.commit(legRing(position, next));
				position = next;
				if ((i + 1) % 1000 == 0) {
					registeredAt.push_back(rec.registered());
				}
			}
			std::fprintf(stderr, "[seen wander] sweep registered %zu rings; 5000 legs: %d covered, %zu registered; registry after each 1000 legs:", seedRings,
						 rec.dropped, rec.registered() - seedRings);
			for (const std::size_t count : registeredAt) {
				std::fprintf(stderr, " %zu", count);
			}
			std::fprintf(stderr, "\n");
			printTimes("groundCovers", rec.coversUs, "us");
			printEdgeTest(rec);
			printTimes("patch (registered legs)", rec.patchMs, "ms");
			const double buckets[] = {0, 2, 4, 8, 16, 32, 1e9};
			for (std::size_t bucket = 0; bucket + 1 < std::size(buckets); ++bucket) {
				std::vector<double> inBucket;
				for (std::size_t i = 0; i < rec.coversK.size(); ++i) {
					if (rec.coversK[i] >= buckets[bucket] && rec.coversK[i] < buckets[bucket + 1]) {
						inBucket.push_back(rec.coversUs[i]);
					}
				}
				if (!inBucket.empty()) {
					std::fprintf(stderr, "  K in [%.0f, %.0f): %zu legs, groundCovers us mean %.2f p99 %.2f\n", buckets[bucket], std::min(buckets[bucket + 1], 999.0), inBucket.size(),
								 meanOf(inBucket), percentileOf(inBucket, 0.99));
				}
			}
			const Bytes bytes = bytesOf(rec.mesh);
			std::fprintf(stderr, "  K mean %.1f max %.0f; registry %.0f KB, triangles %.0f KB; %d live triangles\n", meanOf(rec.coversK), maxOf(rec.coversK),
						 bytes.registry / 1024.0, bytes.triangles / 1024.0, rec.mesh.liveTriangleCount());
			state.SetIterationTime(meanOf(rec.coversUs) * static_cast<double>(rec.coversUs.size()) / 1e6);
			state.counters["covered"]	  = rec.dropped;
			state.counters["registered"]  = static_cast<double>(rec.registered() - seedRings);
			state.counters["coversUs"]	  = meanOf(rec.coversUs);
			state.counters["coversP99Us"] = percentileOf(rec.coversUs, 0.99);
			state.counters["edgeTestUs"]  = meanOf(rec.edgeTestUs);
			state.counters["edgeWrong"]   = rec.edgeWrong;
			state.counters["Kmax"]		  = maxOf(rec.coversK);
		}
	}

	// --- Re-walks: a leg walked again a few state.range(0) mm to either side ------------
	//
	// A 100 m leg at 17.3 degrees, then 40 re-walks offset alternately left and right by
	// 1 to 40 times the unit, each with its far end 3 mm further over than its near end:
	// near-coincident, nearly parallel edges.

	void BM_SeenRewalk(benchmark::State& state) {
		const std::int64_t unit = state.range(0);
		for (auto _ : state) {
			SeenRecord	   rec;
			const double   heading = 17.3 * kPi / 180.0;
			const Vec2i64  origin{37123456, -18456789};
			const Vec2i64  end = stepFrom(origin, heading, 100000.0);
			const double   normalX  = -std::sin(heading);
			const double   normalY  = std::cos(heading);
			rec.commit(legRing(origin, end));
			for (std::int64_t i = 1; i <= 40; ++i) {
				const double off = static_cast<double>((i % 2 == 0 ? 1 : -1) * i * unit);
				const Vec2i64 first{origin.x + std::llround(normalX * off), origin.y + std::llround(normalY * off)};
				const Vec2i64 second{end.x + std::llround(normalX * (off + 3.0)), end.y + std::llround(normalY * (off + 3.0))};
				rec.commit(legRing(first, second));
			}
			const std::string valid = rec.mesh.validate();
			std::fprintf(stderr, "[seen rewalk] offsets of %lld mm: %zu of 41 registered; %d live triangles, %lld live vertices; unreadCovers %lld; validate %s\n",
						 static_cast<long long>(unit), rec.registered(), rec.mesh.liveTriangleCount(), static_cast<long long>(liveVertexCount(rec.mesh)),
						 static_cast<long long>(rec.unread), valid.empty() ? "clean" : valid.c_str());
			printTimes("patch", rec.patchMs, "ms");
			printTimes("groundCovers", rec.coversUs, "us");
			printEdgeTest(rec);
			std::fprintf(stderr, "  passes mean %.2f max %.0f\n", meanOf(rec.passes), maxOf(rec.passes));
			state.SetIterationTime(meanOf(rec.patchMs) * static_cast<double>(rec.patchMs.size()) / 1e3);
			state.counters["registered"]  = static_cast<double>(rec.registered());
			state.counters["patchMeanMs"] = meanOf(rec.patchMs);
			state.counters["patchMaxMs"]  = maxOf(rec.patchMs);
			state.counters["unread"]	  = static_cast<double>(rec.unread);
			state.counters["valid"]		  = valid.empty() ? 1.0 : 0.0;
		}
	}

	// --- Indoors: visibility polygons at a doorway -----------------------------------
	//
	// computeVisibilityPolygon at 30 m from viewpoints 0.5 m apart walking past a 1 m
	// doorway in a 0.2 m wall, along one jamb's face, and through it twice (the scene of
	// the PatchableMesh doorway test), each committed as its own ring.

	void BM_SeenDoorway(benchmark::State& state) {
		std::vector<geometry::OccluderSegment> walls;
		for (const auto& [x0, x1] : {std::pair<std::int64_t, std::int64_t>{-20000, -500}, std::pair<std::int64_t, std::int64_t>{500, 20000}}) {
			walls.push_back({{x0, 0}, {x1, 0}});
			walls.push_back({{x1, 0}, {x1, 200}});
			walls.push_back({{x1, 200}, {x0, 200}});
			walls.push_back({{x0, 200}, {x0, 0}});
		}
		std::vector<Vec2i64> viewpoints;
		for (std::int64_t x = -10000; x <= 10000; x += 500) {
			viewpoints.push_back({x, -3000});
		}
		for (std::int64_t y = -8000; y <= -500; y += 500) {
			viewpoints.push_back({500, y});
		}
		for (std::int64_t y = -8000; y <= 8000; y += 500) {
			viewpoints.push_back({0, y});
		}
		for (std::int64_t k = -17; k <= 17; ++k) {
			viewpoints.push_back({k * 350, k * 350 + 100});
		}
		for (auto _ : state) {
			SeenRecord			rec;
			std::vector<double> buildUs;
			std::vector<double> sizes;
			for (const Vec2i64& point : viewpoints) {
				geometry::Ring ring;
				buildUs.push_back(secondsOf([&] { ring = geometry::computeVisibilityPolygon(point, kSightMm, walls); }) * 1e6);
				sizes.push_back(static_cast<double>(ring.size()));
				rec.commit(ring);
			}
			const std::string valid = rec.mesh.validate();
			std::fprintf(stderr,
						 "[seen doorway] %zu viewpoints: %zu registered, %d covered; ring vertices mean %.0f max %.0f; %d live triangles; unreadCovers %lld; validate %s\n",
						 viewpoints.size(), rec.registered(), rec.dropped, meanOf(sizes), maxOf(sizes), rec.mesh.liveTriangleCount(),
						 static_cast<long long>(rec.unread), valid.empty() ? "clean" : valid.c_str());
			printTimes("computeVisibilityPolygon", buildUs, "us");
			printTimes("groundCovers", rec.coversUs, "us");
			printEdgeTest(rec);
			printTimes("patch", rec.patchMs, "ms");
			std::fprintf(stderr, "  K mean %.1f max %.0f\n", meanOf(rec.coversK), maxOf(rec.coversK));
			state.SetIterationTime(meanOf(rec.patchMs) * static_cast<double>(rec.patchMs.size()) / 1e3);
			state.counters["ringVerts"]	  = meanOf(sizes);
			state.counters["coversUs"]	  = meanOf(rec.coversUs);
			state.counters["coversMaxUs"] = maxOf(rec.coversUs);
			state.counters["patchMeanMs"] = meanOf(rec.patchMs);
			state.counters["patchMaxMs"]  = maxOf(rec.patchMs);
		}
	}

	// --- A camp: state.range(0) walkers wander a 150 m landing disc for an hour -----------
	//
	// The landing polygon goes in first. Each walker starts within 20 m of the drop and
	// walks legs of 2 to 15 m at random headings at 2 m/s, turning back toward the drop when
	// a leg would end more than 150 m from it; walkers take turns a minute at a time.

	void BM_SeenCamp(benchmark::State& state) {
		const auto walkers = static_cast<std::size_t>(state.range(0));
		for (auto _ : state) {
			SeenRecord rec;
			rec.commit(sightPolygon({0, 0}, kLandingMm));
			std::mt19937		 rng(17);
			std::vector<Vec2i64> positions(walkers);
			for (Vec2i64& point : positions) {
				const std::int64_t x = static_cast<std::int64_t>(rng() % 28001) - 14000;
				const std::int64_t y = static_cast<std::int64_t>(rng() % 28001) - 14000;
				point					 = {x, y};
			}
			const double			 perMinuteMm = kWalkMps * 60.0 * 1000.0;
			std::vector<std::size_t> byMinute;
			for (int minute = 0; minute < 60; ++minute) {
				for (Vec2i64& point : positions) {
					double walked = 0.0;
					while (walked < perMinuteMm) {
						double		 heading = static_cast<double>(rng() % 6284) / 1000.0;
						const double length	 = 2000.0 + static_cast<double>(rng() % 13001);
						Vec2i64		 next		 = stepFrom(point, heading, length);
						if (std::hypot(static_cast<double>(next.x), static_cast<double>(next.y)) > static_cast<double>(kLandingMm)) {
							heading = std::atan2(-static_cast<double>(point.y), -static_cast<double>(point.x));
							next		= stepFrom(point, heading, length);
						}
						rec.commit(legRing(point, next));
						point = next;
						walked += length;
					}
				}
				byMinute.push_back(rec.registered());
			}
			const double registered = static_cast<double>(rec.registered() - 1);
			std::fprintf(stderr,
						 "[seen camp] %zu walkers, 1 h: %zu legs, %.0f registered (%.1f per walker-hour); registered by minute 10 %zu, 30 %zu, 50 %zu, 60 %zu\n",
						 walkers, rec.coversUs.size() - 1, registered, registered / static_cast<double>(walkers), byMinute[9] - 1, byMinute[29] - 1,
						 byMinute[49] - 1, byMinute[59] - 1);
			printTimes("groundCovers", rec.coversUs, "us");
			printEdgeTest(rec);
			printTimes("patch", rec.patchMs, "ms");
			const Bytes bytes = bytesOf(rec.mesh);
			std::fprintf(stderr, "  K mean %.1f max %.0f; registry %.0f KB, triangles %.0f KB; %d live triangles; unreadCovers %lld\n", meanOf(rec.coversK),
						 maxOf(rec.coversK), bytes.registry / 1024.0, bytes.triangles / 1024.0, rec.mesh.liveTriangleCount(), static_cast<long long>(rec.unread));
			state.SetIterationTime(meanOf(rec.coversUs) * static_cast<double>(rec.coversUs.size()) / 1e6);
			state.counters["perWalkerHour"] = registered / static_cast<double>(walkers);
			state.counters["coversUs"]		= meanOf(rec.coversUs);
			state.counters["coversP99Us"]	= percentileOf(rec.coversUs, 0.99);
			state.counters["patchP99Ms"]	= percentileOf(rec.patchMs, 0.99);
			state.counters["Kmax"]			= maxOf(rec.coversK);
		}
	}

	// --- Nav upkeep a seen record pays for nothing -------------------------------------
	//
	// The 10 km walk in 20 m legs, twice. First with widths never refreshed: per patch, the
	// component labels' update (a second MeshComponents fed the same report, the work the
	// mesh does inside applyPatch) and the door tags of every created triangle (the mesh's
	// own applyDoorTags, run again), and how far the width lists grow. Then refreshWidths
	// after every patch.

	void BM_SeenUpkeep(benchmark::State& state) {
		for (auto _ : state) {
			const std::vector<Vec2i64> path = exploreWalk(20000, 10000000, 7);
			SeenRecord				   rec;
			MeshComponents			   labels;
			std::vector<double>		   labelUs;
			std::vector<double>		   doorUs;
			const auto				   doorTags = memberOf(DoorTagsKey{});
			for (std::size_t i = 1; i < path.size(); ++i) {
				if (!rec.commit(legRing(path[i - 1], path[i]))) {
					continue;
				}
				labelUs.push_back(secondsOf([&] { labels.update(rec.mesh.view(), rec.last); }) * 1e6);
				doorUs.push_back(secondsOf([&] {
									 for (const ChangedTriangle& changed : rec.last.changes) {
										 if ((changed.why & kTriangleCreated) != 0) {
											 (rec.mesh.*doorTags)(changed.id);
										 }
									 }
								 }) *
								 1e6);
			}
			const auto&		  fresh		 = rec.mesh.*memberOf(FreshWidthsKey{});
			const ListSize	  seeds		 = widthSeedsOf(rec.mesh);
			const double	  listBytes	 = static_cast<double>(fresh.capacity() * sizeof(std::int32_t) + seeds.bytes);
			const std::size_t freshCount = fresh.size();
			const std::size_t seedCount	 = seeds.count;
			const double	  catchUpMs	 = secondsOf([&] { rec.mesh.refreshWidths(); }) * 1e3;

			SeenRecord			eager;
			std::vector<double> refreshMs;
			for (std::size_t i = 1; i < path.size(); ++i) {
				if (eager.commit(legRing(path[i - 1], path[i]))) {
					refreshMs.push_back(secondsOf([&] { eager.mesh.refreshWidths(); }) * 1e3);
				}
			}
			std::fprintf(stderr, "[seen upkeep] 10 km in 20 m legs, %zu patches\n", rec.patchMs.size());
			printTimes("patch (labels and door tags inside)", rec.patchMs, "ms");
			printTimes("labels update", labelUs, "us");
			printTimes("door tags", doorUs, "us");
			std::fprintf(stderr, "  widths never refreshed: %zu fresh triangle ids and %zu seeds held, %.0f KB; one refresh then %.1f ms\n", freshCount,
						 seedCount, listBytes / 1024.0, catchUpMs);
			printTimes("refreshWidths after every patch", refreshMs, "ms");
			state.SetIterationTime(meanOf(labelUs) * 1e-6);
			state.counters["patchMeanMs"]	= meanOf(rec.patchMs);
			state.counters["labelMeanUs"]	= meanOf(labelUs);
			state.counters["doorMeanUs"]	= meanOf(doorUs);
			state.counters["widthListKB"]	= listBytes / 1024.0;
			state.counters["refreshMeanMs"] = meanOf(refreshMs);
		}
	}

} // namespace

BENCHMARK(BM_SeenExplore)
	->Args({20, 10})
	->Args({50, 10})
	->Args({100, 10})
	->Args({200, 10})
	->Args({20, 50})
	->Args({50, 50})
	->Args({100, 50})
	->Args({200, 50})
	->Iterations(1)
	->UseManualTime()
	->Unit(benchmark::kMillisecond);
BENCHMARK(BM_SeenCoveredVsK)->Arg(1)->Arg(4)->Arg(16)->Arg(64)->Arg(256)->Iterations(1)->UseManualTime()->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_SeenWander)->Iterations(1)->UseManualTime()->Unit(benchmark::kMillisecond);
BENCHMARK(BM_SeenDoorway)->Iterations(1)->UseManualTime()->Unit(benchmark::kMillisecond);
BENCHMARK(BM_SeenRewalk)->Arg(1)->Arg(10)->Iterations(1)->UseManualTime()->Unit(benchmark::kMillisecond);
BENCHMARK(BM_SeenCamp)->Arg(10)->Arg(50)->Iterations(1)->UseManualTime()->Unit(benchmark::kMillisecond);
BENCHMARK(BM_SeenUpkeep)->Iterations(1)->UseManualTime()->Unit(benchmark::kMicrosecond);
