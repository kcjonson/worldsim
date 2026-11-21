#include "../ActionSystem.h"

#include "../../World.h"
#include "../../components/Action.h"
#include "../../components/AgentRadius.h"
#include "../../components/PlayerControlled.h"
#include "../../components/Skills.h"
#include "../../components/Structure.h"
#include "../../components/StructureBlueprint.h"
#include "../../components/Task.h"
#include "../../components/Transform.h"

#include <construction/ConstructionWorld.h>
#include <construction/WallSides.h>

#include <utils/Log.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <vector>

namespace ecs {

	bool ActionSystem::completeWork(const WorkRow& row, const Action& action) {
		if (!action.hasProgressEffect()) {
			return refuseWithoutTarget(action, "work");
		}
		const auto& progressEff = action.progressEffect();
		auto		blueprintEntity = static_cast<EntityID>(progressEff.targetEntityId);

		if (row.down) {
			LOG_INFO(
				Engine,
				"[Action] Deconstruct complete on blueprint %llu - signaling removal/refund",
				static_cast<unsigned long long>(progressEff.targetEntityId)
			);
			if (m_onStructureDeconstructed) {
				m_onStructureDeconstructed(blueprintEntity);
			} else {
				LOG_WARNING(Engine, "[Action] No structure-deconstructed callback set - removal/refund skipped");
			}
		} else {
			LOG_INFO(
				Engine,
				"[Action] Build complete on blueprint %llu - signaling structure built",
				static_cast<unsigned long long>(progressEff.targetEntityId)
			);
			if (m_onStructureCompleted) {
				m_onStructureCompleted(blueprintEntity);
			} else {
				LOG_WARNING(Engine, "[Action] No structure-completed callback set - structure state not flipped");
			}
		}
		return true;
	}

	void ActionSystem::startBuildAction(EntityID entity, Task& task, Action& action) {
		auto blueprintEntity = static_cast<EntityID>(task.buildBlueprintEntityId);
		auto* blueprint = world->getComponent<StructureBlueprint>(blueprintEntity);
		if (blueprint == nullptr) {
			LOG_WARNING(
				Engine,
				"[Action] %s task targets entity %llu with no StructureBlueprint - clearing",
				task.type == TaskType::Deconstruct ? "Deconstruct" : "Build",
				static_cast<unsigned long long>(task.buildBlueprintEntityId)
			);
			action.clear();
			return;
		}

		const bool			deconstruct = (task.type == TaskType::Deconstruct);
		const ActionValues& values = deconstruct ? actionEntries.deconstruct() : actionEntries.build();
		const auto&			work = std::get<WorkRow>(values.effects[values.boundRow]);

		// A start the guards refuse clears the action so the colonist re-evaluates rather than spinning on a
		// finished or not-yet-ready blueprint.
		if (!workMayStart(work, *blueprint)) {
			if (work.down) {
				LOG_DEBUG(
					Engine,
					"[Action] Deconstruct skipped: blueprint %llu has no work to undo",
					static_cast<unsigned long long>(task.buildBlueprintEntityId)
				);
			} else {
				LOG_DEBUG(
					Engine,
					"[Action] Build skipped: blueprint %llu not buildable (phase %d, demolishing %d)",
					static_cast<unsigned long long>(task.buildBlueprintEntityId),
					static_cast<int>(blueprint->phase),
					blueprint->demolishing ? 1 : 0
				);
			}
			action.clear();
			return;
		}

		// Capture the builder's level in the work row's skill to scale the work rate. Skills is optional; a
		// colonist without it (or untrained) still builds at the base rate.
		float skillLevel = 0.0F;
		if (const auto* skills = world->getComponent<Skills>(entity)) {
			skillLevel = skills->getLevel(work.skill);
		}

		action = deconstruct ? Action::Deconstruct(actionEntries, task.buildBlueprintEntityId, task.targetPosition, skillLevel)
							 : Action::Build(actionEntries, task.buildBlueprintEntityId, task.targetPosition, skillLevel);

		LOG_DEBUG(
			Engine,
			"[Action] Starting %s action on blueprint %llu (skill %.1f, rate %.1f/s, workDone %.1f/%.1f)",
			deconstruct ? "Deconstruct" : "Build",
			static_cast<unsigned long long>(task.buildBlueprintEntityId),
			skillLevel,
			constructionWorkRate(skillLevel),
			blueprint->workDone,
			blueprint->workTotal
		);
	}

