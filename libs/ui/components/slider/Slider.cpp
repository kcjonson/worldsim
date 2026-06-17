#include "components/slider/Slider.h"

#include "font/FontRenderer.h"
#include "graphics/PrimitiveStyles.h"
#include "graphics/Rect.h"
#include "primitives/Primitives.h"
#include "theme/Tokens.h"
#include "theme/Variants.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace UI {

namespace {
	constexpr double kFineStep = 0.01;
	constexpr float  kHandleHitRadius = 12.0F;
	// Labeled sliders: name label on the left, value text on the right,
	// track in between. Both reserve fixed-width columns.
	constexpr float kLabelWidth = 90.0F;
	constexpr float kLabelGap = 6.0F;
	constexpr float kValueWidth = 60.0F;
	constexpr float kValueGap = 6.0F;
}

Slider::Slider(const Args& args)
	: FocusableBase<Slider>(args.tabIndex),
	  min(args.min),
	  max(args.max),
	  step(args.step),
	  logScale(args.logScale),
	  detent(args.detent),
	  label(args.label),
	  valueFormatter(args.valueFormatter),
	  onChanged(args.onChanged),
	  id(args.id),
	  disabled(args.disabled),
	  style(args.style) {

	position = args.position;
	size = args.size;
	margin = args.margin;

	// Clamp initial value to [min, max]
	value = clampValue(args.value);
	if (step > 0.0) {
		value = snapToStep(value);
	}

	computeLayout();
}

double Slider::positionToValue(double t) const {
	if (max <= min) {
		return min;
	}
	t = std::clamp(t, 0.0, 1.0);
	if (logScale && min > 0.0 && max > 0.0) {
		return min * std::pow(max / min, t);
	}
	return min + t * (max - min);
}

double Slider::valueToPosition(double val) const {
	if (max <= min) {
		return 0.0;
	}
	if (logScale && min > 0.0 && max > 0.0) {
		if (val <= 0.0) {
			return 0.0;
		}
		return std::log(val / min) / std::log(max / min);
	}
	return (val - min) / (max - min);
}

double Slider::snapToStep(double val) const {
	if (step <= 0.0) {
		return val;
	}
	double snapped = min + std::round((val - min) / step) * step;
	return clampValue(snapped);
}

double Slider::clampValue(double val) const {
	return std::clamp(val, min, max);
}

void Slider::computeLayout() {
	Foundation::Vec2 contentPos = getContentPosition();

	float leftOffset = label.empty() ? 0.0F : kLabelWidth + kLabelGap;
	float rightOffset = label.empty() ? 0.0F : kValueWidth + kValueGap;
	trackLeft = contentPos.x + leftOffset + style.handleRadius;
	trackRight = contentPos.x + size.x - rightOffset - style.handleRadius;
	trackY = contentPos.y + size.y * 0.5F;
}

float Slider::handleX() const {
	double t = valueToPosition(value);
	return trackLeft + static_cast<float>(t) * (trackRight - trackLeft);
}

void Slider::setValueFromTrackX(float x) {
	if (trackRight <= trackLeft) {
		return;
	}
	double t = static_cast<double>(x - trackLeft) / static_cast<double>(trackRight - trackLeft);
	double newValue = positionToValue(t);
	if (step > 0.0) {
		newValue = snapToStep(newValue);
	} else {
		newValue = clampValue(newValue);
	}
	if (newValue != value) {
		value = newValue;
		fireChanged();
	}
}

void Slider::fireChanged() {
	if (inCallback) {
		return;
	}
	if (onChanged) {
		inCallback = true;
		onChanged(value);
		inCallback = false;
	}
}

void Slider::setValue(double newValue) {
	double clamped = clampValue(newValue);
	if (step > 0.0) {
		clamped = snapToStep(clamped);
	}
	if (clamped != value) {
		value = clamped;
		fireChanged();
	}
}

void Slider::update(float /*deltaTime*/) {
	computeLayout();
}

