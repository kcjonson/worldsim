#pragma once

// StorageConfiguration Component - Per-instance storage rules
//
// This component stores the runtime configuration for a storage container.
// Unlike StorageCapability (in AssetDefinition), which defines what a container
// CAN hold based on its type, StorageConfiguration defines what it SHOULD hold
// based on player configuration.
//
// Key concepts:
// - Rules: Individual storage preferences (item + priority + min/max amounts)
// - Wildcards: Rules with defName="*" match entire categories
// - Priority: Higher priority containers "pull" items from lower priority ones
// - Min Amount: Pull threshold - maintain at least this many items
// - Max Amount: Stop accepting after this many (0 = unlimited)
//
// Example rules:
// - {"Stick", RawMaterial, High, min=10, max=0} = Keep at least 10 sticks, no limit
// - {"*", Tool, Medium, min=0, max=0} = Accept all tools at medium priority
// - {"Berry", Food, Critical, min=5, max=20} = Always keep 5-20 berries

#include "assets/AssetDefinition.h" // For ItemCategory

#include <cstdint>
#include <string>
#include <vector>

namespace ecs {

/// Priority level for storage rules
/// Higher priority containers pull items from lower priority ones
enum class StoragePriority : uint8_t {
	Low = 0,	 // Fill last
	Medium = 1,	 // Default
	High = 2,	 // Fill before normal
	Critical = 3 // Fill first, pull from lower
};

/// Convert priority to display string
inline const char* storagePriorityToString(StoragePriority priority) {
	switch (priority) {
	case StoragePriority::Low:
		return "Low";
	case StoragePriority::Medium:
		return "Medium";
	case StoragePriority::High:
		return "High";
	case StoragePriority::Critical:
		return "Critical";
	default:
		return "Unknown";
	}
}

/// A single storage rule defining how to handle a specific item or category
struct StorageRule {
	std::string					   defName;	 // Item defName, or "*" for category wildcard
	engine::assets::ItemCategory category; // Category this rule applies to
	StoragePriority				   priority = StoragePriority::Medium;
	uint32_t					   minAmount = 0; // Pull threshold (maintain at least this many)
	uint32_t					   maxAmount = 0; // Max to store (0 = unlimited)

	// Quality filtering (placeholder for future)
	// uint8_t minQuality = 0;   // 0 = Any
	// uint8_t maxQuality = 255; // 255 = Any

	/// Check if this is a wildcard rule (matches entire category)
	[[nodiscard]] bool isWildcard() const { return defName == "*"; }

	/// Check if this rule matches a specific item
	[[nodiscard]] bool matches(const std::string& itemDefName,
							   engine::assets::ItemCategory itemCategory) const {
		// Category must match
		if (category != itemCategory) {
			return false;
		}
		// Wildcard matches all items in category
		if (isWildcard()) {
			return true;
		}
		// Specific item match
		return defName == itemDefName;
	}
};

/// Storage configuration component - attached to storage container entities
/// Stores the player-configured rules for what this container should hold
struct StorageConfiguration {
	std::vector<StorageRule> rules;

	// ============================================================================
	// Query Methods
	// ============================================================================

	/// Check if this container accepts a specific item
	/// Returns true if any rule matches the item
	[[nodiscard]] bool acceptsItem(const std::string& defName,
								   engine::assets::ItemCategory category) const {
		for (const auto& rule : rules) {
			if (rule.matches(defName, category)) {
				return true;
			}
		}
		return false;
	}

	/// Get the highest priority for a specific item
	/// Returns Low if no rule matches
	[[nodiscard]] StoragePriority getPriorityFor(
		const std::string& defName, engine::assets::ItemCategory category) const {
		StoragePriority highest = StoragePriority::Low;
		bool			found = false;

		for (const auto& rule : rules) {
			if (rule.matches(defName, category)) {
				if (!found || rule.priority > highest) {
					highest = rule.priority;
					found = true;
				}
			}
		}
		return highest;
	}

