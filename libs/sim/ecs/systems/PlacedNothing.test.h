#pragma once

#include <assets/AssetRegistry.h>
#include <assets/placement/PlacementExecutor.h>

#include <world/chunk/ChunkCoordinate.h>

#include <cstdint>
#include <unordered_set>

namespace ecs {

	// The chunks round the origin every test fixture's chunk region lies in.
	constexpr std::int32_t kFixtureChunkReach = 4;

	// Every fixture chunk with no placement yet is stored placed with nothing, and marked processed
	// when `processed` is given, as the game's placement does for a chunk it generates empty: a
	// visibility limit over it then need not wait for placement. `lo` and `hi` bound the chunks, for
	// a fixture whose region lies beyond the usual one.
	inline void placeNothingElsewhere(assets::PlacementExecutor& executor, std::unordered_set<world::ChunkCoordinate>* processed,
									  world::ChunkCoordinate lo = {-kFixtureChunkReach, -kFixtureChunkReach},
									  world::ChunkCoordinate hi = {kFixtureChunkReach, kFixtureChunkReach}) {
		for (std::int32_t cy = lo.y; cy <= hi.y; ++cy) {
			for (std::int32_t cx = lo.x; cx <= hi.x; ++cx) {
				if (executor.getChunkIndex({cx, cy}) != nullptr) {
					continue;
				}
				assets::AsyncChunkPlacementResult empty;
				empty.coord = {cx, cy};
				executor.storeChunkResult(std::move(empty));
				if (processed != nullptr) {
					processed->insert({cx, cy});
				}
			}
		}
	}

	// Placement data holding nothing anywhere in the fixture's chunk region, for a test that sets a
	// chunk manager on nav and has no placed obstacles of its own.
	struct EmptyPlacement {
		assets::PlacementExecutor					executor{assets::AssetRegistry::Get()};
		std::unordered_set<world::ChunkCoordinate> processed;

		EmptyPlacement() { placeNothingElsewhere(executor, &processed); }
	};

} // namespace ecs
