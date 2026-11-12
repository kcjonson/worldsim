#include "PlacementExecutor.h"

#include "world/generation/GroveField.h"

#include "assets/AssetRegistry.h"
#include "assets/TreeTraits.h"

#include <assets/placement/WorldDepthSort.h> // meshYExtent, computeAnchorY
#include <utils/Log.h>
#include <world/chunk/Chunk.h> // Surface, surfaceToString
#include <world/chunk/ChunkCoordinate.h>

#include <glm/vec2.hpp>

#include <algorithm>
#include <cmath>

namespace assets {

	namespace {

		// Half the mesh's width across the view: for a tree, its crown's radius.
		float crownRadius(const renderer::TessellatedMesh* mesh) {
			if (mesh == nullptr || mesh->vertices.empty()) {
				return 0.0F;
			}
			float minX = mesh->vertices.front().x;
			float maxX = minX;
			for (const auto& v : mesh->vertices) {
				minX = std::min(minX, v.x);
				maxX = std::max(maxX, v.x);
			}
			return 0.5F * (maxX - minX);
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
		CrownRadii crowns;
		for (const auto& defName : m_spawnOrder) {
			placeEntityType(defName, context, chunkIndex, adjacentProvider, rng, crowns, result.entities);
		}

		// Initialize resource counts for harvestable entities with resource pools
		for (const auto& entity : result.entities) {
			const auto* def = m_registry.getDefinition(entity.defName);
			if (def != nullptr && def->capabilities.harvestable.has_value()) {
				const auto& harv = def->capabilities.harvestable.value();
				if (harv.totalResourceMin > 0 && harv.totalResourceMax > 0) {
					std::uniform_int_distribution<uint32_t> resourceDist(harv.totalResourceMin, harv.totalResourceMax);
					uint32_t initialCount = resourceDist(rng);
					initResourceCount(entity.position, entity.defName, initialCount);
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
		CrownRadii&					  crowns,
		std::vector<PlacedEntity>&	  outEntities
	) const {
		const auto* def = m_registry.getDefinition(defName);
		if (def == nullptr) {
			return;
		}

		// Resolve this def's 2.5D depth-sort inputs once per type and template, not per
		// placed instance and not per frame in WorldDepthGather: whether the def routes
		// tall (one answer for every instance, D9), and per template the id the renderers
		// key by and its local-Y extent for the anchor. The registry is mutex-guarded and
		// builds outside its lock, so this is safe on the placement worker.
		AssetRegistry& registry = AssetRegistry::Get();
		const bool	   isTall = registry.isTallDef(defName);
		struct ResolvedTemplate {
			uint32_t			id = 0;
			assets::MeshYExtent extent;
			bool				resolved = false;
		};
		// One slot per (age class, variant), indexed by the class's first slot plus the variant.
		std::vector<uint32_t> classFirstSlot(std::max<size_t>(def->ageClasses.size(), 1), 0);
		size_t				  slotCount = def->ageClasses.empty() ? std::max<uint32_t>(def->variantCount, 1U) : 0;
		for (size_t a = 0; a < def->ageClasses.size(); ++a) {
			classFirstSlot[a] = static_cast<uint32_t>(slotCount);
			slotCount += def->ageClasses[a].variants;
		}
		std::vector<ResolvedTemplate> templates(slotCount);

		// Stamp a candidate's traits and template from its position (drawing nothing from rng),
		// so spacing can read its crown before anything is drawn for it.
		auto resolve = [&](PlacedEntity& entity) -> const ResolvedTemplate& {
			entity.traits = computeTreeTraits(*def, entity.position, context.worldSeed);
			ResolvedTemplate& tmpl = templates[classFirstSlot[entity.traits.ageClass] + entity.traits.variant];
			if (!tmpl.resolved) {
				tmpl.id = registry.templateId(defName, entity.traits);
				const renderer::TessellatedMesh* mesh = registry.getTemplate(tmpl.id);
				tmpl.extent = assets::meshYExtent(mesh);
				tmpl.resolved = true;
				if (def->placement.crownSpacing > 0.0F && tmpl.id != 0) {
					const float radius = crownRadius(mesh);
					crowns.byTemplate[tmpl.id] = radius;
					crowns.largest = std::max(crowns.largest, radius);
				}
			}
			entity.templateId = tmpl.id;
			return tmpl;
		};

		// Whether a crown-spaced candidate stands too close to a crown-spaced instance already
		// in the chunk (PlacementParams::crownSpacing), with the spacing scaled by `tighten`.
		auto crowded = [&](const PlacedEntity& candidate, float tighten) {
			const auto found = crowns.byTemplate.find(candidate.templateId);
			if (found == crowns.byTemplate.end() || found->second <= 0.0F) {
				return false;
			}
			const float r = found->second;
			const float k = def->placement.crownSpacing * tighten;
			for (const PlacedEntity* other : chunkIndex.queryRadius(candidate.position, k * (r + crowns.largest))) {
				const auto otherCrown = crowns.byTemplate.find(other->templateId);
				if (otherCrown == crowns.byTemplate.end() || otherCrown->second <= 0.0F) {
					continue;
				}
				const float		small = std::min(r, otherCrown->second);
				const float		large = std::max(r, otherCrown->second);
				const float		need = k * (r + otherCrown->second) * (0.5F + 0.5F * small / large);
				const glm::vec2 d = other->position - candidate.position;
				if (d.x * d.x + d.y * d.y < need * need) {
					return true;
				}
			}
			return false;
		};

		// Stamp a placed instance's traits, template, and depth attributes, then file it in
		// the chunk index and the result list. anchorY + isTallOccluder then ride along on
		// the frozen index copies for the gather to read directly. Traits draw nothing from rng.
		auto record = [&](PlacedEntity& entity, const ResolvedTemplate& tmpl) {
			entity.anchorY = assets::computeAnchorY(
				entity.position.y, assets::anchorLocalY(def, entity.traits.ageClass, tmpl.extent), entity.scale
			);
			entity.isTallOccluder = isTall;
			chunkIndex.insert(entity);
			outEntities.push_back(entity);
		};

		// Get chunk origin for world position calculation
		world::WorldPosition origin = context.coord.origin();

		// An instance belongs to this chunk only inside its square (beyond it, to the
		// neighbor), and never stands in water.
		auto isPlaceable = [&context, &origin](glm::vec2 position) {
			const float localX = position.x - origin.x;
			const float localY = position.y - origin.y;
			const auto	size   = static_cast<float>(world::kChunkSize);
			return localX >= 0.0F && localY >= 0.0F && localX < size && localY < size && !context.isWater(position);
		};

		// RNG distributions
		std::uniform_real_distribution<float> chanceDist(0.0F, 1.0F);
		std::uniform_real_distribution<float> offsetDist(0.0F, 1.0F); // Position within tile
		// Random orientation only for assets that opt in (ground scatter); upright by default so
		// trees, grass, and other standing billboards are not tilted. Zero range yields 0 rotation.
		std::uniform_real_distribution<float> rotationDist(-def->maxRandomRotation, def->maxRandomRotation);
		std::uniform_real_distribution<float> scaleDist(def->placement.scale.min, def->placement.scale.max);
		std::uniform_real_distribution<float> colorDist(-0.08F, 0.08F);

		// Spawn-point stride: one jittered tile per samplePitch-sized block. At the default 4 a
		// 512x512 chunk tries (512/4)^2 = 16,384 tiles, down from 262K, while keeping coverage.
		const uint16_t kTileStride = def->placement.samplePitch;

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

				// Calculate tile world position (corner)
				float tileWorldX = origin.x + static_cast<float>(localX);
				float tileWorldY = origin.y + static_cast<float>(localY);

				// Skip water tiles - entities should not spawn in water
				if (context.isWater({tileWorldX + 0.5F, tileWorldY + 0.5F})) {
					continue;
				}

				// Find placement config for this biome
				const BiomePlacement* bp = def->placement.findBiome(biomeName);
				if (bp == nullptr) {
					continue; // This entity doesn't spawn in this biome
				}

				// Surface proximity ("near Mud" etc): the surface drawn at the tile centers
				// around this one, water where the polygons hold it, else the painted land.
				if (!bp->nearTileType.empty()) {
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
								const glm::vec2		 center{origin.x + static_cast<float>(checkX) + 0.5F, origin.y + static_cast<float>(checkY) + 0.5F};
								const world::Surface surface = context.isWater(center) ? world::Surface::Water : context.landSurface(center);
								if (world::surfaceToString(surface) == bp->nearTileType) {
									foundNearby = true;
								}
							}
						}
					}

					if (!foundNearby) {
						continue;
					}
				}

				// Roll for spawn. Spaced flora (trees) sample the grove field: its low tail
				// thins out into open glades here (via spawn chance), rising to stand cores, and
				// the gap field opens small gaps inside stands; its high tail tightens spacing
				// into denser thickets below (via minDistance).
				float groveG = 0.0F;
				float effectiveChance = bp->spawnChance;
				if (bp->distribution == Distribution::Spaced) {
					groveG = world::grove::groveValue(tileWorldX, tileWorldY, context.worldSeed);
					effectiveChance *= world::grove::spawnScale(groveG, tileWorldX, tileWorldY, context.worldSeed);
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
							if (!isPlaceable(position)) {
								continue;
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

							record(entity, resolve(entity));
						}
						break;
					}

					case Distribution::Spaced: {
						// Poisson-style spacing: a single candidate per sampled tile,
						// rejected if another of the same type already sits within
						// minDistance. (Previously this fell through to Uniform, so
						// minDistance did nothing and "spaced" forests placed randomly.)
						glm::vec2 position{tileWorldX + offsetDist(rng), tileWorldY + offsetDist(rng)};
						if (!isPlaceable(position)) {
							break;
						}

						// Enforce minimum spacing against already-placed instances of this
						// type in the chunk, and crown spacing against every crown-spaced
						// instance of any type (makes a "spaced" stand pack to a natural canopy
						// instead of random clumps). In thicket cores (grove field high tail)
						// both tighten so small patches read denser than the forest.
						const float tighten = 1.0F - world::grove::kThicketTighten * world::grove::thicketWeight(groveG);
						const float effMinDist = bp->spacing.minDistance * tighten;
						if (effMinDist > 0.0F && chunkIndex.hasNearby(position, effMinDist, defName)) {
							break;
						}
						PlacedEntity entity;
						entity.position = position;
						const ResolvedTemplate& tmpl = resolve(entity);
						if (crowded(entity, tighten)) {
							break;
						}

						// Relationship modifiers (cross-species avoidance/affinity).
						float modifier = calculateRelationshipModifier(*def, position, chunkIndex, adjacentProvider);
						if (modifier <= 0.0F) {
							break;
						}

						float brightnessVar = colorDist(rng);
						float brightness = 0.9F + brightnessVar;

						entity.defName = defName;
						entity.rotation = rotationDist(rng);
						entity.scale = scaleDist(rng);
						entity.colorTint = glm::vec4(brightness, brightness, brightness, 1.0F);

						record(entity, tmpl);
						break;
					}

					case Distribution::Uniform:
					default: {
						// Single entity at random position within tile
						glm::vec2 position{tileWorldX + offsetDist(rng), tileWorldY + offsetDist(rng)};
						if (!isPlaceable(position)) {
							break;
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

						record(entity, resolve(entity));
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
		CrownRadii crowns;
		for (const auto& defName : m_spawnOrder) {
			placeEntityType(defName, context, result.spatialIndex, adjacentProvider, rng, crowns, result.entities);
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
					initResourceCount(entity.position, entity.defName, initialCount);
				}
			}
		}

		m_chunkIndices[result.coord] = std::move(result.spatialIndex);
	}

