#pragma once

// Action System for Colonist Need Fulfillment
// Processes colonists that have arrived at their task targets and performs
// the appropriate action (eating, drinking, sleeping, toilet).
// Actions have duration and apply effects when complete.
// See /docs/design/game-systems/colonists/needs.md for design details.

#include "../ActionEntries.h"
#include "../DropResourceCallback.h"
#include "../EntityID.h"
#include "../ISystem.h"
#include "../LoosePileIndex.h"
#include "../components/Action.h"

#include <glm/vec2.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace construction {
	class ConstructionWorld;
	class WallClosure;
	using SegmentId = std::uint64_t;
} // namespace construction

namespace ecs {

enum class ActionMoment : uint8_t { Started, Finished };

/// A colonist started or finished an action. An action finishes only when its effect takes (an Eat whose
/// food is gone, a pickup from a source already taken, or a deposit the target refuses does not), an action
/// cut short never finishes, and one taking up work already begun (a build on a site with build work on it,
/// a teardown already under way, a craft batch with a unit made) does not start.
struct ActionEvent {
	EntityID	 colonist = kInvalidEntity;
	ActionType	 type = ActionType::None;
	/// The action entry the action was built from, which a thought source names.
	assets::EntryHandle<assets::ActionEntry> action;
	ActionMoment							 moment = ActionMoment::Started;
	/// The AssetRegistry id of the kind of thing the action is about (the food eaten, the bed slept in,
	/// the item crafted, the plant harvested); 0 when it is about no thing, as a sleep on the ground is.
	uint32_t aboutDefinition = 0;
};

using ActionObserver = std::function<void(const ActionEvent& event)>;

class ActionSystem : public ISystem {
public:
	/// `actions` must outlive the system.
	ActionSystem(const ActionEntries& actions, uint32_t rngSeed)
		: actionEntries(actions),
		  m_rng(rngSeed) {}

	void update(float deltaTime) override;

	/// Priority 350: Runs after movement/physics, processes arrived colonists
	[[nodiscard]] int priority() const override { return 350; }
	[[nodiscard]] const char* name() const override { return "Action"; }

	/// Set callback for item crafted notifications
	/// Called with item label when colonist finishes crafting something
	using ItemCraftedCallback = std::function<void(const std::string& itemLabel)>;
	void setItemCraftedCallback(ItemCraftedCallback callback) { m_onItemCrafted = std::move(callback); }

	/// Set callback for spawning a crafted FURNITURE output as a PACKAGED entity at the station.
	/// Furniture (ItemCategory::Furniture) is never seated in the colonist's hands like a tool or
	/// resource: it comes out packaged and is installed a short distance off the station via the
	/// existing PlacePackaged carry+install flow. The app resolves a valid spot near `stationPos`
	/// (NavigationSystem::findValidPositionNear) and spawns the packaged entity there with its
	/// targetPosition set, so BuildGoalSystem auto-creates the place goal. Fires from inside the
	/// ActionSystem view loop, so the app must ENQUEUE the spawn (deferred-spawn rule), never
	/// spawn synchronously here.
	using SpawnPackagedAtCallback = std::function<void(const std::string& defName, glm::vec2 stationPos)>;
	void setSpawnPackagedAtCallback(SpawnPackagedAtCallback callback) { m_onSpawnPackagedAt = std::move(callback); }

	/// Set callback for dropping loose units on the ground (fell remainder, hand overflow,
	/// stowed-tool drop).
	void setDropResourceCallback(DropResourceCallback callback) { m_onDropResource = std::move(callback); }

	/// What a harvest or pickup found its source to be at the moment it completed.
	enum class SourceState : uint8_t {
		Gone,		// already removed (another colonist's cut or clear, a fell): yields nothing
		Ready,		// standing and harvestable: yields
		OnCooldown, // standing but regrowing from an earlier cut: yields nothing
	};

	/// How a completing harvest or pickup leaves its source.
	enum class SourceTake : uint8_t {
		Remove, // felled, picked up, or cleared
		Regrow, // cut: starts its regrowth cooldown, unless it is already on one
		Keep,	// left as it is
	};

