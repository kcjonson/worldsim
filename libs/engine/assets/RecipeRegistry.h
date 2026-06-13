#pragma once

// Recipe Registry
// Central catalog for recipe definitions loaded from XML files.
// Handles recipe loading, caching, and queries for crafting system.
//
// See /docs/design/game-systems/colonists/technology-discovery.md for design details.

#include "assets/RecipeDef.h"

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace engine::assets {

/// Central registry for recipe definitions.
/// Recipes are loaded from XML definition files and cached for query.
class RecipeRegistry {
  public:
    /// Get the singleton registry instance
    static RecipeRegistry& Get();

    // --- Loading ---

    /// Load recipes from an XML file
    /// @param xmlPath Path to the XML recipe file
    /// @return true if loading succeeded
    bool loadRecipes(const std::string& xmlPath);

    /// Load all recipes from a folder recursively
    /// Scans for all *.xml files in the folder and subfolders.
    /// @param folderPath Path to the recipes folder
    /// @return Number of recipes loaded
    size_t loadRecipesFromFolder(const std::string& folderPath);

    /// Clear all loaded recipes
    void clear();

    // --- Queries ---

    /// Get a recipe by defName
    /// @param defName The recipe definition name
    /// @return Pointer to recipe, or nullptr if not found
    [[nodiscard]] const RecipeDef* getRecipe(const std::string& defName) const;

    /// Get all recipes that can be crafted at a specific station
    /// @param stationDefName The station definition name (e.g., "CraftingSpot")
    /// @return Vector of recipes (pointers to internal storage)
    [[nodiscard]] std::vector<const RecipeDef*> getRecipesForStation(const std::string& stationDefName) const;

    /// Get all recipes that a colonist can craft (knows all inputs)
    /// @param knownDefs Set of defNameIds the colonist knows
    /// @return Vector of recipes the colonist can make
    [[nodiscard]] std::vector<const RecipeDef*> getAvailableRecipes(
        const std::unordered_set<uint32_t>& knownDefs) const;

    /// Get all innate recipes (known from start)
    /// @return Vector of innate recipes
    [[nodiscard]] const std::vector<const RecipeDef*>& getInnateRecipes() const;

    /// Get all loaded recipe defNames
    /// @return Vector of recipe names
    [[nodiscard]] std::vector<std::string> getRecipeNames() const;

    /// Get all loaded recipes (for iteration)
    /// @return Const reference to internal recipe map
    [[nodiscard]] const std::unordered_map<std::string, RecipeDef>& allRecipes() const;

    /// Get count of loaded recipes
    [[nodiscard]] size_t size() const;

    // --- Test Support ---

    /// Register a recipe directly (for unit tests)
    /// This bypasses XML loading for test scenarios.
    /// @param recipe The recipe to register
    void registerTestRecipe(RecipeDef recipe);

  private:
    RecipeRegistry() = default;

    /// Parse a single recipe from XML node
    bool parseRecipeFromNode(const void* node); // pugi::xml_node passed as void*

    /// Populate cached defNameIds after loading
    void populateDefNameIds();

    // --- Storage ---

    /// All loaded recipes by defName
    std::unordered_map<std::string, RecipeDef> m_recipes;

    /// Recipes indexed by station defName
    std::unordered_map<std::string, std::vector<const RecipeDef*>> m_byStation;

    /// Innate recipes (known from start)
    std::vector<const RecipeDef*> m_innateRecipes;

    /// Empty string for invalid ID lookups
    static const std::string s_emptyString;
};

} // namespace engine::assets
