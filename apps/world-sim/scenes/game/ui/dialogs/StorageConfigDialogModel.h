#pragma once

// StorageConfigDialogModel - ViewModel for StorageConfigDialog
//
// Encapsulates:
// - Available items list (grouped by category)
// - Selected item for rule editing
// - Current rules for the storage container
// - Inventory counts (current stock in container)
//
// Supports per-frame refresh with change detection for live updates.

#include <assets/AssetDefinition.h>
#include <assets/AssetRegistry.h>
#include <ecs/EntityID.h>
#include <ecs/World.h>
#include <ecs/components/StorageConfiguration.h>

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace world_sim {

/// An item category in the left column tree
struct ItemCategoryGroup {
	engine::assets::ItemCategory category;
	std::string					 label;		 // Display name (e.g., "Raw Materials")
	bool						 expanded{true};
	std::vector<size_t>			 itemIndices; // Indices into availableItems
};

/// An available item in the left column
struct AvailableItem {
	std::string					 defName;	// Asset defName
	std::string					 label;		// Display name
	engine::assets::ItemCategory category;
	uint32_t					 currentCount;	  // Items in this container
	uint32_t					 requestedCount;  // Sum of max amounts from rules (0 = unlimited)
	bool						 hasRules;		  // True if any rules exist for this item
};

/// A storage rule for display in the right column
struct DisplayRule {
	size_t						   ruleIndex;  // Index in StorageConfiguration::rules
	std::string					   defName;	   // Item defName or "*"
	std::string					   label;	   // Display label
	ecs::StoragePriority		   priority;
	uint32_t					   minAmount;
	uint32_t					   maxAmount;  // 0 = unlimited
	bool						   isWildcard; // defName == "*"
	engine::assets::ItemCategory category;
	// Whether the colony knows a source to fill this box with this item (engine pull's source set).
	// Only meaningful for specific-item rules; wildcards name no single item to source, so left true.
	bool						   knownSource{true};
};

/// ViewModel for StorageConfigDialog
class StorageConfigDialogModel {
  public:
	/// Type of update needed after refresh()
	enum class UpdateType {
		None,		// No change
		Inventory,	// Item counts changed
		Rules,		// Rules changed
		Selection,	// Selected item changed
		Full,		// Container changed or initial load
	};

	/// Set the storage container to configure
	/// @param containerId ECS entity ID of the storage container
	/// @param containerDefName Asset defName (e.g., "BasicShelf")
	void setContainer(ecs::EntityID containerId, const std::string& containerDefName);

	/// Clear selection (when dialog closes)
	void clear();

	/// Refresh model from ECS world
	/// @return Type of update the dialog should perform
	/// World is non-const: the known-source check resolves storage sources via World::view, which
	/// has no const overload.
	[[nodiscard]] UpdateType refresh(ecs::World& world,
									 const engine::assets::AssetRegistry& registry);

	/// Select an item by defName (for rule editing)
	void selectItem(const std::string& defName);

	/// Deselect current item
	void clearSelection();

	// --- Rule editing (center column) ---

	/// Set priority for new rule
	void setPriority(ecs::StoragePriority priority) { pendingPriority = priority; }

	/// Set min amount for new rule
	void setMinAmount(uint32_t amount) { pendingMinAmount = amount; }

	/// Set max amount for new rule
	void setMaxAmount(uint32_t amount) { pendingMaxAmount = amount; }

	/// Set unlimited flag for new rule
	void setUnlimited(bool unlimited) { pendingUnlimited = unlimited; }

	/// Add rule for currently selected item with pending settings
	/// @return true if rule was added
	bool addRule(ecs::World& world);

	/// Add wildcard rule for currently selected category
	/// @return true if rule was added
	bool addCategoryWildcard(ecs::World& world);

	/// Remove a rule by index
	void removeRule(ecs::World& world, size_t ruleIndex);

	// --- Bulk actions ---

	/// Add wildcard rules for all categories (Select All)
	void addAllCategories(ecs::World& world);

	/// Remove all rules (Select None)
	void removeAllRules(ecs::World& world);

	// --- Getters ---

	[[nodiscard]] bool isValid() const { return valid; }
	[[nodiscard]] const std::string& containerName() const { return containerLabel; }
	[[nodiscard]] ecs::EntityID containerId() const { return currentContainerId; }
	[[nodiscard]] const std::string& containerDefName() const { return currentContainerDefName; }

	[[nodiscard]] const std::vector<ItemCategoryGroup>& categoryGroups() const { return groups; }
	[[nodiscard]] const std::vector<AvailableItem>& availableItems() const { return items; }

	[[nodiscard]] const std::string& selectedItemDefName() const { return selectedItem; }
	[[nodiscard]] const AvailableItem* selectedItemData() const;

	[[nodiscard]] const std::vector<DisplayRule>& rulesForSelectedItem() const {
		return selectedItemRules;
	}

	/// DefNames of configured specific-item rules that newly lost their known source on the last
	/// refresh() and have not yet been toasted. The view drains this to fire one warning toast per
	/// (container, item); an item that recovers a source is erased so a future loss re-toasts once.
	[[nodiscard]] const std::vector<std::string>& newlyUnknownSourceItems() const {
		return newlyUnknownItems;
	}

	// Pending rule settings (for center column form)
	[[nodiscard]] ecs::StoragePriority pendingRulePriority() const { return pendingPriority; }
	[[nodiscard]] uint32_t pendingRuleMinAmount() const { return pendingMinAmount; }
	[[nodiscard]] uint32_t pendingRuleMaxAmount() const { return pendingMaxAmount; }
	[[nodiscard]] bool pendingRuleUnlimited() const { return pendingUnlimited; }

	// Get accepted categories for this container (from asset definition)
	[[nodiscard]] const std::vector<engine::assets::ItemCategory>& acceptedCategories() const {
		return containerCategories;
	}

  private:
	/// Build available items list from registry
	void extractAvailableItems(const engine::assets::AssetRegistry& registry);

	/// Update inventory counts from container
	void updateInventoryCounts(const ecs::World& world);

	/// Compute, per configured specific-item rule, whether the colony knows a source the engine pull
	/// would use to fill this box with that item; drive the unknown-source toast de-dup from it.
	void updateKnownSources(ecs::World& world);

	/// Update rules for selected item
	void updateSelectedItemRules(const ecs::World& world);

	/// Get category display label
	static std::string getCategoryLabel(engine::assets::ItemCategory category);

	// State
	ecs::EntityID currentContainerId{0};
	std::string	  currentContainerDefName;
	std::string	  containerLabel;
	bool		  valid = false;

	// Container capabilities
	std::vector<engine::assets::ItemCategory> containerCategories;

	// Available items (left column)
	std::vector<ItemCategoryGroup> groups;
	std::vector<AvailableItem>	   items;

	// Selection
	std::string				 selectedItem;
	std::vector<DisplayRule> selectedItemRules;

	// Unknown-source toast de-dup. Items whose configured rule currently has no known source (so a
	// toast already fired and shouldn't repeat). `newlyUnknownItems` is the subset that crossed into
	// "unknown" on the last refresh; the view drains it. Both reset on container change.
	std::unordered_set<std::string> toastedUnknownSourceItems;
	std::vector<std::string>		newlyUnknownItems;

	// Per-refresh cache of defName -> hasKnownSource for every configured specific-item rule, so the
	// right-column DisplayRule build reuses the world scan refresh() already paid for.
	std::unordered_map<std::string, bool> knownSourceByDefName;

	// Pending rule settings (center column form state)
	ecs::StoragePriority pendingPriority = ecs::StoragePriority::Medium;
	uint32_t			 pendingMinAmount = 0;
	uint32_t			 pendingMaxAmount = 0;
	bool				 pendingUnlimited = true;

	// Change detection
	size_t	 prevRuleCount = 0;
	uint32_t prevTotalItems = 0;
};

} // namespace world_sim
