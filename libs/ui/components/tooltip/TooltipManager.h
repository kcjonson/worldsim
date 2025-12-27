#pragma once

// TooltipManager - Singleton for tooltip display coordination
//
// Manages tooltip display timing and positioning. Only one tooltip
// can be visible at a time. Components report hover state, and the
// manager handles the delay and display.
//
// Usage:
//   // In Application.cpp update loop:
//   TooltipManager::Get().update(deltaTime);
//   TooltipManager::Get().render();
//
//   // In component handleEvent:
//   if (hovered && !wasHovered) {
//       TooltipManager::Get().startHover(content, cursorPos);
//   } else if (!hovered && wasHovered) {
//       TooltipManager::Get().endHover();
//   }

#include "Tooltip.h"
#include "theme/Theme.h"

#include <memory>

namespace UI {

/// Singleton manager for tooltip display coordination
class TooltipManager {
  public:
	// Singleton access
	static TooltipManager& Get();
	static void			   setInstance(TooltipManager* instance);

	TooltipManager() = default;
	~TooltipManager() = default;

	// Disable copy/move
	TooltipManager(const TooltipManager&) = delete;
	TooltipManager& operator=(const TooltipManager&) = delete;
	TooltipManager(TooltipManager&&) = delete;
	TooltipManager& operator=(TooltipManager&&) = delete;

	/// Called when mouse hovers over a component with tooltip
	void startHover(const TooltipContent& content, Foundation::Vec2 newCursor);

	/// Called when mouse leaves the component
	void endHover();

	/// Called when cursor moves while hovering
	void updateCursorPosition(Foundation::Vec2 newCursor);

	/// Set screen bounds for tooltip positioning
	void setScreenBounds(float width, float height);

	/// Update (call each frame to handle delay timer)
	void update(float deltaTime);

	/// Render the active tooltip (if any)
	void render();

	/// Check if a tooltip is currently visible
	[[nodiscard]] bool isTooltipVisible() const;

	/// Get current state (for testing)
	enum class State {
		Idle,	  // No hover
		Waiting,  // Hovering, waiting for delay
		Showing,  // Tooltip visible, fading in
		Visible,  // Fully visible
		Hiding	  // Fading out
	};
	[[nodiscard]] State getState() const { return state; }

  private:
	static TooltipManager* s_instance;

	State						   state{State::Idle};
	float						   stateTimer{0.0F};
	TooltipContent				   pendingContent;
	Foundation::Vec2			   cursorPosition;
	float						   screenWidth{800.0F};
	float						   screenHeight{600.0F};
	std::unique_ptr<Tooltip>	   activeTooltip;

	// Animation constants
	static constexpr float kFadeInDuration = 0.1F;
	static constexpr float kFadeOutDuration = 0.08F;

	/// Calculate tooltip position to stay on screen
	[[nodiscard]] Foundation::Vec2 calculateTooltipPosition(
		Foundation::Vec2 cursor, float tooltipWidth, float tooltipHeight) const;
};

} // namespace UI
