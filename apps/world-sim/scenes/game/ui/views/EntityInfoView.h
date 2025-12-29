#pragma once

// EntityInfoView - UI panel showing selected entity information
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

#include "scenes/game/ui/adapters/CraftingAdapter.h"
#include "scenes/game/ui/components/InfoSlot.h"
#include "scenes/game/ui/components/NeedBar.h"
#include "scenes/game/ui/components/Selection.h"
#include "scenes/game/ui/models/EntityInfoModel.h"

#include <assets/AssetRegistry.h>
#include <assets/RecipeRegistry.h>
#include <component/Component.h>
#include <ecs/World.h>
#include <graphics/Color.h>
#include <input/InputEvent.h>
#include <layer/Layer.h>
#include <shapes/Shapes.h>

#include <functional>
#include <string>
#include <vector>

namespace world_sim {

/// UI panel for displaying selected entity information via slots
class EntityInfoView : public UI::Component {
  public:
	struct Args {
		Foundation::Vec2	  position{0.0F, 0.0F};
		float				  width = 340.0F;		// Per plan: 340px for two-column layout
		std::string			  id = "entity_info";
		std::function<void()> onClose;				// Called when close button clicked
		std::function<void()> onDetails;			// Called when Details button clicked
		QueueRecipeCallback   onQueueRecipe;		// Called when recipe is queued at station
	};

	explicit EntityInfoView(const Args& args);

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
	[[nodiscard]] float getHeight() const override { return panelHeight; }

	/// Update panel position with bottom-left alignment
	/// @param x Left edge X coordinate
	/// @param viewportHeight Total viewport height (panel bottom will align to this)
	void setBottomLeftPosition(float x, float viewportHeight);

	/// Handle input event, returns true if consumed
	bool handleEvent(UI::InputEvent& event) override;

  private:
	/// Render PanelContent by laying out slots (structure tier update)
	void renderContent(const PanelContent& content);

	/// Render single-column layout (items, flora, fauna, crafting stations)
	void renderSingleColumnLayout(const PanelContent& content, float panelY);

	/// Render two-column layout (colonists)
	void renderTwoColumnLayout(const PanelContent& content, float panelY);

	/// Update only dynamic values (progress bars) without relayout (value tier update)
	void updateValues(const PanelContent& content);

	/// Hide all slot UI elements via visibility flag
	void hideSlots();

	/// Render an individual slot at given Y offset, returns height consumed
	/// @param xOffset X offset from panel left edge (for column layouts)
	/// @param maxWidth Max width for this slot (0 = use contentWidth)
	float renderSlot(const InfoSlot& slot, float yOffset, float xOffset = 0.0F, float maxWidth = 0.0F);

	// Slot type rendering helpers
	float renderTextSlot(const TextSlot& slot, float yOffset, float xOffset);
	float renderProgressBarSlot(const ProgressBarSlot& slot, float yOffset, float xOffset, float maxWidth);
	float renderTextListSlot(const TextListSlot& slot, float yOffset, float xOffset);
	float renderSpacerSlot(const SpacerSlot& slot, float yOffset);
	float renderClickableTextSlot(const ClickableTextSlot& slot, float yOffset, float xOffset);
	float renderRecipeSlot(const RecipeSlot& slot, float yOffset);
	float renderIconSlot(const IconSlot& slot, float yOffset);

	/// Get close button top-left position for current panel position
	[[nodiscard]] Foundation::Vec2 getCloseButtonPosition(float panelY) const;

	/// Get details button top-left position for current panel position
	[[nodiscard]] Foundation::Vec2 getDetailsButtonPosition(float panelY) const;

	// ViewModel (owns selection cache, content generation)
	EntityInfoModel m_model;

	// Callbacks
	std::function<void()> onCloseCallback;
	std::function<void()> onDetailsCallback;
	QueueRecipeCallback   onQueueRecipeCallback;

	// Background panel
	UI::LayerHandle backgroundHandle;

	// Close button [X]
	UI::LayerHandle closeButtonBgHandle;
	UI::LayerHandle closeButtonTextHandle;

