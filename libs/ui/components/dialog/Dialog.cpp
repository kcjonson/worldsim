#include "Dialog.h"

#include "primitives/Primitives.h"

#include <algorithm>

namespace UI {

	// Constants for close button X text centering (approximate glyph dimensions)
	constexpr float kCloseButtonTextWidth = 10.0F;
	constexpr float kCloseButtonTextHeight = 14.0F;
	constexpr float kSeparatorOpacityMultiplier = 0.5F;

	Dialog::Dialog(const Args& args)
		: FocusableBase<Dialog>(args.tabIndex),
		  title(args.title),
		  dialogSize(args.size),
		  onClose(args.onClose),
		  modal(args.modal) {
		// Dialog covers entire screen when open
		position = {0.0F, 0.0F};
		size = {0.0F, 0.0F}; // Set properly when open() is called
	}

	Dialog::~Dialog() {
		// Ensure focus scope is popped if we're destroyed while open
		if (state == State::Open || state == State::Closing) {
			performCleanup();
		}
	}

	void Dialog::performCleanup() {
		// Prevent double cleanup (destructor + close animation completion)
		if (cleanupPerformed) {
			return;
		}
		cleanupPerformed = true;

		if (!contentFocusables.empty()) {
			FocusManager::Get().popFocusScope();
		}
		if (onClose) {
			onClose();
		}
	}

	void Dialog::open(float width, float height) {
		if (state != State::Closed) {
			return; // Already open or animating
		}

		screenWidth = width;
		screenHeight = height;
		size = {width, height}; // Dialog covers entire screen

		state = State::Opening;
		stateTimer = 0.0F;
		opacity = 0.0F;
		visible = true;
		cleanupPerformed = false; // Reset for new open/close cycle

		// Set up content area clipping and offset
		updateContentArea();

		// Take focus so we receive keyboard input (Escape to close)
		FocusManager::Get().setFocus(this);

		// Push focus scope for dialog content
		// Note: Content focusables should be registered before opening
		if (!contentFocusables.empty()) {
			FocusManager::Get().pushFocusScope(contentFocusables);
		}
	}

	void Dialog::close() {
		if (state == State::Closed || state == State::Closing) {
			return; // Already closed or closing
		}

		state = State::Closing;
		stateTimer = 0.0F;
	}

	Foundation::Vec2 Dialog::getPanelPosition() const {
		// Center the dialog panel on screen
		float x = (screenWidth - dialogSize.x) / 2.0F;
		float y = (screenHeight - dialogSize.y) / 2.0F;
		return {x, y};
	}

	Foundation::Rect Dialog::getPanelBounds() const {
		Foundation::Vec2 pos = getPanelPosition();
		return {pos.x, pos.y, dialogSize.x, dialogSize.y};
	}

	Foundation::Rect Dialog::getTitleBarBounds() const {
		Foundation::Vec2 pos = getPanelPosition();
		return {pos.x, pos.y, dialogSize.x, Theme::Dialog::titleBarHeight};
	}

	Foundation::Rect Dialog::getCloseButtonBounds() const {
		Foundation::Vec2 pos = getPanelPosition();
		float			 buttonX = pos.x + dialogSize.x - kCloseButtonMargin - kCloseButtonSize;
		float			 buttonY = pos.y + (Theme::Dialog::titleBarHeight - kCloseButtonSize) / 2.0F;
		return {buttonX, buttonY, kCloseButtonSize, kCloseButtonSize};
	}

	Foundation::Rect Dialog::getContentBounds() const {
		Foundation::Vec2 pos = getPanelPosition();
		float			 contentY = pos.y + Theme::Dialog::titleBarHeight;
		float			 contentHeight = dialogSize.y - Theme::Dialog::titleBarHeight;
		return {
			pos.x + Theme::Dialog::contentPadding,
			contentY + Theme::Dialog::contentPadding,
			dialogSize.x - Theme::Dialog::contentPadding * 2,
			contentHeight - Theme::Dialog::contentPadding * 2
		};
	}

	bool Dialog::isPointInPanel(Foundation::Vec2 point) const {
		Foundation::Rect bounds = getPanelBounds();
		return point.x >= bounds.x && point.x < bounds.x + bounds.width && point.y >= bounds.y && point.y < bounds.y + bounds.height;
	}

