#pragma once

// Inventory Component for Item Storage
//
// Generic inventory component that can be attached to any entity (colonists,
// pack animals, carts, storage containers). Stores items as a list of physical
// stacks: one material per stack, quantity in [1, material.stackSize].
//
// Design notes:
// - Slot-based: maxCapacity is the number of slots; each slot holds one stack.
// - Per-material cap: a stack never exceeds the item's own stackSize (from the
//   asset def). Multiple stacks of the same material may coexist across slots.
// - There is no per-container stack cap; the item's stackSize is the only bound.
// - A defName with no asset def / no itemProperties is unbounded (no stack cap):
//   bare-defName unit tests run without a registry and must not be capped at 1.
//
// Related docs:
// - /docs/technical/physical-stack-inventory.md

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "assets/AssetDefinition.h"
#include "assets/AssetRegistry.h"

namespace ecs {

	/// One physical stack: a single material, quantity in [1, stackSize].
	struct ItemStack {
		std::string defName;
		uint32_t	quantity = 0;
	};

	/// Inventory component - stores items as physical stacks, with hand slots and a belt.
	///
	/// Colonists have:
	/// - 2 hand slots (leftHand, rightHand) for actively held items
	/// - Backpack (items list) for stored stacks
	///
	/// Carrying rules:
	/// - 1-hand items: can be held in hand OR stored in backpack
	/// - 2-hand items: must be held (both hands), cannot fit in backpack
	struct Inventory {
		// ============================================================================
		// Hand Slots (for colonists)
		// ============================================================================

		/// Left hand - holds one item (quantity usually 1)
		std::optional<ItemStack> leftHand;

		/// Right hand - holds one item (quantity usually 1)
		std::optional<ItemStack> rightHand;

		// ============================================================================
		// Belt (quick-draw tool slots)
		// ============================================================================

		/// Belt - fixed quick-draw slots, each holding exactly one one-hand item (a tool
		/// or sidearm), quantity 1. Two-hand "bulky" items never fit here. Inventory has no
		/// asset registry, so callers must guarantee what they stow is one-hand.
		std::array<std::optional<ItemStack>, 2> belt;

		// ============================================================================
		// Large Entity Carrying
		// ============================================================================

		/// Entity ID of a packaged entity being carried (requires both hands)
		/// This is separate from hand ItemStacks because packaged entities are
		/// full ECS entities (with Position, components, etc.), not simple items.
		/// When set, leftHand/rightHand should show the entity's defName for UI display.
		std::optional<uint64_t> carryingPackagedEntity;

		// ============================================================================
		// Backpack Storage
		// ============================================================================

		/// Stored stacks. Each entry is one physical stack (defName + quantity in
		/// [1, stackSize]); the same material may appear in several entries (slots).
		std::vector<ItemStack> items;

		/// Number of slots: how many distinct stacks the backpack can hold.
		uint32_t maxCapacity = 10;

		/// Max cargo weight this entity can carry, kilograms. Tools (equipment) don't count
		/// against it; see ecs::carriedCargoMassKg. Caps how much a colonist hauls per trip.
		float carryCapacityKg = 35.0F;

	  private:
		/// Per-stack cap for `defName`: the item's stackSize, or unbounded (UINT32_MAX) when
		/// the def is unknown or carries no itemProperties. Unbounded (not 1) is intentional:
		/// entities and unregistered defNames aren't stack-limited, and registry-free tests
		/// rely on bare defNames staying uncapped.
		[[nodiscard]] static uint32_t itemStackSize(const std::string& defName) {
			const auto* def = engine::assets::AssetRegistry::Get().getDefinition(defName);
			if (def != nullptr && def->itemProperties.has_value()) {
				return def->itemProperties->stackSize;
			}
			return UINT32_MAX;
		}

		/// Slots currently holding a stack.
		[[nodiscard]] uint32_t usedSlots() const { return static_cast<uint32_t>(items.size()); }

		/// Free slots remaining (never negative).
		[[nodiscard]] uint32_t freeSlots() const {
			const uint32_t used = usedSlots();
			return used < maxCapacity ? maxCapacity - used : 0U;
		}

	  public:
		// ============================================================================
		// Query Methods
		// ============================================================================

