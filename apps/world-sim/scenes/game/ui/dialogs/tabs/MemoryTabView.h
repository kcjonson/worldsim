#pragma once

#include <component/Container.h>
#include <graphics/Rect.h>
#include <layer/Layer.h>

#include <string>
#include <vector>

namespace world_sim {

/// A single known entity for Memory tab display
struct MemoryEntity {
	std::string name;     // e.g., "Berry Bush"
	float x = 0.0F;       // Position
	float y = 0.0F;
};

/// A category of known entities
struct MemoryCategory {
	std::string name;                    // e.g., "Food Sources"
	std::vector<MemoryEntity> entities;
	size_t count = 0;                    // Total count (may differ if truncated)
};

/// Data for Memory tab
struct MemoryData {
	std::vector<MemoryCategory> categories;
	size_t totalKnown = 0;
};

/// Memory tab content for ColonistDetailsDialog
/// Shows: Known entities grouped by category in a scrollable TreeView
class MemoryTabView : public UI::Container {
  public:
	/// Create the tab view with content bounds from parent dialog
	void create(const Foundation::Rect& contentBounds);

	/// Update content from model data
	void update(const MemoryData& data);

  private:
	UI::LayerHandle layoutHandle;
	UI::LayerHandle headerTextHandle;	   // "Known Entities: X total"
	UI::LayerHandle scrollContainerHandle; // ScrollContainer
	UI::LayerHandle treeViewHandle;		   // TreeView inside ScrollContainer
};

} // namespace world_sim
