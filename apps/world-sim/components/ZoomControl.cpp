#include "ZoomControl.h"

#include <sstream>

namespace world_sim {

namespace {
constexpr float kButtonSize = 28.0F;
constexpr float kTextWidth = 50.0F;
constexpr float kSpacing = 4.0F;
constexpr float kFontSize = 14.0F;
}  // namespace

ZoomControl::ZoomControl(const Args& args) : m_position(args.position) {
	float x = m_position.x;
	float y = m_position.y;

	// Zoom out button (-)
	m_zoomOutButton = std::make_unique<UI::Button>(UI::Button::Args{
		.label = "-",
		.position = {x, y},
		.size = {kButtonSize, kButtonSize},
		.type = UI::Button::Type::Primary,
		.onClick = args.onZoomOut,
		.id = "btn_zoom_out"
	});
	x += kButtonSize + kSpacing;

	// Zoom percentage text (centered between buttons)
	m_zoomText = std::make_unique<UI::Text>(UI::Text::Args{
		.position = {x + kTextWidth * 0.5F, y + kButtonSize * 0.5F},
		.text = "100%",
		.style = {
			.color = Foundation::Color::white(),
			.fontSize = kFontSize,
			.hAlign = Foundation::HorizontalAlign::Center,
			.vAlign = Foundation::VerticalAlign::Middle,
		},
		.id = "zoom_text"
	});
	x += kTextWidth + kSpacing;

	// Zoom in button (+)
	m_zoomInButton = std::make_unique<UI::Button>(UI::Button::Args{
		.label = "+",
		.position = {x, y},
		.size = {kButtonSize, kButtonSize},
		.type = UI::Button::Type::Primary,
		.onClick = args.onZoomIn,
		.id = "btn_zoom_in"
	});
}

void ZoomControl::setZoomPercent(int percent) {
	if (m_zoomPercent != percent) {
		m_zoomPercent = percent;
		updateZoomText();
	}
}

void ZoomControl::setPosition(Foundation::Vec2 position) {
	if (m_position.x == position.x && m_position.y == position.y) {
		return;
	}
	m_position = position;

	float x = m_position.x;
	float y = m_position.y;

	// Update button positions
	if (m_zoomOutButton) {
		m_zoomOutButton->position = {x, y};
	}
	x += kButtonSize + kSpacing;

	// Text is recreated in updateZoomText(), just update position reference
	x += kTextWidth + kSpacing;

	if (m_zoomInButton) {
		m_zoomInButton->position = {x, y};
	}

	// Update text position
	updateZoomText();
}

void ZoomControl::updateZoomText() {
	std::ostringstream oss;
	oss << m_zoomPercent << "%";

	float x = m_position.x + kButtonSize + kSpacing;
	float y = m_position.y;

	m_zoomText = std::make_unique<UI::Text>(UI::Text::Args{
		.position = {x + kTextWidth * 0.5F, y + kButtonSize * 0.5F},
		.text = oss.str(),
		.style = {
			.color = Foundation::Color::white(),
			.fontSize = kFontSize,
			.hAlign = Foundation::HorizontalAlign::Center,
			.vAlign = Foundation::VerticalAlign::Middle,
		},
		.id = "zoom_text"
	});
}

void ZoomControl::handleInput() {
	if (m_zoomOutButton) {
		m_zoomOutButton->handleInput();
	}
	if (m_zoomInButton) {
		m_zoomInButton->handleInput();
	}
}

void ZoomControl::render() {
	if (m_zoomOutButton) {
		m_zoomOutButton->update(0.0F);  // Update label position/style
		m_zoomOutButton->render();
	}
	if (m_zoomText) {
		m_zoomText->render();
	}
	if (m_zoomInButton) {
		m_zoomInButton->update(0.0F);  // Update label position/style
		m_zoomInButton->render();
	}
}

}  // namespace world_sim