	bool Dialog::isPointInCloseButton(Foundation::Vec2 point) const {
		Foundation::Rect bounds = getCloseButtonBounds();
		return point.x >= bounds.x && point.x < bounds.x + bounds.width && point.y >= bounds.y && point.y < bounds.y + bounds.height;
	}

	bool Dialog::isPointInTitleBar(Foundation::Vec2 point) const {
		Foundation::Rect bounds = getTitleBarBounds();
		return point.x >= bounds.x && point.x < bounds.x + bounds.width && point.y >= bounds.y && point.y < bounds.y + bounds.height;
	}

	void Dialog::setPosition(float x, float y) {
		// Dialog always covers the screen, position is ignored
		position = {0.0F, 0.0F};
	}

	bool Dialog::containsPoint(Foundation::Vec2 point) const {
		if (state == State::Closed) {
			return false;
		}
		// Modal dialogs contain the entire screen (to block clicks)
		if (modal) {
			return point.x >= 0 && point.x < screenWidth && point.y >= 0 && point.y < screenHeight;
		}
		// Non-modal only contains the panel
		return isPointInPanel(point);
	}

	bool Dialog::handleEvent(InputEvent& event) {
		if (state == State::Closed) {
			return false;
		}

		// Dispatch to content children FIRST (they get priority over chrome)
		// Use Container::handleEvent to properly transform coordinates for content offset
		if (Container::handleEvent(event)) {
			return true;
		}

		// If event wasn't consumed by children, handle chrome
		switch (event.type) {
			case InputEvent::Type::MouseMove: {
				closeButtonHovered = isPointInCloseButton(event.position);
				// In modal mode, block all hover events; in non-modal, only block if in panel
				if (modal) {
					return true;
				}
				return isPointInPanel(event.position);
			}

			case InputEvent::Type::MouseDown: {
				// Check close button first
				if (isPointInCloseButton(event.position)) {
					event.consume();
					return true;
				}

				// Check if click is outside panel
				if (!isPointInPanel(event.position)) {
					// Close dialog when clicking outside (both modal and non-modal)
					close();
					if (modal) {
						// Modal: consume the event (block game interaction)
						event.consume();
						return true;
					}
					// Non-modal: let click pass through to game
					return false;
				}

				// Click is inside panel - in non-modal mode, don't consume
				// so child components can handle the event
				if (!modal) {
					return false;
				}
				event.consume();
				return true;
			}

			case InputEvent::Type::MouseUp: {
				if (isPointInCloseButton(event.position)) {
					close();
					event.consume();
					return true;
				}

				// In modal mode, consume all mouse up events
				if (modal) {
					event.consume();
					return true;
				}
				// Non-modal: don't consume inside panel
				return false;
			}

			default:
				break;
		}

		// In modal mode, consume all events to block interaction
		if (modal) {
			event.consume();
			return true;
		}
		return false;
	}

	void Dialog::update(float deltaTime) {
		if (state == State::Closed) {
			return;
		}

		stateTimer += deltaTime;

		switch (state) {
			case State::Opening:
				opacity = std::min(1.0F, stateTimer / kFadeInDuration);
				if (stateTimer >= kFadeInDuration) {
					state = State::Open;
					stateTimer = 0.0F;
					opacity = 1.0F;
				}
				break;

			case State::Open:
				opacity = 1.0F;
				break;

			case State::Closing:
				opacity = std::max(0.0F, 1.0F - stateTimer / kFadeOutDuration);
				if (stateTimer >= kFadeOutDuration) {
					state = State::Closed;
					opacity = 0.0F;
					visible = false;
					performCleanup();
				}
				break;

			case State::Closed:
				break;
		}
	}