		/// Check if there's a free slot for one more stack.
		[[nodiscard]] bool hasSpace() const { return usedSlots() < maxCapacity; }

		/// Check if `quantity` of `defName` fits: existing-stack headroom plus free-slot capacity.
		[[nodiscard]] bool canAdd(const std::string& defName, uint32_t quantity = 1) const {
			return quantity <= addableCount(defName);
		}

		/// How many of `defName` addItem() would actually accept right now: the sum of headroom
		/// left in every existing stack of that material, plus a full stack for each free slot.
		/// Callers that withdraw from a finite source (a tree's resource pool) must clamp by this
		/// so they never pull more than the backpack can hold.
		[[nodiscard]] uint32_t addableCount(const std::string& defName) const {
			const uint32_t cap = itemStackSize(defName);
			uint64_t	   total = 0; // 64-bit accumulate: the freeSlots * cap term overflows uint32 for an unbounded cap (existing-stack headroom can't)
			for (const auto& stack : items) {
				if (stack.defName == defName && stack.quantity < cap) {
					total += cap - stack.quantity;
				}
			}
			total += static_cast<uint64_t>(freeSlots()) * cap;
			return total > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(total);
		}

		/// Check if inventory contains any stack of an item.
		[[nodiscard]] bool hasItem(const std::string& defName) const {
			return std::any_of(items.begin(), items.end(), [&](const ItemStack& s) { return s.defName == defName; });
		}

		/// Check if inventory has at least the specified quantity (summed across stacks).
		[[nodiscard]] bool hasQuantity(const std::string& defName, uint32_t quantity) const {
			return getQuantity(defName) >= quantity;
		}

		/// Get total quantity of an item across all its stacks (0 if not present).
		[[nodiscard]] uint32_t getQuantity(const std::string& defName) const {
			uint64_t total = 0;
			for (const auto& stack : items) {
				if (stack.defName == defName) {
					total += stack.quantity;
				}
			}
			return total > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(total);
		}

		/// Get all items for UI display, aggregated to ONE ItemStack per material (summed),
		/// so the gear panel shows "Wood x46" rather than one row per slot.
		[[nodiscard]] std::vector<ItemStack> getAllItems() const {
			std::vector<ItemStack> result;
			for (const auto& stack : items) {
				auto existing = std::find_if(result.begin(), result.end(), [&](const ItemStack& s) { return s.defName == stack.defName; });
				if (existing != result.end()) {
					existing->quantity += stack.quantity;
				} else {
					result.push_back(stack);
				}
			}
			return result;
		}

		/// Number of slots in use (one per stack).
		[[nodiscard]] uint32_t getSlotCount() const { return usedSlots(); }

		/// Check if backpack is empty.
		[[nodiscard]] bool isEmpty() const { return items.empty(); }

		/// Check if completely empty (no items in hands, belt, backpack, or carrying entity)
		[[nodiscard]] bool isCompletelyEmpty() const {
			const bool beltEmpty = !belt[0].has_value() && !belt[1].has_value();
			return items.empty() && !leftHand.has_value() && !rightHand.has_value() && beltEmpty &&
				   !carryingPackagedEntity.has_value();
		}

		// ============================================================================
		// Hand Query Methods
		// ============================================================================

		/// Get number of free hands (0, 1, or 2)
		[[nodiscard]] uint8_t freeHandCount() const {
			uint8_t count = 0;
			if (!leftHand.has_value()) {
				++count;
			}
			if (!rightHand.has_value()) {
				++count;
			}
			return count;
		}

		/// Check if colonist has enough free hands
		[[nodiscard]] bool hasHandsFree(uint8_t count) const { return freeHandCount() >= count; }

		/// Check if holding a specific item in either hand
		[[nodiscard]] bool isHolding(const std::string& defName) const {
			return (leftHand.has_value() && leftHand->defName == defName) || (rightHand.has_value() && rightHand->defName == defName);
		}

		/// Get item held in left hand (nullptr if empty)
		[[nodiscard]] const ItemStack* getLeftHand() const { return leftHand.has_value() ? &leftHand.value() : nullptr; }

