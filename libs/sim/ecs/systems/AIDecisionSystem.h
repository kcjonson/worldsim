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
#include "../components/SourceReachMemo.h"
#include "../components/Task.h"
#include "GoalSystemHelpers.h"
#include "WorkSources.h"

#include <array>
#include <functional>
#include <limits>
#include <glm/vec2.hpp>
#include <optional>
#include <random>
#include <span>
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

struct EvaluatedOption;
class LoosePileIndex;
class NavigationSystem;

/// The colonist whose task this is decides at the AI's next update: shouldReEvaluate's interval check
/// passes. The one way another system asks for a decision now.
inline void requestDecision(Task& task) {
	task.timeSinceEvaluation = std::numeric_limits<float>::max();
}

/// One kind of goods a deciding colonist carries, read once per decision for the carry-in.
struct CarriedKind {
	uint32_t item = 0;
	uint32_t quantity = 0;
	bool	 edible = false;
};

/// A root goal and one item it wants.
struct RootItem {
	uint64_t root = 0;
	uint32_t item = 0;
};

/// What a storage box offers each item before inbound claims are taken out: the headroom in that
/// item's partial stacks, its stack size, and the box's free slots. None of it depends on who asks, so
/// each (box, item) is read from the world once per update; every colonist then takes its own claim
/// exclusion out of the claim index on top. Box contents change outside an AI update, except when a
/// colonist inside one puts goods away in a box within reach (storeOrDrop); the AI then refreshes that
/// box. It also holds each (box, item)'s configured StoragePriority, which only the player changes,
/// read the first time an option asks for it. An open table stamped with the update, so a warm cache
/// allocates nothing.
class BoxRoomCache {
  public:
	/// A new update: every slot of the last one reads as empty and none counts toward the load, so the
	/// table stays sized to one update's reads.
	void invalidate() {
		++pass;
		used = 0;
	}

	struct Room {
		InboundStackRoom stack;
		uint32_t		 freeSlots = 0;
	};
	/// The box's room for `item`, or nullopt when the box has no Inventory.
	[[nodiscard]] std::optional<Room> get(World& world, const assets::AssetRegistry& registry, EntityID box, uint32_t item);

	/// The box's StoragePriority for `item` (StorageConfiguration::getPriorityFor), or nullopt when the box
	/// has no StorageConfiguration or the item no definition.
	[[nodiscard]] std::optional<StoragePriority> priority(World& world, const assets::AssetRegistry& registry, EntityID box, uint32_t item);

	/// Read again, from the world, every item of this update's entries for `box`.
	void refresh(World& world, const assets::AssetRegistry& registry, EntityID box);

#ifdef DEVELOPMENT_BUILD
	/// (box, item) pairs read from the world since construction; tests read it to show each is read once
	/// per update, and an item no source offers not at all.
	uint64_t readOut = 0;
	/// (box, item) priorities read from the world since construction.
	uint64_t priorityReadOut = 0;
	[[nodiscard]] size_t capacity() const { return slots.size(); }
#endif

  private:
	struct Slot {
		uint64_t pass = 0;
		EntityID box = kInvalidEntity;
		uint32_t item = 0;
		bool	 roomRead = false;
		bool	 hasStore = false;
		Room	 room;
		bool						   priorityRead = false;
		std::optional<StoragePriority> priority;
	};
	/// This update's slot for (box, item), added unread when it has none.
	Slot& slotFor(EntityID box, uint32_t item);
	void  read(World& world, const assets::AssetRegistry& registry, Slot& slot);
	uint64_t		  pass = 1;
	uint32_t		  used = 0;
	std::vector<Slot> slots;
};

/// What every colonist deciding in one update reads alike of the colony's work: the stations with a
/// job and whether their stores hold its inputs, the blueprints ready to build or tear down, the
/// packaged furniture awaiting placement, and the lower-priority boxes a storage goal may pull an item
/// from. Each is worked out the first time a colonist asks in an update; the colonist's own parts
/// (claims, distance, carry) are applied on top. A colonist inside the update that puts goods in a box
/// (storeOrDrop) or sets furniture down changes what feeds them, and has them worked out again.
class SharedWork {
  public:
	/// A new update: nothing worked out yet.
	void invalidate();

