// GameWorldState.cpp - Implementation of static state holder

#include "GameWorldState.h"

namespace world_sim {

	// Static member definition
	std::unique_ptr<GameWorldState> GameWorldState::s_pending = nullptr;

	void GameWorldState::SetPending(std::unique_ptr<GameWorldState> state) {
		s_pending = std::move(state);
	}

	std::unique_ptr<GameWorldState> GameWorldState::Take() {
		return std::move(s_pending);
	}

	bool GameWorldState::HasPending() {
		return s_pending != nullptr;
	}

} // namespace world_sim
