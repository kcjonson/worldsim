#pragma once

#include "../ISystem.h"

#include <cstddef>

namespace ecs {

class World;

/// BuildGoalSystem creates PlacePackaged goals from Packaged entities awaiting delivery.
///
/// When a player places a blueprint for furniture/structures, the game creates a
/// Packaged entity with a targetPosition set. This system:
/// 1. Scans for Packaged entities with targetPosition (awaiting placement)
/// 2. Creates/updates PlacePackaged goals in GoalTaskRegistry
/// 3. Removes goals when placement is complete or the target is cleared
///
/// It touches only goals it owns. A moved box's storage goal is StorageGoalSystem's to retire, and the
/// placement goal waits for the destination to come free.
///
/// Priority: 57 (after CraftingGoalSystem at 56)
class BuildGoalSystem : public ISystem {
  public:
	BuildGoalSystem() = default;

	void update(float deltaTime) override;

	[[nodiscard]] int priority() const override { return 57; }

	[[nodiscard]] const char* name() const override { return "BuildGoal"; }

	/// Debug: Get number of active placement goals
	[[nodiscard]] size_t getActiveGoalCount() const { return activeGoalCount; }

  private:
	size_t activeGoalCount = 0;

	// Throttling: only update every N frames
	int frameCounter = 0;
	static constexpr int updateFrameInterval = 30; // ~0.5s at 60fps
};

} // namespace ecs
