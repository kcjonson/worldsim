#pragma once

// ColonistListView - Left-side panel showing all colonists.
// Displays clickable portraits that select colonists.
//
// Uses ColonistListModel for data and change detection.
// Only rebuilds UI when model indicates data has changed.
//
// Now uses LayoutContainer + ColonistListItem for automatic layout.

#include "scenes/game/ui/components/ColonistListItem.h"
#include "scenes/game/ui/models/ColonistListModel.h"

#include <ecs/World.h>
#include <graphics/Rect.h>
#include <input/InputEvent.h>
#include <layout/LayoutContainer.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace world_sim {

/// Left-side panel showing all colonists with clickable portraits
class ColonistListView {
  public:
	struct Args {
		float width = 60.0F;
		float itemHeight = 50.0F;
		std::function<void(ecs::EntityID)> onColonistSelected;
		std::string id = "colonist_list";
	};

	explicit ColonistListView(const Args& args);

	/// Position the panel (top-left corner)
	void setPosition(float x, float y);

	/// Update panel using model data
	/// @param model The colonist list model (will call refresh internally)
	/// @param world ECS world for model refresh
	void update(ColonistListModel& model, ecs::World& world);

	/// Handle input event, returns true if consumed
	bool handleEvent(UI::InputEvent& event);

	/// Render the panel
	void render();

	/// Get panel bounds for layout calculations
	[[nodiscard]] Foundation::Rect getBounds() const;

  private:
	/// Rebuild all UI elements from model data
	void rebuildUI(const std::vector<adapters::ColonistData>& colonists);

	/// Update only the selection highlight (cheap operation)
	void updateSelectionHighlight(ecs::EntityID selectedId);

	/// Update mood bars from current model data
	void updateMoodBars(const std::vector<adapters::ColonistData>& colonists);

	// Configuration
	float panelWidth;
	float itemHeight;
	float panelX = 0.0F;
	float panelY = 80.0F;  // Below top overlay
	std::function<void(ecs::EntityID)> onSelectCallback;

	// Selection tracking
	ecs::EntityID selectedId{0};

	// UI elements
	std::unique_ptr<UI::Rectangle> backgroundRect;
	std::unique_ptr<UI::LayoutContainer> itemLayout;

	// Handles to items for updates
	std::vector<UI::LayerHandle> itemHandles;

	// Layout constants
	static constexpr float kPadding = 4.0F;
	static constexpr float kItemSpacing = 2.0F;
	static constexpr size_t kMaxColonists = 20;
};

}  // namespace world_sim
