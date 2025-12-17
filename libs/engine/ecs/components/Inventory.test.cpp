#include "Inventory.h"

#include <gtest/gtest.h>

using namespace ecs;

// ============================================================================
// Basic Operations Tests
// ============================================================================

TEST(InventoryTests, DefaultConstruction) {
	Inventory inv;

	EXPECT_TRUE(inv.isEmpty());
	EXPECT_EQ(inv.getSlotCount(), 0);
	EXPECT_TRUE(inv.hasSpace());
	EXPECT_EQ(inv.maxCapacity, 10);
	EXPECT_EQ(inv.maxStackSize, 99);
}

TEST(InventoryTests, AddSingleItem) {
	Inventory inv;
	uint32_t added = inv.addItem("Berry", 5);

	EXPECT_EQ(added, 5);
	EXPECT_FALSE(inv.isEmpty());
	EXPECT_EQ(inv.getSlotCount(), 1);
	EXPECT_TRUE(inv.hasItem("Berry"));
	EXPECT_EQ(inv.getQuantity("Berry"), 5);
}

TEST(InventoryTests, AddMultipleItemTypes) {
	Inventory inv;
	inv.addItem("Berry", 10);
	inv.addItem("Stick", 5);
	inv.addItem("Stone", 3);

	EXPECT_EQ(inv.getSlotCount(), 3);
	EXPECT_EQ(inv.getQuantity("Berry"), 10);
	EXPECT_EQ(inv.getQuantity("Stick"), 5);
	EXPECT_EQ(inv.getQuantity("Stone"), 3);
}

TEST(InventoryTests, AddToExistingStack) {
	Inventory inv;
	inv.addItem("Berry", 5);
	uint32_t added = inv.addItem("Berry", 10);

	EXPECT_EQ(added, 10);
	EXPECT_EQ(inv.getSlotCount(), 1); // Still one slot
	EXPECT_EQ(inv.getQuantity("Berry"), 15);
}

TEST(InventoryTests, ClearInventory) {
	Inventory inv;
	inv.addItem("Berry", 10);
	inv.addItem("Stick", 5);

	EXPECT_FALSE(inv.isEmpty());

	inv.clear();

	EXPECT_TRUE(inv.isEmpty());
	EXPECT_EQ(inv.getSlotCount(), 0);
	EXPECT_FALSE(inv.hasItem("Berry"));
	EXPECT_FALSE(inv.hasItem("Stick"));
}

// ============================================================================
// Query Method Tests
// ============================================================================

TEST(InventoryTests, HasSpaceWhenEmpty) {
	Inventory inv;
	EXPECT_TRUE(inv.hasSpace());
}

TEST(InventoryTests, HasSpaceWhenPartiallyFull) {
	Inventory inv;
	inv.maxCapacity = 3;
	inv.addItem("Berry", 1);
	inv.addItem("Stick", 1);

	EXPECT_TRUE(inv.hasSpace());
}

TEST(InventoryTests, HasSpaceWhenFull) {
	Inventory inv;
	inv.maxCapacity = 2;
	inv.addItem("Berry", 1);
	inv.addItem("Stick", 1);

	EXPECT_FALSE(inv.hasSpace());
}

TEST(InventoryTests, HasItemReturnsTrue) {
	Inventory inv;
	inv.addItem("Berry", 5);

	EXPECT_TRUE(inv.hasItem("Berry"));
}

TEST(InventoryTests, HasItemReturnsFalse) {
	Inventory inv;
	inv.addItem("Berry", 5);

	EXPECT_FALSE(inv.hasItem("Stick"));
}

TEST(InventoryTests, HasQuantityExact) {
	Inventory inv;
	inv.addItem("Berry", 10);

	EXPECT_TRUE(inv.hasQuantity("Berry", 10));
}

TEST(InventoryTests, HasQuantityLessThanActual) {
	Inventory inv;
	inv.addItem("Berry", 10);

	EXPECT_TRUE(inv.hasQuantity("Berry", 5));
}

TEST(InventoryTests, HasQuantityMoreThanActual) {
	Inventory inv;
	inv.addItem("Berry", 10);

	EXPECT_FALSE(inv.hasQuantity("Berry", 15));
}

TEST(InventoryTests, HasQuantityItemNotPresent) {
	Inventory inv;

	EXPECT_FALSE(inv.hasQuantity("Berry", 1));
}

