#pragma once

// EntityInfoPanel - UI panel showing selected entity information
//
// Uses a slot-based architecture for flexible content display:
// - Receives PanelContent from SelectionAdapter
// - Dynamically renders slots (TextSlot, ProgressBarSlot, TextListSlot)
// - Panel handles only rendering, not data transformation

#include "InfoSlot.h"
#include "NeedBar.h"
#include "Selection.h"

#include <assets/AssetRegistry.h>
#include <component/Component.h>
#include <ecs/World.h>
#include <graphics/Color.h>
#include <layer/Layer.h>
#include <shapes/Shapes.h>

#include <functional>
#include <string>
#include <vector>

namespace world_sim {

/// UI panel for displaying selected entity information via slots
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
	/// @param world ECS world (for adapter)
	/// @param registry Asset registry (for adapter)
	/// @param selection Current selection state
	void update(const ecs::World& world, const engine::assets::AssetRegistry& registry, const Selection& selection);

	/// Check if panel is visible
	[[nodiscard]] bool isVisible() const { return visible; }

  private:
	/// Render PanelContent by laying out slots
	void renderContent(const PanelContent& content);

	/// Clear all slot UI elements (position offscreen)
	void clearSlots();

	/// Render an individual slot at given Y offset, returns height consumed
	float renderSlot(const InfoSlot& slot, float yOffset);

	// Slot type rendering helpers
	float renderTextSlot(const TextSlot& slot, float yOffset);
	float renderProgressBarSlot(const ProgressBarSlot& slot, float yOffset);
	float renderTextListSlot(const TextListSlot& slot, float yOffset);
	float renderSpacerSlot(const SpacerSlot& slot, float yOffset);

	// Close button callback
	std::function<void()> onCloseCallback;

	// Background panel
	UI::LayerHandle backgroundHandle;

	// Close button [X]
	UI::LayerHandle closeButtonBgHandle;
	UI::LayerHandle closeButtonTextHandle;

	// Header text (entity name/title)
	UI::LayerHandle titleHandle;

	// Pool of reusable slot UI elements
	// Text elements (for TextSlot label:value pairs)
	static constexpr size_t kMaxTextSlots = 8;
	std::vector<UI::LayerHandle> textHandles;

	// Progress bars (for ProgressBarSlot)
	static constexpr size_t kMaxProgressBars = 6;
	std::vector<UI::LayerHandle> progressBarHandles;

	// List items (for TextListSlot)
	static constexpr size_t kMaxListItems = 8;
	UI::LayerHandle listHeaderHandle;
	std::vector<UI::LayerHandle> listItemHandles;

	// Pool indices (track which elements are in use)
	size_t usedTextSlots = 0;
	size_t usedProgressBars = 0;
	size_t usedListItems = 0;

	// State
	bool  visible = false;
	float panelWidth;
	float panelHeight;
	float contentWidth;

	// Cached position for layout
	Foundation::Vec2 panelPosition;

	// Layout constants
	static constexpr float kPadding = 8.0F;
	static constexpr float kTitleFontSize = 14.0F;
	static constexpr float kTextFontSize = 11.0F;
	static constexpr float kProgressBarHeight = 14.0F;
	static constexpr float kLineSpacing = 4.0F;
	static constexpr float kCloseButtonSize = 16.0F;
	static constexpr float kHiddenY = -10000.0F;
};

} // namespace world_sim
