#pragma once

// AI Decision System for Colonist Autonomous Behavior
// Evaluates options and selects a task via a lexicographic (tier, score) key (tier compared
// first, inviolable; score orders within a tier only). Tier ladder (lower = higher priority):
// - Tier 2: Critical needs (<10%)
// - Tier 4: Active work orders (queued craft/build + their provisioning chain) - STICKY
// - Tier 5: Actionable needs (below seek threshold)
// - Tier 6: Opportunistic work (loose hauls, stocking, undirected harvest)
// - Tier 7: Idle (gather food, wander)
// See /docs/technical/colonist-task-arbitration.md for the (tier, score) design.
// See /docs/design/game-systems/colonists/ai-behavior.md for the design intent.
// See /docs/design/game-systems/colonists/decision-trace.md for task queue display.

#include "../EntityID.h"
#include "../ISystem.h"
#include "../components/Needs.h"
#include "../components/Task.h"

#include <functional>
#include <glm/vec2.hpp>
#include <optional>
#include <random>

namespace engine::assets {
class AssetRegistry;
class PriorityConfig;
class RecipeRegistry;
}

namespace engine::world {
class ChunkManager;
}

namespace ecs {

class NavigationSystem;

namespace test {
class AIDecisionSystemTest;
}

class AIDecisionSystem : public ISystem {
	// Tests drive the private chain-interruption handler directly to assert the real
	// belt -> backpack -> drop ordering, not a stand-in for it.
	friend class test::AIDecisionSystemTest;

public:
	/// Construct with optional RNG seed (defaults to random_device for non-determinism)
	explicit AIDecisionSystem(
		const engine::assets::AssetRegistry& registry,
		const engine::assets::RecipeRegistry& recipeRegistry,
		std::optional<uint32_t> rngSeed = std::nullopt);

	void update(float deltaTime) override;

	/// Set the ChunkManager for terrain queries (required for smart toilet location)
	void setChunkManager(engine::world::ChunkManager* chunkManager) { m_chunkManager = chunkManager; }

	/// Set the NavigationSystem used to resolve a navmesh path when a task picks a destination.
	/// The running game always wires this; null is only for headless tests, where movement is a
	/// direct MovementTarget follow (no mesh exists to path over).
	void setNavigationSystem(NavigationSystem* navSystem) { m_navSystem = navSystem; }

	/// Set the colony origin: the validated, entity-cleared walkable clearing the colonist spawned
	/// into (the home anchor, owned by GameWorldState::Colony and pushed here because the engine
	/// can't see app state). The off-mesh recovery snaps a stranded colonist here as a last resort
	/// when nearestPathablePoint finds no walkable face in range, so a colonist can never be
	/// permanently stranded off the mesh. This is a read of the colony origin, not an independent copy.
	void setColonyOrigin(glm::vec2 origin) { m_colonyOrigin = origin; }

	/// Set callback for dropping items on the ground (when chain is interrupted)
	/// Same signature as ActionSystem's drop callback for consistency
	using DropItemCallback = std::function<void(const std::string& defName, float x, float y)>;
	void setDropItemCallback(DropItemCallback callback) { m_onDropItem = std::move(callback); }

	/// Enter direct player control: suspend this colonist's autonomous task selection. While the
	/// PlayerControlled tag is present, update() skips task (re)selection, so the colonist stands
	/// and waits until the player issues a move order. Abandons the current task and stops; there
	/// is no reservation system, so clearing the task is the whole "release its work" story, and
	/// any carried items ride along (handled normally once control is released). Vision is a
	/// separate system and keeps running.
	void enterControl(EntityID entity);

	/// Leave direct player control: remove the tag and clear the player-issued task so the next
	/// update() re-selects autonomously from a clean slate.
	void releaseControl(EntityID entity);

	/// Issue a player walk order to a (controlled) colonist. An unwalkable goal (water, inside a
	/// wall, off the sim area) snaps to the nearest pathable point. Sets a Moving task and resolves
	/// a navmesh route through the normal pipeline (so vision-driven repath and the arrival hand-off
	/// work for free). Returns the goal actually routed to (post-snap), or nullopt if nothing
	/// reachable is near the click (no order issued).
	[[nodiscard]] std::optional<glm::vec2> issuePlayerMoveOrder(EntityID entity, glm::vec2 worldGoal);

	[[nodiscard]] int priority() const override { return 60; }
	[[nodiscard]] const char* name() const override { return "AIDecision"; }

private:
	/// Generate a random position within wander radius
	[[nodiscard]] glm::vec2 generateWanderTarget(const glm::vec2& currentPos);

	/// Check if entity should re-evaluate its current task
	/// @param task Current task
	/// @param needs Current needs component
	/// @param action Optional action component (nullptr if entity has no Action component)
	/// @param movementTargetActive Whether the entity's MovementTarget is active (walking to a point);
	///        a moving Wander re-evaluates every tick only when true, so the no-option hold (movement off)
	///        keeps its periodic throttle
	[[nodiscard]] bool shouldReEvaluate(const struct Task& task, const struct NeedsComponent& needs, const struct Action* action, bool movementTargetActive);

