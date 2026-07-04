#pragma once

// EntityInfoView - bottom-left panel showing the selected entity.
//
// Composition: an immediate-mode UI::Panel surface behind a LayoutContainer
// tree; the layout engine owns all positioning (no manual yOffset math).
// - Colonist: header row (Avatar + name + mood/task meters + eye/close
//   buttons), TabBar (Needs/Bio/Gear/Log), the active tab body, and an action
//   row (Draft / Go to / Priorities).
// - Everything else: title row + slot rows rebuilt from the EntityInfoModel's
//   InfoSlot list on structure changes, updated in place on value changes.
//
// Bottom-left anchored: height hugs content, Y derives from viewport height.
// Child components live in the component arena (stable addresses), so the
// static tree is held by typed pointers; only the generic slot rows are
// rebuilt (clearChildren + addChild) and then only on structure changes.

#include "scenes/game/ui/components/NeedBar.h"
#include "scenes/game/ui/models/EntityInfoModel.h"
#include "scenes/game/world/selection/SelectionTypes.h"

#include <assets/AssetRegistry.h>
#include <assets/RecipeRegistry.h>
#include <component/Component.h>
#include <components/button/Button.h>
#include <components/progress/ProgressBar.h>
#include <components/tabbar/TabBar.h>
#include <construction/ConstructionWorld.h>
#include <ecs/World.h>
#include <input/InputEvent.h>
#include <layout/LayoutContainer.h>
#include <shapes/Shapes.h>

#include <functional>
#include <string>
#include <vector>

namespace world_sim {

/// Callback to query remaining resource count for a world entity
using ResourceQueryCallback = std::function<std::optional<uint32_t>(const std::string& defName, Foundation::Vec2 position)>;

/// UI panel for displaying selected entity information
class EntityInfoView : public UI::Component {
  public:
	using OpenCraftingDialogCallback = std::function<void(ecs::EntityID, const std::string&)>;
	using OpenStorageConfigCallback = std::function<void(ecs::EntityID, const std::string&)>;

	struct Args {
		float				  width = 320.0F;
		std::string			  id = "entity_info";
		std::function<void()> onClose;	 // Close button clicked
		std::function<void()> onDetails; // Eye button clicked (opens the dossier)
		std::function<void(ecs::EntityID)> onToggleControl; // Draft/Release button
		std::function<void(ecs::EntityID)> onGoTo;			// Go to button (center camera)
		OpenCraftingDialogCallback onOpenCraftingDialog;
		std::function<void()>	   onPlace;
		std::function<void()>	   onMoveFurniture;
		OpenStorageConfigCallback  onOpenStorageConfig;
		ResourceQueryCallback	   queryResources;
		std::function<void()>	   onDemolishFoundation;
		std::function<void()>	   onDemolishBuilding;
		std::function<void()>	   onDemolishWallSegment;
		std::function<void()>	   onDemolishOpening;
	};

	explicit EntityInfoView(const Args& args);

	// Unhide ILayer::update(float) next to the data-refresh overload below
	using UI::Component::update;

	/// Update panel with current selection (called every frame by GameUI)
	void update(
		ecs::World&									   world,
		const engine::assets::AssetRegistry&		   assetRegistry,
		const engine::assets::RecipeRegistry&		   recipeRegistry,
		const Selection&							   selection,
		const engine::construction::ConstructionWorld* constructionWorld = nullptr,
		const ecs::RoomDetectionSystem*				   roomDetection = nullptr
	);

	[[nodiscard]] bool isVisible() const { return visible; }

	/// Update panel position with bottom-left alignment
	void setBottomLeftPosition(float x, float viewportHeight);

	void render() override;
	bool handleEvent(UI::InputEvent& event) override;
	bool containsPoint(Foundation::Vec2 point) const override;

	const char* debugTypeName() const override { return "EntityInfoView"; }
	const char* debugId() const override { return m_id.c_str(); }

  private:
	// Small leaf components defined in the .cpp
	class AvatarChip;
	class GlyphButton;
	class BadgeChip;
	class EmptyStateBox;

	/// Push model content into the component tree. structural = rebuild slot
	/// rows / reset tab; otherwise update values in place.
	void applyContent(const PanelContent& content, bool structural);
	void applyColonist(const ColonistPanelData& data, bool structural);
	void applyGeneric(const PanelContent& content, bool structural);

