#include "PlacementExecutor.h"

#include "assets/AssetRegistry.h"

#include <utils/Log.h>
#include <world/chunk/ChunkCoordinate.h>

#include <glm/vec2.hpp>

#include <algorithm>
#include <cmath>

namespace engine::assets {

	namespace {

		// Grove-density noise: a low-frequency, domain-warped value-noise field in [0,1]
		// that varies "spaced" flora (trees) so a forest reads as dense stands with both
		// rare open glades and rare extra-dense thickets, rather than an even orchard. A
		// pure function of world position + seed, so it is continuous across chunk seams.
		// The low tail thins to glades (via spawn chance); the high tail tightens spacing
		// into thickets (via minDistance). The broad middle is normal dense forest.
		constexpr float kGroveFeatureMeters = 105.0F; // grove/glade/thicket scale
		constexpr float kGladeSpawnMin = 0.12F;		  // glade floor: fraction of base spawn chance
		constexpr float kGladeBandLo = 0.20F;		  // raw noise below this -> full glade
		constexpr float kGladeBandHi = 0.42F;		  // above this -> normal forest density
		constexpr float kThicketBandLo = 0.70F;		  // raw noise above this -> thickets begin
		constexpr float kThicketBandHi = 0.90F;		  // ...fully a thicket core here (rare)
		constexpr float kThicketTighten = 0.45F;	  // thicket cores pack to (1 - this) of base minDistance

		float smooth01(float a, float b, float v) {
			const float t = std::clamp((v - a) / (b - a), 0.0F, 1.0F);
			return t * t * (3.0F - 2.0F * t);
		}

		float groveHash01(int64_t ix, int64_t iy, uint64_t seed) {
			uint64_t h = seed;
			h ^= static_cast<uint64_t>(ix) * 0x9E3779B97F4A7C15ull;
			h ^= static_cast<uint64_t>(iy) * 0xC2B2AE3D27D4EB4Full;
			h ^= h >> 29;
			h *= 0xBF58476D1CE4E5B9ull;
			h ^= h >> 32;
			return static_cast<float>(static_cast<double>(h >> 11) * (1.0 / 9007199254740992.0));
		}

		// Smooth bilinear value noise in [0,1] at lattice coords (x,y).
		float groveValueNoise(float x, float y, uint64_t seed) {
			const auto	x0 = static_cast<int64_t>(std::floor(x));
			const auto	y0 = static_cast<int64_t>(std::floor(y));
			const float fx = x - static_cast<float>(x0);
			const float fy = y - static_cast<float>(y0);
			const float sx = fx * fx * (3.0F - 2.0F * fx); // smoothstep
			const float sy = fy * fy * (3.0F - 2.0F * fy);
			const float n00 = groveHash01(x0, y0, seed);
			const float n10 = groveHash01(x0 + 1, y0, seed);
			const float n01 = groveHash01(x0, y0 + 1, seed);
			const float n11 = groveHash01(x0 + 1, y0 + 1, seed);
			const float nx0 = n00 + (n10 - n00) * sx;
			const float nx1 = n01 + (n11 - n01) * sx;
			return nx0 + (nx1 - nx0) * sy; // [0,1]
		}

		// Raw grove field in [0,1]: domain-warped two-octave value noise. The callers
		// map its tails to glades (low) and thickets (high).
		float groveValue(float worldX, float worldY, uint64_t seed) {
			float x = worldX / kGroveFeatureMeters;
			float y = worldY / kGroveFeatureMeters;
			// Domain-warp the sample point so the outlines are organic rather than
			// aligned to the noise lattice (which reads as square patches).
			const float wx = groveValueNoise(x * 0.6F + 3.1F, y * 0.6F + 1.7F, seed ^ 0x51u) - 0.5F;
			const float wy = groveValueNoise(x * 0.6F + 5.2F, y * 0.6F + 9.3F, seed ^ 0x52u) - 0.5F;
			x += wx * 1.1F;
			y += wy * 1.1F;
			// Two octaves so shapes aren't a single grid frequency.
			return 0.65F * groveValueNoise(x, y, seed) +
				   0.35F * groveValueNoise(x * 2.1F + 11.0F, y * 2.1F + 7.0F, seed ^ 0xA3u);
		}

	} // namespace

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
		for (const auto& defName : m_spawnOrder) {
			placeEntityType(defName, context, chunkIndex, adjacentProvider, rng, result.entities);
		}

