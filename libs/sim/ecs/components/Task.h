#pragma once

// Task Component for Colonist AI Decision System
// Tracks the current task a colonist is performing.
// See /docs/design/game-systems/colonists/ai-behavior.md for design details.

#include "Needs.h"

#include <glm/vec2.hpp>

#include <array>
#include <cstdlib>
#include <cstdint>
#include <optional>
#include <string>

namespace ecs {

/// What a claim commits to. See /docs/technical/task-claims.md.
enum class ClaimKind : uint8_t {
	Use,			// exclusive use of the target, quantity 1
	SourceQuantity, // units to be taken from a pile or box
	Slot,			// one slot of a station, bed, or spot; numbered when slotIndex is set
	Inbound,		// units to be deposited at a destination
	InFlight		// units on their way toward a root goal
};

/// Which key space Claim::target is in; the raw integers overlap across them.
enum class TargetKind : uint8_t {
	WorldKey, // Memory's hashWorldEntity(position, defNameId)
	Entity,	  // EntityID
	Goal	  // GoalTask::id
};

constexpr uint16_t kAnySlot = 0xFFFF;

/// One commitment held by a task. Built only through the factories, which fix each kind's key space,
/// so no claim can be filed under a key space its queries never look in. Only the quantity changes
/// after that, as a chain advances.
class Claim {
  public:
	Claim() = default;

	[[nodiscard]] static Claim use(TargetKind targetKind, uint64_t target) {
		return {target, 0, 1, kAnySlot, ClaimKind::Use, targetKind};
	}
	[[nodiscard]] static Claim sourceQuantity(TargetKind targetKind, uint64_t source, uint32_t itemDefNameId, uint32_t quantity) {
		return {source, itemDefNameId, quantity, kAnySlot, ClaimKind::SourceQuantity, targetKind};
	}
	/// kAnySlot is the target's one unnumbered slot (a station), held exclusively like every numbered
	/// one; a numbered slot is one position of several (a builder position).
	[[nodiscard]] static Claim slot(uint64_t entity, uint16_t slotIndex = kAnySlot) {
		return {entity, 0, 1, slotIndex, ClaimKind::Slot, TargetKind::Entity};
	}
	[[nodiscard]] static Claim inbound(uint64_t destination, uint32_t itemDefNameId, uint32_t quantity) {
		return {destination, itemDefNameId, quantity, kAnySlot, ClaimKind::Inbound, TargetKind::Entity};
	}
	[[nodiscard]] static Claim inFlight(uint64_t rootGoalId, uint32_t itemDefNameId, uint32_t quantity) {
		return {rootGoalId, itemDefNameId, quantity, kAnySlot, ClaimKind::InFlight, TargetKind::Goal};
	}

	[[nodiscard]] uint64_t	 target() const { return targetId; }
	[[nodiscard]] uint32_t	 itemDefNameId() const { return item; } // 0 for Use and Slot
	[[nodiscard]] uint32_t	 quantity() const { return count; }		// 1 for Use and Slot
	[[nodiscard]] uint16_t	 slotIndex() const { return slotNumber; }
	[[nodiscard]] ClaimKind	 kind() const { return claimKind; }
	[[nodiscard]] TargetKind targetKind() const { return space; }

	void setQuantity(uint32_t newQuantity) { count = newQuantity; }

  private:
	Claim(uint64_t newTarget, uint32_t newItem, uint32_t newCount, uint16_t newSlot, ClaimKind newKind, TargetKind newSpace)
		: targetId(newTarget),
		  item(newItem),
		  count(newCount),
		  slotNumber(newSlot),
		  claimKind(newKind),
		  space(newSpace) {}

	uint64_t   targetId = 0;
	uint32_t   item = 0;
	uint32_t   count = 1;
	uint16_t   slotNumber = kAnySlot;
	ClaimKind  claimKind = ClaimKind::Use;
	TargetKind space = TargetKind::WorldKey;
};

/// Fixed-capacity inline claim list, so a task's claims never need the heap; overfilling one is a
/// programming error.
template <uint8_t Capacity>
class BasicClaimList {
  public:
	static constexpr uint8_t kCapacity = Capacity;

	[[nodiscard]] bool push(const Claim& claim) {
		if (count >= kCapacity) {
#ifdef DEVELOPMENT_BUILD
			std::abort();
#endif
			return false;
		}
		items[count++] = claim;
		return true;
	}

	void clear() { count = 0; }

	/// Remove every claim `pred` accepts, keeping the order of the rest.
	template <typename Pred>
	void eraseIf(Pred&& pred) {
		uint8_t kept = 0;
		for (uint8_t index = 0; index < count; ++index) {
			if (!pred(items[index])) {
				items[kept++] = items[index];
			}
		}
		count = kept;
	}

	[[nodiscard]] uint8_t size() const { return count; }
	[[nodiscard]] bool empty() const { return count == 0; }
	[[nodiscard]] const Claim& operator[](uint8_t index) const { return items[index]; }
	[[nodiscard]] Claim* begin() { return items.data(); }
	[[nodiscard]] Claim* end() { return items.data() + count; }
	[[nodiscard]] const Claim* begin() const { return items.data(); }
	[[nodiscard]] const Claim* end() const { return items.data() + count; }

