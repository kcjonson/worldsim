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
#include <construction/WallStand.h>

#include <utils/Log.h>

#include <algorithm>
#include <optional>
#include <vector>

namespace ecs {

	void ActionSystem::applyProgressEffect(const Action& action) {
		const auto& progressEff = action.progressEffect();
		auto		blueprintEntity = static_cast<EntityID>(progressEff.targetEntityId);

		if (progressEff.deconstruct) {
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

		const bool deconstruct = (task.type == TaskType::Deconstruct);

		// Build requires the blueprint to be under construction (materials staged) and not ordered
		// demolished; Deconstruct requires there to be work to undo. A no-op start clears the action
		// so the colonist re-evaluates rather than spinning on a finished or not-yet-ready blueprint.
		if (deconstruct) {
			if (blueprint->workDone <= 0.0F) {
				LOG_DEBUG(
					Engine,
					"[Action] Deconstruct skipped: blueprint %llu has no work to undo",
					static_cast<unsigned long long>(task.buildBlueprintEntityId)
				);
				action.clear();
				return;
			}
		} else {
			if (blueprint->phase != StructureBlueprint::BuildPhase::UnderConstruction || blueprint->demolishing) {
				LOG_DEBUG(
					Engine,
					"[Action] Build skipped: blueprint %llu not buildable (phase %d, demolishing %d)",
					static_cast<unsigned long long>(task.buildBlueprintEntityId),
					static_cast<int>(blueprint->phase),
					blueprint->demolishing ? 1 : 0
				);
				action.clear();
				return;
			}
		}

		// Capture the builder's Construction skill to scale the work rate. Skills is optional;
		// a colonist without it (or untrained) still builds at the base rate.
		float skillLevel = 0.0F;
		if (const auto* skills = world->getComponent<Skills>(entity)) {
			skillLevel = skills->getLevel("Construction");
		}

		action = deconstruct ? Action::Deconstruct(task.buildBlueprintEntityId, task.targetPosition, skillLevel)
							  : Action::Build(task.buildBlueprintEntityId, task.targetPosition, skillLevel);

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

	bool ActionSystem::advanceConstructionWork(float deltaTime, Action& action) {
		// Transition Starting -> InProgress on the first tick, mirroring processAction.
		if (action.state == ActionState::Starting) {
			action.state = ActionState::InProgress;
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
		if (progressEff.deconstruct) {
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

		if (progressEff.deconstruct) {
			blueprint->workDone -= delta;
			blueprint->teardownBegun = true;
			if (blueprint->workDone <= 0.0F) {
				blueprint->workDone = 0.0F;
				action.state = ActionState::Complete;
				return true;
			}
		} else {
			// The work can be done while the wall is not: it waits there for its band to be clear.
			blueprint->workDone = std::min(blueprint->workDone + delta, blueprint->workTotal);
			if (blueprint->workDone >= blueprint->workTotal && wallBandClear(blueprintEntity)) {
				blueprint->phase = StructureBlueprint::BuildPhase::Complete;
				action.state = ActionState::Complete;
				return true;
			}
		}

		return false;
	}

	bool ActionSystem::wallBandClear(EntityID blueprintEntity) {
		const auto*						   structure = world->getComponent<Structure>(blueprintEntity);
		const construction::WallSegment* segment = m_constructionWorld != nullptr && structure != nullptr && structure->kind == StructureKind::Wall
													   ? m_constructionWorld->getSegment(structure->graphId)
													   : nullptr;
		const std::optional<construction::WallBand> band =
			segment != nullptr ? construction::wallBand(*m_constructionWorld, *segment) : std::nullopt;
		if (!band.has_value()) {
			return true;
		}
		bool								 clear = true;
		std::vector<construction::WallStand> sides;
		for (auto [agent, position, radius] : world->view<Position, AgentRadius>()) {
			if (!band->overlaps(position.value, radius.radiusMeters)) {
				continue;
			}
			if (world->getComponent<PlayerControlled>(agent) != nullptr) {
				clear = false;
				continue;
			}
			if (sides.empty()) {
				sides = construction::wallStands(*m_constructionWorld, segment->id);
			}
			const auto side = std::find_if(sides.begin(), sides.end(), [&](const construction::WallStand& stand) { return stand.onSide(position.value); });
			const glm::vec2 outward = side != sides.end() ? side->outward : sides.front().outward;
			position.value = band->stepOut(position.value, radius.radiusMeters, outward);
			LOG_DEBUG(
				Engine, "[Action] Entity %llu stepped out of wall %llu as it completes, to (%.2f, %.2f)", static_cast<unsigned long long>(agent),
				static_cast<unsigned long long>(segment->id), position.value.x, position.value.y
			);
		}
		return clear;
	}

} // namespace ecs
