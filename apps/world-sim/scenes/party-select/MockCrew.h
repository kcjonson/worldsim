#pragma once

// Mock crew for the party-select stub, ported from the UI prototype
// (docs/ui-prototype/src/data/mock.ts COLONISTS + RESERVE_COLONISTS), plus a
// name/role pool that seeds roster slots beyond the pool and feeds Randomize
// rerolls. Placeholder until crew becomes data-driven. Skill names follow the
// engine's defNames (Farming, not the prototype's Growing).

#include <array>
#include <cstddef>

namespace world_sim {

struct CrewSkillDef {
	const char* name;
	float		level; // 0..20
};

enum class TraitTone { Good, Bad, Neutral };

struct CrewTraitDef {
	const char* name; // nullptr -> unused slot
	TraitTone	tone;
};

struct CrewDef {
	const char* name;
	const char* role;
	const char* origin;
	int			age;
	float		mood; // 0..1
	const char* backstory;
	std::array<CrewSkillDef, 6> skills;
	std::array<CrewTraitDef, 3> traits;
};

inline constexpr std::array<CrewDef, 4> kCrewPool{{
	{"Mara Vance", "Flight Engineer", "Outpost 28-B", 34, 0.72F,
	 "Twelve years keeping prospecting rigs in the black. Knows which welds hold and which are "
	 "lying to you. Took the expedition to outrun a debt she will not discuss.",
	 {{{"Construction", 14.0F}, {"Crafting", 11.0F}, {"Mining", 9.0F},
	   {"Medicine", 4.0F}, {"Cooking", 6.0F}, {"Research", 8.0F}}},
	 {{{"Steady Hands", TraitTone::Good}, {"Insomniac", TraitTone::Bad}, {"Hauler", TraitTone::Neutral}}}},
	{"Idris Okonkwo", "Field Medic", "Kessler Station", 41, 0.58F,
	 "A trauma surgeon who left a good hospital for reasons that made sense at the time. Calm in "
	 "a crisis, restless in the quiet between them.",
	 {{{"Medicine", 16.0F}, {"Research", 12.0F}, {"Cooking", 9.0F},
	   {"Social", 11.0F}, {"Construction", 3.0F}, {"Mining", 2.0F}}},
	 {{{"Compassionate", TraitTone::Good}, {"Squeamish (Mining)", TraitTone::Bad}, {nullptr, TraitTone::Neutral}}}},
	{"Rin Calloway", "Botanist", "Greenhouse Collective", 28, 0.81F,
	 "Grew food in places food has no business growing. Talks to plants, and the unsettling part "
	 "is that it seems to work. Optimist by stubbornness.",
	 {{{"Farming", 15.0F}, {"Cooking", 12.0F}, {"Research", 10.0F},
	   {"Animals", 8.0F}, {"Medicine", 6.0F}, {"Construction", 5.0F}}},
	 {{{"Green Thumb", TraitTone::Good}, {"Optimist", TraitTone::Good}, {"Frail", TraitTone::Bad}}}},
	{"Dex Aludra", "Security", "Belt Patrol", 37, 0.49F,
	 "Carried a badge in three jurisdictions, kept it in none. Reliable when things go loud, "
	 "prickly when they don't.",
	 {{{"Shooting", 14.0F}, {"Melee", 11.0F}, {"Construction", 7.0F},
	   {"Mining", 9.0F}, {"Social", 4.0F}, {"Medicine", 5.0F}}},
	 {{{"Trigger-Steady", TraitTone::Good}, {"Abrasive", TraitTone::Bad}, {nullptr, TraitTone::Neutral}}}},
}};

// Reroll pools. Names must stay disjoint from kCrewPool's and large enough to
// cover the biggest scenario party (8) rerolling every slot.
inline constexpr std::array<const char*, 12> kRerollNames{
	"Joon Barta", "Vale Okiro", "Sefa Lindqvist", "Bram Holt",
	"Nia Solano", "Kellan Dray", "Petra Voss", "Ondrej Silt",
	"Tamsin Reyes", "Callum Iri", "Yusuf Andrade", "Freya Malin"};
inline constexpr std::array<const char*, 8> kRerollRoles{
	"Geologist", "Rigger", "Navigator", "Quartermaster",
	"Surveyor", "Mechanic", "Comms Officer", "Hydroponicist"};

} // namespace world_sim
