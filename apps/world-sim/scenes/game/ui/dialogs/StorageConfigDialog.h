#pragma once

// StorageConfigDialog - Three-column dialog for storage container configuration
//
// Layout:
// - Left column: Available items grouped by category (tree view with counts)
// - Center column: Rule configuration form (priority, min/max, add buttons)
// - Right column: Rules for selected item (with delete buttons)
//
// Changes take effect immediately (no Apply/Cancel).
// Game continues running while dialog is open.

#include "StorageConfigDialogModel.h"

#include <component/Component.h>
#include <components/button/Button.h>
#include <components/dialog/Dialog.h>
#include <components/scroll/ScrollContainer.h>
#include <components/select/Select.h>
#include <ecs/EntityID.h>
#include <ecs/World.h>
#include <layout/LayoutContainer.h>

#include <functional>
#include <string>
#include <vector>

namespace world_sim {

/// Callback type for opening storage configuration dialog
using OpenStorageConfigCallback =
	std::function<void(ecs::EntityID containerId, const std::string& containerDefName)>;

class StorageConfigDialog : public UI::Component {
  public:
	struct Args {
		std::function<void()> onClose;
	};

	explicit StorageConfigDialog(const Args& args);
	~StorageConfigDialog() override = default;

	// Disable copy
	StorageConfigDialog(const StorageConfigDialog&) = delete;
	StorageConfigDialog& operator=(const StorageConfigDialog&) = delete;

	// Allow move
	StorageConfigDialog(StorageConfigDialog&&) noexcept = default;
	StorageConfigDialog& operator=(StorageConfigDialog&&) noexcept = default;

	// Open dialog for a specific storage container
	void open(ecs::EntityID containerId, const std::string& containerDefName,
			  float screenWidth, float screenHeight);

	// Close dialog
	void close();

	// Query state
	[[nodiscard]] bool isOpen() const;
	[[nodiscard]] ecs::EntityID getContainerId() const { return model.containerId(); }

	// Per-frame update with ECS world for live data
	void update(ecs::World& world,
				const engine::assets::AssetRegistry& registry,
				float deltaTime);

	// IComponent overrides
	void render() override;
	bool handleEvent(UI::InputEvent& event) override;
	bool containsPoint(Foundation::Vec2 point) const override;

  private:
	// Dialog dimensions
	static constexpr float kDialogWidth = 720.0F;
	static constexpr float kDialogHeight = 480.0F;
	static constexpr float kColumnGap = 12.0F;
	static constexpr float kContentPadding = 12.0F;

	// Column widths
	static constexpr float kLeftColumnWidth = 200.0F;
	static constexpr float kRightColumnWidth = 200.0F;
	// Center column fills remaining space

	// Callbacks
	std::function<void()> onCloseCallback;

	// Model
	StorageConfigDialogModel model;

	// ECS world reference (set during update, used by event handlers)
	ecs::World* worldPtr = nullptr;

	// Child component handles
	UI::LayerHandle dialogHandle;
	UI::LayerHandle contentLayoutHandle;

	// Left column - available items
	UI::LayerHandle leftColumnHandle;
	int				itemHoveredIndex = -1;
	int				itemSelectedIndex = -1;
	std::vector<int> expandedCategories; // Indices of expanded category groups

	// Center column - rule configuration
	UI::LayerHandle centerColumnHandle;
	UI::LayerHandle prioritySelectHandle;
	UI::LayerHandle minAmountHandle;
	UI::LayerHandle maxAmountHandle;
	UI::LayerHandle unlimitedCheckHandle;
	UI::LayerHandle addRuleButtonHandle;
	UI::LayerHandle addAllButtonHandle;

	// Right column - rules for selected item
	UI::LayerHandle					 rightColumnHandle;
	std::vector<UI::LayerHandle> ruleDeleteHandles;

	// Track if content has been created
	bool contentCreated = false;
	bool needsInitialRebuild = false;
	bool needsCenterRebuild = false;
	bool needsRulesRebuild = false;

	// Internal methods
	void createDialog();
	void createColumns();
	void rebuildLeftColumn();
	void rebuildCenterColumn();
	void rebuildRulesColumn();

	// Item list rendering (direct rendering like CraftingDialog)
	void renderItemList();
	[[nodiscard]] int getItemIndexAtPosition(Foundation::Vec2 pos) const;
	[[nodiscard]] Foundation::Rect getItemBounds(int flatIndex) const;
	[[nodiscard]] Foundation::Rect getCategoryHeaderBounds(int categoryIndex) const;

	// Event handlers
	void handleItemClick(int flatIndex);
	void handleCategoryToggle(int categoryIndex);
	void handleAddRule();
	void handleAddAll();
	void handleRemoveRule(size_t ruleIndex);
	void handleSelectAll();
	void handleSelectNone();

	// Helper to access content layout
	UI::LayoutContainer* getContentLayout();

	// Build flat list of visible items (respecting expand/collapse)
	struct FlatItem {
		enum class Type { CategoryHeader, Item };
		Type   type;
		size_t index; // Category index or item index
	};
	std::vector<FlatItem> flatItems;
	void				  rebuildFlatList();
};

} // namespace world_sim