void Slider::render() {
	if (!visible) {
		return;
	}
	using Renderer::Primitives::drawRect;
	using Renderer::Primitives::drawText;

	// drawText scale is relative to a 16px base.
	constexpr float kTextBasePx = 16.0F;
	const float		headerScale = fs_sm / kTextBasePx;

	// Salvage geometry: 4px full-pill track, 14px square thumb.
	constexpr float kTrackHeight = 4.0F;
	constexpr float kThumbSize = 14.0F;

	const Foundation::Vec2 contentPos = getContentPosition();

	// Header row: label left in text_dim, value right in accent_bright, both mono.
	if (!label.empty()) {
		drawText({.text = label,
				  .position = {contentPos.x, contentPos.y},
				  .scale = headerScale,
				  .color = disabled ? text_disabled : text_dim,
				  .font = fontMono,
				  .id = id});

		const std::string valStr = valueFormatter ? valueFormatter(value) : "";
		if (!valStr.empty()) {
			drawText({.text = valStr,
					  .position = {contentPos.x, contentPos.y},
					  .scale = headerScale,
					  .color = disabled ? text_disabled : accent_bright,
					  .font = fontMono,
					  .hAlign = Foundation::HorizontalAlign::Right,
					  .boxWidth = size.x,
					  .id = nullptr});
		}
	}

	const float hx = handleX();
	const float trackTop = trackY - (kTrackHeight * 0.5F);
	const float radius = kTrackHeight * 0.5F; // full pill

	// Track: bg_inset fill with a hairline inside border.
	const Foundation::Rect track{trackLeft, trackTop, trackRight - trackLeft, kTrackHeight};
	drawRect({.bounds = track,
			  .style = {.fill = bg_inset,
						.border = Foundation::BorderStyle{
							.color = line_hairline, .width = bw, .cornerRadius = radius, .position = Foundation::BorderPosition::Inside}},
			  .id = id});

	// Accent fill from the left edge to the value position.
	const float fillWidth = hx - trackLeft;
	if (fillWidth > 0.0F) {
		drawRect({.bounds = {track.x, trackTop, fillWidth, kTrackHeight},
				  .style = {.fill = accent,
							.border = Foundation::BorderStyle{
								.color = accent, .width = 0.0F, .cornerRadius = radius, .position = Foundation::BorderPosition::Inside}},
				  .id = nullptr});
	}

	// Detent: a 2px teal reference tick centered on its normalized position.
	if (detent >= 0.0) {
		const float d = static_cast<float>(std::clamp(detent, 0.0, 1.0));
		const float tickX = (trackLeft + (d * (trackRight - trackLeft))) - (bw_thick * 0.5F);
		drawRect({.bounds = {tickX, trackY - (kThumbSize * 0.5F) + space_0_5, bw_thick, kThumbSize - (space_0_5 * 2.0F)},
				  .style = {.fill = withAlpha(data, 0.7F)},
				  .id = nullptr});
	}

	// Thumb: a 14px square (r_sm) centered on the value, with a bg_void hairline
	// border and a soft accent glow behind it.
	const float thumbX = hx - (kThumbSize * 0.5F);
	const float thumbY = trackY - (kThumbSize * 0.5F);
	drawRect({.bounds = {thumbX, thumbY, kThumbSize, kThumbSize},
			  .style = {.fill = accent_bright,
						.border = Foundation::BorderStyle{
							.color = bg_void, .width = bw, .cornerRadius = r_sm, .position = Foundation::BorderPosition::Inside},
						.boxShadow = Foundation::BoxShadow{.color = withAlpha(accent, 0.4F), .blur = 8.0F, .spread = 0.0F, .offset = {0.0F, 0.0F}}},
			  .id = nullptr});
}

bool Slider::containsPoint(Foundation::Vec2 point) const {
	return point.x >= position.x && point.x <= position.x + getWidth() &&
	       point.y >= position.y && point.y <= position.y + getHeight();
}

bool Slider::handleEvent(InputEvent& event) {
	if (disabled || !visible) {
		return false;
	}

	float hx = handleX();

	switch (event.type) {
		case InputEvent::Type::MouseDown:
			if (event.button == engine::MouseButton::Left && containsPoint(event.position)) {
				float distToHandle = std::abs(event.position.x - hx);
				if (distToHandle <= kHandleHitRadius) {
					dragging = true;
				} else {
					// Click on track - jump to that position
					setValueFromTrackX(event.position.x);
					dragging = true;
				}
				event.consume();
				return true;
			}
			break;

		case InputEvent::Type::MouseUp:
			if (dragging && event.button == engine::MouseButton::Left) {
				dragging = false;
				event.consume();
				return true;
			}
			break;

		case InputEvent::Type::MouseMove:
			if (dragging) {
				setValueFromTrackX(event.position.x);
			}
			// Update handle hover state
			{
				float dist = std::abs(event.position.x - hx);
				bool isOverHandle = dist <= kHandleHitRadius && containsPoint(event.position);
				handleHovered = isOverHandle;
			}
			break;

		case InputEvent::Type::Scroll:
			break;
	}
	return false;
}

void Slider::onFocusGained() {
	focused = true;
}

void Slider::onFocusLost() {
	focused = false;
}

void Slider::handleKeyInput(engine::Key key, bool /*shift*/, bool /*ctrl*/, bool /*alt*/) {
	if (disabled) {
		return;
	}

	switch (key) {
		case engine::Key::Left:
		case engine::Key::Down: {
			double fineStep = (step > 0.0) ? step : (max - min) * kFineStep;
			setValue(value - fineStep);
			break;
		}
		case engine::Key::Right:
		case engine::Key::Up: {
			double fineStep = (step > 0.0) ? step : (max - min) * kFineStep;
			setValue(value + fineStep);
			break;
		}
		case engine::Key::Home:
			setValue(min);
			break;
		case engine::Key::End:
			setValue(max);
			break;
		default:
			break;
	}
}

void Slider::handleCharInput(char32_t /*codepoint*/) {
	// Slider doesn't use char input
}

bool Slider::canReceiveFocus() const {
	return !disabled;
}

} // namespace UI