	/// Set callback that takes a harvested or picked-up source from the world as the action completes,
	/// returning the state it was in just before. The world is the truth at completion: a source
	/// already gone or regrowing pays nothing, whatever the task expected when it set out, so two
	/// colonists finishing on one plant never both get its yield. A single-shot source is taken before
	/// it pays; a pooled one is checked with Keep before its pool is drawn, then removed (its pool with
	/// it) once the draw empties the pool or a clear takes it. `regrowthSeconds` is read for Regrow.
	/// Unwired (unit contexts with no world behind them), every source reads Ready.
	using TakeSourceCallback =
		std::function<SourceState(const std::string& defName, float x, float y, SourceTake take, float regrowthSeconds)>;
	void setTakeSourceCallback(TakeSourceCallback callback) { m_onTakeSource = std::move(callback); }

	/// Set callback for removing a specific world entity by id.
	/// Used when the system already holds the exact entity to remove (a drained loose pile),
	/// so the app removes that entity directly instead of re-scanning by position - two
	/// same-material piles in one cell would otherwise alias each other.
	using RemoveEntityByIdCallback = std::function<void(EntityID entity)>;
	void setRemoveEntityByIdCallback(RemoveEntityByIdCallback callback) { m_onRemoveEntityById = std::move(callback); }

	/// Hears every action a colonist starts and finishes, once the update's pass over the colonists is over.
	void addActionObserver(ActionObserver observer) { m_actionObservers.push_back(std::move(observer)); }

	/// The loose ground piles a pickup draws from, filed by makeLoosePile and unfiled by
	/// destroyWorldEntity.
	[[nodiscard]] LoosePileIndex& loosePiles() { return m_loosePiles; }

	/// Result of withdrawing from a harvestable's resource pool.
	struct ResourceDraw {
		uint32_t removed = 0;	  // Units actually taken (clamped to what the pool held)
		bool	 depleted = false; // True if the pool is now empty (entity should be removed)
	};

	/// Set callback for withdrawing from an entity's resource pool.
	/// Called when harvesting an entity with a resource pool, only once the take-source callback has
	/// found the source still standing. `requested` is how many yield units the colonist wants this
	/// chop; the callback removes up to that from the pool and reports how many it actually took plus
	/// whether the pool is now empty.
	using DecrementResourceCallback = std::function<ResourceDraw(const std::string& defName, float x, float y, uint32_t requested)>;
	void setDecrementResourceCallback(DecrementResourceCallback callback) { m_onDecrementResource = std::move(callback); }

	/// Set callback fired when a Build action completes a structure (workDone reached workTotal).
	/// ActionSystem lives in libs/sim and does not know ConstructionWorld; the app wires this
	/// to flip the ConstructionWorld structure state to Built and mark its render dirty.
	/// The StructureBlueprint component's phase is already set to Complete before this fires.
	using StructureCompletedCallback = std::function<void(EntityID blueprintEntity)>;
	void setStructureCompletedCallback(StructureCompletedCallback callback) { m_onStructureCompleted = std::move(callback); }

	/// Set callback fired when a Deconstruct action finishes tearing down a structure
	/// (workDone reached 0). The app wires this to remove the structure from ConstructionWorld,
	/// issue the material refund, and run the demolish cascade. See building-construction D7.
	using StructureDeconstructedCallback = std::function<void(EntityID blueprintEntity)>;
	void setStructureDeconstructedCallback(StructureDeconstructedCallback callback) {
		m_onStructureDeconstructed = std::move(callback);
	}

	/// The topology store a wall's band and sides are read from, so a wall does not complete over a colonist
	/// the player controls or with its builder shut in. Null (headless contexts): walls complete as their
	/// work does.
	void setConstructionWorld(const construction::ConstructionWorld* topology) { constructionWorld = topology; }

	/// Set callback fired once when a colonist the player controls starts holding a wall whose work is done,
	/// by standing in its band. Fires again only after the wall has been free of such a colonist.
	using WallHeldCallback = std::function<void(EntityID blueprintEntity, EntityID colonist)>;
	void setWallHeldCallback(WallHeldCallback callback) { onWallHeld = std::move(callback); }

private:
	/// The values every action this system starts takes from its entry.
	const ActionEntries& actionEntries;

