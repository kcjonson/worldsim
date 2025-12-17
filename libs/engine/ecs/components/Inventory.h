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
// - /docs/design/game-systems/colonists/inventory.md (TODO: create)

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace ecs {

/// Represents a stack of items for serialization/display
struct ItemStack {
	std::string defName;
	uint32_t	quantity = 0;
};

/// Inventory component - stores items by defName with stack quantities
struct Inventory {
	/// Items stored: defName -> quantity
	std::unordered_map<std::string, uint32_t> items;

	/// Maximum number of distinct item types (slots)
	/// NOTE: Temporary default. Future: modified by equipment (backpacks),
	/// colonist attributes (strength), and entity type.
	uint32_t maxCapacity = 10;

	/// Maximum quantity per item stack
	/// NOTE: Temporary default. Future: may vary by item type (heavy vs light).
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
	[[nodiscard]] bool hasItem(const std::string& defName) const {
		return items.find(defName) != items.end();
	}

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

	/// Check if inventory is empty
	[[nodiscard]] bool isEmpty() const { return items.empty(); }

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

	/// Clear all items from inventory
	void clear() { items.clear(); }

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