	void Dialog::render() {
		if (state == State::Closed || opacity <= 0.0F) {
			return;
		}

		// Draw semi-transparent overlay covering entire screen (only in modal mode)
		if (modal) {
			Foundation::Color overlayColor = Theme::Dialog::overlayBackground;
			overlayColor.a *= opacity;

			Renderer::Primitives::drawRect(
				Renderer::Primitives::RectArgs{
					.bounds = {0.0F, 0.0F, screenWidth, screenHeight},
					.style = {.fill = overlayColor},
				}
			);
		}

		// Draw dialog panel
		Foundation::Rect  panelBounds = getPanelBounds();
		Foundation::Color panelBg = Theme::Dialog::panelBackground;
		panelBg.a *= opacity;
		Foundation::Color panelBorder = Theme::Dialog::panelBorder;
		panelBorder.a *= opacity;

		Renderer::Primitives::drawRect(
			Renderer::Primitives::RectArgs{
				.bounds = panelBounds,
				.style = {.fill = panelBg, .border = Foundation::BorderStyle{.color = panelBorder, .width = 1.0F}},
			}
		);

		// Draw title bar background
		Foundation::Rect  titleBarBounds = getTitleBarBounds();
		Foundation::Color titleBg = Theme::Dialog::titleBackground;
		titleBg.a *= opacity;

		Renderer::Primitives::drawRect(
			Renderer::Primitives::RectArgs{
				.bounds = titleBarBounds,
				.style = {.fill = titleBg},
			}
		);

		// Draw title text
		Foundation::Color titleColor = Theme::Colors::textTitle;
		titleColor.a *= opacity;

		Renderer::Primitives::drawText(
			Renderer::Primitives::TextArgs{
				.text = title,
				.position =
					{panelBounds.x + Theme::Dialog::contentPadding,
					 panelBounds.y + (Theme::Dialog::titleBarHeight - Theme::Typography::titleSize) / 2.0F},
				.scale = Theme::Typography::titleSize / 16.0F,
				.color = titleColor,
			}
		);

		// Draw close button
		Foundation::Rect closeBounds = getCloseButtonBounds();

		// Close button background (only when hovered)
		if (closeButtonHovered) {
			Foundation::Color closeBg = Theme::Colors::closeButtonBackground;
			closeBg.a *= opacity;

			Renderer::Primitives::drawRect(
				Renderer::Primitives::RectArgs{
					.bounds = closeBounds,
					.style = {.fill = closeBg},
				}
			);
		}

		// Draw X text
		Foundation::Color xColor = closeButtonHovered ? Theme::Colors::closeButtonText : Theme::Colors::textSecondary;
		xColor.a *= opacity;

		float xTextX = closeBounds.x + (kCloseButtonSize - kCloseButtonTextWidth) / 2.0F;
		float xTextY = closeBounds.y + (kCloseButtonSize - kCloseButtonTextHeight) / 2.0F;

		Renderer::Primitives::drawText(
			Renderer::Primitives::TextArgs{
				.text = "X",
				.position = {xTextX, xTextY},
				.scale = kCloseButtonTextHeight / 16.0F,
				.color = xColor,
			}
		);

		// Draw separator line below title bar
		Foundation::Color lineColor = panelBorder;
		lineColor.a *= opacity * kSeparatorOpacityMultiplier;

		Renderer::Primitives::drawRect(
			Renderer::Primitives::RectArgs{
				.bounds = {panelBounds.x, titleBarBounds.y + titleBarBounds.height, panelBounds.width, 1.0F},
				.style = {.fill = lineColor},
			}
		);

		// Render content children (Container handles clipping and offset)
		Container::render();
	}

	// IFocusable implementation

	void Dialog::onFocusGained() {
		// Dialog doesn't need to track focus state visually
	}

	void Dialog::onFocusLost() {
		// Dialog doesn't close on focus lost (unlike dropdown)
		// User must explicitly close via [X], Escape, or click outside
	}

	void Dialog::handleKeyInput(engine::Key key, bool /*shift*/, bool /*ctrl*/, bool /*alt*/) {
		if (key == engine::Key::Escape && state == State::Open) {
			close();
		}
	}

	void Dialog::handleCharInput(char32_t /*codepoint*/) {
		// Dialog doesn't handle character input directly
	}

	bool Dialog::canReceiveFocus() const {
		return state == State::Open;
	}

	void Dialog::updateContentArea() {
		auto bounds = getContentBounds();

		// Clip children to content area
		setClip(Foundation::ClipSettings{
			.shape = Foundation::ClipRect{.bounds = bounds}
		});

		// Offset children so (0,0) is top-left of content area
		setContentOffset({bounds.x, bounds.y});
	}

} // namespace UI
