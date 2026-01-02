#include "StorageConfigDialogModel.h"

#include <ecs/components/Inventory.h>
#include <ecs/components/StorageConfiguration.h>

#include <algorithm>
#include <cctype>

namespace world_sim {

void StorageConfigDialogModel::setContainer(ecs::EntityID containerId,
											const std::string& containerDefName) {
	currentContainerId = containerId;
	currentContainerDefName = containerDefName;

	// Create human-readable label from defName
	// e.g., "BasicShelf" -> "Basic Shelf"
	std::string result;
	result.reserve(containerDefName.size() + 4);
	for (size_t i = 0; i < containerDefName.size(); ++i) {
		char c = containerDefName[i];
		if (i > 0 && std::isupper(static_cast<unsigned char>(c)) != 0) {
			result += ' ';
		}
		result += c;
	}
	containerLabel = std::move(result);

	// Reset selection and pending form state
	selectedItem.clear();
	selectedItemRules.clear();
	pendingPriority = ecs::StoragePriority::Medium;
	pendingMinAmount = 0;
	pendingMaxAmount = 0;
	pendingUnlimited = true;
	valid = false;

	// Clear cached data
	groups.clear();
	items.clear();
	containerCategories.clear();

	// Reset change detection
	prevRuleCount = 0;
	prevTotalItems = 0;
}

void StorageConfigDialogModel::clear() {
	currentContainerId = ecs::EntityID{0};
	currentContainerDefName.clear();
	containerLabel.clear();
	valid = false;
	selectedItem.clear();
	selectedItemRules.clear();
	groups.clear();
	items.clear();
	containerCategories.clear();
}

StorageConfigDialogModel::UpdateType StorageConfigDialogModel::refresh(
	const ecs::World& world, const engine::assets::AssetRegistry& registry) {
	if (currentContainerId == ecs::EntityID{0}) {
		valid = false;
		return UpdateType::None;
	}

	bool wasValid = valid;
	valid = true;

	// Get container's accepted categories from asset definition
	const auto* containerDef = registry.getDefinition(currentContainerDefName);
	if (containerDef != nullptr && containerDef->capabilities.storage.has_value()) {
		const auto& storageCap = containerDef->capabilities.storage.value();
		if (storageCap.acceptedCategories.empty()) {
			// Accepts all categories
			containerCategories = {
				engine::assets::ItemCategory::RawMaterial,
				engine::assets::ItemCategory::Food,
				engine::assets::ItemCategory::Tool,
				engine::assets::ItemCategory::Furniture,
			};
		} else {
			containerCategories = storageCap.acceptedCategories;
		}
	}

	// Extract all data
	extractAvailableItems(registry);
	updateInventoryCounts(world);
	updateSelectedItemRules(world);

	// If we just became valid, it's a full update
	if (!wasValid) {
		return UpdateType::Full;
	}

	// Check for rule changes
	const auto* config = world.getComponent<ecs::StorageConfiguration>(currentContainerId);
	if (config != nullptr) {
		if (config->getRuleCount() != prevRuleCount) {
			prevRuleCount = config->getRuleCount();
			return UpdateType::Rules;
		}
	}

	// Check for inventory changes
	const auto* inventory = world.getComponent<ecs::Inventory>(currentContainerId);
	if (inventory != nullptr) {
		uint32_t totalItems = 0;
		for (const auto& [defName, count] : inventory->items) {
			totalItems += count;
		}
		if (totalItems != prevTotalItems) {
			prevTotalItems = totalItems;
			return UpdateType::Inventory;
		}
	}

	return UpdateType::None;
}

void StorageConfigDialogModel::selectItem(const std::string& defName) {
	if (defName != selectedItem) {
		selectedItem = defName;
		// Reset pending form to defaults when selection changes
		pendingPriority = ecs::StoragePriority::Medium;
		pendingMinAmount = 0;
		pendingMaxAmount = 0;
		pendingUnlimited = true;
	}
}

void StorageConfigDialogModel::clearSelection() {
	selectedItem.clear();
	selectedItemRules.clear();
}

const AvailableItem* StorageConfigDialogModel::selectedItemData() const {
	if (selectedItem.empty()) {
		return nullptr;
	}
	for (const auto& item : items) {
		if (item.defName == selectedItem) {
			return &item;
		}
	}
	return nullptr;
}

bool StorageConfigDialogModel::addRule(ecs::World& world) {
	if (selectedItem.empty()) {
		return false;
	}

	const AvailableItem* itemData = selectedItemData();
	if (itemData == nullptr) {
		return false;
	}

	auto* config = world.getComponent<ecs::StorageConfiguration>(currentContainerId);
	if (config == nullptr) {
		return false;
	}

	// Auto-remove category wildcards when adding a specific item rule
	// This provides better UX: adding "Stone: max 10" removes "All Raw Materials: unlimited"
	// Iterate backwards to safely remove multiple wildcards if they exist
	for (size_t i = config->rules.size(); i > 0; --i) {
		const auto& existingRule = config->rules[i - 1];
		if (existingRule.isWildcard() && existingRule.category == itemData->category) {
			config->removeRule(i - 1);
		}
	}

	ecs::StorageRule rule{
		.defName = selectedItem,
		.category = itemData->category,
		.priority = pendingPriority,
		.minAmount = pendingMinAmount,
		.maxAmount = pendingUnlimited ? 0 : pendingMaxAmount,
	};

	config->addRule(rule);
	return true;
}

bool StorageConfigDialogModel::addCategoryWildcard(ecs::World& world) {
	const AvailableItem* itemData = selectedItemData();
	if (itemData == nullptr) {
		return false;
	}

	auto* config = world.getComponent<ecs::StorageConfiguration>(currentContainerId);
	if (config == nullptr) {
		return false;
	}

	ecs::StorageRule rule{
		.defName = "*",
		.category = itemData->category,
		.priority = ecs::StoragePriority::Medium,
		.minAmount = 0,
		.maxAmount = 0, // Unlimited
	};

	config->addRule(rule);
	return true;
}

void StorageConfigDialogModel::removeRule(ecs::World& world, size_t ruleIndex) {
	auto* config = world.getComponent<ecs::StorageConfiguration>(currentContainerId);
	if (config == nullptr) {
		return;
	}
	config->removeRule(ruleIndex);
}

void StorageConfigDialogModel::addAllCategories(ecs::World& world) {
	auto* config = world.getComponent<ecs::StorageConfiguration>(currentContainerId);
	if (config == nullptr) {
		return;
	}

	// Add wildcard rules for all accepted categories
	for (auto category : containerCategories) {
		// Check if wildcard already exists for this category
		bool exists = false;
		for (const auto& rule : config->rules) {
			if (rule.isWildcard() && rule.category == category) {
				exists = true;
				break;
			}
		}
		if (!exists) {
			config->addRule(ecs::StorageRule{
				.defName = "*",
				.category = category,
				.priority = ecs::StoragePriority::Medium,
				.minAmount = 0,
				.maxAmount = 0,
			});
		}
	}
}

void StorageConfigDialogModel::removeAllRules(ecs::World& world) {
	auto* config = world.getComponent<ecs::StorageConfiguration>(currentContainerId);
	if (config == nullptr) {
		return;
	}
	config->clear();
}

void StorageConfigDialogModel::extractAvailableItems(
	const engine::assets::AssetRegistry& registry) {
	items.clear();
	groups.clear();

	// Build category groups for accepted categories
	for (auto category : containerCategories) {
		ItemCategoryGroup group;
		group.category = category;
		group.label = getCategoryLabel(category);
		group.expanded = true;
		groups.push_back(group);
	}

	// Get all asset definitions and filter to storable items
	const auto& defNames = registry.getDefinitionNames();
	for (const auto& defName : defNames) {
		const auto* defPtr = registry.getDefinition(defName);
		if (defPtr == nullptr) {
			continue;
		}
		const auto& def = *defPtr;
		// Skip items with no category
		if (def.category == engine::assets::ItemCategory::None) {
			continue;
		}

		// Skip items not in our accepted categories
		bool accepted = false;
		for (auto category : containerCategories) {
			if (def.category == category) {
				accepted = true;
				break;
			}
		}
		if (!accepted) {
			continue;
		}

		// Skip non-carryable items (can't be stored)
		if (!def.capabilities.carryable.has_value()) {
			continue;
		}

		AvailableItem item;
		item.defName = defName;
		item.label = def.label.empty() ? defName : def.label;
		item.category = def.category;
		item.currentCount = 0;	   // Updated in updateInventoryCounts
		item.requestedCount = 0;   // Updated in updateInventoryCounts
		item.hasRules = false;	   // Updated in updateInventoryCounts

		size_t itemIndex = items.size();
		items.push_back(item);

		// Add to appropriate group
		for (auto& group : groups) {
			if (group.category == def.category) {
				group.itemIndices.push_back(itemIndex);
				break;
			}
		}
	}

	// Sort items within each group alphabetically
	for (auto& group : groups) {
		std::sort(group.itemIndices.begin(), group.itemIndices.end(),
				  [this](size_t a, size_t b) { return items[a].label < items[b].label; });
	}
}

void StorageConfigDialogModel::updateInventoryCounts(const ecs::World& world) {
	// Reset all counts
	for (auto& item : items) {
		item.currentCount = 0;
		item.requestedCount = 0;
		item.hasRules = false;
	}

	// Update current counts from Inventory
	const auto* inventory = world.getComponent<ecs::Inventory>(currentContainerId);
	if (inventory != nullptr) {
		for (const auto& [defName, count] : inventory->items) {
			for (auto& item : items) {
				if (item.defName == defName) {
					item.currentCount = count;
					break;
				}
			}
		}
	}

	// Update requested counts from StorageConfiguration
	const auto* config = world.getComponent<ecs::StorageConfiguration>(currentContainerId);
	if (config != nullptr) {
		for (auto& item : items) {
			auto matchingRules = config->getRulesFor(item.defName, item.category);
			if (!matchingRules.empty()) {
				item.hasRules = true;
				// Sum up max amounts (0 means unlimited, so any unlimited = unlimited)
				uint32_t total = 0;
				bool	 hasUnlimited = false;
				for (const auto* rule : matchingRules) {
					if (rule->maxAmount == 0) {
						hasUnlimited = true;
					} else {
						total += rule->maxAmount;
					}
				}
				item.requestedCount = hasUnlimited ? 0 : total;
			}
		}
	}
}

void StorageConfigDialogModel::updateSelectedItemRules(const ecs::World& world) {
	selectedItemRules.clear();

	if (selectedItem.empty()) {
		return;
	}

	const auto* config = world.getComponent<ecs::StorageConfiguration>(currentContainerId);
	if (config == nullptr) {
		return;
	}

	// Find the selected item's category
	engine::assets::ItemCategory selectedCategory = engine::assets::ItemCategory::None;
	for (const auto& item : items) {
		if (item.defName == selectedItem) {
			selectedCategory = item.category;
			break;
		}
	}

	// Find all rules that match the selected item
	// This includes both specific rules and wildcard rules for the category
	for (size_t i = 0; i < config->rules.size(); ++i) {
		const auto& rule = config->rules[i];

		// Check if rule matches this item
		if (!rule.matches(selectedItem, selectedCategory)) {
			continue;
		}

		DisplayRule display;
		display.ruleIndex = i;
		display.defName = rule.defName;
		display.priority = rule.priority;
		display.minAmount = rule.minAmount;
		display.maxAmount = rule.maxAmount;
		display.isWildcard = rule.isWildcard();
		display.category = rule.category;

		// Create display label
		if (display.isWildcard) {
			display.label = "All " + getCategoryLabel(rule.category);
		} else {
			// Find the item's label
			for (const auto& item : items) {
				if (item.defName == rule.defName) {
					display.label = item.label;
					break;
				}
			}
			if (display.label.empty()) {
				display.label = rule.defName;
			}
		}

		selectedItemRules.push_back(display);
	}
}

std::string StorageConfigDialogModel::getCategoryLabel(engine::assets::ItemCategory category) {
	switch (category) {
	case engine::assets::ItemCategory::RawMaterial:
		return "Raw Materials";
	case engine::assets::ItemCategory::Food:
		return "Food";
	case engine::assets::ItemCategory::Tool:
		return "Tools";
	case engine::assets::ItemCategory::Furniture:
		return "Furniture";
	default:
		return "Unknown";
	}
}

} // namespace world_sim