  private:
	std::array<Claim, kCapacity> items{};
	uint8_t count = 0;
};

/// A task's claims. The longest chain (fetch from stock) holds three.
using ClaimList = BasicClaimList<4>;

/// One interrupted work task, kept across a need so its claims on the goods still held stay its own.
/// A record only ever holds that item's Inbound and InFlight.
struct SuspendedWork {
	uint64_t		  instanceId = 0;
	uint64_t		  claimSeq = 0;
	uint64_t		  rootGoalId = 0;
	uint32_t		  itemDefNameId = 0;
	BasicClaimList<2> claims;
};

/// Task types that colonists can perform. Arbitration tiers (assigned in priority-tuning.xml's
/// <TaskTiers> + classifyTier promotions): see colonist-task-arbitration.md.
enum class TaskType : uint8_t {
	None = 0,
	FulfillNeed,   // Need fulfillment: tier 2 (critical), 5 (actionable), or 7 (gathering food)
	Harvest,	   // Harvesting resources: tier 4 if serving a work order, else 6 (opportunistic)
	Craft,		   // Crafting at a station: tier 4 (active work order)
	Haul,		   // Moving loose items to storage: tier 4 if serving a work order, else 6
	PlacePackaged, // Carrying packaged items to placement: tier 4 if serving/carrying, else 6
	Build,		   // Advancing construction on a blueprint: tier 4 (active work order)
	Deconstruct,   // Tearing down a structure: tier 4 (active work order)
	Wander		   // Random exploration: tier 7 (idle)
};

/// Task state machine
enum class TaskState : uint8_t {
	Pending, // Task assigned, not yet started movement
	Moving,	 // Moving toward target position
	Arrived	 // Reached target (ready for Actions System)
};

/// Navigation belief state - what the colonist believes about its route.
/// Drives the info panel vocabulary ("Going to", "Re-routing", "Can't find a way").
/// SearchingLKP and LookingForWayIn are reserved for deferred search behaviors.
enum class NavState : uint8_t {
	Traveling,      // Route planned and being followed
	Rerouting,      // Replanning because a new wall/opening was discovered
	SearchingLKP,   // (reserved) searching last-known position
	LookingForWayIn, // (reserved) searching for an entrance to a building
	CantFindWayTo,  // Believed route denied; colonist is stopped
	AwaitingMesh    // No built mesh covers the route yet; held until the nav generation moves
};

/// Task component - tracks a colonist's current activity
struct Task {
	TaskType  type = TaskType::None;
	TaskState state = TaskState::Pending;
	NavState  navState = NavState::Traveling;
	// Rerouting is momentary: set to ~30 ticks when a repath fires so the player sees a brief
	// "Re-routing" beat before the panel reverts to "Going to". Decremented each update tick;
	// zero means the Rerouting display window has elapsed (revert to Traveling).
	std::uint8_t navStateHold = 0;
	// NavigationSystem::generation() when the route was deferred (navState AwaitingMesh). The route
	// is requested again once generation() moves off this value, i.e. a mesh build has landed.
	std::uint64_t deferredNavGeneration = 0;
	// SourceReachMemo::Key::stamp() of the colonist's reach when the route came back with no believed
	// route (navState CantFindWayTo). The next decision records needSourceKey unreachable only while its
	// own key still stamps the same, so the verdict never outlives the meshes and belief it was found
	// under, and a swap in some unrelated region doesn't stop it being recorded.
	std::uint64_t blockedReachStamp = 0;
	// Memory key of the remembered source a FulfillNeed task is heading for; 0 for none (ground, pack).
	// Copied from the selected EvaluatedOption::sourceKey, kept on the task so a failed route can name it.
	std::uint64_t needSourceKey = 0;
	// Hunger: how many units are one meal, which the take makes room for. From stored food, also the
	// item it takes, from a loose pile (needSourceKey is the pile's) or, when needSourceStorageId is
	// set, from that storage box; a bush to eat from sets only the meal. All 0 when gathering food,
	// eating from the pack, and for every other need.
	std::uint32_t needItemDefNameId = 0;
	std::uint32_t needQuantity = 0;
	std::uint64_t needSourceStorageId = 0;

	/// Target position to move to
	glm::vec2 targetPosition{0.0F, 0.0F};

	/// For FulfillNeed tasks: which need is being addressed
	NeedType needToFulfill = NeedType::Count;

	/// For Harvest tasks: target to harvest and goal context
	uint64_t harvestTargetEntityId = 0;	   // Memory key of the harvestable (tree, bush), not an ECS id
	uint64_t harvestGoalId = 0;			   // Goal ID this harvest is contributing to
	uint32_t harvestYieldDefNameId = 0;	   // What item type will be yielded

	/// For Craft tasks: recipe to craft and station entity ID
	std::string craftRecipeDefName;
	uint64_t targetStationId = 0;

