#pragma once

// ToastStack - Container for managing toast notifications
//
// Manages a stack of Toast notifications, handling positioning,
// animation timing, and automatic removal of dismissed toasts.
//
// Features:
// - Stacks toasts vertically from an anchor position
// - Handles toast lifecycle (add, dismiss, remove)
// - Configurable anchor (bottom-right by default)
// - Maximum toast limit with oldest removal
// - Smooth repositioning when toasts are dismissed

#include "Toast.h"
#include "component/Component.h"

#include <memory>
#include <vector>

namespace UI {

/// Anchor position for the toast stack
enum class ToastAnchor {
	TopRight,
	TopLeft,
	BottomRight,
	BottomLeft
};

class ToastStack : public Component {
  public:
	struct Args {
		Foundation::Vec2 position{0.0F, 0.0F}; // Screen position for anchor point
		ToastAnchor		 anchor{ToastAnchor::BottomRight};
		float			 spacing{8.0F};	   // Space between toasts
		size_t			 maxToasts{5};	   // Maximum visible toasts
		float			 toastWidth{Theme::Toast::defaultWidth};
		const char*		 id = nullptr;
	};

	explicit ToastStack(const Args& args);
	~ToastStack() override = default;

	// Disable copy
	ToastStack(const ToastStack&) = delete;
	ToastStack& operator=(const ToastStack&) = delete;

	// Allow move
	ToastStack(ToastStack&&) noexcept = default;
	ToastStack& operator=(ToastStack&&) noexcept = default;

	// Add a new toast notification
	void addToast(const std::string& title, const std::string& message,
				  ToastSeverity severity = ToastSeverity::Info,
				  float autoDismissTime = Theme::Toast::defaultAutoDismiss);

	// Add a toast with click-to-navigate callback
	void addToast(const std::string& title, const std::string& message,
				  ToastSeverity severity, float autoDismissTime,
				  std::function<void()> onClick);

	// Add a toast with full configuration
	void addToast(Toast::Args args);

	// Dismiss all toasts
	void dismissAll();

	// Get number of active toasts (including those being dismissed)
	[[nodiscard]] size_t getToastCount() const { return toasts.size(); }

	// Get number of visible toasts (not finished)
	[[nodiscard]] size_t getVisibleToastCount() const;

	// Accessors
	[[nodiscard]] ToastAnchor getAnchor() const { return anchor; }
	[[nodiscard]] float		  getSpacing() const { return spacing; }
	[[nodiscard]] size_t	  getMaxToasts() const { return maxToasts; }

	// IComponent overrides
	void render() override;
	bool handleEvent(InputEvent& event) override;
	bool containsPoint(Foundation::Vec2 point) const override;
	void setPosition(float x, float y) override;

	// ILayer overrides
	void update(float deltaTime) override;

  private:
	ToastAnchor					   anchor;
	float						   spacing;
	size_t						   maxToasts;
	float						   toastWidth;
	std::vector<std::unique_ptr<Toast>> toasts;

	// Recalculate toast positions based on anchor and current toasts
	void repositionToasts();

	// Remove finished toasts from the list
	void removeFinishedToasts();
};

} // namespace UI
