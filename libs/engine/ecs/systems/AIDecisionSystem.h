#pragma once

// AI Decision System for Colonist Autonomous Behavior
// Evaluates colonist needs and assigns movement targets based on tier priority:
// - Tier 3: Critical Needs (<10%) - immediate fulfillment
// - Tier 5: Actionable Needs (below seek threshold, varies by need type) - seek fulfillment
// - Tier 6: Gather Food - proactive harvesting when no food in inventory
// - Tier 7: Wander - random exploration when all needs satisfied
// See /docs/design/game-systems/colonists/ai-behavior.md for design details.
// See /docs/design/game-systems/colonists/decision-trace.md for task queue display.

#include "../EntityID.h"
#include "../ISystem.h"

#include <glm/vec2.hpp>
#include <optional>
#include <random>

namespace engine::assets {
class AssetRegistry;
class RecipeRegistry;
}

namespace engine::world {
class ChunkManager;
}

namespace ecs {

class AIDecisionSystem : public ISystem {
public:
	/// Construct with optional RNG seed (defaults to random_device for non-determinism)
	explicit AIDecisionSystem(
		const engine::assets::AssetRegistry& registry,
		const engine::assets::RecipeRegistry& recipeRegistry,
		std::optional<uint32_t> rngSeed = std::nullopt);

	void update(float deltaTime) override;

	/// Set the ChunkManager for terrain queries (required for smart toilet location)
	void setChunkManager(engine::world::ChunkManager* chunkManager) { m_chunkManager = chunkManager; }

	[[nodiscard]] int priority() const override { return 60; }

private:
	/// Generate a random position within wander radius
	[[nodiscard]] glm::vec2 generateWanderTarget(const glm::vec2& currentPos);

	/// Check if entity should re-evaluate its current task
	/// @param task Current task
	/// @param needs Current needs component
	/// @param action Optional action component (nullptr if entity has no Action component)
	[[nodiscard]] bool shouldReEvaluate(const struct Task& task, const struct NeedsComponent& needs, const struct Action* action);

	/// Build decision trace by evaluating all options
	/// Populates the trace with all needs + wander, sorted by priority
	/// @param currentTask Current task (used to preserve target when already pursuing a need)
	/// @param inventory Colonist inventory (for checking food availability)
	void buildDecisionTrace(
		EntityID entity,
		const struct Position& position,
		const struct NeedsComponent& needs,
		const struct Memory& memory,
		const struct Task& currentTask,
		const struct Inventory& inventory,
		struct DecisionTrace& trace);

	/// Select task from the decision trace (picks first Selected option)
	void selectTaskFromTrace(
		struct Task& task,
		struct MovementTarget& movementTarget,
		const struct DecisionTrace& trace,
		const struct Position& position);

	/// Format a human-readable reason for an option
	[[nodiscard]] static std::string formatOptionReason(
		const struct EvaluatedOption& option,
		const char* needName);

	const engine::assets::AssetRegistry& m_registry;
	const engine::assets::RecipeRegistry& m_recipeRegistry;

	/// ChunkManager for terrain queries (optional, fallback to current position if null)
	engine::world::ChunkManager* m_chunkManager = nullptr;

	/// How often to re-evaluate tasks (seconds)
	static constexpr float kReEvalInterval = 0.5F;

	/// Minimum priority gap required to switch tasks while an action is in progress.
	/// This prevents minor priority fluctuations from causing task switches, but allows
	/// emergencies (fires, critical needs ~300 vs actionable ~100) to interrupt.
	/// Example: Current task priority 110, new task 115 (gap 5) → NO switch
	/// Example: Current task priority 110, new task 305 (gap 195) → SWITCH
	static constexpr float kPrioritySwitchThreshold = 50.0F;

	/// Maximum distance for wander targets
	static constexpr float kWanderRadius = 8.0F;

	/// Random number generator for wander behavior (seeded from random_device by default)
	std::mt19937 m_rng;
};

} // namespace ecs