	/// A box's contents changed inside the update.
	void boxChanged();

	/// A packaged item was set down inside the update.
	void packagesChanged() { packagesBuilt = false; }

	struct Station {
		EntityID	entity = kInvalidEntity;
		glm::vec2	position{0.0F, 0.0F};
		std::string recipeDefName;
		bool		hasAllInputs = false; // the station's store holds every input of its next job
	};
	/// Stations whose next job names a known recipe, in view order.
	[[nodiscard]] const std::vector<Station>& stations(World& world, const assets::RecipeRegistry& recipes);

	struct Site {
		uint64_t  goalId = 0;
		EntityID  blueprint = kInvalidEntity;
		glm::vec2 position{0.0F, 0.0F};
	};
	/// Available Build goals whose blueprint is UnderConstruction, in goal order.
	[[nodiscard]] const std::vector<Site>& buildSites(World& world);
	/// Available Deconstruct goals whose blueprint has work to undo, in goal order.
	[[nodiscard]] const std::vector<Site>& teardownSites(World& world);

	struct Package {
		EntityID  entity = kInvalidEntity;
		glm::vec2 position{0.0F, 0.0F};
		glm::vec2 target{0.0F, 0.0F};
		bool	  beingCarried = false;
	};
	/// Packaged items with a placement target, in view order.
	[[nodiscard]] const std::vector<Package>& packages(World& world);

	struct PullSource {
		EntityID  box = kInvalidEntity;
		glm::vec2 position{0.0F, 0.0F};
		uint32_t  drainable = 0; // what it holds above its own minimum for the item
	};
	/// The boxes `goal` may pull `item` from: another box, not being moved, holding some of it above its
	/// own minimum, ranked strictly lower for it than `destPriority`, and known to the colony. View order.
	[[nodiscard]] std::span<const PullSource> pullSources(
		World& world, const assets::AssetRegistry& registry, ColonyStorageKnowledge& storageKnowledge, const GoalTask& goal, uint32_t item,
		const assets::AssetDefinition& itemDef, StoragePriority destPriority
	);

  private:
	std::vector<Station> stationList;
	std::vector<Site>	 buildList;
	std::vector<Site>	 teardownList;
	std::vector<Package> packageList;
	bool				 stationsBuilt = false;
	bool				 buildSitesBuilt = false;
	bool				 teardownSitesBuilt = false;
	bool				 packagesBuilt = false;

	struct PullEntry {
		uint64_t goalId = 0;
		uint32_t item = 0;
		uint32_t begin = 0;
		uint32_t end = 0;
	};
	std::vector<PullEntry>	pullEntries;
	std::vector<PullSource> pullList;
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

	/// Where packaged furniture a colonist sets down at his feet (`at`) lands: walkable ground off every
	/// unbuilt footprint, by the loose-drop rule. Unwired (unit contexts with no world behind them), at his feet.
	using SetDownSpotCallback = std::function<glm::vec2(glm::vec2 at)>;
	void setSetDownSpotCallback(SetDownSpotCallback callback) { onSetDownSpot = std::move(callback); }

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

	/// PriorityConfig's base tier of each TaskType, indexed by the type
	using BaseTiers = std::array<int, static_cast<size_t>(TaskType::Wander) + 1>;
	[[nodiscard]] const char* name() const override { return "AIDecision"; }

private:
	/// Generate a random position within wander radius
	[[nodiscard]] glm::vec2 generateWanderTarget(const glm::vec2& currentPos);

	/// Check if entity should re-evaluate its current task
	/// @param task Current task
	/// @param needs Current needs component
	/// @param movementTargetActive Whether the entity's MovementTarget is active (walking to a point);
	///        a moving Wander re-evaluates every tick only when true, so the no-option hold (movement off)
	///        keeps its periodic throttle
	[[nodiscard]] bool shouldReEvaluate(const struct Task& task, const struct NeedsComponent& needs, bool movementTargetActive);

