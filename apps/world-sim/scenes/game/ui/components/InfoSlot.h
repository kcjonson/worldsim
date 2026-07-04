#pragma once

// InfoSlot - Generic slot types for EntityInfoView
//
// Building blocks for entity information display. Adapters convert domain data
// (stations, storage, construction, world entities) into slot lists; colonists
// get the richer ColonistPanelData that drives the tabbed layout.

#include <ecs/EntityID.h>

#include <functional>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace world_sim {

/// Text with label: "Task: Going to eat"
struct TextSlot {
	std::string label;
	std::string value;
};

/// Progress bar with label and 0-100 value: "Hunger: [====    ]"
struct ProgressBarSlot {
	std::string label;
	float		value; // 0.0 to 100.0
};

/// List of text items with header: "Work Orders:" followed by bullet points
struct TextListSlot {
	std::string				 header;
	std::vector<std::string> items;
};

/// Action button for entity actions: [Place], [Demolish], [Open Crafting Menu]
struct ActionButtonSlot {
	std::string			  label;
	std::function<void()> onClick;
};

/// Union of all slot types - adapters return vectors of these
using InfoSlot = std::variant<TextSlot, ProgressBarSlot, TextListSlot, ActionButtonSlot>;

/// Colonist data for the tabbed panel: header (name/mood/task) + Needs/Bio/Gear
/// tabs + the Draft/Go-to action row. Produced by adaptColonistStatus.
struct ColonistPanelData {
	ecs::EntityID id{0};
	std::string	  name;
	float		  moodValue{0.0F}; // 0-100
	std::string	  moodLabel;	   // "Content", "Happy", ...
	std::string	  currentTask;	   // header task meter label ("Idle" when none)
	float		  taskProgress{-1.0F}; // 0..1 while an action runs, <0 otherwise
	bool		  controlled{false};   // direct player control (drives Draft/Release)
	std::string	  age{"--"};		   // placeholder until an age system exists

	std::vector<ProgressBarSlot> needs;

	// Gear (from ecs::Inventory)
	std::string				 hands; // "(empty)" | "L: X  R: Y" | "Axe x2 (both hands)"
	std::vector<std::string> belt;	// filled belt slots
	std::vector<std::string> backpack; // "Item xN" lines
	float					 carriedKg{0.0F};
	float					 capacityKg{0.0F};
};

/// Complete panel content description produced by adapters
struct PanelContent {
	std::string title;
	std::string subtitle; // dim line under the title (world entities: resource status)

	// Generic entities: slots rendered top-to-bottom
	std::vector<InfoSlot> slots;

	// Colonist selection: tabbed layout data (slots unused)
	std::optional<ColonistPanelData> colonist;
};

} // namespace world_sim
