#pragma once

// Tooltip - Hover information popup
//
// A tooltip that displays information when hovering over UI elements.
// Managed by TooltipManager, not directly instantiated by components.
//
// Features:
// - Title, description, and optional hotkey hint
// - Automatic positioning near cursor
// - Fade in/out animation
// - Never consumes events (passive display)

#include "component/Component.h"
#include "graphics/Color.h"
#include "theme/Theme.h"

#include <string>

namespace UI {

/// Content for a tooltip
struct TooltipContent {
	std::string title;		  // Primary text (bold)
	std::string description;  // Secondary text (optional)
	std::string hotkey;		  // Hotkey hint like "Ctrl+S" (optional)
};

/// Tooltip visual component
/// Created and managed by TooltipManager
class Tooltip : public Component {
  public:
	struct Args {
		TooltipContent	 content;
		Foundation::Vec2 position{0.0F, 0.0F};
		float			 maxWidth{Theme::Tooltip::maxWidth};
	};

	explicit Tooltip(const Args& args);
	~Tooltip() override = default;

	// Disable copy
	Tooltip(const Tooltip&) = delete;
	Tooltip& operator=(const Tooltip&) = delete;

	// Allow move
	Tooltip(Tooltip&&) noexcept = default;
	Tooltip& operator=(Tooltip&&) noexcept = default;

	// Content
	void setContent(const TooltipContent& content);
	[[nodiscard]] const TooltipContent& getContent() const { return content; }

	// Opacity (controlled by TooltipManager for fade animation)
	void				  setOpacity(float alpha) { opacity = alpha; }
	[[nodiscard]] float getOpacity() const { return opacity; }

	// Computed dimensions (for positioning by TooltipManager)
	[[nodiscard]] float getTooltipWidth() const;
	[[nodiscard]] float getTooltipHeight() const;

	// IComponent overrides
	void render() override;
	bool handleEvent(InputEvent& event) override { return false; } // Never consume
	bool containsPoint(Foundation::Vec2 point) const override;
	void setPosition(float x, float y) override;

	// ILayer overrides
	void update(float deltaTime) override {}

  private:
	TooltipContent content;
	float		   maxWidth;
	float		   opacity{1.0F};

	// Layout constants
	static constexpr float kTitleFontSize = 13.0F;
	static constexpr float kDescFontSize = 11.0F;
	static constexpr float kHotkeyFontSize = 10.0F;
	static constexpr float kLineSpacing = 4.0F;

	[[nodiscard]] float calculateHeight() const;
};

} // namespace UI
