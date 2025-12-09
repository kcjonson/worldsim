#include "NeedBar.h"

#include <algorithm>

namespace world_sim {

NeedBar::NeedBar(const Args& args)
	: width(args.width)
	, height(args.height)
	, currentPosition(args.position) {

	// Calculate bar width (total width minus label space)
	barWidth = width - kLabelWidth - kBarGap;

	// Add label as child
	labelHandle = addChild(UI::Text(UI::Text::Args{
		.position = args.position,
		.text = args.label,
		.style =
			{
				.color = Foundation::Color::white(),
				.fontSize = kLabelFontSize,
				.hAlign = Foundation::HorizontalAlign::Left,
				.vAlign = Foundation::VerticalAlign::Top,
			},
		.id = (args.id + "_label").c_str()}));

	// Bar position (after label)
	float barX = args.position.x + kLabelWidth + kBarGap;
	float barY = args.position.y;

	// Add background bar (dark gray) as child
	backgroundHandle = addChild(UI::Rectangle(UI::Rectangle::Args{
		.position = {barX, barY},
		.size = {barWidth, height},
		.style =
			{
				.fill = Foundation::Color(0.2F, 0.2F, 0.25F, 1.0F),
				.border = Foundation::BorderStyle{.color = Foundation::Color(0.3F, 0.3F, 0.35F, 1.0F), .width = 1.0F}},
		.id = (args.id + "_bg").c_str()}));

	// Add fill bar (starts at full width, colored green) as child
	fillHandle = addChild(UI::Rectangle(UI::Rectangle::Args{
		.position = {barX + 1.0F, barY + 1.0F}, // Inset by border
		.size = {barWidth - 2.0F, height - 2.0F},
		.style = {.fill = valueToColor(value)},
		.id = (args.id + "_fill").c_str()}));
}

void NeedBar::setValue(float newValue) {
	value = std::clamp(newValue, 0.0F, 100.0F);

	// Update fill rectangle via handle
	auto* fill = getChild<UI::Rectangle>(fillHandle);
	if (fill != nullptr) {
		// Update fill width based on value
		float fillWidth = (barWidth - 2.0F) * (value / 100.0F);
		fill->size.x = std::max(0.0F, fillWidth);

		// Update color based on value
		fill->style.fill = valueToColor(value);
	}
}

void NeedBar::setLabel(const std::string& newLabel) {
	if (auto* label = getChild<UI::Text>(labelHandle)) {
		label->text = newLabel;
	}
}

float NeedBar::getTotalHeight() const {
	return height;
}

void NeedBar::setPosition(Foundation::Vec2 newPos) {
	currentPosition = newPos;
	position = newPos; // Also update base class position for consistency

	// Update label position
	if (auto* label = getChild<UI::Text>(labelHandle)) {
		label->position = newPos;
	}

	// Bar position (after label)
	float barX = newPos.x + kLabelWidth + kBarGap;
	float barY = newPos.y;

	// Update background position
	if (auto* bg = getChild<UI::Rectangle>(backgroundHandle)) {
		bg->position = {barX, barY};
	}

	// Update fill position (inset by border)
	if (auto* fill = getChild<UI::Rectangle>(fillHandle)) {
		fill->position = {barX + 1.0F, barY + 1.0F};
	}
}

Foundation::Color NeedBar::valueToColor(float newValue) {
	// Color gradient: red (0%) → yellow (50%) → green (100%)
	// Note: Low values indicate need is depleted (bad), high values indicate satisfied (good)
	// Using HSL-ish interpolation for pleasing gradient

	if (newValue <= 0.0F) {
		return Foundation::Color(0.8F, 0.2F, 0.2F, 1.0F); // Red
	}
	if (newValue >= 100.0F) {
		return Foundation::Color(0.2F, 0.8F, 0.3F, 1.0F); // Green
	}

	// Normalize to 0-1 range
	float t = newValue / 100.0F;

	if (t < 0.5F) {
		// Red to Yellow (0% - 50%)
		float ratio = t * 2.0F; // 0 to 1
		return Foundation::Color(
			0.8F + 0.15F * ratio, // R: 0.8 → 0.95
			0.2F + 0.6F * ratio,  // G: 0.2 → 0.8
			0.2F,				  // B stays low
			1.0F
		);
	}
	// Yellow to Green (50% - 100%)
	float ratio = (t - 0.5F) * 2.0F; // 0 to 1
	return Foundation::Color(
		0.95F - 0.75F * ratio, // R: 0.95 → 0.2
		0.8F,				   // G stays high
		0.2F + 0.1F * ratio,   // B: 0.2 → 0.3
		1.0F
	);
}

} // namespace world_sim
