#include "BuildGoalSystem.h"

#include "../GoalTaskRegistry.h"
#include "../World.h"
#include "../components/Packaged.h"
#include "../components/Transform.h"

namespace ecs {

void BuildGoalSystem::update(float /*deltaTime*/) {
	if (world == nullptr) {
		return;
	}

	// Throttle: only update every N frames
	frameCounter++;
	if (frameCounter < updateFrameInterval) {
		return;
	}
	frameCounter = 0;

	auto& registry = GoalTaskRegistry::Get();

	// Track existing goals to detect removed/completed packaged items
	// Using ownership: only track goals that BuildGoalSystem created
	std::unordered_set<EntityID> packagesWithGoals;
	for (const auto* goal : registry.getGoalsByOwner(GoalOwner::BuildGoalSystem)) {
		packagesWithGoals.insert(goal->destinationEntity);
	}

	activeGoalCount = 0;

	// Query all entities with Packaged + Position
	for (auto [entity, packaged, position] : world->view<Packaged, Position>()) {
		// Another system's goal on the entity (a moved box's storage umbrella, which StorageGoalSystem
		// retires at its next poll) is not ours to remove or overwrite: wait for the destination to come free
		const auto* existingGoal = registry.getGoalByDestination(entity);
		if (existingGoal != nullptr && existingGoal->owner != GoalOwner::BuildGoalSystem) {
			continue;
		}

		// No target position - player hasn't placed it yet
		if (!packaged.targetPosition.has_value()) {
			if (existingGoal != nullptr) {
				registry.removeGoal(existingGoal->id);
			}
			packagesWithGoals.erase(entity);
			continue;
		}

		packagesWithGoals.erase(entity);
		activeGoalCount++;
		if (existingGoal != nullptr) {
			continue;
		}

		GoalTask goal;
		goal.type = TaskType::PlacePackaged;
		goal.owner = GoalOwner::BuildGoalSystem;
		goal.destinationEntity = entity; // The packaged item entity
		goal.destinationPosition = *packaged.targetPosition;

		registry.createGoal(std::move(goal));
	}

	// Remove goals for packaged entities that no longer exist
	// (were placed successfully or removed from world)
	for (EntityID oldPackage : packagesWithGoals) {
		registry.removeGoalByDestination(oldPackage);
	}
}

} // namespace ecs
