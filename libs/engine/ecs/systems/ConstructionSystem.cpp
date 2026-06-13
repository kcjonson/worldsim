#include "ConstructionSystem.h"

#include "../GoalTaskRegistry.h"
#include "../World.h"
#include "../components/Inventory.h"
#include "../components/Structure.h"
#include "../components/Transform.h"

#include <construction/ConstructionWorld.h>

#include <assets/AssetRegistry.h>
#include <assets/placement/PlacementExecutor.h>
#include <core/Vec2i64.h>
#include <world/chunk/ChunkCoordinate.h>

#include <utils/Log.h>

#include <algorithm>
#include <cmath>

namespace ecs {

	namespace {
		// Chain id source for linking Harvest -> Haul material chains, matching
		// CraftingGoalSystem's local generator (continuity bonus for the cutter-hauler).
		uint64_t generateChainId() {
			static uint64_t nextChainId = 1;
			return nextChainId++;
		}
	} // namespace

	ConstructionDecision decideConstructionPhase(
		const StructureBlueprint& blueprint,
		bool					  footprintClear,
		bool					  materialsComplete
	) {
		ConstructionDecision decision;

		// A blueprint slated for demolition is owned by the Deconstruct path; emit nothing
		// and leave the phase where it is.
		if (blueprint.demolishing) {
			decision.nextPhase = blueprint.phase;
			return decision;
		}

		// Already done: no goals, stay Complete.
		if (blueprint.phase == StructureBlueprint::BuildPhase::Complete) {
			decision.nextPhase = StructureBlueprint::BuildPhase::Complete;
			return decision;
		}

		if (!footprintClear) {
			decision.nextPhase	   = StructureBlueprint::BuildPhase::Clearing;
			decision.emitClearGoals = true;
			return decision;
		}

		if (!materialsComplete) {
			decision.nextPhase		   = StructureBlueprint::BuildPhase::AwaitingMaterials;
			decision.emitMaterialGoals = true;
			return decision;
		}

		decision.nextPhase	  = StructureBlueprint::BuildPhase::UnderConstruction;
		decision.emitBuildGoal = true;
		return decision;
	}

	void ConstructionSystem::update(float /*deltaTime*/) {
		if (world == nullptr) {
			return;
		}

		m_frameCounter++;
		if (m_frameCounter < kUpdateFrameInterval) {
			return;
		}
		m_frameCounter = 0;

		auto& registry = GoalTaskRegistry::Get();

		// Track which blueprint entities currently own goals so we can clean up goals for
		// blueprints that vanished or completed (BuildGoalSystem's cleanup shape). A blueprint
		// can own several goals (clear Harvest, a Harvest+Haul chain per material, a Build), so
		// this is the set of destination entities, not goal ids.
		std::unordered_set<EntityID> entitiesWithGoals;
		for (const auto* goal : registry.getGoalsByOwner(GoalOwner::ConstructionGoalSystem)) {
			entitiesWithGoals.insert(goal->destinationEntity);
		}

		m_activeBlueprintCount = 0;

		for (auto [entity, structure, blueprint] : world->view<Structure, StructureBlueprint>()) {
			// Only foundations carry a footprint in the ConstructionWorld for now.
			if (structure.kind != StructureKind::Foundation) {
				continue;
			}

			const uint64_t foundationId = structure.graphId;

			// A completed or demolishing blueprint emits nothing; leave it in the stale set so
			// the cleanup pass below drops any lingering goals (the Build goal never self-retires
			// via delivery). Completion itself is flipped by ActionSystem's callback.
			if (blueprint.phase == StructureBlueprint::BuildPhase::Complete || blueprint.demolishing) {
				continue;
			}

			// Reconcile delivered[] from the on-site inventory before gating on materials.
			reconcileDelivered(entity, blueprint);

			const bool footprintClear	= isFootprintClear(foundationId);
			const bool materialsDone	= blueprint.materialsComplete();

			const ConstructionDecision decision =
				decideConstructionPhase(blueprint, footprintClear, materialsDone);

			// Advance the phase (idempotent). ActionSystem flips Complete itself on the last
			// Build tick, so we never downgrade out of Complete here.
			if (blueprint.phase != StructureBlueprint::BuildPhase::Complete) {
				blueprint.phase = decision.nextPhase;
			}

			if (decision.emitClearGoals) {
				emitClearGoals(entity, foundationId);
			} else if (decision.emitMaterialGoals) {
				emitMaterialGoals(entity, blueprint);
			} else if (decision.emitBuildGoal) {
				emitBuildGoal(entity, blueprint);
			}

			entitiesWithGoals.erase(entity);
			m_activeBlueprintCount++;
		}

		// Any construction-owned goals left over belong to blueprints that disappeared
		// (demolished, completed and removed). Drop them and their children.
		for (EntityID stale : entitiesWithGoals) {
			const auto* goal = registry.getGoalByDestination(stale);
			while (goal != nullptr) {
				registry.removeGoalWithChildren(goal->id);
				goal = registry.getGoalByDestination(stale);
			}
		}
	}