	/// Random number generator for yield calculations
	std::mt19937 m_rng;

	/// Start an action based on the task's need type
	void startAction(
		EntityID entity,
		struct Task& task,
		struct Action& action,
		const struct Position& position,
		struct Memory& memory,
		const struct NeedsComponent& needs,
		struct Inventory& inventory
	);

	/// Start a need-fulfillment action (Hunger/Thirst/Energy/Bladder/Digestion).
	/// Extracted from startAction's FulfillNeed switch; handles the inventory-food, harvestable-food,
	/// drink, sleep, and toilet cases.
	void startNeedAction(
		struct Task& task,
		struct Action& action,
		const struct Position& position,
		struct Memory& memory,
		const struct NeedsComponent& needs,
		struct Inventory& inventory
	);

	/// A hungry colonist about to take `meal` units of `food` from a bush, pile, or box makes room for
	/// them, freeing only what limits it and no more than the meal needs. A slot: the pack's last stack
	/// that is neither a tool nor food goes whole, or failing that a pack tool moves to the belt or a
	/// free hand. Weight: just enough units of such goods, pack (last stack first) then hands. Goods its
	/// suspended record carries go only once nothing else can. What goes is put away by the store-or-drop
	/// rule; tools never go.
	void makeRoomToEat(struct Inventory& inventory, const struct Task& task, const std::string& food, uint32_t meal, glm::vec2 position);

	/// The creature an action's effect rows apply to.
	struct Actor {
		EntityID			   entity;
		glm::vec2			   position;
		struct NeedsComponent& needs;
		struct Inventory&	   inventory;
		struct Memory&		   memory;
	};

	/// Which of an action's rows apply now.
	enum class RowsDue : uint8_t {
		All,
		/// All but its `<restore>` rows, which an action paying along has paid as it went.
		AllButRestores,
		/// All but the row an open-ended action ran until, which it paid as it went.
		AllButBound,
	};

	/// Advance an action one tick: its time and what it pays along, or its open-ended bound. Marks it Complete once it
	/// has run its course. See /docs/technical/queries-and-chains.md (An action entry, Paying out as it goes).
	void advanceAction(float deltaTime, const Actor& actor, struct Action& action);

	/// Advance a fixed-duration action's time.
	void processAction(float deltaTime, struct Action& action);

	/// Pay an action paying along the share of its `<restore>` rows earned since the last tick, shaped by its curve.
	void payAlong(struct Action& action, struct NeedsComponent& needs);

	/// Advance an open-ended `<restore>`: its amount per its seconds, stopping at upTo, Complete once its need is there.
	void advanceRestore(float deltaTime, const RestoreRow& row, struct Action& action, struct NeedsComponent& needs);

	/// Advance an open-ended `<consume>`: every seconds, one round of every row in the order written; Complete once its
	/// need reaches upTo, or a round takes nothing.
	void advanceRounds(float deltaTime, const ConsumeRow& row, const Actor& actor, struct Action& action);

	/// Apply the action's due rows in the order written. A row that takes nothing stops the rest; false then.
	bool applyRows(const Actor& actor, struct Action& action, RowsDue due);

	/// Pay the `<restore>` row at `index` of the action's values `amount`, counting it toward what the action reports
	/// it restored when the row is the need it serves.
	void payRestore(std::size_t index, const RestoreRow& row, float amount, struct Action& action, struct NeedsComponent& needs);

	/// Complete an action and apply effects
	void completeAction(
		EntityID entity,
		const struct Position& position,
		struct Action& action,
		struct NeedsComponent& needs,
		struct Task& task,
		struct Inventory& inventory,
		struct Memory& memory
	);

	/// Hand a haul over to its deposit leg: point the task, its MovementTarget, and a fresh route at the
	/// destination. The one handoff every way into the deposit leg takes; claims are rewritten by the caller.
	void headToDeposit(EntityID entity, struct Task& task);

