#pragma once

// Recipe Registry
// Central catalog for recipe definitions loaded from XML files.
// Handles recipe loading, caching, and queries for crafting system.
//
// See /docs/design/game-systems/colonists/technology-discovery.md for design details.

#include "assets/RecipeDef.h"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace assets {

/// Central registry for recipe definitions.
/// Recipes are loaded from XML definition files and cached for query.
class RecipeRegistry {
  public:
    /// Get the singleton registry instance
    static RecipeRegistry& Get();

    // --- Loading ---

    /// Replace every recipe with those in the .xml files under `folder` (configFilesIn's order), one
    /// <RecipeDef> a file, and resolve their def-name ids against AssetRegistry's index as it stands, so
    /// AssetRegistry must already hold the definitions (loadAssetSystem orders the two). False, with
    /// problems() saying why and no recipes held, when the folder can't be read or any file has a mistake.
    bool loadRecipesFromFolder(const std::filesystem::path& folder);

    /// What the files got wrong: each names the file (relative to the folder), the <RecipeDef> and the element.
    [[nodiscard]] const std::vector<std::string>& problems() const { return m_problems; }

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

    /// Resolve every recipe's def-name ids against AssetRegistry's index, which must already hold
    /// the loaded definitions; a name it doesn't hold is logged as an error.
    void populateDefNameIds();

    // --- Storage ---

    /// All loaded recipes by defName
    std::unordered_map<std::string, RecipeDef> m_recipes;

    /// Recipes indexed by station defName
    std::unordered_map<std::string, std::vector<const RecipeDef*>> m_byStation;

    /// Innate recipes (known from start)
    std::vector<const RecipeDef*> m_innateRecipes;

    std::vector<std::string> m_problems;
};

} // namespace assets
