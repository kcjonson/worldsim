// What a mesh that never drops a mover's ground costs as it grows (nav-mesh-variable-resolution.md
// section 10): meshes built directly from chunk rings, the way NavigationSystem patches them in,
// at 25, 121, 500 and 1,000 chunks of ground. "real" is the Quick Start planet's chunks round its
// landing spawn (WORLDSIM_PLANET names the .wsplanet; skipped without it), "open" is the same count
// of chunks with no water. Memory is the process's committed private bytes before and after a build,
// so it is coarse below a few hundred KB; per chunk it is read off the larger builds.
//
// BM_NavScaleGround: per-chunk bytes of the structural mesh and of the nav mesh (ground only), the
// whole build's patch and forest-rebuild time, then on the built structural mesh: a wall edit's job
// (its patch, its forest rebuild) and one more chunk's ground arriving (the same split); a failing
// walk the forest rejects (a goal ringed by water) and one it can't (a goal behind walls the belief
// knows), capped at NavigationSystem::kReachExpansionCap and uncapped as requestPath's pre-check
// runs; and the farthest straight-line distance a capped walk still finds east of the start.
//
// BM_NavScaleDetail: one chunk fully covered by forest at the game's density (a tree every 3.54 m,
// colliders by age class), patched into a nav mesh 16 m cell by cell as the view brings it: rings,
// bytes and time.

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
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
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

	gnav::PatchableMesh makeMesh(bool detail) {
		gnav::PatchableMeshArgs args;
		args.maxBoundsEdgeMm = ecs::NavigationSystem::kMaxBoundsEdgeMm;
		if (detail) {
			args.detailTags = {nav::kProvenancePlaced};
		}
		return gnav::PatchableMesh(args);
	}

	gnav::NavInputPolygon box(glm::vec2 lo, glm::vec2 hi, bool blocked, std::int64_t provenance) {
		return {{nav::toMm(lo), nav::toMm({hi.x, lo.y}), nav::toMm(hi), nav::toMm({lo.x, hi.y})}, blocked, provenance};
	}

	// Four thin rings round [lo, hi]: walls (provenance = segment ids from firstId) or water.
	std::vector<gnav::NavInputPolygon> ringRound(glm::vec2 lo, glm::vec2 hi, float thick, std::int64_t provenance, std::int64_t firstId) {
		auto id = [&](int k) { return provenance > 0 ? firstId + k : provenance; };
		return {box({lo.x - thick, lo.y - thick}, {hi.x + thick, lo.y}, true, id(0)), box({lo.x - thick, hi.y}, {hi.x + thick, hi.y + thick}, true, id(1)),
				box({lo.x - thick, lo.y}, {lo.x, hi.y}, true, id(2)), box({hi.x, lo.y}, {hi.x + thick, hi.y}, true, id(3))};
	}

	// A land point near `p` (the structural mesh walkable there in truth).
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
			std::size_t waterVertices = 0;
			// The nav mesh's ground first, measured and let go, then the structural mesh, kept.
			std::int64_t navBytes = 0;
			{
				const std::int64_t	before = privateBytes();
				gnav::PatchableMesh nav	   = makeMesh(true);
				for (std::size_t c = 0; c < n; ++c) {
					nav.applyPatch({.adds = region->chunks[c].rings});
				}
				nav.refreshWidths();
				navBytes = privateBytes() - before;
			}
			const std::int64_t		before	  = privateBytes();
			gnav::PatchableMesh		structural = makeMesh(false);
			const Clock::time_point patchStart = Clock::now();
			for (std::size_t c = 0; c < n; ++c) {
				structural.applyPatch({.adds = region->chunks[c].rings});
				waterVertices += region->chunks[c].waterVertices;
			}
			const double			patchMs		= msSince(patchStart);
			const Clock::time_point forestStart = Clock::now();
			structural.rebuildForests();
			const double	   forestMs		   = msSince(forestStart);
			const std::int64_t structuralBytes = privateBytes() - before;

			const glm::vec2 start = *landNear(structural, region->start);
			// A goal ringed by water (the forest rejects it) and one behind walls the belief knows.
			const glm::vec2 moated = *landNear(structural, start + glm::vec2{40.0F, 0.0F});
			const glm::vec2 walled = *landNear(structural, start + glm::vec2{-40.0F, 0.0F});
			std::vector<gnav::NavInputPolygon> obstacles = ringRound(moated - glm::vec2{4.0F}, moated + glm::vec2{4.0F}, 3.0F, nav::kProvenanceWater, 0);
			const std::vector<gnav::NavInputPolygon> walls = ringRound(walled - glm::vec2{4.0F}, walled + glm::vec2{4.0F}, 0.2F, 1, 1);
			obstacles.insert(obstacles.end(), walls.begin(), walls.end());
			structural.applyPatch({.adds = obstacles});
			structural.rebuildForests();
			const std::unordered_set<std::uint64_t> known{1, 2, 3, 4};
			const std::unordered_set<std::uint64_t> noOpenings;
			const gnav::BeliefFilter				belief{&known, &noOpenings};
			const std::int64_t						radius = 300;

			// A wall edit's structural job.
			const Clock::time_point wallStart = Clock::now();
			const gnav::PatchReport wallPatch =
				structural.applyPatch({.adds = {box(start + glm::vec2{10.0F, 10.0F}, start + glm::vec2{18.0F, 10.2F}, true, 99)}});
			const double			wallPatchMs	 = msSince(wallStart);
			const Clock::time_point wallForest	 = Clock::now();
			structural.rebuildForests();
			const double wallForestMs = msSince(wallForest);
			(void)wallPatch;

			// One more chunk's ground arriving.
			const Clock::time_point groundStart = Clock::now();
			structural.applyPatch({.adds = region->chunks[n].rings});
			const double			groundPatchMs = msSince(groundStart);
			const Clock::time_point groundForest  = Clock::now();
			structural.rebuildForests();
			const double groundForestMs = msSince(groundForest);

			const auto timedWalk = [&](glm::vec2 goal, std::uint32_t cap, double& us) {
				const Clock::time_point t = Clock::now();
				const gnav::PathResult	r = gnav::pathThrough(structural.view(), nav::toMm(start), nav::toMm(goal), radius, belief, {.maxExpansions = cap});
				us						  = std::chrono::duration<double, std::micro>(Clock::now() - t).count();
				return r;
			};
			double				   moatUs		 = 0.0;
			const gnav::PathResult moat			 = timedWalk(moated, ecs::NavigationSystem::kReachExpansionCap, moatUs);
			double				   wallCappedUs	 = 0.0;
			const gnav::PathResult wallCapped	 = timedWalk(walled, ecs::NavigationSystem::kReachExpansionCap, wallCappedUs);
			double				   wallUncappedUs = 0.0;
			const gnav::PathResult wallUncapped	  = timedWalk(walled, 0, wallUncappedUs);

			// The farthest goal east (100 m steps) a capped walk still finds.
			float		  farthest	   = 0.0F;
			std::uint32_t farthestExps = 0;
			for (float d = 100.0F; d < 16000.0F; d += 100.0F) {
				const std::optional<glm::vec2> goal = landNear(structural, start + glm::vec2{d, 0.0F});
				if (!goal.has_value()) {
					continue;
				}
				double				   us = 0.0;
				const gnav::PathResult r  = timedWalk(*goal, ecs::NavigationSystem::kReachExpansionCap, us);
				if (r.stop == gnav::PathStop::LimitReached) {
					break;
				}
				if (r.stop == gnav::PathStop::Found) {
					farthest	 = glm::distance(start, *goal);
					farthestExps = r.expansions;
				}
			}

			state.counters["chunks"]				= static_cast<double>(n);
			state.counters["waterVertsPerChunk"]	= static_cast<double>(waterVertices) / static_cast<double>(n);
			state.counters["structKBPerChunk"]		= static_cast<double>(structuralBytes) / 1024.0 / static_cast<double>(n);
			state.counters["navKBPerChunk"]			= static_cast<double>(navBytes) / 1024.0 / static_cast<double>(n);
			state.counters["structSlots"]			= static_cast<double>(structural.triangles().size());
			state.counters["buildPatchMs"]			= patchMs;
			state.counters["buildForestMs"]			= forestMs;
			state.counters["wallPatchMs"]			= wallPatchMs;
			state.counters["wallForestMs"]			= wallForestMs;
			state.counters["groundPatchMs"]			= groundPatchMs;
			state.counters["groundForestMs"]		= groundForestMs;
			state.counters["moatWalkUs"]			= moatUs;
			state.counters["moatWalkExps"]			= static_cast<double>(moat.expansions);
			state.counters["moatWalkStop"]			= static_cast<double>(moat.stop);
			state.counters["walledWalkUs"]			= wallCappedUs;
			state.counters["walledWalkExps"]		= static_cast<double>(wallCapped.expansions);
			state.counters["walledWalkStop"]		= static_cast<double>(wallCapped.stop);
			state.counters["walledPrecheckUs"]		= wallUncappedUs;
			state.counters["walledPrecheckExps"]	= static_cast<double>(wallUncapped.expansions);
			state.counters["farthestCappedWalkM"]	= farthest;
			state.counters["farthestCappedWalkExps"] = static_cast<double>(farthestExps);
		}
	}

	// One chunk fully covered by forest at the game's density, patched into a nav mesh cell by cell.
	void BM_NavScaleDetail(benchmark::State& state) {
		for (auto _ : state) {
			gnav::PatchableMesh nav = makeMesh(true);
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
