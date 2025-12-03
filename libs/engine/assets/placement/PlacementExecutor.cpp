#include "PlacementExecutor.h"

#include "assets/AssetRegistry.h"

#include <utils/Log.h>
#include <world/chunk/ChunkCoordinate.h>

#include <glm/vec2.hpp>

namespace engine::assets {

	PlacementExecutor::PlacementExecutor(const AssetRegistry& registry)
		: m_registry(registry) {}

	void PlacementExecutor::initialize() {
		buildDependencyGraph();
		m_initialized = true;
		LOG_DEBUG(Engine, "PlacementExecutor initialized with %zu entity types in spawn order", m_spawnOrder.size());
	}

	void PlacementExecutor::buildDependencyGraph() {
		m_dependencyGraph.clear();
		m_spawnOrder.clear();

		// Add all entity types that have placement rules
		auto defNames = m_registry.getDefinitionNames();
		for (const auto& defName : defNames) {
			const auto* def = m_registry.getDefinition(defName);
			if (def == nullptr) {
				continue;
			}

			// Only add to graph if it has biome placement rules
			if (def->placement.biomes.empty()) {
				continue;
			}

			m_dependencyGraph.addNode(defName);

			// Add dependencies from "requires" relationships
			for (const auto& rel : def->placement.relationships) {
				if (rel.kind != RelationshipKind::Requires) {
					continue;
				}

				// Add edges based on target type
				switch (rel.target.type) {
					case EntityRef::Type::DefName:
						m_dependencyGraph.addDependency(defName, rel.target.value);
						break;

					case EntityRef::Type::Group: {
						// Add dependency on all members of the group
						auto members = m_registry.getGroupMembers(rel.target.value);
						for (const auto& member : members) {
							m_dependencyGraph.addDependency(defName, member);
						}
						break;
					}

					case EntityRef::Type::Same:
						// Self-reference doesn't create a dependency
						// (we can spawn more of the same type after initial spawn)
						break;
				}
			}
		}

		// Get topological spawn order
		try {
			m_spawnOrder = m_dependencyGraph.getSpawnOrder();
		} catch (const CyclicDependencyError& e) {
			LOG_ERROR(Engine, "Cyclic dependency in entity placement: %s", e.what());
			m_spawnOrder.clear();
		}
	}

	ChunkPlacementResult
	PlacementExecutor::processChunk(const ChunkPlacementContext& context, const IAdjacentChunkProvider* adjacentProvider) {
		ChunkPlacementResult result;
		result.coord = context.coord;

		LOG_DEBUG(Engine, "PlacementExecutor::processChunk starting for chunk (%d, %d)", context.coord.x, context.coord.y);

		if (!m_initialized) {
			LOG_WARNING(Engine, "PlacementExecutor::processChunk called before initialize()");
			return result;
		}

		// Create or get spatial index for this chunk
		auto& chunkIndex = m_chunkIndices[context.coord];
		chunkIndex.clear();

		// Create deterministic RNG from chunk coordinate and world seed
		uint64_t chunkSeed = context.worldSeed;
		chunkSeed ^= static_cast<uint64_t>(context.coord.x) * 0x9E3779B97F4A7C15ULL;
		chunkSeed ^= static_cast<uint64_t>(context.coord.y) * 0x6C62272E07BB0143ULL;
		std::mt19937 rng(static_cast<uint32_t>(chunkSeed));

		// Process entity types in dependency order
		size_t typeIndex = 0;
		for (const auto& defName : m_spawnOrder) {
			size_t entitiesBefore = result.entities.size();
			LOG_DEBUG(Engine, "  Processing entity type %zu/%zu: %s", typeIndex + 1, m_spawnOrder.size(), defName.c_str());
			placeEntityType(defName, context, chunkIndex, adjacentProvider, rng, result.entities);
			size_t entitiesPlaced = result.entities.size() - entitiesBefore;
			LOG_DEBUG(Engine, "    Placed %zu entities for %s", entitiesPlaced, defName.c_str());
			++typeIndex;
		}

		result.entitiesPlaced = result.entities.size();
		LOG_DEBUG(Engine, "PlacementExecutor::processChunk completed for chunk (%d, %d) - total %zu entities",
			context.coord.x, context.coord.y, result.entitiesPlaced);
		return result;
	}