	bool ActionSystem::workMayStart(const WorkRow& row, const StructureBlueprint& blueprint) {
		if (row.down) {
			return blueprint.workDone > 0.0F;
		}
		return blueprint.phase == StructureBlueprint::BuildPhase::UnderConstruction && !blueprint.demolishing;
	}

	bool ActionSystem::advanceWork(float deltaTime, const WorkRow& row, Action& action) {
		// Transition Starting -> InProgress on the first tick, mirroring processAction.
		if (action.state == ActionState::Starting) {
			action.state = ActionState::InProgress;
		}
		if (!action.hasProgressEffect()) {
			refuseWithoutTarget(action, "work");
			action.clear();
			return false;
		}

		const auto& progressEff = action.progressEffect();
		auto		blueprintEntity = static_cast<EntityID>(progressEff.targetEntityId);
		auto* blueprint = world->getComponent<StructureBlueprint>(blueprintEntity);
		if (blueprint == nullptr) {
			// Blueprint vanished mid-work (e.g. cancelled). Abandon the action; the task will
			// re-evaluate next tick. Don't fire a completion callback for a target that's gone.
			LOG_WARNING(
				Engine,
				"[Action] Construction target %llu vanished mid-work - abandoning action",
				static_cast<unsigned long long>(progressEff.targetEntityId)
			);
			action.clear();
			return false;
		}

		// Redundant-builder guard: completion is gated on the actual phase/work-bound transition,
		// not just on the work bound being reached. With multiple concurrent builders, a second
		// builder still arriving after the structure already flipped Complete (or a deconstruct
		// already at 0) would otherwise re-set its action to Complete every tick and re-fire the
		// completion callback (duplicate toast + redundant world version bump). Treat such a
		// builder as redundant: clear its action and return without firing. A site ordered
		// demolished takes no more build work from that tick on, and never completes.
		if (row.down) {
			if (blueprint->workDone <= 0.0F) {
				action.clear();
				return false;
			}
		} else {
			if (blueprint->phase == StructureBlueprint::BuildPhase::Complete || blueprint->demolishing) {
				action.clear();
				return false;
			}
		}

		const float delta = constructionWorkRate(progressEff.skillLevel) * deltaTime;

		if (row.down) {
			blueprint->workDone -= delta;
			blueprint->teardownBegun = true;
			if (blueprint->workDone <= 0.0F) {
				blueprint->workDone = 0.0F;
				action.state = ActionState::Complete;
				return true;
			}
		} else {
			blueprint->workDone += delta;
			if (blueprint->workDone >= blueprint->workTotal) {
				blueprint->workDone = blueprint->workTotal;
				// A wall's work can be done while the wall is not: it waits there until nothing holds it, and
				// nothing completes or moves on a tick in which no time passes (a paused game).
				const auto* structure = world->getComponent<Structure>(blueprintEntity);
				const bool	isWall = structure != nullptr && structure->kind == StructureKind::Wall;
				if (!isWall || (deltaTime > 0.0F && clearWallToComplete(blueprintEntity))) {
					blueprint->phase = StructureBlueprint::BuildPhase::Complete;
					action.state = ActionState::Complete;
					forgetWall(blueprintEntity);
					return true;
				}
				// Waiting is no work to commit to: the builder may be taken away like from interruptable work.
				action.interruptable = true;
			}
		}

		return false;
	}

	namespace {
		// Whether `task` is taking a colonist out of the room `closure` would close: it is under way (its
		// route not found wanting), and the point it walks to, or a later leg's (a haul's deposit, a
		// placement's spot), is not shut in.
		bool leaving(const Task& task, const construction::WallClosure& closure) {
			if (!task.isActive() || task.navState == NavState::CantFindWayTo) {
				return false;
			}
			if (!closure.shutsIn(task.targetPosition)) {
				return true;
			}
			if (task.type == TaskType::Haul) {
				return !closure.shutsIn(task.haulTargetPosition);
			}
			if (task.type == TaskType::PlacePackaged) {
				return !closure.shutsIn(task.placeTargetPosition);
			}
			return false;
		}
	} // namespace

	bool ActionSystem::buildsAWall(const Task& task) const {
		const auto* structure = task.type == TaskType::Build ? world->getComponent<Structure>(static_cast<EntityID>(task.buildBlueprintEntityId)) : nullptr;
		return structure != nullptr && structure->kind == StructureKind::Wall;
	}

