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
	// Invalid until the first ghost update reports a placeable spot under the cursor.
	m_isValidPlacement = false;
	LOG_DEBUG(Game, "PlacementMode: selected '%s' for placement", defName.c_str());
}

void PlacementMode::cancel() {
	if (m_state == PlacementState::None) {
		return;
	}
	LOG_DEBUG(Game, "PlacementMode: cancelled from state %d", static_cast<int>(m_state));
	m_state = PlacementState::None;
	m_selectedDefName.clear();
	m_isValidPlacement = false;
}

void PlacementMode::updateGhostPosition(Foundation::Vec2 worldPos, bool valid) {
	m_ghostPosition = worldPos;
	m_isValidPlacement = valid;
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
