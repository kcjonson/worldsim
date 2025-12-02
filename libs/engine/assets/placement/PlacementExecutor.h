#pragma once

// Placement Executor - Entity Placement Engine
// Orchestrates entity placement using dependency graph for spawn ordering
// and spatial index for relationship-based spawn probability modifiers.

#include "DependencyGraph.h"
#include "PlacementTypes.h"
#include "SpatialIndex.h"

#include "assets/AssetDefinition.h"

#include <world/Biome.h>
#include <world/chunk/ChunkCoordinate.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace engine::assets {

	class AssetRegistry;

	/// Chunk data needed for entity placement
	struct ChunkPlacementContext {
		world::ChunkCoordinate coord;
		uint64_t			   worldSeed = 0;

		/// Get biome at local tile coordinates
		/// @param localX Tile X within chunk (0 to kChunkSize-1)
		/// @param localY Tile Y within chunk (0 to kChunkSize-1)
		/// @return Primary biome at this tile
		std::function<world::Biome(uint16_t localX, uint16_t localY)> getBiome;

		/// Get ground cover type at local tile coordinates (for "near Water" etc)
		/// @param localX Tile X within chunk (0 to kChunkSize-1)
		/// @param localY Tile Y within chunk (0 to kChunkSize-1)
		/// @return Ground cover type name (e.g., "Water", "Rock")
		std::function<std::string(uint16_t localX, uint16_t localY)> getGroundCover;
	};

	/// Result of placing entities in a chunk
	struct ChunkPlacementResult {
		world::ChunkCoordinate		coord;
		std::vector<PlacedEntity>	entities;
		size_t						entitiesPlaced = 0;
	};

	/// Interface for querying adjacent chunks during placement.
	/// Allows cross-chunk relationship lookups (e.g., mushroom near tree at chunk edge).
	class IAdjacentChunkProvider {
	  public:
		virtual ~IAdjacentChunkProvider() = default;

		/// Get spatial index for an adjacent chunk (may be null if chunk not loaded)
		[[nodiscard]] virtual const SpatialIndex* getChunkIndex(world::ChunkCoordinate coord) const = 0;
	};

	/// Placement Executor - Main entity placement engine.
	/// Processes chunks and spawns entities based on biome rules and relationships.
	class PlacementExecutor {
	  public:
		/// Create executor with reference to asset registry
		explicit PlacementExecutor(const AssetRegistry& registry);

		/// Initialize the executor by building dependency graph from asset definitions.
		/// Call this after all assets are loaded.
		void initialize();

		/// Place entities in a chunk according to biome rules and relationships.
		/// @param context Chunk data (coord, seed, biome/ground lookups)
		/// @param adjacentProvider Optional provider for cross-chunk queries (can be null)
		/// @return Placement result with list of spawned entities
		ChunkPlacementResult processChunk(const ChunkPlacementContext& context,
										  const IAdjacentChunkProvider* adjacentProvider = nullptr);

		/// Get the spatial index for a previously processed chunk.
		/// Returns nullptr if chunk hasn't been processed.
		[[nodiscard]] const SpatialIndex* getChunkIndex(world::ChunkCoordinate coord) const;

		/// Remove chunk data (call when chunk is unloaded)
		void unloadChunk(world::ChunkCoordinate coord);

		/// Get spawn order (for debugging/testing)
		[[nodiscard]] const std::vector<std::string>& getSpawnOrder() const { return m_spawnOrder; }

		/// Check if executor has been initialized
		[[nodiscard]] bool isInitialized() const { return m_initialized; }

		/// Clear all state
		void clear();

	  private:
		const AssetRegistry& m_registry;
		DependencyGraph		 m_dependencyGraph;
		std::vector<std::string> m_spawnOrder; // Topologically sorted entity types
		bool m_initialized = false;

		// Per-chunk spatial indices
		std::unordered_map<world::ChunkCoordinate, SpatialIndex> m_chunkIndices;

		/// Build dependency graph from asset definitions
		void buildDependencyGraph();

		/// Place all entities of a given type in a chunk
		void placeEntityType(const std::string& defName,
							 const ChunkPlacementContext& context,
							 SpatialIndex& chunkIndex,
							 const IAdjacentChunkProvider* adjacentProvider,
							 std::mt19937& rng,
							 std::vector<PlacedEntity>& outEntities);

		/// Calculate spawn probability modifier from relationships
		/// @param def Asset definition with relationships
		/// @param position World position being tested
		/// @param chunkIndex Current chunk's spatial index
		/// @param adjacentProvider Provider for adjacent chunk indices
		/// @return Probability multiplier (1.0 = no change, 0 = don't spawn)
		float calculateRelationshipModifier(const AssetDefinition& def,
											glm::vec2 position,
											const SpatialIndex& chunkIndex,
											const IAdjacentChunkProvider* adjacentProvider) const;

		/// Check if a "requires" relationship is satisfied
		bool isRequirementSatisfied(const PlacementRelationship& rel,
									const std::string& defName,
									glm::vec2 position,
									const SpatialIndex& chunkIndex,
									const IAdjacentChunkProvider* adjacentProvider) const;

		/// Get group members as a set for efficient lookup
		[[nodiscard]] std::unordered_set<std::string> getGroupMembersSet(const std::string& groupName) const;

		/// Query nearby entities across chunk boundaries
		[[nodiscard]] bool hasNearbyAcrossChunks(glm::vec2 position,
												 float radius,
												 const std::string& defName,
												 const SpatialIndex& chunkIndex,
												 const IAdjacentChunkProvider* adjacentProvider) const;

		[[nodiscard]] bool hasNearbyGroupAcrossChunks(glm::vec2 position,
													  float radius,
													  const std::unordered_set<std::string>& defNames,
													  const SpatialIndex& chunkIndex,
													  const IAdjacentChunkProvider* adjacentProvider) const;
	};

} // namespace engine::assets
