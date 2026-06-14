#include "ConstructionSystem.h"

#include "../GoalTaskRegistry.h"
#include "../World.h"
#include "../components/Inventory.h"
#include "../components/Needs.h"
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

	uint32_t constructionHarvestDemand(uint32_t remaining, uint32_t carried, uint32_t carryCapacity) {
		// Per-trip target: chop up to what the site still needs, but never more than one
		// trip's worth can carry. A colonist's stack caps at carryCapacity, so for a manifest
		// larger than a stack `carried` can never reach `remaining`; capping demand at
		// `remaining` alone would keep the Harvest goal Available forever and the colonist
		// would hoard at the stack cap instead of delivering. Bounding by min(remaining,
		// carryCapacity) means once carried fills a trip, demand drops to 0, the Harvest goal
		// retires, and the Haul wins so the load is delivered. Empty hands with work
		// outstanding gives demand > 0 again for the next trip.
		const uint32_t tripTarget = std::min(remaining, carryCapacity);
		return carried >= tripTarget ? 0U : tripTarget - carried;
	}

	ConstructionDecision decideConstructionPhase(const StructureBlueprint& blueprint, bool footprintClear, bool materialsComplete) {
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
			decision.nextPhase = StructureBlueprint::BuildPhase::Clearing;
			decision.emitClearGoals = true;
			return decision;
		}

		if (!materialsComplete) {
			decision.nextPhase = StructureBlueprint::BuildPhase::AwaitingMaterials;
			decision.emitMaterialGoals = true;
			return decision;
		}

		decision.nextPhase = StructureBlueprint::BuildPhase::UnderConstruction;
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
			// Foundations, walls, and openings all flow through; rooms are not blueprints yet.
			if (structure.kind != StructureKind::Foundation && structure.kind != StructureKind::Wall &&
				structure.kind != StructureKind::Opening) {
				continue;
			}
			const bool isWall = (structure.kind == StructureKind::Wall);
			const bool isOpening = (structure.kind == StructureKind::Opening);

			const uint64_t graphId = structure.graphId;

			// A completed or demolishing blueprint emits nothing; leave it in the stale set so
			// the cleanup pass below drops any lingering goals (the Build goal never self-retires
			// via delivery). Completion itself is flipped by ActionSystem's callback.
			if (blueprint.phase == StructureBlueprint::BuildPhase::Complete || blueprint.demolishing) {
				continue;
			}

			// DEV/TEST free-build: drive every non-Complete blueprint straight to Built, skipping
			// clear/haul/build entirely. Inert unless the flag is set, so normal play is untouched.
			// The entity is left in the stale set so its leftover goals are dropped by the cleanup
			// pass below; completion fires the SAME callback a normal build uses.
			if (m_freeBuild) {
				completeBlueprintNow(entity, blueprint);
				continue;
			}

			// Host gate: a wall waits on its host foundation being Built; an opening waits
			// on its host wall segment being Built. While gated the blueprint holds nothing
			// but a Blocked umbrella, and no haul/build goals emit (design: Walls /
			// Prerequisites; openings sit on a built wall). This is the front of the pipeline.
			const bool hostGated = (isWall && !isWallHostBuilt(graphId)) || (isOpening && !isOpeningHostSegmentBuilt(graphId));
			if (hostGated) {
				ensureUmbrellaGoal(entity, blueprint, GoalStatus::Blocked);
				entitiesWithGoals.erase(entity);
				m_activeBlueprintCount++;
				continue;
			}

			// Reconcile delivered[] from the on-site inventory before gating on materials.
			reconcileDelivered(entity, blueprint);

			// Walls and openings have no clear phase: they sit on a cleared, built host, so
			// the footprint is clear by construction. Foundations query their footprint.
			const bool footprintClear = isWall || isOpening || isFootprintClear(graphId);
			const bool materialsDone = blueprint.materialsComplete();

			const ConstructionDecision decision = decideConstructionPhase(blueprint, footprintClear, materialsDone);

			// Advance the phase (idempotent). ActionSystem flips Complete itself on the last
			// Build tick, so we never downgrade out of Complete here.
			if (blueprint.phase != StructureBlueprint::BuildPhase::Complete) {
				blueprint.phase = decision.nextPhase;
			}

			// One umbrella Build goal per blueprint owns the destination slot and parents every
			// phase goal. It is Available only once materials are staged (UnderConstruction),
			// matching evaluateBuildOptions' gate; otherwise Blocked so builders aren't dispatched
			// while the site still needs clearing or materials. The umbrella persists across phases
			// so the child Harvest/Haul goals never collide on the single-top-level-per-destination
			// guard.
			const GoalStatus umbrellaStatus = decision.emitBuildGoal ? GoalStatus::Available : GoalStatus::Blocked;
			const uint64_t	 umbrellaId = ensureUmbrellaGoal(entity, blueprint, umbrellaStatus);

			if (decision.emitClearGoals) {
				emitClearGoals(entity, graphId, umbrellaId);
			} else if (decision.emitMaterialGoals) {
				// Footprint is clear: the clear-Harvest child is obsolete, drop it; keep the
				// per-material Harvest/Haul children sized against the live manifest.
				retireChildGoals(entity, umbrellaId, /*keepMaterialChildren=*/true);
				emitMaterialGoals(entity, blueprint, umbrellaId);
			} else if (decision.emitBuildGoal) {
				// Materials staged: no child goals remain relevant; the umbrella is now Available
				// and ActionSystem advances workDone directly.
				retireChildGoals(entity, umbrellaId, /*keepMaterialChildren=*/false);
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

		const auto	aabb = m_constructionWorld->footprintAabb(foundationId);
		const float minX = geometry::dequantize(aabb.min).x;
		const float minY = geometry::dequantize(aabb.min).y;
		const float maxX = geometry::dequantize(aabb.max).x;
		const float maxY = geometry::dequantize(aabb.max).y;

		auto& assetRegistry = engine::assets::AssetRegistry::Get();

		constexpr float kChunkWorldSize = static_cast<float>(engine::world::kChunkSize);
		const int32_t	chunkMinX = static_cast<int32_t>(std::floor(minX / kChunkWorldSize));
		const int32_t	chunkMaxX = static_cast<int32_t>(std::floor(maxX / kChunkWorldSize));
		const int32_t	chunkMinY = static_cast<int32_t>(std::floor(minY / kChunkWorldSize));
		const int32_t	chunkMaxY = static_cast<int32_t>(std::floor(maxY / kChunkWorldSize));

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

	bool ConstructionSystem::isWallHostBuilt(uint64_t segmentId) const {
		if (m_constructionWorld == nullptr) {
			// No topology wired (headless/unit context): ungate so the lifecycle still runs.
			return true;
		}
		const auto* segment = m_constructionWorld->getSegment(segmentId);
		if (segment == nullptr) {
			return false; // unknown segment: keep it gated until the topology catches up
		}
		const auto* host = m_constructionWorld->get(segment->hostFoundation);
		// A freestanding wall (no host) is ungated; a hosted wall waits for Built.
		if (host == nullptr) {
			return segment->hostFoundation == engine::construction::kInvalidFoundation;
		}
		return host->state == engine::construction::FoundationState::Built;
	}

	bool ConstructionSystem::isOpeningHostSegmentBuilt(uint64_t openingId) const {
		if (m_constructionWorld == nullptr) {
			// No topology wired (headless/unit context): ungate so the lifecycle still runs.
			return true;
		}
		const auto* opening = m_constructionWorld->getOpening(openingId);
		if (opening == nullptr) {
			return false; // unknown opening: keep it gated until the topology catches up
		}
		const auto* segment = m_constructionWorld->getSegment(opening->segment);
		if (segment == nullptr) {
			return false; // unknown host segment: stay gated
		}
		return segment->state == engine::construction::FoundationState::Built;
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

	void ConstructionSystem::emitClearGoals(EntityID blueprintEntity, uint64_t foundationId, uint64_t umbrellaGoalId) {
		if (m_constructionWorld == nullptr || m_placementExecutor == nullptr || m_processedChunks == nullptr) {
			return;
		}

		auto& registry = GoalTaskRegistry::Get();
		auto& assetRegistry = engine::assets::AssetRegistry::Get();

		// One clear Harvest CHILD per blueprint is enough: it requests the yield of the blockers
		// (e.g. Wood from trees), which AIDecision serves against any matching harvestable on the
		// site. Chopping clears the footprint AND feeds the Wood manifest. The child hangs under
		// the umbrella (parentGoalId set) so it is owned/cleaned alongside the blueprint without
		// fighting the umbrella for the single-top-level-per-destination slot; the actual harvest
		// target is a tree the colonist knows about.
		for (const auto* g : registry.getGoalsByOwner(GoalOwner::ConstructionGoalSystem)) {
			if (g->destinationEntity == blueprintEntity && g->type == TaskType::Harvest && g->parentGoalId.has_value() &&
				g->parentGoalId.value() == umbrellaGoalId) {
				return; // a clear/material Harvest child is already active for this blueprint
			}
		}

		// Find the dominant yield among footprint blockers so the Harvest goal has a concrete
		// yieldDefNameId for AIDecision to match.
		const auto	aabb = m_constructionWorld->footprintAabb(foundationId);
		const float minX = geometry::dequantize(aabb.min).x;
		const float minY = geometry::dequantize(aabb.min).y;
		const float maxX = geometry::dequantize(aabb.max).x;
		const float maxY = geometry::dequantize(aabb.max).y;

		uint32_t  yieldDefNameId = 0;
		glm::vec2 blockerPos{(minX + maxX) * 0.5F, (minY + maxY) * 0.5F};

		constexpr float kChunkWorldSize = static_cast<float>(engine::world::kChunkSize);
		const int32_t	chunkMinX = static_cast<int32_t>(std::floor(minX / kChunkWorldSize));
		const int32_t	chunkMaxX = static_cast<int32_t>(std::floor(maxX / kChunkWorldSize));
		const int32_t	chunkMinY = static_cast<int32_t>(std::floor(minY / kChunkWorldSize));
		const int32_t	chunkMaxY = static_cast<int32_t>(std::floor(maxY / kChunkWorldSize));

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
						blockerPos = placed->position;
						break;
					}
				}
			}
		}

		if (yieldDefNameId == 0) {
			return; // nothing concrete to clear (race with removal); next tick re-evaluates
		}

		GoalTask clearGoal;
		clearGoal.type = TaskType::Harvest;
		clearGoal.owner = GoalOwner::ConstructionGoalSystem;
		clearGoal.destinationEntity = blueprintEntity;
		clearGoal.destinationPosition = blockerPos;
		clearGoal.acceptedDefNameIds = {yieldDefNameId};
		clearGoal.targetAmount = 1; // clear at least one blocker per pass; re-emitted while not clear
		clearGoal.yieldDefNameId = yieldDefNameId;
		clearGoal.parentGoalId = umbrellaGoalId; // child of the umbrella: skips the destination guard
		clearGoal.status = GoalStatus::Available;
		registry.createGoal(std::move(clearGoal));
		LOG_DEBUG(
			Engine,
			"[Construction] Emitted clear Harvest goal for blueprint %u (yield %u)",
			static_cast<uint32_t>(blueprintEntity),
			yieldDefNameId
		);
	}

	void ConstructionSystem::emitMaterialGoals(EntityID blueprintEntity, const StructureBlueprint& blueprint, uint64_t umbrellaGoalId) {
		auto& registry = GoalTaskRegistry::Get();
		auto& assetRegistry = engine::assets::AssetRegistry::Get();

		const auto*		position = world->getComponent<Position>(blueprintEntity);
		const glm::vec2 sitePos = position != nullptr ? position->value : glm::vec2{0.0F, 0.0F};

		// The clear-Harvest child from the previous phase was already retired by the caller once
		// the footprint went clear. The per-material goals below are keyed by type + accepted id,
		// and the set of construction children for this entity is rebuilt against the live
		// `remaining` each pass. Every child parents to the umbrella so a multi-material site
		// carries one Harvest + one Haul child per material without colliding on the destination.
		//
		// Model: one Harvest goal and one Haul goal per material, BOTH Available, NO dependency.
		// The colonist chops a tree (Wood into inventory), then the inventory-source Haul (which
		// AIDecision only surfaces while carrying) brings it to the site and deposits into the
		// blueprint Inventory. Yields are random, so a strict harvest-all-then-haul gate would
		// make colonists hoard; the open chain lets each trip deliver.
		//
		// Two corrections over a naive refresh:
		//  - Harvest demand is bounded by what colonists already CARRY toward the site AND by one
		//    trip's carry capacity, not just the site shortfall. Without this, the Harvest goal
		//    stays full-size while the colonist is already carrying a full load, so chopping (a
		//    near tree, with a skill bonus) keeps out-scoring delivery (a far site, no skill bonus)
		//    and the load never gets delivered. For a manifest larger than one stack the colonist's
		//    `carried` caps at the stack size and can never reach `remaining`, so bounding by
		//    `remaining` alone would stall forever; bounding by min(remaining, carryCapacity)
		//    retires the Harvest goal once a trip's worth is in hand and lets the Haul win.
		//  - The Harvest and Haul share a stable chainId. selectTaskFromTrace tags the Haul as
		//    chain step 1, so once the colonist commits to delivering, the chain-continuation
		//    bonus keeps it on the trip instead of flip-flopping back to harvest each AI tick.
		// `remaining` and `carried` are recomputed each pass, so the goals self-size as material
		// is chopped and lands on site.

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

		// One trip's carry capacity for this site, shared across all of its materials.
		const uint32_t carryCapacity = colonistCarryCapacity(blueprintEntity);

		for (const auto& [defName, requiredQty] : blueprint.required) {
			const uint32_t remaining = blueprint.remaining(defName);
			const uint32_t defNameId = assetRegistry.getDefNameId(defName);
			if (defNameId == 0) {
				continue;
			}

			const GoalTask* harvest = findGoal(TaskType::Harvest, defNameId);
			const GoalTask* haul = findGoal(TaskType::Haul, defNameId);

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

			// One stable chainId per (blueprint, material), preserved across refreshes so the
			// chain-continuation bonus doesn't churn. Reuse whichever goal already carries it.
			uint64_t chainId = 0;
			if (haul != nullptr && haul->chainId.has_value()) {
				chainId = haul->chainId.value();
			} else if (harvest != nullptr && harvest->chainId.has_value()) {
				chainId = harvest->chainId.value();
			} else {
				chainId = generateChainId();
			}

			const uint32_t carried = carriedAmount(blueprintEntity, defName);
			const uint32_t harvestDemand = constructionHarvestDemand(remaining, carried, carryCapacity);

			if (harvestDemand == 0) {
				// Colonists carry a full trip's worth (or enough to finish the site): stop
				// chopping so the load gets delivered. The Haul goal below stays open until
				// material lands on site.
				if (harvest != nullptr) {
					registry.removeGoal(harvest->id);
				}
			} else if (harvest != nullptr) {
				registry.updateGoal(harvest->id, [&](GoalTask& g) {
					g.targetAmount = harvestDemand;
					g.deliveredAmount = 0;
					g.status = GoalStatus::Available;
				});
			} else {
				GoalTask harvestGoal;
				harvestGoal.type = TaskType::Harvest;
				harvestGoal.owner = GoalOwner::ConstructionGoalSystem;
				harvestGoal.destinationEntity = blueprintEntity;
				harvestGoal.destinationPosition = sitePos;
				harvestGoal.acceptedDefNameIds = {defNameId};
				harvestGoal.targetAmount = harvestDemand;
				harvestGoal.yieldDefNameId = defNameId;
				harvestGoal.parentGoalId = umbrellaGoalId; // child of umbrella: skips destination guard
				harvestGoal.status = GoalStatus::Available;
				harvestGoal.chainId = chainId;
				registry.createGoal(std::move(harvestGoal));
			}

			if (haul != nullptr) {
				registry.updateGoal(haul->id, [&](GoalTask& g) {
					g.targetAmount = remaining;
					g.deliveredAmount = 0;
					g.status = GoalStatus::Available;
					g.chainId = chainId;
				});
			} else {
				GoalTask haulGoal;
				haulGoal.type = TaskType::Haul;
				haulGoal.owner = GoalOwner::ConstructionGoalSystem;
				haulGoal.destinationEntity = blueprintEntity;
				haulGoal.destinationPosition = sitePos;
				haulGoal.acceptedDefNameIds = {defNameId};
				haulGoal.targetAmount = remaining;
				haulGoal.parentGoalId = umbrellaGoalId; // child of umbrella: skips destination guard
				haulGoal.status = GoalStatus::Available;
				haulGoal.chainId = chainId;
				registry.createGoal(std::move(haulGoal));
			}

			LOG_DEBUG(
				Engine,
				"[Construction] Material goals for blueprint %u: %u x %s outstanding (%u carried, harvest demand %u)",
				static_cast<uint32_t>(blueprintEntity),
				remaining,
				defName.c_str(),
				carried,
				harvestDemand
			);
		}
	}

	uint32_t ConstructionSystem::carriedAmount(EntityID buildSite, const std::string& defName) const {
		// Sum the material carried by colonists (NeedsComponent distinguishes a colonist from the
		// build site, which also owns an Inventory). Goals are global with no in-flight accounting,
		// so this is how the system learns how much material is already en route to the site.
		uint32_t total = 0;
		for (auto [entity, needs, inventory] : world->view<NeedsComponent, Inventory>()) {
			if (entity == buildSite) {
				continue; // the delivery inventory is on-site, not in flight
			}
			total += inventory.getQuantity(defName);
		}
		return total;
	}

	uint32_t ConstructionSystem::colonistCarryCapacity(EntityID buildSite) const {
		// Largest backpack stack among colonists: how much one of them can carry of a single
		// material in one trip. A max (not a sum or the first hit) keeps this independent of
		// view iteration order, so the harvest-demand bound stays deterministic. Falls back to
		// the colonist default if no colonist exists yet (headless/unit context).
		uint32_t capacity = 0;
		for (auto [entity, needs, inventory] : world->view<NeedsComponent, Inventory>()) {
			if (entity == buildSite) {
				continue;
			}
			capacity = std::max(capacity, inventory.maxStackSize);
		}
		return capacity > 0 ? capacity : Inventory::createForColonist().maxStackSize;
	}

	uint64_t ConstructionSystem::ensureUmbrellaGoal(EntityID blueprintEntity, const StructureBlueprint& blueprint, GoalStatus status) {
		auto& registry = GoalTaskRegistry::Get();

		const auto*		position = world->getComponent<Position>(blueprintEntity);
		const glm::vec2 sitePos = position != nullptr ? position->value : glm::vec2{0.0F, 0.0F};

		// The umbrella owns the destination slot. Because every child carries parentGoalId it is
		// always the (only) goal getGoalByDestination returns for this blueprint.
		const auto* existing = registry.getGoalByDestination(blueprintEntity);
		if (existing != nullptr && existing->type == TaskType::Build) {
			if (existing->status != status) {
				registry.updateGoal(existing->id, [status](GoalTask& g) { g.status = status; });
			}
			return existing->id;
		}

		// No umbrella yet (first tick for this blueprint). A stray non-Build top-level goal here
		// would be a bug from an earlier owner; createGoal updates the destination slot in place,
		// so this both creates the umbrella and reclaims the slot if needed.
		GoalTask buildGoal;
		buildGoal.type = TaskType::Build;
		buildGoal.owner = GoalOwner::ConstructionGoalSystem;
		buildGoal.destinationEntity = blueprintEntity;
		buildGoal.destinationPosition = sitePos;
		buildGoal.targetAmount = 1; // a single goal; multiple colonists can all work it
		buildGoal.status = status;
		const uint64_t umbrellaId = registry.createGoal(std::move(buildGoal));

		LOG_DEBUG(
			Engine,
			"[Construction] Umbrella Build goal %llu for blueprint %u (status %d, work %.0f/%.0f)",
			static_cast<unsigned long long>(umbrellaId),
			static_cast<uint32_t>(blueprintEntity),
			static_cast<int>(status),
			static_cast<double>(blueprint.workDone),
			static_cast<double>(blueprint.workTotal)
		);
		return umbrellaId;
	}

	void ConstructionSystem::completeBlueprintNow(EntityID entity, StructureBlueprint& blueprint) {
		// Satisfy the manifest on the delivery inventory so delivered[] reconciles full and the
		// build site genuinely holds its materials (matching a normally-built structure, and
		// surviving a later reconcile if free-build is toggled off). Inventory is the source of
		// truth; delivered[] is its mirror, so mirror it here too.
		if (auto* inventory = world->getComponent<Inventory>(entity)) {
			for (const auto& [defName, qty] : blueprint.required) {
				const uint32_t have = inventory->getQuantity(defName);
				if (have < qty) {
					inventory->addItem(defName, qty - have);
				}
			}
		}
		blueprint.delivered = blueprint.required;

		// Finish the work and flip the phase exactly as the last Build tick would.
		blueprint.workDone = blueprint.workTotal;
		blueprint.phase = StructureBlueprint::BuildPhase::Complete;

		// Retire every goal this blueprint owns (umbrella + children) so no Build goal lingers.
		auto&		registry = GoalTaskRegistry::Get();
		const auto* goal = registry.getGoalByDestination(entity);
		while (goal != nullptr) {
			registry.removeGoalWithChildren(goal->id);
			goal = registry.getGoalByDestination(entity);
		}

		// Fire the SAME completion callback a real build uses (GameScene wires it to the same
		// lambda it gives ActionSystem), flipping ConstructionWorld state to Built and toasting.
		if (m_onStructureCompleted) {
			m_onStructureCompleted(entity);
		}

		LOG_INFO(Engine, "[Construction] DEV free-build completed blueprint %u", static_cast<uint32_t>(entity));
	}

	bool ConstructionSystem::forceCompleteBlueprint(EntityID blueprintEntity) {
		if (world == nullptr) {
			return false;
		}
		const auto* structure = world->getComponent<Structure>(blueprintEntity);
		auto*		blueprint = world->getComponent<StructureBlueprint>(blueprintEntity);
		if (structure == nullptr || blueprint == nullptr) {
			return false;
		}
		if (structure->kind != StructureKind::Foundation && structure->kind != StructureKind::Wall) {
			return false;
		}
		if (blueprint->phase == StructureBlueprint::BuildPhase::Complete || blueprint->demolishing) {
			return false;
		}
		completeBlueprintNow(blueprintEntity, *blueprint);
		return true;
	}

	uint32_t ConstructionSystem::creditMaterialToSites(const std::string& defName, uint32_t amount) {
		if (world == nullptr || amount == 0) {
			return 0;
		}
		uint32_t credited = 0;
		for (auto [entity, structure, blueprint] : world->view<Structure, StructureBlueprint>()) {
			if (amount == 0) {
				break;
			}
			if (structure.kind != StructureKind::Foundation && structure.kind != StructureKind::Wall) {
				continue;
			}
			if (blueprint.phase == StructureBlueprint::BuildPhase::Complete || blueprint.demolishing) {
				continue;
			}
			const uint32_t need = blueprint.remaining(defName);
			if (need == 0) {
				continue;
			}
			auto* inventory = world->getComponent<Inventory>(entity);
			if (inventory == nullptr) {
				continue;
			}
			// Fill only this site's outstanding need, never more than we have left to hand out.
			// remaining() already encodes the manifest math, so this never re-derives it.
			const uint32_t give = std::min(need, amount);
			const uint32_t added = inventory->addItem(defName, give);
			credited += added;
			amount -= added;
		}
		return credited;
	}

	void ConstructionSystem::retireChildGoals(EntityID blueprintEntity, uint64_t umbrellaGoalId, bool keepMaterialChildren) {
		auto& registry = GoalTaskRegistry::Get();

		std::vector<uint64_t> toRemove;
		for (const auto* child : registry.getChildGoals(umbrellaGoalId)) {
			if (child->destinationEntity != blueprintEntity) {
				continue;
			}
			// Material children carry a chainId (the Harvest/Haul delivery chain); the clear-Harvest
			// child does not. When keeping material children, retire only the chain-less clear goal.
			const bool isMaterialChild = child->chainId.has_value();
			if (keepMaterialChildren && isMaterialChild) {
				continue;
			}
			toRemove.push_back(child->id);
		}
		for (uint64_t id : toRemove) {
			registry.removeGoalWithChildren(id);
		}
	}

} // namespace ecs
