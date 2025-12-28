#include "DateTimeDisplay.h"

#include <theme/Theme.h>

namespace world_sim {

namespace {
	// Approximate average character width for simple text layout calculations.
	// This is a rough estimate - for precise layout, use FontRenderer::measureText().
	constexpr float kApproxCharWidth = 7.0F;

	// Expected max character count for date/time string (e.g., "Day 999, Winter | 23:59")
	constexpr size_t kExpectedMaxChars = 24;
}  // namespace

DateTimeDisplay::DateTimeDisplay(const Args& args) {
	position = args.position;

	timeTextHandle = addChild(UI::Text(UI::Text::Args{
		.position = position,
		.text = "Day 1, Spring | 06:00",
		.style =
			{
				.color = UI::Theme::Colors::textBody,
				.fontSize = UI::Theme::Typography::bodySize,
				.hAlign = Foundation::HorizontalAlign::Left,
				.vAlign = Foundation::VerticalAlign::Top,
			},
		.id = args.id.c_str(),
		.zIndex = 501}));
}

void DateTimeDisplay::setDateTime(const std::string& formattedTime) {
	if (auto* text = getChild<UI::Text>(timeTextHandle)) {
		text->text = formattedTime;
	}
}

void DateTimeDisplay::setPosition(float x, float y) {
	Component::setPosition(x, y);
	if (auto* text = getChild<UI::Text>(timeTextHandle)) {
		text->setPosition(x, y);
	}
}

float DateTimeDisplay::getWidth() const {
	// Calculate width based on expected max text length and font size.
	// Uses approximate character width since the format is predictable.
	return static_cast<float>(kExpectedMaxChars) * kApproxCharWidth;
}

float DateTimeDisplay::getHeight() const {
	return UI::Theme::Typography::bodySize + 4.0F;  // Font size + padding
}

// render() inherited from Component - automatically renders all children

}  // namespace world_sim
