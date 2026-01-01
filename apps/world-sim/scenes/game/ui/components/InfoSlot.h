#pragma once

// InfoSlot - Generic slot types for EntityInfoView
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

/// Recipe card for crafting UI
/// Displays as a visual card with name, ingredients, and queue button:
/// ┌────────────────────────────────┐
/// │ Primitive Axe             [+]  │
/// │ 2× Stone, 1× Stick             │
/// └────────────────────────────────┘
struct RecipeSlot {
	std::string			  name;		   // Recipe display name (e.g., "Primitive Axe")
	std::string			  ingredients; // Required inputs (e.g., "2× Stone, 1× Stick")
	std::function<void()> onQueue;	   // Called when [+] button clicked
};

/// Centered icon for items/flora/fauna
/// Displays as centered icon with entity name below:
/// ┌────────────────────────────────┐
/// │         [Icon 48×48]           │
/// │         Berry Bush             │
/// └────────────────────────────────┘
struct IconSlot {
	std::string iconPath;	 // Path to SVG asset (empty = placeholder)
	float		size{48.0F}; // Icon size (width and height)
	std::string label;		 // Entity name displayed below icon
};

/// Action button for entity actions
/// Displays as a prominent button: [Place] or [Package]
struct ActionButtonSlot {
	std::string			  label;   // Button text (e.g., "Place", "Package")
	std::function<void()> onClick; // Callback when button clicked
};

/// Union of all slot types - adapters return vectors of these
using InfoSlot = std::variant<TextSlot, ProgressBarSlot, TextListSlot, SpacerSlot, ClickableTextSlot, RecipeSlot, IconSlot, ActionButtonSlot>;

/// Panel layout mode
enum class PanelLayout {
	SingleColumn, // Items, flora, fauna, crafting stations - simple vertical layout
	TwoColumn	  // Colonists - left column (task/gear) + right column (needs)
};

/// Colonist header data (portrait area)
struct ColonistHeader {
	std::string name;		  // "Sarah Chen"
	float		moodValue{0}; // 0-100
	std::string moodLabel;	  // "Content", "Happy", "Stressed"
};

/// Complete panel content description produced by adapters
struct PanelContent {
	std::string title; // Used for single-column layout title
	PanelLayout layout{PanelLayout::SingleColumn};

	// For SingleColumn layout: all content in 'slots'
	std::vector<InfoSlot> slots;

	// For TwoColumn layout (colonists only):
	// - header: portrait area with name/age/mood
	// - leftColumn: Current task, Next task, Gear list
	// - rightColumn: "Needs:" header + need bars
	ColonistHeader		  header;
	std::vector<InfoSlot> leftColumn;
	std::vector<InfoSlot> rightColumn;

	// Colonist-specific: callback for Details button
	std::function<void()> onDetails;

	// Furniture-specific: callbacks for Place/Package actions
	std::function<void()> onPlace;	 // Called when [Place] button clicked (for packaged furniture)
	std::function<void()> onPackage; // Called when [Package] button clicked (for placed furniture)

	// Storage-specific: callback for Configure button (opens StorageConfigDialog)
	std::function<void()> onConfigure;
};

} // namespace world_sim