	bool ConstructionSystem::isFootprintClear(uint64_t foundationId) const {
		if (m_constructionWorld == nullptr || m_placementExecutor == nullptr || m_processedChunks == nullptr) {
			// No placement data wired: treat as clear so the lifecycle still progresses
			// (e.g. unit/headless contexts). Clearing is a no-op without a world to query.
			return true;
		}

		const auto aabb = m_constructionWorld->footprintAabb(foundationId);
		const float minX = geometry::dequantize(aabb.min).x;
		const float minY = geometry::dequantize(aabb.min).y;
		const float maxX = geometry::dequantize(aabb.max).x;
		const float maxY = geometry::dequantize(aabb.max).y;

		auto& assetRegistry = engine::assets::AssetRegistry::Get();

		constexpr float kChunkWorldSize = static_cast<float>(engine::world::kChunkSize);
		const int32_t chunkMinX = static_cast<int32_t>(std::floor(minX / kChunkWorldSize));
		const int32_t chunkMaxX = static_cast<int32_t>(std::floor(maxX / kChunkWorldSize));
		const int32_t chunkMinY = static_cast<int32_t>(std::floor(minY / kChunkWorldSize));
		const int32_t chunkMaxY = static_cast<int32_t>(std::floor(maxY / kChunkWorldSize));

		for (int32_t cy = chunkMinY; cy <= chunkMaxY; ++cy) {
			for (int32_t cx = chunkMinX; cx <= chunkMaxX; ++cx) {
				engine::world::ChunkCoordinate coord{cx, cy};
				if (m_processedChunks->find(coord) == m_processedChunks->end()) {
					continue;
				}
				const auto* chunkIndex = m_placementExecutor->getChunkIndex(coord);
				if (chunkIndex == nullptr) {
					continue;
				}
				for (const auto* placed : chunkIndex->queryRect(minX, minY, maxX, maxY)) {
					const uint32_t defNameId = assetRegistry.getDefNameId(placed->defName);
					if (defNameId == 0) {
						continue;
					}
					if (assetRegistry.hasCapability(defNameId, engine::assets::CapabilityType::Harvestable)) {
						return false; // a choppable blocker sits on the footprint
					}
				}
			}
		}
		return true;
	}

	void ConstructionSystem::reconcileDelivered(EntityID blueprintEntity, StructureBlueprint& blueprint) {
		const auto* inventory = world->getComponent<Inventory>(blueprintEntity);
		if (inventory == nullptr) {
			return; // no delivery inventory yet; delivered[] stays as-is
		}

		// delivered[] mirrors the on-site inventory exactly: it is derived, never the source
		// of truth, so rebuild it from the inventory each tick. Only required materials matter.
		blueprint.delivered.clear();
		for (const auto& [defName, qty] : blueprint.required) {
			const uint32_t have = inventory->getQuantity(defName);
			if (have > 0) {
				blueprint.delivered.emplace_back(defName, have);
			}
		}
	}