	/// Build the decision trace: every need, work, and idle option the colonist has, sorted by the
	/// (tier, score) key, with the best reachable one marked Selected
	/// @param currentTask Current task (used to preserve target when already pursuing a need)
	/// @param inventory Colonist inventory (for checking food availability)
	/// @param skills Optional colonist skills (for skill bonus calculation)
	/// @param sourceReach The colonist's route verdicts (targets a route found no known way to)
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

	/// Select task from the decision trace (picks first Selected option); a route is planned over
	/// `memory`, the colonist's belief
	void selectTaskFromTrace(
		EntityID entity,
		struct Task& task,
		struct MovementTarget& movementTarget,
		const struct DecisionTrace& trace,
		const struct Position& position,
		const struct Memory& memory);

	/// Ask nav for a route from where the colonist stands to the task's target (NavigationSystem::requestRoute),
	/// under his belief (known segments and openings), and keep the ticket and its goal on the task,
	/// cancelling any still out. `keepWalking`: the target is the one his route leads to (a replan); while
	/// he has that route he walks on, and the answer replaces it. Otherwise, or with no route left to walk,
	/// he stands (movement off, no route) awaiting it (NavState::AwaitingRoute). An answer nav gives at
	/// once is applied here.
	///
	/// MOVEMENT INVARIANT: a colonist moves ONLY by following a NavPath. With no NavigationSystem
	/// wired (headless tests only) there is no mesh, and MovementSystem steers straight at the target.
	void requestRoute(EntityID entity, struct Task& task, const struct Position& position, const struct Memory& memory,
					  struct MovementTarget& movementTarget, bool keepWalking);

	/// Apply the answer to the task's route once nav has one; nothing while it is out, and a ticket nav no
	/// longer holds is asked again. An answer for a goal the task has since left is dropped and the current
	/// target asked for. Routed: a need asked for several sources (Task::sourceChoices) heads for the one
	/// the route reached, its claim moved there; the route is attached from where he now stands
	/// (joinRoute), stamped with the belief it was asked under and the current nav generation, and he
	/// walks it. No known way: he stops (NavState::CantFindWayTo, failRoute), a verdict on every target. Start off the mesh: not a failure, he waits
	/// (AwaitingMesh) and asks again once the nav generation moves. No known way for a colonist the snap
	/// holds inside a placed obstacle is "not now": he stands (awaitingSnap) until the snap lets go.
	void takeRouteAnswer(EntityID entity, struct Task& task, const struct Position& position, const struct Memory& memory,
						 struct MovementTarget& movementTarget);

	/// Where a colonist at `from` stands to reach `target`: beside the placed obstacle in the mesh it lies
	/// in (NavigationSystem::standPointBeside), else the target itself. `placedTarget`: the target is a
	/// placed thing (a harvest's source), always asked of standPointBeside. nullopt: nowhere to stand.
	[[nodiscard]] std::optional<glm::vec2> standPointFor(glm::vec2 target, glm::vec2 from, float radius, bool placedTarget) const;

	/// The route stands as failed: stopped, CantFindWayTo, and a verdict on every target it was asked for
	/// (SourceReachMemo), found under `foundUnder` (a route answer's label epoch and the belief it was asked
	/// under; nullopt, as things stand now), so the next decision takes his next choice.
	void failRoute(EntityID entity, struct Task& task, const struct Memory& memory, struct MovementTarget& movementTarget,
				   std::optional<SourceReachMemo::Key> foundUnder = std::nullopt);

	/// The task heads for its `index`th source choice: its target, goal, and what it takes from it.
	static void takeSourceChoice(struct Task& task, std::uint8_t index);

	/// Where a need option's source choices are kept while the colonist decides: one slot per need, and
	/// one for the gather-food option.
	[[nodiscard]] static std::size_t needChoiceSlot(NeedType needType, bool gathersFood) {
		return gathersFood ? static_cast<std::size_t>(NeedType::Count) : static_cast<std::size_t>(needType);
	}

	/// Cancel the task's route still out, if any.
	void dropRoute(struct Task& task);

