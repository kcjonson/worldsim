#pragma once

#include <component/Container.h>
#include <ecs/components/Inventory.h>
#include <graphics/Rect.h>
#include <layer/Layer.h>

#include <optional>
#include <vector>

namespace world_sim {

/// Data for Gear tab
struct GearData {
	// Hand items (what colonist is holding)
	std::optional<ecs::ItemStack> leftHand;
	std::optional<ecs::ItemStack> rightHand;

	// Backpack items
	std::vector<ecs::ItemStack> items;
	uint32_t					slotCount = 0;
	uint32_t					maxSlots = 0;
};

/// Gear tab content for ColonistDetailsDialog
/// Shows: Attire slots, inventory items
class GearTabView : public UI::Container {
  public:
	/// Create the tab view with content bounds from parent dialog
	void create(const Foundation::Rect& contentBounds);

	/// Update content from model data
	void update(const GearData& data);

  private:
	UI::LayerHandle layoutHandle;
	UI::LayerHandle handsTextHandle;		// "Left: X, Right: Y" or "(empty)"
	UI::LayerHandle inventoryHeaderHandle;	// "Inventory: X/Y slots"
	UI::LayerHandle itemsTextHandle;		// Items list or "Empty"
};

} // namespace world_sim
