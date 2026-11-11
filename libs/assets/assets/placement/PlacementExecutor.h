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
// 5. REGROWTH: A non-destructive harvest (a berry bush) stamps the time the
//    entity is ready again. Times are the caller's simulation clock; assets
//    keeps none of its own. An entry lapses on the first lookup past its time.
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
//
// Related Documentation:
// - /docs/design/game-systems/world/entity-placement.md (design spec)
// - /docs/technical/procedural-generation.md (algorithm details)
// - AssetDefinition.h for PlacementRelationship struct

#include "DecimeterGrid.h"
#include "DependencyGraph.h"
#include "PlacementTypes.h"
#include "SpatialIndex.h"

#include "assets/AssetDefinition.h"

#include <world/Biome.h>
#include <world/chunk/ChunkCoordinate.h>

#include <glm/vec2.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace world {
	enum class Surface : uint8_t;
}

namespace assets {

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

		/// Whether a world position (meters) is in water, from the chunk's terrain
		/// polygons (terrain-polygons-architecture.md D1).
		std::function<bool(glm::vec2 worldPos)> isWater;

		/// The land surface painted at a world position (meters) in the chunk square:
		/// the ground shader's field (SurfaceField, D16), so what grows on a spot is
		/// what is drawn there. Never Water; isWater answers that.
		std::function<world::Surface(glm::vec2 worldPos)> landSurface;
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
		uint64_t				  worldSeed = 0;	///< World seed for deterministic resource count init
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

		/// Remove an entity at the specified position, with its cooldown and resource pool
		/// @param coord Chunk coordinate containing the entity
		/// @param position World position of the entity
		/// @param defName DefName of the entity to remove
		/// @return true if entity was found and removed
		bool removeEntity(world::ChunkCoordinate coord, glm::vec2 position, const std::string& defName);

		/// Erase an entity's cooldown and resource pool. removeEntity does this for placed flora; a
		/// runtime-spawned harvestable (an ECS entity in no chunk index) is forgotten here when taken.
		void forgetEntityState(glm::vec2 position, const std::string& defName);

		/// Start an entity regrowing after a non-destructive harvest: it is ready again at now + seconds,
		/// on the caller's clock. Every entry is kept across chunk unloads (it is keyed by world position,
		/// not by chunk), so a bush regrowing when its chunk unloads is still regrowing on reload until
		/// its time. Lapsed entries nothing looked up again are swept here once the map has doubled since
		/// the last sweep, so it stays within twice the entities regrowing at that sweep.
		void startRegrowth(glm::vec2 position, uint32_t defNameId, double now, double seconds);

		/// When a regrowing entity is ready again, or nullopt if it is ready at `now`. An entry whose time
		/// has come is erased here. Keyed by interned def id (AssetRegistry::getDefNameId), so a lookup
		/// hashes three integers and never a name.
		[[nodiscard]] std::optional<double> regrowthReadyAt(glm::vec2 position, uint32_t defNameId, double now);

		/// Regrowth entries held, lapsed ones not yet looked up or swept included
		[[nodiscard]] size_t regrowthEntryCount() const { return m_regrowthReadyAt.size(); }

		/// Initialize resource count for an entity (called during spawn)
		/// @param position World position of the entity
		/// @param defName DefName of the entity
		/// @param count Initial resource count
		void initResourceCount(glm::vec2 position, const std::string& defName, uint32_t count);

		/// Get the current resource count for an entity
		/// @return Resource count, or nullopt if entity has no resource tracking
		[[nodiscard]] std::optional<uint32_t> getResourceCount(glm::vec2 position, const std::string& defName) const;

		/// Withdraw up to `requested` units from an entity's resource pool after a harvest.
		/// @return The number of units actually removed (clamped to what the pool held; 0 if
		///         the entity has no resource tracking). When this empties the pool the entry
		///         is erased, so a following getResourceCount() returns nullopt (= depleted).
		uint32_t decrementResourceCount(glm::vec2 position, const std::string& defName, uint32_t requested);

		/// Monotonic counter bumped every time a placed entity is removed from a chunk index
		/// (a destructive harvest felling a tree, a depleted resource node). The nav mesh
		/// gathers its flora obstacle rings from the LIVE chunk indices, so a removal must
		/// force the covering region to rebuild -- otherwise the felled tree's trunk hole
		/// lingers in the mesh and that tile never becomes walkable. NavigationSystem folds
		/// this epoch into its rebuild gate; chunk membership and the construction version
		/// alone don't see an in-chunk entity removal.
		[[nodiscard]] uint64_t removalEpoch() const { return m_removalEpoch; }

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

		// Bumped on every successful entity removal; see removalEpoch().
		uint64_t m_removalEpoch = 0;

		/// One entity's identity for its cooldown and resource pool: its placed position on the 0.1 m
		/// grid colonist Memory keys it by, and its interned def id. Two bushes of one def in the same tile
		/// are two entities and never share a regrowth timer or a pool.
		struct EntityStateKey {
			DecimeterCell cell;
			uint32_t	  defNameId = 0;

			bool operator==(const EntityStateKey& other) const {
				return cell.x == other.cell.x && cell.y == other.cell.y && defNameId == other.defNameId;
			}
		};

		struct EntityStateKeyHash {
			std::size_t operator()(const EntityStateKey& key) const {
				std::size_t seed = std::hash<int32_t>{}(key.cell.x);
				seed = seed * 31 + std::hash<int32_t>{}(key.cell.y);
				seed = seed * 31 + std::hash<uint32_t>{}(key.defNameId);
				return seed;
			}
		};

		/// The time each regrowing entity is ready again, on the caller's clock
		std::unordered_map<EntityStateKey, double, EntityStateKeyHash> m_regrowthReadyAt;

		/// startRegrowth sweeps lapsed entries once the map reaches this size
		static constexpr size_t kMinRegrowthSweepSize = 1024;
		size_t					m_regrowthSweepSize = kMinRegrowthSweepSize;

		/// Remaining resource pool (units left to harvest), per entity
		std::unordered_map<EntityStateKey, uint32_t, EntityStateKeyHash> m_resourceCounts;

		[[nodiscard]] EntityStateKey makeEntityStateKey(glm::vec2 position, const std::string& defName) const;
		[[nodiscard]] static EntityStateKey makeEntityStateKey(glm::vec2 position, uint32_t defNameId);

		/// Build dependency graph from asset definitions
		void buildDependencyGraph();

		/// Crown radius in meters by template id, for the templates of crown-spaced defs placed in
		/// one chunk (PlacementParams::crownSpacing); 0 for any other template. `largest` is the
		/// biggest radius held.
		struct CrownRadii {
			std::unordered_map<uint32_t, float> byTemplate;
			float								largest = 0.0F;
		};

		/// Place all entities of a given type in a chunk
		void placeEntityType(
			const std::string&			  defName,
			const ChunkPlacementContext&  context,
			SpatialIndex&				  chunkIndex,
			const IAdjacentChunkProvider* adjacentProvider,
			std::mt19937&				  rng,
			CrownRadii&					  crowns,
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

} // namespace assets
