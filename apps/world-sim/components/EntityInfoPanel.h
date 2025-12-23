#pragma once

// EntityInfoPanel - UI panel showing selected entity information
//
// Uses a slot-based architecture for flexible content display:
// - Receives PanelContent from SelectionAdapter
// - Dynamically renders slots (TextSlot, ProgressBarSlot, TextListSlot)
// - Panel handles only rendering, not data transformation
//
// Performance optimization: Three-tier update system
// - Visibility tier: O(1) toggle when selection changes to/from NoSelection
// - Structure tier: Full relayout when different entity selected
// - Value tier: O(dynamic) update only for progress bars when same entity

#include "CraftingAdapter.h"
#include "InfoSlot.h"
#include "NeedBar.h"
#include "Selection.h"

#include <assets/AssetRegistry.h>
#include <assets/RecipeRegistry.h>
#include <component/Component.h>
#include <components/tabbar/TabBar.h>
#include <ecs/World.h>
#include <graphics/Color.h>
#include <input/InputEvent.h>
#include <layer/Layer.h>
#include <shapes/Shapes.h>

#include <functional>
#include <string>
#include <vector>

namespace world_sim {

/// Cached selection identity for detecting structural vs value-only updates
struct CachedSelection {
	enum class Type { None, Colonist, WorldEntity, CraftingStation };

	Type			  type = Type::None;
	ecs::EntityID	  colonistId{0};	 // For Colonist selection
	ecs::EntityID	  stationId{0};		 // For CraftingStation selection
	std::string		  worldEntityDef;	 // For WorldEntity selection
	std::string		  stationDefName;	 // For CraftingStation selection
	Foundation::Vec2 worldEntityPos;	 // For WorldEntity selection

	/// Check if this cache matches the given selection
	[[nodiscard]] bool matches(const Selection& selection) const;

	/// Update cache to match the given selection
	void update(const Selection& selection);
};

/// UI panel for displaying selected entity information via slots
class EntityInfoPanel : public UI::Component {
  public:
	struct Args {
		Foundation::Vec2	  position{0.0F, 0.0F};
		float				  width = 180.0F;
		std::string			  id = "entity_info";
		std::function<void()> onClose;			// Called when close button clicked
		std::function<void()> onTaskListToggle; // Called when task list toggle clicked
		QueueRecipeCallback   onQueueRecipe;	// Called when recipe is queued at station
	};

	explicit EntityInfoPanel(const Args& args);

	/// Update panel with current selection
	/// @param world ECS world (for adapter)
	/// @param assetRegistry Asset registry (for adapter)
	/// @param recipeRegistry Recipe registry (for crafting stations)
	/// @param selection Current selection state
	void update(
		const ecs::World& world,
		const engine::assets::AssetRegistry& assetRegistry,
		const engine::assets::RecipeRegistry& recipeRegistry,
		const Selection& selection
	);

	/// Check if panel is visible
	[[nodiscard]] bool isVisible() const { return visible; }

	/// Get current panel height (dynamic based on content)
	[[nodiscard]] float getHeight() const { return panelHeight; }

	/// Update panel position with bottom-left alignment
	/// @param x Left edge X coordinate
	/// @param viewportHeight Total viewport height (panel bottom will align to this)
	void setBottomLeftPosition(float x, float viewportHeight);

	/// Handle input event, returns true if consumed
	bool handleEvent(UI::InputEvent& event) override;

  private:
	/// Render PanelContent by laying out slots (structure tier update)
	void renderContent(const PanelContent& content);

	/// Update only dynamic values (progress bars) without relayout (value tier update)
	void updateValues(const PanelContent& content);

	/// Hide all slot UI elements via visibility flag
	void hideSlots();

	/// Render an individual slot at given Y offset, returns height consumed
	float renderSlot(const InfoSlot& slot, float yOffset);

	// Slot type rendering helpers
	float renderTextSlot(const TextSlot& slot, float yOffset);
	float renderProgressBarSlot(const ProgressBarSlot& slot, float yOffset);
	float renderTextListSlot(const TextListSlot& slot, float yOffset);
	float renderSpacerSlot(const SpacerSlot& slot, float yOffset);
	float renderClickableTextSlot(const ClickableTextSlot& slot, float yOffset);
	float renderRecipeSlot(const RecipeSlot& slot, float yOffset);