TEST(InventoryTests, GetQuantityReturnsZeroForMissing) {
	Inventory inv;

	EXPECT_EQ(inv.getQuantity("Berry"), 0);
}

TEST(InventoryTests, GetAllItemsEmpty) {
	Inventory inv;
	auto items = inv.getAllItems();

	EXPECT_TRUE(items.empty());
}

TEST(InventoryTests, GetAllItemsMultiple) {
	Inventory inv;
	inv.addItem("Berry", 10);
	inv.addItem("Stick", 5);

	auto items = inv.getAllItems();

	EXPECT_EQ(items.size(), 2);

	// Find items by name (order not guaranteed due to unordered_map)
	bool foundBerry = false;
	bool foundStick = false;
	for (const auto& item : items) {
		if (item.defName == "Berry" && item.quantity == 10) {
			foundBerry = true;
		}
		if (item.defName == "Stick" && item.quantity == 5) {
			foundStick = true;
		}
	}
	EXPECT_TRUE(foundBerry);
	EXPECT_TRUE(foundStick);
}

// ============================================================================
// CanAdd Tests
// ============================================================================

TEST(InventoryTests, CanAddNewItemWithSpace) {
	Inventory inv;

	EXPECT_TRUE(inv.canAdd("Berry", 5));
}

TEST(InventoryTests, CanAddNewItemNoSpace) {
	Inventory inv;
	inv.maxCapacity = 1;
	inv.addItem("Stick", 1);

	EXPECT_FALSE(inv.canAdd("Berry", 5));
}

TEST(InventoryTests, CanAddToExistingStackWithRoom) {
	Inventory inv;
	inv.maxStackSize = 20;
	inv.addItem("Berry", 10);

	EXPECT_TRUE(inv.canAdd("Berry", 5));
}

TEST(InventoryTests, CanAddToExistingStackNoRoom) {
	Inventory inv;
	inv.maxStackSize = 10;
	inv.addItem("Berry", 8);

	EXPECT_FALSE(inv.canAdd("Berry", 5)); // 8 + 5 = 13 > 10
}

TEST(InventoryTests, CanAddToExistingStackExactFit) {
	Inventory inv;
	inv.maxStackSize = 10;
	inv.addItem("Berry", 5);

	EXPECT_TRUE(inv.canAdd("Berry", 5)); // 5 + 5 = 10
}

TEST(InventoryTests, CanAddOverflowProtection) {
	Inventory inv;
	inv.maxStackSize = 100;
	inv.addItem("Berry", 90);

	// This would overflow if we did iter->second + quantity without checking
	EXPECT_FALSE(inv.canAdd("Berry", UINT32_MAX));
}

TEST(InventoryTests, CanAddNewItemExceedsStackSize) {
	Inventory inv;
	inv.maxStackSize = 10;

	EXPECT_FALSE(inv.canAdd("Berry", 15)); // New item but quantity > maxStackSize
}

// ============================================================================
// AddItem Capacity Tests
// ============================================================================

TEST(InventoryTests, AddItemCappedAtStackSize) {
	Inventory inv;
	inv.maxStackSize = 10;

	uint32_t added = inv.addItem("Berry", 20);

	EXPECT_EQ(added, 10);
	EXPECT_EQ(inv.getQuantity("Berry"), 10);
}

TEST(InventoryTests, AddItemToFullStack) {
	Inventory inv;
	inv.maxStackSize = 10;
	inv.addItem("Berry", 10);

	uint32_t added = inv.addItem("Berry", 5);

	EXPECT_EQ(added, 0);
	EXPECT_EQ(inv.getQuantity("Berry"), 10);
}

TEST(InventoryTests, AddItemPartialStackRoom) {
	Inventory inv;
	inv.maxStackSize = 10;
	inv.addItem("Berry", 7);

	uint32_t added = inv.addItem("Berry", 5);

	EXPECT_EQ(added, 3); // Only 3 can fit
	EXPECT_EQ(inv.getQuantity("Berry"), 10);
}

TEST(InventoryTests, AddItemNoSlotAvailable) {
	Inventory inv;
	inv.maxCapacity = 1;
	inv.addItem("Berry", 5);

	uint32_t added = inv.addItem("Stick", 5);

	EXPECT_EQ(added, 0);
	EXPECT_FALSE(inv.hasItem("Stick"));
}

