#pragma once

// Toast - Notification popup with severity styling
//
// A temporary notification popup that displays a title, message, and optional
// icon. Supports three severity levels (Info, Warning, Critical), each mapped to
// a Salvage status tone.
//
// Features:
// - Title and message text
// - Optional SVG icon
// - Auto-dismiss timer or manual dismiss button
// - Fade in/out animation
// - Severity-based styling

#include "component/Component.h"
#include "graphics/Color.h"
#include "theme/Tokens.h"
#include "theme/Variants.h"

#include <functional>
#include <string>

namespace UI {

/// Default toast geometry/timing (formerly Theme::Toast).
inline constexpr float kToastDefaultWidth = 300.0F;
inline constexpr float kToastDefaultAutoDismiss = 5.0F;

/// Severity levels for toast notifications
enum class ToastSeverity {
	Info,	 /// Informational messages (data tone)
	Warning, /// Warnings (warn tone)
	Critical /// Critical alerts (crit tone)
};

class Toast : public Component {
  public:
	struct Args {
		std::string				  title;
		std::string				  message;
		ToastSeverity			  severity{ToastSeverity::Info};
		float					  autoDismissTime{kToastDefaultAutoDismiss}; // 0 = persistent
		std::string				  iconPath;									 // Optional SVG icon
		std::function<void()>	  onDismiss;
		std::function<void()>	  onClick; // Called when toast body is clicked (for navigation)
		Foundation::Vec2		  position{0.0F, 0.0F};
		float					  width{kToastDefaultWidth};
		const char*				  id = nullptr;
		float					  margin{0.0F};
	};

	explicit Toast(const Args& args);
	~Toast() override = default;

	// Disable copy
	Toast(const Toast&) = delete;
	Toast& operator=(const Toast&) = delete;

	// Allow move
	Toast(Toast&&) noexcept = default;
	Toast& operator=(Toast&&) noexcept = default;

	// Dismiss the toast
	void dismiss();

	// Check if toast is finished (dismissed and animation complete)
	[[nodiscard]] bool isFinished() const { return state == State::Finished; }

	// Check if toast is being dismissed
	[[nodiscard]] bool isDismissing() const { return state == State::Dismissing; }

	// Get remaining time before auto-dismiss (0 if persistent or already dismissing)
	[[nodiscard]] float getRemainingTime() const;

	// Get current opacity (for testing animation)
	[[nodiscard]] float getOpacity() const { return opacity; }

	// Accessors
	[[nodiscard]] const std::string& getTitle() const { return title; }
	[[nodiscard]] const std::string& getMessage() const { return message; }
	[[nodiscard]] ToastSeverity		 getSeverity() const { return severity; }
	[[nodiscard]] bool				 isPersistent() const { return autoDismissTime <= 0.0F; }

	// IComponent overrides
	void render() override;
	bool handleEvent(InputEvent& event) override;
	bool containsPoint(Foundation::Vec2 point) const override;
	void setPosition(float x, float y) override;

	// ILayer overrides
	void update(float deltaTime) override;

  private:
	enum class State {
		Appearing,	// Fading in
		Visible,	// Fully visible
		Dismissing, // Fading out
		Finished	// Can be removed
	};

	std::string			   title;
	std::string			   message;
	ToastSeverity		   severity;
	float				   autoDismissTime;
	std::string			   iconPath;
	std::function<void()>  onDismiss;
	std::function<void()>  onClick;
	float				   toastWidth;

	State				   state{State::Appearing};
	float				   opacity{0.0F};
	float				   stateTimer{0.0F};
	bool				   dismissButtonHovered{false};

	// Animation constants
	static constexpr float kFadeInDuration = 0.2F;
	static constexpr float kFadeOutDuration = 0.3F;

	// Layout constants
	static constexpr float kPadding = 12.0F;
	static constexpr float kTitleFontSize = 14.0F;
	static constexpr float kMessageFontSize = 12.0F;
	static constexpr float kDismissButtonSize = 20.0F;
	static constexpr float kIconSize = 24.0F;
	static constexpr float kLineSpacing = 4.0F;

	// Calculate toast height based on content
	[[nodiscard]] float calculateHeight() const;

	// Resolve the severity accent color (status tone) used for the stripe, title,
	// icon, and glow.
	[[nodiscard]] Foundation::Color getSeverityColor() const;

	// Get bounds for dismiss button
	[[nodiscard]] Foundation::Rect getDismissButtonBounds() const;

	// Check if point is in dismiss button
	[[nodiscard]] bool isPointInDismissButton(Foundation::Vec2 point) const;
};

} // namespace UI
