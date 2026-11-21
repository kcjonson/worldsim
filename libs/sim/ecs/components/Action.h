#pragma once

// Action Component for Colonist Activities
//
// Actions represent what a colonist is doing at a location. This design uses
// std::variant for polymorphic effect data - each action category has its own
// effect struct containing only relevant data.
//
// Design Rationale (see /docs/technical/ecs-patterns.md):
// - std::variant is ECS-idiomatic: no heap allocation, type-safe, contiguous memory
// - Each effect type contains only the data it needs (no confusing unused fields)
// - Adding new action categories = add a new effect struct + variant alternative
//
// Related docs:
// - /docs/design/game-systems/colonists/ai-behavior.md
// - /docs/design/game-systems/world/entity-capabilities.md

#include "../ActionEntries.h"
#include "../EntityID.h"
#include "Needs.h"

#include <assets/Entries.h>

#include <glm/vec2.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace ecs {

	// ============================================================================
	// Action Types (what the colonist is doing)
	// ============================================================================

	/// Action types that colonists can perform
	enum class ActionType : uint8_t {
		None = 0,

		// Need Fulfillment Actions
		Eat,	// Consuming food item from inventory
		Drink,	// Drinking from water tile (Pond)
		Wash,	// Washing at water (shore)
		Sleep,	// Sleeping on ground or bed
		Toilet, // Using toilet or ground relief

		// Resource Collection Actions
		Pickup,	  // Pick up ground item directly into inventory
		Harvest,  // Harvest from entity (bush, plant) into inventory
		Withdraw, // Take items out of a storage container's inventory into the colonist's (storage->storage pull)

		// Work Actions
		Craft,	 // Creating items at workbench
		Deposit, // Depositing items into storage container

		// Construction Actions
		Build,		 // Advancing construction work on a blueprint (UnderConstruction phase)
		Deconstruct, // Tearing down a structure (mirrors Build, counts work down)

		// Placement Actions
		PickupPackaged, // Clear hands + pick up 2-handed packaged item
		PlacePackaged,	// Place packaged item at target location

		// A chain's `do` step: any action entry, started by the one chain start path with no factory
		Chain,
	};

	/// Action state machine
	enum class ActionState : uint8_t {
		Starting,	// Action just began, initialization frame
		InProgress, // Action is ongoing
		Complete	// Action finished, ready for cleanup
	};

	// ============================================================================
	// Construction work rate
	// ============================================================================

	/// Construction work units produced per second at a given Construction skill level.
	///
	/// Pure function (no state, easily unit-tested) used by Build and Deconstruct actions
	/// to advance StructureBlueprint.workDone: workDone += constructionWorkRate(skill) x dt.
	///
	/// Linear in skill: a level-0 (untrained) builder still works at the base rate, and the
	/// rate scales up to roughly base x (1 + 20 x perLevel) at master. The same multiplier
	/// shape as the AI-priority skill bonus (level x multiplier), expressed as a work
	/// multiplier on a base rate so untrained colonists are not stuck at zero.
	/// @param skillLevel Construction skill in [0, 20]; clamped if out of range.
	[[nodiscard]] inline float constructionWorkRate(float skillLevel) {
		// Base work units/second for an untrained builder. workTotal is in the same units
		// (area/length x material factor), so base x typical workTotal gives a sane build time.
		constexpr float kBaseWorkRate = 10.0F;

		// Per-skill-level fractional speedup. At level 20 the builder is 1 + 20*0.08 = 2.6x
		// the base rate; tuned so skill matters without dwarfing the untrained baseline.
		constexpr float kPerLevelBonus = 0.08F;

		constexpr float kMaxSkillLevel = 20.0F;
		float			clamped = skillLevel < 0.0F ? 0.0F : (skillLevel > kMaxSkillLevel ? kMaxSkillLevel : skillLevel);
		return kBaseWorkRate * (1.0F + kPerLevelBonus * clamped);
	}

	/// Harvest work units produced per second at a given Harvesting skill level.
	///
	/// Mirrors constructionWorkRate (same base rate and per-level shape, shared tuning) but is
	/// driven by the Harvesting skill and applied to a harvestable's `durability`: chop time =
	/// durability / harvestWorkRate(skill). An untrained forager works at the base rate; a master
	/// is ~2.6x faster. Kept separate from construction so the two activities can tune apart.
	/// @param skillLevel Harvesting skill in [0, 20]; clamped if out of range.
	[[nodiscard]] inline float harvestWorkRate(float skillLevel) {
		constexpr float kBaseWorkRate = 10.0F;	 // work units/second for an untrained forager
		constexpr float kPerLevelBonus = 0.08F;	 // +8% per skill level (master = 1 + 20*0.08 = 2.6x)
		constexpr float kMaxSkillLevel = 20.0F;
		float			clamped = skillLevel < 0.0F ? 0.0F : (skillLevel > kMaxSkillLevel ? kMaxSkillLevel : skillLevel);
		return kBaseWorkRate * (1.0F + kPerLevelBonus * clamped);
	}

	// ============================================================================
	// Effect context (what an action's effect rows read from where it runs)
	// ============================================================================

	/// What an action's effect rows name and do not carry: set when it starts, and by its `<consume>` each time it
	/// uses up a unit.
	struct EffectContext {
		/// The definition of the unit the last `<consume>` used up, or the one the action set out to eat; 0 for none.
		uint32_t consumedDefNameId = 0;
		/// The nutrition of the unit the last `<consume>` used up, which a `<restore from="nutrition">` scales.
		float nutrition = 0.0F;
		/// The target's sleep quality, which a `<restore scaleBy="sleep_quality">` scales.
		float sleepQuality = 1.0F;
		/// The step's target creature, whose need a `<restore who="target">` moves.
		EntityID targetCreature = kInvalidEntity;
	};

	/// The whole signed amount a `<restore>` row moves its need by: per its `seconds` for an open-ended row.
	[[nodiscard]] inline float restoredAmount(const RestoreRow& row, const EffectContext& context) {
		const float amount = row.fromNutrition ? context.nutrition * row.amount : row.amount;
		return row.bySleepQuality ? amount * context.sleepQuality : amount;
	}

	/// The share of an action's total an action paying along earns from `fromFraction` to `toFraction` of it done: by a
	/// fraction f it has earned f^curve of the whole.
	[[nodiscard]] inline float earnedShare(float fromFraction, float toFraction, float curve) {
		if (curve == 1.0F) {
			return toFraction - fromFraction;
		}
		return std::pow(toFraction, curve) - std::pow(fromFraction, curve);
	}

	// ============================================================================
	// Effect Types: the targets and items each effect row acts on
	// ============================================================================

	/// Effect for item collection actions (Pickup, Harvest)
	/// Adds items to inventory and optionally affects the source entity.
	struct CollectionEffect {
		/// Item definition name to add to inventory
		std::string itemDefName;

		/// Quantity of items to collect
		uint32_t quantity = 1;

		/// Position of the source entity (for removal/cooldown)
		glm::vec2 sourcePosition{0.0F, 0.0F};

		/// DefName of the source entity (for removal/cooldown)
		std::string sourceDefName;

		/// If true, source entity is destroyed after collection
		bool destroySource = true;

		/// If destroySource is false and this > 0, entity enters cooldown (regrowth)
		float regrowthTime = 0.0F;

		/// Construction footprint clear, set by the harvest factory for a clear goal (a `<take clears="true">` row
		/// clears too): the source is removed by this action whatever kind it is (regrowing, pooled) and however much
		/// the colonist can carry; the uncarried yield drops as a loose pile.
		bool clearsSource = false;

		/// Where the yield the colonist can't carry drops: beside the source when unset; a footprint
		/// clear drops it just outside the footprint, off the ground the site will stand on.
		std::optional<glm::vec2> dropAt;

		/// Storage-to-storage pull only (ActionType::Withdraw): the SOURCE box entity to take items
		/// FROM. When non-zero the collection removes `quantity` of `itemDefName` from that box's
		/// Inventory (clamped to the live quantity) instead of touching a ground entity; the pile/pool/
		/// single-shot ground paths are bypassed. 0 for a ground Pickup or a Harvest.
		uint64_t sourceStorageId = 0;
	};

	/// What a `<work>` row works on (Build, Deconstruct).
	///
	/// Unlike fixed-duration effects (applied once on completion), construction work is
	/// continuous: ActionSystem advances the target blueprint's workDone every
	/// tick by rate(skill) x dt, so multiple colonists and multiple work sessions accumulate
	/// onto the same StructureBlueprint. The action ends when the blueprint reaches its work
	/// bound (workTotal for Build, 0 for Deconstruct), at which point ActionSystem fires the
	/// structure-completion callback. See building-construction-architecture.md D7.
	struct ProgressEffect {
		/// Target blueprint entity whose StructureBlueprint.workDone is advanced.
		uint64_t targetEntityId = 0;

		/// Builder's level in the work row's skill (0-20), captured when the action starts.
		/// Scales the per-second work rate; see constructionWorkRate().
		float skillLevel = 0.0F;
	};

	/// Effect for crafting actions (Craft at station)
	/// Consumes inputs from inventory and produces outputs.
	struct CraftingEffect {
		/// Recipe being crafted
		std::string recipeDefName;

		/// Station entity ID (for updating WorkQueue)
		uint64_t stationEntityId = 0;

		/// Input items to consume (defName -> count)
		std::vector<std::pair<std::string, uint32_t>> inputs;

		/// Output items to produce (defName -> count)
		std::vector<std::pair<std::string, uint32_t>> outputs;
	};

	/// Effect for deposit actions (putting items into storage containers)
	/// Moves item from colonist inventory to storage container inventory.
	struct DepositEffect {
		/// Item definition name to deposit
		std::string itemDefName;

		/// Quantity to deposit
		uint32_t quantity = 1;

		/// Target storage container entity ID
		uint64_t storageEntityId = 0;

		/// Craft-material delivery: the target is a crafting station. The items are MOVED from
		/// the colonist's inventory into the station's own Inventory store (the Craft action
		/// later consumes them from the station), and the deposit credits the parent Craft goal
		/// so the goal hierarchy advances. When true, storageEntityId is the station, which
		/// carries both a WorkQueue and an Inventory store.
		bool deliverToCraftStation = false;
	};

	/// Effect for placing packaged items at target locations.
	/// Moves entity to target position and removes Packaged component.
	struct PlacePackagedEffect {
		/// Entity ID of the packaged item being placed
		uint64_t packagedEntityId = 0;

		/// Target position for placement (from Packaged.targetPosition)
		glm::vec2 targetPosition{0.0F, 0.0F};

		/// The turn it is installed at, copied with the position (Packaged.targetRotation); unset keeps its own.
		std::optional<float> targetRotation;
	};

	/// Variant holding the effect data for the current action
	using ActionEffect = std::variant<std::monostate, CollectionEffect, ProgressEffect, CraftingEffect, DepositEffect, PlacePackagedEffect>;

	// ============================================================================
	// Action Component
	// ============================================================================

	/// Action component - tracks a colonist's current action and its effect
	struct Action {
		// --- Common action state (shared by all action types) ---

		ActionType	type = ActionType::None;
		ActionState state = ActionState::Starting;

		/// The action entry the action runs, its effect rows applied in the order written; null while no action runs.
		/// Points into the ActionEntries the ActionSystem was given, which outlives every action.
		const ActionValues* values = nullptr;

		/// What the effect rows read from where the action runs.
		EffectContext context;

		/// Duration of the action in seconds
		float duration = 0.0F;

		/// Elapsed time in this action (seconds)
		float elapsed = 0.0F;

		/// The fraction of an action paying along already paid out.
		float paidFraction = 0.0F;

		/// The times an open-ended action has paid as it went: each round of its `<consume>`, each tick its `<restore>`
		/// moved its need. One that reached its bound before paying anything finishes nothing.
		uint32_t rounds = 0;

		/// What the action has paid to the need it serves (its values' primary row), which the completion log reports for
		/// an open-ended action.
		float restored = 0.0F;

		/// Target position (the point the action works at)
		glm::vec2 targetPosition{0.0F, 0.0F};

		/// Where the creature stood when the action started, which a `<spawn>` puts its thing at.
		glm::vec2 startPosition{0.0F, 0.0F};

		/// Whether this action can be interrupted by higher-priority tasks.
		/// Biological needs (Eat, Drink, Toilet) are NOT interruptable.
		/// Sleep can be interrupted for critical needs.
		bool interruptable = true;

		// --- Effect data (variant - contains type-specific data) ---

		ActionEffect effect;

		// --- Query methods ---

		/// Check if an action is currently in progress
		[[nodiscard]] bool isActive() const { return type != ActionType::None; }

		/// Check if action is complete
		[[nodiscard]] bool isComplete() const { return state == ActionState::Complete; }

		/// Get progress as 0.0 - 1.0
		[[nodiscard]] float progress() const {
			if (duration <= 0.0F) {
				return 1.0F;
			}
			return elapsed / duration;
		}

		/// Check if this action has a collection effect
		[[nodiscard]] bool hasCollectionEffect() const { return std::holds_alternative<CollectionEffect>(effect); }

		/// Get the collection effect
		[[nodiscard]] const CollectionEffect& collectionEffect() const { return std::get<CollectionEffect>(effect); }
		[[nodiscard]] CollectionEffect&		  collectionEffect() { return std::get<CollectionEffect>(effect); }

		/// Check if this action has a crafting effect
		[[nodiscard]] bool hasCraftingEffect() const { return std::holds_alternative<CraftingEffect>(effect); }

		/// Get the crafting effect
		[[nodiscard]] const CraftingEffect& craftingEffect() const { return std::get<CraftingEffect>(effect); }
		[[nodiscard]] CraftingEffect&		craftingEffect() { return std::get<CraftingEffect>(effect); }

		/// Check if this action has a deposit effect
		[[nodiscard]] bool hasDepositEffect() const { return std::holds_alternative<DepositEffect>(effect); }

		/// Get the deposit effect
		[[nodiscard]] const DepositEffect& depositEffect() const { return std::get<DepositEffect>(effect); }
		[[nodiscard]] DepositEffect&	   depositEffect() { return std::get<DepositEffect>(effect); }

		/// Check if this action has a construction progress effect (Build, Deconstruct)
		[[nodiscard]] bool hasProgressEffect() const { return std::holds_alternative<ProgressEffect>(effect); }

		/// Get the progress effect (call hasProgressEffect() first or use std::get_if)
		[[nodiscard]] const ProgressEffect& progressEffect() const { return std::get<ProgressEffect>(effect); }
		[[nodiscard]] ProgressEffect&		progressEffect() { return std::get<ProgressEffect>(effect); }

		/// Check if this action has a place packaged effect
		[[nodiscard]] bool hasPlacePackagedEffect() const { return std::holds_alternative<PlacePackagedEffect>(effect); }

		/// Get the place packaged effect
		[[nodiscard]] const PlacePackagedEffect& placePackagedEffect() const { return std::get<PlacePackagedEffect>(effect); }
		[[nodiscard]] PlacePackagedEffect&		 placePackagedEffect() { return std::get<PlacePackagedEffect>(effect); }

		// --- Mutation methods ---

		/// Reset action to default state
		void clear() {
			type = ActionType::None;
			state = ActionState::Starting;
			values = nullptr;
			context = EffectContext{};
			duration = 0.0F;
			elapsed = 0.0F;
			paidFraction = 0.0F;
			rounds = 0;
			restored = 0.0F;
			targetPosition = glm::vec2{0.0F, 0.0F};
			startPosition = glm::vec2{0.0F, 0.0F};
			interruptable = true;
			effect = std::monostate{};
		}

		// --- Factory methods for creating actions ---
		//
		// Each factory takes its duration, interruptability, amounts, and effect rows from its action entry
		// (ActionEntries), and sets the context and targets the rows act on.

		/// An action of `type` starting, running the entry `values` (which must outlive it), with its fixed duration
		/// and interruptability. The factories start from it; an action with no factory sets its own context and
		/// targets.
		static Action fromEntry(ActionType type, const ActionValues& values) {
			Action action;
			action.type = type;
			action.state = ActionState::Starting;
			action.values = &values;
			action.duration = values.seconds;
			action.interruptable = values.interruptable;
			return action;
		}

		/// Factory: Eat action - its `<consume>` uses up a held unit of food, chosen when it applies, and its restores
		/// scale by that unit's nutrition (hunger up, digestion down as the gut fills).
		/// @param foodDefNameId The food the colonist sets out to eat, which the started action reports
		/// @param nutrition That food's nutrition (0-1 scale)
		static Action Eat(const ActionEntries& actions, uint32_t foodDefNameId, float nutrition) {
			Action action = fromEntry(ActionType::Eat, actions.eat());
			action.context.consumedDefNameId = foodDefNameId;
			action.context.nutrition = nutrition;
			return action;
		}

		/// Factory: Drink action at water, which is inexhaustible.
		static Action Drink(const ActionEntries& actions) { return fromEntry(ActionType::Drink, actions.drink()); }

		/// Factory: Wash action at water.
		static Action Wash(const ActionEntries& actions) { return fromEntry(ActionType::Wash, actions.wash()); }

		/// Factory: Sleep action - restores energy by its entry's amount times the sleep quality.
		/// @param quality The place's sleep quality (the ground's from ActionEntries, 1.0 for a bed)
		static Action Sleep(const ActionEntries& actions, float quality) {
			Action action = fromEntry(ActionType::Sleep, actions.sleep());
			action.context.sleepQuality = quality;
			return action;
		}

		/// Factory: Toilet action - relieves the bladder, the bowels, or both, from the matching entry; only a relief
		/// whose entry spawns (the bowels) leaves a bio pile.
		/// @param position Where the colonist relieves himself
		/// @param doPee Whether to relieve bladder; at least one of doPee and doPoop is true
		/// @param doPoop Whether to relieve digestion
		static Action Toilet(const ActionEntries& actions, glm::vec2 position, bool doPee, bool doPoop) {
			const ActionValues& values = doPee && doPoop ? actions.relieveBoth() : (doPoop ? actions.defecate() : actions.urinate());
			Action				action = fromEntry(ActionType::Toilet, values);
			action.targetPosition = position;
			return action;
		}

		/// Factory: Pickup action - instantly pick up a ground item
		/// @param itemDefName Item definition to add to inventory
		/// @param quantity Number of items to pick up
		/// @param sourcePos Position of the source entity
		/// @param sourceDefName DefName of the source entity (for removal)
		static Action Pickup(
			const ActionEntries& actions,
			const std::string&	 itemDefName,
			uint32_t			 quantity,
			glm::vec2			 sourcePos,
			const std::string&	 sourceDefName
		) {
			Action action = fromEntry(ActionType::Pickup, actions.pickup());
			action.targetPosition = sourcePos;

			CollectionEffect collEff;
			collEff.itemDefName = itemDefName;
			collEff.quantity = quantity;
			collEff.sourcePosition = sourcePos;
			collEff.sourceDefName = sourceDefName;
			collEff.destroySource = true; // Picking up removes the ground item
			collEff.regrowthTime = 0.0F;
			action.effect = collEff;

			return action;
		}

		/// Factory: Withdraw action - take items out of a storage container into the colonist's
		/// inventory (the pickup leg of a storage->storage pull). Mirrors Pickup, but the source is a
		/// box's Inventory (sourceStorageId), not a ground entity: the effect removes from that box
		/// (clamped to its live quantity) and never destroys it.
		/// @param itemDefName Item to take from the source box and add to inventory
		/// @param quantity Number of items to withdraw
		/// @param sourceStorageId Entity ID of the source storage container
		/// @param sourcePos Position of the source box (the walk-to target)
		static Action Withdraw(
			const ActionEntries& actions,
			const std::string&	 itemDefName,
			uint32_t			 quantity,
			uint64_t			 sourceStorageId,
			glm::vec2			 sourcePos
		) {
			Action action = fromEntry(ActionType::Withdraw, actions.withdraw());
			action.targetPosition = sourcePos;

			CollectionEffect collEff;
			collEff.itemDefName = itemDefName;
			collEff.quantity = quantity;
			collEff.sourcePosition = sourcePos;
			collEff.sourceDefName = itemDefName; // unused by the box-withdraw path, but kept consistent
			collEff.destroySource = false; // never destroy the box
			collEff.regrowthTime = 0.0F;
			collEff.sourceStorageId = sourceStorageId;
			action.effect = collEff;

			return action;
		}

		/// Factory: Harvest action - harvest items from an entity. Its duration is the source's durability in work
		/// units, which ActionSystem turns into seconds at the work rate of the entry's skill when it starts.
		/// @param itemDefName Item definition to yield
		/// @param quantity Number of items to harvest
		/// @param durability The source's durability (work units)
		/// @param sourcePos Position of the source entity
		/// @param sourceDefName DefName of the source entity
		/// @param destructive If true, entity is destroyed after harvest
		/// @param regrowthTime If not destructive, time until harvestable again
		static Action Harvest(
			const ActionEntries& actions,
			const std::string&	 itemDefName,
			uint32_t			 quantity,
			float				 durability,
			glm::vec2			 sourcePos,
			const std::string&	 sourceDefName,
			bool				 destructive,
			float				 regrowthTime
		) {
			Action action = fromEntry(ActionType::Harvest, actions.harvest());
			action.duration = durability;
			action.targetPosition = sourcePos;

			CollectionEffect collEff;
			collEff.itemDefName = itemDefName;
			collEff.quantity = quantity;
			collEff.sourcePosition = sourcePos;
			collEff.sourceDefName = sourceDefName;
			collEff.destroySource = destructive;
			collEff.regrowthTime = regrowthTime;
			action.effect = collEff;

			return action;
		}

		/// Factory: Craft action - craft items at a station, for the job's work amount at the fixed rate of `workFrom="job"`.
		/// @param recipeDefName Recipe to craft
		/// @param stationEntityId Entity ID of the crafting station
		/// @param stationPos Position of the station
		/// @param workAmount Work ticks to complete (converted to seconds)
		/// @param inputs Input items to consume (defName -> count)
		/// @param outputs Output items to produce (defName -> count)
		static Action Craft(
			const ActionEntries&								 actions,
			const std::string&									 recipeDefName,
			uint64_t											 stationEntityId,
			glm::vec2											 stationPos,
			float												 workAmount,
			const std::vector<std::pair<std::string, uint32_t>>& inputs,
			const std::vector<std::pair<std::string, uint32_t>>& outputs
		) {
			Action action = fromEntry(ActionType::Craft, actions.craft());
			action.duration = workAmount / assets::kJobWorkTicksPerSecond;
			action.targetPosition = stationPos;

			CraftingEffect craftEff;
			craftEff.recipeDefName = recipeDefName;
			craftEff.stationEntityId = stationEntityId;
			craftEff.inputs = inputs;
			craftEff.outputs = outputs;
			action.effect = craftEff;

			return action;
		}

		/// Factory: Deposit action - deposit items into a storage container
		/// @param itemDefName Item to deposit from inventory
		/// @param quantity Number of items to deposit
		/// @param storageEntityId Entity ID of the target storage container
		/// @param storagePos Position of the storage container
		/// @param deliverToCraftStation If true, the target is a crafting station: items stay
		///        in inventory and the deposit only marks them delivered (see DepositEffect).
		static Action Deposit(
			const ActionEntries& actions,
			const std::string&	 itemDefName,
			uint32_t			 quantity,
			uint64_t			 storageEntityId,
			glm::vec2			 storagePos,
			bool				 deliverToCraftStation = false
		) {
			Action action = fromEntry(ActionType::Deposit, actions.deposit());
			action.targetPosition = storagePos;

			DepositEffect depEff;
			depEff.itemDefName = itemDefName;
			depEff.quantity = quantity;
			depEff.storageEntityId = storageEntityId;
			depEff.deliverToCraftStation = deliverToCraftStation;
			action.effect = depEff;

			return action;
		}

		/// Factory: Build action - advance construction work on a blueprint.
		///
		/// Continuous model: this action has no fixed duration. ActionSystem advances the
		/// target blueprint's StructureBlueprint.workDone by constructionWorkRate(skill) x dt
		/// each tick and ends the action when workDone reaches workTotal (firing the
		/// structure-completion callback). A level-0 builder still makes progress; higher
		/// Construction skill builds faster. See building-construction-architecture.md D7.
		/// @param blueprintEntityId Entity carrying the StructureBlueprint to build
		/// @param blueprintPos Position of the build work slot
		/// @param skillLevel Builder's Construction skill (0-20)
		static Action Build(const ActionEntries& actions, uint64_t blueprintEntityId, glm::vec2 blueprintPos, float skillLevel) {
			// Open-ended: completion is gated by workDone >= workTotal, not elapsed time, so the duration is 0.
			Action action = fromEntry(ActionType::Build, actions.build());
			action.targetPosition = blueprintPos;

			ProgressEffect progressEff;
			progressEff.targetEntityId = blueprintEntityId;
			progressEff.skillLevel = skillLevel;
			action.effect = progressEff;

			return action;
		}

		/// Factory: Deconstruct action - tear down a structure (mirror of Build).
		///
		/// Counts the blueprint's workDone back down toward 0 at constructionWorkRate(skill) x dt
		/// each tick, ending when workDone reaches 0 (firing the structure-removed callback).
		/// @param blueprintEntityId Entity carrying the StructureBlueprint to deconstruct
		/// @param blueprintPos Position of the deconstruct work slot
		/// @param skillLevel Builder's Construction skill (0-20)
		static Action Deconstruct(const ActionEntries& actions, uint64_t blueprintEntityId, glm::vec2 blueprintPos, float skillLevel) {
			Action action = fromEntry(ActionType::Deconstruct, actions.deconstruct());
			action.targetPosition = blueprintPos;

			ProgressEffect progressEff;
			progressEff.targetEntityId = blueprintEntityId;
			progressEff.skillLevel = skillLevel;
			action.effect = progressEff;

			return action;
		}

		/// Factory: PickupPackaged action - clear hands and pick up a 2-handed packaged item
		/// Phase 1 of PlacePackaged task. Colonist clears hands first, then picks up.
		/// @param packagedEntityId Entity ID of the packaged item
		/// @param sourcePos Position of the packaged item
		static Action PickupPackaged(const ActionEntries& actions, uint64_t packagedEntityId, glm::vec2 sourcePos) {
			Action action = fromEntry(ActionType::PickupPackaged, actions.pickupPackaged());
			action.targetPosition = sourcePos;

			PlacePackagedEffect placeEff;
			placeEff.packagedEntityId = packagedEntityId;
			// targetPosition not set yet - will be set during phase 2
			action.effect = placeEff;

			return action;
		}

		/// Factory: PlacePackaged action - place a packaged item at target location
		/// Phase 2 of PlacePackaged task. Colonist puts down the item and it becomes placed.
		/// @param packagedEntityId Entity ID of the packaged item being placed
		/// @param targetPos Position to place the item (from Packaged.targetPosition)
		/// @param targetRotation The turn to place it at, taken with the position; unset keeps its own
		static Action PlacePackaged(const ActionEntries& actions, uint64_t packagedEntityId, glm::vec2 targetPos, std::optional<float> targetRotation) {
			Action action = fromEntry(ActionType::PlacePackaged, actions.placePackaged());
			action.targetPosition = targetPos;

			PlacePackagedEffect placeEff;
			placeEff.packagedEntityId = packagedEntityId;
			placeEff.targetPosition = targetPos;
			placeEff.targetRotation = targetRotation;
			action.effect = placeEff;

			return action;
		}
	};

	/// Every action type with its name.
	inline constexpr std::array<std::pair<ActionType, std::string_view>, 16> kActionTypeNames = {{
		{ActionType::None, "None"},
		{ActionType::Eat, "Eat"},
		{ActionType::Drink, "Drink"},
		{ActionType::Wash, "Wash"},
		{ActionType::Sleep, "Sleep"},
		{ActionType::Toilet, "Toilet"},
		{ActionType::Pickup, "Pickup"},
		{ActionType::Harvest, "Harvest"},
		{ActionType::Withdraw, "Withdraw"},
		{ActionType::Craft, "Craft"},
		{ActionType::Deposit, "Deposit"},
		{ActionType::Build, "Build"},
		{ActionType::Deconstruct, "Deconstruct"},
		{ActionType::PickupPackaged, "PickupPackaged"},
		{ActionType::PlacePackaged, "PlacePackaged"},
		{ActionType::Chain, "Chain"},
	}};

	/// The action type's name.
	[[nodiscard]] inline const char* actionTypeName(ActionType type) {
		const auto found = std::ranges::find(kActionTypeNames, type, &std::pair<ActionType, std::string_view>::first);
		return found == kActionTypeNames.end() ? "Unknown" : found->second.data();
	}

	/// What the logs and the dev tools call a running action: a factory's action by its type, a chain's by the entry it
	/// runs.
	[[nodiscard]] inline const char* actionName(const Action& action) {
		return action.type == ActionType::Chain && action.values != nullptr ? action.values->id.c_str() : actionTypeName(action.type);
	}

} // namespace ecs
