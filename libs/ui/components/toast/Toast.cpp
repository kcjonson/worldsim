#include "Toast.h"

#include "primitives/Primitives.h"
#include "theme/PanelStyle.h"

namespace UI {

Toast::Toast(const Args& args)
	: title(args.title),
	  message(args.message),
	  severity(args.severity),
	  autoDismissTime(args.autoDismissTime),
	  iconPath(args.iconPath),
	  onDismiss(args.onDismiss),
	  toastWidth(args.width) {
	position = args.position;
	size = {toastWidth, calculateHeight()};
	margin = args.margin;
}

void Toast::dismiss() {
	if (state != State::Dismissing && state != State::Finished) {
		state = State::Dismissing;
		stateTimer = 0.0F;
	}
}

float Toast::getRemainingTime() const {
	if (autoDismissTime <= 0.0F || state == State::Dismissing || state == State::Finished) {
		return 0.0F;
	}
	if (state == State::Appearing) {
		return autoDismissTime;
	}
	return std::max(0.0F, autoDismissTime - stateTimer);
}

float Toast::calculateHeight() const {
	// Title line + message line + padding
	float contentHeight = kTitleFontSize + kLineSpacing + kMessageFontSize;
	return kPadding * 2 + contentHeight;
}

Foundation::Color Toast::getBackgroundColor() const {
	switch (severity) {
		case ToastSeverity::Warning:
			return Theme::Toast::warningBackground;
		case ToastSeverity::Critical:
			return Theme::Toast::criticalBackground;
		case ToastSeverity::Info:
		default:
			return Theme::Toast::infoBackground;
	}
}

Foundation::Rect Toast::getDismissButtonBounds() const {
	Foundation::Vec2 contentPos = getContentPosition();
	float			 buttonX = contentPos.x + toastWidth - kPadding - kDismissButtonSize;
	float			 buttonY = contentPos.y + kPadding;
	return {buttonX, buttonY, kDismissButtonSize, kDismissButtonSize};
}

bool Toast::isPointInDismissButton(Foundation::Vec2 point) const {
	Foundation::Rect bounds = getDismissButtonBounds();
	return point.x >= bounds.x && point.x < bounds.x + bounds.width && point.y >= bounds.y &&
		   point.y < bounds.y + bounds.height;
}

void Toast::setPosition(float x, float y) {
	position = {x, y};
}

bool Toast::containsPoint(Foundation::Vec2 point) const {
	Foundation::Vec2 contentPos = getContentPosition();
	float			 height = calculateHeight();
	return point.x >= contentPos.x && point.x < contentPos.x + toastWidth &&
		   point.y >= contentPos.y && point.y < contentPos.y + height;
}

bool Toast::handleEvent(InputEvent& event) {
	if (!visible || state == State::Finished) {
		return false;
	}

	switch (event.type) {
		case InputEvent::Type::MouseMove: {
			dismissButtonHovered = isPointInDismissButton(event.position);
			return false; // Don't consume mouse move
		}

		case InputEvent::Type::MouseDown: {
			if (isPointInDismissButton(event.position)) {
				event.consume();
				return true;
			}
			break;
		}

		case InputEvent::Type::MouseUp: {
			if (isPointInDismissButton(event.position)) {
				dismiss();
				event.consume();
				return true;
			}
			break;
		}

		default:
			break;
	}

	return false;
}

void Toast::update(float deltaTime) {
	stateTimer += deltaTime;

	switch (state) {
		case State::Appearing:
			opacity = std::min(1.0F, stateTimer / kFadeInDuration);
			if (stateTimer >= kFadeInDuration) {
				state = State::Visible;
				stateTimer = 0.0F;
				opacity = 1.0F;
			}
			break;

		case State::Visible:
			opacity = 1.0F;
			// Check auto-dismiss timer
			if (autoDismissTime > 0.0F && stateTimer >= autoDismissTime) {
				dismiss();
			}
			break;

		case State::Dismissing:
			opacity = std::max(0.0F, 1.0F - stateTimer / kFadeOutDuration);
			if (stateTimer >= kFadeOutDuration) {
				state = State::Finished;
				opacity = 0.0F;
				if (onDismiss) {
					onDismiss();
				}
			}
			break;

		case State::Finished:
			// Nothing to do
			break;
	}
}

void Toast::render() {
	if (!visible || state == State::Finished || opacity <= 0.0F) {
		return;
	}

	Foundation::Vec2 contentPos = getContentPosition();
	float			 height = calculateHeight();

	// Apply opacity to background color
	Foundation::Color bgColor = getBackgroundColor();
	bgColor.a *= opacity;

	// Draw background with border
	Foundation::Color borderColor = Foundation::Color(1.0F, 1.0F, 1.0F, 0.2F * opacity);
	Renderer::Primitives::drawRect(Renderer::Primitives::RectArgs{
		.bounds = {contentPos.x, contentPos.y, toastWidth, height},
		.style = {.fill = bgColor,
				  .border = Foundation::BorderStyle{.color = borderColor, .width = 1.0F}},
		.zIndex = zIndex,
	});

	// Calculate text area (leaving space for dismiss button)
	float textAreaWidth = toastWidth - kPadding * 2 - kDismissButtonSize - 8.0F;

	// Draw title
	Foundation::Color titleColor = Foundation::Color(1.0F, 1.0F, 1.0F, opacity);
	Renderer::Primitives::drawText(Renderer::Primitives::TextArgs{
		.text = title,
		.position = {contentPos.x + kPadding, contentPos.y + kPadding},
		.scale = kTitleFontSize / 16.0F,
		.color = titleColor,
		.zIndex = static_cast<float>(zIndex) + 0.1F,
	});

	// Draw message
	Foundation::Color messageColor = Foundation::Color(0.9F, 0.9F, 0.9F, opacity * 0.9F);
	Renderer::Primitives::drawText(Renderer::Primitives::TextArgs{
		.text = message,
		.position = {contentPos.x + kPadding,
					 contentPos.y + kPadding + kTitleFontSize + kLineSpacing},
		.scale = kMessageFontSize / 16.0F,
		.color = messageColor,
		.zIndex = static_cast<float>(zIndex) + 0.1F,
	});

	// Draw dismiss button (X)
	Foundation::Rect  dismissBounds = getDismissButtonBounds();
	Foundation::Color dismissBg =
		dismissButtonHovered ? Foundation::Color(1.0F, 1.0F, 1.0F, 0.2F * opacity)
							 : Foundation::Color(0.0F, 0.0F, 0.0F, 0.0F);

	if (dismissButtonHovered) {
		Renderer::Primitives::drawRect(Renderer::Primitives::RectArgs{
			.bounds = dismissBounds,
			.style = {.fill = dismissBg},
			.zIndex = zIndex + 1,
		});
	}

	// Draw X text
	Foundation::Color xColor =
		dismissButtonHovered ? Foundation::Color(1.0F, 1.0F, 1.0F, opacity)
							 : Foundation::Color(0.7F, 0.7F, 0.7F, opacity);
	float xTextX = dismissBounds.x + (kDismissButtonSize - 8.0F) / 2.0F;
	float xTextY = dismissBounds.y + (kDismissButtonSize - 12.0F) / 2.0F;
	Renderer::Primitives::drawText(Renderer::Primitives::TextArgs{
		.text = "X",
		.position = {xTextX, xTextY},
		.scale = 12.0F / 16.0F,
		.color = xColor,
		.zIndex = static_cast<float>(zIndex) + 0.2F,
	});

	// Draw remaining time indicator if auto-dismiss
	if (autoDismissTime > 0.0F && state == State::Visible) {
		float remaining = getRemainingTime();
		int	  seconds = static_cast<int>(remaining) + 1;
		std::string timerText = "[" + std::to_string(seconds) + "s]";

		Foundation::Color timerColor = Foundation::Color(0.6F, 0.6F, 0.6F, opacity * 0.8F);
		float			  timerX = dismissBounds.x - 30.0F;
		float			  timerY = dismissBounds.y + (kDismissButtonSize - 10.0F) / 2.0F;
		Renderer::Primitives::drawText(Renderer::Primitives::TextArgs{
			.text = timerText,
			.position = {timerX, timerY},
			.scale = 10.0F / 16.0F,
			.color = timerColor,
			.zIndex = static_cast<float>(zIndex) + 0.1F,
		});
	}
}

} // namespace UI
