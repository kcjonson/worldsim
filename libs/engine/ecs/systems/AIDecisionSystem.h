#pragma once

// AI Decision System for Colonist Autonomous Behavior
// Evaluates colonist needs and assigns movement targets based on tier priority:
// - Tier 3: Critical Needs (<10%) - immediate fulfillment
// - Tier 5: Actionable Needs (below seek threshold, varies by need type) - seek fulfillment
// - Tier 7: Wander - random exploration when all needs satisfied
// See /docs/design/game-systems/colonists/ai-behavior.md for design details.

#include "../EntityID.h"
#include "../ISystem.h"

#include <glm/vec2.hpp>
#include <optional>
#include <random>

namespace engine::assets {
class AssetRegistry;
}

namespace ecs {

class AIDecisionSystem : public ISystem {
public:
	/// Construct with optional RNG seed (defaults to random_device for non-determinism)
	explicit AIDecisionSystem(
		const engine::assets::AssetRegistry& registry,
		std::optional<uint32_t> rngSeed = std::nullopt);

	void update(float deltaTime) override;

	[[nodiscard]] int priority() const override { return 60; }

private:
	/// Evaluate critical needs (Tier 3) - returns true if a task was assigned
	bool evaluateCriticalNeeds(
		EntityID entity,
		const struct NeedsComponent& needs,
		const struct Memory& memory,
		struct Task& task,
		const struct Position& position);

	/// Evaluate actionable needs (Tier 5) - returns true if a task was assigned
	bool evaluateActionableNeeds(
		EntityID entity,
		const struct NeedsComponent& needs,
		const struct Memory& memory,
		struct Task& task,
		const struct Position& position);

	/// Assign wander behavior (Tier 7)
	void assignWander(EntityID entity, struct Task& task, const struct Position& position);

	/// Generate a random position within wander radius
	[[nodiscard]] glm::vec2 generateWanderTarget(const glm::vec2& currentPos);

	/// Check if entity should re-evaluate its current task
	[[nodiscard]] bool shouldReEvaluate(const struct Task& task, const struct NeedsComponent& needs);

	const engine::assets::AssetRegistry& m_registry;

	/// How often to re-evaluate tasks (seconds)
	static constexpr float kReEvalInterval = 0.5F;

	/// Maximum distance for wander targets
	static constexpr float kWanderRadius = 8.0F;

	/// Random number generator for wander behavior (seeded from random_device by default)
	std::mt19937 m_rng;
};

} // namespace ecs
