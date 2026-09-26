#pragma once

// A small generated world with one carved river, for tests and benchmarks that
// need real RiverNetwork2D geometry (trunk sub-segments, headwater and
// along-channel feeders) through GeneratedWorldSampler. SemiDesert everywhere:
// DesertGenerator never produces water, so any water comes from the river.

#include <worldgen/data/GeneratedWorld.h>
#include <worldgen/data/PlanetParams.h>
#include <worldgen/data/WorldData.h>
#include <worldgen/grid/SphereGrid.h>

#include <array>
#include <cstdint>
#include <memory>
#include <unordered_set>

namespace engine::world::river_test {

inline constexpr uint32_t kSubdivision = 24; // ~300 km tiles
inline constexpr float kLand = 500.0F;

inline std::shared_ptr<worldgen::GeneratedWorld> makeSemiDesertWorld() {
	using namespace worldgen;
	auto world = std::make_shared<GeneratedWorld>();
	world->params.gridSubdivision = kSubdivision;
	world->params.seed = 0xDEADBEEFULL;
	world->derived = derive(world->params);
	world->grid = std::make_shared<SphereGrid>(kSubdivision);
	world->data.allocate(world->grid->tileCount());
	world->seaLevelMeters = 0.0F;
	world->validFields = static_cast<uint32_t>(WorldField::Elevation) | static_cast<uint32_t>(WorldField::Biome) |
						 static_cast<uint32_t>(WorldField::Flags) | static_cast<uint32_t>(WorldField::FlowAccum) |
						 static_cast<uint32_t>(WorldField::Downhill);
	for (TileId t = 0; t < world->grid->tileCount(); ++t) {
		world->data.elevation[t] = kLand;
		world->data.biome[t] = static_cast<uint8_t>(Biome::SemiDesert);
	}
	return world;
}

// Flag a river chain from `source` toward `mouth` (flow 80, widening by 20 per
// tile), returning the source tile.
inline worldgen::TileId carveRiver(worldgen::GeneratedWorld& world, worldgen::TileId source, worldgen::TileId mouth) {
	using namespace worldgen;
	auto dot = [](const Vec3d& a, const Vec3d& b) { return a.x * b.x + a.y * b.y + a.z * b.z; };
	const SphereGrid& grid = *world.grid;
	const Vec3d target = grid.tileCenter(mouth);
	std::unordered_set<TileId> visited;
	TileId cur = source;
	float flow = 80.0F; // wide enough channel to clearly rasterize
	for (int guard = 0; guard < 1000; ++guard) {
		if (cur == mouth || !visited.insert(cur).second) {
			break;
		}
		std::array<TileId, 6> nbrs{};
		const uint32_t count = grid.neighbors(cur, nbrs);
		int bestIdx = -1;
		double bestDot = -2.0;
		for (uint32_t i = 0; i < count; ++i) {
			const double d = dot(grid.tileCenter(nbrs[i]), target);
			if (d > bestDot) {
				bestDot = d;
				bestIdx = static_cast<int>(i);
			}
		}
		if (bestIdx < 0) {
			break;
		}
		world.data.flags[cur] |= kFlagRiver;
		world.data.flowAccum[cur] = flow;
		world.data.downhill[cur] = static_cast<uint8_t>(bestIdx);
		flow += 20.0F;
		cur = nbrs[static_cast<uint32_t>(bestIdx)];
	}
	return source;
}

// The world above with its river carved from (0, 0) toward (0, 40) lat/lon,
// plus the landing lat/lon that puts the river source at world (0, 0).
struct CarvedRiverWorld {
	std::shared_ptr<worldgen::GeneratedWorld> world;
	double landingLat = 0.0;
	double landingLon = 0.0;
};

inline CarvedRiverWorld makeCarvedRiverWorld() {
	CarvedRiverWorld out;
	out.world = makeSemiDesertWorld();
	const worldgen::TileId source = out.world->grid->fromLatLon(0.0, 0.0);
	const worldgen::TileId mouth = out.world->grid->fromLatLon(0.0, 40.0);
	carveRiver(*out.world, source, mouth);
	out.world->grid->latLonOf(source, out.landingLat, out.landingLon);
	return out;
}

} // namespace engine::world::river_test
