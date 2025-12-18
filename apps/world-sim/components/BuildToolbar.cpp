#include "BuildToolbar.h"

namespace world_sim {

namespace {
constexpr float kButtonWidth = 70.0F;
constexpr float kButtonHeight = 28.0F;
} // namespace

BuildToolbar::BuildToolbar(const Args& args)
	: m_position(args.position),
	  m_onBuildClick(args.onBuildClick) {
	m_buildButton = std::make_unique<UI::Button>(UI::Button::Args{
		.label = "Build",
		.position = m_position,
		.size = {kButtonWidth, kButtonHeight},
		.type = UI::Button::Type::Primary,
		.onClick = m_onBuildClick,
		.id = "btn_build"
	});
}

void BuildToolbar::setPosition(Foundation::Vec2 newPosition) {
	if (m_position.x == newPosition.x && m_position.y == newPosition.y) {
		return;
	}
	m_position = newPosition;
	if (m_buildButton) {
		m_buildButton->position = m_position;
		// Force button to update its internal text position
		m_buildButton->update(0.0F);
	}
}

void BuildToolbar::setActive(bool active) {
	if (m_isActive == active) {
		return;
	}
	m_isActive = active;
	updateButtonStyle();
}

void BuildToolbar::updateButtonStyle() {
	// Button type is set at construction time
	// For now, active state doesn't change appearance
	// Future: could modify button appearance directly
}

void BuildToolbar::handleInput() {
	if (m_buildButton) {
		m_buildButton->handleInput();
	}
}

void BuildToolbar::render() {
	if (m_buildButton) {
		m_buildButton->render();
	}
}

bool BuildToolbar::isPointOver(Foundation::Vec2 point) const {
	return point.x >= m_position.x && point.x <= m_position.x + kButtonWidth && point.y >= m_position.y &&
		   point.y <= m_position.y + kButtonHeight;
}

} // namespace world_sim