	const construction::WallClosure& ActionSystem::wallClosure(EntityID blueprintEntity, construction::SegmentId segment) {
		auto cached = wallClosures.find(blueprintEntity);
		if (cached == wallClosures.end() || cached->second.segment != segment || cached->second.version != constructionWorld->version()) {
			cached = wallClosures
						 .insert_or_assign(
							 blueprintEntity, CachedClosure{.segment = segment,
															.version = constructionWorld->version(),
															.closure = std::make_shared<const construction::WallClosure>(*constructionWorld, segment)}
						 )
						 .first;
		}
		return *cached->second.closure;
	}

	void ActionSystem::forgetWall(EntityID blueprintEntity) {
		wallsHeldByControlled.erase(blueprintEntity);
		wallClosures.erase(blueprintEntity);
	}

	void ActionSystem::forgetGoneWalls() {
		const auto gone = [this](EntityID blueprintEntity) {
			const auto* blueprint = world->getComponent<StructureBlueprint>(blueprintEntity);
			return blueprint == nullptr || blueprint->phase == StructureBlueprint::BuildPhase::Complete;
		};
		std::erase_if(wallsHeldByControlled, gone);
		std::erase_if(wallClosures, [&gone](const auto& entry) { return gone(entry.first); });
	}

	bool ActionSystem::clearWallToComplete(EntityID blueprintEntity) {
		const auto*						 structure = world->getComponent<Structure>(blueprintEntity);
		const construction::WallSegment* segment = constructionWorld != nullptr && structure != nullptr && structure->kind == StructureKind::Wall
													   ? constructionWorld->getSegment(structure->graphId)
													   : nullptr;
		const std::optional<construction::WallBand> band =
			segment != nullptr ? construction::wallBand(*constructionWorld, *segment) : std::nullopt;
		if (!band.has_value()) {
			return true;
		}

		// What holds the wall, moving nobody: a builder of it it would shut in, whose work ends so he decides
		// again and goes to a side it allows; anyone else not under the player's control it would shut in who
		// is building another wall (waited for until that work ends, its route not found wanting) or is on
		// the way out (leaving);
		// and a colonist the player controls in its band. Someone whose task keeps them inside is not waited
		// for.
		const construction::WallClosure& closure = wallClosure(blueprintEntity, segment->id);
		bool							 shutsInSomeone = false;
		for (auto [colonist, position, task] : world->view<Position, Task>()) {
			if (!closure.shutsIn(position.value)) {
				continue;
			}
			if (task.type == TaskType::Build && task.buildBlueprintEntityId == static_cast<uint64_t>(blueprintEntity)) {
				shutsInSomeone = true;
				if (auto* work = world->getComponent<Action>(colonist)) {
					work->clear();
				}
			} else if (world->getComponent<PlayerControlled>(colonist) == nullptr &&
					   ((task.navState != NavState::CantFindWayTo && buildsAWall(task)) || leaving(task, closure))) {
				shutsInSomeone = true;
			}
		}
		EntityID controlledInBand = kInvalidEntity;
		for (auto [agent, position, radius] : world->view<Position, AgentRadius>()) {
			if (controlledInBand == kInvalidEntity && world->getComponent<PlayerControlled>(agent) != nullptr &&
				band->overlaps(position.value, radius.radiusMeters)) {
				controlledInBand = agent;
			}
		}
		if (controlledInBand != kInvalidEntity) {
			if (wallsHeldByControlled.insert(blueprintEntity).second && onWallHeld) {
				onWallHeld(blueprintEntity, controlledInBand);
			}
		} else {
			wallsHeldByControlled.erase(blueprintEntity);
		}
		if (shutsInSomeone || controlledInBand != kInvalidEntity) {
			return false;
		}

		const std::vector<construction::WallStand>& sides = closure.stands();
		for (auto [agent, position, radius] : world->view<Position, AgentRadius>()) {
			if (!band->overlaps(position.value, radius.radiusMeters)) {
				continue;
			}
			const auto side = std::find_if(sides.begin(), sides.end(), [&](const construction::WallStand& stand) { return stand.onSide(position.value); });
			const glm::vec2 outward = side != sides.end() ? side->outward : sides.front().outward;
			position.value = band->stepOut(position.value, radius.radiusMeters, outward);
			LOG_DEBUG(
				Engine, "[Action] Entity %llu stepped out of wall %llu as it completes, to (%.2f, %.2f)", static_cast<unsigned long long>(agent),
				static_cast<unsigned long long>(segment->id), position.value.x, position.value.y
			);
		}
		return true;
	}

} // namespace ecs
