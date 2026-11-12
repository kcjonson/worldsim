// What a mesh that never drops a mover's ground costs as it grows (nav-mesh-variable-resolution.md
// section 10): meshes built directly from chunk rings, the way NavigationSystem patches them in,
// at 25, 121, 500 and 1,000 chunks of ground. "real" is the Quick Start planet's chunks round its
// landing spawn (WORLDSIM_PLANET names the .wsplanet; skipped without it), "open" is the same count
// of chunks with no water. Memory is the process's committed private bytes before and after a build,
// so it is coarse below a few hundred KB; per chunk it is read off the larger builds.
//
// BM_NavScaleGround: per-chunk bytes of the nav mesh (ground only), the whole build's patch and
// width-refresh time, then on the built mesh: a wall edit's job (its patch, its width refresh) and
// one more chunk's ground arriving (the same split).
//
// BM_NavScaleDetail: one chunk fully covered by forest at the game's density (a tree every 3.54 m,
// colliders by age class), patched into a nav mesh 16 m cell by cell as the view brings it: rings,
// bytes and time.
//
// BM_SearchCases: how many steps real routes take (see "Choosing the search cap" below).

#include "NavigationSystem.h"

#include <nav/NavCoords.h>
#include <nav/NavInputBuilder.h>
#include <nav/PatchableMesh.h>
#include <nav/PathQuery.h>

#include <world/chunk/Chunk.h>
#include <world/chunk/ChunkManager.h>
#include <world/chunk/GeneratedWorldSampler.h>
#include <worldgen/io/PlanetIO.h>
#include <worldgen/sampling/LandingSite.h>
#include <worldgen/sampling/SpawnSite.h>

#include <benchmark/benchmark.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <utility>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#endif

namespace {
	namespace gnav = geometry::nav;
	using Clock	   = std::chrono::steady_clock;

	double msSince(Clock::time_point start) { return std::chrono::duration<double, std::milli>(Clock::now() - start).count(); }

	std::int64_t privateBytes() {
#ifdef _WIN32
		PROCESS_MEMORY_COUNTERS_EX counters{};
		GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters));
		return static_cast<std::int64_t>(counters.PrivateUsage);
#else
		std::ifstream statm("/proc/self/statm");
		long		  size = 0;
		long		  resident = 0;
		statm >> size >> resident;
		return static_cast<std::int64_t>(resident) * 4096;
