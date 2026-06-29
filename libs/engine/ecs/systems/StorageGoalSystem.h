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

namespace engine::assets {
	class AssetRegistry;
}

namespace ecs {

	class GoalTaskRegistry;
	struct StorageConfiguration;
	struct Inventory;

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
		/// Emit/maintain stocking Harvest goals for a storage's specific-item rules whose minAmount
		/// exceeds the count already in the box, when a colonist knows a harvestable source. Each is
		/// a child of the storage's umbrella Haul goal. Drives the chop -> carry-in chain that fills
		/// the box to its configured minimum; lower priority than construction/craft work.
		void reconcileStockingHarvests(
			GoalTaskRegistry&					 registry,
			const engine::assets::AssetRegistry& assetRegistry,
			const StorageConfiguration&			 config,
			const Inventory&					 inventory,
			uint64_t							 umbrellaId,
			float								 maxColonistCarryKg
		);


		// Throttle updates (not needed every frame)
		uint32_t				 frameCounter = 0;
		static constexpr uint32_t updateFrameInterval = 60; // Update every 60 frames

		// Statistics
		size_t activeGoalCount = 0;
	};

} // namespace ecs
