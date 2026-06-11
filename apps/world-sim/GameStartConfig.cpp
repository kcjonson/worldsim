#include "GameStartConfig.h"

namespace world_sim {

std::unique_ptr<GameStartConfig> GameStartConfig::s_pending;

void GameStartConfig::SetPending(std::unique_ptr<GameStartConfig> config) {
	s_pending = std::move(config);
}

std::unique_ptr<GameStartConfig> GameStartConfig::Take() {
	return std::move(s_pending);
}

bool GameStartConfig::HasPending() {
	return s_pending != nullptr;
}

} // namespace world_sim