	void PlacementExecutor::placeEntityType(
		const std::string&			  defName,
		const ChunkPlacementContext&  context,
		SpatialIndex&				  chunkIndex,
		const IAdjacentChunkProvider* adjacentProvider,
		std::mt19937&				  rng,
		std::vector<PlacedEntity>&	  outEntities
	) const {
		const auto* def = m_registry.getDefinition(defName);
		if (def == nullptr) {
			return;
		}

		// Get chunk origin for world position calculation
		world::WorldPosition origin = context.coord.origin();

		// RNG distributions
		std::uniform_real_distribution<float> chanceDist(0.0F, 1.0F);
		std::uniform_real_distribution<float> offsetDist(0.0F, 1.0F); // Position within tile
		std::uniform_real_distribution<float> rotationDist(-0.3F, 0.3F);
		std::uniform_real_distribution<float> scaleDist(0.8F, 1.2F);
		std::uniform_real_distribution<float> colorDist(-0.08F, 0.08F);

		// Tile stride optimization: with 512x512 chunks, sampling every 4th tile
		// reduces iterations from 262K to ~16K per chunk while maintaining coverage
		constexpr uint16_t kTileStride = 4;

		// Iterate over sampled tiles in the chunk
		for (uint16_t localY = 0; localY < world::kChunkSize; localY += kTileStride) {
			for (uint16_t localX = 0; localX < world::kChunkSize; localX += kTileStride) {
				// Get biome at this tile
				world::Biome biome = context.getBiome(localX, localY);
				std::string	 biomeName = world::biomeToString(biome);

				// Find placement config for this biome
				const BiomePlacement* bp = def->placement.findBiome(biomeName);
				if (bp == nullptr) {
					continue; // This entity doesn't spawn in this biome
				}

				// Check tile-type proximity ("near Water" etc)
				if (!bp->nearTileType.empty() && context.getGroundCover) {
					bool foundNearby = false;
					int	 searchRadius = static_cast<int>(bp->nearDistance);
					if (searchRadius < 1) {
						searchRadius = 1;
					}

					for (int dy = -searchRadius; dy <= searchRadius && !foundNearby; ++dy) {
						for (int dx = -searchRadius; dx <= searchRadius && !foundNearby; ++dx) {
							int checkX = static_cast<int>(localX) + dx;
							int checkY = static_cast<int>(localY) + dy;

							if (checkX >= 0 && checkX < world::kChunkSize && checkY >= 0 && checkY < world::kChunkSize) {
								std::string groundCover =
									context.getGroundCover(static_cast<uint16_t>(checkX), static_cast<uint16_t>(checkY));
								if (groundCover == bp->nearTileType) {
									foundNearby = true;
								}
							}
						}
					}

					if (!foundNearby) {
						continue;
					}
				}

				// Calculate tile world position (corner)
				float tileWorldX = origin.x + static_cast<float>(localX);
				float tileWorldY = origin.y + static_cast<float>(localY);

				// Roll for spawn based on distribution type
				if (chanceDist(rng) >= bp->spawnChance) {
					continue; // Spawn chance failed
				}

				// Handle distribution types
				switch (bp->distribution) {
					case Distribution::Clumped: {
						// Generate clump center randomly within tile
						glm::vec2 clumpCenter{
							tileWorldX + offsetDist(rng),
							tileWorldY + offsetDist(rng)
						};

						// Clump parameters
						std::uniform_int_distribution<int32_t> clumpSizeDist(
							bp->clumping.clumpSizeMin, bp->clumping.clumpSizeMax);
						int32_t clumpSize = clumpSizeDist(rng);

						std::uniform_real_distribution<float> clumpRadiusDist(
							bp->clumping.clumpRadiusMin, bp->clumping.clumpRadiusMax);
						float clumpRadius = clumpRadiusDist(rng);

						// Spawn instances in clump
						for (int32_t i = 0; i < clumpSize; ++i) {
							// Random offset within clump radius
							std::uniform_real_distribution<float> clumpOffsetDist(-clumpRadius, clumpRadius);
							glm::vec2 position{
								clumpCenter.x + clumpOffsetDist(rng),
								clumpCenter.y + clumpOffsetDist(rng)
							};

							// Check relationship modifiers for this position
							float modifier = calculateRelationshipModifier(*def, position, chunkIndex, adjacentProvider);
							if (modifier <= 0.0F) {
								continue;
							}

							// Generate visual variation
							float greenVar = colorDist(rng);

							PlacedEntity entity;
							entity.defName = defName;
							entity.position = position;
							entity.rotation = rotationDist(rng);
							entity.scale = scaleDist(rng);
							entity.colorTint = glm::vec4(
								0.15F + greenVar,
								0.35F + greenVar * 2.0F,
								0.1F + greenVar * 0.5F,
								1.0F
							);

							chunkIndex.insert(entity);
							outEntities.push_back(entity);
						}
						break;
					}

					case Distribution::Spaced: {
						// TODO: Implement Poisson disk sampling
						// For now, fall through to uniform
						[[fallthrough]];
					}

					case Distribution::Uniform:
					default: {
						// Single entity at random position within tile
						glm::vec2 position{
							tileWorldX + offsetDist(rng),
							tileWorldY + offsetDist(rng)
						};

						// Check relationship modifiers
						float modifier = calculateRelationshipModifier(*def, position, chunkIndex, adjacentProvider);
						if (modifier <= 0.0F) {
							continue;
						}

						// Generate visual variation
						float greenVar = colorDist(rng);

						PlacedEntity entity;
						entity.defName = defName;
						entity.position = position;
						entity.rotation = rotationDist(rng);
						entity.scale = scaleDist(rng);
						entity.colorTint = glm::vec4(
							0.15F + greenVar,
							0.35F + greenVar * 2.0F,
							0.1F + greenVar * 0.5F,
							1.0F
						);

						chunkIndex.insert(entity);
						outEntities.push_back(entity);
						break;
					}
				}
			}
		}
	}

