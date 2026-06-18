#include "VisionSystem.h"

#include "../World.h"
#include "../components/Appearance.h"
#include "../components/Knowledge.h"
#include "../components/Memory.h"
#include "../components/Transform.h"

#include "assets/AssetDefinition.h"
#include "assets/AssetRegistry.h"
#include "assets/RecipeRegistry.h"
#include "assets/placement/PlacementExecutor.h"
#include "world/chunk/Chunk.h"
#include "world/chunk/ChunkManager.h"

#include "nav/NavCoords.h"

#include <predicates/Predicates.h>

#include <utils/Log.h>

#include <cmath>

namespace ecs {

	namespace {
		// Synthetic definition name for shore tiles (land adjacent to water)
		// Shore tiles are where colonists stand to drink from water
		constexpr const char* kShoreTileDefName = "Terrain_Shore";

		/// Check if learning a new defNameId unlocks any recipes
		/// @param knowledge The colonist's knowledge (after learning)
		/// @param newlyLearnedId The defNameId that was just learned
		/// @param registry Asset registry for ID lookups
		/// @param recipeRegistry Recipe registry to check recipes
		/// @return Label of newly unlocked recipe, or empty if none
		std::string checkForRecipeUnlock(
			const Knowledge& knowledge,
			uint32_t newlyLearnedId,
			const engine::assets::AssetRegistry& registry,
			const engine::assets::RecipeRegistry& recipeRegistry
		) {
			// Only check non-innate recipes (innate recipes are always available)
			// A recipe is "newly unlocked" when the colonist now knows all inputs,
			// and the newly-learned item was the final missing input
			for (const auto& [defName, recipe] : recipeRegistry.allRecipes()) {
				if (recipe.innate) {
					continue;
				}

				// Check if this recipe has the newly learned item as an input
				bool hasNewInput = false;
				std::vector<uint32_t> inputIds;
				for (const auto& input : recipe.inputs) {
					uint32_t inputId = registry.getDefNameId(input.defName);
					inputIds.push_back(inputId);
					if (inputId == newlyLearnedId) {
						hasNewInput = true;
					}
				}

				// Skip recipes that don't involve the newly learned item
				if (!hasNewInput) {
					continue;
				}

				// Check if all inputs are now known
				if (knowledge.knowsAll(inputIds)) {
					return recipe.label;
				}
			}
			return "";
		}
	} // namespace

	void VisionSystem::ensureTerrainDefinitionsRegistered() {
		if (m_terrainDefsRegistered) {
			return;
		}

		auto& registry = engine::assets::AssetRegistry::Get();

		// Create capability mask for shore (Drinkable - colonists drink AT the shore)
		m_shoreTileCapabilityMask = (1 << static_cast<uint8_t>(engine::assets::CapabilityType::Drinkable));

		// Register synthetic shore tile definition
		m_shoreTileDefNameId = registry.registerSyntheticDefinition(kShoreTileDefName, m_shoreTileCapabilityMask);

		m_terrainDefsRegistered = true;
	}

