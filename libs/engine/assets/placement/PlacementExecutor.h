#pragma once

// Placement Executor - Entity Placement Engine
//
// This is the central orchestrator for procedural entity placement in chunks.
// It transforms asset definitions (trees, bushes, stones, etc.) into actual
// placed entities in the world, respecting biome rules, spawn probabilities,
// and inter-entity relationships.
//
// Key Responsibilities:
// 1. DEPENDENCY-ORDERED SPAWNING: Entities are spawned in dependency order
//    (e.g., trees before mushrooms that "require nearby Tree"). Built from
//    asset definitions using DependencyGraph for topological sorting.
//
// 2. RELATIONSHIP-BASED PROBABILITY: Spawn probability is modified by
//    relationships defined in asset XML (e.g., "near Tree" → 2x probability,
//    "requires nearby Water" → must have water tile within radius).
//
// 3. CROSS-CHUNK QUERIES: Entities near chunk edges can query adjacent
//    chunks via IAdjacentChunkProvider for relationship checks.
//
// 4. ENTITY REMOVAL: Supports removing entities when harvested/destroyed.
//    Called by ActionSystem when collection actions complete.
//
// 5. COOLDOWN TRACKING: Non-destructive harvests (berry bushes) put entities
//    on cooldown. Tracks remaining time and exposes query for AI/Vision.
//
// Thread Safety:
// - processChunk() modifies internal state - NOT thread-safe
// - computeChunkEntities() is const and thread-safe for parallel chunk gen
// - storeChunkResult() must be called from main thread after async compute
//
// Usage Flow:
// 1. Create PlacementExecutor with AssetRegistry reference
// 2. Call initialize() after assets are loaded (builds dependency graph)
// 3. For each chunk: processChunk() or computeChunkEntities()+storeChunkResult()
// 4. Query getChunkIndex() for spatial lookups
// 5. Call updateCooldowns() each frame for regrowth timing
//
// Related Documentation:
// - /docs/design/game-systems/world/entity-placement.md (design spec)
// - /docs/technical/procedural-generation.md (algorithm details)
// - AssetDefinition.h for PlacementRelationship struct

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

		/// Get surface type at local tile coordinates (for "near Water" etc)
		/// @param localX Tile X within chunk (0 to kChunkSize-1)
		/// @param localY Tile Y within chunk (0 to kChunkSize-1)
		/// @return Surface type name (e.g., "Water", "Rock", "Grass")
		std::function<std::string(uint16_t localX, uint16_t localY)> getSurface;
	};

	/// Result of placing entities in a chunk
	struct ChunkPlacementResult {
		world::ChunkCoordinate	  coord;
		std::vector<PlacedEntity> entities;
		size_t					  entitiesPlaced = 0;
	};

	/// Result of async chunk computation (includes spatial index for later storage)
	struct AsyncChunkPlacementResult {
		world::ChunkCoordinate	  coord;
		std::vector<PlacedEntity> entities;
		SpatialIndex			  spatialIndex;
		size_t					  entitiesPlaced = 0;
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
	/// Implements IAdjacentChunkProvider to serve as its own cross-chunk query source.
	class PlacementExecutor : public IAdjacentChunkProvider {
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
		/// @note This method modifies internal state - NOT thread-safe
		ChunkPlacementResult processChunk(const ChunkPlacementContext& context, const IAdjacentChunkProvider* adjacentProvider = nullptr);

		/// Compute entity placements without storing to internal state (thread-safe).
		/// Use this for async/background processing, then call storeChunkResult() on main thread.
		/// @param context Chunk data (coord, seed, biome/ground lookups)
		/// @param adjacentProvider Optional provider for cross-chunk queries (can be null)
		/// @return Result including computed SpatialIndex for later storage
		/// @note Thread-safe: does not modify internal state
		[[nodiscard]] AsyncChunkPlacementResult
		computeChunkEntities(const ChunkPlacementContext& context, const IAdjacentChunkProvider* adjacentProvider = nullptr) const;

		/// Store a pre-computed chunk result (main thread only).
		/// Call this after computeChunkEntities() completes on a background thread.
		/// @param result The async result from computeChunkEntities()
		/// @note NOT thread-safe - call only from main thread
		void storeChunkResult(AsyncChunkPlacementResult&& result);

		/// Get the spatial index for a previously processed chunk.
		/// Returns nullptr if chunk hasn't been processed.
		[[nodiscard]] const SpatialIndex* getChunkIndex(world::ChunkCoordinate coord) const;

		/// Remove chunk data (call when chunk is unloaded)
		void unloadChunk(world::ChunkCoordinate coord);

		/// Remove an entity at the specified position
		/// @param coord Chunk coordinate containing the entity
		/// @param position World position of the entity
		/// @param defName DefName of the entity to remove
		/// @return true if entity was found and removed
		bool removeEntity(world::ChunkCoordinate coord, glm::vec2 position, const std::string& defName);

		/// Set an entity on cooldown (for regrowth after non-destructive harvest)
		/// @param coord Chunk coordinate containing the entity
		/// @param position World position of the entity
		/// @param defName DefName of the entity
		/// @param cooldownSeconds Time until entity can be harvested again
		void setEntityCooldown(world::ChunkCoordinate coord, glm::vec2 position, const std::string& defName, float cooldownSeconds);

		/// Check if an entity is currently on cooldown
		/// @param coord Chunk coordinate containing the entity
		/// @param position World position of the entity
		/// @param defName DefName of the entity
		/// @return true if entity is on cooldown (not harvestable)
		[[nodiscard]] bool isEntityOnCooldown(world::ChunkCoordinate coord, glm::vec2 position, const std::string& defName) const;

		/// Update cooldown timers (call once per frame)
		/// @param deltaTime Time elapsed since last update
		void updateCooldowns(float deltaTime);

		/// Get spawn order (for debugging/testing)
		[[nodiscard]] const std::vector<std::string>& getSpawnOrder() const { return m_spawnOrder; }

		/// Check if executor has been initialized
		[[nodiscard]] bool isInitialized() const { return m_initialized; }

		/// Clear all state
		void clear();

	  private:
		const AssetRegistry&	 m_registry;
		DependencyGraph			 m_dependencyGraph;
		std::vector<std::string> m_spawnOrder; // Topologically sorted entity types
		bool					 m_initialized = false;

		// Per-chunk spatial indices
		std::unordered_map<world::ChunkCoordinate, SpatialIndex> m_chunkIndices;

		/// Entity cooldown key - uniquely identifies an entity for cooldown tracking
		/// Uses quantized position (integer tile coordinates) for reliable hashing
		struct CooldownKey {
			world::ChunkCoordinate coord;
			int32_t				   tileX; // Position quantized to tile
			int32_t				   tileY;
			std::string			   defName;

			bool operator==(const CooldownKey& other) const {
				return coord == other.coord && tileX == other.tileX && tileY == other.tileY && defName == other.defName;
			}
		};

		/// Hash function for CooldownKey
		/// Uses multiplicative hash combining for better avalanche properties
		struct CooldownKeyHash {
			std::size_t operator()(const CooldownKey& key) const {
				std::size_t h1 = std::hash<int32_t>{}(key.coord.x);
				std::size_t h2 = std::hash<int32_t>{}(key.coord.y);
				std::size_t h3 = std::hash<int32_t>{}(key.tileX);
				std::size_t h4 = std::hash<int32_t>{}(key.tileY);
				std::size_t h5 = std::hash<std::string>{}(key.defName);
				// Combine hashes using multiplicative method for better distribution
				std::size_t seed = 0;
				seed = seed * 31 + h1;
				seed = seed * 31 + h2;
				seed = seed * 31 + h3;
				seed = seed * 31 + h4;
				seed = seed * 31 + h5;
				return seed;
			}
		};

		/// Entity cooldown tracking using hash map for O(1) lookup
		/// Key: (chunk coord, quantized position, defName)
		/// Value: remaining cooldown time in seconds
		std::unordered_map<CooldownKey, float, CooldownKeyHash> m_cooldowns;

		/// Convert world position to cooldown key
		[[nodiscard]] static CooldownKey makeCooldownKey(world::ChunkCoordinate coord, glm::vec2 position, const std::string& defName);

		/// Build dependency graph from asset definitions
		void buildDependencyGraph();

		/// Place all entities of a given type in a chunk
		void placeEntityType(
			const std::string&			  defName,
			const ChunkPlacementContext&  context,
			SpatialIndex&				  chunkIndex,
			const IAdjacentChunkProvider* adjacentProvider,
			std::mt19937&				  rng,
			std::vector<PlacedEntity>&	  outEntities
		) const;

		/// Calculate spawn probability modifier from relationships
		/// @param def Asset definition with relationships
		/// @param position World position being tested
		/// @param chunkIndex Current chunk's spatial index
		/// @param adjacentProvider Provider for adjacent chunk indices
		/// @return Probability multiplier (1.0 = no change, 0 = don't spawn)
		float calculateRelationshipModifier(
			const AssetDefinition&		  def,
			glm::vec2					  position,
			const SpatialIndex&			  chunkIndex,
			const IAdjacentChunkProvider* adjacentProvider
		) const;

		/// Check if a "requires" relationship is satisfied
		bool isRequirementSatisfied(
			const PlacementRelationship&  rel,
			const std::string&			  defName,
			glm::vec2					  position,
			const SpatialIndex&			  chunkIndex,
			const IAdjacentChunkProvider* adjacentProvider
		) const;

		/// Get group members as a set for efficient lookup
		[[nodiscard]] std::unordered_set<std::string> getGroupMembersSet(const std::string& groupName) const;

		/// Query nearby entities across chunk boundaries
		[[nodiscard]] bool hasNearbyAcrossChunks(
			glm::vec2					  position,
			float						  radius,
			const std::string&			  defName,
			const SpatialIndex&			  chunkIndex,
			const IAdjacentChunkProvider* adjacentProvider
		) const;

		[[nodiscard]] bool hasNearbyGroupAcrossChunks(
			glm::vec2							   position,
			float								   radius,
			const std::unordered_set<std::string>& defNames,
			const SpatialIndex&					   chunkIndex,
			const IAdjacentChunkProvider*		   adjacentProvider
		) const;
	};

} // namespace engine::assets