	// Header text (entity name/title) - used for single-column layout
	UI::LayerHandle titleHandle;

	// Colonist header elements (two-column layout)
	UI::LayerHandle portraitHandle;			 // Gray placeholder rectangle (64×64)
	UI::LayerHandle headerNameHandle;		 // "Sarah Chen, 28"
	UI::LayerHandle headerMoodBarHandle;	 // Mood bar (NeedBar with no label, uses color gradient)
	UI::LayerHandle headerMoodLabelHandle;	 // "72% Content" (right of bar)
	UI::LayerHandle needsLabelHandle;		 // "Needs:" section header

	// Centered icon (single-column layout for items/flora)
	UI::LayerHandle centeredIconHandle;	 // Icon placeholder (48×48)
	UI::LayerHandle centeredLabelHandle; // Entity name below icon

	// Details button icon (only shown for colonists)
	// Icon: "open in new window" - a small rectangle with arrow pointing out
	UI::LayerHandle detailsButtonBgHandle;
	UI::LayerHandle detailsIconLine1Handle; // Top-left to bottom-left
	UI::LayerHandle detailsIconLine2Handle; // Bottom-left to bottom-right
	UI::LayerHandle detailsIconLine3Handle; // Top-left to top-mid
	UI::LayerHandle detailsIconLine4Handle; // Arrow diagonal
	UI::LayerHandle detailsIconLine5Handle; // Arrow head part 1
	UI::LayerHandle detailsIconLine6Handle; // Arrow head part 2

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

	// Layout constants (per plan)
	static constexpr float kPadding = 12.0F;		  // Outer padding
	static constexpr float kSectionGap = 12.0F;		  // Gap between sections
	static constexpr float kItemGap = 4.0F;			  // Gap between items
	static constexpr float kNameFontSize = 14.0F;	  // Entity name (colonist name can be bigger)
	static constexpr float kLabelFontSize = 12.0F;	  // Property labels (match NeedBar)
	static constexpr float kHeaderFontSize = 12.0F;	  // Section headers
	static constexpr float kNeedBarHeight = 16.0F;	  // Need bars (slightly taller)
	static constexpr float kCloseButtonSize = 16.0F;

	// Portrait/Icon sizes
	static constexpr float kPortraitSize = 64.0F;	  // Colonist portrait
	static constexpr float kEntityIconSize = 48.0F;	  // Item/flora/fauna icon

	// Header mood bar (compact summary, next to name)
	// Note: Height intentionally differs from NeedBar::kCompactHeight (10px) for tighter header layout
	static constexpr float kHeaderMoodBarWidth = 50.0F;   // Half width - it's a summary
	static constexpr float kHeaderMoodBarHeight = 8.0F;
	static constexpr float kMoodLabelFontSize = 11.0F;	  // Slightly smaller for mood percentage text

	// Two-column layout constants (colonists)
	static constexpr float kColumnGap = 16.0F;			 // Gap between columns
	static constexpr float kLeftColumnWidth = 140.0F;	 // Fixed left column width

	// Details button layout (square icon button, same size as close button)
	static constexpr float kDetailsButtonSize = 16.0F;
	static constexpr float kButtonGap = 4.0F;			  // Gap between buttons (e.g., Details and Close)

	// Spacing constants
	static constexpr float kIconLabelGap = 8.0F;		  // Gap between icon and label below it
	static constexpr float kHeaderMoodBarOffset = 8.0F;   // Vertical offset for mood bar below name
	static constexpr float kBorderWidth = 1.0F;			  // Standard border width for UI elements

	// Recipe card layout constants
	static constexpr float kRecipeCardHeight = 58.0F;	  // Total height of recipe card
	static constexpr float kRecipeCardPadding = 10.0F;	  // Padding inside card
	static constexpr float kRecipeNameFontSize = 14.0F;	  // Recipe name text size
	static constexpr float kRecipeIngredientsFontSize = 12.0F; // Ingredients text size
	static constexpr float kRecipeQueueButtonSize = 32.0F;	  // [+] button size
	static constexpr float kRecipeCardSpacing = 8.0F;	  // Space between cards
};

} // namespace world_sim
