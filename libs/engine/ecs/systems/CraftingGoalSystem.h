#pragma once

// CraftingGoalSystem - Creates Craft goals from crafting stations
//
// This system scans all crafting stations with WorkQueues and creates GoalTasks
// for stations with pending work. It's the primary goal generator for Craft tasks.
//
// Design:
// - Runs periodically (not every frame)
// - One goal per crafting station with pending work
// - Goal specifies the recipe to craft and station location
// - Colonists query goals to find crafting work

#include "../EntityID.h"
#include "../ISystem.h"

#include <cstddef>
#include <cstdint>

namespace ecs {

	/// System that creates Craft goals from crafting stations with WorkQueues
	/// Priority: 56 (runs after StorageGoalSystem, before AIDecision)
	class CraftingGoalSystem : public ISystem {
	  public:
		/// Update crafting goals
		/// @param deltaTime Time since last frame (seconds)
		void update(float deltaTime) override;

		[[nodiscard]] int		  priority() const override { return 56; }
		[[nodiscard]] const char* name() const override { return "CraftingGoal"; }

		/// Get count of crafting goals currently active
		[[nodiscard]] size_t getActiveGoalCount() const { return activeGoalCount; }

	  private:
		// Throttle updates (not needed every frame)
		uint32_t frameCounter = 0;
		uint32_t updateFrameInterval = 60; // Update every 60 frames

		// Statistics
		size_t activeGoalCount = 0;
	};

} // namespace ecs
