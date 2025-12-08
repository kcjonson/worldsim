#pragma once

// EntityInfoPanel - UI panel showing selected entity information
//
// Displays information for different selection types:
// - NoSelection: Panel is hidden
// - ColonistSelection: Name, 4 need bars, task, action
// - WorldEntitySelection: DefName, position, capabilities
//
// Uses Container-based UI tree pattern (extends Component, uses addChild).

#include "NeedBar.h"
#include "Selection.h"

#include <assets/AssetRegistry.h>
#include <component/Component.h>
#include <ecs/World.h>
#include <graphics/Color.h>
#include <layer/Layer.h>
#include <shapes/Shapes.h>

#include <array>
#include <functional>
#include <string>

namespace world_sim {

/// UI panel for displaying selected entity information
class EntityInfoPanel : public UI::Component {
  public:
	struct Args {
		Foundation::Vec2	  position{0.0F, 0.0F};
		float				  width = 180.0F;
		std::string			  id = "entity_info";
		std::function<void()> onClose; // Called when close button clicked
	};

	explicit EntityInfoPanel(const Args& args);

	/// Update panel with current selection
	/// @param world ECS world to query for colonist data
	/// @param registry Asset registry to look up world entity capabilities
	/// @param selection Current selection state
	void update(const ecs::World& world, const engine::assets::AssetRegistry& registry, const Selection& selection);

	/// Check if panel is visible
	[[nodiscard]] bool isVisible() const { return visible; }

  private:
	/// Update display for colonist selection
	void updateColonistDisplay(const ecs::World& world, ecs::EntityID entityId);

	/// Update display for world entity selection
	void updateWorldEntityDisplay(const engine::assets::AssetRegistry& registry, const WorldEntitySelection& sel);

	/// Hide all UI elements (position offscreen)
	void hideAllElements();

	/// Show colonist-specific UI elements
	void showColonistUI();

	/// Show world entity-specific UI elements
	void showWorldEntityUI();

	// Close button callback
	std::function<void()> onCloseCallback;

	// Background panel
	UI::LayerHandle backgroundHandle;

	// Close button [X]
	UI::LayerHandle closeButtonBgHandle;
	UI::LayerHandle closeButtonTextHandle;

	// Header text (entity name)
	UI::LayerHandle nameHandle;

	// Need bars (colonist only, indexed by NeedType)
	static constexpr size_t kNeedCount = 4;
	std::array<UI::LayerHandle, kNeedCount> needBarHandles;

	// Status text handles (colonist only)
	UI::LayerHandle taskHandle;
	UI::LayerHandle actionHandle;

	// World entity info handles
	UI::LayerHandle positionHandle;
	UI::LayerHandle capabilitiesHeaderHandle;
	std::array<UI::LayerHandle, 4> capabilityHandles; // Max 4 capabilities

	// State
	bool  visible = false; // Panel hidden by default (no selection)
	float panelWidth;
	float panelHeight;

	// Cached position for layout
	Foundation::Vec2 panelPosition;

	// Cached Y offsets for content sections (set during construction)
	float colonistContentY = 0.0F;
	float worldEntityContentY = 0.0F;

	// Layout constants
	static constexpr float kPadding = 8.0F;
	static constexpr float kHeaderFontSize = 14.0F;
	static constexpr float kStatusFontSize = 11.0F;
	static constexpr float kNeedBarHeight = 14.0F;
	static constexpr float kNeedBarSpacing = 4.0F;
	static constexpr float kSectionSpacing = 8.0F;
	static constexpr float kCloseButtonSize = 16.0F;
};

} // namespace world_sim
