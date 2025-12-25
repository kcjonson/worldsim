#pragma once

// TaskListView - Expanded task queue display
//
// Shows the full decision trace for a colonist:
// - Current task with details
// - Up Next: prioritized tasks from DecisionTrace
// - Recent Tasks: completed task history (future)
//
// Appears above EntityInfoView when user clicks "Tasks: Show"

#include <component/Component.h>
#include <ecs/EntityID.h>
#include <ecs/World.h>
#include <graphics/Color.h>
#include <input/InputEvent.h>
#include <layer/Layer.h>
#include <shapes/Shapes.h>

#include <functional>
#include <string>
#include <vector>

namespace world_sim {

/// Expanded panel showing full task queue and decision trace
class TaskListView : public UI::Component {
  public:
	struct Args {
		float				  width = 360.0F;	 // 2x info panel width
		float				  maxHeight = 400.0F; // Maximum height before scrolling
		std::function<void()> onClose;			 // Called when close button clicked
		std::string			  id = "task_list";
	};

	explicit TaskListView(const Args& args);

	/// Update panel with colonist's decision trace
	/// @param world ECS world for component queries
	/// @param colonistId The colonist to display tasks for
	void update(const ecs::World& world, ecs::EntityID colonistId);

	/// Set panel position (bottom-left alignment, appears above info panel)
	/// @param x Left edge X coordinate
	/// @param bottomY Bottom edge Y coordinate (top of info panel)
	void setPosition(float x, float bottomY);

	/// Get current panel height (for layout calculations)
	[[nodiscard]] float getHeight() const { return m_panelHeight; }

	/// Get current panel width
	[[nodiscard]] float getWidth() const { return m_panelWidth; }

	/// Handle input event, returns true if consumed
	bool handleEvent(UI::InputEvent& event) override;

  private:
	/// Rebuild panel content from decision trace
	void renderContent(const ecs::World& world, ecs::EntityID colonistId);

	/// Hide all text elements
	void hideContent();

	/// Format a task option for display
	[[nodiscard]] std::string formatOption(const struct EvaluatedOption& option) const;

	// Close button callback
	std::function<void()> m_onClose;

	// Panel dimensions
	float m_panelWidth;
	float m_maxHeight;
	float m_panelHeight = 200.0F;

	// Position (X = left edge, Y = bottom edge)
	float m_panelX = 0.0F;
	float m_panelY = 0.0F;

	// UI elements
	UI::LayerHandle m_backgroundHandle;
	UI::LayerHandle m_closeButtonBgHandle;
	UI::LayerHandle m_closeButtonTextHandle;
	UI::LayerHandle m_titleHandle;

	// Section headers
	UI::LayerHandle m_currentTaskHeader;
	UI::LayerHandle m_upNextHeader;

	// Content text elements (pooled)
	static constexpr size_t kMaxTextLines = 12;
	std::vector<UI::LayerHandle> m_textHandles;
	size_t m_usedTextLines = 0;

	// Layout constants
	static constexpr float kPadding = 10.0F;
	static constexpr float kTitleFontSize = 14.0F;
	static constexpr float kHeaderFontSize = 12.0F;
	static constexpr float kTextFontSize = 11.0F;
	static constexpr float kLineSpacing = 3.0F;
	static constexpr float kSectionSpacing = 8.0F;
	static constexpr float kCloseButtonSize = 16.0F;
};

} // namespace world_sim
