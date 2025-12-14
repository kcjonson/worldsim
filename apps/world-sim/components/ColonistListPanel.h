#pragma once

// ColonistListPanel - Left-side panel showing all colonists.
// Displays clickable portraits that select colonists.

#include "Selection.h"

#include <ecs/World.h>
#include <ecs/components/Colonist.h>
#include <graphics/Rect.h>
#include <shapes/Shapes.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace world_sim {

/// Individual colonist portrait item
struct ColonistItem {
	ecs::EntityID entityId;
	std::string name;
};

/// Left-side panel showing all colonists with clickable portraits
class ColonistListPanel {
  public:
	struct Args {
		float width = 60.0F;
		float itemHeight = 50.0F;
		std::function<void(ecs::EntityID)> onColonistSelected;
		std::string id = "colonist_list";
	};

	explicit ColonistListPanel(const Args& args);

	/// Position the panel (top-left corner)
	void setPosition(float x, float y);

	/// Update colonist list from ECS world
	/// Note: Uses const_cast internally since ecs::World::view() is not const
	void update(const ecs::World& world, ecs::EntityID selectedColonistId);

	/// Handle input (clicks on portraits)
	/// @return true if input was consumed
	bool handleInput();

	/// Render the panel
	void render();

	/// Get panel bounds for hit testing
	[[nodiscard]] Foundation::Rect getBounds() const;

  private:
	// Configuration
	float panelWidth;
	float itemHeight;
	float panelX = 0.0F;
	float panelY = 80.0F;  // Below top overlay
	std::function<void(ecs::EntityID)> onSelectCallback;

	// Cached colonist data
	std::vector<ColonistItem> colonists;
	ecs::EntityID selectedId{0};

	// UI elements
	std::unique_ptr<UI::Rectangle> backgroundRect;
	std::vector<std::unique_ptr<UI::Rectangle>> itemBackgrounds;
	std::vector<std::unique_ptr<UI::Text>> itemNames;

	// Layout constants
	static constexpr float kPadding = 4.0F;
	static constexpr float kItemSpacing = 2.0F;
	static constexpr size_t kMaxColonists = 20;
};

}  // namespace world_sim
