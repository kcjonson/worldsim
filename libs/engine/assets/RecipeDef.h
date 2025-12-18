#pragma once

// Recipe Definition - Data structures for crafting recipes
//
// Recipes define how to transform inputs into outputs at crafting stations.
// See /docs/design/game-systems/colonists/technology-discovery.md for design details.

#include <cstdint>
#include <string>
#include <vector>

namespace engine::assets {

/// Single input requirement for a recipe
struct RecipeInput {
    std::string defName;      ///< Thing definition name (e.g., "Stone", "Stick")
    uint32_t defNameId = 0;   ///< Interned ID (populated by RecipeRegistry)
    uint32_t count = 1;       ///< Amount required
};

/// Single output from a recipe
struct RecipeOutput {
    std::string defName;      ///< Thing definition name (e.g., "AxePrimitive")
    uint32_t defNameId = 0;   ///< Interned ID (populated by RecipeRegistry)
    uint32_t count = 1;       ///< Amount produced
};

/// Complete recipe definition
struct RecipeDef {
    // --- Identity ---
    std::string defName;      ///< Unique recipe ID (e.g., "Recipe_AxePrimitive")
    std::string label;        ///< Human-readable name (e.g., "Primitive Axe")
    std::string description;  ///< Tooltip description

    // --- Requirements ---
    std::vector<RecipeInput> inputs;   ///< Required input things
    std::string stationDefName;        ///< Required station (e.g., "CraftingSpot", "none")
    uint32_t stationDefNameId = 0;     ///< Interned station ID
    std::string skillDefName;          ///< Skill used (affects quality), optional

    // --- Output ---
    std::vector<RecipeOutput> outputs; ///< Produced things

    // --- Work ---
    float workAmount = 500.0F;         ///< Work ticks to complete

    // --- Flags ---
    bool innate = false;               ///< If true, all colonists know this from start

    // --- Cached for efficiency ---
    std::vector<uint32_t> inputDefNameIds;  ///< Pre-computed for Knowledge::knowsAll() check

    // --- Query Methods ---

    /// Check if this recipe requires no station (can be done anywhere)
    [[nodiscard]] bool isStationless() const {
        return stationDefName.empty() || stationDefName == "none";
    }

    /// Check if this recipe requires any inputs
    [[nodiscard]] bool hasInputs() const {
        return !inputs.empty();
    }
};

} // namespace engine::assets
