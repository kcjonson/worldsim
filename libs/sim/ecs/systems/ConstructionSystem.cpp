#include "ConstructionSystem.h"

#include "GoalSystemHelpers.h"

#include "../GoalTaskRegistry.h"
#include "../World.h"
#include "../WorldSource.h"
#include "../components/Appearance.h"
#include "../components/Packaged.h"
#include "../components/ResourceStack.h"
#include "../components/Structure.h"
#include "../components/Task.h"
#include "../components/Transform.h"

#include <construction/ConstructionWorld.h>

#include <assets/AssetRegistry.h>
#include <assets/placement/PlacementExecutor.h>
#include <world/chunk/ChunkCoordinate.h>

#include <utils/Log.h>

#include <algorithm>
#include <cmath>

namespace ecs {

	ConstructionDecision decideConstructionPhase(const StructureBlueprint& blueprint, bool footprintClear, bool materialsComplete) {
		ConstructionDecision decision;

		// Forward build progression only. Demolishing is NOT handled here: callers MUST route a
		// demolishing blueprint to decideDeconstruct before reaching this helper (update() does
		// so). A demolishing blueprint passed in here would be mis-decided as a build.

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
		// can own several goals (clear Harvest, a Harvest per material, a Build), so
		// this is the set of destination entities, not goal ids.
		std::unordered_set<EntityID> entitiesWithGoals;
		for (const auto* goal : registry.getGoalsByOwner(GoalOwner::ConstructionGoalSystem)) {
			entitiesWithGoals.insert(goal->destinationEntity);
		}

		m_activeBlueprintCount = 0;
		bool gatheredSpawned = false;

		for (auto [entity, structure, blueprint] : world->view<Structure, StructureBlueprint>()) {
			// Foundations, walls, and openings all flow through; rooms are not blueprints yet.
			if (structure.kind != StructureKind::Foundation && structure.kind != StructureKind::Wall &&
				structure.kind != StructureKind::Opening) {
				continue;
			}
			const bool isWall = (structure.kind == StructureKind::Wall);
			const bool isOpening = (structure.kind == StructureKind::Opening);

			const uint64_t graphId = structure.graphId;

			// A demolishing blueprint is owned by the Deconstruct path: drop any leftover Build
			// goals and drive its Deconstruct goal (gated by the cascade order). Keep it OUT of the
			// stale set so the cleanup pass doesn't tear down the Deconstruct goal we just emitted.
			if (blueprint.demolishing) {
				decideDeconstruct(entity, structure, blueprint);
				entitiesWithGoals.erase(entity);
				m_activeBlueprintCount++;
				continue;
			}

			// A completed blueprint emits nothing; leave it in the stale set so the cleanup pass
			// below drops any lingering goals (the Build goal never self-retires via delivery).
			// Completion itself is flipped by ActionSystem's callback.
			if (blueprint.phase == StructureBlueprint::BuildPhase::Complete) {
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

			// Walls and openings have no clear phase: they sit on a cleared, built host, so
			// the footprint is clear by construction. A foundation queries its footprint while clearing,
			// and once more as its materials complete, on the way into UnderConstruction: flora placed
			// after the site left Clearing (a chunk processed late) sends it back to Clearing then.
			// Between the two it is not rescanned (editing the footprint puts it back to Clearing).
			const bool materialsDone = blueprint.materialsComplete();
			const bool isFoundation = !isWall && !isOpening;
			const bool checkFootprint =
				isFoundation && (blueprint.phase == StructureBlueprint::BuildPhase::Clearing ||
								 (blueprint.phase == StructureBlueprint::BuildPhase::AwaitingMaterials && materialsDone));
			if (checkFootprint && !gatheredSpawned) {
				gatherSpawnedHarvestables();
				gatheredSpawned = true;
			}
			const bool footprintClear = !checkFootprint || isFootprintClear(graphId);

			const ConstructionDecision decision = decideConstructionPhase(blueprint, footprintClear, materialsDone);

			// Advance the phase (idempotent). ActionSystem flips Complete itself on the last
			// Build tick, so we never downgrade out of Complete here.
			if (blueprint.phase != StructureBlueprint::BuildPhase::Complete) {
				// A foundation is built on bare ground: what is left on its footprint goes as building starts.
				if (isFoundation && m_constructionWorld != nullptr && blueprint.phase != StructureBlueprint::BuildPhase::UnderConstruction &&
					decision.nextPhase == StructureBlueprint::BuildPhase::UnderConstruction) {
					m_sitesEnteringConstruction.push_back(footprintBounds(graphId));
				}
				blueprint.phase = decision.nextPhase;
			}

			// One umbrella Build goal per blueprint owns the destination slot and parents every
			// phase goal. It is Available only once materials are staged (UnderConstruction),
			// matching evaluateBuildOptions' gate; otherwise Blocked so builders aren't dispatched
			// while the site still needs clearing or materials. The umbrella persists across phases
			// so the child Harvest goals never collide on the single-top-level-per-destination
			// guard.
			const GoalStatus umbrellaStatus = decision.emitBuildGoal ? GoalStatus::Available : GoalStatus::Blocked;
			const uint64_t	 umbrellaId = ensureUmbrellaGoal(entity, blueprint, umbrellaStatus);

			if (decision.emitClearGoals) {
				emitClearGoals(entity, graphId, umbrellaId);
			} else if (decision.emitMaterialGoals) {
				// Footprint is clear: the clear-Harvest child is obsolete, drop it; keep the
				// per-material Harvest children for what the manifest still lacks.
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
			resetBlueprintGoals(stale);
		}

		bareSites();
	}

	void ConstructionSystem::bareSites() {
		if (m_sitesEnteringConstruction.empty()) {
			return;
		}
		removeStaticsFromSites();
		movePilesOffSites();
		movePackagedOffSites();
		m_sitesEnteringConstruction.clear();
	}

	void ConstructionSystem::removeStaticsFromSites() {
		if (m_placementExecutor == nullptr) {
			return;
		}
		struct Standing {
			std::string defName;
			glm::vec2	position;
		};
		const auto&			  assetRegistry = assets::AssetRegistry::Get();
		std::vector<Standing> standing;
		for (const FootprintBounds& site : m_sitesEnteringConstruction) {
			const world::ChunkCoordinate first = world::worldToChunk({site.min.x, site.min.y});
			const world::ChunkCoordinate last = world::worldToChunk({site.max.x, site.max.y});
			for (int32_t cy = first.y; cy <= last.y; ++cy) {
				for (int32_t cx = first.x; cx <= last.x; ++cx) {
					const world::ChunkCoordinate coord{cx, cy};
					const auto*					 chunkIndex = m_placementExecutor->getChunkIndex(coord);
					if (chunkIndex == nullptr) {
						continue;
					}
					// Copied out first: a removal moves the entities the query points at.
					standing.clear();
					for (const auto* placed : chunkIndex->queryRect(site.min.x, site.min.y, site.max.x, site.max.y)) {
						if (site.contains(placed->position)) {
							standing.push_back({placed->defName, placed->position});
						}
					}
					for (const Standing& gone : standing) {
						const auto* def = assetRegistry.getDefinition(gone.defName);
						const bool	carried = def != nullptr && def->capabilities.carryable.has_value();
						if (carried && !m_onDropResource) {
							continue;
						}
						m_placementExecutor->removeEntity(coord, gone.position, gone.defName);
						if (carried) {
							const glm::vec2 to = site.nearestOutside(gone.position, kPileMoveMargin);
							m_onDropResource(gone.defName, to.x, to.y, def->capabilities.carryable->quantity, gone.position);
						}
					}
				}
			}
		}
	}

	void ConstructionSystem::movePilesOffSites() {
		// With nowhere to drop them (no drop path wired), the piles stay where they are.
		if (!m_onDropResource) {
			return;
		}
		struct Moved {
			EntityID	pile;
			std::string defName;
			uint32_t	quantity;
			glm::vec2	to;
			glm::vec2	from;
		};
		std::vector<Moved> moved;
		for (auto [pile, stack, position, appearance] : world->view<ResourceStack, Position, Appearance>()) {
			for (const FootprintBounds& site : m_sitesEnteringConstruction) {
				if (stack.quantity > 0 && site.contains(position.value)) {
					moved.push_back(
						{pile, appearance.defName, stack.quantity, site.nearestOutside(position.value, kPileMoveMargin), position.value}
					);
					break;
				}
			}
		}
		for (const Moved& move : moved) {
			destroyWorldEntity(*world, move.pile);
			// The old spot is on the site, so the drop never lands there: it is where the search for
			// ground restarts when none lies past the nearest edge.
			m_onDropResource(move.defName, move.to.x, move.to.y, move.quantity, move.from);
			LOG_DEBUG(
				Engine, "[Construction] Moved %u x %s off a site about to be built, to (%.1f, %.1f)", move.quantity, move.defName.c_str(), move.to.x,
				move.to.y
			);
		}
	}

	void ConstructionSystem::movePackagedOffSites() {
		if (!m_packagedSetDown) {
			return;
		}
		// One a colonist is on his way to fetch is left to him: moved, his pickup would land where it
		// was. He carries it off to its placement, or sets it down off the site if he drops the job.
		std::unordered_set<uint64_t> fetched;
		for (auto [colonist, task] : world->view<Task>()) {
			(void)colonist;
			if (task.type == TaskType::PlacePackaged) {
				fetched.insert(task.placePackagedEntityId);
			}
		}
		struct Lying {
			EntityID  item;
			glm::vec2 to;
		};
		std::vector<Lying> lying;
		for (auto [item, packaged, position] : world->view<Packaged, Position>()) {
			// A carried item's position is where it was picked up, not where it is.
			if (packaged.beingCarried || fetched.contains(static_cast<uint64_t>(item))) {
				continue;
			}
			for (const FootprintBounds& site : m_sitesEnteringConstruction) {
				// One whose placement is on the footprint is installed there; moving it off gains nothing.
				const bool placedOnSite = packaged.targetPosition.has_value() && site.contains(*packaged.targetPosition);
				if (site.contains(position.value) && !placedOnSite) {
					lying.push_back({item, site.nearestOutside(position.value, kPileMoveMargin)});
					break;
				}
			}
		}
		// One at a time, so each set-down sees where the one before it went.
		for (const Lying& move : lying) {
			const glm::vec2 setDownAt = m_packagedSetDown(move.to);
			world->getComponent<Position>(move.item)->value = setDownAt;
			LOG_DEBUG(
				Engine, "[Construction] Moved packaged entity %u off a site about to be built, to (%.1f, %.1f)",
				static_cast<uint32_t>(move.item), setDownAt.x, setDownAt.y
			);
		}
	}

	void ConstructionSystem::resetBlueprintGoals(EntityID blueprintEntity) {
		auto&		registry = GoalTaskRegistry::Get();
		const auto* goal = registry.getGoalByDestination(blueprintEntity);
		while (goal != nullptr) {
			registry.removeGoalWithChildren(goal->id);
			goal = registry.getGoalByDestination(blueprintEntity);
		}
	}

	FootprintBounds ConstructionSystem::footprintBounds(uint64_t foundationId) const {
		const auto* foundation = m_constructionWorld->get(foundationId);
		return FootprintBounds{foundation != nullptr ? foundation->ring : geometry::Ring{}};
	}

	void ConstructionSystem::gatherSpawnedHarvestables() {
		m_spawnedHarvestables.clear();
		const auto& assetRegistry = assets::AssetRegistry::Get();
		for (auto [entity, position, appearance] : world->view<Position, Appearance>()) {
			(void)entity;
			const uint32_t defNameId = assetRegistry.getDefNameId(appearance.defName);
			if (assetRegistry.hasCapability(defNameId, assets::CapabilityType::Harvestable)) {
				m_spawnedHarvestables.push_back({defNameId, position.value});
			}
		}
	}

	bool ConstructionSystem::isFootprintClear(uint64_t foundationId) const {
		if (m_constructionWorld == nullptr) {
			// No topology wired (unit/headless contexts): there is no footprint to clear.
			return true;
		}

		const FootprintBounds bounds = footprintBounds(foundationId);
		auto&				  assetRegistry = assets::AssetRegistry::Get();

		for (const SpawnedHarvestable& spawned : m_spawnedHarvestables) {
			if (bounds.blockedBy(assetRegistry, spawned.defNameId, spawned.position)) {
				return false;
			}
		}

		if (m_placementExecutor == nullptr || m_processedChunks == nullptr) {
			return true;
		}

		const float minX = bounds.min.x;
		const float minY = bounds.min.y;
		const float maxX = bounds.max.x;
		const float maxY = bounds.max.y;

		constexpr float kChunkWorldSize = static_cast<float>(world::kChunkSize);
		const int32_t	chunkMinX = static_cast<int32_t>(std::floor(minX / kChunkWorldSize));
		const int32_t	chunkMaxX = static_cast<int32_t>(std::floor(maxX / kChunkWorldSize));
		const int32_t	chunkMinY = static_cast<int32_t>(std::floor(minY / kChunkWorldSize));
		const int32_t	chunkMaxY = static_cast<int32_t>(std::floor(maxY / kChunkWorldSize));

		for (int32_t cy = chunkMinY; cy <= chunkMaxY; ++cy) {
			for (int32_t cx = chunkMinX; cx <= chunkMaxX; ++cx) {
				// A chunk whose flora isn't placed yet may hold a blocker: not known clear, so the site waits.
				world::ChunkCoordinate coord{cx, cy};
				const auto* chunkIndex = m_processedChunks->contains(coord) ? m_placementExecutor->getChunkIndex(coord) : nullptr;
				if (chunkIndex == nullptr) {
					return false;
				}
				for (const auto* placed : chunkIndex->queryRect(minX, minY, maxX, maxY)) {
					if (bounds.blockedBy(assetRegistry, assetRegistry.getDefNameId(placed->defName), placed->position)) {
						return false;
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
			return segment->hostFoundation == construction::kInvalidFoundation;
		}
		return host->state == construction::FoundationState::Built;
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
		return segment->state == construction::FoundationState::Built;
	}

	void ConstructionSystem::emitClearGoals(EntityID blueprintEntity, uint64_t foundationId, uint64_t umbrellaGoalId) {
		if (m_constructionWorld == nullptr) {
			return;
		}

		auto& registry = GoalTaskRegistry::Get();

		// One clear Harvest CHILD per blueprint, served by any harvestable a colonist knows inside the
		// footprint; each such harvest removes its blocker. The goal keeps one identity until the
		// footprint is empty (update() then retires it via retireChildGoals), so several colonists
		// clearing different blockers never see it vanish under them. The yield stays with the
		// colonist; once materials are due, it carries in any of it the site needs.
		// The child hangs under the umbrella (parentGoalId set) so it is owned/cleaned alongside the
		// blueprint without fighting the umbrella for the single-top-level-per-destination slot.
		for (const auto* child : registry.getChildGoals(umbrellaGoalId)) {
			if (child->clearFootprint.has_value()) {
				return;
			}
		}

		const FootprintBounds bounds = footprintBounds(foundationId);

		GoalTask clearGoal;
		clearGoal.type = TaskType::Harvest;
		clearGoal.owner = GoalOwner::ConstructionGoalSystem;
		clearGoal.destinationEntity = blueprintEntity;
		clearGoal.destinationPosition = bounds.interiorPoint();
		clearGoal.clearFootprint = bounds;
		clearGoal.parentGoalId = umbrellaGoalId; // child of the umbrella: skips the destination guard
		clearGoal.status = GoalStatus::Available;
		registry.createGoal(std::move(clearGoal));
		LOG_DEBUG(Engine, "[Construction] Emitted clear Harvest goal for blueprint %u", static_cast<uint32_t>(blueprintEntity));
	}

	void ConstructionSystem::emitMaterialGoals(EntityID blueprintEntity, const StructureBlueprint& blueprint, uint64_t umbrellaGoalId) {
		auto& registry = GoalTaskRegistry::Get();
		auto& assetRegistry = assets::AssetRegistry::Get();

		const auto*		position = world->getComponent<Position>(blueprintEntity);
		const glm::vec2 sitePos = position != nullptr ? position->value : glm::vec2{0.0F, 0.0F};

		// The umbrella accepts every material the site still lacks; its remaining need for each is
		// the manifest less delivered[], which any colonist holding the material may carry in. One
		// Harvest child per such material marks that cutting it is open; how much more to cut is the
		// umbrella's remaining need net of in-flight, and every cut carries its yield to the site.
		std::vector<uint32_t> due;
		for (const auto& [defName, requiredQty] : blueprint.required) {
			const uint32_t defNameId = assetRegistry.getDefNameId(defName);
			if (defNameId != 0 && blueprint.remaining(defName) > 0) {
				due.push_back(defNameId);
			}
		}
		registry.updateGoal(umbrellaGoalId, [&due](GoalTask& goal) { goal.acceptedDefNameIds = due; });

		std::vector<uint32_t> harvested;
		std::vector<uint64_t> retired;
		for (const auto* child : registry.getChildGoals(umbrellaGoalId)) {
			if (child->clearFootprint.has_value()) {
				continue;
			}
			const bool wanted = std::find(due.begin(), due.end(), child->yieldDefNameId) != due.end() &&
								std::find(harvested.begin(), harvested.end(), child->yieldDefNameId) == harvested.end();
			if (wanted) {
				harvested.push_back(child->yieldDefNameId);
			} else {
				retired.push_back(child->id);
			}
		}
		for (uint64_t id : retired) {
			registry.removeGoal(id);
		}

		for (uint32_t defNameId : due) {
			if (std::find(harvested.begin(), harvested.end(), defNameId) != harvested.end()) {
				continue;
			}
			GoalTask harvestGoal;
			harvestGoal.type = TaskType::Harvest;
			harvestGoal.owner = GoalOwner::ConstructionGoalSystem;
			harvestGoal.destinationEntity = blueprintEntity;
			harvestGoal.destinationPosition = sitePos;
			harvestGoal.acceptedDefNameIds = {defNameId};
			harvestGoal.yieldDefNameId = defNameId;
			harvestGoal.parentGoalId = umbrellaGoalId; // child of umbrella: skips destination guard
			harvestGoal.status = GoalStatus::Available;
			registry.createGoal(std::move(harvestGoal));
			LOG_DEBUG(
				Engine,
				"[Construction] Harvest goal for blueprint %u: %s outstanding",
				static_cast<uint32_t>(blueprintEntity),
				assetRegistry.getDefName(defNameId).c_str()
			);
		}
	}

	uint64_t ConstructionSystem::ensureUmbrellaGoal(EntityID blueprintEntity, const StructureBlueprint& blueprint, GoalStatus status) {
		auto& registry = GoalTaskRegistry::Get();

		const auto*		position = world->getComponent<Position>(blueprintEntity);
		const glm::vec2 sitePos = position != nullptr ? position->value : glm::vec2{0.0F, 0.0F};

		// The umbrella owns the destination slot. Because every child carries parentGoalId it is
		// always the (only) goal getGoalByDestination returns for this blueprint.
		const auto* existing = registry.getGoalByDestination(blueprintEntity);
		if (existing != nullptr && existing->type == TaskType::Build) {
			// Materials are accepted only while due; emitMaterialGoals names them again each pass.
			registry.updateGoal(existing->id, [status](GoalTask& g) {
				g.status = status;
				g.acceptedDefNameIds.clear();
			});
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

	bool ConstructionSystem::deconstructDependentsCleared(const Structure& structure) const {
		if (m_constructionWorld == nullptr) {
			// No topology wired (headless/unit context): nothing to cascade-gate on.
			return true;
		}
		switch (structure.kind) {
			case StructureKind::Foundation:
				// A foundation may not deconstruct while any wall still stands on it, nor
				// while an extension waits to merge into it (the extension goes first, so
				// it never outlives its target). foundationHasWalls is the allocation-free
				// early-exit query (this runs every tick for each demolishing foundation).
				return !m_constructionWorld->foundationHasWalls(structure.graphId) &&
					   m_constructionWorld->pendingExtensionOf(structure.graphId) == construction::kInvalidFoundation;
			case StructureKind::Wall: {
				// A wall may not deconstruct while any opening sits on it.
				for (const auto& opening : m_constructionWorld->openings()) {
					if (opening.segment == structure.graphId) {
						return false;
					}
				}
				return true;
			}
			case StructureKind::Opening:
			case StructureKind::Room:
				// Openings (and the non-blueprint Room) have no structural dependents.
				return true;
		}
		return true;
	}

	void ConstructionSystem::dumpDeliveredToGround(EntityID blueprintEntity, StructureBlueprint& blueprint) {
		if (blueprint.delivered.empty()) {
			return;
		}
		const auto*		position = world->getComponent<Position>(blueprintEntity);
		const glm::vec2 dropPos = position != nullptr ? position->value : glm::vec2{0.0F, 0.0F};

		// Drop every staged material in full. The only caller is the no-work cancel: nothing was
		// built, so all in-flight material returns to the ground. The drop callback splits each
		// quantity into stacks <= stackSize and scatters them, so one call per material is enough.
		// Clear delivered[] afterward: the materials are now loose on the ground, no longer staged on
		// this (about-to-be-removed) blueprint.
		if (m_onDropResource) {
			for (const auto& [defName, qty] : blueprint.delivered) {
				if (qty > 0) {
					m_onDropResource(defName, dropPos.x, dropPos.y, qty, dropPos);
				}
			}
		}
		blueprint.delivered.clear();
	}

	void ConstructionSystem::decideDeconstruct(EntityID blueprintEntity, const Structure& structure, StructureBlueprint& blueprint) {
		auto& registry = GoalTaskRegistry::Get();

		// A demolishing structure with no work to undo can't be work-deconstructed (startBuildAction
		// rejects workDone <= 0). Remove it immediately through the SAME removal callback a worked
		// deconstruct fires, and retire any goals it owned. This is the cancel-a-blueprint case
		// (Clearing/AwaitingMaterials, workDone == 0): nothing was built, so ALL staged delivered[]
		// material is returned to the ground (fraction 1.0) at order-time, since the site is removed
		// promptly. A worked/built structure takes the path below; its salvage cut is dropped later,
		// when the tear-down work completes (the deconstructed-completion callback in GameScene).
		if (blueprint.workDone <= 0.0F) {
			dumpDeliveredToGround(blueprintEntity, blueprint);
			resetBlueprintGoals(blueprintEntity);
			if (m_onStructureDeconstructed) {
				m_onStructureDeconstructed(blueprintEntity);
			} else if (m_warnedNoDeconstructCallback.insert(blueprintEntity).second) {
				// No callback: the blueprint stays demolishing and re-enters this branch every tick,
				// so warn once per entity instead of per tick.
				LOG_WARNING(
					Engine,
					"[Construction] Demolishing blueprint %u has no work to undo but no deconstructed callback set - not removed",
					static_cast<uint32_t>(blueprintEntity)
				);
			}
			return;
		}

		// Worked/built teardown: leave delivered[] intact. The salvage drop (refundPercent% of the
		// manifest) happens when the tear-down WORK completes and the structure is actually removed,
		// via the deconstructed-completion callback in GameScene, NOT here at the order tick while the
		// structure is still standing.

		const auto*		position = world->getComponent<Position>(blueprintEntity);
		const glm::vec2 sitePos = position != nullptr ? position->value : glm::vec2{0.0F, 0.0F};

		// Cascade gate: a structure's Deconstruct goal goes Available only once its dependents are
		// gone (openings off a wall, walls off a foundation). Until then it holds a Blocked goal so
		// the building tears down in order: openings -> walls -> foundation.
		const GoalStatus status = deconstructDependentsCleared(structure) ? GoalStatus::Available : GoalStatus::Blocked;

		// One top-level Deconstruct goal owns the destination slot. If a stale Build umbrella (and
		// its children) is still parked there from before the demolish order, drop the whole tree
		// so the slot is free for the Deconstruct goal.
		const auto* existing = registry.getGoalByDestination(blueprintEntity);
		if (existing != nullptr && existing->type != TaskType::Deconstruct) {
			registry.removeGoalWithChildren(existing->id);
			existing = nullptr;
		}

		if (existing != nullptr) {
			if (existing->status != status) {
				registry.updateGoal(existing->id, [status](GoalTask& g) { g.status = status; });
			}
			return;
		}

		GoalTask deconstructGoal;
		deconstructGoal.type = TaskType::Deconstruct;
		deconstructGoal.owner = GoalOwner::ConstructionGoalSystem;
		deconstructGoal.destinationEntity = blueprintEntity;
		deconstructGoal.destinationPosition = sitePos;
		deconstructGoal.status = status;
		const uint64_t goalId = registry.createGoal(std::move(deconstructGoal));

		LOG_DEBUG(
			Engine,
			"[Construction] Deconstruct goal %llu for blueprint %u (status %d, work %.0f/%.0f)",
			static_cast<unsigned long long>(goalId),
			static_cast<uint32_t>(blueprintEntity),
			static_cast<int>(status),
			static_cast<double>(blueprint.workDone),
			static_cast<double>(blueprint.workTotal)
		);
	}

	void ConstructionSystem::completeBlueprintNow(EntityID entity, StructureBlueprint& blueprint) {
		// Satisfy the manifest directly: delivered[] is the authoritative tracker (a build site
		// holds no Inventory), so a full build means delivered == required.
		blueprint.delivered = blueprint.required;

		// Finish the work and flip the phase exactly as the last Build tick would.
		blueprint.workDone = blueprint.workTotal;
		blueprint.phase = StructureBlueprint::BuildPhase::Complete;

		// Retire every goal this blueprint owns (umbrella + children) so no Build goal lingers.
		resetBlueprintGoals(entity);

		// It skipped the clear and the start of building, so its ground is bared on the pass this
		// ends (or the next, from the dev endpoint): whatever stands there, trees included, with no yield.
		const auto* structure = world->getComponent<Structure>(entity);
		if (m_constructionWorld != nullptr && structure != nullptr && structure->kind == StructureKind::Foundation) {
			m_sitesEnteringConstruction.push_back(footprintBounds(structure->graphId));
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
		if (structure->kind != StructureKind::Foundation && structure->kind != StructureKind::Wall &&
			structure->kind != StructureKind::Opening) {
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
			if (structure.kind != StructureKind::Foundation && structure.kind != StructureKind::Wall &&
				structure.kind != StructureKind::Opening) {
				continue;
			}
			if (blueprint.phase == StructureBlueprint::BuildPhase::Complete || blueprint.demolishing) {
				continue;
			}
			const uint32_t need = blueprint.remaining(defName);
			if (need == 0) {
				continue;
			}
			// Fill only this site's outstanding need, never more than we have left to hand out.
			// recordDelivery caps at the requirement, so this never over-fills the manifest.
			const uint32_t give = std::min(need, amount);
			const uint32_t recorded = blueprint.recordDelivery(defName, give);
			credited += recorded;
			amount -= recorded;
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
			// When keeping material children, retire only the clear goal.
			if (keepMaterialChildren && !child->clearFootprint.has_value()) {
				continue;
			}
			toRemove.push_back(child->id);
		}
		for (uint64_t id : toRemove) {
			registry.removeGoalWithChildren(id);
		}
	}

} // namespace ecs
