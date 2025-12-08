#pragma once

// Action System for Colonist Need Fulfillment
// Processes colonists that have arrived at their task targets and performs
// the appropriate action (eating, drinking, sleeping, toilet).
// Actions have duration and apply effects when complete.
// See /docs/design/game-systems/colonists/needs.md for design details.

#include "../ISystem.h"

namespace ecs {

class ActionSystem : public ISystem {
public:
	void update(float deltaTime) override;

	/// Priority 350: Runs after movement/physics, processes arrived colonists
	[[nodiscard]] int priority() const override { return 350; }

private:
	/// Start an action based on the task's need type
	void startAction(
		struct Task& task,
		struct Action& action,
		const struct Position& position,
		const struct Memory& memory
	);

	/// Process an in-progress action
	void processAction(
		float deltaTime,
		struct Action& action,
		struct NeedsComponent& needs,
		struct Task& task
	);

	/// Complete an action and apply effects
	void completeAction(struct Action& action, struct NeedsComponent& needs, struct Task& task);
};

} // namespace ecs
