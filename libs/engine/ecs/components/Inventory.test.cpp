#include "Inventory.h"

#include "assets/AssetDefinition.h"
#include "assets/AssetRegistry.h"

#include <gtest/gtest.h>

using namespace ecs;

// ============================================================================
// Registry-free tests
//
// With no asset registry, a bare defName has no itemProperties, so its stack cap
// is UNBOUNDED (UINT32_MAX), not 1. The backpack is then bound only by its slot
// count (maxCapacity). Most inventory call sites and these tests run without a
// registry, so this fallback is what keeps a single slot holding an arbitrary
// quantity. Per-material stack caps are exercised in InventoryStackTest below.
// ============================================================================

TEST(InventoryTests, DefaultConstruction) {
	Inventory inv;

	EXPECT_TRUE(inv.isEmpty());
	EXPECT_EQ(inv.getSlotCount(), 0);
	EXPECT_TRUE(inv.hasSpace());
	EXPECT_EQ(inv.maxCapacity, 10);
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

TEST(InventoryTests, AddToExistingStackUnbounded) {
	// No registry: one unbounded slot soaks up the whole amount.
	Inventory inv;
	inv.addItem("Berry", 5);
	uint32_t added = inv.addItem("Berry", 10);

	EXPECT_EQ(added, 10);
	EXPECT_EQ(inv.getSlotCount(), 1); // still one slot, no cap reached
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
// CanAdd Tests (slot-bound; no registry => unbounded per-stack)
// ============================================================================

TEST(InventoryTests, CanAddNewItemWithSpace) {
	Inventory inv;
	EXPECT_TRUE(inv.canAdd("Berry", 5));
}

TEST(InventoryTests, CanAddNewItemNoSpace) {
	Inventory inv;
	inv.maxCapacity = 1;
	inv.addItem("Stick", 1);

	EXPECT_FALSE(inv.canAdd("Berry", 5)); // no free slot for a new type
}

TEST(InventoryTests, CanAddToExistingUnboundedStack) {
	Inventory inv;
	inv.addItem("Berry", 10);

	EXPECT_TRUE(inv.canAdd("Berry", 5)); // unbounded stack always has room
}

// ============================================================================
// AddItem slot-accounting (no registry)
// ============================================================================

TEST(InventoryTests, AddItemNoSlotAvailable) {
	Inventory inv;
	inv.maxCapacity = 1;
	inv.addItem("Berry", 5);

	uint32_t added = inv.addItem("Stick", 5);

	EXPECT_EQ(added, 0);
	EXPECT_FALSE(inv.hasItem("Stick"));
}

TEST(InventoryTests, FullContainerRefusesNewType) {
	// Every slot used (by Berry): a new type has nowhere to go even though the
	// existing unbounded stack would still accept more Berry.
	Inventory inv;
	inv.maxCapacity = 2;
	inv.addItem("Berry", 5);
	inv.addItem("Stick", 5);

	EXPECT_FALSE(inv.hasSpace());
	EXPECT_EQ(inv.addItem("Stone", 1), 0U) << "no free slot for a third type";
	EXPECT_EQ(inv.addableCount("Stone"), 0U);
	EXPECT_GT(inv.addItem("Berry", 1), 0U) << "existing stacks still accept their own type";
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
// Factory Method Tests (slot counts only; no per-container stack cap)
// ============================================================================

TEST(InventoryTests, CreateForColonist) {
	Inventory inv = Inventory::createForColonist();

	EXPECT_EQ(inv.maxCapacity, 10);
	EXPECT_TRUE(inv.isEmpty());
}

TEST(InventoryTests, CreateForPackAnimal) {
	Inventory inv = Inventory::createForPackAnimal();

	EXPECT_EQ(inv.maxCapacity, 30);
	EXPECT_TRUE(inv.isEmpty());
}

TEST(InventoryTests, CreateForCart) {
	Inventory inv = Inventory::createForCart();

	EXPECT_EQ(inv.maxCapacity, 100);
	EXPECT_TRUE(inv.isEmpty());
}

TEST(InventoryTests, CreateForStorage) {
	Inventory inv = Inventory::createForStorage();

	EXPECT_EQ(inv.maxCapacity, 50);
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

TEST(InventoryTests, EmptyStringDefName) {
	Inventory inv;
	inv.addItem("", 5);

	EXPECT_TRUE(inv.hasItem(""));
	EXPECT_EQ(inv.getQuantity(""), 5);
}

TEST(InventoryTests, LargeQuantities) {
	// Unbounded fallback: a single slot holds an arbitrarily large quantity.
	Inventory inv;
	uint32_t added = inv.addItem("Berry", 1000000);

	EXPECT_EQ(added, 1000000);
	EXPECT_EQ(inv.getQuantity("Berry"), 1000000);
	EXPECT_EQ(inv.getSlotCount(), 1U);
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

// ============================================================================
// addableCount: stack headroom + free-slot capacity. Harvest withdrawal clamps
// by this so a finite source pool is never over-debited. No registry => unbounded.
// ============================================================================

TEST(InventoryTests, AddableCount_NewItemNoFreeSlotIsZero) {
	Inventory inv;
	inv.maxCapacity = 1;
	inv.addItem("Wood", 1);
	EXPECT_EQ(inv.addableCount("Stone"), 0U); // no slot left for a new type
}

TEST(InventoryTests, AddableCount_UnboundedNewItemSaturates) {
	// A free slot for an unbounded item reports UINT32_MAX of headroom (no count cap).
	Inventory inv;
	EXPECT_EQ(inv.addableCount("Wood"), UINT32_MAX);
}

TEST(InventoryTests, AddItemZeroIsNoOp) {
	Inventory inv;
	EXPECT_EQ(inv.addItem("Wood", 0), 0U);
	EXPECT_FALSE(inv.hasItem("Wood")); // no zero-quantity slot created
	EXPECT_EQ(inv.getSlotCount(), 0U);
}

// ============================================================================
// Per-material stack caps (with a registry): spill-on-add, drain-across-stacks,
// addableCount summing, and the display aggregation getAllItems performs.
// ============================================================================

class InventoryStackTest : public ::testing::Test {
  protected:
	void SetUp() override {
		auto& assets = engine::assets::AssetRegistry::Get();
		assets.clearDefinitions();
		// Wood: a 40-stack material. Stone: a 10-stack material.
		registerStackable("Wood", 40);
		registerStackable("Stone", 10);
	}

	void TearDown() override { engine::assets::AssetRegistry::Get().clearDefinitions(); }

	static void registerStackable(const char* name, uint32_t stackSize) {
		engine::assets::AssetDefinition def;
		def.defName = name;
		def.label = name;
		engine::assets::ItemProperties item;
		item.stackSize = stackSize;
		def.itemProperties = item;
		engine::assets::AssetRegistry::Get().registerTestDefinition(std::move(def));
	}
};

TEST_F(InventoryStackTest, AddSpillsAcrossStacksAtTheStackSize) {
	// 50 Wood at a 40 stack size => a full stack of 40 plus a stack of 10 (two slots).
	Inventory inv;
	const uint32_t added = inv.addItem("Wood", 50);

	EXPECT_EQ(added, 50U);
	EXPECT_EQ(inv.getQuantity("Wood"), 50U) << "getQuantity sums across both stacks";
	EXPECT_EQ(inv.getSlotCount(), 2U) << "spilled into two slots";
	ASSERT_EQ(inv.items.size(), 2U);
	EXPECT_EQ(inv.items[0].quantity, 40U) << "first stack filled to the cap";
	EXPECT_EQ(inv.items[1].quantity, 10U) << "remainder opened a new stack";
}

TEST_F(InventoryStackTest, AddTopsUpExistingStackBeforeOpeningNew) {
	Inventory inv;
	inv.addItem("Wood", 30);		   // one stack of 30
	const uint32_t added = inv.addItem("Wood", 25); // tops the first to 40, then 15 in a new slot

	EXPECT_EQ(added, 25U);
	EXPECT_EQ(inv.getQuantity("Wood"), 55U);
	ASSERT_EQ(inv.items.size(), 2U);
	EXPECT_EQ(inv.items[0].quantity, 40U);
	EXPECT_EQ(inv.items[1].quantity, 15U);
}

TEST_F(InventoryStackTest, AddStopsWhenSlotsRunOut) {
	// 2 slots, 40 stack => capacity 80. Adding 100 takes 80 and reports the shortfall.
	Inventory inv;
	inv.maxCapacity = 2;
	const uint32_t added = inv.addItem("Wood", 100);

	EXPECT_EQ(added, 80U) << "two 40-stacks is all that fits";
	EXPECT_EQ(inv.getQuantity("Wood"), 80U);
	EXPECT_EQ(inv.getSlotCount(), 2U);
}

TEST_F(InventoryStackTest, RemoveDrainsAcrossStacks) {
	Inventory inv;
	inv.addItem("Wood", 90); // stacks: 40, 40, 10 (three slots)
	ASSERT_EQ(inv.getSlotCount(), 3U);

	const uint32_t removed = inv.removeItem("Wood", 55); // drains 10 + 40 + 5 from the back

	EXPECT_EQ(removed, 55U);
	EXPECT_EQ(inv.getQuantity("Wood"), 35U);
	EXPECT_EQ(inv.getSlotCount(), 1U) << "two emptied stacks erased, one partial remains";
}

TEST_F(InventoryStackTest, RemoveLeavesTheFrontStackAsThePartial) {
	// Locks the back-to-front drain order the summed assertions above can't see: from
	// stacks (40, 40, 10), removing 55 drains 10 + 40 + 5, erasing the two rear stacks and
	// shaving the front one to 35. The single survivor is slot 0 at 35 — proof the drain ran
	// strictly from the back and didn't re-pack the remainder into a fresh slot.
	Inventory inv;
	inv.addItem("Wood", 90); // stacks: 40, 40, 10
	ASSERT_EQ(inv.items.size(), 3U);

	inv.removeItem("Wood", 55);

	ASSERT_EQ(inv.items.size(), 1U) << "two rear stacks erased, the front one survives";
	EXPECT_EQ(inv.items[0].quantity, 35U) << "front stack shaved last, drained strictly from the back";
}

TEST_F(InventoryStackTest, RemoveErasesEmptiedSlotsExactly) {
	Inventory inv;
	inv.addItem("Wood", 80); // two full stacks
	ASSERT_EQ(inv.getSlotCount(), 2U);

	EXPECT_EQ(inv.removeItem("Wood", 80), 80U);
	EXPECT_TRUE(inv.isEmpty()) << "draining everything leaves no empty slots behind";
	EXPECT_EQ(inv.getSlotCount(), 0U);
}

TEST_F(InventoryStackTest, AddableCountSumsStackHeadroomPlusFreeSlots) {
	// 4 slots. One Wood stack at 30 (10 headroom). 3 free slots * 40 = 120. Total 130.
	Inventory inv;
	inv.maxCapacity = 4;
	inv.addItem("Wood", 30);

	EXPECT_EQ(inv.addableCount("Wood"), 10U + 3U * 40U);
}

TEST_F(InventoryStackTest, AddableCountIsZeroWhenStacksFullAndNoFreeSlot) {
	Inventory inv;
	inv.maxCapacity = 1;
	inv.addItem("Wood", 40); // single slot at the cap, no headroom, no free slot

	EXPECT_EQ(inv.addableCount("Wood"), 0U);
	EXPECT_FALSE(inv.canAdd("Wood", 1));
}

TEST_F(InventoryStackTest, AddableCountBoundsActualAdd) {
	// Clamping the requested amount by addableCount guarantees addItem takes all of it,
	// so a withdrawal from a finite pool is conserved.
	Inventory inv;
	inv.maxCapacity = 2;
	inv.addItem("Wood", 75); // stacks: 40, 35 -> headroom 5, no free slot
	const uint32_t want = std::min(20U, inv.addableCount("Wood")); // 5
	EXPECT_EQ(want, 5U);
	EXPECT_EQ(inv.addItem("Wood", want), want); // nothing lost
}

TEST_F(InventoryStackTest, MaterialsCompeteForAFiniteSlotPool) {
	// 4 slots shared by two materials. Wood (cap 40) claims 3 for 90 units; Stone (cap 10)
	// can only land what fits in the one remaining slot, and a full pool refuses the rest.
	Inventory inv;
	inv.maxCapacity = 4;
	ASSERT_EQ(inv.addItem("Wood", 90), 90U); // 40, 40, 10 -> 3 slots
	ASSERT_EQ(inv.getSlotCount(), 3U);

	EXPECT_EQ(inv.addItem("Stone", 25), 10U) << "one Stone stack fills the last slot, the rest spills";
	EXPECT_EQ(inv.getQuantity("Stone"), 10U);
	EXPECT_EQ(inv.getSlotCount(), 4U);
	EXPECT_FALSE(inv.hasSpace());

	EXPECT_EQ(inv.addItem("Stone", 5), 0U) << "no slot left, even for the same material";
}

TEST_F(InventoryStackTest, GetAllItemsAggregatesStacksPerType) {
	// Display must show one row per material with the summed quantity, not one per slot.
	Inventory inv;
	inv.addItem("Wood", 90);  // 3 stacks
	inv.addItem("Stone", 15); // 2 stacks

	auto display = inv.getAllItems();
	ASSERT_EQ(display.size(), 2U) << "two materials, two display rows (not five stacks)";

	uint32_t wood = 0;
	uint32_t stone = 0;
	for (const auto& row : display) {
		if (row.defName == "Wood") {
			wood = row.quantity;
		}
		if (row.defName == "Stone") {
			stone = row.quantity;
		}
	}
	EXPECT_EQ(wood, 90U);
	EXPECT_EQ(stone, 15U);
}

TEST_F(InventoryStackTest, ManifestSlotSizingCoversMultiStackMaterials) {
	// sum_i ceil(required_i / stackSize_i) + headroom: 200 Wood (5) + 25 Stone (3) + 2.
	const std::vector<std::pair<std::string, uint32_t>> manifest = {{"Wood", 200}, {"Stone", 25}};
	const uint32_t slots = Inventory::slotsForManifest(manifest);
	EXPECT_EQ(slots, 5U + 3U + Inventory::kManifestSlotHeadroom);

	// A delivery inventory sized this way holds the whole manifest with room to spare.
	Inventory inv;
	inv.maxCapacity = slots;
	EXPECT_EQ(inv.addItem("Wood", 200), 200U);
	EXPECT_EQ(inv.addItem("Stone", 25), 25U);
	EXPECT_EQ(inv.getQuantity("Wood"), 200U);
	EXPECT_EQ(inv.getQuantity("Stone"), 25U);
}

TEST_F(InventoryStackTest, UnregisteredMaterialStaysUnbounded) {
	// A defName with no asset def is not stack-limited: one slot, arbitrary quantity.
	Inventory inv;
	EXPECT_EQ(inv.addItem("Mystery", 5000), 5000U);
	EXPECT_EQ(inv.getSlotCount(), 1U);
	EXPECT_EQ(inv.getQuantity("Mystery"), 5000U);
}

// ============================================================================
// Belt: two quick-draw slots for one-hand tools. Belt ops are pure inventory
// (no asset registry); the caller guarantees the item is one-hand.
// ============================================================================

TEST(InventoryTests, BeltEmptyByDefault) {
	Inventory inv;
	EXPECT_TRUE(inv.beltHasFreeSlot());
	EXPECT_FALSE(inv.belt[0].has_value());
	EXPECT_FALSE(inv.belt[1].has_value());
}

TEST(InventoryTests, BeltStowSeatsQuantityOne) {
	Inventory inv;
	EXPECT_TRUE(inv.stowToBelt("Axe"));
	EXPECT_TRUE(inv.belt[0].has_value());
	EXPECT_EQ(inv.belt[0]->defName, "Axe");
	EXPECT_EQ(inv.belt[0]->quantity, 1U);
}

TEST(InventoryTests, BeltFillsBothSlotsThenRejectsThird) {
	Inventory inv;
	EXPECT_TRUE(inv.stowToBelt("Axe"));
	EXPECT_TRUE(inv.stowToBelt("Hammer"));
	EXPECT_FALSE(inv.beltHasFreeSlot());
	EXPECT_FALSE(inv.stowToBelt("Knife")); // no free slot left
}

TEST(InventoryTests, BeltTakeRoundTrips) {
	Inventory inv;
	inv.stowToBelt("Axe");
	EXPECT_TRUE(inv.takeFromBelt("Axe"));
	EXPECT_TRUE(inv.beltHasFreeSlot());
	EXPECT_FALSE(inv.belt[0].has_value());
	EXPECT_FALSE(inv.belt[1].has_value());
}

TEST(InventoryTests, BeltTakeMissingItemReturnsFalse) {
	Inventory inv;
	inv.stowToBelt("Axe");
	EXPECT_FALSE(inv.takeFromBelt("Hammer")); // not on the belt
	EXPECT_TRUE(inv.belt[0].has_value());	   // Axe untouched
}

TEST(InventoryTests, BeltTakeRemovesFirstMatch) {
	Inventory inv;
	inv.stowToBelt("Axe");
	inv.stowToBelt("Axe");
	EXPECT_TRUE(inv.takeFromBelt("Axe"));
	EXPECT_FALSE(inv.belt[0].has_value()); // first slot cleared
	EXPECT_TRUE(inv.belt[1].has_value());  // second still held
}

TEST(InventoryTests, BeltCountsTowardCompletelyEmpty) {
	Inventory inv;
	EXPECT_TRUE(inv.isCompletelyEmpty());
	inv.stowToBelt("Axe");
	EXPECT_FALSE(inv.isCompletelyEmpty()); // belted item is not empty
	inv.takeFromBelt("Axe");
	EXPECT_TRUE(inv.isCompletelyEmpty());
}

TEST(InventoryTests, ClearAllEmptiesBelt) {
	Inventory inv;
	inv.stowToBelt("Axe");
	inv.stowToBelt("Hammer");
	inv.clearAll();
	EXPECT_FALSE(inv.belt[0].has_value());
	EXPECT_FALSE(inv.belt[1].has_value());
	EXPECT_TRUE(inv.beltHasFreeSlot());
	EXPECT_TRUE(inv.isCompletelyEmpty());
}