	/// Point the task at its next leg's target, Moving, for the AI to route: its MovementTarget follows,
	/// and the old leg's route and any route still out for it are dropped. Every change of a task's
	/// target in action code goes through here.
	void headTo(EntityID entity, struct Task& task, glm::vec2 target);

	/// A goal's cut has landed `landed` units: turn the Harvest task into the deposit leg that carries its
	/// cargo (what earlier cuts of the gathering carry, plus this cut) to its root's destination,
	/// committing what the root still needs that no other task is bringing. False (the task ends) when
	/// the cut brings nothing toward a root: a clear, nothing carried, or no need.
	bool carryHarvestIn(EntityID entity, const struct Position& position, struct Task& task, const struct Inventory& inventory, uint32_t landed);

	// --- The primitive effects: one per effect row kind, the only implementation of each. ---

	/// `<restore>`: move the row's need, the creature's own or the target creature's, by `amount` (restoredAmount, or
	/// the share of it earned).
	void applyRestore(const RestoreRow& row, float amount, const EffectContext& context, struct NeedsComponent& needs);

	/// The need a `<restore>` row moves: the creature's own, or the target creature's; null when the target has none.
	[[nodiscard]] struct Need* restoredNeed(const RestoreRow& row, const EffectContext& context, struct NeedsComponent& needs);

	/// `<consume>`: use up the row's count of a held unit with its capability, chosen as it applies, and set the
	/// action's context to that unit and its nutrition. False, using up nothing, when no such unit is held.
	bool applyConsume(const ConsumeRow& row, struct Action& action, struct Inventory& inventory);

	/// The held unit a `<consume>` row uses up, edible being the one capability the resolver lets it name: the first,
	/// by name, of the pack's edible units; null when it holds none.
	[[nodiscard]] static const std::string* heldEdibleUnit(const struct Inventory& inventory);

	/// The action is missing the target data a primitive acts on (one started without the factory that supplies it):
	/// a warning naming the action, and the row takes nothing.
	bool refuseWithoutTarget(const struct Action& action, const char* element);

	/// `<take>`: take from the action's target into hands or pack. A harvestable, a loose pile, or a box (its
	/// CollectionEffect): pool-withdraw vs single-shot, source removal or cooldown, the row's `clears` removing the
	/// source whatever it is. A packaged item (its PlacePackagedEffect): lifted into both hands once they are cleared.
	/// `colonistPosition` is where a tool stowed to free the hands is put away or dropped from. False when nothing came
	/// off the source and no clear removed it, or the package is gone, placed, or carried.
	bool applyTake(
		const TakeRow&		 row,
		const struct Action& action,
		glm::vec2			 colonistPosition,
		struct Inventory&	 inventory,
		struct Memory&		 memory
	);

	/// Take from a harvestable, a loose pile, or a box, `clears` removing the source whatever it is.
	bool takeFromSource(
		bool				 clears,
		const struct Action& action,
		glm::vec2			 colonistPosition,
		struct Inventory&	 inventory,
		struct Memory&		 memory
	);

	/// Lift the packaged item into both hands, cleared first.
	bool liftPackaged(const struct Action& action, struct Inventory& inventory);

	/// `<put>`: put what the creature carries into the action's target (a storage box, a craft station's store, a
	/// blueprint's manifest), metered to what it still needs and has room for. False when the target took nothing.
	bool applyPut(const struct Action& action, struct Inventory& inventory);

	/// `<work>` completing: the bound is crossed, so the structure is built, or torn down and refunded. False when the
	/// action has no work target.
	bool completeWork(const WorkRow& row, const struct Action& action);

	/// `<craft>`: the completion service (see startCraftAction for its start). Checks again that the station's store
	/// holds every input, spends them, makes the outputs (furniture packaged through the deferred spawn queue, anything
	/// else to the crafter's hands, belt, pack, then stored or dropped), fires the item-crafted event, and advances the
	/// job. False, making and spending nothing, when the store no longer holds every input.
	bool applyCraft(const struct Action& action, struct Inventory& inventory);

