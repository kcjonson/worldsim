// computeVisibilityPolygon's cost in a base (player-visibility.md 6.8): the eight rooms of the
// geometry bench's base (SeenRecord.bench.cpp), 3,000 viewpoints in and round it, on open floor,
// a few millimetres off wall corners and in doorways, each built at vision's radius, 30 m sight plus
// its 0.5 m rebuild step, against every wall face of the building. A hash of every ring is printed so
// two builds of the construction can be checked for the same output.

#include "visibility/Buildings.test.h"
#include "visibility/Visibility.h"

#include <benchmark/benchmark.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

namespace {

	using geometry::Vec2i64;

	constexpr std::int64_t kVisionRadiusMm = 30500;
	constexpr double	   kPi			   = 3.14159265358979323846;

	geometry::buildings::Building eightRooms() {
		std::mt19937 rng(31);
		return geometry::buildings::makeBuilding({5000, 8000, 4000, 6500}, {6000, 4500}, 20.0 * kPi / 180.0, {37123456, -18456789}, 0.4, rng);
	}

	std::uint64_t hashOf(std::uint64_t hash, const geometry::Ring& ring) {
		hash = (hash ^ static_cast<std::uint64_t>(ring.size())) * 0x100000001B3ULL;
		for (const Vec2i64& vertex : ring) {
			hash = (hash ^ static_cast<std::uint64_t>(vertex.x)) * 0x100000001B3ULL;
			hash = (hash ^ static_cast<std::uint64_t>(vertex.y)) * 0x100000001B3ULL;
		}
		return hash;
	}

	void BM_VisibilityPolygonBase(benchmark::State& state) {
		const geometry::buildings::Building building = eightRooms();
		std::mt19937						rng(7);
		const std::vector<Vec2i64>			viewpoints = geometry::buildings::observersFor(building, rng, 3000);
		for (auto _ : state) {
			std::vector<double> buildUs;
			buildUs.reserve(viewpoints.size());
			std::uint64_t hash	   = 0xCBF29CE484222325ULL;
			double		  vertices = 0.0;
			for (const Vec2i64& viewpoint : viewpoints) {
				const auto			 start = std::chrono::steady_clock::now();
				const geometry::Ring ring  = geometry::computeVisibilityPolygon(viewpoint, kVisionRadiusMm, building.walls);
				buildUs.push_back(std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count());
				hash = hashOf(hash, ring);
				vertices += static_cast<double>(ring.size());
			}
			std::vector<double> sorted = buildUs;
			std::sort(sorted.begin(), sorted.end());
			double sum = 0.0;
			for (const double value : buildUs) {
				sum += value;
			}
			const double mean = sum / static_cast<double>(buildUs.size());
			const double p99  = sorted[static_cast<std::size_t>(0.99 * static_cast<double>(sorted.size() - 1))];
			std::fprintf(stderr, "[visibility base] %zu viewpoints, %zu wall faces: build us mean %.1f p99 %.1f max %.1f; ring vertices mean %.0f; hash %llu\n",
						 viewpoints.size(), building.walls.size(), mean, p99, sorted.back(), vertices / static_cast<double>(viewpoints.size()),
						 static_cast<unsigned long long>(hash));
			state.SetIterationTime(sum / 1e6);
			state.counters["buildUs"]	 = mean;
			state.counters["buildP99Us"] = p99;
			state.counters["buildMaxUs"] = sorted.back();
		}
	}

} // namespace

BENCHMARK(BM_VisibilityPolygonBase)->Iterations(1)->UseManualTime()->Unit(benchmark::kMillisecond);