		/// Get item held in right hand (nullptr if empty)
		[[nodiscard]] const ItemStack* getRightHand() const { return rightHand.has_value() ? &rightHand.value() : nullptr; }

		// ============================================================================
		// Mutation Methods
		// ============================================================================

		/// Add items to inventory: top up existing non-full stacks of `defName` to its stackSize,
		/// then open new slots (each a stack <= stackSize) while a free slot remains.
		/// @return Amount actually added (may be < quantity when out of slots).
		uint32_t addItem(const std::string& defName, uint32_t quantity) {
			if (quantity == 0) {
				return 0; // adding nothing is a no-op (never create a zero-quantity slot)
			}
			const uint32_t cap = itemStackSize(defName);
			if (cap == 0) {
				return 0; // an unstackable-at-zero item can never be held
			}
			uint32_t remaining = quantity;

			// Top up existing stacks of this material first.
			for (auto& stack : items) {
				if (remaining == 0) {
					break;
				}
				if (stack.defName == defName && stack.quantity < cap) {
					const uint32_t room = cap - stack.quantity;
					const uint32_t toAdd = std::min(remaining, room);
					stack.quantity += toAdd;
					remaining -= toAdd;
				}
			}

			// Spill the rest into new slots.
			while (remaining > 0 && hasSpace()) {
				const uint32_t toAdd = std::min(remaining, cap);
				items.push_back(ItemStack{defName, toAdd});
				remaining -= toAdd;
			}

			return quantity - remaining;
		}

		/// Remove items from inventory, draining across this material's stacks (last slot first)
		/// and erasing emptied slots.
		/// @return Amount actually removed (may be less if not enough).
		uint32_t removeItem(const std::string& defName, uint32_t quantity) {
			if (quantity == 0) {
				return 0;
			}
			uint32_t remaining = quantity;
			// Drain from the back so erasing emptied slots doesn't disturb the iteration.
			for (auto it = items.rbegin(); it != items.rend() && remaining > 0;) {
				if (it->defName == defName) {
					const uint32_t toRemove = std::min(remaining, it->quantity);
					it->quantity -= toRemove;
					remaining -= toRemove;
					if (it->quantity == 0) {
						// Convert reverse iterator to the forward iterator it refers to, then erase.
						it = std::reverse_iterator(items.erase(std::next(it).base()));
						continue;
					}
				}
				++it;
			}
			return quantity - remaining;
		}

		/// Clear all items from inventory (backpack only)
		void clear() { items.clear(); }

		/// Clear everything including hands, belt, and carried entity
		void clearAll() {
			items.clear();
			leftHand.reset();
			rightHand.reset();
			belt[0].reset();
			belt[1].reset();
			carryingPackagedEntity.reset();
		}

		// ============================================================================
		// Hand Mutation Methods
		// ============================================================================

		/// Pick up an item into hands
		/// @param defName Item definition name
		/// @param handsRequired How many hands needed (1 or 2)
		/// @return true if successfully picked up
		bool pickUp(const std::string& defName, uint8_t handsRequired = 1) {
			if (handsRequired == 2) {
				// Two-handed: need both hands free
				if (leftHand.has_value() || rightHand.has_value()) {
					return false;
				}
				// Put in both hands (same item reference)
				leftHand = ItemStack{defName, 1};
				rightHand = ItemStack{defName, 1};
				return true;
			}

			// One-handed: find a free hand
			if (!rightHand.has_value()) {
				rightHand = ItemStack{defName, 1};
				return true;
			}
			if (!leftHand.has_value()) {
				leftHand = ItemStack{defName, 1};
				return true;
			}
			return false; // No free hands
		}

		/// Put down item(s) from hands
		/// @param defName If specified, only put down this item; otherwise put down everything
		/// @return The item that was put down (nullopt if nothing)
		std::optional<ItemStack> putDown(const std::string& defName = "") {
			if (defName.empty()) {
				// Put down everything - prioritize right hand
				if (rightHand.has_value()) {
					auto item = rightHand.value();
					// Check if two-handed (same item in both hands)
					if (leftHand.has_value() && leftHand->defName == item.defName) {
						leftHand.reset();
					}
					rightHand.reset();
					return item;
				}
				if (leftHand.has_value()) {
					auto item = leftHand.value();
					leftHand.reset();
					return item;
				}
				return std::nullopt;
			}

			// Put down specific item
			if (rightHand.has_value() && rightHand->defName == defName) {
				auto item = rightHand.value();
				// Check if two-handed
				if (leftHand.has_value() && leftHand->defName == defName) {
					leftHand.reset();
				}
				rightHand.reset();
				return item;
			}
			if (leftHand.has_value() && leftHand->defName == defName) {
				auto item = leftHand.value();
				leftHand.reset();
				return item;
			}
			return std::nullopt;
		}

