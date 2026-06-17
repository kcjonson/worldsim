#include "Toast.h"

#include "graphics/PrimitiveStyles.h"
#include "primitives/Primitives.h"
#include "theme/Tokens.h"
#include "theme/Variants.h"

namespace UI {

Toast::Toast(const Args& args)
	: title(args.title),
	  message(args.message),
	  severity(args.severity),
	  autoDismissTime(args.autoDismissTime),
	  iconPath(args.iconPath),
	  onDismiss(args.onDismiss),
	  onClick(args.onClick),
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

Foundation::Color Toast::getSeverityColor() const {
	switch (severity) {
		case ToastSeverity::Warning:
			return toneColor(Tone::Warn);
		case ToastSeverity::Critical:
			return toneColor(Tone::Crit);
		case ToastSeverity::Info:
		default:
			return toneColor(Tone::Data);
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
			// Consume click on toast body or dismiss button
			if (containsPoint(event.position)) {
				event.consume();
				return true;
			}
			break;
		}

		case InputEvent::Type::MouseUp: {
			if (isPointInDismissButton(event.position)) {
				// Clicking dismiss button just dismisses
				dismiss();
				event.consume();
				return true;
			}
			if (containsPoint(event.position)) {
				// Clicking toast body triggers onClick callback and dismisses
				if (onClick) {
					onClick();
				}
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

	using Renderer::Primitives::drawRect;
	using Renderer::Primitives::drawText;

	// Fold the fade opacity into any color's alpha (matches Dialog).
	const auto fade = [&](Foundation::Color c) {
		c.a *= opacity;
		return c;
	};

	const Foundation::Vec2	contentPos = getContentPosition();
	const float				height = calculateHeight();
	const Foundation::Color severityColor = getSeverityColor();

	// Raised Salvage card: edge border (r_sm) plus a soft glow tinted by the
	// severity tone.
	drawRect({
		.bounds = {contentPos.x, contentPos.y, toastWidth, height},
		.style = {.fill = fade(bg_panel_raised),
				  .border = Foundation::BorderStyle{.color = fade(line_edge),
													.width = bw,
													.cornerRadius = r_sm,
													.position = Foundation::BorderPosition::Inside},
				  .boxShadow = Foundation::BoxShadow{
					  .color = fade(withAlpha(severityColor, 0.35F)), .blur = 12.0F, .spread = 0.0F, .offset = {0.0F, 0.0F}}},
		.zIndex = zIndex,
	});

	// Left severity accent stripe in the card's gutter (above the card fill).
	constexpr float kStripeWidth = 3.0F;
	drawRect({
		.bounds = {contentPos.x, contentPos.y, kStripeWidth, height},
		.style = {.fill = fade(severityColor)},
		.zIndex = zIndex + 1,
	});

	// Title in the severity tone (UI font).
	drawText({
		.text = title,
		.position = {contentPos.x + kPadding, contentPos.y + kPadding},
		.scale = kTitleFontSize / 16.0F,
		.color = fade(severityColor),
		.font = fontUi,
		.zIndex = static_cast<float>(zIndex) + 0.1F,
	});

	// Body text.
	drawText({
		.text = message,
		.position = {contentPos.x + kPadding, contentPos.y + kPadding + kTitleFontSize + kLineSpacing},
		.scale = kMessageFontSize / 16.0F,
		.color = fade(text),
		.font = fontUi,
		.zIndex = static_cast<float>(zIndex) + 0.1F,
	});

	// Dismiss button (X): hover wash + a mono glyph.
	const Foundation::Rect dismissBounds = getDismissButtonBounds();
	if (dismissButtonHovered) {
		drawRect({
			.bounds = dismissBounds,
			.style = {.fill = fade(bg_hover),
					  .border = Foundation::BorderStyle{.color = fade(bg_hover),
														.width = 0.0F,
														.cornerRadius = r_sm,
														.position = Foundation::BorderPosition::Inside}},
			.zIndex = zIndex + 1,
		});
	}
	drawText({
		.text = "X",
		.position = {dismissBounds.x, dismissBounds.y},
		.scale = fs_sm / 16.0F,
		.color = fade(dismissButtonHovered ? text_bright : text_dim),
		.font = fontMono,
		.hAlign = Foundation::HorizontalAlign::Center,
		.vAlign = Foundation::VerticalAlign::Middle,
		.boxWidth = dismissBounds.width,
		.boxHeight = dismissBounds.height,
		.zIndex = static_cast<float>(zIndex) + 0.2F,
	});

	// Remaining-time indicator (mono label) while auto-dismiss is counting down.
	if (autoDismissTime > 0.0F && state == State::Visible) {
		const float		  remaining = getRemainingTime();
		const int		  seconds = static_cast<int>(remaining) + 1;
		const std::string timerText = "[" + std::to_string(seconds) + "s]";

		const float timerX = dismissBounds.x - 30.0F;
		const float timerY = dismissBounds.y + (kDismissButtonSize - fs_2xs) / 2.0F;
		drawText({
			.text = timerText,
			.position = {timerX, timerY},
			.scale = fs_2xs / 16.0F,
			.color = fade(text_dim),
			.font = fontMono,
			.zIndex = static_cast<float>(zIndex) + 0.1F,
		});
	}
}

} // namespace UI
