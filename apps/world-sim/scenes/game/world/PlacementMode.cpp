#include "PlacementMode.h"

#include <utils/Log.h>

namespace world_sim {

PlacementMode::PlacementMode(const Args& args)
	: m_onPlace(args.onPlace) {}

void PlacementMode::enterMenu() {
	if (m_state != PlacementState::None) {
		return;
	}
	m_state = PlacementState::MenuOpen;
	m_selectedDefName.clear();
	LOG_DEBUG(Game, "PlacementMode: entered menu");
}

void PlacementMode::selectItem(const std::string& defName) {
	// Allow selection from None (direct dropdown) or MenuOpen (build menu flow)
	if (m_state != PlacementState::None && m_state != PlacementState::MenuOpen) {
		return;
	}
	if (defName.empty()) {
		return;
	}
	m_selectedDefName = defName;
	m_state = PlacementState::Placing;
	m_isValidPlacement = true;
	LOG_DEBUG(Game, "PlacementMode: selected '%s' for placement", defName.c_str());
}

void PlacementMode::cancel() {
	if (m_state == PlacementState::None) {
		return;
	}
	LOG_DEBUG(Game, "PlacementMode: cancelled from state %d", static_cast<int>(m_state));
	m_state = PlacementState::None;
	m_selectedDefName.clear();
	m_isValidPlacement = true;
}

void PlacementMode::updateGhostPosition(Foundation::Vec2 worldPos) {
	m_ghostPosition = worldPos;
	// For now, all positions are valid
	// Future: Check for obstacles, terrain validity, etc.
	m_isValidPlacement = true;
}

bool PlacementMode::tryPlace() {
	if (m_state != PlacementState::Placing) {
		return false;
	}

	if (!m_isValidPlacement) {
		LOG_DEBUG(Game, "PlacementMode: invalid placement position");
		return false;
	}
	if (!m_onPlace) {
		LOG_WARNING(Game, "PlacementMode: no place callback registered");
		return false;
	}

	LOG_DEBUG(Game, "PlacementMode: placing '%s' at (%.1f, %.1f)", m_selectedDefName.c_str(), m_ghostPosition.x, m_ghostPosition.y);

	m_onPlace(m_selectedDefName, m_ghostPosition);

	// Return to normal state after placing
	m_state = PlacementState::None;
	m_selectedDefName.clear();
	return true;
}

} // namespace world_sim