	/// `<spawn>`: a thing of the row's definition where the creature stands (`standing`), or with `into="hands"` given
	/// to it, hands first, then belt, pack, and stored or dropped, as a crafted output is.
	bool applySpawn(const SpawnRow& row, glm::vec2 standing, struct Inventory& inventory);

	/// `<install>`: put the packaged item the creature carries down at its placement target and unpackage it. False
	/// when the package is gone or already placed.
	bool applyInstall(const struct Action& action, struct Inventory& inventory);

	/// Start a crafting action on the task's recipe: the `<craft>` service's start, which sums the recipe's repeated
	/// input lines and starts nothing unless the station's store holds every input.
	void startCraftAction(struct Task& task, struct Action& action);

	/// Start a Build or Deconstruct action on the task's blueprint, once the `<work>` guards let it start.
	/// Reads the builder's level in the work row's skill to scale the work rate. @param entity is the builder.
	void startBuildAction(
		EntityID entity,
		struct Task& task,
		struct Action& action
	);

	/// The `<work>` guards a start passes: a build needs the blueprint under construction with no demolish order; a
	/// teardown needs work done above 0.
	[[nodiscard]] static bool workMayStart(const WorkRow& row, const struct StructureBlueprint& blueprint);

	/// `<work>` paying along: advance the work on the action's blueprint by the rate of the skill level it started
	/// with x dt, up or down as the row says, clamped at its bound, Complete when the bound is crossed. Keeps the
	/// construction guards each tick: a builder arriving after completion, or at a site ordered demolished, or at a
	/// teardown already at 0, is cleared without adding work or completing; completion fires once, whatever the
	/// number of builders; a teardown begins once; a wall waits while it would shut someone in.
	/// @return true if the action reached completion this tick.
	bool advanceWork(float deltaTime, const WorkRow& row, struct Action& action);

	/// Readies the wall `blueprintEntity` to complete, true when it may. It is held, moving nobody, while it
	/// would shut in one of its builders (construction::WallClosure::shutsIn; his action ends, so he decides
	/// again and goes to a side it allows), or anyone else not under the player's control whose task is
	/// taking them out of the room it closes, or while a colonist the player controls stands in its band
	/// (construction::wallBand). Otherwise anyone else in the band steps out to a side its builders may stand
	/// on, the side he is on when it is one, and it may complete. True for a wall with no band.
	bool clearWallToComplete(EntityID blueprintEntity);

	/// What completing the wall `blueprintEntity` closes, worked out again when the topology has changed.
	const construction::WallClosure& wallClosure(EntityID blueprintEntity, construction::SegmentId segment);
	/// Whether `task` is a Build on a wall blueprint.
	[[nodiscard]] bool buildsAWall(const struct Task& task) const;
	/// Drop what is kept for a wall: its hold toast and its closure. Done as it completes, and in update for
	/// walls that are gone or complete.
	void forgetWall(EntityID blueprintEntity);
	void forgetGoneWalls();

	/// Start a harvest action for goal-driven harvesting (crafting materials)
	/// Finds harvestable at target position that yields the required item type.
	/// Memory is mutable so a vanished target can be forgotten (prevents re-selection).
	void startHarvestAction(struct Task& task, struct Action& action, struct Memory& memory, const struct Inventory& inventory);

	/// Start a haul action (pickup from source, then deposit to storage)
	/// Memory is mutable so a vanished pickup target can be forgotten.
	/// Inventory decides phase: a standard haul deposits only once it actually carries the
	/// item, otherwise it picks up first (position alone is ambiguous when source and target
	/// sit within arrival tolerance of each other). A colonist that arrives at the source
	/// already carrying the item (at carry cap, or holding leftover stock the option routed
	/// back to the source) is redirected to the deposit leg from here, which is why this needs
	/// the entity to re-point its MovementTarget/NavPath.
	void startHaulAction(
		EntityID entity,
		struct Task& task,
		struct Action& action,
		const struct Position& position,
		struct Memory& memory,
		const struct Inventory& inventory
	);

