#pragma once

// TasksTabView - Tab showing tasks known by a specific colonist
//
// Displays a scrollable list of tasks that this colonist knows about.
// Unlike the global task list, this shows tasks from the colonist's perspective
// (distance from colonist, "In Progress" for their own tasks).
//
// Reuses GlobalTaskRow component (with showKnownBy=false) for consistent layout.

#include "scenes/game/ui/adapters/GlobalTaskAdapter.h"

#include <component/Container.h>
#include <graphics/Rect.h>
#include <layer/Layer.h>

#include <vector>

namespace world_sim {

/// Data for Tasks tab - uses adapter's GlobalTaskDisplayData for consistency
struct TasksTabData {
	std::vector<adapters::GlobalTaskDisplayData> tasks;
	size_t totalCount = 0;
};

/// Tasks tab content for ColonistDetailsDialog
/// Shows: Scrollable list of tasks this colonist knows about
class TasksTabView : public UI::Container {
  public:
	/// Create the tab view with content bounds from parent dialog
	void create(const Foundation::Rect& contentBounds);

	/// Update content from model data
	void update(const TasksTabData& data);

  private:
	float tabWidth = 0.0F;
	UI::LayerHandle layoutHandle;
	UI::LayerHandle headerTextHandle;       // "Known Tasks: X"
	UI::LayerHandle scrollContainerHandle;  // ScrollContainer
	UI::LayerHandle taskLayoutHandle;       // LayoutContainer inside scroll

	// Cached task row handles for efficient updates
	std::vector<UI::LayerHandle> taskRowHandles;

	/// Rebuild task rows when count changes
	void rebuildTaskRows(const TasksTabData& data);

	/// Update existing task rows when data changes
	void updateTaskRows(const TasksTabData& data);
};

} // namespace world_sim
