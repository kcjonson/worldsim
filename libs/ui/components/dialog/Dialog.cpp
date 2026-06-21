#include "Dialog.h"

#include "graphics/PrimitiveStyles.h"
#include "primitives/Primitives.h"
#include "theme/Tokens.h"
#include "theme/Variants.h"

#include <algorithm>

namespace UI {

	Dialog::Dialog(const Args& args)
		: FocusableBase<Dialog>(args.tabIndex),
		  title(args.title),
		  kicker(args.kicker),
		  dialogSize(args.size),
		  onClose(args.onClose),
		  modal(args.modal),
		  footerHeight(args.footerHeight) {
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
		return {pos.x, pos.y, dialogSize.x, titleAreaHeight()};
	}

	Foundation::Rect Dialog::getCloseButtonBounds() const {
		Foundation::Vec2 pos = getPanelPosition();
		float			 buttonX = pos.x + dialogSize.x - kCloseButtonMargin - kCloseButtonSize;
		// Pin the close button to the top of the title area (kicker pushes title down).
		float			 buttonY = pos.y + (kDialogTitleBarHeight - kCloseButtonSize) / 2.0F;
		return {buttonX, buttonY, kCloseButtonSize, kCloseButtonSize};
	}

	Foundation::Rect Dialog::getContentBounds() const {
		Foundation::Vec2 pos = getPanelPosition();
		float			 contentY = pos.y + titleAreaHeight();
		float			 contentHeight = dialogSize.y - titleAreaHeight() - footerHeight;
		return {
			pos.x + kDialogContentPadding,
			contentY + kDialogContentPadding,
			std::max(0.0F, dialogSize.x - kDialogContentPadding * 2),
			std::max(0.0F, contentHeight - kDialogContentPadding * 2)
		};
	}

	Foundation::Rect Dialog::getFooterBounds() const {
		if (footerHeight <= 0.0F) {
			return {};
		}
		Foundation::Vec2 pos = getPanelPosition();
		return {pos.x, pos.y + dialogSize.y - footerHeight, dialogSize.x, footerHeight};
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
						// When modal: consume the event (block game interaction)
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

		using Renderer::Primitives::drawLine;
		using Renderer::Primitives::drawRect;
		using Renderer::Primitives::drawText;

		// Fold the fade opacity into any color's alpha.
		const auto fade = [&](Foundation::Color c) {
			c.a *= opacity;
			return c;
		};

		// Full-bleed scrim (modal only).
		if (modal) {
			drawRect({.bounds = {0.0F, 0.0F, screenWidth, screenHeight}, .style = {.fill = fade(scrim)}});
		}

		const Foundation::Rect panel = getPanelBounds();
		const float			   titleH = titleAreaHeight();

		// Raised, bracketed Salvage surface with an edge border and a soft accent glow.
		drawRect({.bounds = panel,
				  .style = {.fill = fade(bg_panel_raised),
							.border = Foundation::BorderStyle{
								.color = fade(line_edge), .width = bw, .cornerRadius = r_sm, .position = Foundation::BorderPosition::Inside},
							.boxShadow = Foundation::BoxShadow{.color = fade(withAlpha(accent, 0.4F)), .blur = 24.0F, .spread = 2.0F, .offset = {0.0F, 0.0F}}}});

		// Optional kicker eyebrow above the title: mono, faint, uppercase, wide tracking.
		float titleY = panel.y + ((titleH - fs_md) * 0.5F);
		if (!kicker.empty()) {
			const float kickerY = panel.y + space_2;
			drawText({.text = kicker,
					  .position = {panel.x + kDialogContentPadding, kickerY},
					  .scale = fs_2xs / 16.0F,
					  .color = fade(text_faint),
					  .font = fontMono,
					  .letterSpacing = fs_2xs * ls_wider,
					  .transform = Foundation::TextTransform::Uppercase});
			titleY = kickerY + fs_2xs + space_1_5;
		}

		// Title: display font, uppercase, letter-spaced.
		if (!title.empty()) {
			drawText({.text = title,
					  .position = {panel.x + kDialogContentPadding, titleY},
					  .scale = fs_md / 16.0F,
					  .color = fade(text_bright),
					  .font = fontDisplay,
					  .letterSpacing = fs_md * ls_wide,
					  .transform = Foundation::TextTransform::Uppercase});
		}

		// Hairline divider under the title.
		drawLine({.start = {panel.x, panel.y + titleH}, .end = {panel.x + panel.width, panel.y + titleH}, .style = {.color = fade(line_hairline), .width = bw}});

		// Footer hairline (when a footer band is reserved).
		if (footerHeight > 0.0F) {
			const float footerY = panel.y + dialogSize.y - footerHeight;
			drawLine({.start = {panel.x, footerY}, .end = {panel.x + panel.width, footerY}, .style = {.color = fade(line_hairline), .width = bw}});
		}

		// Close button: hover wash + a mono "X".
		const Foundation::Rect closeBounds = getCloseButtonBounds();
		if (closeButtonHovered) {
			drawRect({.bounds = closeBounds,
					  .style = {.fill = fade(bg_hover),
								.border = Foundation::BorderStyle{
									.color = fade(bg_hover), .width = 0.0F, .cornerRadius = r_sm, .position = Foundation::BorderPosition::Inside}}});
		}
		drawText({.text = "X",
				  .position = {closeBounds.x, closeBounds.y},
				  .scale = fs_sm / 16.0F,
				  .color = fade(closeButtonHovered ? accent_bright : text_dim),
				  .font = fontMono,
				  .hAlign = Foundation::HorizontalAlign::Center,
				  .vAlign = Foundation::VerticalAlign::Middle,
				  .boxWidth = closeBounds.width,
				  .boxHeight = closeBounds.height});

		// L-bracket corner ticks straddling the panel edge.
		{
			const Foundation::Color tick = fade(accent);
			const float				leg = space_3;
			const float				th = bw_thick;
			const float				ox = panel.x;
			const float				oy = panel.y;
			const float				rx = panel.x + panel.width;
			const float				by = panel.y + panel.height;
			const auto				bracket = [&](Foundation::Rect r) { drawRect({.bounds = r, .style = {.fill = tick}}); };
			bracket({ox, oy, leg, th});
			bracket({ox, oy, th, leg});
			bracket({rx - leg, oy, leg, th});
			bracket({rx - th, oy, th, leg});
			bracket({ox, by - th, leg, th});
			bracket({ox, by - leg, th, leg});
			bracket({rx - leg, by - th, leg, th});
			bracket({rx - th, by - leg, th, leg});
		}

		// Render content children (Container handles clipping and offset).
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