#endif
	}

	struct ChunkRings {
		world::ChunkCoordinate			   coord;
		std::vector<gnav::NavInputPolygon> rings; // the square, then its water
		std::size_t						   waterVertices = 0;
	};

	// The chunks of a 32 x 32 block, nearest the middle first.
	struct Region {
		std::vector<ChunkRings> chunks;
		glm::vec2				start{0.0F, 0.0F}; // land near the middle
	};

	std::vector<world::ChunkCoordinate> spiral(world::ChunkCoordinate centre, int side) {
		std::vector<world::ChunkCoordinate> out;
		for (int y = -side / 2; y < side - side / 2; ++y) {
			for (int x = -side / 2; x < side - side / 2; ++x) {
				out.push_back({centre.x + x, centre.y + y});
			}
		}
		std::stable_sort(out.begin(), out.end(), [centre](world::ChunkCoordinate a, world::ChunkCoordinate b) {
			return std::max(std::abs(a.x - centre.x), std::abs(a.y - centre.y)) < std::max(std::abs(b.x - centre.x), std::abs(b.y - centre.y));
		});
		return out;
	}

	const Region* realRegion() {
		static const std::optional<Region> region = []() -> std::optional<Region> {
			const char*					planetEnv  = std::getenv("WORLDSIM_PLANET");
			const std::filesystem::path planetPath = planetEnv != nullptr ? std::filesystem::path(planetEnv) : std::filesystem::path("planets/quickstart.wsplanet");
			if (!std::filesystem::exists(planetPath)) {
				return std::nullopt;
			}
			const std::shared_ptr<const worldgen::GeneratedWorld> planet = worldgen::loadPlanet(planetPath);
			if (!planet) {
				return std::nullopt;
			}
			const worldgen::LatLon	  site	= worldgen::findDefaultLandingSite(*planet, worldgen::Biome::TemperateDeciduousForest);
			const worldgen::SpawnSite spawn = worldgen::findRiverbankSpawn(planet, site.latDeg, site.lonDeg);
			Region					  out;
			out.start = {static_cast<float>(spawn.xMeters), static_cast<float>(spawn.yMeters)};
			world::ChunkManager chunks(std::make_unique<world::GeneratedWorldSampler>(planet, site.latDeg, site.lonDeg));
			chunks.setLoadRadius(0);
			chunks.setUnloadRadius(0);
			for (const world::ChunkCoordinate coord : spiral(world::worldToChunk({out.start.x, out.start.y}), 32)) {
				const glm::vec2 centre{(static_cast<float>(coord.x) + 0.5F) * 512.0F, (static_cast<float>(coord.y) + 0.5F) * 512.0F};
				chunks.update({centre.x, centre.y});
				chunks.finishPendingGeneration();
				const world::Chunk* chunk = chunks.getChunk(coord);
				if (chunk == nullptr) {
					continue;
				}
				ChunkRings rings{coord, {nav::chunkSquare(coord)}, 0};
				nav::appendChunkWater(chunk->terrainPolygons(), rings.rings);
				for (std::size_t k = 1; k < rings.rings.size(); ++k) {
					rings.waterVertices += rings.rings[k].ring.size();
				}
				out.chunks.push_back(std::move(rings));
			}
			return out;
		}();
		return region ? &*region : nullptr;
	}

	const Region* openRegion() {
		static const Region region = [] {
			Region out;
			out.start = {256.0F, 256.0F};
			for (const world::ChunkCoordinate coord : spiral({0, 0}, 32)) {
				out.chunks.push_back({coord, {nav::chunkSquare(coord)}, 0});
			}
			return out;
		}();
		return &region;
	}

	gnav::PatchableMesh makeMesh() {
		return gnav::PatchableMesh({.maxBoundsEdgeMm = ecs::NavigationSystem::kMaxBoundsEdgeMm});
	}

	gnav::NavInputPolygon box(glm::vec2 lo, glm::vec2 hi, bool blocked, std::int64_t provenance) {
		return {{nav::toMm(lo), nav::toMm({hi.x, lo.y}), nav::toMm(hi), nav::toMm({lo.x, hi.y})}, blocked, provenance};
	}

	// A land point near `p` (the mesh walkable there in truth).
	std::optional<glm::vec2> landNear(const gnav::PatchableMesh& mesh, glm::vec2 p) {
		for (int r = 0; r < 60; ++r) {
			for (int k = 0; k < 8; ++k) {
				const glm::vec2	   q   = p + static_cast<float>(r) * 2.0F * glm::vec2{std::cos(0.785398F * k), std::sin(0.785398F * k)};
				const std::int32_t tri = mesh.locate(nav::toMm(q));
				if (tri >= 0 && gnav::truthTraversable(mesh.triangles()[static_cast<std::size_t>(tri)])) {
					return q;
				}
			}
		}
		return std::nullopt;
	}

	// range(0): chunks (25, 121, 500, 1000); range(1): 0 the real mix, 1 open ground.
	void BM_NavScaleGround(benchmark::State& state) {
		const std::size_t n		 = static_cast<std::size_t>(state.range(0));
		const Region*	  region = state.range(1) == 0 ? realRegion() : openRegion();
		if (region == nullptr) {
			state.SkipWithMessage("no planet (WORLDSIM_PLANET)");
			return;
		}
		if (region->chunks.size() < n + 1) {
			state.SkipWithMessage("not enough chunks");
			return;
		}
		for (auto _ : state) {
			std::size_t				waterVertices = 0;
			const std::int64_t		before		  = privateBytes();
			gnav::PatchableMesh		mesh		  = makeMesh();
			const Clock::time_point patchStart	  = Clock::now();
			for (std::size_t c = 0; c < n; ++c) {
				mesh.applyPatch({.adds = region->chunks[c].rings});
				waterVertices += region->chunks[c].waterVertices;
			}
			const double			patchMs		= msSince(patchStart);
			const Clock::time_point widthsStart = Clock::now();
			mesh.refreshWidths();
			const double	   widthsMs = msSince(widthsStart);
			const std::int64_t navBytes = privateBytes() - before;

			const glm::vec2 start = *landNear(mesh, region->start);

			// A wall edit's job.
			const Clock::time_point wallStart = Clock::now();
			mesh.applyPatch({.adds = {box(start + glm::vec2{10.0F, 10.0F}, start + glm::vec2{18.0F, 10.2F}, true, 99)}});
			const double			wallPatchMs = msSince(wallStart);
			const Clock::time_point wallWidths	= Clock::now();
			mesh.refreshWidths();
			const double wallWidthsMs = msSince(wallWidths);

			// One more chunk's ground arriving.
			const Clock::time_point groundStart = Clock::now();
			mesh.applyPatch({.adds = region->chunks[n].rings});
			const double			groundPatchMs = msSince(groundStart);
			const Clock::time_point groundWidths  = Clock::now();
			mesh.refreshWidths();
			const double groundWidthsMs = msSince(groundWidths);

			state.counters["chunks"]			 = static_cast<double>(n);
			state.counters["waterVertsPerChunk"] = static_cast<double>(waterVertices) / static_cast<double>(n);
			state.counters["navKBPerChunk"]		 = static_cast<double>(navBytes) / 1024.0 / static_cast<double>(n);
			state.counters["slots"]				 = static_cast<double>(mesh.triangles().size());
			state.counters["buildPatchMs"]		 = patchMs;
			state.counters["buildWidthsMs"]		 = widthsMs;
			state.counters["wallPatchMs"]		 = wallPatchMs;
			state.counters["wallWidthsMs"]		 = wallWidthsMs;
			state.counters["groundPatchMs"]		 = groundPatchMs;
			state.counters["groundWidthsMs"]	 = groundWidthsMs;
		}
	}

	// One chunk fully covered by forest at the game's density, patched into a nav mesh cell by cell.
	void BM_NavScaleDetail(benchmark::State& state) {
		for (auto _ : state) {
			gnav::PatchableMesh nav = makeMesh();
			nav.applyPatch({.adds = {nav::chunkSquare({0, 0})}});
			nav.refreshWidths();
			const float					   halfExtents[] = {0.125F + 0.05F, 0.225F + 0.05F, 0.36F + 0.05F};
			std::map<std::int64_t, gnav::PatchEdits> cells;
			std::size_t					   rings = 0;
			for (float x = 1.0F; x < 511.0F; x += 3.54F) {
				for (float y = 1.0F; y < 511.0F; y += 3.54F) {
					const glm::vec2		p{x + std::fmod(y * 0.37F, 1.4F), y + std::fmod(x * 0.53F, 1.4F)};
					const std::uint32_t h	  = static_cast<std::uint32_t>(p.x * 131.0F + p.y * 7.0F) % 100U;
					const float			e	  = halfExtents[h < 15 ? 0 : h < 75 ? 1 : 2];
					const std::int64_t	cell = static_cast<std::int64_t>(p.x / 16.0F) * 64 + static_cast<std::int64_t>(p.y / 16.0F);
					cells[cell].adds.push_back(box(p - glm::vec2{e}, p + glm::vec2{e}, true, nav::kProvenancePlaced));
					++rings;
				}
			}
			const std::int64_t		before = privateBytes();
			const Clock::time_point start  = Clock::now();
			double					worstCellMs = 0.0;
			for (const auto& [cell, edits] : cells) {
				const Clock::time_point t = Clock::now();
				nav.applyPatch(edits);
				worstCellMs = std::max(worstCellMs, msSince(t));
			}
			nav.refreshWidths();
			state.counters["rings"]			  = static_cast<double>(rings);
			state.counters["cells"]			  = static_cast<double>(cells.size());
			state.counters["patchMs"]		  = msSince(start);
			state.counters["worstCellMs"]	  = worstCellMs;
			state.counters["detailKBPerChunk"] = static_cast<double>(privateBytes() - before) / 1024.0;
			state.counters["bytesPerRing"]	  = static_cast<double>(privateBytes() - before) / static_cast<double>(rings);
			state.counters["slots"]			  = static_cast<double>(nav.triangles().size());
		}
	}

	double meanOf(const std::vector<double>& v) {
		double sum = 0.0;
		for (const double x : v) {
			sum += x;
		}
		return v.empty() ? 0.0 : sum / static_cast<double>(v.size());
	}

	// --- Choosing the search cap ----------------------------------------------------------------
	//
	// Three grounds, each 1,024 chunks (16 km a side) of mesh: "open" (squares, no water), "real" (the
	// Quick Start chunks round its landing, rivers and lakes; needs WORLDSIM_PLANET) and "forest" (open
	// ground with the chunk round the start covered by forest at the game's density in a nav mesh,
	// its placed obstacles blocking).

	enum class Ground { Open, Real, Forest };
	const char* groundName(Ground g) {
		switch (g) {
			case Ground::Open:
				return "open";
			case Ground::Real:
				return "real";
			case Ground::Forest:
				return "forest";
		}
		return "";
	}

	struct SearchGround {
		gnav::PatchableMesh mesh = makeMesh();
		glm::vec2			centre{0.0F, 0.0F};
	};

	// The forest of BM_NavScaleDetail over the chunk holding `centre`.
	std::vector<gnav::NavInputPolygon> forestOver(world::ChunkCoordinate chunk) {
		const float						   halfExtents[] = {0.125F + 0.05F, 0.225F + 0.05F, 0.36F + 0.05F};
		const glm::vec2					   origin{static_cast<float>(chunk.x) * 512.0F, static_cast<float>(chunk.y) * 512.0F};
		std::vector<gnav::NavInputPolygon> trees;
		for (float x = 1.0F; x < 511.0F; x += 3.54F) {
			for (float y = 1.0F; y < 511.0F; y += 3.54F) {
				const glm::vec2		p = origin + glm::vec2{x + std::fmod(y * 0.37F, 1.4F), y + std::fmod(x * 0.53F, 1.4F)};
				const std::uint32_t h = static_cast<std::uint32_t>(x * 131.0F + y * 7.0F) % 100U;
				const float			e = halfExtents[h < 15 ? 0 : h < 75 ? 1 : 2];
				trees.push_back(box(p - glm::vec2{e}, p + glm::vec2{e}, true, nav::kProvenancePlaced));
			}
		}
		return trees;
	}

	const SearchGround* searchGround(Ground g) {
		static std::map<Ground, std::unique_ptr<SearchGround>> grounds;
		auto&												   slot = grounds[g];
		if (slot) {
			return slot.get();
		}
		const Region* region = g == Ground::Real ? realRegion() : openRegion();
		if (region == nullptr) {
			return nullptr;
		}
		auto out	= std::make_unique<SearchGround>();
		out->centre = region->start;
		for (const ChunkRings& c : region->chunks) {
			out->mesh.applyPatch({.adds = c.rings});
		}
		if (g == Ground::Forest) {
			out->mesh.applyPatch({.adds = forestOver(world::worldToChunk({out->centre.x, out->centre.y}))});
		}
		out->mesh.refreshWidths();
		if (g == Ground::Real) {
			out->centre = *landNear(out->mesh, out->centre);
		}
		slot = std::move(out);
		return slot.get();
	}

	struct Timed {
		gnav::PathResult result;
		double			 us = 0.0;
	};

	Timed timedSearch(const SearchGround& g, glm::vec2 from, glm::vec2 to) {
		const Clock::time_point t = Clock::now();
		Timed					out;
		out.result = gnav::pathThrough(g.mesh.view(), nav::toMm(from), nav::toMm(to), 300);
		out.us	   = std::chrono::duration<double, std::micro>(Clock::now() - t).count();
		return out;
	}

	// Starts round the ground's centre: within 2 km on open and real ground, inside the forest chunk's
	// middle 200 m.
	std::vector<glm::vec2> startsFor(Ground g, const SearchGround& ground, int count) {
		std::vector<glm::vec2> out;
		std::uint32_t		   seed = 97;
		const auto			   unit = [&seed] {
			  seed = seed * 1664525U + 1013904223U;
			  return static_cast<float>(seed >> 8) / static_cast<float>(1U << 24);
		};
		const float spread = g == Ground::Forest ? 200.0F : 4000.0F;
		while (static_cast<int>(out.size()) < count) {
			const glm::vec2 p = ground.centre + glm::vec2{(unit() - 0.5F) * spread, (unit() - 0.5F) * spread};
			const std::optional<glm::vec2> land = landNear(ground.mesh, p);
			if (land.has_value() && (g != Ground::Forest || ground.mesh.locate(nav::toMm(*land)) >= 0)) {
				out.push_back(*land);
			}
		}
		return out;
	}

	// Steps real cases need. range(0): ground; range(1): straight-line distance (m). From 8 starts, 16
	// directions each, uncapped: the expansions and time of each search that found its goal, mean and
	// worst, and how many found none.
	void BM_SearchCases(benchmark::State& state) {
		const auto				  g		 = static_cast<Ground>(state.range(0));
		const auto				  d		 = static_cast<float>(state.range(1));
		const SearchGround* const ground = searchGround(g);
		if (ground == nullptr) {
			state.SkipWithMessage("no planet (WORLDSIM_PLANET)");
			return;
		}
		for (auto _ : state) {
			std::vector<double> steps;
			std::vector<double> ms;
			int					none = 0;
			for (const glm::vec2 start : startsFor(g, *ground, 8)) {
				for (int k = 0; k < 16; ++k) {
					const float					   a	= 0.392699F * static_cast<float>(k) + 0.1F;
					const std::optional<glm::vec2> goal = landNear(ground->mesh, start + d * glm::vec2{std::cos(a), std::sin(a)});
					if (!goal.has_value()) {
						continue;
					}
					const Timed t = timedSearch(*ground, start, *goal);
					if (t.result.stop != gnav::PathStop::Found) {
						++none;
						continue;
					}
					steps.push_back(static_cast<double>(t.result.expansions));
					ms.push_back(t.us / 1000.0);
				}
			}
			state.SetLabel(groundName(g));
			state.counters["distanceM"]	 = d;
			state.counters["found"]		 = static_cast<double>(steps.size());
			state.counters["noPath"]	 = none;
			state.counters["stepsMean"]	 = meanOf(steps);
			state.counters["stepsWorst"] = steps.empty() ? 0.0 : *std::max_element(steps.begin(), steps.end());
			state.counters["msMean"]	 = meanOf(ms);
			state.counters["msWorst"]	 = ms.empty() ? 0.0 : *std::max_element(ms.begin(), ms.end());
		}
	}

} // namespace

BENCHMARK(BM_NavScaleGround)
	->Args({25, 1})
	->Args({121, 1})
	->Args({500, 1})
	->Args({1000, 1})
	->Args({25, 0})
	->Args({121, 0})
	->Args({500, 0})
	->Args({1000, 0})
	->Iterations(1)
	->Unit(benchmark::kMillisecond);
BENCHMARK(BM_NavScaleDetail)->Iterations(1)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_SearchCases)
	->ArgsProduct({{0, 1, 2}, {100, 500, 2000, 10000}})
	->Iterations(1)
	->Unit(benchmark::kMillisecond);
