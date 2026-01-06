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
	std::unordered_set<EntityID> packagesWithGoals;
	for (const auto* goal : registry.getGoalsOfType(TaskType::PlacePackaged)) {
		packagesWithGoals.insert(goal->destinationEntity);
	}

	activeGoalCount = 0;

	// Query all entities with Packaged + Position
	for (auto [entity, packaged, position] : world->view<Packaged, Position>()) {
		// Only create goals for items that have a target position set
		if (!packaged.targetPosition.has_value()) {
			// No target position - player hasn't placed it yet
			// Remove goal if one exists
			registry.removeGoalByDestination(entity);
			packagesWithGoals.erase(entity);
			continue;
		}

		// Skip items currently being carried (they're already being handled)
		// But we still keep the goal active for priority tracking
		const glm::vec2& targetPos = *packaged.targetPosition;

		// Check if goal already exists
		const auto* existingGoal = registry.getGoalByDestination(entity);
		if (existingGoal != nullptr) {
			// Goal exists - verify it's still valid
			packagesWithGoals.erase(entity);
			activeGoalCount++;
			continue;
		}

		// Create new goal for this packaged item
		GoalTask goal;
		goal.type = TaskType::PlacePackaged;
		goal.destinationEntity = entity; // The packaged item entity
		goal.destinationPosition = targetPos;
		goal.destinationDefNameId = 0; // Could get from Appearance if available
		goal.acceptedCategory = engine::assets::ItemCategory::None;
		goal.targetAmount = 1; // One item to place
		goal.deliveredAmount = 0;
		goal.createdAt = 0.0F; // TODO: use actual game time

		registry.createGoal(std::move(goal));
		packagesWithGoals.erase(entity);
		activeGoalCount++;
	}

	// Remove goals for packaged entities that no longer exist
	// (were placed successfully or removed from world)
	for (EntityID oldPackage : packagesWithGoals) {
		registry.removeGoalByDestination(oldPackage);
	}
}

} // namespace ecs
