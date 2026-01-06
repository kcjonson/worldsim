#pragma once

// GlobalTaskListView - Collapsible panel showing colony-wide task list
//
// Design:
// - Collapsed: "Tasks (N) ▼" button
// - Expanded: Scrollable list of task rows
// - Position: Top-right, below ResourcesPanel
//
// Task rows show:
// - Line 1: Description + position + distance
// - Line 2: Status (colored) + "Known by: X, Y"
//
// Display-only - no click-to-navigate functionality.
//
// Extends UI::Component to integrate with the component tree and
// properly route events (especially scroll wheel for Apple Mouse).

#include "scenes/game/ui/adapters/GlobalTaskAdapter.h"

#include <component/Component.h>
#include <components/button/Button.h>
#include <components/icon/Icon.h>
#include <components/scroll/ScrollContainer.h>
#include <graphics/Rect.h>
#include <layer/Layer.h>
#include <layout/LayoutContainer.h>
#include <shapes/Shapes.h>

#include <vector>

namespace world_sim {

/// Collapsible panel showing all tasks known to the colony
class GlobalTaskListView : public UI::Component {
  public:
	struct Args {
		float width = 300.0F;
	};

	explicit GlobalTaskListView(const Args& args);

	/// Set position (anchor point is top-right of collapsed button)
	void setAnchorPosition(float x, float y);

	/// Handle input events (uses Component::dispatchEvent for proper routing)
	bool handleEvent(UI::InputEvent& event) override;

	/// Update scroll container animation
	void update(float deltaTime) override;

	/// Check if panel is expanded
	[[nodiscard]] bool isExpanded() const { return expanded; }

	/// Get current bounds (for layout calculations)
	[[nodiscard]] Foundation::Rect getBounds() const;

	/// Update the displayed tasks (called when model data changes)
	void setTasks(const std::vector<adapters::GlobalTaskDisplayData>& tasks);

	/// Set task count for collapsed header (can be called before expand)
	void setTaskCount(size_t count);

	// render() inherited from Component - auto-renders children

  private:
	// Layout constants
	static constexpr float kCollapsedHeight = 28.0F;
	static constexpr float kExpandedMaxHeight = 320.0F;
	static constexpr float kPadding = 8.0F;
	static constexpr float kHeaderHeight = 28.0F;
	static constexpr float kRowHeight = 36.0F;

	float panelWidth;
	bool expanded = false;
	size_t cachedTaskCount = 0;

	// Child handles (managed by Component tree)
	UI::LayerHandle headerButtonHandle;
	UI::LayerHandle chevronHandle;
	UI::LayerHandle contentBackgroundHandle;
	UI::LayerHandle scrollContainerHandle;
	UI::LayerHandle layoutHandle;

	// Task row handles (inside layout)
	std::vector<UI::LayerHandle> taskRowHandles;

	/// Toggle expanded state
	void toggle();

	/// Update header button text with count
	void updateHeaderText();

	/// Update chevron icon direction (up when expanded, down when collapsed)
	void updateChevron();

	/// Update child positions after state change
	void updateLayout();

	/// Rebuild scroll content with new tasks
	void rebuildContent(const std::vector<adapters::GlobalTaskDisplayData>& tasks);
};

} // namespace world_sim
