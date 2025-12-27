#pragma once

// Dialog - Modal dialog with overlay
//
// A modal dialog that displays content over the game with a semi-transparent
// overlay. Blocks all interaction with content behind it.
//
// Features:
// - Full-screen semi-transparent overlay
// - Centered content panel with title bar and close button
// - Close via [X] button, Escape key, or clicking outside panel
// - Focus trapping (Tab stays within dialog content)
// - Fade in/out animation

#include "component/Component.h"
#include "focus/FocusableBase.h"
#include "focus/FocusManager.h"
#include "graphics/Color.h"
#include "theme/Theme.h"

#include <functional>
#include <string>
#include <vector>

namespace UI {

class Dialog : public Component, public FocusableBase<Dialog> {
  public:
	struct Args {
		std::string			  title;
		Foundation::Vec2	  size{Theme::Dialog::defaultWidth, Theme::Dialog::defaultHeight};
		std::function<void()> onClose;
		int					  tabIndex = -1;
	};

	explicit Dialog(const Args& args);
	~Dialog() override;

	// Disable copy
	Dialog(const Dialog&) = delete;
	Dialog& operator=(const Dialog&) = delete;

	// Allow move
	Dialog(Dialog&&) noexcept = default;
	Dialog& operator=(Dialog&&) noexcept = default;

	// Open/close control
	void open(float screenWidth, float screenHeight);
	void close();

	// Query state
	[[nodiscard]] bool isOpen() const { return state != State::Closed; }
	[[nodiscard]] bool isAnimating() const {
		return state == State::Opening || state == State::Closing;
	}
	[[nodiscard]] float getOpacity() const { return opacity; }

	// Title
	[[nodiscard]] const std::string& getTitle() const { return title; }
	void						setTitle(const std::string& newTitle) { title = newTitle; }

	// Dialog panel size (not including overlay)
	[[nodiscard]] const Foundation::Vec2& getDialogSize() const { return dialogSize; }

	// Get content area bounds (inside dialog, below title bar)
	[[nodiscard]] Foundation::Rect getContentBounds() const;

	// IComponent overrides
	void render() override;
	bool handleEvent(InputEvent& event) override;
	bool containsPoint(Foundation::Vec2 point) const override;
	void setPosition(float x, float y) override;

	// ILayer overrides
	void update(float deltaTime) override;

	// IFocusable overrides
	void onFocusGained() override;
	void onFocusLost() override;
	void handleKeyInput(engine::Key key, bool shift, bool ctrl, bool alt) override;
	void handleCharInput(char32_t codepoint) override;
	bool canReceiveFocus() const override;

  private:
	enum class State {
		Closed,	 // Not visible
		Opening, // Fading in
		Open,	 // Fully visible
		Closing	 // Fading out
	};

	std::string			  title;
	Foundation::Vec2	  dialogSize;
	std::function<void()> onClose;

	State				  state{State::Closed};
	float				  opacity{0.0F};
	float				  stateTimer{0.0F};
	bool				  closeButtonHovered{false};

	// Screen dimensions (set on open)
	float				  screenWidth{800.0F};
	float				  screenHeight{600.0F};

	// Focus scope for content
	std::vector<IFocusable*> contentFocusables;

	// Animation constants
	static constexpr float kFadeInDuration = 0.15F;
	static constexpr float kFadeOutDuration = 0.10F;

	// Layout constants
	static constexpr float kCloseButtonSize = 28.0F;
	static constexpr float kCloseButtonMargin = 6.0F;

	// Computed positions
	[[nodiscard]] Foundation::Vec2 getPanelPosition() const;
	[[nodiscard]] Foundation::Rect getPanelBounds() const;
	[[nodiscard]] Foundation::Rect getTitleBarBounds() const;
	[[nodiscard]] Foundation::Rect getCloseButtonBounds() const;

	// Hit testing
	[[nodiscard]] bool isPointInPanel(Foundation::Vec2 point) const;
	[[nodiscard]] bool isPointInCloseButton(Foundation::Vec2 point) const;
	[[nodiscard]] bool isPointInTitleBar(Foundation::Vec2 point) const;

	// Cleanup helpers
	void performCleanup();
};

} // namespace UI
