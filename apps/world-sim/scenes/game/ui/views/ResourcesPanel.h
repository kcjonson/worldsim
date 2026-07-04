#pragma once

// ResourcesPanel - Collapsible panel showing colony storage resources.
//
// Design (from main-game-ui-design.md Section 4):
// - Collapsed: [Storage ▼] link
// - Expanded: one row per resource (display name left, count right in mono),
//   summed across all storage containers, scrollable past the height cap
// - Empty state: "No stockpiles built" message (no storage containers exist)
//
// Position: Top-right, below where minimap will be.

#include "scenes/game/ui/adapters/ResourcesAdapter.h"

#include <component/Component.h>
#include <components/button/Button.h>
#include <components/icon/Icon.h>
#include <components/scroll/ScrollContainer.h>
#include <graphics/Rect.h>
#include <input/InputEvent.h>
#include <layer/Layer.h>
#include <shapes/Shapes.h>

#include <functional>
#include <vector>

namespace world_sim {

/// Resources panel with collapsed/expanded states.
class ResourcesPanel : public UI::Component {
  public:
	struct Args {
		float width = 180.0F;
		const char* id = "resources_panel";
		std::function<void()> onToggle; ///< Fired after expand/collapse (panel height changed)
	};

	explicit ResourcesPanel(const Args& args);

	/// Set position (anchor point is top-right of collapsed button)
	void setAnchorPosition(float x, float y);

	/// Replace the displayed resource rows (called when model data changes)
	void setResources(const std::vector<adapters::ResourceRowData>& rows, size_t containers);

	/// Handle input events
	bool handleEvent(UI::InputEvent& event) override;

	/// Update scroll container animation
	void update(float deltaTime) override;

	/// Check if panel is expanded
	[[nodiscard]] bool isExpanded() const { return expanded; }

	/// Get current bounds (for layout calculations)
	[[nodiscard]] Foundation::Rect getBounds() const;

	// render() inherited from Component - auto-renders children

  private:
	// Layout constants
	static constexpr float kCollapsedHeight = 28.0F;
	static constexpr float kHeaderHeight = 28.0F;
	static constexpr float kEmptyExpandedHeight = 120.0F;
	static constexpr float kMaxExpandedHeight = 240.0F;
	static constexpr float kRowHeight = 24.0F;

	float panelWidth;
	bool expanded = false;
	size_t rowCount = 0;
	size_t containerCount = 0;
	std::function<void()> onToggle;

	// Child handles
	UI::LayerHandle headerButtonHandle;
	UI::LayerHandle chevronHandle;
	UI::LayerHandle contentBackgroundHandle;
	UI::LayerHandle emptyMessageHandle;
	UI::LayerHandle scrollContainerHandle;
	UI::LayerHandle layoutHandle;

	/// Toggle expanded state
	void toggle();

	/// Panel height when expanded (grows with rows, capped at kMaxExpandedHeight)
	[[nodiscard]] float expandedHeight() const;

	/// Update chevron icon direction
	void updateChevron();

	/// Update child positions and visibility after state or data change
	void updateLayout();
};

}  // namespace world_sim
