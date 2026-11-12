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
// BM_SearchReach, BM_SearchCases and BM_SearchDeadEnds: what one search step costs, how far a capped
// search reaches, and how many steps real routes and real dead ends take (see "Choosing the search
// cap" below).

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
			gnav::PatchableMesh		mesh		  = makeMesh(true);
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

	double meanOf(const std::vector<double>& v) {
		double sum = 0.0;
		for (const double x : v) {
			sum += x;
		}
		return v.empty() ? 0.0 : sum / static_cast<double>(v.size());
	}

	// --- Choosing the search cap ----------------------------------------------------------------
	//
	// Four grounds, each 1,024 chunks (16 km a side) of mesh: "open" (squares, no water), "real" (the
	// Quick Start chunks round its landing, rivers and lakes; needs WORLDSIM_PLANET), "forest" (open
	// ground with the chunk round the start covered by forest at the game's density in a nav mesh,
	// its placed obstacles blocking) and "compound" (open ground with a
	// 5 x 5 block of 10 m rooms round the start, a door gap in every wall, and a belief that knows
	// every wall).

	enum class Ground { Open, Real, Forest, Compound };
	const char* groundName(Ground g) {
		switch (g) {
			case Ground::Open:
				return "open";
			case Ground::Real:
				return "real";
			case Ground::Forest:
				return "forest";
			case Ground::Compound:
				return "compound";
		}
		return "";
	}

	struct SearchGround {
		gnav::PatchableMesh				  mesh;
		glm::vec2						  centre{0.0F, 0.0F};
		std::unordered_set<std::uint64_t> knownWalls;
		std::unordered_set<std::uint64_t> knownOpenings;
		bool							  belief = false;

		explicit SearchGround(bool detail)
			: mesh(makeMesh(detail)) {}

		[[nodiscard]] gnav::BeliefFilter filter() const {
			gnav::BeliefFilter f;
			if (belief) {
				f.knownSegments = &knownWalls;
				f.knownOpenings = &knownOpenings;
			}
			return f;
		}
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

	// A 5 x 5 block of 10 m rooms centred on `centre`: every wall 0.2 m thick, with a 1.2 m gap in its
	// middle (outer walls included). Wall ids start at 1000.
	std::vector<gnav::NavInputPolygon> compoundAt(glm::vec2 centre, std::unordered_set<std::uint64_t>& ids) {
		std::vector<gnav::NavInputPolygon> walls;
		const glm::vec2					   lo = centre - glm::vec2{25.0F};
		std::int64_t					   id = 1000;
		const auto						   wall = [&](glm::vec2 a, glm::vec2 b) {
			   const bool	   alongX = a.y == b.y;
			   const glm::vec2 mid	  = (a + b) * 0.5F;
			   const glm::vec2 dir	  = alongX ? glm::vec2{1.0F, 0.0F} : glm::vec2{0.0F, 1.0F};
			   const glm::vec2 side	  = alongX ? glm::vec2{0.0F, 0.1F} : glm::vec2{0.1F, 0.0F};
			   for (const auto& [from, to] : {std::pair{a, mid - 0.6F * dir}, std::pair{mid + 0.6F * dir, b}}) {
				   walls.push_back(box(glm::min(from, to) - side, glm::max(from, to) + side, true, id));
				   ids.insert(static_cast<std::uint64_t>(id));
				   ++id;
			   }
		};
		for (int k = 0; k <= 5; ++k) {
			const float o = static_cast<float>(k) * 10.0F;
			for (int j = 0; j < 5; ++j) {
				const float s = static_cast<float>(j) * 10.0F;
				wall(lo + glm::vec2{s, o}, lo + glm::vec2{s + 10.0F, o});
				wall(lo + glm::vec2{o, s}, lo + glm::vec2{o, s + 10.0F});
			}
		}
		return walls;
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
		auto out	= std::make_unique<SearchGround>(g == Ground::Forest);
		out->centre = region->start;
		for (const ChunkRings& c : region->chunks) {
			out->mesh.applyPatch({.adds = c.rings});
		}
		if (g == Ground::Forest) {
			out->mesh.applyPatch({.adds = forestOver(world::worldToChunk({out->centre.x, out->centre.y}))});
		}
		if (g == Ground::Compound) {
			out->mesh.applyPatch({.adds = compoundAt(out->centre, out->knownWalls)});
			out->belief = true;
		}
		out->mesh.refreshWidths();
		out->mesh.rebuildForests();
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

	Timed timedSearch(const SearchGround& g, glm::vec2 from, glm::vec2 to, std::uint32_t cap) {
		const Clock::time_point t = Clock::now();
		Timed					out;
		out.result = gnav::pathThrough(g.mesh.view(), nav::toMm(from), nav::toMm(to), 300, g.filter(), {.maxExpansions = cap});
		out.us	   = std::chrono::duration<double, std::micro>(Clock::now() - t).count();
		return out;
	}

	// Starts round the ground's centre: within 2 km on open and real ground, inside the forest chunk's
	// middle 200 m, and in the compound's rooms.
	std::vector<glm::vec2> startsFor(Ground g, const SearchGround& ground, int count) {
		std::vector<glm::vec2> out;
		std::uint32_t		   seed = 97;
		const auto			   unit = [&seed] {
			  seed = seed * 1664525U + 1013904223U;
			  return static_cast<float>(seed >> 8) / static_cast<float>(1U << 24);
		};
		const float spread = g == Ground::Forest ? 200.0F : g == Ground::Compound ? 46.0F : 4000.0F;
		while (static_cast<int>(out.size()) < count) {
			glm::vec2 p = ground.centre + glm::vec2{(unit() - 0.5F) * spread, (unit() - 0.5F) * spread};
			if (g == Ground::Compound) {
				p = ground.centre + glm::vec2{std::floor((p.x - ground.centre.x) / 10.0F) * 10.0F + 5.0F, std::floor((p.y - ground.centre.y) / 10.0F) * 10.0F + 5.0F};
			}
			const std::optional<glm::vec2> land = landNear(ground.mesh, p);
			if (land.has_value() && (g != Ground::Forest || ground.mesh.locate(nav::toMm(*land)) >= 0)) {
				out.push_back(*land);
			}
		}
		return out;
	}

	double median(std::vector<double> v) {
		if (v.empty()) {
			return 0.0;
		}
		std::sort(v.begin(), v.end());
		return v[v.size() / 2];
	}

	// Cost of a step, and how far N steps reach. range(0): ground; range(1): N. From each start, in 8
	// directions, the farthest goal (to 5 m) a search capped at N still finds, by doubling then halving
	// the distance. Per start: reach = the nearest of the 8 limits (the nearest goal not found),
	// farthest = the farthest found. Every search that stopped at its cap gives a microseconds-per-
	// expansion sample.
	void BM_SearchReach(benchmark::State& state) {
		const auto				  g		 = static_cast<Ground>(state.range(0));
		const auto				  n		 = static_cast<std::uint32_t>(state.range(1));
		const SearchGround* const ground = searchGround(g);
		if (ground == nullptr) {
			state.SkipWithMessage("no planet (WORLDSIM_PLANET)");
			return;
		}
		const int	maxStarts = g == Ground::Open || g == Ground::Real ? 32 : 16;
		const float maxD	  = g == Ground::Compound ? 400.0F : 16000.0F;
		for (auto _ : state) {
			std::vector<double> reach;
			std::vector<double> farthest;
			std::vector<double> usPerStep;
			for (const glm::vec2 start : startsFor(g, *ground, maxStarts)) {
				double nearestMiss = maxD;
				double farHit	   = 0.0;
				for (int k = 0; k < 8; ++k) {
					const glm::vec2 dir{std::cos(0.785398F * static_cast<float>(k) + 0.3F), std::sin(0.785398F * static_cast<float>(k) + 0.3F)};
					const auto		finds = [&](float d) -> std::optional<bool> {
						 const std::optional<glm::vec2> goal = landNear(ground->mesh, start + d * dir);
						 if (!goal.has_value()) {
							 return std::nullopt;
						 }
						 const Timed t = timedSearch(*ground, start, *goal, n);
						 if (t.result.stop == gnav::PathStop::LimitReached && t.result.expansions > 100) {
							 usPerStep.push_back(t.us / static_cast<double>(t.result.expansions));
						 }
						 return t.result.stop == gnav::PathStop::Found;
					};
					float hit  = 0.0F;
					float miss = 5.0F;
					while (miss < maxD && finds(miss).value_or(true)) {
						hit	 = miss;
						miss = miss * 2.0F;
					}
					miss = std::min(miss, maxD);
					while (miss - hit > 5.0F) {
						const float mid = 0.5F * (hit + miss);
						(finds(mid).value_or(true) ? hit : miss) = mid;
					}
					nearestMiss = std::min(nearestMiss, static_cast<double>(miss));
					farHit		= std::max(farHit, static_cast<double>(hit));
				}
				reach.push_back(nearestMiss);
				farthest.push_back(farHit);
			}
			state.SetLabel(groundName(g));
			state.counters["steps"]			   = n;
			state.counters["starts"]		   = static_cast<double>(reach.size());
			state.counters["reachMedianM"]	   = median(reach);
			state.counters["reachWorstM"]	   = *std::min_element(reach.begin(), reach.end());
			state.counters["farthestMedianM"]  = median(farthest);
			state.counters["farthestBestM"]	   = *std::max_element(farthest.begin(), farthest.end());
			state.counters["usPerStepMean"]	   = meanOf(usPerStep);
			state.counters["usPerStepWorst"]   = usPerStep.empty() ? 0.0 : *std::max_element(usPerStep.begin(), usPerStep.end());
			state.counters["usPerStepSamples"] = static_cast<double>(usPerStep.size());
		}
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
					const Timed t = timedSearch(*ground, start, *goal, 0);
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

	// Real dead ends on open ground: a goal on an islet (water all round), a goal in a sealed room
	// whose walls the belief knows, asked from the goal side and from the start side, uncapped: the
	// expansions each takes before it can say no, and the time.
	void BM_SearchDeadEnds(benchmark::State& state) {
		const Region* region = openRegion();
		for (auto _ : state) {
			gnav::PatchableMesh mesh = makeMesh(false);
			for (const ChunkRings& c : region->chunks) {
				mesh.applyPatch({.adds = c.rings});
			}
			const glm::vec2					   start = region->start;
			const glm::vec2					   islet = start + glm::vec2{300.0F, 0.0F};
			const glm::vec2					   room	 = start + glm::vec2{-300.0F, 0.0F};
			std::vector<gnav::NavInputPolygon> adds	 = ringRound(islet - glm::vec2{10.0F}, islet + glm::vec2{10.0F}, 20.0F, nav::kProvenanceWater, 0);
			const auto walls = ringRound(room - glm::vec2{5.0F}, room + glm::vec2{5.0F}, 0.2F, 1, 1);
			adds.insert(adds.end(), walls.begin(), walls.end());
			mesh.applyPatch({.adds = adds});
			mesh.refreshWidths();
			mesh.rebuildForests();
			const std::unordered_set<std::uint64_t> known{1, 2, 3, 4};
			const std::unordered_set<std::uint64_t> noOpenings;
			const gnav::BeliefFilter				belief{&known, &noOpenings};
			const auto								timed = [&](glm::vec2 a, glm::vec2 b, const gnav::BeliefFilter& f, const char* name) {
				 const Clock::time_point t = Clock::now();
				 const gnav::PathResult	 r = gnav::pathThrough(mesh.view(), nav::toMm(a), nav::toMm(b), 300, f, {});
				 state.counters[std::string(name) + "Steps"] = static_cast<double>(r.expansions);
				 state.counters[std::string(name) + "Ms"]	 = msSince(t);
				 state.counters[std::string(name) + "Stop"] = static_cast<double>(r.stop);
			};
			timed(start, islet, belief, "islet");
			timed(room, start, belief, "roomFromGoal");
			timed(start, room, belief, "roomFromStart");
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
BENCHMARK(BM_SearchReach)
	->ArgsProduct({{0, 1, 2, 3}, {2000, 5000, 20000}})
	->Iterations(1)
	->Unit(benchmark::kMillisecond);
BENCHMARK(BM_SearchCases)
	->ArgsProduct({{0, 1, 2}, {100, 500, 2000, 10000}})
	->Iterations(1)
	->Unit(benchmark::kMillisecond);
BENCHMARK(BM_SearchDeadEnds)->Iterations(1)->Unit(benchmark::kMillisecond);
