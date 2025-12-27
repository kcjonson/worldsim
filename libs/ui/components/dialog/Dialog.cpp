#include "Dialog.h"

#include "primitives/Primitives.h"

namespace UI {

Dialog::Dialog(const Args& args)
	: FocusableBase<Dialog>(args.tabIndex),
	  title(args.title),
	  dialogSize(args.size),
	  onClose(args.onClose) {
	// Dialog covers entire screen when open
	position = {0.0F, 0.0F};
	size = {0.0F, 0.0F}; // Set properly when open() is called
}

Dialog::~Dialog() {
	// Ensure focus scope is popped if we're destroyed while open
	if (state == State::Open || state == State::Closing) {
		if (!contentFocusables.empty()) {
			FocusManager::Get().popFocusScope();
		}
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
	float			 buttonX =
		pos.x + dialogSize.x - kCloseButtonMargin - kCloseButtonSize;
	float buttonY = pos.y + (Theme::Dialog::titleBarHeight - kCloseButtonSize) / 2.0F;
	return {buttonX, buttonY, kCloseButtonSize, kCloseButtonSize};
}

Foundation::Rect Dialog::getContentBounds() const {
	Foundation::Vec2 pos = getPanelPosition();
	float			 contentY = pos.y + Theme::Dialog::titleBarHeight;
	float			 contentHeight = dialogSize.y - Theme::Dialog::titleBarHeight;
	return {pos.x + Theme::Dialog::contentPadding,
			contentY + Theme::Dialog::contentPadding,
			dialogSize.x - Theme::Dialog::contentPadding * 2,
			contentHeight - Theme::Dialog::contentPadding * 2};
}

bool Dialog::isPointInPanel(Foundation::Vec2 point) const {
	Foundation::Rect bounds = getPanelBounds();
	return point.x >= bounds.x && point.x < bounds.x + bounds.width &&
		   point.y >= bounds.y && point.y < bounds.y + bounds.height;
}

bool Dialog::isPointInCloseButton(Foundation::Vec2 point) const {
	Foundation::Rect bounds = getCloseButtonBounds();
	return point.x >= bounds.x && point.x < bounds.x + bounds.width &&
		   point.y >= bounds.y && point.y < bounds.y + bounds.height;
}

bool Dialog::isPointInTitleBar(Foundation::Vec2 point) const {
	Foundation::Rect bounds = getTitleBarBounds();
	return point.x >= bounds.x && point.x < bounds.x + bounds.width &&
		   point.y >= bounds.y && point.y < bounds.y + bounds.height;
}

void Dialog::setPosition(float x, float y) {
	// Dialog always covers the screen, position is ignored
	position = {0.0F, 0.0F};
}

bool Dialog::containsPoint(Foundation::Vec2 point) const {
	// When open, the dialog contains the entire screen (to block clicks)
	if (state != State::Closed) {
		return point.x >= 0 && point.x < screenWidth && point.y >= 0 &&
			   point.y < screenHeight;
	}
	return false;
}

bool Dialog::handleEvent(InputEvent& event) {
	if (state == State::Closed) {
		return false;
	}

	switch (event.type) {
		case InputEvent::Type::MouseMove: {
			closeButtonHovered = isPointInCloseButton(event.position);
			// Don't consume - allow hover tracking in content
			return false;
		}

		case InputEvent::Type::MouseDown: {
			// Check close button first
			if (isPointInCloseButton(event.position)) {
				event.consume();
				return true;
			}

			// Check if click is outside panel (close on overlay click)
			if (!isPointInPanel(event.position)) {
				close();
				event.consume();
				return true;
			}

			// Click is inside panel - consume to prevent click-through
			event.consume();
			return true;
		}

		case InputEvent::Type::MouseUp: {
			if (isPointInCloseButton(event.position)) {
				close();
				event.consume();
				return true;
			}

			// Consume all mouse up events when open
			event.consume();
			return true;
		}

		default:
			break;
	}

	// Consume all events to block interaction with content behind
	event.consume();
	return true;
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

				// Pop focus scope
				if (!contentFocusables.empty()) {
					FocusManager::Get().popFocusScope();
				}

				// Call onClose callback
				if (onClose) {
					onClose();
				}
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

	// Draw semi-transparent overlay covering entire screen
	Foundation::Color overlayColor = Theme::Dialog::overlayBackground;
	overlayColor.a *= opacity;

	Renderer::Primitives::drawRect(Renderer::Primitives::RectArgs{
		.bounds = {0.0F, 0.0F, screenWidth, screenHeight},
		.style = {.fill = overlayColor},
		.zIndex = zIndex,
	});

	// Draw dialog panel
	Foundation::Rect  panelBounds = getPanelBounds();
	Foundation::Color panelBg = Theme::Dialog::panelBackground;
	panelBg.a *= opacity;
	Foundation::Color panelBorder = Theme::Dialog::panelBorder;
	panelBorder.a *= opacity;

	Renderer::Primitives::drawRect(Renderer::Primitives::RectArgs{
		.bounds = panelBounds,
		.style = {.fill = panelBg,
				  .border = Foundation::BorderStyle{.color = panelBorder, .width = 1.0F}},
		.zIndex = zIndex + 1,
	});

	// Draw title bar background
	Foundation::Rect  titleBarBounds = getTitleBarBounds();
	Foundation::Color titleBg = Theme::Dialog::titleBackground;
	titleBg.a *= opacity;

	Renderer::Primitives::drawRect(Renderer::Primitives::RectArgs{
		.bounds = titleBarBounds,
		.style = {.fill = titleBg},
		.zIndex = zIndex + 2,
	});

	// Draw title text
	Foundation::Color titleColor = Theme::Colors::textTitle;
	titleColor.a *= opacity;

	Renderer::Primitives::drawText(Renderer::Primitives::TextArgs{
		.text = title,
		.position = {panelBounds.x + Theme::Dialog::contentPadding,
					 panelBounds.y + (Theme::Dialog::titleBarHeight - Theme::Typography::titleSize) / 2.0F},
		.scale = Theme::Typography::titleSize / 16.0F,
		.color = titleColor,
		.zIndex = static_cast<float>(zIndex) + 2.1F,
	});

	// Draw close button
	Foundation::Rect closeBounds = getCloseButtonBounds();

	// Close button background (only when hovered)
	if (closeButtonHovered) {
		Foundation::Color closeBg = Theme::Colors::closeButtonBackground;
		closeBg.a *= opacity;

		Renderer::Primitives::drawRect(Renderer::Primitives::RectArgs{
			.bounds = closeBounds,
			.style = {.fill = closeBg},
			.zIndex = zIndex + 3,
		});
	}

	// Draw X text
	Foundation::Color xColor = closeButtonHovered ? Theme::Colors::closeButtonText
												  : Theme::Colors::textSecondary;
	xColor.a *= opacity;

	float xTextX = closeBounds.x + (kCloseButtonSize - 10.0F) / 2.0F;
	float xTextY = closeBounds.y + (kCloseButtonSize - 14.0F) / 2.0F;

	Renderer::Primitives::drawText(Renderer::Primitives::TextArgs{
		.text = "X",
		.position = {xTextX, xTextY},
		.scale = 14.0F / 16.0F,
		.color = xColor,
		.zIndex = static_cast<float>(zIndex) + 3.1F,
	});

	// Draw separator line below title bar
	Foundation::Color lineColor = panelBorder;
	lineColor.a *= opacity * 0.5F;

	Renderer::Primitives::drawRect(Renderer::Primitives::RectArgs{
		.bounds = {panelBounds.x, titleBarBounds.y + titleBarBounds.height,
				   panelBounds.width, 1.0F},
		.style = {.fill = lineColor},
		.zIndex = zIndex + 2,
	});

	// Render children (dialog content)
	Component::render();
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

} // namespace UI
