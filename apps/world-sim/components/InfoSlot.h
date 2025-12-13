#pragma once

// InfoSlot - Generic slot types for EntityInfoPanel
//
// Defines the building blocks for displaying entity information.
// Each slot type represents a different kind of UI element.
// Adapters convert domain data (colonist, world entity) into slots.

#include <functional>
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

/// List of text items with header: "Capabilities:" followed by bullet points
struct TextListSlot {
	std::string				 header;
	std::vector<std::string> items;
};

/// Spacing between sections
struct SpacerSlot {
	float height;
};

/// Clickable text with callback: "Tasks: ▸ Show"
struct ClickableTextSlot {
	std::string			  label;
	std::string			  value;
	std::function<void()> onClick;
};

/// Union of all slot types - adapters return vectors of these
using InfoSlot = std::variant<TextSlot, ProgressBarSlot, TextListSlot, SpacerSlot, ClickableTextSlot>;

/// Complete panel content description produced by adapters
struct PanelContent {
	std::string			  title;
	std::vector<InfoSlot> slots;
};

} // namespace world_sim
