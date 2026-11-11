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

#include "../ClaimIndex.h"
#include "../DropResourceCallback.h"
#include "../EntityID.h"
#include "../GoalTaskRegistry.h"
#include "../ISystem.h"
#include "../components/MemoryQueries.h"
#include "../components/Needs.h"
#include "../components/Task.h"
#include "GoalSystemHelpers.h"
#include "WorkSources.h"

#include <functional>
#include <glm/vec2.hpp>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace assets {
class AssetRegistry;
class PriorityConfig;
class RecipeRegistry;
}

namespace world {
class ChunkManager;
}

namespace ecs {

class LoosePileIndex;
class NavigationSystem;

/// One kind of goods a deciding colonist carries, read once per decision for the carry-in.
struct CarriedKind {
	uint32_t item = 0;
	uint32_t quantity = 0;
	bool	 edible = false;
};

namespace test {
class AIDecisionSystemTest;
}

class AIDecisionSystem : public ISystem {
	// Tests drive the private chain-interruption handler directly to assert the real
	// belt -> backpack -> drop ordering, not a stand-in for it.
	friend class test::AIDecisionSystemTest;

public:
	explicit AIDecisionSystem(
		const assets::AssetRegistry& registry,
		const assets::RecipeRegistry& recipeRegistry,
		uint32_t rngSeed);

	void update(float deltaTime) override;

	/// Set the ChunkManager for terrain queries (required for smart toilet location)
	void setChunkManager(world::ChunkManager* chunkManager) { m_chunkManager = chunkManager; }

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

	/// Set callback for dropping loose units on the ground when a chain is interrupted.
	void setDropResourceCallback(DropResourceCallback callback) { onDropResource = std::move(callback); }

	/// When a harvestable the colonist remembers is ready to give its yield again while it regrows from
	/// an earlier cut, on the simulation clock; nullopt when it is ready now. Colonists read the live
	/// state of sources they already know, as they read claims; this never reveals a source. Asked
	/// lazily, only of the candidate that would otherwise win, so a regrowing bush is passed over for the
	/// next option and taken again once it is ready. `now` is TimeSystem::simulationSeconds for this
	/// update. Unwired (unit contexts with no world behind them), every source is ready.
	using SourceRegrowthCallback = std::function<std::optional<double>(uint32_t defNameId, glm::vec2 position, double now)>;
	void setSourceRegrowthCallback(SourceRegrowthCallback callback) { onSourceRegrowth = std::move(callback); }

	/// Enter direct player control: suspend this colonist's autonomous task selection. While the
	/// PlayerControlled tag is present, update() skips task (re)selection, so the colonist stands
	/// and waits until the player issues a move order. Abandons the current task and its suspended
	/// record, whose claims go with them, so the work returns to the pool at once; any carried items
	/// ride along uncommitted. Vision is a separate system and keeps running.
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