	void PlacementExecutor::clear() {
		m_dependencyGraph.clear();
		m_spawnOrder.clear();
		m_chunkIndices.clear();
		m_regrowthReadyAt.clear();
		m_regrowthSweepSize = kMinRegrowthSweepSize;
		m_resourceCounts.clear();
		m_initialized = false;
	}

	PlacementExecutor::EntityStateKey PlacementExecutor::makeEntityStateKey(glm::vec2 position, const std::string& defName) const {
		return makeEntityStateKey(position, m_registry.getDefNameId(defName));
	}

	PlacementExecutor::EntityStateKey PlacementExecutor::makeEntityStateKey(glm::vec2 position, uint32_t defNameId) {
		return EntityStateKey{decimeterCell(position), defNameId};
	}

	bool PlacementExecutor::removeEntity(world::ChunkCoordinate coord, glm::vec2 position, const std::string& defName) {
		auto it = m_chunkIndices.find(coord);
		if (it == m_chunkIndices.end()) {
			LOG_WARNING(Engine, "PlacementExecutor::removeEntity: Chunk (%d, %d) not found", coord.x, coord.y);
			return false;
		}

		const PlacedEntity* found = it->second.find(position, defName);
		if (found == nullptr) {
			return false;
		}
		// The log records the stored position: the match allows 1 mm of slack, and the entry is
		// how the nav mesh finds the entity's ring.
		const glm::vec2 stored = found->position;
		it->second.remove(stored, defName);
		forgetEntityState(stored, defName);
		m_removals.push_back({coord, stored, defName});
		LOG_DEBUG(
			Engine,
			"PlacementExecutor: Removed entity %s at (%.1f, %.1f) in chunk (%d, %d)",
			defName.c_str(),
			position.x,
			position.y,
			coord.x,
			coord.y
		);
		return true;
	}

