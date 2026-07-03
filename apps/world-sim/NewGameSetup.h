#pragma once

// NewGameSetup - session-scoped state for the New Game flow.
// ScenarioSelect stores the scenario, PartySelect stores the crew, and
// WorldCreatorScene::land() copies the party into the GameStartConfig handoff.
// Unlike GameStartConfig's SetPending/Take, this persists across the whole
// three-step flow (Back navigation must not lose selections); the main menu's
// "New Game" entry resets it.

#include <string>
#include <unordered_map>
#include <vector>

namespace world_sim {

struct PartyMember {
	std::string name;
	std::string role;
	std::string origin;
	int			age = 0;
	float		mood = 1.0F; // 0..1
	// Skill defName -> level (0-20). Applied via ecs::Skills at spawn.
	std::unordered_map<std::string, float> skills;
	// Traits and backstory are display-only for now (no backing ECS components).
	std::vector<std::string> traits;
	std::string				 backstory;
};

struct NewGameSetup {
	std::string				 scenarioId;
	std::vector<PartyMember> party;

	static NewGameSetup& Get();
	static void			 Reset();

  private:
	static NewGameSetup s_instance;
};

} // namespace world_sim
