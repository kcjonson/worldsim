#pragma once

// ResourcesPanel - Collapsible panel showing colony storage resources.
//
// Design (from main-game-ui-design.md Section 4):
// - Collapsed: [Storage ▼] link
// - Expanded: TreeView of storage by category (when stockpiles exist)
// - Empty state: "No stockpiles built" message
//
// Position: Top-right, below where minimap will be.
//
// Current state: Empty state only - stockpiles not yet implemented.

#include <component/Component.h>
#include <components/button/Button.h>
#include <graphics/Rect.h>
#include <input/InputEvent.h>
#include <layer/Layer.h>
#include <shapes/Shapes.h>

#include <functional>

namespace world_sim {

/// Resources panel with collapsed/expanded states.
class ResourcesPanel : public UI::Component {
  public:
	struct Args {
		float width = 180.0F;
		const char* id = "resources_panel";
	};

	explicit ResourcesPanel(const Args& args);

	/// Set position (anchor point is top-right of collapsed button)
	void setAnchorPosition(float x, float y);

	/// Handle input events
	bool handleEvent(UI::InputEvent& event) override;

	/// Check if panel is expanded
	[[nodiscard]] bool isExpanded() const { return expanded; }

	/// Get current bounds (for layout calculations)
	[[nodiscard]] Foundation::Rect getBounds() const;

	/// Render the panel
	void render() override;

  private:
	// Layout constants
	static constexpr float kCollapsedHeight = 28.0F;
	static constexpr float kExpandedHeight = 120.0F;
	static constexpr float kPadding = 10.0F;
	static constexpr float kHeaderHeight = 28.0F;

	float panelWidth;
	bool expanded = false;
	Foundation::Vec2 anchorPosition{0.0F, 0.0F};

	// Child handles
	UI::LayerHandle headerButtonHandle;
	UI::LayerHandle contentBackgroundHandle;
	UI::LayerHandle emptyMessageHandle;

	/// Toggle expanded state
	void toggle();

	/// Update child positions after state change
	void updateLayout();
};

}  // namespace world_sim
