#pragma once

// CraftingDialog - Three-column dialog for recipe selection and queue management
//
// Layout:
// - Left column: Scrollable recipe list with craftability indicators
// - Center column: Selected recipe details, quantity picker, "Add to Queue" button
// - Right column: Current job progress + queue with cancel buttons
//
// The dialog stays open after adding items (batch mode).
// Game continues running while dialog is open - queue updates in real-time.

#include "CraftingDialogModel.h"
#include "../adapters/CraftingAdapter.h"  // For QueueRecipeCallback

#include <component/Component.h>
#include <components/button/Button.h>
#include <components/dialog/Dialog.h>
#include <components/scroll/ScrollContainer.h>
#include <ecs/EntityID.h>
#include <ecs/World.h>
#include <layout/LayoutContainer.h>

#include <functional>
#include <string>
#include <vector>

namespace world_sim {

class CraftingDialog : public UI::Component {
  public:
	struct Args {
		std::function<void()> onClose;
		QueueRecipeCallback onQueueRecipe;  ///< Called with recipe and quantity to add to queue
		std::function<void(const std::string& recipeDefName)> onCancelJob;
	};

	explicit CraftingDialog(const Args& args);
	~CraftingDialog() override = default;

	// Disable copy
	CraftingDialog(const CraftingDialog&) = delete;
	CraftingDialog& operator=(const CraftingDialog&) = delete;

	// Allow move
	CraftingDialog(CraftingDialog&&) noexcept = default;
	CraftingDialog& operator=(CraftingDialog&&) noexcept = default;

	// Open dialog for a specific crafting station
	void open(ecs::EntityID stationId, const std::string& stationDefName,
	          float screenWidth, float screenHeight);

	// Close dialog
	void close();

	// Query state
	[[nodiscard]] bool isOpen() const;
	[[nodiscard]] ecs::EntityID getStationId() const { return model.stationId(); }

	// Per-frame update with ECS world for live queue data
	void update(const ecs::World& world,
	            const engine::assets::RecipeRegistry& registry,
	            float deltaTime);

	// IComponent overrides
	void render() override;
	bool handleEvent(UI::InputEvent& event) override;
	bool containsPoint(Foundation::Vec2 point) const override;

  private:
	// Dialog dimensions
	static constexpr float kDialogWidth = 620.0F;
	static constexpr float kDialogHeight = 450.0F;
	static constexpr float kColumnGap = 12.0F;
	static constexpr float kContentPadding = 12.0F;

	// Column widths
	static constexpr float kLeftColumnWidth = 160.0F;
	static constexpr float kRightColumnWidth = 180.0F;
	// Center column fills remaining space

	// Callbacks
	std::function<void()> onCloseCallback;
	QueueRecipeCallback onQueueRecipeCallback;
	std::function<void(const std::string&)> onCancelJobCallback;

	// Model
	CraftingDialogModel model;

	// Child component handles
	UI::LayerHandle dialogHandle;
	UI::LayerHandle contentLayoutHandle;  // Horizontal layout for columns

	// Left column - recipe list (rendered directly like TabBar)
	UI::LayerHandle leftColumnHandle;
	int recipeHoveredIndex = -1;   // Which recipe is hovered (-1 = none)
	int recipeSelectedIndex = -1;  // Which recipe is selected (-1 = none)

	// Center column - recipe details
	UI::LayerHandle centerColumnHandle;
	UI::LayerHandle addToQueueHandle;

	// Right column - queue
	UI::LayerHandle rightColumnHandle;
	std::vector<UI::LayerHandle> queueItemHandles;

	// Track if content has been created
	bool contentCreated = false;
	bool needsInitialRebuild = false;
	bool needsCenterRebuild = false;  // Set when selection changes, cleared after rebuild

	// Internal methods
	void createDialog();
	void createColumns();
	void rebuildCenterColumn();
	void rebuildQueueColumn();

	// Recipe list rendering (TabBar-style direct rendering)
	void renderRecipeList();
	[[nodiscard]] int getRecipeIndexAtPosition(Foundation::Vec2 pos) const;
	[[nodiscard]] Foundation::Rect getRecipeItemBounds(int index) const;

	void handleRecipeClick(int recipeIndex);
	void handleQuantityChange(int delta);
	void handleAddToQueue();
	void handleCancelJob(const std::string& recipeDefName);

	// Helper to access content layout (columns are children of this)
	UI::LayoutContainer* getContentLayout();
};

} // namespace world_sim
