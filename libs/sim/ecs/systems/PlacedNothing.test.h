#pragma once

#include <assets/placement/PlacementExecutor.h>

#include <world/chunk/ChunkCoordinate.h>

#include <cstdint>
#include <unordered_set>

namespace ecs {

	// The chunks round the origin every test fixture's chunk region lies in.
	constexpr std::int32_t kFixtureChunkReach = 4;

	// Every fixture chunk with no placement yet is stored placed with nothing, and marked processed
	// when `processed` is given, as the game's placement does for a chunk it generates empty: a
	// visibility limit over it then need not wait for placement.
	inline void placeNothingElsewhere(assets::PlacementExecutor& executor, std::unordered_set<world::ChunkCoordinate>* processed) {
		for (std::int32_t cy = -kFixtureChunkReach; cy <= kFixtureChunkReach; ++cy) {
			for (std::int32_t cx = -kFixtureChunkReach; cx <= kFixtureChunkReach; ++cx) {
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

} // namespace ecs