	float PlacementExecutor::calculateRelationshipModifier(
		const AssetDefinition&		  def,
		glm::vec2					  position,
		const SpatialIndex&			  chunkIndex,
		const IAdjacentChunkProvider* adjacentProvider
	) const {
		float modifier = 1.0F;

		for (const auto& rel : def.placement.relationships) {
			switch (rel.kind) {
				case RelationshipKind::Requires: {
					if (!isRequirementSatisfied(rel, def.defName, position, chunkIndex, adjacentProvider)) {
						return 0.0F; // Hard requirement not met
					}
					break;
				}

				case RelationshipKind::Affinity: {
					// Check if target is nearby
					bool hasNearby = false;
					switch (rel.target.type) {
						case EntityRef::Type::DefName:
							hasNearby = hasNearbyAcrossChunks(position, rel.distance, rel.target.value, chunkIndex, adjacentProvider);
							break;
						case EntityRef::Type::Group: {
							auto members = getGroupMembersSet(rel.target.value);
							hasNearby = hasNearbyGroupAcrossChunks(position, rel.distance, members, chunkIndex, adjacentProvider);
							break;
						}
						case EntityRef::Type::Same:
							hasNearby = hasNearbyAcrossChunks(position, rel.distance, def.defName, chunkIndex, adjacentProvider);
							break;
					}

					if (hasNearby) {
						modifier *= rel.strength; // Boost spawn chance
					}
					break;
				}

				case RelationshipKind::Avoids: {
					// Check if target is nearby
					bool hasNearby = false;
					switch (rel.target.type) {
						case EntityRef::Type::DefName:
							hasNearby = hasNearbyAcrossChunks(position, rel.distance, rel.target.value, chunkIndex, adjacentProvider);
							break;
						case EntityRef::Type::Group: {
							auto members = getGroupMembersSet(rel.target.value);
							hasNearby = hasNearbyGroupAcrossChunks(position, rel.distance, members, chunkIndex, adjacentProvider);
							break;
						}
						case EntityRef::Type::Same:
							hasNearby = hasNearbyAcrossChunks(position, rel.distance, def.defName, chunkIndex, adjacentProvider);
							break;
					}

					if (hasNearby) {
						modifier *= rel.penalty; // Reduce spawn chance
					}
					break;
				}
			}
		}

		return modifier;
	}

	bool PlacementExecutor::isRequirementSatisfied(
		const PlacementRelationship&  rel,
		const std::string&			  defName,
		glm::vec2					  position,
		const SpatialIndex&			  chunkIndex,
		const IAdjacentChunkProvider* adjacentProvider
	) const {
		switch (rel.target.type) {
			case EntityRef::Type::DefName:
				return hasNearbyAcrossChunks(position, rel.distance, rel.target.value, chunkIndex, adjacentProvider);

			case EntityRef::Type::Group: {
				auto members = getGroupMembersSet(rel.target.value);
				return hasNearbyGroupAcrossChunks(position, rel.distance, members, chunkIndex, adjacentProvider);
			}

			case EntityRef::Type::Same:
				return hasNearbyAcrossChunks(position, rel.distance, defName, chunkIndex, adjacentProvider);
		}

		return false;
	}

	std::unordered_set<std::string> PlacementExecutor::getGroupMembersSet(const std::string& groupName) const {
		auto members = m_registry.getGroupMembers(groupName);
		return {members.begin(), members.end()};
	}