// ============================================================================
// RemoveItem Tests
// ============================================================================

TEST(InventoryTests, RemoveItemFully) {
	Inventory inv;
	inv.addItem("Berry", 10);

	uint32_t removed = inv.removeItem("Berry", 10);

	EXPECT_EQ(removed, 10);
	EXPECT_FALSE(inv.hasItem("Berry"));
	EXPECT_TRUE(inv.isEmpty());
}

TEST(InventoryTests, RemoveItemPartially) {
	Inventory inv;
	inv.addItem("Berry", 10);

	uint32_t removed = inv.removeItem("Berry", 3);

	EXPECT_EQ(removed, 3);
	EXPECT_TRUE(inv.hasItem("Berry"));
	EXPECT_EQ(inv.getQuantity("Berry"), 7);
}

TEST(InventoryTests, RemoveItemMoreThanAvailable) {
	Inventory inv;
	inv.addItem("Berry", 5);

	uint32_t removed = inv.removeItem("Berry", 10);

	EXPECT_EQ(removed, 5);
	EXPECT_FALSE(inv.hasItem("Berry"));
}

TEST(InventoryTests, RemoveItemNotPresent) {
	Inventory inv;

	uint32_t removed = inv.removeItem("Berry", 5);

	EXPECT_EQ(removed, 0);
}

TEST(InventoryTests, RemoveItemZeroQuantity) {
	Inventory inv;
	inv.addItem("Berry", 10);

	uint32_t removed = inv.removeItem("Berry", 0);

	EXPECT_EQ(removed, 0);
	EXPECT_EQ(inv.getQuantity("Berry"), 10);
}

// ============================================================================
// Factory Method Tests
// ============================================================================

TEST(InventoryTests, CreateForColonist) {
	Inventory inv = Inventory::createForColonist();

	EXPECT_EQ(inv.maxCapacity, 10);
	EXPECT_EQ(inv.maxStackSize, 99);
	EXPECT_TRUE(inv.isEmpty());
}

TEST(InventoryTests, CreateForPackAnimal) {
	Inventory inv = Inventory::createForPackAnimal();

	EXPECT_EQ(inv.maxCapacity, 30);
	EXPECT_EQ(inv.maxStackSize, 99);
	EXPECT_TRUE(inv.isEmpty());
}

TEST(InventoryTests, CreateForCart) {
	Inventory inv = Inventory::createForCart();

	EXPECT_EQ(inv.maxCapacity, 100);
	EXPECT_EQ(inv.maxStackSize, 999);
	EXPECT_TRUE(inv.isEmpty());
}

TEST(InventoryTests, CreateForStorage) {
	Inventory inv = Inventory::createForStorage();

	EXPECT_EQ(inv.maxCapacity, 50);
	EXPECT_EQ(inv.maxStackSize, 999);
	EXPECT_TRUE(inv.isEmpty());
}

// ============================================================================
// Edge Case Tests
// ============================================================================

TEST(InventoryTests, ZeroCapacity) {
	Inventory inv;
	inv.maxCapacity = 0;

	EXPECT_FALSE(inv.hasSpace());
	EXPECT_EQ(inv.addItem("Berry", 5), 0);
}

TEST(InventoryTests, ZeroStackSize) {
	Inventory inv;
	inv.maxStackSize = 0;

	EXPECT_EQ(inv.addItem("Berry", 5), 0);
	EXPECT_FALSE(inv.canAdd("Berry", 1));
}

TEST(InventoryTests, EmptyStringDefName) {
	Inventory inv;
	inv.addItem("", 5);

	EXPECT_TRUE(inv.hasItem(""));
	EXPECT_EQ(inv.getQuantity(""), 5);
}

TEST(InventoryTests, LargeQuantities) {
	Inventory inv;
	inv.maxStackSize = UINT32_MAX;

	uint32_t added = inv.addItem("Berry", 1000000);

	EXPECT_EQ(added, 1000000);
	EXPECT_EQ(inv.getQuantity("Berry"), 1000000);
}

TEST(InventoryTests, FillToExactCapacity) {
	Inventory inv;
	inv.maxCapacity = 3;

	inv.addItem("Berry", 1);
	inv.addItem("Stick", 1);
	inv.addItem("Stone", 1);

	EXPECT_FALSE(inv.hasSpace());
	EXPECT_EQ(inv.getSlotCount(), 3);
}