		/// Stow item from hands to backpack
		/// @param defName Item to stow (must be in hands)
		/// @return true if successfully stowed
		/// @note Two-handed items (held in both hands) cannot be stowed - they must be
		///       placed on the ground. This is intentional as large items like furniture
		///       shouldn't fit in a backpack.
		bool stowToBackpack(const std::string& defName) {
			// Check if we're holding this item
			bool inRight = rightHand.has_value() && rightHand->defName == defName;
			bool inLeft = leftHand.has_value() && leftHand->defName == defName;

			if (!inRight && !inLeft) {
				return false; // Not holding this item
			}

			// Two-handed items can't go in backpack
			if (inRight && inLeft) {
				return false;
			}

			// Check if backpack has room
			if (!canAdd(defName, 1)) {
				return false;
			}

			// Move to backpack
			addItem(defName, 1);

			// Remove from hand
			if (inRight) {
				rightHand.reset();
			} else {
				leftHand.reset();
			}
			return true;
		}

		/// Take item from backpack to hands
		/// @param defName Item to take
		/// @param handsRequired How many hands needed
		/// @return true if successfully taken
		bool takeFromBackpack(const std::string& defName, uint8_t handsRequired = 1) {
			// Check if item is in backpack
			if (!hasItem(defName)) {
				return false;
			}

			// Try to pick up
			if (!pickUp(defName, handsRequired)) {
				return false;
			}

			// Remove from backpack
			removeItem(defName, 1);
			return true;
		}

		// ============================================================================
		// Belt Mutation Methods
		// ============================================================================

		/// Check if the belt has a free quick-draw slot
		[[nodiscard]] bool beltHasFreeSlot() const { return !belt[0].has_value() || !belt[1].has_value(); }

		/// Stow a one-hand item into the first free belt slot
		/// @param defName Item to stow (one slot holds exactly one, quantity 1)
		/// @return true if seated, false if the belt is full
		/// @note The belt only accepts one-hand items, but Inventory has no asset registry to
		///       verify hand class - the CALLER must guarantee `defName` is one-hand.
		bool stowToBelt(const std::string& defName) {
			for (auto& slot : belt) {
				if (!slot.has_value()) {
					slot = ItemStack{defName, 1};
					return true;
				}
			}
			return false; // No free belt slot
		}

		/// Take an item off the belt by defName (first matching slot)
		/// @param defName Item to remove
		/// @return true if a matching belt item was removed
		bool takeFromBelt(const std::string& defName) {
			for (auto& slot : belt) {
				if (slot.has_value() && slot->defName == defName) {
					slot.reset();
					return true;
				}
			}
			return false; // Not on the belt
		}

		// ============================================================================
		// Factory Methods
		// ============================================================================

		/// Create inventory for a colonist (standard slot count)
		static Inventory createForColonist() {
			Inventory inv;
			inv.maxCapacity = 10;
			inv.carryCapacityKg = 35.0F;
			return inv;
		}

		/// Create inventory for a pack animal (more slots)
		static Inventory createForPackAnimal() {
			Inventory inv;
			inv.maxCapacity = 30;
			inv.carryCapacityKg = 120.0F;
			return inv;
		}

		/// Create inventory for a cart/wagon (most slots)
		static Inventory createForCart() {
			Inventory inv;
			inv.maxCapacity = 100;
			inv.carryCapacityKg = 500.0F;
			return inv;
		}

		/// Create inventory for a storage container (static; weight is effectively unbounded)
		static Inventory createForStorage() {
			Inventory inv;
			inv.maxCapacity = 50;
			inv.carryCapacityKg = 1.0e9F;
			return inv;
		}
	};

} // namespace ecs
