#include "DateTimeDisplay.h"

#include <core/RenderContext.h>
#include <theme/Theme.h>

namespace world_sim {

DateTimeDisplay::DateTimeDisplay(const Args& args)
	: position(args.position) {
	timeText = std::make_unique<UI::Text>(UI::Text::Args{
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
		.zIndex = 501
	});
}

void DateTimeDisplay::setDateTime(const std::string& formattedTime) {
	if (timeText) {
		timeText->text = formattedTime;
	}
}

void DateTimeDisplay::setPosition(Foundation::Vec2 pos) {
	position = pos;
	if (timeText) {
		timeText->setPosition(pos.x, pos.y);
	}
}

void DateTimeDisplay::render() {
	if (timeText) {
		UI::RenderContext::setZIndex(timeText->zIndex);
		timeText->render();
	}
}

float DateTimeDisplay::getWidth() const {
	// Approximate width based on typical text length
	// "Day 15, Summer | 14:32" is about 22 characters
	return 180.0F;
}

float DateTimeDisplay::getHeight() const {
	return UI::Theme::Typography::bodySize + 4.0F;  // Font size + padding
}

}  // namespace world_sim
