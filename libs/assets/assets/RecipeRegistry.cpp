// Recipe Registry Implementation
// Handles XML parsing with pugixml for recipe definitions.

#include "assets/RecipeRegistry.h"
#include "assets/AssetRegistry.h"
#include "assets/ConfigFiles.h"
#include "assets/ConfigNumber.h"
#include "assets/ConfigXml.h"

#include <utils/Log.h>

#include <pugixml.hpp>

#include <optional>
#include <string_view>
#include <utility>

namespace assets {

	namespace {

		/// The <input> or <output> lines of `list`, each naming a thing and an optional count.
		template <typename Line>
		std::vector<Line> linesFrom(
			pugi::xml_node list, const char* lineName, const std::string& what, const std::string& source, std::vector<std::string>& errors
		) {
			std::vector<Line> lines;
			for (const pugi::xml_node node : list.children()) {
				if (node.type() != pugi::node_element) {
					continue;
				}
				if (std::string_view(node.name()) != lineName) {
					errors.push_back(source + ": " + what + ": unknown element <" + node.name() + "> in <" + list.name() + ">");
					continue;
				}
				const std::string lineWhat = what + " <" + lineName + "> " + std::to_string(lines.size() + 1);
				onlyAttributes(node, {"thing", "count"}, lineWhat, source, errors);
				Line line;
				line.defName = node.attribute("thing").as_string();
				if (line.defName.empty()) {
					errors.push_back(source + ": " + lineWhat + " has no thing");
				}
				line.count = node.attribute("count").as_uint(1);
				lines.push_back(std::move(line));
			}
			return lines;
		}

		/// The recipe a <RecipeDef> file holds; nothing when it has any mistake, each added to `errors`.
		std::optional<RecipeDef> recipeFrom(const std::filesystem::path& file, const std::string& source, std::vector<std::string>& errors) {
			pugi::xml_document	 document;
			const pugi::xml_node node = readRootElement(document, file, "RecipeDef", source, errors);
			if (!node) {
				return std::nullopt;
			}
			const std::size_t errorsBefore = errors.size();
			ConfigReader	  reader(source);
			const std::string what = ConfigReader::describe(node, "RecipeDef");
			onlyAttributes(node, {}, what, source, errors);

			RecipeDef recipe;
			for (const pugi::xml_node child : node.children()) {
				if (child.type() != pugi::node_element) {
					continue;
				}
				const std::string_view element = child.name();
				if (element == "defName") {
					recipe.defName = child.text().as_string();
				} else if (element == "label") {
					recipe.label = child.text().as_string();
				} else if (element == "description") {
					recipe.description = child.text().as_string();
				} else if (element == "inputs") {
					recipe.inputs = linesFrom<RecipeInput>(child, "input", what, source, errors);
				} else if (element == "outputs") {
					recipe.outputs = linesFrom<RecipeOutput>(child, "output", what, source, errors);
				} else if (element == "station") {
					recipe.stationDefName = child.text().as_string();
				} else if (element == "skill") {
					recipe.skillDefName = child.text().as_string();
				} else if (element == "innate") {
					recipe.innate = child.text().as_bool(false);
				} else if (element != "workAmount") {
					errors.push_back(source + ": " + what + ": unknown element <" + std::string(element) + ">");
				}
			}
			if (recipe.defName.empty()) {
				errors.push_back(source + ": the <RecipeDef> has no <defName>");
			}
			if (recipe.label.empty()) {
				recipe.label = recipe.defName;
			}
			recipe.workAmount = reader.childNumber(node, "RecipeDef", "workAmount");
			if (recipe.workAmount < 0.0F) {
				reader.refuseChild(node, "RecipeDef", "workAmount", "a number of work ticks, 0 or more");
			}

			std::vector<std::string> numbers = reader.takeProblems();
			errors.insert(errors.end(), numbers.begin(), numbers.end());
			if (errors.size() != errorsBefore) {
				return std::nullopt;
			}
			return recipe;
		}

	} // namespace

	RecipeRegistry& RecipeRegistry::Get() {
		static RecipeRegistry instance;
		return instance;
	}

	bool RecipeRegistry::loadRecipesFromFolder(const std::filesystem::path& folder) {
		clear();

		const ConfigFiles found = configFilesIn(folder);
		if (found.error.has_value()) {
			m_problems.push_back(*found.error);
			return false;
		}
		for (const std::filesystem::path& file : found.files) {
			const std::string		 source = configFileName(file, folder);
			std::optional<RecipeDef> recipe = recipeFrom(file, source, m_problems);
			if (!recipe.has_value()) {
				continue;
			}
			const std::string defName = recipe->defName;
			if (!m_recipes.emplace(defName, std::move(*recipe)).second) {
				m_problems.push_back(source + ": <RecipeDef defName=\"" + defName + "\"> is declared in another file too");
			}
		}
		if (!m_problems.empty()) {
			m_recipes.clear();
			LOG_ERROR(Engine, "Recipes in %s have %zu problem(s)", folder.generic_string().c_str(), m_problems.size());
			return false;
		}

		populateDefNameIds();
		LOG_INFO(Engine, "Loaded %zu recipes from %s", m_recipes.size(), folder.generic_string().c_str());
		return true;
	}

	void RecipeRegistry::clear() {
		m_recipes.clear();
		m_byStation.clear();
		m_innateRecipes.clear();
		m_problems.clear();
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

	std::vector<const RecipeDef*> RecipeRegistry::getAvailableRecipes(const std::unordered_set<uint32_t>& knownDefs) const {

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

	void RecipeRegistry::registerTestRecipe(RecipeDef recipe) {
		const std::string defName = recipe.defName;
		// Inputs are matched by interned id, so a test recipe names the items its test registered.
		for (auto& input : recipe.inputs) {
			input.defNameId = AssetRegistry::Get().getDefNameId(input.defName);
		}
		auto [it, inserted] = m_recipes.emplace(defName, std::move(recipe));
		if (inserted) {
			// Index by station (don't need full populateDefNameIds for test recipes)
			m_byStation[it->second.stationDefName].push_back(&it->second);
			if (it->second.innate) {
				m_innateRecipes.push_back(&it->second);
			}
		}
	}

	void RecipeRegistry::populateDefNameIds() {
		auto& assetRegistry = AssetRegistry::Get();

		m_byStation.clear();
		m_innateRecipes.clear();

		auto resolve = [&assetRegistry](const std::string& recipeName, const char* role, const std::string& defName) {
			const uint32_t id = assetRegistry.getDefNameId(defName);
			if (id == 0) {
				LOG_ERROR(Engine, "Recipe %s names %s '%s', which is not a loaded definition", recipeName.c_str(), role, defName.c_str());
			}
			return id;
		};

		for (auto& [name, recipe] : m_recipes) {
			recipe.inputDefNameIds.clear();
			for (auto& input : recipe.inputs) {
				input.defNameId = resolve(name, "input", input.defName);
				if (input.defNameId != 0) {
					recipe.inputDefNameIds.push_back(input.defNameId);
				}
			}

			for (auto& output : recipe.outputs) {
				output.defNameId = resolve(name, "output", output.defName);
			}

			if (!recipe.stationDefName.empty() && recipe.stationDefName != "none") {
				recipe.stationDefNameId = resolve(name, "station", recipe.stationDefName);
			}

			// Index by station
			m_byStation[recipe.stationDefName].push_back(&recipe);

			// Track innate recipes
			if (recipe.innate) {
				m_innateRecipes.push_back(&recipe);
			}
		}
	}

} // namespace assets