	bool PlacementExecutor::hasNearbyAcrossChunks(
		glm::vec2					  position,
		float						  radius,
		const std::string&			  defName,
		const SpatialIndex&			  chunkIndex,
		const IAdjacentChunkProvider* adjacentProvider
	) const {
		// First check current chunk
		if (chunkIndex.hasNearby(position, radius, defName)) {
			return true;
		}

		// Check adjacent chunks if provider is available
		if (adjacentProvider == nullptr) {
			return false;
		}

		// Determine which adjacent chunks might contain entities within radius
		world::ChunkCoordinate centerChunk = world::worldToChunk(world::WorldPosition{position.x, position.y});

		// Check 3x3 grid of chunks around position
		for (int dy = -1; dy <= 1; ++dy) {
			for (int dx = -1; dx <= 1; ++dx) {
				if (dx == 0 && dy == 0) {
					continue; // Already checked
				}

				world::ChunkCoordinate adjacentCoord{centerChunk.x + dx, centerChunk.y + dy};
				const SpatialIndex*	   adjacentIndex = adjacentProvider->getChunkIndex(adjacentCoord);
				if (adjacentIndex != nullptr && adjacentIndex->hasNearby(position, radius, defName)) {
					return true;
				}
			}
		}

		return false;
	}

	bool PlacementExecutor::hasNearbyGroupAcrossChunks(
		glm::vec2							   position,
		float								   radius,
		const std::unordered_set<std::string>& defNames,
		const SpatialIndex&					   chunkIndex,
		const IAdjacentChunkProvider*		   adjacentProvider
	) const {
		// First check current chunk
		if (chunkIndex.hasNearbyGroup(position, radius, defNames)) {
			return true;
		}

		// Check adjacent chunks if provider is available
		if (adjacentProvider == nullptr) {
			return false;
		}

		world::ChunkCoordinate centerChunk = world::worldToChunk(world::WorldPosition{position.x, position.y});

		for (int dy = -1; dy <= 1; ++dy) {
			for (int dx = -1; dx <= 1; ++dx) {
				if (dx == 0 && dy == 0) {
					continue;
				}

				world::ChunkCoordinate adjacentCoord{centerChunk.x + dx, centerChunk.y + dy};
				const SpatialIndex*	   adjacentIndex = adjacentProvider->getChunkIndex(adjacentCoord);
				if (adjacentIndex != nullptr && adjacentIndex->hasNearbyGroup(position, radius, defNames)) {
					return true;
				}
			}
		}

		return false;
	}

	const SpatialIndex* PlacementExecutor::getChunkIndex(world::ChunkCoordinate coord) const {
		auto it = m_chunkIndices.find(coord);
		if (it != m_chunkIndices.end()) {
			return &it->second;
		}
		return nullptr;
	}

	void PlacementExecutor::unloadChunk(world::ChunkCoordinate coord) {
		m_chunkIndices.erase(coord);
	}

	AsyncChunkPlacementResult PlacementExecutor::computeChunkEntities(
		const ChunkPlacementContext&  context,
		const IAdjacentChunkProvider* adjacentProvider
	) const {
		AsyncChunkPlacementResult result;
		result.coord = context.coord;

		LOG_INFO(Engine, "PlacementExecutor::computeChunkEntities starting for chunk (%d, %d)", context.coord.x, context.coord.y);

		if (!m_initialized) {
			LOG_WARNING(Engine, "PlacementExecutor::computeChunkEntities called before initialize()");
			return result;
		}

		// Create local spatial index (not stored in m_chunkIndices yet)
		result.spatialIndex.clear();

		// Create deterministic RNG from chunk coordinate and world seed
		uint64_t chunkSeed = context.worldSeed;
		chunkSeed ^= static_cast<uint64_t>(context.coord.x) * 0x9E3779B97F4A7C15ULL;
		chunkSeed ^= static_cast<uint64_t>(context.coord.y) * 0x6C62272E07BB0143ULL;
		std::mt19937 rng(static_cast<uint32_t>(chunkSeed));

		// Process entity types in dependency order
		size_t typeIndex = 0;
		for (const auto& defName : m_spawnOrder) {
			size_t entitiesBefore = result.entities.size();
			LOG_INFO(Engine, "  [async] Processing entity type %zu/%zu: %s", typeIndex + 1, m_spawnOrder.size(), defName.c_str());
			placeEntityType(defName, context, result.spatialIndex, adjacentProvider, rng, result.entities);
			size_t entitiesPlaced = result.entities.size() - entitiesBefore;
			LOG_INFO(Engine, "    [async] Placed %zu entities for %s", entitiesPlaced, defName.c_str());
			++typeIndex;
		}

		result.entitiesPlaced = result.entities.size();
		LOG_INFO(Engine, "PlacementExecutor::computeChunkEntities completed for chunk (%d, %d) - total %zu entities",
			context.coord.x, context.coord.y, result.entitiesPlaced);
		return result;
	}

	void PlacementExecutor::storeChunkResult(AsyncChunkPlacementResult&& result) {
		m_chunkIndices[result.coord] = std::move(result.spatialIndex);
	}

	void PlacementExecutor::clear() {
		m_dependencyGraph.clear();
		m_spawnOrder.clear();
		m_chunkIndices.clear();
		m_initialized = false;
	}

} // namespace engine::assets