	void VisionSystem::update(float /*deltaTime*/) {
		// Throttle: only run every N frames to reduce CPU overhead
		// Colonists move ~2-3 tiles/second, so 12 updates/sec is plenty
		m_frameCounter++;
		if (m_frameCounter < m_updateInterval) {
			return;
		}
		m_frameCounter = 0;

		// Refresh the wall-occluder cache before any observer reads it (version-gated,
		// so this is a no-op when the construction graph hasn't changed). With no
		// construction world wired the index stays empty and every observer below
		// takes the outdoor fast path.
		m_geometry.rebuildIfStale();

		// Register terrain definitions on first update
		ensureTerrainDefinitionsRegistered();

		// Chunk size in world units (meters, since kTileSize = 1.0F)
		constexpr float kChunkWorldSize = static_cast<float>(engine::world::kChunkSize);

		auto& registry = engine::assets::AssetRegistry::Get();
		auto& recipeRegistry = engine::assets::RecipeRegistry::Get();

		// Scratch reused across observers to avoid per-observer allocation.
		std::vector<geometry::OccluderSegment> occluderScratch;

		// Iterate all entities with Position and Memory components
		for (auto [entity, pos, memory] : world->view<Position, Memory>()) {
			// Get optional Knowledge component for permanent discovery tracking
			auto* knowledge = world->getComponent<Knowledge>(entity);

			// Calculate bounding box of vision in world coordinates
			float minX = pos.value.x - memory.sightRadius;
			float maxX = pos.value.x + memory.sightRadius;
			float minY = pos.value.y - memory.sightRadius;
			float maxY = pos.value.y + memory.sightRadius;

			// Convert to chunk coordinate range
			int32_t chunkMinX = static_cast<int32_t>(std::floor(minX / kChunkWorldSize));
			int32_t chunkMaxX = static_cast<int32_t>(std::floor(maxX / kChunkWorldSize));
			int32_t chunkMinY = static_cast<int32_t>(std::floor(minY / kChunkWorldSize));
			int32_t chunkMaxY = static_cast<int32_t>(std::floor(maxY / kChunkWorldSize));

			float sightRadiusSq = memory.sightRadius * memory.sightRadius;

			// --- Occlusion gate setup (per observer) ---
			//
			// Find the opaque wall occluders within this observer's sight radius. The
			// meters->mm boundary is crossed here (and only here) via NavCoords.
			const std::int64_t	   radiusMm = std::llround(static_cast<double>(memory.sightRadius) * 1000.0);
			const geometry::Vec2i64 observerMm = engine::nav::toMm(pos.value);
			m_geometry.queryOccluders(observerMm, radiusMm, occluderScratch);
			const bool outdoors = occluderScratch.empty();

			// Outdoor fast path: no occluder anywhere in range means no polygon and no
			// gate -- the radius test alone decides visibility, exactly as before walls
			// existed. This is the common case and must stay as cheap as the (already
			// cheap) occluder query. Only when a wall is nearby do we build/lookup a
			// visibility polygon and gate candidates against it.
			VisibilityCache* cache = nullptr;
			if (!outdoors) {
				VisibilityCache& entry = m_visibilityCache[entity];
				entry.seenThisTick = true;

				// Rebuild conditions (D3): no usable polygon yet, the wall set changed
				// (GeometryIndex generation moved), or the observer moved more than ~0.5 m
				// from where the polygon was built. A stationary indoor colonist hits none
				// of these and reuses last tick's polygon.
				const float	  dxc = pos.value.x - entry.builtPos.x;
				const float	  dyc = pos.value.y - entry.builtPos.y;
				const bool	  moved = (dxc * dxc + dyc * dyc) > (0.5F * 0.5F);
				const bool	  staleGen = entry.builtVersion != m_geometry.generation();
				const bool	  noPolygon = !entry.hadOccluders;
				const bool	  polygonRebuilt = moved || staleGen || noPolygon;
				if (polygonRebuilt) {
					entry.polygon = geometry::computeVisibilityPolygon(observerMm, radiusMm, occluderScratch);
					entry.builtPos = pos.value;
					entry.builtVersion = m_geometry.generation();
					entry.hadOccluders = true;
					++m_polygonBuildCount;
				}
				cache = &entry;

				// Pass 0: structure-as-observable (vision-architecture D4). A wall or
				// opening that bounds or intersects the visibility polygon is seen, and
				// its stable construction id enters memory. Runs only for indoor
				// observers (a polygon exists here), over built structures radius-culled
				// to dozens; the per-edge crossing test is O(structures * ring edges) but
				// both are small. Deterministic: set inserts are order-independent.
				//
				// Only when the polygon was (re)built this tick: a reused polygon sees the
				// same structures, already in memory, so re-scanning every tick is wasted.
				// New structures appear only via a geometry change, which bumps the
				// generation and forces a rebuild here.
				if (polygonRebuilt) {
					const auto& ring = cache->polygon;

					// A structure point [p0,p1] is seen if either endpoint lies in/on the
					// polygon, or the span crosses a ring edge. The crossing fallback matters
					// because a wall IS (part of) the polygon boundary: a long wall facing the
					// observer can have both endpoints out of sight radius while its middle is
					// the boundary seen along, which an endpoint-only test would miss.
					auto structureSeen = [&](geometry::Vec2i64 p0, geometry::Vec2i64 p1) -> bool {
						if (geometry::pointInPolygon(p0, ring) != geometry::PointInPolygon::Outside ||
							geometry::pointInPolygon(p1, ring) != geometry::PointInPolygon::Outside) {
							return true;
						}
						for (std::size_t i = 0; i < ring.size(); ++i) {
							const geometry::Vec2i64& e0 = ring[i];
							const geometry::Vec2i64& e1 = ring[(i + 1) % ring.size()];
							if (geometry::intersectSegments(p0, p1, e0, e1).relation != geometry::SegmentRelation::Disjoint) {
								return true;
							}
						}
						return false;
					};

					for (const auto& seg : m_geometry.builtSegments()) {
						if (!geometry::withinDistanceOfSegment(observerMm, seg.a, seg.b, radiusMm)) {
							continue;
						}
						if (structureSeen(seg.a, seg.b)) {
							memory.rememberSegment(seg.id);
						}
					}

					for (const auto& op : m_geometry.builtOpenings()) {
						if (!geometry::withinDistanceOfSegment(observerMm, op.jambA, op.jambB, radiusMm)) {
							continue;
						}
						if (structureSeen(op.jambA, op.jambB)) {
							memory.rememberOpening(op.openingId);
						}
					}
				}
			}

			// A candidate already inside the sight radius is visible iff it is not
			// strictly outside the visibility polygon (Inside or OnBoundary count as
			// seen). Outdoors there is no polygon, so everything in radius is visible.
			auto visible = [&](const glm::vec2& candidatePos) -> bool {
				if (cache == nullptr) {
					return true;
				}
				return geometry::pointInPolygon(engine::nav::toMm(candidatePos), cache->polygon)
					   != geometry::PointInPolygon::Outside;
			};

			// Pass 1: placed entities from world generation (needs placement data wired).
			if (m_placementExecutor != nullptr && m_processedChunks != nullptr) {
				// Query each potentially visible chunk for placed entities
				for (int32_t cy = chunkMinY; cy <= chunkMaxY; ++cy) {
					for (int32_t cx = chunkMinX; cx <= chunkMaxX; ++cx) {
						engine::world::ChunkCoordinate coord{cx, cy};

						// Only query processed chunks for placed entities
						if (m_processedChunks->find(coord) == m_processedChunks->end()) {
							continue;
						}

						const auto* chunkIndex = m_placementExecutor->getChunkIndex(coord);
						if (chunkIndex == nullptr) {
							continue;
						}

						// Query entities within sight radius from this chunk
						auto nearbyEntities = chunkIndex->queryRadius(pos.value, memory.sightRadius);

						// Remember each discovered entity
						for (const auto* placedEntity : nearbyEntities) {
							// Occlusion gate: skip entities a wall hides.
							if (!visible(placedEntity->position)) {
								continue;
							}

							// Get defNameId and capability mask for registry notification
							uint32_t defNameId = registry.getDefNameId(placedEntity->defName);
							if (defNameId == 0) {
								continue;
							}
							uint8_t capabilityMask = registry.getCapabilityMask(defNameId);

							// Remember in colonist's memory (returns true only for NEW discoveries)
							bool isNewDiscovery = memory.rememberWorldEntity(placedEntity->position, defNameId, capabilityMask);

							// Update permanent knowledge if Knowledge component exists
							if (isNewDiscovery && knowledge != nullptr && knowledge->learn(defNameId)) {
								// New discovery - check for recipe unlocks
								std::string unlockedRecipe = checkForRecipeUnlock(*knowledge, defNameId, registry, recipeRegistry);
								if (!unlockedRecipe.empty() && m_onRecipeDiscovery) {
									m_onRecipeDiscovery(unlockedRecipe);
								}
							}
						}
					}
				}
			}

			// Pass 2: ECS entities with Appearance (e.g., bio piles created by ActionSystem).
			// These are runtime-spawned entities, as opposed to those placed during world generation.
			for (auto [otherEntity, otherPos, appearance] : world->view<Position, Appearance>()) {
				// Don't "see" yourself
				if (otherEntity == entity) {
					continue;
				}

				// Check if within sight radius
				float dx = otherPos.value.x - pos.value.x;
				float dy = otherPos.value.y - pos.value.y;
				if (dx * dx + dy * dy <= sightRadiusSq) {
					// Occlusion gate: skip entities a wall hides.
					if (!visible(otherPos.value)) {
						continue;
					}

					// Get defNameId and capability mask from registry
					uint32_t defNameId = registry.getDefNameId(appearance.defName);
					if (defNameId != 0) {
						uint8_t capabilityMask = registry.getCapabilityMask(defNameId);

						// Remember in colonist's memory (returns true only for NEW discoveries)
						bool isNewDiscovery = memory.rememberWorldEntity(otherPos.value, defNameId, capabilityMask);

						// Update permanent knowledge if Knowledge component exists
						if (isNewDiscovery && knowledge != nullptr && knowledge->learn(defNameId)) {
							// New discovery - check for recipe unlocks
							std::string unlockedRecipe = checkForRecipeUnlock(*knowledge, defNameId, registry, recipeRegistry);
							if (!unlockedRecipe.empty() && m_onRecipeDiscovery) {
								m_onRecipeDiscovery(unlockedRecipe);
							}
						}
					}
				}
			}

			// Pass 3: shore tiles using pre-cached shore tile positions.
			// Shore tiles are pre-computed during chunk generation for O(N) lookup
			// instead of iterating all ~3600 tiles in vision range every frame.
			if (m_chunkManager != nullptr && m_shoreTileDefNameId != 0) {
				for (int32_t cy = chunkMinY; cy <= chunkMaxY; ++cy) {
					for (int32_t cx = chunkMinX; cx <= chunkMaxX; ++cx) {
						engine::world::ChunkCoordinate coord{cx, cy};

						const auto* chunk = m_chunkManager->getChunk(coord);
						if (chunk == nullptr || !chunk->isReady()) {
							continue;
						}

						auto origin = chunk->worldOrigin();

						// Use cached shore tiles instead of iterating all tiles
						for (const auto& [localX, localY] : chunk->getShoreTiles()) {
							glm::vec2 shoreWorldPos{
								origin.x + static_cast<float>(localX) + 0.5F, origin.y + static_cast<float>(localY) + 0.5F
							};

							// Check if within sight radius
							float dx = shoreWorldPos.x - pos.value.x;
							float dy = shoreWorldPos.y - pos.value.y;
							if (dx * dx + dy * dy <= sightRadiusSq) {
								// Occlusion gate: skip shore a wall hides.
								if (!visible(shoreWorldPos)) {
									continue;
								}

								memory.rememberWorldEntity(shoreWorldPos, m_shoreTileDefNameId, m_shoreTileCapabilityMask);

								// Update permanent knowledge for shore tiles
								if (knowledge != nullptr && knowledge->learn(m_shoreTileDefNameId)) {
									// New discovery - check for recipe unlocks (unlikely for shore tiles, but consistent)
									std::string unlockedRecipe = checkForRecipeUnlock(*knowledge, m_shoreTileDefNameId, registry, recipeRegistry);
									if (!unlockedRecipe.empty() && m_onRecipeDiscovery) {
										m_onRecipeDiscovery(unlockedRecipe);
									}
								}
							}
						}
					}
				}
			}
		}

		// Prune cache entries for observers not seen this tick (dead/despawned, or moved
		// outdoors so no longer carry an entry). Mark-and-sweep keeps the map bounded.
		for (auto it = m_visibilityCache.begin(); it != m_visibilityCache.end();) {
			if (it->second.seenThisTick) {
				it->second.seenThisTick = false; // reset for next tick
				++it;
			} else {
				it = m_visibilityCache.erase(it);
			}
		}
	}

} // namespace ecs