	/// Start a place packaged action (pickup packaged item, then place at target)
	/// Two-phase task like Haul: Phase 1 = PickupPackaged at source, Phase 2 = PlacePackaged at target
	/// Uses both position and inventory state to determine which phase to execute
	void startPlacePackagedAction(
		struct Task& task,
		struct Action& action,
		const struct Position& position,
		const struct Inventory& inventory
	);

	/// Clear colonist hands for two-handed pickup
	/// Stows items to backpack if possible, drops if not (2-handed or full backpack)
	/// @param inventory Colonist's inventory to modify
	/// @param dropPosition Position to drop items that can't be stowed
	void clearHandsForTwoHandedPickup(
		struct Inventory& inventory,
		glm::vec2 dropPosition
	);

	/// Stow any held ONE-hand tool out of the hands (belt -> pack -> drop) so both hands are free
	/// for a two-hand armful (wood). Called just before lifting an armful at harvest/haul time so a
	/// crafted axe in hand doesn't block the pickup. A one-hand tool the belt/pack can't take is
	/// put in storage within reach, else dropped loose at `dropPosition` (ecs::storeOrDrop). A two-hand
	/// item already held is left alone.
	void stowHeldToolsForArmful(
		struct Inventory& inventory,
		glm::vec2 dropPosition
	);

	/// Clear a single hand slot
	/// Helper for clearHandsForTwoHandedPickup - stows to backpack or drops
	/// @param handSlot The hand slot to clear (leftHand or rightHand)
	/// @param inventory Colonist's inventory for backpack storage
	/// @param dropPosition Position to drop items that can't be stowed
	/// @param handName Name for logging ("left" or "right")
	void clearHandItem(
		std::optional<struct ItemStack>& handSlot,
		struct Inventory& inventory,
		glm::vec2 dropPosition,
		const char* handName
	);

	/// Hold for the action observers that the colonist started or finished the action.
	void reportAction(
		EntityID entity,
		const struct Action& action,
		ActionMoment moment,
		const struct Task& task,
		const struct Memory& memory
	);

	/// True when starting the action takes up work already begun: a Build on a blueprint with build work
	/// done, a Deconstruct of a structure whose teardown has begun, or a Craft of a batch with a unit
	/// already made.
	[[nodiscard]] bool continuesWork(const struct Action& action) const;

	std::vector<ActionObserver> m_actionObservers;
	std::vector<ActionEvent>	m_reportedActions;

	/// Callback for item crafted notifications
	ItemCraftedCallback m_onItemCrafted = nullptr;

	/// Callback for spawning a crafted furniture output as a packaged entity at the station
	SpawnPackagedAtCallback m_onSpawnPackagedAt = nullptr;

	/// Callback for dropping loose units as a ground pile
	DropResourceCallback m_onDropResource = nullptr;

	/// Callback taking a harvested or picked-up source from the world
	TakeSourceCallback m_onTakeSource = nullptr;

	/// Callback for removing a specific world entity by id (drained loose pile)
	RemoveEntityByIdCallback m_onRemoveEntityById = nullptr;

	/// Every loose ground pile by world key
	LoosePileIndex m_loosePiles;

	/// Callback for decrementing entity resource count
	DecrementResourceCallback m_onDecrementResource = nullptr;

	/// Callback fired when a Build action completes a structure
	StructureCompletedCallback m_onStructureCompleted = nullptr;

	/// Callback fired when a Deconstruct action finishes tearing down a structure
	StructureDeconstructedCallback m_onStructureDeconstructed = nullptr;

	const construction::ConstructionWorld* constructionWorld = nullptr;
	WallHeldCallback					   onWallHeld = nullptr;
	std::unordered_set<EntityID>		   wallsHeldByControlled;
	struct CachedClosure {
		construction::SegmentId							 segment = 0;
		std::uint64_t									 version = 0;
		// Shared, not unique: WallClosure is only declared here, and a shared_ptr's deleter is fixed where it is
		// made, so ActionSystem's implicit destructor needs no complete type.
		std::shared_ptr<const construction::WallClosure> closure;
	};
	std::unordered_map<EntityID, CachedClosure> wallClosures;
};

} // namespace ecs