	/// Get close button top-left position for current panel position
	[[nodiscard]] Foundation::Vec2 getCloseButtonPosition(float panelY) const;

	/// Tab change callback - updates content for selected tab
	void onTabChanged(const std::string& tabId);

	/// Get content for current active tab (colonist)
	[[nodiscard]] PanelContent getContentForColonistTab(
		const ecs::World& world,
		ecs::EntityID entityId
	) const;

	// Callbacks
	std::function<void()> onCloseCallback;
	std::function<void()> onTaskListToggleCallback;
	QueueRecipeCallback   onQueueRecipeCallback;

	// Background panel
	UI::LayerHandle backgroundHandle;

	// Close button [X]
	UI::LayerHandle closeButtonBgHandle;
	UI::LayerHandle closeButtonTextHandle;

	// Header text (entity name/title)
	UI::LayerHandle titleHandle;

	// Tab bar (only shown for colonists)
	UI::LayerHandle tabBarHandle;
	std::string m_activeTab = "status";	 // Current tab: "status" or "inventory"
	bool m_showTabs = false;			 // True for colonists, false for world entities
	bool m_tabChangeRequested = false;	 // Set by onTabChanged to trigger re-render

	// Pool of reusable slot UI elements
	// Text elements (for TextSlot label:value pairs)
	static constexpr size_t kMaxTextSlots = 8;
	std::vector<UI::LayerHandle> textHandles;

	// Progress bars (for ProgressBarSlot)
	static constexpr size_t kMaxProgressBars = 12; // Mood + all needs
	std::vector<UI::LayerHandle> progressBarHandles;

	// List items (for TextListSlot)
	static constexpr size_t kMaxListItems = 8;
	UI::LayerHandle listHeaderHandle;
	std::vector<UI::LayerHandle> listItemHandles;

	// Clickable text (for ClickableTextSlot)
	UI::LayerHandle clickableTextHandle;
	std::function<void()> clickableCallback;
	Foundation::Vec2 clickableBoundsMin;
	Foundation::Vec2 clickableBoundsMax;

	// Recipe cards (for RecipeSlot)
	static constexpr size_t kMaxRecipeCards = 8;
	struct RecipeCardHandles {
		UI::LayerHandle background;
		UI::LayerHandle nameText;
		UI::LayerHandle ingredientsText;
		UI::LayerHandle queueButton;
		UI::LayerHandle queueButtonText;
	};
	std::vector<RecipeCardHandles> recipeCardHandles;
	std::vector<std::function<void()>> recipeCallbacks;
	std::vector<Foundation::Rect> recipeButtonBounds;

	// Pool indices (track which elements are in use)
	size_t usedTextSlots = 0;
	size_t usedProgressBars = 0;
	size_t usedListItems = 0;
	size_t usedRecipeCards = 0;

	// State (note: visible is inherited from IComponent)
	float panelWidth;
	float panelHeight;
	float contentWidth;

	// Cached position for layout (X is left edge, Y computed from viewportHeight)
	float panelX{0.0F};
	float m_viewportHeight{0.0F};

	// Cached selection for detecting structure vs value updates
	CachedSelection m_cachedSelection;

	// Layout constants
	static constexpr float kPadding = 8.0F;
	static constexpr float kTitleFontSize = 14.0F;
	static constexpr float kTextFontSize = 11.0F;
	static constexpr float kProgressBarHeight = 14.0F;
	static constexpr float kLineSpacing = 4.0F;
	static constexpr float kCloseButtonSize = 16.0F;
	static constexpr float kTabBarHeight = 24.0F;  // Height of tab bar when shown

	// Recipe card layout constants
	static constexpr float kRecipeCardHeight = 58.0F;	  // Total height of recipe card
	static constexpr float kRecipeCardPadding = 10.0F;	  // Padding inside card
	static constexpr float kRecipeNameFontSize = 14.0F;	  // Recipe name text size
	static constexpr float kRecipeIngredientsFontSize = 12.0F; // Ingredients text size
	static constexpr float kRecipeQueueButtonSize = 32.0F;	  // [+] button size
	static constexpr float kRecipeCardSpacing = 8.0F;	  // Space between cards
};

} // namespace world_sim
