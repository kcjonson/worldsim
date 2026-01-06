#pragma once

// StorageGoalSystem - Creates Haul goals for storage containers
//
// This system scans all storage containers and creates GoalTasks for
// containers that want items. It's the primary goal generator for Haul tasks.
//
// Design:
// - Runs periodically (not every frame)
// - One goal per storage container with available capacity
// - Goal specifies what items the storage accepts (from StorageConfiguration)
// - Colonists query goals and find fulfillment items in their Memory

#include "../EntityID.h"
#include "../ISystem.h"

#include <cstddef>
#include <cstdint>

namespace ecs {

	/// System that creates Haul goals from storage containers
	/// Priority: 55 (runs after NeedsDecay, before AIDecision)
	class StorageGoalSystem : public ISystem {
	  public:
		/// Update storage goals
		/// @param deltaTime Time since last frame (seconds)
		void update(float deltaTime) override;

		[[nodiscard]] int		  priority() const override { return 55; }
		[[nodiscard]] const char* name() const override { return "StorageGoal"; }

		/// Get count of storage goals currently active
		[[nodiscard]] size_t getActiveGoalCount() const { return activeGoalCount; }

	  private:

		// Throttle updates (not needed every frame)
		float	 updateTimer = 0.0F;
		float	 updateInterval = 1.0F; // Update every 1 second
		uint32_t frameCounter = 0;
		uint32_t updateFrameInterval = 60; // Alternative: update every 60 frames

		// Statistics
		size_t activeGoalCount = 0;

		/// Create or update a goal for a storage container
		void updateGoalForStorage(EntityID storageEntity);
	};

} // namespace ecs