	void PlacementExecutor::forgetEntityState(glm::vec2 position, const std::string& defName) {
		const EntityStateKey key = makeEntityStateKey(position, defName);
		m_regrowthReadyAt.erase(key);
		m_resourceCounts.erase(key);
	}

	void PlacementExecutor::startRegrowth(glm::vec2 position, uint32_t defNameId, double now, double seconds) {
		if (m_regrowthReadyAt.size() >= m_regrowthSweepSize) {
			std::erase_if(m_regrowthReadyAt, [now](const auto& entry) { return entry.second <= now; });
			m_regrowthSweepSize = std::max(kMinRegrowthSweepSize, 2 * m_regrowthReadyAt.size());
		}
		m_regrowthReadyAt.insert_or_assign(makeEntityStateKey(position, defNameId), now + seconds);
		LOG_DEBUG(
			Engine,
			"PlacementExecutor: %s at (%.1f, %.1f) regrows for %.1fs",
			m_registry.getDefName(defNameId).c_str(),
			position.x,
			position.y,
			seconds
		);
	}

	std::optional<double> PlacementExecutor::regrowthReadyAt(glm::vec2 position, uint32_t defNameId, double now) {
		const auto it = m_regrowthReadyAt.find(makeEntityStateKey(position, defNameId));
		if (it == m_regrowthReadyAt.end()) {
			return std::nullopt;
		}
		if (it->second <= now) {
			m_regrowthReadyAt.erase(it);
			return std::nullopt;
		}
		return it->second;
	}

	void PlacementExecutor::initResourceCount(glm::vec2 position, const std::string& defName, uint32_t count) {
		m_resourceCounts[makeEntityStateKey(position, defName)] = count;
		LOG_DEBUG(
			Engine,
			"PlacementExecutor: Initialized resource count for %s at (%.1f, %.1f) = %u",
			defName.c_str(),
			position.x,
			position.y,
			count
		);
	}

	std::optional<uint32_t> PlacementExecutor::getResourceCount(glm::vec2 position, const std::string& defName) const {
		auto it = m_resourceCounts.find(makeEntityStateKey(position, defName));
		if (it != m_resourceCounts.end()) {
			return it->second;
		}
		return std::nullopt;
	}

	uint32_t PlacementExecutor::decrementResourceCount(glm::vec2 position, const std::string& defName, uint32_t requested) {
		auto it = m_resourceCounts.find(makeEntityStateKey(position, defName));
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

} // namespace assets
