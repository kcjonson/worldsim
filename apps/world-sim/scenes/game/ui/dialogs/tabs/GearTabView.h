#pragma once

#include <component/Component.h>
#include <ecs/components/Inventory.h>
#include <graphics/Rect.h>
#include <layer/Layer.h>

#include <vector>

namespace world_sim {

/// Data for Gear tab
struct GearData {
	std::vector<ecs::ItemStack> items;
	uint32_t slotCount = 0;
	uint32_t maxSlots = 0;
};

/// Gear tab content for ColonistDetailsDialog
/// Shows: Attire slots, inventory items
class GearTabView : public UI::Component {
  public:
	/// Create the tab view with content bounds from parent dialog
	void create(const Foundation::Rect& contentBounds);

	/// Update content from model data
	void update(const GearData& data);

  private:
	UI::LayerHandle layoutHandle;
};

} // namespace world_sim
