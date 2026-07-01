#include "ColonistAdapter.h"

#include <ecs/components/Action.h>
#include <ecs/components/Colonist.h>
#include <ecs/components/Mood.h>
#include <ecs/components/Needs.h>
#include <ecs/components/PlayerControlled.h>
#include <ecs/components/StructureBlueprint.h>
#include <ecs/components/Task.h>

namespace world_sim::adapters {

	namespace {
		// Concise verb for the roster tile's meter. Kept short for the narrow card; the
		// dossier carries the richer, nav-aware phrasing.
		std::string taskLabel(ecs::TaskType type) {
			switch (type) {
				case ecs::TaskType::FulfillNeed:   return "Tending need";
				case ecs::TaskType::Craft:         return "Crafting";
				case ecs::TaskType::Haul:          return "Hauling";
				case ecs::TaskType::PlacePackaged: return "Placing";
				case ecs::TaskType::Harvest:       return "Harvesting";
				case ecs::TaskType::Build:         return "Building";
				case ecs::TaskType::Deconstruct:   return "Deconstructing";
				case ecs::TaskType::Wander:        return "Wandering";
				default:                           return "";
			}
		}
	} // namespace

	ColonistActivity getColonistActivity(ecs::World& world, ecs::EntityID colonist) {
		const auto* task = world.getComponent<ecs::Task>(colonist);
		if (task == nullptr || !task->isActive()) {
			return {"", -1.0F};
		}

		// Progress comes from the running action, if it has started. Before that (still
		// walking over) the colonist has a task but no live action, so progress stays <0
		// and the tile shows the labelled-but-empty track.
		float progress = -1.0F;
		if (const auto* action = world.getComponent<ecs::Action>(colonist); action != nullptr && action->isActive()) {
			if (action->hasProgressEffect()) {
				// Build/Deconstruct advance the blueprint's workDone, not the action clock.
				// Deconstruct counts workDone DOWN, so its meter is the complement (see
				// StructureBlueprint::displayProgress) -- raw progress() would run backwards.
				if (task->buildBlueprintEntityId != 0) {
					if (const auto* bp = world.getComponent<ecs::StructureBlueprint>(static_cast<ecs::EntityID>(task->buildBlueprintEntityId))) {
						progress = bp->displayProgress(task->type == ecs::TaskType::Deconstruct);
					}
				}
			} else {
				progress = action->progress();
			}
		}

		return {taskLabel(task->type), progress};
	}

	std::vector<ColonistData> getColonists(ecs::World& world) {
		std::vector<ColonistData> result;

		// Query all colonist entities
		for (auto [entity, colonist] : world.view<ecs::Colonist>()) {
			float mood = 100.0F; // Default to full mood

			// Compute mood from needs if available
			if (auto* needs = world.getComponent<ecs::NeedsComponent>(entity)) {
				mood = ecs::computeMood(*needs);
			}

			ColonistActivity activity = getColonistActivity(world, entity);
			result.push_back({
				.id = entity,
				.name = colonist.name,
				.mood = mood,
				.activity = std::move(activity.label),
				.activityProgress = activity.progress,
				.playerControlled = world.getComponent<ecs::PlayerControlled>(entity) != nullptr,
			});
		}

		return result;
	}

} // namespace world_sim::adapters
