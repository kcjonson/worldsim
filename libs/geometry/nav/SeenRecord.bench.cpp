// Player visibility's seen record (player-visibility.md 3.3) as a second, standalone
// PatchableMesh holding only sight outlines as ground rings: legs walked in the open, the
// landing polygon, and visibility polygons near walls, each tested with sightShapeCovered and
// patched in only when it is not covered. The rows measure what that record costs: rings
// registered and bytes held, patch and covered-test time, point location, the nav upkeep every
// patch runs, a walk of the union's edge, a base of rooms walked by colonists, and how often a
// visibility polygon is not a simple ring. Every row runs once and prints its figures on stderr;
// the counters carry the headline numbers.

#include "nav/MeshComponents.h"
#include "nav/NavMesh.h"
#include "nav/PatchableMesh.h"
#include "nav/SightCover.h"
#include "predicates/Predicates.h"
#include "visibility/Buildings.test.h"
#include "visibility/SightPolygon.h"
#include "visibility/Visibility.h"

#include <benchmark/benchmark.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace {

	using geometry::Vec2i64;
	using namespace geometry::nav;

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
	using RingsMember		= geometry::PagedVector<RegisteredRing> PatchableMesh::*;
	using PiecesMember		= geometry::PagedVector<std::vector<std::int32_t>> PatchableMesh::*;
	using EdgeOfMember		= geometry::PagedVector<EdgeSource> PatchableMesh::*;
	using FreshWidthsMember = std::vector<std::int32_t> PatchableMesh::*;
	using DoorTagsMember	= void (PatchableMesh::*)(std::int32_t);
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
	using ListSizeOf		 = ListSize (*)(const PatchableMesh&);
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

	double sumOf(const std::vector<double>& values) { return meanOf(values) * static_cast<double>(values.size()); }

	double percentileOf(std::vector<double> values, double quantile) {
		if (values.empty()) {
			return 0.0;
		}
		std::sort(values.begin(), values.end());
		return values[std::min(values.size() - 1, static_cast<std::size_t>(quantile * static_cast<double>(values.size() - 1) + 0.5))];
	}

	double maxOf(const std::vector<double>& values) { return values.empty() ? 0.0 : *std::max_element(values.begin(), values.end()); }

	void boxOf(const std::vector<Vec2i64>& ring, Vec2i64& low, Vec2i64& high) {
		low	 = ring.front();
		high = ring.front();
		for (const Vec2i64& point : ring) {
			low	 = {std::min(low.x, point.x), std::min(low.y, point.y)};
			high = {std::max(high.x, point.x), std::max(high.y, point.y)};
		}
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
		std::vector<Vec2i64>	   pts	 = geometry::sightPolygon(start, kLegRadiusMm);
		const std::vector<Vec2i64> atEnd = geometry::sightPolygon(end, kLegRadiusMm);
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
		double registry	 = 0.0;
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

	std::vector<Vec2i64> liveVertices(const PatchableMesh& mesh) {
		std::vector<std::uint8_t> used(mesh.vertices().size(), 0);
		std::vector<Vec2i64>	  out;
		for (std::int32_t t = 0; t < static_cast<std::int32_t>(mesh.triangles().size()); ++t) {
			if (!mesh.triangleLive(t)) {
				continue;
			}
			for (const std::uint32_t vertex : mesh.triangles()[static_cast<std::size_t>(t)].v) {
				if (used[vertex] == 0) {
					out.push_back(mesh.vertices()[vertex]);
				}
				used[vertex] = 1;
			}
		}
		std::sort(out.begin(), out.end());
		return out;
	}

	// The union's area in square metres, from its live triangles.
	double areaOf(const PatchableMesh& mesh) {
		double doubled = 0.0;
		for (std::int32_t t = 0; t < static_cast<std::int32_t>(mesh.triangles().size()); ++t) {
			if (!mesh.triangleLive(t)) {
				continue;
			}
			const NavTriangle& tri = mesh.triangles()[static_cast<std::size_t>(t)];
			doubled += std::abs(static_cast<double>(cross3(mesh.vertices()[tri.v[0]], mesh.vertices()[tri.v[1]], mesh.vertices()[tri.v[2]])));
		}
		return doubled / 2e6;
	}

	// The union's boundary: every live triangle edge with nothing across it, collected in
	// one pass over the triangle slots.
	std::vector<std::pair<Vec2i64, Vec2i64>> boundaryOf(const PatchableMesh& mesh) {
		std::vector<std::pair<Vec2i64, Vec2i64>> edges;
		for (std::int32_t t = 0; t < static_cast<std::int32_t>(mesh.triangles().size()); ++t) {
			if (!mesh.triangleLive(t)) {
				continue;
			}
			const NavTriangle& tri = mesh.triangles()[static_cast<std::size_t>(t)];
			for (std::size_t e = 0; e < 3; ++e) {
				if (tri.neighbor[e] < 0) {
					edges.emplace_back(mesh.vertices()[tri.v[e]], mesh.vertices()[tri.v[(e + 1) % 3]]);
				}
			}
		}
		std::sort(edges.begin(), edges.end());
		return edges;
	}

	// Best of five passes of boundaryOf.
	struct EdgeWalk {
		double		micros = 0.0;
		std::size_t edges  = 0;
	};

	EdgeWalk walkBoundary(const PatchableMesh& mesh) {
		EdgeWalk out;
		out.micros = 1e30;
		for (int run = 0; run < 5; ++run) {
			std::size_t	 count	 = 0;
			const double seconds = secondsOf([&] {
				for (std::int32_t t = 0; t < static_cast<std::int32_t>(mesh.triangles().size()); ++t) {
					if (!mesh.triangleLive(t)) {
						continue;
					}
					const NavTriangle& tri = mesh.triangles()[static_cast<std::size_t>(t)];
					for (std::size_t e = 0; e < 3; ++e) {
						count += tri.neighbor[e] < 0 ? 1 : 0;
					}
				}
			});
			benchmark::DoNotOptimize(count);
			out.micros = std::min(out.micros, seconds * 1e6);
			out.edges  = count;
		}
		return out;
	}

	// The record: each shape tested with sightShapeCovered and patched in only when it is not
	// covered, as player-visibility.md 3.3 has it. K for a shape is the registered rings whose
	// boxes meet its box: what groundCovers and the patch read there. Where `exact` is set,
	// groundCovers runs beside it on every shape, timed, to count where the two differ: a
	// shape the ground holds exactly must always be covered (exactNotCovered stays 0), and
	// coveredNotExact counts the shapes the tolerance let through.
	struct SeenRecord {
		PatchableMesh							 mesh;
		SightCoverScratch						 scratch;
		bool									 exact		= true;
		bool									 keepRings	= false;
		std::vector<std::pair<Vec2i64, Vec2i64>> boxes;
		std::vector<std::vector<Vec2i64>>		 rings; // registered, when keepRings
		std::vector<double>						 patchMs;
		std::vector<double>						 coveredUs; // sightShapeCovered
		std::vector<double>						 groundCoversUs;
		std::vector<double>						 coversK;
		std::vector<double>						 passes;
		int										 exactNotCovered = 0;
		int										 readRings		 = 0; // answers sightShapeCovered took from groundCovers
		int										 coveredNotExact = 0;
		PatchReport								 last;
		int										 dropped = 0;
		std::int64_t							 unread	 = 0;

		double ringsOver(const Vec2i64& low, const Vec2i64& high) const {
			double count = 0.0;
			for (const auto& [otherLow, otherHigh] : boxes) {
				count += otherLow.x <= high.x && low.x <= otherHigh.x && otherLow.y <= high.y && low.y <= otherHigh.y ? 1.0 : 0.0;
			}
			return count;
		}

		bool commit(const std::vector<Vec2i64>& ring, const Vec2i64& centre) {
			Vec2i64 low;
			Vec2i64 high;
			boxOf(ring, low, high);
			if (exact) {
				coversK.push_back(ringsOver(low, high));
			}
			bool covered = false;
			coveredUs.push_back(secondsOf([&] { covered = sightShapeCovered(mesh, ring, centre, scratch); }) * 1e6);
			readRings += scratch.readRings ? 1 : 0;
			if (exact) {
				bool held = false;
				groundCoversUs.push_back(secondsOf([&] { held = mesh.groundCovers(ring); }) * 1e6);
				exactNotCovered += held && !covered ? 1 : 0;
				coveredNotExact += covered && !held ? 1 : 0;
			}
			if (covered) {
				++dropped;
				return false;
			}
			PatchEdits edits;
			edits.adds.push_back(NavInputPolygon{ring, false, kSeenProvenance});
			patchMs.push_back(secondsOf([&] { last = mesh.applyPatch(edits); }) * 1e3);
			passes.push_back(last.passes);
			unread += last.unreadCovers;
			boxes.push_back({low, high});
			if (keepRings) {
				rings.push_back(ring);
			}
			return true;
		}

		std::size_t registered() const { return boxes.size(); }

		// Whether a mesh built from every registered ring in one patch has the same live
		// vertices and the same boundary.
		bool equalsFreshBuild() const {
			PatchableMesh fresh;
			PatchEdits	  edits;
			for (const std::vector<Vec2i64>& ring : rings) {
				edits.adds.push_back(NavInputPolygon{ring, false, kSeenProvenance});
			}
			fresh.applyPatch(edits);
			return liveVertices(fresh) == liveVertices(mesh) && boundaryOf(fresh) == boundaryOf(mesh);
		}
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
			const double seconds  = secondsOf([&] {
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
			const std::size_t  leg		= rng() % (path.size() - 1);
			const double	   fraction = static_cast<double>(rng() % 1001) / 1000.0;
			const std::int64_t offsetX	= static_cast<std::int64_t>(rng() % 50001) - 25000;
			const std::int64_t offsetY	= static_cast<std::int64_t>(rng() % 50001) - 25000;
			const Vec2i64&	   first	= path[leg];
			const Vec2i64&	   second	= path[leg + 1];
			out.push_back({first.x + std::llround(fraction * static_cast<double>(second.x - first.x)) + offsetX,
						   first.y + std::llround(fraction * static_cast<double>(second.y - first.y)) + offsetY});
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

	void printCovered(const SeenRecord& rec) {
		printTimes("sightShapeCovered", rec.coveredUs, "us");
		std::fprintf(stderr, "  sightShapeCovered answered by groundCovers %d of %zu\n", rec.readRings, rec.coveredUs.size());
		if (rec.exact) {
			printTimes("groundCovers", rec.groundCoversUs, "us");
			std::fprintf(stderr, "  held exactly but not covered %d; covered within the tolerance but not exactly %d\n", rec.exactNotCovered, rec.coveredNotExact);
		}
	}

	// --- Exploration: legs of state.range(0) m over state.range(1) km --------------------

	void BM_SeenExplore(benchmark::State& state) {
		const std::int64_t legMm   = state.range(0) * 1000;
		const std::int64_t totalMm = state.range(1) * 1000000;
		for (auto _ : state) {
			const std::vector<Vec2i64> path = exploreWalk(legMm, totalMm, 7);
			SeenRecord				   rec;
			for (std::size_t i = 1; i < path.size(); ++i) {
				rec.commit(legRing(path[i - 1], path[i]), path[i - 1]);
			}
			const double	   kilometres = static_cast<double>(totalMm) / 1e6;
			const Bytes		   bytes	  = bytesOf(rec.mesh);
			const auto		   verts	  = static_cast<std::int64_t>(liveVertices(rec.mesh).size());
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
			printCovered(rec);
			std::fprintf(stderr, "  K mean %.1f max %.0f; passes mean %.2f max %.0f; unreadCovers %lld\n", meanOf(rec.coversK), maxOf(rec.coversK),
						 meanOf(rec.passes), maxOf(rec.passes), static_cast<long long>(rec.unread));
			std::fprintf(stderr, "  locate ns mean %.0f p99 %.0f, off mesh %.0f of 10000; boundary walk %.0f us for %zu edges; validate %s\n", locate.meanNs,
						 locate.p99Ns, locate.offMesh, edge.micros, edge.edges, valid.empty() ? "clean" : valid.c_str());
			state.SetIterationTime(sumOf(rec.patchMs) / 1e3);
			state.counters["rings"]		  = static_cast<double>(rec.registered());
			state.counters["regKBPerKm"]  = bytes.registry / 1024.0 / kilometres;
			state.counters["patchMeanMs"] = meanOf(rec.patchMs);
			state.counters["coveredUs"]	  = meanOf(rec.coveredUs);
			state.counters["groundUs"]	  = meanOf(rec.groundCoversUs);
			state.counters["exactMiss"]	  = rec.exactNotCovered;
			state.counters["tris"]		  = rec.mesh.liveTriangleCount();
			state.counters["locateNs"]	  = locate.meanNs;
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
		const auto count = static_cast<std::uint32_t>(state.range(0));
		int		   bits	 = 0;
		while ((1U << bits) < count) {
			++bits;
		}
		for (auto _ : state) {
			SeenRecord rec;
			for (std::uint32_t i = 0; i < count; ++i) {
				const double heading = 2.0 * kPi * static_cast<double>(bitReversed(i, bits)) / static_cast<double>(1U << bits);
				rec.commit(legRing({0, 0}, stepFrom({0, 0}, heading, 500000.0)), {0, 0});
			}
			std::mt19937		rng(5);
			std::vector<double> groundUs;
			std::vector<double> coveredUs;
			std::vector<double> ringCounts;
			int					held	= 0;
			int					covered = 0;
			for (int i = 0; i < 1000; ++i) {
				const std::int64_t		   x	   = static_cast<std::int64_t>(rng() % 10001) - 5000;
				const std::int64_t		   y	   = static_cast<std::int64_t>(rng() % 10001) - 5000;
				const Vec2i64			   first{x, y};
				const double			   heading = static_cast<double>(rng() % 6284) / 1000.0;
				const double			   length  = 2000.0 + static_cast<double>(rng() % 13001);
				const std::vector<Vec2i64> ring	   = legRing(first, stepFrom(first, heading, length));
				Vec2i64					   low;
				Vec2i64					   high;
				boxOf(ring, low, high);
				ringCounts.push_back(rec.ringsOver(low, high));
				bool probeHeld = false;
				groundUs.push_back(secondsOf([&] { probeHeld = rec.mesh.groundCovers(ring); }) * 1e6);
				bool probeCovered = false;
				coveredUs.push_back(secondsOf([&] { probeCovered = sightShapeCovered(rec.mesh, ring, first, rec.scratch); }) * 1e6);
				held += probeHeld ? 1 : 0;
				covered += probeCovered ? 1 : 0;
			}
			std::fprintf(stderr,
						 "[seen covered vs K] %u spokes of 500 m: %zu registered; probes held %d and covered %d of 1000, K mean %.1f\n"
						 "  groundCovers us mean %.2f p99 %.2f; sightShapeCovered us mean %.2f p99 %.2f max %.2f\n",
						 count, rec.registered(), held, covered, meanOf(ringCounts), meanOf(groundUs), percentileOf(groundUs, 0.99), meanOf(coveredUs),
						 percentileOf(coveredUs, 0.99), maxOf(coveredUs));
			state.SetIterationTime(meanOf(coveredUs) / 1e6);
			state.counters["K"]			   = meanOf(ringCounts);
			state.counters["groundUs"]	   = meanOf(groundUs);
			state.counters["coveredUs"]	   = meanOf(coveredUs);
			state.counters["coveredP99Us"] = percentileOf(coveredUs, 0.99);
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
				rec.commit(legRing(sweep[i - 1], sweep[i]), sweep[i - 1]);
			}
			const std::size_t seedRings = rec.registered();
			rec.patchMs.clear();
			rec.coveredUs.clear();
			rec.groundCoversUs.clear();
			rec.coversK.clear();
			rec.dropped = 0;

			std::mt19937			 rng(3);
			Vec2i64					 position{0, 0};
			std::vector<std::size_t> registeredAt; // after each 1,000 legs
			for (int i = 0; i < 5000; ++i) {
				const double heading = static_cast<double>(rng() % 6284) / 1000.0;
				const double length	 = 2000.0 + static_cast<double>(rng() % 13001);
				Vec2i64		 next	 = stepFrom(position, heading, length);
				next.x				 = next.x > kHalf ? 2 * kHalf - next.x : (next.x < -kHalf ? -2 * kHalf - next.x : next.x);
				next.y				 = next.y > kHalf ? 2 * kHalf - next.y : (next.y < -kHalf ? -2 * kHalf - next.y : next.y);
				rec.commit(legRing(position, next), position);
				position = next;
				if ((i + 1) % 1000 == 0) {
					registeredAt.push_back(rec.registered());
				}
			}
			std::fprintf(stderr, "[seen wander] sweep registered %zu rings; 5000 legs: %d covered, %zu registered; registry after each 1000 legs:", seedRings,
						 rec.dropped, rec.registered() - seedRings);
			for (const std::size_t registered : registeredAt) {
				std::fprintf(stderr, " %zu", registered);
			}
			std::fprintf(stderr, "\n");
			printCovered(rec);
			printTimes("patch (registered legs)", rec.patchMs, "ms");
			std::fprintf(stderr, "  K mean %.1f max %.0f\n", meanOf(rec.coversK), maxOf(rec.coversK));
			state.SetIterationTime(sumOf(rec.coveredUs) / 1e6);
			state.counters["covered"]	   = rec.dropped;
			state.counters["registered"]   = static_cast<double>(rec.registered() - seedRings);
			state.counters["coveredUs"]	   = meanOf(rec.coveredUs);
			state.counters["coveredP99Us"] = percentileOf(rec.coveredUs, 0.99);
			state.counters["groundUs"]	   = meanOf(rec.groundCoversUs);
			state.counters["exactMiss"]	   = rec.exactNotCovered;
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
			SeenRecord	  rec;
			const double  heading = 17.3 * kPi / 180.0;
			const Vec2i64 origin{37123456, -18456789};
			const Vec2i64 end	  = stepFrom(origin, heading, 100000.0);
			const double  normalX = -std::sin(heading);
			const double  normalY = std::cos(heading);
			rec.commit(legRing(origin, end), origin);
			for (std::int64_t i = 1; i <= 40; ++i) {
				const double  off = static_cast<double>((i % 2 == 0 ? 1 : -1) * i * unit);
				const Vec2i64 first{origin.x + std::llround(normalX * off), origin.y + std::llround(normalY * off)};
				const Vec2i64 second{end.x + std::llround(normalX * (off + 3.0)), end.y + std::llround(normalY * (off + 3.0))};
				rec.commit(legRing(first, second), first);
			}
			const std::string valid = rec.mesh.validate();
			std::fprintf(stderr, "[seen rewalk] offsets of %lld mm: %zu of 41 registered; %d live triangles; unreadCovers %lld; validate %s\n",
						 static_cast<long long>(unit), rec.registered(), rec.mesh.liveTriangleCount(), static_cast<long long>(rec.unread),
						 valid.empty() ? "clean" : valid.c_str());
			printTimes("patch", rec.patchMs, "ms");
			printCovered(rec);
			state.SetIterationTime(sumOf(rec.patchMs) / 1e3);
			state.counters["registered"]  = static_cast<double>(rec.registered());
			state.counters["patchMeanMs"] = meanOf(rec.patchMs);
			state.counters["unread"]	  = static_cast<double>(rec.unread);
			state.counters["valid"]		  = valid.empty() ? 1.0 : 0.0;
		}
	}

	// --- Indoors: visibility polygons at a doorway -----------------------------------
	//
	// computeVisibilityPolygon at 30 m from viewpoints 0.5 m apart walking past a 1 m
	// doorway in a 0.2 m wall, along one jamb's face, and through it twice (the scene of
	// the PatchableMesh doorway test), each committed as its own ring with its viewpoint as
	// the centre.

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
			SeenRecord rec;
			rec.keepRings = true;
			std::vector<double> buildUs;
			std::vector<double> simpleUs;
			std::vector<double> sizes;
			int					notSimple = 0;
			for (const Vec2i64& point : viewpoints) {
				geometry::Ring ring;
				buildUs.push_back(secondsOf([&] { ring = geometry::computeVisibilityPolygon(point, kSightMm, walls); }) * 1e6);
				sizes.push_back(static_cast<double>(ring.size()));
				bool simple = false;
				simpleUs.push_back(secondsOf([&] { simple = geometry::simpleAround(ring, point); }) * 1e6);
				if (!simple) {
					++notSimple;
					continue;
				}
				rec.commit(ring, point);
			}
			const std::string valid = rec.mesh.validate();
			const bool		  fresh = rec.equalsFreshBuild();
			std::fprintf(stderr,
						 "[seen doorway] %zu viewpoints: %zu registered, %d covered, %d not simple; ring vertices mean %.0f max %.0f; %d live triangles; "
						 "unreadCovers %lld; validate %s; equals a fresh build %s\n",
						 viewpoints.size(), rec.registered(), rec.dropped, notSimple, meanOf(sizes), maxOf(sizes), rec.mesh.liveTriangleCount(),
						 static_cast<long long>(rec.unread), valid.empty() ? "clean" : valid.c_str(), fresh ? "yes" : "NO");
			printTimes("computeVisibilityPolygon", buildUs, "us");
			printTimes("simpleAround", simpleUs, "us");
			printCovered(rec);
			printTimes("patch", rec.patchMs, "ms");
			std::fprintf(stderr, "  K mean %.1f max %.0f\n", meanOf(rec.coversK), maxOf(rec.coversK));
			state.SetIterationTime(sumOf(rec.patchMs) / 1e3);
			state.counters["ringVerts"]	  = meanOf(sizes);
			state.counters["buildUs"]	  = meanOf(buildUs);
			state.counters["coveredUs"]	  = meanOf(rec.coveredUs);
			state.counters["groundUs"]	  = meanOf(rec.groundCoversUs);
			state.counters["patchMeanMs"] = meanOf(rec.patchMs);
			state.counters["patchMaxMs"]  = maxOf(rec.patchMs);
			state.counters["registered"]  = static_cast<double>(rec.registered());
			state.counters["fresh"]		  = fresh ? 1.0 : 0.0;
		}
	}

	// --- A base: state.range(0) colonists walking a building of eight rooms ---------------
	//
	// state.range(1): 1 when the landing polygon, centred 40 m west of the building, goes in first,
	// 0 for a record that starts empty (a base beyond the landing area). state.range(2) simulated
	// seconds, reported every state.range(3). The empty-record row takes about 12 minutes, nearly
	// all of it patches; with 20 colonists it did not reach its first simulated minute in 15.
	//
	// Rooms 4 to 8 m across in two rows, walls 0.2 m thick with a door in every inner wall and
	// in some outer ones, turned 20 degrees and 41 km from the origin. Each colonist walks at
	// 2 m/s from room to room through the doors, and now and then out of an outer door to points
	// 3 to 60 m out and back.
	// Sight runs every 0.1 s. With any of the building within 30 m (vision's rule: a wall in
	// sight) the colonist's visibility polygon is rebuilt after 0.5 m of travel and committed
	// with him as its centre; farther out he walks legs (spec 3.3), each ended when a position
	// held since it began strays more than kSlackMm off its line, committed as its capsule.

	struct Walker {
		Vec2i64				 position;
		std::vector<Vec2i64> route;
		std::size_t			 room		= 0;
		bool				 hasPolygon = false;
		Vec2i64				 builtAt;
		bool				 legOpen = false;
		Vec2i64				 legStart;
		std::vector<Vec2i64> held; // positions since the leg began
	};

	void BM_SeenBase(benchmark::State& state) {
		const auto walkers = static_cast<std::size_t>(state.range(0));
		const bool landed  = state.range(1) != 0;
		for (auto _ : state) {
			std::mt19937					   rng(31);
			const geometry::buildings::Building building =
				geometry::buildings::makeBuilding({5000, 8000, 4000, 6500}, {6000, 4500}, 20.0 * kPi / 180.0, {37123456, -18456789}, 0.4, rng);
			std::vector<std::size_t> outerDoors;
			for (std::size_t i = 0; i < building.doors.size(); ++i) {
				outerDoors.push_back(i);
			}
			outerDoors.erase(std::remove_if(outerDoors.begin(), outerDoors.end(),
											[&](std::size_t door) { return building.doors[door].other != geometry::buildings::kOutside; }),
							 outerDoors.end());
			const std::size_t rooms = building.roomCentres.size();

			// Doors in and out of each room, and the route between two rooms by breadth-first search.
			std::vector<std::vector<std::size_t>> doorsOf(rooms);
			for (std::size_t i = 0; i < building.doors.size(); ++i) {
				doorsOf[building.doors[i].room].push_back(i);
				if (building.doors[i].other != geometry::buildings::kOutside) {
					doorsOf[building.doors[i].other].push_back(i);
				}
			}
			auto routeBetween = [&](std::size_t from, std::size_t to, std::vector<Vec2i64>& route) {
				std::vector<std::size_t> cameBy(rooms, building.doors.size());
				std::vector<std::size_t> queue{from};
				std::vector<std::uint8_t> reached(rooms, 0);
				reached[from] = 1;
				for (std::size_t head = 0; head < queue.size(); ++head) {
					for (const std::size_t door : doorsOf[queue[head]]) {
						const geometry::buildings::Door& through = building.doors[door];
						if (through.other == geometry::buildings::kOutside) {
							continue;
						}
						const std::size_t next = through.room == queue[head] ? through.other : through.room;
						if (reached[next] == 0) {
							reached[next] = 1;
							cameBy[next]  = door;
							queue.push_back(next);
						}
					}
				}
				std::vector<Vec2i64> backwards;
				for (std::size_t room = to; room != from;) {
					const geometry::buildings::Door& door = building.doors[cameBy[room]];
					backwards.push_back(building.roomCentres[room]);
					backwards.push_back(door.centre);
					room = door.room == room ? door.other : door.room;
				}
				route.insert(route.end(), backwards.rbegin(), backwards.rend());
			};

			SeenRecord rec;
			rec.exact = false;
			if (landed) {
				const Vec2i64 drop = building.roomCentres.front() + Vec2i64{-40000, 0};
				rec.commit(geometry::sightPolygon(drop, kLandingMm), drop);
			}
			const std::size_t landingRings = rec.registered();
			rec.coveredUs.clear();
			rec.patchMs.clear();

			std::vector<Walker> crew(walkers);
			for (std::size_t i = 0; i < walkers; ++i) {
				crew[i].room	 = i % rooms;
				crew[i].position = building.roomCentres[crew[i].room];
			}
			std::vector<double>		 polygonCoveredUs;
			std::vector<double>		 legCoveredUs;
			std::vector<double>		 polygonPatchMs;
			std::vector<double>		 legPatchMs;
			std::vector<double>		 buildUs;
			std::vector<double>		 simpleUs;
			std::vector<double>		 vertexCounts;
			int						 polygons		  = 0;
			int						 polygonsCovered  = 0;
			int						 legs			  = 0;
			int						 legsCovered	  = 0;
			int						 notSimple		  = 0;
			std::vector<std::size_t> registeredByPeriod;
			std::vector<double>		 bytesByPeriod;
			std::vector<int>		 trianglesByPeriod;

			// What each registered shape added to the union, in square metres, bucketed: under
			// 0.01, 0.1, 1 and 10, and the rest; and K where each registered.
			std::array<int, 5>	gainBuckets{};
			std::vector<double> registeredK;
			double				unionArea = areaOf(rec.mesh);
			auto commitShape = [&](const std::vector<Vec2i64>& shape, const Vec2i64& centre, bool polygon) {
				const std::size_t before = rec.patchMs.size();
				Vec2i64			  low;
				Vec2i64			  high;
				boxOf(shape, low, high);
				const double overlapping = rec.ringsOver(low, high);
				const bool	 added		 = rec.commit(shape, centre);
				(polygon ? polygonCoveredUs : legCoveredUs).push_back(rec.coveredUs.back());
				if (added) {
					(polygon ? polygonPatchMs : legPatchMs).push_back(rec.patchMs[before]);
					registeredK.push_back(overlapping);
					const double area = areaOf(rec.mesh);
					const double gain = area - unionArea;
					unionArea		  = area;
					++gainBuckets[gain < 0.01 ? 0 : (gain < 0.1 ? 1 : (gain < 1.0 ? 2 : (gain < 10.0 ? 3 : 4)))];
				}
				if (polygon) {
					++polygons;
					polygonsCovered += added ? 0 : 1;
				} else {
					++legs;
					legsCovered += added ? 0 : 1;
				}
			};
			auto closeLeg = [&](Walker& walker, const Vec2i64& end) {
				if (walker.legOpen && end != walker.legStart) {
					commitShape(legRing(walker.legStart, end), walker.legStart, false);
				}
				walker.legOpen = false;
				walker.held.clear();
			};
			auto planNext = [&](Walker& walker) {
				if (rng() % 4 == 0 && !outerDoors.empty()) {
					const geometry::buildings::Door& door = building.doors[outerDoors[rng() % outerDoors.size()]];
					routeBetween(walker.room, door.room, walker.route);
					walker.route.push_back(door.centre);
					walker.route.push_back(door.outward);
					const double outX	  = static_cast<double>(door.outward.x - door.centre.x) / 1000.0;
					const double outY	  = static_cast<double>(door.outward.y - door.centre.y) / 1000.0;
					const int	 stops	  = 1 + static_cast<int>(rng() % 3);
					for (int stop = 0; stop < stops; ++stop) {
						const double out	 = 3000.0 + static_cast<double>(rng() % 57001);
						const double lateral = static_cast<double>(rng() % 40001) - 20000.0;
						walker.route.push_back({door.centre.x + std::llround(outX * out - outY * lateral), door.centre.y + std::llround(outY * out + outX * lateral)});
					}
					walker.route.push_back(door.outward);
					walker.route.push_back(door.centre);
					walker.route.push_back(building.roomCentres[door.room]);
					walker.room = door.room;
				} else {
					const std::size_t target = rng() % rooms;
					routeBetween(walker.room, target, walker.route);
					const std::int64_t offsetX = static_cast<std::int64_t>(rng() % 2001) - 1000;
					const std::int64_t offsetY = static_cast<std::int64_t>(rng() % 2001) - 1000;
					walker.route.push_back(building.roomCentres[target] + Vec2i64{offsetX, offsetY});
					walker.room = target;
				}
			};

			constexpr double kStepSeconds = 0.1;
			const int		 periodSeconds = static_cast<int>(state.range(3));
			const int		 periods	   = static_cast<int>(state.range(2)) / periodSeconds;
			const double	 stepMm		  = kWalkMps * 1000.0 * kStepSeconds;
			const int		 stepsPerPeriod = static_cast<int>(periodSeconds / kStepSeconds + 0.5);
			for (int period = 0; period < periods; ++period) {
				for (int step = 0; step < stepsPerPeriod; ++step) {
					for (Walker& walker : crew) {
						double remaining = stepMm;
						while (remaining > 0.0) {
							if (walker.route.empty()) {
								planNext(walker);
							}
							const Vec2i64 target   = walker.route.front();
							const double  toX	   = static_cast<double>(target.x - walker.position.x);
							const double  toY	   = static_cast<double>(target.y - walker.position.y);
							const double  distance = std::sqrt(toX * toX + toY * toY);
							if (distance <= remaining) {
								walker.position = target;
								walker.route.erase(walker.route.begin());
								remaining -= distance;
							} else {
								walker.position = {walker.position.x + std::llround(toX / distance * remaining), walker.position.y + std::llround(toY / distance * remaining)};
								remaining		= 0.0;
							}
						}
						const bool indoors = building.distanceTo(walker.position) <= static_cast<double>(kSightMm);
						if (indoors) {
							if (walker.legOpen) {
								closeLeg(walker, walker.held.empty() ? walker.position : walker.held.back());
							}
							const double movedX = static_cast<double>(walker.position.x - walker.builtAt.x);
							const double movedY = static_cast<double>(walker.position.y - walker.builtAt.y);
							if (walker.hasPolygon && movedX * movedX + movedY * movedY <= 500.0 * 500.0) {
								continue;
							}
							walker.hasPolygon = true;
							walker.builtAt	  = walker.position;
							geometry::Ring ring;
							buildUs.push_back(secondsOf([&] { ring = geometry::computeVisibilityPolygon(walker.position, kSightMm, building.walls); }) * 1e6);
							vertexCounts.push_back(static_cast<double>(ring.size()));
							bool simple = false;
							simpleUs.push_back(secondsOf([&] { simple = geometry::simpleAround(ring, walker.position); }) * 1e6);
							if (!simple) {
								++notSimple;
								continue;
							}
							commitShape(ring, walker.position, true);
							continue;
						}
						walker.hasPolygon = false;
						if (!walker.legOpen) {
							walker.legOpen	= true;
							walker.legStart = walker.position;
							walker.held.clear();
							continue;
						}
						// The leg ends where a position held since it began strays off the line
						// from its start to here; it is committed up to the last position before.
						const double lineX	= static_cast<double>(walker.position.x - walker.legStart.x);
						const double lineY	= static_cast<double>(walker.position.y - walker.legStart.y);
						const double length = std::sqrt(lineX * lineX + lineY * lineY);
						bool		 strayed = false;
						for (const Vec2i64& point : walker.held) {
							const double offX = static_cast<double>(point.x - walker.legStart.x);
							const double offY = static_cast<double>(point.y - walker.legStart.y);
							if (length > 0.0 && std::abs(lineX * offY - lineY * offX) / length > static_cast<double>(kSlackMm)) {
								strayed = true;
								break;
							}
						}
						if (strayed) {
							const Vec2i64 corner = walker.held.back();
							closeLeg(walker, corner);
							walker.legOpen	= true;
							walker.legStart = corner;
						}
						walker.held.push_back(walker.position);
					}
				}
				registeredByPeriod.push_back(rec.registered() - landingRings);
				bytesByPeriod.push_back(bytesOf(rec.mesh).registry);
				trianglesByPeriod.push_back(rec.mesh.liveTriangleCount());
				std::fprintf(stderr, "  %d s: %zu rings registered, %d polygons, %d legs, %d answered by groundCovers; patches %.0f ms, covered tests %.0f ms so far\n",
							 (period + 1) * periodSeconds, rec.registered() - landingRings, polygons, legs, rec.readRings, sumOf(polygonPatchMs) + sumOf(legPatchMs),
							 (sumOf(polygonCoveredUs) + sumOf(legCoveredUs)) / 1e3);
			}
			const std::string valid	  = rec.mesh.validate();
			const double	  seconds = static_cast<double>(periodSeconds) * periods;
			const double recordMs = (sumOf(simpleUs) + sumOf(polygonCoveredUs) + sumOf(legCoveredUs)) / 1e3 + sumOf(polygonPatchMs) + sumOf(legPatchMs);
			std::fprintf(stderr,
						 "[seen base] %zu colonists, %s, %d rooms, %zu outer doors, %.0f min: %d polygons (%d covered, %.1f%%), %d legs (%d covered), %d not simple; "
						 "%zu rings registered; %d live triangles; unreadCovers %lld; validate %s\n",
						 walkers, landed ? "landed 40 m away" : "no landing", static_cast<int>(rooms), outerDoors.size(), seconds / 60.0, polygons, polygonsCovered,
						 100.0 * static_cast<double>(polygonsCovered) / std::max(1, polygons), legs, legsCovered, notSimple, (rec.registered() - landingRings),
						 rec.mesh.liveTriangleCount(), static_cast<long long>(rec.unread), valid.empty() ? "clean" : valid.c_str());
			std::fprintf(stderr, "  polygon vertices mean %.0f max %.0f; sightShapeCovered answered by groundCovers %d times\n", meanOf(vertexCounts),
						 maxOf(vertexCounts), rec.readRings);
			printTimes("computeVisibilityPolygon (vision's cost)", buildUs, "us");
			printTimes("simpleAround", simpleUs, "us");
			printTimes("sightShapeCovered, polygons", polygonCoveredUs, "us");
			printTimes("sightShapeCovered, legs", legCoveredUs, "us");
			printTimes("patch, polygons not covered", polygonPatchMs, "ms");
			printTimes("patch, legs not covered", legPatchMs, "ms");
			std::fprintf(stderr, "  K where registered mean %.0f max %.0f; area each registration added: under 0.01 m2 %d, 0.01 to 0.1 %d, 0.1 to 1 %d, 1 to 10 %d, over 10 %d; union %.0f m2\n",
						 meanOf(registeredK), maxOf(registeredK), gainBuckets[0], gainBuckets[1], gainBuckets[2], gainBuckets[3], gainBuckets[4], unionArea);
			std::fprintf(stderr, "  by period of %d s: rings registered", periodSeconds);
			for (std::size_t period = 0; period < registeredByPeriod.size(); ++period) {
				std::fprintf(stderr, " %zu", registeredByPeriod[period] - (period == 0 ? 0 : registeredByPeriod[period - 1]));
			}
			std::fprintf(stderr, "; registry KB");
			for (const double bytes : bytesByPeriod) {
				std::fprintf(stderr, " %.0f", bytes / 1024.0);
			}
			std::fprintf(stderr, "; live triangles");
			for (const int triangles : trianglesByPeriod) {
				std::fprintf(stderr, " %d", triangles);
			}
			std::fprintf(stderr, "\n  record's main-thread cost %.3f ms per simulated second (simple check, covered test, patches); vision's polygons %.3f ms per second\n",
						 recordMs / seconds, sumOf(buildUs) / 1e3 / seconds);
			state.SetIterationTime(recordMs / 1e3);
			state.counters["polygons"]		   = polygons;
			state.counters["coveredShare"]	   = static_cast<double>(polygonsCovered) / std::max(1, polygons);
			state.counters["coveredUs"]		   = meanOf(polygonCoveredUs);
			state.counters["coveredP99Us"]	   = percentileOf(polygonCoveredUs, 0.99);
			state.counters["patchMeanMs"]	   = meanOf(polygonPatchMs);
			state.counters["registered"]	   = static_cast<double>(rec.registered() - landingRings);
			state.counters["lastPeriod"]	   = static_cast<double>(registeredByPeriod.back() - (registeredByPeriod.size() > 1 ? registeredByPeriod[registeredByPeriod.size() - 2] : 0));
			state.counters["msPerSimSecond"]   = recordMs / seconds;
			state.counters["notSimple"]		   = notSimple;
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
			rec.commit(geometry::sightPolygon({0, 0}, kLandingMm), {0, 0});
			std::mt19937		 rng(17);
			std::vector<Vec2i64> positions(walkers);
			for (Vec2i64& point : positions) {
				const std::int64_t x = static_cast<std::int64_t>(rng() % 28001) - 14000;
				const std::int64_t y = static_cast<std::int64_t>(rng() % 28001) - 14000;
				point				 = {x, y};
			}
			const double			 perMinuteMm = kWalkMps * 60.0 * 1000.0;
			std::vector<std::size_t> byMinute;
			for (int minute = 0; minute < 60; ++minute) {
				for (Vec2i64& point : positions) {
					double walked = 0.0;
					while (walked < perMinuteMm) {
						double		 heading = static_cast<double>(rng() % 6284) / 1000.0;
						const double length	 = 2000.0 + static_cast<double>(rng() % 13001);
						Vec2i64		 next	 = stepFrom(point, heading, length);
						if (std::hypot(static_cast<double>(next.x), static_cast<double>(next.y)) > static_cast<double>(kLandingMm)) {
							heading = std::atan2(-static_cast<double>(point.y), -static_cast<double>(point.x));
							next	= stepFrom(point, heading, length);
						}
						rec.commit(legRing(point, next), point);
						point = next;
						walked += length;
					}
				}
				byMinute.push_back(rec.registered());
			}
			const double registered = static_cast<double>(rec.registered() - 1);
			std::fprintf(stderr,
						 "[seen camp] %zu walkers, 1 h: %zu legs, %.0f registered (%.1f per walker-hour); registered by minute 10 %zu, 30 %zu, 50 %zu, 60 %zu\n",
						 walkers, rec.coveredUs.size() - 1, registered, registered / static_cast<double>(walkers), byMinute[9] - 1, byMinute[29] - 1,
						 byMinute[49] - 1, byMinute[59] - 1);
			printCovered(rec);
			printTimes("patch", rec.patchMs, "ms");
			const Bytes bytes = bytesOf(rec.mesh);
			std::fprintf(stderr, "  K mean %.1f max %.0f; registry %.0f KB, triangles %.0f KB; %d live triangles; unreadCovers %lld\n", meanOf(rec.coversK),
						 maxOf(rec.coversK), bytes.registry / 1024.0, bytes.triangles / 1024.0, rec.mesh.liveTriangleCount(), static_cast<long long>(rec.unread));
			state.SetIterationTime(sumOf(rec.coveredUs) / 1e6);
			state.counters["perWalkerHour"] = registered / static_cast<double>(walkers);
			state.counters["coveredUs"]		= meanOf(rec.coveredUs);
			state.counters["coveredP99Us"]	= percentileOf(rec.coveredUs, 0.99);
			state.counters["groundUs"]		= meanOf(rec.groundCoversUs);
			state.counters["exactMiss"]		= rec.exactNotCovered;
			state.counters["Kmax"]			= maxOf(rec.coversK);
		}
	}

	// --- Simple rings: visibility polygons from observers in and round random buildings ----
	//
	// state.range(0) random buildings (2 to 4 by 1 to 3 rooms of 3 to 9 m, at random angles
	// and up to 40 km out), 25 observers each: on open floor, 1 to 300 mm off wall corners,
	// and in doorways. Each polygon is checked by simpleAround and by an exact test of every
	// pair of edges.

	void BM_SeenSimplicityFuzz(benchmark::State& state) {
		for (auto _ : state) {
			std::mt19937		rng(0x5EE4u);
			int					polygons	= 0;
			int					turnedAway	= 0;
			int					notSimple	= 0;
			int					disagree	= 0;
			std::vector<double> buildUs;
			std::vector<double> simpleUs;
			std::vector<double> vertexCounts;
			for (std::int64_t scene = 0; scene < state.range(0); ++scene) {
				const geometry::buildings::Building building = geometry::buildings::randomBuilding(rng);
				for (const Vec2i64& observer : geometry::buildings::observersFor(building, rng, 25)) {
					geometry::Ring ring;
					buildUs.push_back(secondsOf([&] { ring = geometry::computeVisibilityPolygon(observer, kSightMm, building.walls); }) * 1e6);
					vertexCounts.push_back(static_cast<double>(ring.size()));
					bool accepted = false;
					simpleUs.push_back(secondsOf([&] { accepted = geometry::simpleAround(ring, observer); }) * 1e6);
					bool simple = ring.size() >= 3;
					for (std::size_t i = 0; i < ring.size() && simple; ++i) {
						for (std::size_t j = i + 1; j < ring.size() && simple; ++j) {
							const geometry::SegmentRelation relation =
								geometry::intersectSegments(ring[i], ring[(i + 1) % ring.size()], ring[j], ring[(j + 1) % ring.size()]).relation;
							const bool adjacent = j == i + 1 || (i == 0 && j == ring.size() - 1);
							simple = relation == geometry::SegmentRelation::Disjoint || (adjacent && relation == geometry::SegmentRelation::EndpointTouch);
						}
					}
					++polygons;
					turnedAway += accepted ? 0 : 1;
					notSimple += simple ? 0 : 1;
					disagree += accepted != simple ? 1 : 0;
				}
			}
			std::fprintf(stderr, "[seen simple rings] %d polygons from %lld buildings: %d turned away by simpleAround, %d not simple by every pair, %d disagree\n",
						 polygons, static_cast<long long>(state.range(0)), turnedAway, notSimple, disagree);
			std::fprintf(stderr, "  vertices mean %.0f max %.0f\n", meanOf(vertexCounts), maxOf(vertexCounts));
			printTimes("computeVisibilityPolygon", buildUs, "us");
			printTimes("simpleAround", simpleUs, "us");
			state.SetIterationTime(sumOf(simpleUs) / 1e6);
			state.counters["polygons"]	 = polygons;
			state.counters["turnedAway"] = turnedAway;
			state.counters["notSimple"]	 = notSimple;
			state.counters["disagree"]	 = disagree;
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
			rec.exact = false;
			MeshComponents		labels;
			std::vector<double> labelUs;
			std::vector<double> doorUs;
			const auto			doorTags = memberOf(DoorTagsKey{});
			for (std::size_t i = 1; i < path.size(); ++i) {
				if (!rec.commit(legRing(path[i - 1], path[i]), path[i - 1])) {
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

			SeenRecord eager;
			eager.exact = false;
			std::vector<double> refreshMs;
			for (std::size_t i = 1; i < path.size(); ++i) {
				if (eager.commit(legRing(path[i - 1], path[i]), path[i - 1])) {
					refreshMs.push_back(secondsOf([&] { eager.mesh.refreshWidths(); }) * 1e3);
				}
			}
			std::fprintf(stderr, "[seen upkeep] 10 km in 20 m legs, %zu patches\n", rec.patchMs.size());
			printTimes("patch (labels and door tags inside)", rec.patchMs, "ms");
			printTimes("labels update", labelUs, "us");
			printTimes("door tags", doorUs, "us");
			std::fprintf(stderr, "  widths never refreshed: %zu fresh triangle ids and %zu seeds held, %.0f KB; one refresh then %.1f ms\n", freshCount, seedCount,
						 listBytes / 1024.0, catchUpMs);
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
BENCHMARK(BM_SeenBase)->Args({20, 1, 600, 60})->Args({50, 1, 600, 60})->Args({2, 0, 600, 60})->Iterations(1)->UseManualTime()->Unit(benchmark::kMillisecond);
BENCHMARK(BM_SeenSimplicityFuzz)->Arg(400)->Iterations(1)->UseManualTime()->Unit(benchmark::kMillisecond);
BENCHMARK(BM_SeenRewalk)->Arg(1)->Arg(10)->Iterations(1)->UseManualTime()->Unit(benchmark::kMillisecond);
BENCHMARK(BM_SeenCamp)->Arg(10)->Arg(50)->Iterations(1)->UseManualTime()->Unit(benchmark::kMillisecond);
BENCHMARK(BM_SeenUpkeep)->Iterations(1)->UseManualTime()->Unit(benchmark::kMicrosecond);
