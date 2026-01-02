#pragma once

// Work Type Definition
// Defines work categories and work types loaded from XML configuration.
// Work types map entity capabilities to task generation.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace engine::assets {

/// Filter conditions for work type applicability
struct WorkTypeFilter {
    /// Only applies to entities in this group (e.g., "crops", "wild_plants")
    std::optional<std::string> entityGroup;

    /// Only applies to loose items (not in storage)
    std::optional<bool> looseItem;

    /// Only applies indoors/outdoors
    std::optional<bool> indoor;

    /// Only applies to items needed by active recipes
    std::optional<bool> neededByRecipe;

    /// Only applies to items needed by blueprints
    std::optional<bool> neededByBlueprint;

    /// Only applies at this station type
    std::optional<std::string> stationType;

    /// Only applies to items with placement targets
    std::optional<bool> hasPlacementTarget;

    /// Check if filter has any conditions
    [[nodiscard]] bool hasConditions() const {
        return entityGroup.has_value() ||
               looseItem.has_value() ||
               indoor.has_value() ||
               neededByRecipe.has_value() ||
               neededByBlueprint.has_value() ||
               stationType.has_value() ||
               hasPlacementTarget.has_value();
    }
};

/// Definition of a work type
/// Loaded from assets/config/work/work-types.xml
struct WorkTypeDef {
    /// Unique identifier (e.g., "Work_HarvestWild")
    std::string defName;

    /// Human-readable name
    std::string label;

    /// Description of what this work does
    std::string description;

    /// Capability that triggers task generation (e.g., "Harvestable")
    std::string triggerCapability;

    /// Secondary capability for two-target tasks (e.g., "Storage" for hauling)
    std::optional<std::string> targetCapability;

    /// Skill required to do this work (empty = anyone can do it)
    std::optional<std::string> skillRequired;

    /// Minimum skill level required
    float minSkillLevel = 0.0F;

    /// Task chain to use for multi-step tasks
    std::optional<std::string> taskChain;

    /// Filter conditions for when this work type applies
    WorkTypeFilter filter;

    /// Parent category defName
    std::string categoryDefName;
};

/// Definition of a work category (group of related work types)
struct WorkCategoryDef {
    /// Unique identifier (e.g., "Farming", "Hauling")
    std::string defName;

    /// Human-readable name
    std::string label;

    /// Description of this category
    std::string description;

    /// Priority tier (lower = higher priority, 1-10+)
    float tier = 5.0F;

    /// Whether colonists can disable this category (false for Emergency)
    bool canDisable = true;

    /// Work types in this category
    std::vector<std::string> workTypeDefNames;
};

} // namespace engine::assets
