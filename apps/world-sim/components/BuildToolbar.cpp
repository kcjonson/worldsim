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

bool BuildToolbar::handleEvent(UI::InputEvent& event) {
	if (m_buildButton) {
		return m_buildButton->handleEvent(event);
	}
	return false;
}

void BuildToolbar::render() {
	if (m_buildButton) {
		m_buildButton->render();
	}
}

} // namespace world_sim