	/// Rebuild the generic slot rows in slot order (structure changes only)
	void rebuildSlots(const std::vector<InfoSlot>& slots);

	/// Update generic slot rows in place (value changes)
	void updateSlots(const std::vector<InfoSlot>& slots);

	/// Show the tab body matching the TabBar selection
	void setActiveTab(const std::string& tabId);

	/// Re-anchor the panel to the viewport bottom from the measured height
	void applyAnchor();

	/// Mark the layout containers dirty after content mutation
	void markLayoutDirty();

	// ViewModel (owns selection cache, content generation)
	EntityInfoModel m_model;

	// Callbacks
	std::function<void()>			   onCloseCallback;
	std::function<void()>			   onDetailsCallback;
	std::function<void(ecs::EntityID)> onToggleControlCallback;
	std::function<void(ecs::EntityID)> onGoToCallback;
	OpenCraftingDialogCallback		   onOpenCraftingDialogCallback;
	std::function<void()>			   onPlaceCallback;
	std::function<void()>			   onMoveFurnitureCallback;
	OpenStorageConfigCallback		   onOpenStorageConfigCallback;
	ResourceQueryCallback			   queryResourcesCallback;
	std::function<void()>			   onDemolishFoundationCallback;
	std::function<void()>			   onDemolishBuildingCallback;
	std::function<void()>			   onDemolishWallSegmentCallback;
	std::function<void()>			   onDemolishOpeningCallback;

	// Static component tree (arena-owned; addresses stable for the view's life)
	UI::LayoutContainer* root{nullptr};		  // vertical, padded
	UI::LayoutContainer* headerRow{nullptr};  // avatar | identity | buttons
	AvatarChip*			 avatar{nullptr};
	UI::LayoutContainer* identityCol{nullptr}; // name, subtitle, mood, task
	UI::Text*			 nameText{nullptr};
	UI::Text*			 subtitleText{nullptr};
	UI::ProgressBar*	 moodBar{nullptr};
	UI::ProgressBar*	 taskBar{nullptr};
	UI::LayoutContainer* buttonCol{nullptr}; // eye, close
	GlyphButton*		 eyeButton{nullptr};
	GlyphButton*		 closeButton{nullptr};
	UI::TabBar*			 tabBar{nullptr};
	UI::LayoutContainer* needsBody{nullptr};
	UI::LayoutContainer* bioBody{nullptr};
	UI::LayoutContainer* gearBody{nullptr};
	UI::LayoutContainer* logBody{nullptr};
	UI::LayoutContainer* actionRow{nullptr};
	UI::Button*			 draftButton{nullptr};
	UI::Button*			 goToButton{nullptr};
	UI::LayoutContainer* slotsBody{nullptr}; // generic slot rows

	// Needs tab bars (fixed pool, one per ecs::NeedType)
	std::vector<NeedBar*> needBars;

	// Bio tab rows
	UI::Text* bioAge{nullptr};
	UI::Text* bioMood{nullptr};

	// Gear tab rows
	UI::Text*				gearHands{nullptr};
	UI::LayoutContainer*	beltRow{nullptr};
	std::vector<BadgeChip*> beltChips;
	UI::Text*				beltEmpty{nullptr};
	UI::ProgressBar*		carryBar{nullptr};
	UI::Text*				gearPack{nullptr};

	// Generic slot rows (into slotsBody, grouped by slot type in slot order;
	// refreshed by rebuildSlots, mutated in place by updateSlots)
	std::vector<UI::Text*>				slotTexts;
	std::vector<NeedBar*>				slotBars;
	std::vector<UI::Button*>			slotButtons;
	std::vector<std::vector<UI::Text*>> slotListItems; // per TextListSlot
	std::vector<size_t>					slotListSizes; // item counts at build time

	// State
	std::string	  m_id;
	float		  panelWidth;
	float		  contentWidth;
	float		  panelX{0.0F};
	float		  m_viewportHeight{0.0F};
	ecs::EntityID selectedColonistId{0};

	// Layout constants
	static constexpr float kPad = 12.0F;	// UI::space_3
	static constexpr float kGap = 8.0F;		// UI::space_2
	static constexpr float kAvatarSize = 52.0F;
	static constexpr float kIconButtonSize = 18.0F;
	static constexpr float kMeterWidth = 188.0F; // identity column meters
	static constexpr float kActionButtonHeight = 26.0F;
	static constexpr float kSlotButtonHeight = 28.0F;
};

} // namespace world_sim