	/// For Haul tasks: item to haul and storage container target
	uint32_t	haulItemDefNameId = 0;			   // Item being hauled, interned
	uint32_t	haulQuantity = 1;				   // Quantity to haul
	uint64_t	haulSourceStorageId = 0;		   // Entity ID of the source box for a storage->storage pull (0 = loose/inventory source)
	uint64_t	haulTargetStorageId = 0;		   // Entity ID of the storage container (destination)
	uint64_t	haulGoalId = 0;					   // Goal ID being fulfilled (for cleanup on completion)
	glm::vec2	haulSourcePosition{0.0F, 0.0F};	   // Position of the source item
	glm::vec2	haulTargetPosition{0.0F, 0.0F};	   // Position of the storage container

	/// A carry-in: the colonist already carries the goods, so the haul skips the pickup and walks
	/// straight to its destination (a station store, a build site, a box) to deposit them.
	bool haulFromInventory = false;

	/// A delivery leg the AI has kept or chosen: the colonist has chosen to carry its goods in, so no
	/// further leg of a gathering turns it back (task-claims.md, Batching). A leg a landed cut or pickup
	/// just became is not chosen until the decision that follows keeps it.
	bool deliveryChosen = false;

	/// For Build/Deconstruct tasks: the blueprint entity whose StructureBlueprint is
	/// advanced. The target position (build work slot) is carried in targetPosition.
	uint64_t buildBlueprintEntityId = 0;

	/// For PlacePackaged tasks: packaged entity to carry and placement target
	uint64_t  placePackagedEntityId = 0;		   // Entity ID of the packaged item to carry
	glm::vec2 placeSourcePosition{0.0F, 0.0F};	   // Where the packaged item currently is
	glm::vec2 placeTargetPosition{0.0F, 0.0F};	   // Where to place it (from Packaged.targetPosition)

	/// Step within a two-leg chain (Haul pickup then deposit, PlacePackaged pickup then place): 0 on
	/// the first leg, 1 once the goods are in hand. A mid-chain provisioning task is classified at
	/// tier 4 in the (tier, score) arbitration (see colonist-task-arbitration.md).
	uint8_t chainStep = 0;

	/// Time since last decision re-evaluation (seconds)
	float timeSinceEvaluation = 0.0F;

	/// Arbitration tier of this task when selected (lower = higher priority). Paired with `priority`
	/// (the within-tier score) to form the (tier, score) key. The action-interruption gate is
	/// tier-only: a strictly higher-tier challenger interrupts, a same-tier (or lower) challenger
	/// does not. The anti-thrash margin is NOT re-applied here; it already lives in the score at
	/// selection (the in-progress option carries the hysteresis bonus), so re-checking it at the
	/// gate would double-count.
	int priorityTier = 7;

	/// Within-tier score when this task was selected (used for the same-tier switch threshold).
	float priority = 0.0F;

	/// Debug reason for task selection (e.g., "Hunger at 45%")
	std::string reason;

	/// Claim owner identity, assigned at acceptance and never reused; 0 while no task is held.
	uint64_t instanceId = 0;
	/// Acceptance order, for deterministic tie-breaks between claims.
	uint64_t claimSeq = 0;
	ClaimList claims;
	/// Survives clear(): the switch path clears the task after handleChainInterruption builds it.
	std::optional<SuspendedWork> suspended;

	/// Check if a task is currently assigned
	[[nodiscard]] bool isActive() const { return type != TaskType::None; }

	void discardSuspended() { suspended.reset(); }

	/// Reset task to default state (caller responsible for resetting timeSinceEvaluation)
	void clear() {
		type = TaskType::None;
		state = TaskState::Pending;
		navState = NavState::Traveling;
		navStateHold = 0;
		deferredNavGeneration = 0;
		targetPosition = glm::vec2{0.0F, 0.0F};
		needToFulfill = NeedType::Count;
		needItemDefNameId = 0;
		needQuantity = 0;
		needSourceStorageId = 0;
		harvestTargetEntityId = 0;
		harvestGoalId = 0;
		harvestYieldDefNameId = 0;
		craftRecipeDefName.clear();
		targetStationId = 0;
		haulItemDefNameId = 0;
		haulQuantity = 1;
		haulSourceStorageId = 0;
		haulTargetStorageId = 0;
		haulGoalId = 0;
		haulSourcePosition = glm::vec2{0.0F, 0.0F};
		haulTargetPosition = glm::vec2{0.0F, 0.0F};
		haulFromInventory = false;
		deliveryChosen = false;
		buildBlueprintEntityId = 0;
		placePackagedEntityId = 0;
		placeSourcePosition = glm::vec2{0.0F, 0.0F};
		placeTargetPosition = glm::vec2{0.0F, 0.0F};
		chainStep = 0;
		priorityTier = 7;
		priority = 0.0F;
		// Note: timeSinceEvaluation NOT reset here - caller handles timer logic
		reason.clear();
		instanceId = 0;
		claimSeq = 0;
		claims.clear();
	}
};

} // namespace ecs
