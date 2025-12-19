// Recipe Registry Implementation
// Handles XML parsing with pugixml for recipe definitions.

#include "assets/RecipeRegistry.h"
#include "assets/AssetRegistry.h"

#include <utils/Log.h>

#include <pugixml.hpp>

#include <filesystem>

namespace engine::assets {

const std::string RecipeRegistry::s_emptyString;

RecipeRegistry& RecipeRegistry::Get() {
    static RecipeRegistry instance;
    return instance;
}

bool RecipeRegistry::loadRecipes(const std::string& xmlPath) {
    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_file(xmlPath.c_str());

    if (!result) {
        LOG_ERROR(Engine, "Failed to load recipe XML: %s - %s", xmlPath.c_str(), result.description());
        return false;
    }

    // Try both singular and plural root element names
    pugi::xml_node root = doc.child("RecipeDef");
    if (root) {
        // Single recipe file
        return parseRecipeFromNode(&root);
    }

    root = doc.child("recipes");
    if (!root) {
        root = doc.child("Recipes");
    }

    if (!root) {
        LOG_WARNING(Engine, "No recipes or RecipeDef root element in: %s", xmlPath.c_str());
        return false;
    }

    // Multiple recipes in one file
    bool anyLoaded = false;
    for (pugi::xml_node recipeNode : root.children("RecipeDef")) {
        if (parseRecipeFromNode(&recipeNode)) {
            anyLoaded = true;
        }
    }
    // Also try lowercase
    for (pugi::xml_node recipeNode : root.children("recipe")) {
        if (parseRecipeFromNode(&recipeNode)) {
            anyLoaded = true;
        }
    }

    return anyLoaded;
}

size_t RecipeRegistry::loadRecipesFromFolder(const std::string& folderPath) {
    namespace fs = std::filesystem;

    if (!fs::exists(folderPath)) {
        LOG_ERROR(Engine, "Recipe folder not found: %s", folderPath.c_str());
        return 0;
    }

    if (!fs::is_directory(folderPath)) {
        LOG_ERROR(Engine, "Path is not a directory: %s", folderPath.c_str());
        return 0;
    }

    size_t loadedBefore = m_recipes.size();

    try {
        for (const auto& entry : fs::recursive_directory_iterator(folderPath)) {
            if (!entry.is_regular_file()) {
                continue;
            }

            if (entry.path().extension() != ".xml") {
                continue;
            }

            loadRecipes(entry.path().string());
        }
    } catch (const std::exception& e) {
        LOG_ERROR(Engine, "Error scanning recipe folder: %s", e.what());
    }

    size_t loadedNow = m_recipes.size();
    size_t newRecipes = loadedNow - loadedBefore;

    if (newRecipes > 0) {
        // Rebuild indices after loading
        populateDefNameIds();
        LOG_INFO(Engine, "Loaded %zu recipes from %s", newRecipes, folderPath.c_str());
    }

    return newRecipes;
}

void RecipeRegistry::clear() {
    m_recipes.clear();
    m_byStation.clear();
    m_innateRecipes.clear();
}

const RecipeDef* RecipeRegistry::getRecipe(const std::string& defName) const {
    auto it = m_recipes.find(defName);
    if (it != m_recipes.end()) {
        return &it->second;
    }
    return nullptr;
}

std::vector<const RecipeDef*> RecipeRegistry::getRecipesForStation(const std::string& stationDefName) const {
    auto it = m_byStation.find(stationDefName);
    if (it != m_byStation.end()) {
        return it->second;
    }
    return {};
}

std::vector<const RecipeDef*> RecipeRegistry::getAvailableRecipes(
    const std::unordered_set<uint32_t>& knownDefs) const {

    std::vector<const RecipeDef*> available;

    for (const auto& [name, recipe] : m_recipes) {
        // Check if colonist knows all inputs
        bool knowsAll = true;
        for (uint32_t inputId : recipe.inputDefNameIds) {
            if (knownDefs.find(inputId) == knownDefs.end()) {
                knowsAll = false;
                break;
            }
        }

        if (knowsAll) {
            available.push_back(&recipe);
        }
    }

    return available;
}

const std::vector<const RecipeDef*>& RecipeRegistry::getInnateRecipes() const {
    return m_innateRecipes;
}

std::vector<std::string> RecipeRegistry::getRecipeNames() const {
    std::vector<std::string> names;
    names.reserve(m_recipes.size());
    for (const auto& [name, _] : m_recipes) {
        names.push_back(name);
    }
    return names;
}

const std::unordered_map<std::string, RecipeDef>& RecipeRegistry::allRecipes() const {
    return m_recipes;
}

size_t RecipeRegistry::size() const {
    return m_recipes.size();
}

bool RecipeRegistry::parseRecipeFromNode(const void* nodePtr) {
    const pugi::xml_node& node = *static_cast<const pugi::xml_node*>(nodePtr);

    RecipeDef recipe;

    // Required: defName
    pugi::xml_node defNameNode = node.child("defName");
    if (!defNameNode) {
        LOG_WARNING(Engine, "Recipe missing defName");
        return false;
    }
    recipe.defName = defNameNode.text().as_string();

    if (recipe.defName.empty()) {
        LOG_WARNING(Engine, "Recipe has empty defName");
        return false;
    }

    // Optional fields
    if (auto labelNode = node.child("label")) {
        recipe.label = labelNode.text().as_string();
    } else {
        recipe.label = recipe.defName;
    }

    if (auto descNode = node.child("description")) {
        recipe.description = descNode.text().as_string();
    }

    // Parse inputs
    if (auto inputsNode = node.child("inputs")) {
        for (auto inputNode : inputsNode.children("input")) {
            RecipeInput input;
            input.defName = inputNode.attribute("thing").as_string();
            if (input.defName.empty()) {
                // Try alternate attribute names
                input.defName = inputNode.attribute("defName").as_string();
            }
            if (input.defName.empty()) {
                input.defName = inputNode.attribute("material").as_string();
            }
            input.count = inputNode.attribute("count").as_uint(1);

            if (!input.defName.empty()) {
                recipe.inputs.push_back(input);
            }
        }
    }

    // Parse outputs
    if (auto outputsNode = node.child("outputs")) {
        for (auto outputNode : outputsNode.children("output")) {
            RecipeOutput output;
            output.defName = outputNode.attribute("thing").as_string();
            if (output.defName.empty()) {
                output.defName = outputNode.attribute("item").as_string();
            }
            if (output.defName.empty()) {
                output.defName = outputNode.attribute("defName").as_string();
            }
            output.count = outputNode.attribute("count").as_uint(1);

            if (!output.defName.empty()) {
                recipe.outputs.push_back(output);
            }
        }
    }

    // Station
    if (auto stationNode = node.child("station")) {
        recipe.stationDefName = stationNode.text().as_string();
    }

    // Skill
    if (auto skillNode = node.child("skill")) {
        recipe.skillDefName = skillNode.text().as_string();
    }

    // Work amount
    if (auto workNode = node.child("workAmount")) {
        recipe.workAmount = workNode.text().as_float(500.0F);
    }

    // Innate flag
    if (auto innateNode = node.child("innate")) {
        recipe.innate = innateNode.text().as_bool(false);
    }

    // Store recipe
    auto [it, inserted] = m_recipes.emplace(recipe.defName, std::move(recipe));
    if (!inserted) {
        LOG_WARNING(Engine, "Duplicate recipe defName: %s", recipe.defName.c_str());
        return false;
    }

    LOG_DEBUG(Engine, "Loaded recipe: %s", it->first.c_str());
    return true;
}

void RecipeRegistry::populateDefNameIds() {
    auto& assetRegistry = AssetRegistry::Get();

    m_byStation.clear();
    m_innateRecipes.clear();

    for (auto& [name, recipe] : m_recipes) {
        // Populate input defNameIds
        recipe.inputDefNameIds.clear();
        for (auto& input : recipe.inputs) {
            input.defNameId = assetRegistry.getDefNameId(input.defName);
            if (input.defNameId != 0) {
                recipe.inputDefNameIds.push_back(input.defNameId);
            }
        }

        // Populate output defNameIds
        for (auto& output : recipe.outputs) {
            output.defNameId = assetRegistry.getDefNameId(output.defName);
        }

        // Populate station defNameId
        if (!recipe.stationDefName.empty() && recipe.stationDefName != "none") {
            recipe.stationDefNameId = assetRegistry.getDefNameId(recipe.stationDefName);
        }

        // Index by station
        m_byStation[recipe.stationDefName].push_back(&recipe);

        // Track innate recipes
        if (recipe.innate) {
            m_innateRecipes.push_back(&recipe);
        }
    }
}

} // namespace engine::assets
