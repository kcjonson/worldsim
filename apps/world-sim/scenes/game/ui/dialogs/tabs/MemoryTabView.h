#pragma once

#include "../ColonistDetailsModel.h"

#include <component/Component.h>
#include <graphics/Rect.h>
#include <layer/Layer.h>

namespace world_sim {

/// Memory tab content for ColonistDetailsDialog
/// Shows: Known entities grouped by category in a scrollable TreeView
class MemoryTabView : public UI::Component {
  public:
	/// Create the tab view with content bounds from parent dialog
	void create(const Foundation::Rect& contentBounds);

	/// Update content from model data
	void update(const MemoryData& data);

  private:
	UI::LayerHandle layoutHandle;
};

} // namespace world_sim