		// Initialize resource counts for harvestable entities with resource pools
		for (const auto& entity : result.entities) {
			const auto* def = m_registry.getDefinition(entity.defName);
			if (def != nullptr && def->capabilities.harvestable.has_value()) {
				const auto& harv = def->capabilities.harvestable.value();
				if (harv.totalResourceMin > 0 && harv.totalResourceMax > 0) {
					std::uniform_int_distribution<uint32_t> resourceDist(harv.totalResourceMin, harv.totalResourceMax);
					uint32_t initialCount = resourceDist(rng);
					initResourceCount(context.coord, entity.position, entity.defName, initialCount);
				}
			}
		}

		result.entitiesPlaced = result.entities.size();
		LOG_DEBUG(
			Engine,
			"PlacementExecutor: chunk (%d, %d) - %zu entities",
			context.coord.x,
			context.coord.y,
			result.entitiesPlaced
		);
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
		// Random orientation only for assets that opt in (ground scatter); upright by default so
		// trees, grass, and other standing billboards are not tilted. Zero range yields 0 rotation.
		std::uniform_real_distribution<float> rotationDist(-def->maxRandomRotation, def->maxRandomRotation);
		std::uniform_real_distribution<float> scaleDist(0.8F, 1.2F);
		std::uniform_real_distribution<float> colorDist(-0.08F, 0.08F);

		// Spawn-point stride: sample one tile every N in each dimension. At stride 4
		// over a 512x512 chunk that's (512/4)^2 = 16,384 candidate tiles (each
		// jittered within its 4x4 block), down from 262K, while keeping coverage.
		constexpr uint16_t kTileStride = 4;

		// Jitter distribution to break up grid pattern (random offset 0 to stride-1)
		std::uniform_int_distribution<uint16_t> jitterDist(0, kTileStride - 1);

