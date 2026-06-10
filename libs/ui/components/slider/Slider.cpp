#include "components/slider/Slider.h"

#include "primitives/Primitives.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <string>

namespace UI {

namespace {
	constexpr double kFineStep = 0.01;
	constexpr float  kHandleHitRadius = 12.0F;
}

Slider::Slider(const Args& args)
	: FocusableBase<Slider>(args.tabIndex),
	  min(args.min),
	  max(args.max),
	  step(args.step),
	  logScale(args.logScale),
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

	// Label takes left portion if present
	constexpr float kLabelWidth = 90.0F;
	constexpr float kLabelGap = 6.0F;

	float leftOffset = label.empty() ? 0.0F : kLabelWidth + kLabelGap;
	trackLeft = contentPos.x + leftOffset + style.handleRadius;
	trackRight = contentPos.x + size.x - style.handleRadius;
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

	Foundation::Vec2 contentPos = getContentPosition();
	constexpr float  kLabelWidth = 90.0F;
	constexpr float  kLabelGap = 6.0F;

	// Label
	if (!label.empty()) {
		Text labelShape(Text::Args{
			.position = {contentPos.x, trackY},
			.width = kLabelWidth,
			.text = label,
			.style = {
				.color = disabled
					? Foundation::Color{0.45F, 0.45F, 0.45F, 1.0F}
					: style.labelColor,
				.fontSize = style.labelFontSize,
				.hAlign = Foundation::HorizontalAlign::Left,
				.vAlign = Foundation::VerticalAlign::Middle,
			},
		});
		labelShape.render();

		// Value text (right-aligned after track)
		std::string valStr;
		if (valueFormatter) {
			valStr = valueFormatter(value);
		} else {
			valStr = std::format("{:.2f}", value);
		}
		float valX = contentPos.x + size.x;
		constexpr float kValueWidth = 60.0F;
		Text valShape(Text::Args{
			.position = {valX - kValueWidth, trackY},
			.width = kValueWidth,
			.text = valStr,
			.style = {
				.color = disabled
					? Foundation::Color{0.45F, 0.45F, 0.45F, 1.0F}
					: style.labelColor,
				.fontSize = style.labelFontSize,
				.hAlign = Foundation::HorizontalAlign::Right,
				.vAlign = Foundation::VerticalAlign::Middle,
			},
		});
		valShape.render();
	}

	float hx = handleX();
	float halfTrack = style.trackHeight * 0.5F;

	// Track background
	Foundation::Rect trackRect{trackLeft, trackY - halfTrack, trackRight - trackLeft, style.trackHeight};
	Renderer::Primitives::drawRect({
		.bounds = trackRect,
		.style = {
			.fill = style.trackColor,
			.border = Foundation::BorderStyle{
				.color = style.trackBorderColor,
				.width = style.borderWidth,
				.cornerRadius = halfTrack,
			},
		},
		.id = id,
	});

	// Fill (left of handle)
	if (hx > trackLeft) {
		Foundation::Rect fillRect{trackLeft, trackY - halfTrack, hx - trackLeft, style.trackHeight};
		Renderer::Primitives::drawRect({
			.bounds = fillRect,
			.style = {
				.fill = disabled ? style.handleDisabledColor : style.fillColor,
			},
			.id = nullptr,
		});
	}

	// Handle
	Foundation::Color handleColor;
	if (disabled) {
		handleColor = style.handleDisabledColor;
	} else if (dragging) {
		handleColor = style.handleActiveColor;
	} else if (handleHovered) {
		handleColor = style.handleHoverColor;
	} else {
		handleColor = style.handleColor;
	}

	// Focus ring (slightly larger circle behind handle)
	if (focused && !disabled) {
		Renderer::Primitives::drawCircle({
			.center = {hx, trackY},
			.radius = style.handleRadius + 3.0F,
			.style = {.fill = style.focusRingColor},
			.id = nullptr,
		});
	}

	Renderer::Primitives::drawCircle({
		.center = {hx, trackY},
		.radius = style.handleRadius,
		.style = {.fill = handleColor},
		.id = nullptr,
	});
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