	/// Set the task's nav state. Entering or leaving the failed state (CantFindWayTo) re-files the task
	/// in this update's index: failed, it holds only the goods it carries; routed again, all its claims.
	void setNavState(EntityID entity, struct Task& task, NavState navState);

	/// About to wander or gather food holding a two-hand armful that no root it knows wants: put it down as
	/// a loose pile (the drop path, which merges it into a pile at the spot). A carried packaged item is
	/// never one.
	void putDownUnwantedArmful(Inventory& inventory, const Position& position, const DecisionTrace& trace);

	/// Switching to anything but placing the packaged item it carries, or a need its hands are free for:
	/// set the item down where it stands, still packaged, for whoever places it next.
	void setDownUnplacedFurniture(EntityID entity, Inventory& inventory, const Position& position, const EvaluatedOption& selected);

	/// A colonist put goods away in `box` inside this update: what the caches hold for it is read again.
	void boxChanged(EntityID box);

	/// Switching away from a task that carries its chain's goods (past its pickup, or a cut with cargo):
	/// a one-hand item is stowed to the belt, then the backpack, else dropped; a two-hand armful is
	/// dropped when the selected task's first action needs free hands (a carry-in of what it holds does
	/// not). When the new task is a need and the colonist keeps the goods, the task becomes its
	/// suspended record (task-claims.md, interruption by the colonist's own needs).
	void handleChainInterruption(
		EntityID entity,
		Task& task,
		struct Inventory& inventory,
		const struct Position& position,
		const EvaluatedOption& selected);

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
	std::vector<SourceCandidate> m_chosenScratch;

	/// The deciding colonist's source choices per need option (needChoiceSlot), read by selectTaskFromTrace
	std::array<SourceChoices, static_cast<std::size_t>(NeedType::Count) + 1> m_needChoices{};

	/// The deciding colonist's remembered work sources, reused across colonists
	WorkSources m_workSources;

	/// The storage boxes the colony knows, worked out at most once per update
	ColonyStorageKnowledge m_colonyStorage;

	/// Every root's remaining need, worked out at most once per (root, item, basis) per update
	RemainingNeedCache m_needs;

	/// Each storage box's room per item, read from the world at most once per update
	BoxRoomCache m_boxRooms;

	/// The colony's work every colonist reads alike, worked out at most once per update
	SharedWork m_sharedWork;

	/// The deciding colonist's carried kinds, reused across colonists
	std::vector<CarriedKind> m_carried;

	/// The roots and items the deciding colonist was offered a loose pile for, reused across colonists
	std::vector<RootItem> m_offeredPiles;

	/// One edible item a storage box the colony knows holds, worked out the first time a hungry colonist
	/// asks in an update and shared by every one after. Deposits and withdrawals are ActionSystem's; a
	/// colonist putting goods away inside the update (boxChanged) has it worked out again.
	struct FoodBox {
		EntityID  box = kInvalidEntity;
		glm::vec2 position{0.0F, 0.0F};
		uint64_t  key = 0; // the box's world key, as Memory files it
		uint32_t  item = 0;
		uint32_t  quantity = 0;
	};
	std::vector<FoodBox> m_foodBoxes;
	bool				 m_foodBoxesBuilt = false;
	[[nodiscard]] const std::vector<FoodBox>& foodBoxes();

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

	/// The base tiers as of PriorityConfig::generation() m_baseTiersGeneration, read again when it moves
	BaseTiers m_baseTiers{};
	uint64_t  m_baseTiersGeneration = 0;

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

	/// Callback placing a set-down packaged item on valid ground
	SetDownSpotCallback onSetDownSpot = nullptr;

	/// Callback asking the world when a remembered harvestable is ready to give again
	SourceRegrowthCallback onSourceRegrowth = nullptr;

	/// onSourceRegrowth for a remembered source of `defNameId` at `position`; nullopt (ready) when unwired
	[[nodiscard]] std::optional<double> sourceRegrowingUntil(uint32_t defNameId, glm::vec2 position) const;
};

} // namespace ecs
