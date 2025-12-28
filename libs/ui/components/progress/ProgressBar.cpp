#include "ProgressBar.h"

#include <algorithm>

namespace UI {

ProgressBar::ProgressBar(const Args& args)
	: value(std::clamp(args.value, 0.0F, 1.0F))
	, hasLabel(!args.label.empty())
	, borderWidth(args.borderWidth)
	, labelWidth(args.labelWidth)
	, labelGap(args.labelGap) {

	// Set base class members
	position = args.position;
	size = args.size;
	margin = args.margin;

	// Calculate bar width based on whether we have a label
	if (hasLabel) {
		barWidth = size.x - labelWidth - labelGap;
	} else {
		barWidth = size.x;
	}

	// Ensure minimum bar width
	barWidth = std::max(barWidth, borderWidth * 2.0F + 1.0F);

	// Add label if present
	if (hasLabel) {
		labelHandle = addChild(Text(Text::Args{
			.position = args.position,
			.text = args.label,
			.style =
				{
					.color = args.labelColor,
					.fontSize = args.labelFontSize,
					.hAlign = Foundation::HorizontalAlign::Left,
					.vAlign = Foundation::VerticalAlign::Top,
				},
			.id = nullptr}));
	}

	// Bar position (after label if present)
	float barX = hasLabel ? args.position.x + labelWidth + labelGap : args.position.x;
	float barY = args.position.y;

	// Add background bar (dark with border)
	backgroundHandle = addChild(Rectangle(Rectangle::Args{
		.position = {barX, barY},
		.size = {barWidth, size.y},
		.style =
			{
				.fill = args.backgroundColor,
				.border = Foundation::BorderStyle{.color = args.borderColor, .width = args.borderWidth}},
		.id = nullptr}));

	// Add fill bar (inset by border width)
	float fillWidth = (barWidth - borderWidth * 2.0F) * value;
	fillHandle = addChild(Rectangle(Rectangle::Args{
		.position = {barX + borderWidth, barY + borderWidth},
		.size = {std::max(0.0F, fillWidth), size.y - borderWidth * 2.0F},
		.style = {.fill = args.fillColor},
		.id = nullptr}));
}

void ProgressBar::setValue(float newValue) {
	value = std::clamp(newValue, 0.0F, 1.0F);

	// Update fill rectangle via handle
	auto* fill = getChild<Rectangle>(fillHandle);
	if (fill != nullptr) {
		// Update fill width based on value
		float fillWidth = (barWidth - borderWidth * 2.0F) * value;
		fill->size.x = std::max(0.0F, fillWidth);
	}
}

void ProgressBar::setFillColor(Foundation::Color color) {
	auto* fill = getChild<Rectangle>(fillHandle);
	if (fill != nullptr) {
		fill->style.fill = color;
	}
}

void ProgressBar::setLabel(const std::string& newLabel) {
	if (hasLabel) {
		if (auto* label = getChild<Text>(labelHandle)) {
			label->text = newLabel;
		}
	}
}

void ProgressBar::setPosition(Foundation::Vec2 newPos) {
	position = newPos;

	// Update label position if present
	if (hasLabel) {
		if (auto* label = getChild<Text>(labelHandle)) {
			label->position = newPos;
		}
	}

	// Bar position (after label if present)
	float barX = hasLabel ? newPos.x + labelWidth + labelGap : newPos.x;
	float barY = newPos.y;

	// Update background position
	if (auto* bg = getChild<Rectangle>(backgroundHandle)) {
		bg->position = {barX, barY};
	}

	// Update fill position (inset by border)
	if (auto* fill = getChild<Rectangle>(fillHandle)) {
		fill->position = {barX + borderWidth, barY + borderWidth};
	}
}

void ProgressBar::setWidth(float newWidth) {
	size.x = newWidth;

	// Recalculate bar width based on whether we have a label
	if (hasLabel) {
		barWidth = newWidth - labelWidth - labelGap;
	} else {
		barWidth = newWidth;
	}
	barWidth = std::max(barWidth, borderWidth * 2.0F + 1.0F);

	// Update background size
	if (auto* bg = getChild<Rectangle>(backgroundHandle)) {
		bg->size.x = barWidth;
	}

	// Update fill width based on current value
	if (auto* fill = getChild<Rectangle>(fillHandle)) {
		float fillWidth = (barWidth - borderWidth * 2.0F) * value;
		fill->size.x = std::max(0.0F, fillWidth);
	}
}

} // namespace UI