	/// Get the max amount for a specific item (sum of all matching rules)
	/// Returns 0 if unlimited or no rules match
	[[nodiscard]] uint32_t getMaxAmountFor(const std::string& defName,
										   engine::assets::ItemCategory category) const {
		uint32_t total = 0;
		bool	 hasUnlimited = false;

		for (const auto& rule : rules) {
			if (rule.matches(defName, category)) {
				if (rule.maxAmount == 0) {
					hasUnlimited = true;
				} else {
					total += rule.maxAmount;
				}
			}
		}

		// If any rule is unlimited, return 0 (unlimited)
		return hasUnlimited ? 0 : total;
	}

	/// Get the min amount (pull threshold) for a specific item
	/// Returns the sum of all matching rules' min amounts
	[[nodiscard]] uint32_t getMinAmountFor(const std::string& defName,
										   engine::assets::ItemCategory category) const {
		uint32_t total = 0;
		for (const auto& rule : rules) {
			if (rule.matches(defName, category)) {
				total += rule.minAmount;
			}
		}
		return total;
	}

	/// Get all rules that match a specific item
	[[nodiscard]] std::vector<const StorageRule*> getRulesFor(
		const std::string& defName, engine::assets::ItemCategory category) const {
		std::vector<const StorageRule*> result;
		for (const auto& rule : rules) {
			if (rule.matches(defName, category)) {
				result.push_back(&rule);
			}
		}
		return result;
	}

	/// Get all rules for a specific defName (exact match, for UI display)
	[[nodiscard]] std::vector<const StorageRule*> getRulesForDefName(
		const std::string& defName) const {
		std::vector<const StorageRule*> result;
		for (const auto& rule : rules) {
			if (rule.defName == defName) {
				result.push_back(&rule);
			}
		}
		return result;
	}

	/// Check if any rules exist
	[[nodiscard]] bool hasRules() const { return !rules.empty(); }

	/// Get total rule count
	[[nodiscard]] size_t getRuleCount() const { return rules.size(); }

	// ============================================================================
	// Mutation Methods
	// ============================================================================

	/// Add a new rule
	void addRule(StorageRule rule) { rules.push_back(std::move(rule)); }

	/// Remove a rule by index
	void removeRule(size_t index) {
		if (index < rules.size()) {
			rules.erase(rules.begin() + static_cast<std::ptrdiff_t>(index));
		}
	}

	/// Remove all rules for a specific defName
	void removeRulesFor(const std::string& defName) {
		rules.erase(std::remove_if(rules.begin(), rules.end(),
								   [&defName](const StorageRule& rule) {
									   return rule.defName == defName;
								   }),
					rules.end());
	}

	/// Clear all rules
	void clear() { rules.clear(); }

	// ============================================================================
	// Factory Methods
	// ============================================================================

	/// Create configuration that accepts all items in specified categories
	/// Each category gets a wildcard rule at Medium priority, Unlimited
	static StorageConfiguration createAcceptAll(
		const std::vector<engine::assets::ItemCategory>& categories) {
		StorageConfiguration config;
		for (auto category : categories) {
			config.addRule(StorageRule{
				.defName = "*",
				.category = category,
				.priority = StoragePriority::Medium,
				.minAmount = 0,
				.maxAmount = 0 // Unlimited
			});
		}
		return config;
	}

	/// Create configuration that accepts everything (all categories)
	static StorageConfiguration createAcceptEverything() {
		return createAcceptAll({
			engine::assets::ItemCategory::RawMaterial,
			engine::assets::ItemCategory::Food,
			engine::assets::ItemCategory::Tool,
			engine::assets::ItemCategory::Furniture,
		});
	}

	/// Create empty configuration (accepts nothing)
	static StorageConfiguration createEmpty() { return StorageConfiguration{}; }
};

} // namespace ecs