	/// Every colonist's claims, as of the last update in which a colonist decided. Readers outside
	/// this system (the task panel) see it that stale.
	[[nodiscard]] const ClaimIndex& claimIndex() const { return m_claims; }

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
	/// @param sourceReach The colonist's remembered reach verdicts for its known sources
	void buildDecisionTrace(
		EntityID entity,
		const struct Position& position,
		const struct NeedsComponent& needs,
		const struct Memory& memory,
		const struct Task& currentTask,
		const struct Inventory& inventory,
		const struct Skills* skills,
		struct SourceReachMemo& sourceReach,
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
	/// colonist (clears movementTarget.active, which MovementSystem stops) rather than sliding it blind:
	///   - no mesh yet (startup window): hold, deferred until a mesh build lands;
	///   - endpoint outside the built sim area (beyond LOD0): hold, deferred the same way;
	///   - mesh present but belief admits no route (a believed wall): stop, re-decide.
	/// The ONE non-path exception is the off-mesh recovery snap at the top of update() (teleport to
	/// nearest valid ground), which runs BEFORE any path request. The Unmeshed outcome exists only
	/// for headless tests with no NavigationSystem wired; it never occurs in the running game.
	/// Every caller records the outcome on its task through applyNavOutcome.
	enum class NavRequestOutcome {
		Routed,	  // a believed route was found and attached
		Unmeshed, // no NavigationSystem wired at all (headless/tests only): direct move, not stuck
		Waiting,  // no path possible right now (mesh not built, or endpoint off-area): held in place
		Blocked,  // mesh exists but belief admits no route: colonist stopped
	};
	NavRequestOutcome requestNavPath(EntityID entity, const glm::vec2& goal, const struct Position& position,
									 const struct Memory& memory, struct MovementTarget& movementTarget);

	/// Record a route request's outcome as the task's nav state. Only Blocked is "can't find a way".
	/// Waiting defers the route (AwaitingMesh) and stamps the current nav generation, so update()
	/// requests it again once the generation moves; Routed and Unmeshed are ordinary travel.
	void applyNavOutcome(struct Task& task, NavRequestOutcome outcome) const;

	/// Handle chain interruption when switching away from a mid-chain task
	/// Stows 1-handed items to backpack, drops 2-handed items or packaged entities.
	/// @param entity Entity ID for logging
	/// When the new task is a need and the colonist keeps a haul's goods, the haul becomes the task's
	/// suspended record (task-claims.md, interruption by the colonist's own needs).
	/// @param task Current task, carrying its chain's goods (past its pickup, or a cut with cargo)
	/// @param inventory Colonist inventory to modify
	/// @param position Colonist position (for dropping items)
	/// @param newTaskType Type of new task being switched to
	/// @param newNeedType Need type for FulfillNeed tasks
	void handleChainInterruption(
		EntityID entity,
		Task& task,
		struct Inventory& inventory,
		const struct Position& position,
		TaskType newTaskType,
		NeedType newNeedType);

	/// A just-accepted task takes its instance id and records its chain's claims; its suspended record
	/// is kept (a need), adopted (the carry-in of the same goods, or a further cut carrying them), or
	/// discarded (anything else). `handedCargo` is the gathering it continues, whose instance and cargo
	/// it keeps. The result is appended to the index so colonists deciding later this update see it.
	void acceptClaims(
		EntityID entity, Task& task, const struct Memory& memory, const struct Inventory& inventory,
		const std::optional<SuspendedWork>& handedCargo
	);

	/// Rebuild the claim index the first time a colonist decides in this update.
	void ensureClaimIndex();

	const assets::AssetRegistry& m_registry;
	const assets::RecipeRegistry& m_recipeRegistry;

	/// ChunkManager for terrain queries (optional, fallback to current position if null)
	world::ChunkManager* m_chunkManager = nullptr;

	/// NavigationSystem for navmesh path queries (null only in headless tests: direct movement)
	NavigationSystem* m_navSystem = nullptr;

	/// Colony origin: the validated walkable clearing the colonist spawned into, read from the
	/// single source (GameWorldState::Colony) via setColonyOrigin. Last-resort snap target for the
	/// off-mesh recovery. Unset until GameScene wires it; the recovery only uses it once a real
	/// value is present (it has no meaning in headless tests).
	std::optional<glm::vec2> m_colonyOrigin;

	/// Reused candidate storage for need-source queries, so a decision allocates nothing once it has grown
	std::vector<SourceCandidate> sourceScratch;

	/// The deciding colonist's remembered work sources, reused across colonists
	WorkSources m_workSources;

	/// The storage boxes the colony knows, worked out at most once per update
	ColonyStorageKnowledge m_colonyStorage;

	/// Every root's remaining need, worked out at most once per (root, item, basis) per update
	RemainingNeedCache m_needs;

	/// The deciding colonist's carried kinds, reused across colonists
	std::vector<CarriedKind> m_carried;

	/// ActionSystem's piles by world key, for a remembered pile's live count; read once per update,
	/// null in a world without an ActionSystem (a pile's count is then unknown)
	const LoosePileIndex* m_loosePiles = nullptr;

	/// Every task's claims: rebuilt when the first colonist decides in an update, then appended to by
	/// each acceptance in it.
	ClaimIndex m_claims;
	bool	   m_claimsBuilt = false;
#ifdef DEVELOPMENT_BUILD
	/// The validator's fresh rebuild, kept so it reuses its storage.
	ClaimIndex m_claimsCheck;
#endif

	/// SourceReachMemo::Key::stamp() of the colonist being decided this update (0 without a mesh)
	std::uint64_t reachStamp = 0;

	/// How often to re-evaluate tasks (seconds)
	static constexpr float kReEvalInterval = 0.5F;

	/// Maximum distance for wander targets
	static constexpr float kWanderRadius = 8.0F;

	/// The frame deltaTime this system was updated with, summed: unscaled by game speed and still
	/// running while paused, like the re-decide timer (Task::timeSinceEvaluation). Stamps
	/// DecisionTrace::lastEvaluationTime and is the clock of the within-tier task-age term. Regrowth
	/// waits run on TimeSystem::simulationSeconds instead, the clock regrowth is stamped on.
	float m_elapsedTime = 0.0F;

	/// TimeSystem::simulationSeconds, read once per update while onSourceRegrowth is wired (0 otherwise:
	/// nothing regrows then, so nothing reads it)
	double m_simulationNow = 0.0;

	/// Random number generator for wander behavior
	std::mt19937 m_rng;

	/// Callback for dropping items on the ground (when chain is interrupted)
	DropResourceCallback onDropResource = nullptr;

	/// Callback asking the world when a remembered harvestable is ready to give again
	SourceRegrowthCallback onSourceRegrowth = nullptr;

	/// onSourceRegrowth for a remembered source of `defNameId` at `position`; nullopt (ready) when unwired
	[[nodiscard]] std::optional<double> sourceRegrowingUntil(uint32_t defNameId, glm::vec2 position) const;
};

} // namespace ecs
