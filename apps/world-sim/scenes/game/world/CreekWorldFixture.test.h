#pragma once

// A grassland world crossed by a fordable creek 0.8 m wide along y = kCreekY, its chunks round the
// origin generated once and shared, and a nav mesh a mover at the origin has meshed out past it.

#include <ecs/World.h>
#include <ecs/components/AgentRadius.h>
#include <ecs/components/Memory.h>
#include <ecs/components/Transform.h>
#include <ecs/systems/NavigationSystem.h>
#include <ecs/systems/PlacedNothing.test.h>
#include <world/chunk/ChunkManager.h>
#include <world/chunk/IWorldSampler.h>

#include <glm/vec2.hpp>

#include <memory>

namespace world_sim::testing {

	constexpr float kCreekY = 20.0F;
	constexpr float kCreekHalfWidthMeters = 0.4F;

	class CreekSampler : public world::IWorldSampler {
	  public:
		[[nodiscard]] world::ChunkSampleResult sampleChunk(world::ChunkCoordinate) const override {
			world::ChunkSampleResult r = world::makeUniformChunkSampleResult(world::BiomeWeights::single(world::Biome::TemperateGrassland), 1.0F);
			r.riverSegments.push_back({.x0 = -4096.0,
									   .y0 = kCreekY,
									   .x1 = 4096.0,
									   .y1 = kCreekY,
									   .halfWidth0 = kCreekHalfWidthMeters,
									   .halfWidth1 = kCreekHalfWidthMeters,
									   .s0 = 0.0,
									   .s1 = 8192.0});
			return r;
		}
		[[nodiscard]] float	   sampleElevation(world::WorldPosition) const override { return 1.0F; }
		[[nodiscard]] uint64_t getWorldSeed() const override { return 7; }
	};

	inline world::ChunkManager* creekChunks() {
		static const std::unique_ptr<world::ChunkManager> chunks = [] {
			auto manager = std::make_unique<world::ChunkManager>(std::make_unique<CreekSampler>());
			manager->setLoadRadius(2);
			manager->setUnloadRadius(4);
			manager->update({0.0F, 0.0F});
			manager->finishPendingGeneration();
			return manager;
		}();
		return chunks.get();
	}

	// Registers a nav system in `world` over the creek world and meshes it round a mover at the origin.
	inline ecs::NavigationSystem& meshCreekWorld(ecs::World& world, ecs::EmptyPlacement& placement) {
		auto& navigation = world.registerSystem<ecs::NavigationSystem>();
		navigation.setChunkManager(creekChunks());
		navigation.setPlacementData(&placement.executor, &placement.processed);
		const ecs::EntityID mesher = world.createEntity();
		world.addComponent<ecs::Position>(mesher, ecs::Position{{0.0F, 0.0F}});
		world.addComponent<ecs::AgentRadius>(mesher, ecs::AgentRadius{});
		ecs::Memory memory;
		memory.sightRadius = 100.0F;
		world.addComponent<ecs::Memory>(mesher, std::move(memory));
		world.update(0.016F);
		navigation.finishPendingBuilds();
		return navigation;
	}

} // namespace world_sim::testing
