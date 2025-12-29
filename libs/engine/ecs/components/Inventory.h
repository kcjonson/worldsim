#pragma once

// Inventory Component for Item Storage
//
// Generic inventory component that can be attached to any entity (colonists,
// pack animals, carts, storage containers). Stores items by defName with
// stack quantities.
//
// Design notes:
// - Slot-based: maxCapacity limits distinct item types, not total items
// - Stack-based: each slot can hold up to maxStackSize of one item type
// - Future: equipment can modify maxCapacity (e.g., backpack adds +5)
// - Future: weight-based capacity can replace slot-based if needed
//
// Related docs:
// - Inventory documentation to be added when inventory system matures

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ecs {

	/// Represents a stack of items for serialization/display
	struct ItemStack {
		std::string defName;
		uint32_t	quantity = 0;
	};

	/// Inventory component - stores items with hand slots and backpack
	///
	/// Colonists have:
	/// - 2 hand slots (leftHand, rightHand) for actively held items
	/// - Backpack (items map) for stored items
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
		// Backpack Storage
		// ============================================================================

		/// Items stored in backpack: defName -> quantity
		std::unordered_map<std::string, uint32_t> items;

		/// Maximum number of distinct item types in backpack
		uint32_t maxCapacity = 10;

		/// Maximum quantity per item stack in backpack
		uint32_t maxStackSize = 99;

		// ============================================================================
		// Query Methods
		// ============================================================================

		/// Check if there's room for at least one more item type
		[[nodiscard]] bool hasSpace() const { return items.size() < maxCapacity; }

		/// Check if there's room for a specific item (existing stack or new slot)
		[[nodiscard]] bool canAdd(const std::string& defName, uint32_t quantity = 1) const {
			// First check if quantity alone exceeds max stack (prevents underflow in later checks)
			if (quantity > maxStackSize) {
				return false;
			}

			auto iter = items.find(defName);
			if (iter != items.end()) {
				// Item exists - check if stack has room
				// Safe from underflow since quantity <= maxStackSize
				return iter->second <= maxStackSize - quantity;
			}
			// New item - check if we have a free slot
			return hasSpace();
		}

		/// Check if inventory contains an item
		[[nodiscard]] bool hasItem(const std::string& defName) const { return items.find(defName) != items.end(); }

		/// Check if inventory has at least the specified quantity
		[[nodiscard]] bool hasQuantity(const std::string& defName, uint32_t quantity) const {
			auto iter = items.find(defName);
			if (iter == items.end()) {
				return false;
			}
			return iter->second >= quantity;
		}

		/// Get quantity of an item (0 if not present)
		[[nodiscard]] uint32_t getQuantity(const std::string& defName) const {
			auto iter = items.find(defName);
			if (iter == items.end()) {
				return 0;
			}
			return iter->second;
		}

		/// Get all items as a vector of ItemStack (for UI display)
		[[nodiscard]] std::vector<ItemStack> getAllItems() const {
			std::vector<ItemStack> result;
			result.reserve(items.size());
			for (const auto& [defName, quantity] : items) {
				result.push_back({defName, quantity});
			}
			return result;
		}

		/// Get total number of distinct item types stored
		[[nodiscard]] uint32_t getSlotCount() const { return static_cast<uint32_t>(items.size()); }

		/// Check if backpack is empty
		[[nodiscard]] bool isEmpty() const { return items.empty(); }

		/// Check if completely empty (no items in hands or backpack)
		[[nodiscard]] bool isCompletelyEmpty() const { return items.empty() && !leftHand.has_value() && !rightHand.has_value(); }

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

		/// Add items to inventory
		/// @param defName Item definition name
		/// @param quantity Amount to add
		/// @return Amount actually added (may be less if stack is full)
		uint32_t addItem(const std::string& defName, uint32_t quantity) {
			auto iter = items.find(defName);

			if (iter != items.end()) {
				// Item exists - add to stack (capped at maxStackSize)
				uint32_t currentQty = iter->second;
				uint32_t spaceInStack = maxStackSize - currentQty;
				uint32_t toAdd = (quantity < spaceInStack) ? quantity : spaceInStack;
				iter->second += toAdd;
				return toAdd;
			}

			// New item - check if we have a free slot
			if (items.size() >= maxCapacity) {
				return 0; // No room for new item type
			}

			// Add new item stack (capped at maxStackSize)
			uint32_t toAdd = (quantity < maxStackSize) ? quantity : maxStackSize;
			items[defName] = toAdd;
			return toAdd;
		}

		/// Remove items from inventory
		/// @param defName Item definition name
		/// @param quantity Amount to remove
		/// @return Amount actually removed (may be less if not enough)
		uint32_t removeItem(const std::string& defName, uint32_t quantity) {
			auto iter = items.find(defName);
			if (iter == items.end()) {
				return 0; // Item not in inventory
			}

			uint32_t currentQty = iter->second;
			uint32_t toRemove = (quantity < currentQty) ? quantity : currentQty;
			iter->second -= toRemove;

			// Remove entry if quantity reaches 0
			if (iter->second == 0) {
				items.erase(iter);
			}

			return toRemove;
		}

		/// Clear all items from inventory (backpack only)
		void clear() { items.clear(); }

		/// Clear everything including hands
		void clearAll() {
			items.clear();
			leftHand.reset();
			rightHand.reset();
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
		// Factory Methods
		// ============================================================================

		/// Create inventory for a colonist (standard capacity)
		static Inventory createForColonist() {
			Inventory inv;
			inv.maxCapacity = 10;
			inv.maxStackSize = 99;
			return inv;
		}

		/// Create inventory for a pack animal (larger capacity)
		static Inventory createForPackAnimal() {
			Inventory inv;
			inv.maxCapacity = 30;
			inv.maxStackSize = 99;
			return inv;
		}

		/// Create inventory for a cart/wagon (largest capacity)
		static Inventory createForCart() {
			Inventory inv;
			inv.maxCapacity = 100;
			inv.maxStackSize = 999;
			return inv;
		}

		/// Create inventory for a storage container
		static Inventory createForStorage() {
			Inventory inv;
			inv.maxCapacity = 50;
			inv.maxStackSize = 999;
			return inv;
		}
	};

} // namespace ecs