	void ConstructionSystem::emitClearGoals(EntityID blueprintEntity, uint64_t foundationId) {
		if (m_constructionWorld == nullptr || m_placementExecutor == nullptr || m_processedChunks == nullptr) {
			return;
		}

		auto& registry		= GoalTaskRegistry::Get();
		auto& assetRegistry = engine::assets::AssetRegistry::Get();

		// One clear Harvest goal per blueprint is enough: it requests the yield of the blockers
		// (e.g. Wood from trees), which AIDecision serves against any matching harvestable on the
		// site. Chopping clears the footprint AND feeds the Wood manifest. We point destination at
		// the blueprint so the goal is owned/cleaned alongside it; the actual harvest target is a
		// tree the colonist knows about.
		if (registry.getGoalByDestination(blueprintEntity) != nullptr) {
			return; // already has a goal of some kind for this entity
		}

		// Find the dominant yield among footprint blockers so the Harvest goal has a concrete
		// yieldDefNameId for AIDecision to match.
		const auto aabb = m_constructionWorld->footprintAabb(foundationId);
		const float minX = geometry::dequantize(aabb.min).x;
		const float minY = geometry::dequantize(aabb.min).y;
		const float maxX = geometry::dequantize(aabb.max).x;
		const float maxY = geometry::dequantize(aabb.max).y;

		uint32_t  yieldDefNameId = 0;
		glm::vec2 blockerPos{(minX + maxX) * 0.5F, (minY + maxY) * 0.5F};

		constexpr float kChunkWorldSize = static_cast<float>(engine::world::kChunkSize);
		const int32_t chunkMinX = static_cast<int32_t>(std::floor(minX / kChunkWorldSize));
		const int32_t chunkMaxX = static_cast<int32_t>(std::floor(maxX / kChunkWorldSize));
		const int32_t chunkMinY = static_cast<int32_t>(std::floor(minY / kChunkWorldSize));
		const int32_t chunkMaxY = static_cast<int32_t>(std::floor(maxY / kChunkWorldSize));

		for (int32_t cy = chunkMinY; cy <= chunkMaxY && yieldDefNameId == 0; ++cy) {
			for (int32_t cx = chunkMinX; cx <= chunkMaxX && yieldDefNameId == 0; ++cx) {
				engine::world::ChunkCoordinate coord{cx, cy};
				if (m_processedChunks->find(coord) == m_processedChunks->end()) {
					continue;
				}
				const auto* chunkIndex = m_placementExecutor->getChunkIndex(coord);
				if (chunkIndex == nullptr) {
					continue;
				}
				for (const auto* placed : chunkIndex->queryRect(minX, minY, maxX, maxY)) {
					const uint32_t defNameId = assetRegistry.getDefNameId(placed->defName);
					if (defNameId == 0) {
						continue;
					}
					const auto* def = assetRegistry.getDefinition(placed->defName);
					if (def != nullptr && def->capabilities.harvestable.has_value()) {
						yieldDefNameId = assetRegistry.getDefNameId(def->capabilities.harvestable->yieldDefName);
						blockerPos	   = placed->position;
						break;
					}
				}
			}
		}

		if (yieldDefNameId == 0) {
			return; // nothing concrete to clear (race with removal); next tick re-evaluates
		}

		GoalTask clearGoal;
		clearGoal.type				= TaskType::Harvest;
		clearGoal.owner				= GoalOwner::ConstructionGoalSystem;
		clearGoal.destinationEntity = blueprintEntity;
		clearGoal.destinationPosition = blockerPos;
		clearGoal.acceptedDefNameIds = {yieldDefNameId};
		clearGoal.targetAmount		= 1; // clear at least one blocker per pass; re-emitted while not clear
		clearGoal.yieldDefNameId	= yieldDefNameId;
		clearGoal.status			= GoalStatus::Available;
		registry.createGoal(std::move(clearGoal));
		LOG_DEBUG(
			Engine,
			"[Construction] Emitted clear Harvest goal for blueprint %u (yield %u)",
			static_cast<uint32_t>(blueprintEntity),
			yieldDefNameId
		);
	}