		// Iterate over sampled tiles in the chunk with jitter to break grid alignment
		for (uint16_t baseY = 0; baseY < world::kChunkSize; baseY += kTileStride) {
			for (uint16_t baseX = 0; baseX < world::kChunkSize; baseX += kTileStride) {
				// Add random jitter to each sample point
				uint16_t localX = baseX + jitterDist(rng);
				uint16_t localY = baseY + jitterDist(rng);

				// Clamp to chunk bounds
				if (localX >= world::kChunkSize)
					localX = world::kChunkSize - 1;
				if (localY >= world::kChunkSize)
					localY = world::kChunkSize - 1;
				// Get biome at this tile
				world::Biome biome = context.getBiome(localX, localY);
				std::string	 biomeName = world::biomeToString(biome);

				// Skip water tiles - entities should not spawn in water
				if (context.getSurface) {
					std::string surface = context.getSurface(localX, localY);
					if (surface == "Water") {
						continue;
					}
				}

				// Find placement config for this biome
				const BiomePlacement* bp = def->placement.findBiome(biomeName);
				if (bp == nullptr) {
					continue; // This entity doesn't spawn in this biome
				}

				// Check tile-type proximity ("near Water" etc)
				if (!bp->nearTileType.empty() && context.getSurface) {
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
								std::string surface =
									context.getSurface(static_cast<uint16_t>(checkX), static_cast<uint16_t>(checkY));
								if (surface == bp->nearTileType) {
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

				// Roll for spawn. Spaced flora (trees) sample the grove field: its low tail
				// thins out into open glades here (via spawn chance); its high tail tightens
				// spacing into denser thickets below (via minDistance). The middle is normal
				// dense forest.
				float groveG = 0.0F;
				float effectiveChance = bp->spawnChance;
				if (bp->distribution == Distribution::Spaced) {
					groveG = groveValue(tileWorldX, tileWorldY, context.worldSeed);
					const float gladeT = smooth01(kGladeBandLo, kGladeBandHi, groveG);
					effectiveChance *= kGladeSpawnMin + (1.0F - kGladeSpawnMin) * gladeT;
				}
				if (chanceDist(rng) >= effectiveChance) {
					continue; // Spawn chance failed
				}

				// Handle distribution types
				switch (bp->distribution) {
					case Distribution::Clumped: {
						// Generate clump center randomly within tile
						glm::vec2 clumpCenter{tileWorldX + offsetDist(rng), tileWorldY + offsetDist(rng)};

						// Clump parameters
						std::uniform_int_distribution<int32_t> clumpSizeDist(bp->clumping.clumpSizeMin, bp->clumping.clumpSizeMax);
						int32_t								   clumpSize = clumpSizeDist(rng);

						std::uniform_real_distribution<float> clumpRadiusDist(bp->clumping.clumpRadiusMin, bp->clumping.clumpRadiusMax);
						float								  clumpRadius = clumpRadiusDist(rng);

						// Spawn instances in clump
						for (int32_t i = 0; i < clumpSize; ++i) {
							// Random offset within clump radius
							std::uniform_real_distribution<float> clumpOffsetDist(-clumpRadius, clumpRadius);
							glm::vec2 position{clumpCenter.x + clumpOffsetDist(rng), clumpCenter.y + clumpOffsetDist(rng)};

							// Skip entities that would be placed on water
							// Convert world position back to local tile coordinates
							if (context.getSurface) {
								int entityLocalX = static_cast<int>(std::floor(position.x - origin.x));
								int entityLocalY = static_cast<int>(std::floor(position.y - origin.y));

								// Skip if outside chunk bounds - entity would be in adjacent chunk
								if (entityLocalX < 0 || entityLocalX >= world::kChunkSize ||
									entityLocalY < 0 || entityLocalY >= world::kChunkSize) {
									// Entity outside chunk bounds, skip it
									continue;
								}

								// Check for water at entity position
								std::string surface = context.getSurface(
									static_cast<uint16_t>(entityLocalX),
									static_cast<uint16_t>(entityLocalY)
								);
								if (surface == "Water") {
									LOG_DEBUG(Engine, "[Placement] Skipping clumped entity at (%d,%d) - Water surface", entityLocalX, entityLocalY);
									continue;
								}
							} else {
								LOG_WARNING(Engine, "[Placement] getSurface is NULL in clumped check!");
							}

							// Check relationship modifiers for this position
							float modifier = calculateRelationshipModifier(*def, position, chunkIndex, adjacentProvider);
							if (modifier <= 0.0F) {
								continue;
							}

							// Generate visual variation (brightness only, let SVG color show through)
							float brightnessVar = colorDist(rng);
							float brightness = 0.9F + brightnessVar; // Range ~0.82 to 0.98

							PlacedEntity entity;
							entity.defName = defName;
							entity.position = position;
							entity.rotation = rotationDist(rng);
							entity.scale = scaleDist(rng);
							entity.colorTint = glm::vec4(brightness, brightness, brightness, 1.0F);

							chunkIndex.insert(entity);
							outEntities.push_back(entity);
						}
						break;
					}

					case Distribution::Spaced: {
						// Poisson-style spacing: a single candidate per sampled tile,
						// rejected if another of the same type already sits within
						// minDistance. (Previously this fell through to Uniform, so
						// minDistance did nothing and "spaced" forests placed randomly.)
						glm::vec2 position{tileWorldX + offsetDist(rng), tileWorldY + offsetDist(rng)};

						// Skip water / out-of-chunk, same as Uniform.
						if (context.getSurface) {
							int entityLocalX = static_cast<int>(std::floor(position.x - origin.x));
							int entityLocalY = static_cast<int>(std::floor(position.y - origin.y));
							if (entityLocalX < 0 || entityLocalX >= world::kChunkSize ||
								entityLocalY < 0 || entityLocalY >= world::kChunkSize) {
								break;
							}
							std::string surface = context.getSurface(
								static_cast<uint16_t>(entityLocalX), static_cast<uint16_t>(entityLocalY));
							if (surface == "Water") {
								break;
							}
						}

						// Enforce minimum spacing against already-placed instances of this
						// type in the chunk (makes a "spaced" stand pack to a natural canopy
						// instead of random clumps). In thicket cores (grove field high tail)
						// the spacing tightens so small patches read denser than the forest.
						float effMinDist = bp->spacing.minDistance;
						if (effMinDist > 0.0F) {
							const float thicketT = smooth01(kThicketBandLo, kThicketBandHi, groveG);
							effMinDist *= 1.0F - kThicketTighten * thicketT;
							if (chunkIndex.hasNearby(position, effMinDist, defName)) {
								break;
							}
						}

						// Relationship modifiers (cross-species avoidance/affinity).
						float modifier = calculateRelationshipModifier(*def, position, chunkIndex, adjacentProvider);
						if (modifier <= 0.0F) {
							break;
						}

						float brightnessVar = colorDist(rng);
						float brightness = 0.9F + brightnessVar;

						PlacedEntity entity;
						entity.defName = defName;
						entity.position = position;
						entity.rotation = rotationDist(rng);
						entity.scale = scaleDist(rng);
						entity.colorTint = glm::vec4(brightness, brightness, brightness, 1.0F);

						chunkIndex.insert(entity);
						outEntities.push_back(entity);
						break;
					}

					case Distribution::Uniform:
					default: {
						// Single entity at random position within tile
						glm::vec2 position{tileWorldX + offsetDist(rng), tileWorldY + offsetDist(rng)};

						// Skip entities that would be placed on water
						// Convert world position back to local tile coordinates
						if (context.getSurface) {
							int entityLocalX = static_cast<int>(std::floor(position.x - origin.x));
							int entityLocalY = static_cast<int>(std::floor(position.y - origin.y));

							// Skip if outside chunk bounds - entity would be in adjacent chunk
							if (entityLocalX < 0 || entityLocalX >= world::kChunkSize ||
								entityLocalY < 0 || entityLocalY >= world::kChunkSize) {
								break;
							}

							// Check for water at entity position
							std::string surface = context.getSurface(
								static_cast<uint16_t>(entityLocalX),
								static_cast<uint16_t>(entityLocalY)
							);
							if (surface == "Water") {
								break;
							}
						}

						// Check relationship modifiers
						float modifier = calculateRelationshipModifier(*def, position, chunkIndex, adjacentProvider);
						if (modifier <= 0.0F) {
							continue;
						}

						// Generate visual variation (brightness only, let SVG color show through)
						float brightnessVar = colorDist(rng);
						float brightness = 0.9F + brightnessVar; // Range ~0.82 to 0.98

						PlacedEntity entity;
						entity.defName = defName;
						entity.position = position;
						entity.rotation = rotationDist(rng);
						entity.scale = scaleDist(rng);
						entity.colorTint = glm::vec4(brightness, brightness, brightness, 1.0F);

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

	AsyncChunkPlacementResult
	PlacementExecutor::computeChunkEntities(const ChunkPlacementContext& context, const IAdjacentChunkProvider* adjacentProvider) const {
		AsyncChunkPlacementResult result;
		result.coord = context.coord;
		result.worldSeed = context.worldSeed;

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
		for (const auto& defName : m_spawnOrder) {
			placeEntityType(defName, context, result.spatialIndex, adjacentProvider, rng, result.entities);
		}

		result.entitiesPlaced = result.entities.size();
		return result;
	}

	void PlacementExecutor::storeChunkResult(AsyncChunkPlacementResult&& result) {
		// Initialize resource counts for harvestable entities with resource pools
		// Create RNG from world seed and chunk coordinates for deterministic results
		// (must match the seeding in processChunk for consistency)
		uint64_t chunkSeed = result.worldSeed;
		chunkSeed ^= static_cast<uint64_t>(result.coord.x) * 0x9E3779B97F4A7C15ULL;
		chunkSeed ^= static_cast<uint64_t>(result.coord.y) * 0x6C62272E07BB0143ULL;
		std::mt19937 rng(static_cast<uint32_t>(chunkSeed));

		for (const auto& entity : result.entities) {
			const auto* def = m_registry.getDefinition(entity.defName);
			if (def != nullptr && def->capabilities.harvestable.has_value()) {
				const auto& harv = def->capabilities.harvestable.value();
				if (harv.totalResourceMin > 0 && harv.totalResourceMax > 0) {
					std::uniform_int_distribution<uint32_t> resourceDist(harv.totalResourceMin, harv.totalResourceMax);
					uint32_t initialCount = resourceDist(rng);
					initResourceCount(result.coord, entity.position, entity.defName, initialCount);
				}
			}
		}

		m_chunkIndices[result.coord] = std::move(result.spatialIndex);
	}

	void PlacementExecutor::clear() {
		m_dependencyGraph.clear();
		m_spawnOrder.clear();
		m_chunkIndices.clear();
		m_cooldowns.clear();
		m_resourceCounts.clear();
		m_initialized = false;
	}

	PlacementExecutor::CooldownKey PlacementExecutor::makeCooldownKey(
		world::ChunkCoordinate coord,
		glm::vec2 position,
		const std::string& defName
	) {
		// Quantize position to tile coordinates for reliable hashing
		// Using floor to ensure consistent quantization
		return CooldownKey{
			coord,
			static_cast<int32_t>(std::floor(position.x)),
			static_cast<int32_t>(std::floor(position.y)),
			defName
		};
	}

	bool PlacementExecutor::removeEntity(world::ChunkCoordinate coord, glm::vec2 position, const std::string& defName) {
		auto it = m_chunkIndices.find(coord);
		if (it == m_chunkIndices.end()) {
			LOG_WARNING(Engine, "PlacementExecutor::removeEntity: Chunk (%d, %d) not found", coord.x, coord.y);
			return false;
		}

		bool removed = it->second.remove(position, defName);
		if (removed) {
			// A removed flora obstacle changes the nav obstacle set: bump the epoch so the
			// NavigationSystem rebuilds the covering region and reclaims the trunk hole as
			// walkable ground.
			++m_removalEpoch;
			LOG_DEBUG(
				Engine,
				"PlacementExecutor: Removed entity %s at (%.1f, %.1f) in chunk (%d, %d)",
				defName.c_str(),
				position.x,
				position.y,
				coord.x,
				coord.y
			);
		}
		return removed;
	}

	void PlacementExecutor::setEntityCooldown(world::ChunkCoordinate coord, glm::vec2 position,
											  const std::string& defName, float cooldownSeconds) {
		auto key = makeCooldownKey(coord, position, defName);

		// O(1) insert or update
		auto [iter, inserted] = m_cooldowns.insert_or_assign(key, cooldownSeconds);

		if (inserted) {
			LOG_DEBUG(
				Engine,
				"PlacementExecutor: Set cooldown for %s at (%.1f, %.1f) for %.1fs",
				defName.c_str(),
				position.x,
				position.y,
				cooldownSeconds
			);
		} else {
			LOG_DEBUG(
				Engine,
				"PlacementExecutor: Updated cooldown for %s at (%.1f, %.1f) to %.1fs",
				defName.c_str(),
				position.x,
				position.y,
				cooldownSeconds
			);
		}
	}

	bool PlacementExecutor::isEntityOnCooldown(world::ChunkCoordinate coord, glm::vec2 position,
											   const std::string& defName) const {
		auto key = makeCooldownKey(coord, position, defName);
		return m_cooldowns.find(key) != m_cooldowns.end();  // O(1) lookup
	}

	void PlacementExecutor::updateCooldowns(float deltaTime) {
		// Update all cooldowns and remove expired ones
		for (auto it = m_cooldowns.begin(); it != m_cooldowns.end(); ) {
			it->second -= deltaTime;
			if (it->second <= 0.0F) {
				LOG_DEBUG(
					Engine,
					"PlacementExecutor: Cooldown expired for %s at tile (%d, %d)",
					it->first.defName.c_str(),
					it->first.tileX,
					it->first.tileY
				);
				it = m_cooldowns.erase(it);
			} else {
				++it;
			}
		}
	}

	void PlacementExecutor::initResourceCount(world::ChunkCoordinate coord, glm::vec2 position,
											  const std::string& defName, uint32_t count) {
		auto key = makeCooldownKey(coord, position, defName);
		m_resourceCounts[key] = count;
		LOG_DEBUG(
			Engine,
			"PlacementExecutor: Initialized resource count for %s at (%.1f, %.1f) = %u",
			defName.c_str(),
			position.x,
			position.y,
			count
		);
	}

	std::optional<uint32_t> PlacementExecutor::getResourceCount(world::ChunkCoordinate coord, glm::vec2 position,
																const std::string& defName) const {
		auto key = makeCooldownKey(coord, position, defName);
		auto it = m_resourceCounts.find(key);
		if (it != m_resourceCounts.end()) {
			return it->second;
		}
		return std::nullopt;
	}

	uint32_t PlacementExecutor::decrementResourceCount(world::ChunkCoordinate coord, glm::vec2 position,
													   const std::string& defName, uint32_t requested) {
		auto key = makeCooldownKey(coord, position, defName);
		auto it = m_resourceCounts.find(key);
		if (it == m_resourceCounts.end()) {
			// No resource tracking for this entity - nothing to withdraw
			return 0;
		}

		const uint32_t removed = std::min(requested, it->second);
		it->second -= removed;

		if (it->second == 0) {
			m_resourceCounts.erase(it);
			LOG_DEBUG(
				Engine, "PlacementExecutor: Resource depleted for %s at (%.1f, %.1f)", defName.c_str(), position.x, position.y
			);
		} else {
			LOG_DEBUG(
				Engine,
				"PlacementExecutor: Withdrew %u from %s at (%.1f, %.1f), remaining = %u",
				removed,
				defName.c_str(),
				position.x,
				position.y,
				it->second
			);
		}
		return removed;
	}

} // namespace engine::assets
