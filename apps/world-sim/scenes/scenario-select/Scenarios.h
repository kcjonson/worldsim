#pragma once

// Scenario definitions for the New Game flow, ported from the UI prototype's
// mock data (docs/ui-prototype/src/data/mock.ts SCENARIOS). Placeholder until
// scenarios become data-driven.

#include <array>

namespace world_sim {

struct ScenarioDef {
	const char* id;
	const char* name;
	const char* blurb;
	int			difficulty; // 1..5
	int			partyCount;
	const char* tags;
};

inline constexpr std::array<ScenarioDef, 5> kScenarios{{
	{"standard", "Standard Colony",
	 "A balanced wreck site with workable salvage and a temperate landing band. The recommended way in.",
	 2, 3, "Balanced / Recommended"},
	{"harsh", "Harsh World",
	 "Thin atmosphere, scarce water, a climate that does not negotiate. Salvage is light. For veterans.",
	 4, 3, "Scarcity / Climate"},
	{"rich", "Rich Resources",
	 "Dense ore, lush flora, intact cargo pods scattered nearby. A forgiving economy to learn the ropes.",
	 1, 4, "Abundant / Casual"},
	{"lone", "Lone Survivor",
	 "One escape pod. One person. Everything else burned on re-entry. The hardest story we tell.",
	 5, 1, "Solo / Brutal"},
	{"expedition", "Large Expedition",
	 "A full survey crew rode the wreck down. More hands, more mouths, more politics. Sandbox-leaning.",
	 3, 8, "Sandbox / Management"},
}};

} // namespace world_sim