	/// Build decision trace by evaluating all options
	/// Populates the trace with all needs + wander, sorted by priority
	/// @param currentTask Current task (used to preserve target when already pursuing a need)
	/// @param inventory Colonist inventory (for checking food availability)
	/// @param skills Optional colonist skills (for skill bonus calculation)
	void buildDecisionTrace(
		EntityID entity,
		const struct Position& position,
		const struct NeedsComponent& needs,
		const struct Memory& memory,
		const struct Task& currentTask,
		const struct Inventory& inventory,
		const struct Skills* skills,
		struct DecisionTrace& trace);

	/// Select task from the decision trace (picks first Selected option)
	void selectTaskFromTrace(
		EntityID entity,
		struct Task& task,
		struct MovementTarget& movementTarget,
		const struct DecisionTrace& trace,
		const struct Position& position);

	/// Resolve a navmesh path to `goal` for `entity` and attach it as a NavPath, planning over
	/// the colonist's structural belief (its known segments/openings). On success the NavPath is
	/// stamped with the belief/nav versions so the replan loop can detect staleness.
	///
	/// MOVEMENT INVARIANT: a colonist moves ONLY by following a NavPath. There is no straight-line
	/// "beeline" toward a goal without a route. When a path can't be produced, this STOPS the
	/// colonist (clears movementTarget.active, zeros velocity) rather than sliding it blind:
	///   - no mesh yet (startup window): hold, the re-eval picks the task back up once it lands;
	///   - endpoint outside the built sim area (beyond LOD0): hold, can't plan that leg yet;
	///   - mesh present but belief admits no route (a believed wall): stop, re-decide.
	/// The ONE non-path exception is the off-mesh recovery snap at the top of update() (teleport to
	/// nearest valid ground), which runs BEFORE any path request. The Unmeshed outcome exists only
	/// for headless tests with no NavigationSystem wired; it never occurs in the running game.
	/// Outcome -> nav state: only Blocked is "can't find a way"; Waiting/Unmeshed are not stuck.
	enum class NavRequestOutcome {
		Routed,	  // a believed route was found and attached
		Unmeshed, // no NavigationSystem wired at all (headless/tests only): direct move, not stuck
		Waiting,  // no path possible right now (mesh not built, or endpoint off-area): held in place
		Blocked,  // mesh exists but belief admits no route: colonist stopped
	};
	NavRequestOutcome requestNavPath(EntityID entity, const glm::vec2& goal, const struct Position& position,
									 const struct Memory& memory, struct MovementTarget& movementTarget);

	/// Format a human-readable reason for an option
	[[nodiscard]] static std::string formatOptionReason(
		const struct EvaluatedOption& option,
		const char* needName);

	/// Handle chain interruption when switching away from a mid-chain task
	/// Stows 1-handed items to backpack, drops 2-handed items or packaged entities.
	/// @param entity Entity ID for logging
	/// @param task Current task (must be mid-chain: chainId set, chainStep > 0)
	/// @param inventory Colonist inventory to modify
	/// @param position Colonist position (for dropping items)
	/// @param newTaskType Type of new task being switched to
	/// @param newNeedType Need type for FulfillNeed tasks
	void handleChainInterruption(
		EntityID entity,
		const Task& task,
		struct Inventory& inventory,
		const struct Position& position,
		TaskType newTaskType,
		NeedType newNeedType);

	const engine::assets::AssetRegistry& m_registry;
	const engine::assets::RecipeRegistry& m_recipeRegistry;

	/// ChunkManager for terrain queries (optional, fallback to current position if null)
	engine::world::ChunkManager* m_chunkManager = nullptr;

	/// NavigationSystem for navmesh path queries (null only in headless tests: direct movement)
	NavigationSystem* m_navSystem = nullptr;

	/// Colony origin: the validated walkable clearing the colonist spawned into, read from the
	/// single source (GameWorldState::Colony) via setColonyOrigin. Last-resort snap target for the
	/// off-mesh recovery. Unset until GameScene wires it; the recovery only uses it once a real
	/// value is present (it has no meaning in headless tests).
	std::optional<glm::vec2> m_colonyOrigin;

	/// How often to re-evaluate tasks (seconds)
	static constexpr float kReEvalInterval = 0.5F;

	/// Maximum distance for wander targets
	static constexpr float kWanderRadius = 8.0F;

	/// Monotonic seconds-since-start clock for this system (deltaTime accumulated each update).
	/// Used to stamp DecisionTrace::lastEvaluationTime (so the inspector can detect a rebuilt trace)
	/// and as the currentTime for the within-tier task-age term. A real game-time clock (TimeSystem)
	/// is not wired to this system yet; this monotonic accumulator is sufficient for both uses.
	float m_elapsedTime = 0.0F;

	/// Random number generator for wander behavior (seeded from random_device by default)
	std::mt19937 m_rng;

	/// Callback for dropping items on the ground (when chain is interrupted)
	DropItemCallback m_onDropItem = nullptr;
};

} // namespace ecs