	void ConstructionSystem::emitMaterialGoals(EntityID blueprintEntity, const StructureBlueprint& blueprint) {
		auto& registry		= GoalTaskRegistry::Get();
		auto& assetRegistry = engine::assets::AssetRegistry::Get();

		const auto* position = world->getComponent<Position>(blueprintEntity);
		const glm::vec2 sitePos = position != nullptr ? position->value : glm::vec2{0.0F, 0.0F};

		// A clear-Harvest goal from the previous phase points at the site too. It is harmless
		// (it requests the same Wood), so we leave it; the per-material goals below are keyed by
		// type + accepted id, and the loose set of construction goals for this entity is rebuilt
		// against the live `remaining` each pass.
		//
		// Model: one Harvest goal and one Haul goal per material, BOTH Available, NO dependency.
		// The colonist chops a tree (Wood into inventory, harvest goal credited), then the
		// inventory-source Haul (which AIDecision only surfaces while carrying) brings it to the
		// site and deposits into the blueprint Inventory. Yields are random, so a strict
		// harvest-all-then-haul gate would make colonists hoard; the open chain lets each trip
		// deliver. `remaining` is recomputed from the on-site inventory, so goals self-retire as
		// material lands. Goals are sized/refreshed in place to avoid churn.

		// Find this entity's existing construction goal by (type, accepted id) so we update
		// rather than duplicate.
		auto findGoal = [&](TaskType type, uint32_t defNameId) -> const GoalTask* {
			for (const auto* g : registry.getGoalsByOwner(GoalOwner::ConstructionGoalSystem)) {
				if (g->destinationEntity == blueprintEntity && g->type == type && !g->acceptedDefNameIds.empty() &&
					g->acceptedDefNameIds.front() == defNameId) {
					return g;
				}
			}
			return nullptr;
		};

		for (const auto& [defName, requiredQty] : blueprint.required) {
			const uint32_t remaining = blueprint.remaining(defName);
			const uint32_t defNameId = assetRegistry.getDefNameId(defName);
			if (defNameId == 0) {
				continue;
			}

			const GoalTask* harvest = findGoal(TaskType::Harvest, defNameId);
			const GoalTask* haul	= findGoal(TaskType::Haul, defNameId);

			if (remaining == 0) {
				// Material satisfied: retire its goals.
				if (harvest != nullptr) {
					registry.removeGoal(harvest->id);
				}
				if (haul != nullptr) {
					registry.removeGoal(haul->id);
				}
				continue;
			}

			if (harvest != nullptr) {
				registry.updateGoal(harvest->id, [&](GoalTask& g) {
					g.targetAmount	  = remaining;
					g.deliveredAmount = 0;
					g.status		  = GoalStatus::Available;
				});
			} else {
				GoalTask harvestGoal;
				harvestGoal.type			  = TaskType::Harvest;
				harvestGoal.owner			  = GoalOwner::ConstructionGoalSystem;
				harvestGoal.destinationEntity = blueprintEntity;
				harvestGoal.destinationPosition = sitePos;
				harvestGoal.acceptedDefNameIds = {defNameId};
				harvestGoal.targetAmount	  = remaining;
				harvestGoal.yieldDefNameId	  = defNameId;
				harvestGoal.status			  = GoalStatus::Available;
				registry.createGoal(std::move(harvestGoal));
			}

			if (haul != nullptr) {
				registry.updateGoal(haul->id, [&](GoalTask& g) {
					g.targetAmount	  = remaining;
					g.deliveredAmount = 0;
					g.status		  = GoalStatus::Available;
				});
			} else {
				GoalTask haulGoal;
				haulGoal.type				 = TaskType::Haul;
				haulGoal.owner				 = GoalOwner::ConstructionGoalSystem;
				haulGoal.destinationEntity	 = blueprintEntity;
				haulGoal.destinationPosition = sitePos;
				haulGoal.acceptedDefNameIds	 = {defNameId};
				haulGoal.targetAmount		 = remaining;
				haulGoal.status				 = GoalStatus::Available;
				registry.createGoal(std::move(haulGoal));
			}

			LOG_DEBUG(
				Engine,
				"[Construction] Material goals for blueprint %u: %u x %s outstanding",
				static_cast<uint32_t>(blueprintEntity),
				remaining,
				defName.c_str()
			);
		}
	}

	void ConstructionSystem::emitBuildGoal(EntityID blueprintEntity, const StructureBlueprint& blueprint) {
		auto& registry = GoalTaskRegistry::Get();

		const auto* position = world->getComponent<Position>(blueprintEntity);
		const glm::vec2 sitePos = position != nullptr ? position->value : glm::vec2{0.0F, 0.0F};

		const auto* existing = registry.getGoalByDestination(blueprintEntity);
		if (existing != nullptr) {
			if (existing->type == TaskType::Build) {
				return; // build goal already active
			}
			// A leftover material goal: materials are done, replace it with the Build goal.
			registry.removeGoalWithChildren(existing->id);
		}

		GoalTask buildGoal;
		buildGoal.type				 = TaskType::Build;
		buildGoal.owner				 = GoalOwner::ConstructionGoalSystem;
		buildGoal.destinationEntity	 = blueprintEntity;
		buildGoal.destinationPosition = sitePos;
		buildGoal.targetAmount		 = 1; // a single goal; multiple colonists can all work it
		buildGoal.status			 = GoalStatus::Available;
		registry.createGoal(std::move(buildGoal));

		LOG_DEBUG(
			Engine,
			"[Construction] Emitted Build goal for blueprint %u (work %.0f/%.0f)",
			static_cast<uint32_t>(blueprintEntity),
			static_cast<double>(blueprint.workDone),
			static_cast<double>(blueprint.workTotal)
		);
	}

} // namespace ecs
