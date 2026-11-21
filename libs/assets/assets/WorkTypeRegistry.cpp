// Work Type Registry Implementation
// Handles XML parsing with pugixml for work categories and work types.

#include "assets/WorkTypeRegistry.h"

#include "assets/ConfigNumber.h"
#include "assets/ConfigXml.h"

#include <utils/Log.h>

#include <pugixml.hpp>

#include <algorithm>
#include <optional>
#include <string_view>
#include <utility>

namespace assets {

	namespace {

		WorkTypeFilter filterFrom(
			pugi::xml_node			  node,
			const std::string&		  what,
			const std::string&		  source,
			ConfigReader&			  reader,
			std::vector<std::string>& errors
		) {
			const std::string label = what + " filter";
			WorkTypeFilter	  filter;
			for (const pugi::xml_node child : node.children()) {
				if (child.type() != pugi::node_element) {
					continue;
				}
				const std::string_view element = child.name();
				if (element == "entityGroup") {
					filter.entityGroup = child.text().as_string();
				} else if (element == "looseItem") {
					filter.looseItem = reader.labeledChildBoolean(label, node, "looseItem");
				} else if (element == "indoor") {
					filter.indoor = reader.labeledChildBoolean(label, node, "indoor");
				} else if (element == "neededByRecipe") {
					filter.neededByRecipe = reader.labeledChildBoolean(label, node, "neededByRecipe");
				} else if (element == "neededByBlueprint") {
					filter.neededByBlueprint = reader.labeledChildBoolean(label, node, "neededByBlueprint");
				} else if (element == "stationType") {
					filter.stationType = child.text().as_string();
				} else if (element == "hasPlacementTarget") {
					filter.hasPlacementTarget = reader.labeledChildBoolean(label, node, "hasPlacementTarget");
				} else {
					errors.push_back(source + ": " + what + ": unknown element <" + std::string(element) + "> in <filter>");
				}
			}
			return filter;
		}

		std::optional<WorkTypeDef> workTypeFrom(
			pugi::xml_node			  node,
			const std::string&		  categoryDefName,
			const std::string&		  source,
			ConfigReader&			  reader,
			std::vector<std::string>& errors
		) {
			WorkTypeDef workType;
			workType.categoryDefName = categoryDefName;
			workType.defName = node.attribute("defName").as_string();
			if (workType.defName.empty()) {
				errors.push_back(source + ": a <WorkType> in <Category defName=\"" + categoryDefName + "\"> has no defName");
				return std::nullopt;
			}
			const std::string what = ConfigReader::describe(node, "WorkType");
			onlyAttributes(node, {"defName"}, what, source, errors);

			workType.label = workType.defName;
			for (const pugi::xml_node child : node.children()) {
				if (child.type() != pugi::node_element) {
					continue;
				}
				const std::string_view element = child.name();
				if (element == "label") {
					workType.label = child.text().as_string();
				} else if (element == "description") {
					workType.description = child.text().as_string();
				} else if (element == "triggerCapability") {
					workType.triggerCapability = child.text().as_string();
				} else if (element == "targetCapability") {
					workType.targetCapability = child.text().as_string();
				} else if (element == "skillRequired") {
					workType.skillRequired = child.text().as_string();
				} else if (element == "minSkillLevel") {
					workType.minSkillLevel = reader.childNumber(node, "WorkType", "minSkillLevel");
					if (workType.minSkillLevel < 0.0F) {
						reader.refuseChild(node, "WorkType", "minSkillLevel", "a number, 0 or more");
					}
				} else if (element == "filter") {
					workType.filter = filterFrom(child, what, source, reader, errors);
				} else {
					errors.push_back(source + ": " + what + ": unknown element <" + std::string(element) + ">");
				}
			}
			return workType;
		}

	} // namespace

	WorkTypeRegistry& WorkTypeRegistry::Get() {
		static WorkTypeRegistry instance;
		return instance;
	}

	bool WorkTypeRegistry::loadFromFile(const std::filesystem::path& configRoot, const std::filesystem::path& file) {
		clear();

		const std::string	 source = file.generic_string();
		pugi::xml_document	 document;
		const pugi::xml_node root = readRootElement(document, configRoot / file, "WorkTypes", source, problemList);
		if (!root) {
			return false;
		}

		ConfigReader reader(source);
		for (const pugi::xml_node categoryNode : root.children()) {
			if (categoryNode.type() != pugi::node_element) {
				continue;
			}
			if (std::string_view(categoryNode.name()) != "Category") {
				problemList.push_back(source + ": unknown element <" + categoryNode.name() + ">");
				continue;
			}
			WorkCategoryDef category;
			category.defName = categoryNode.attribute("defName").as_string();
			if (category.defName.empty()) {
				problemList.push_back(source + ": a <Category> has no defName");
				continue;
			}
			const std::string what = ConfigReader::describe(categoryNode, "Category");
			onlyAttributes(categoryNode, {"defName", "tier", "canDisable"}, what, source, problemList);
			category.tier = reader.attributeNumber(categoryNode, "Category", "tier");
			if (category.tier < 0.0F) {
				reader.refuseAttribute(what, categoryNode, "tier", "a number, 0 or more");
			}
			category.canDisable = reader.attributeBoolean(categoryNode, "Category", "canDisable").value_or(true);
			category.label = category.defName;

			for (const pugi::xml_node child : categoryNode.children()) {
				if (child.type() != pugi::node_element) {
					continue;
				}
				const std::string_view element = child.name();
				if (element == "label") {
					category.label = child.text().as_string();
				} else if (element == "description") {
					category.description = child.text().as_string();
				} else if (element == "WorkType") {
					std::optional<WorkTypeDef> workType = workTypeFrom(child, category.defName, source, reader, problemList);
					if (!workType.has_value()) {
						continue;
					}
					const std::string defName = workType->defName;
					if (workTypes.emplace(defName, std::move(*workType)).second) {
						category.workTypeDefNames.push_back(defName);
					} else {
						problemList.push_back(source + ": <WorkType defName=\"" + defName + "\"> is declared twice");
					}
				} else {
					problemList.push_back(source + ": " + what + ": unknown element <" + std::string(element) + ">");
				}
			}

			const std::string categoryName = category.defName;
			if (!categories.emplace(categoryName, std::move(category)).second) {
				problemList.push_back(source + ": " + what + " is declared twice");
			}
		}

		std::vector<std::string> numbers = reader.takeProblems();
		problemList.insert(problemList.end(), numbers.begin(), numbers.end());
		if (categories.empty() && problemList.empty()) {
			problemList.push_back(source + ": no <Category>");
		}
		if (!problemList.empty()) {
			categories.clear();
			workTypes.clear();
			LOG_ERROR(Engine, "Work types %s have %zu problem(s)", source.c_str(), problemList.size());
			return false;
		}

		buildCapabilityIndex();
		LOG_INFO(Engine, "Loaded %zu work types in %zu categories from %s", workTypes.size(), categories.size(), source.c_str());
		return true;
	}

	void WorkTypeRegistry::clear() {
		categories.clear();
		workTypes.clear();
		byCapability.clear();
		problemList.clear();
	}

	const WorkCategoryDef* WorkTypeRegistry::getCategory(const std::string& defName) const {
		auto it = categories.find(defName);
		if (it != categories.end()) {
			return &it->second;
		}
		return nullptr;
	}

	bool WorkTypeRegistry::hasCategory(const std::string& defName) const {
		return categories.find(defName) != categories.end();
	}

	std::vector<const WorkCategoryDef*> WorkTypeRegistry::getAllCategories() const {
		std::vector<const WorkCategoryDef*> result;
		result.reserve(categories.size());
		for (const auto& [_, cat] : categories) {
			result.push_back(&cat);
		}
		// Sort by tier (ascending = highest priority first)
		std::sort(result.begin(), result.end(), [](const WorkCategoryDef* a, const WorkCategoryDef* b) { return a->tier < b->tier; });
		return result;
	}

	std::vector<std::string> WorkTypeRegistry::getCategoryNames() const {
		std::vector<std::string> names;
		names.reserve(categories.size());
		for (const auto& [name, _] : categories) {
			names.push_back(name);
		}
		return names;
	}

	const WorkTypeDef* WorkTypeRegistry::getWorkType(const std::string& defName) const {
		auto it = workTypes.find(defName);
		if (it != workTypes.end()) {
			return &it->second;
		}
		return nullptr;
	}

	bool WorkTypeRegistry::hasWorkType(const std::string& defName) const {
		return workTypes.find(defName) != workTypes.end();
	}

	std::vector<const WorkTypeDef*> WorkTypeRegistry::getWorkTypesInCategory(const std::string& categoryDefName) const {
		std::vector<const WorkTypeDef*> result;
		auto							catIt = categories.find(categoryDefName);
		if (catIt == categories.end()) {
			return result;
		}

		for (const auto& workTypeDefName : catIt->second.workTypeDefNames) {
			if (const auto* wt = getWorkType(workTypeDefName)) {
				result.push_back(wt);
			}
		}
		return result;
	}

	std::vector<const WorkTypeDef*> WorkTypeRegistry::getWorkTypesForCapability(const std::string& capabilityName) const {
		auto it = byCapability.find(capabilityName);
		if (it != byCapability.end()) {
			return it->second;
		}
		return {};
	}

	std::vector<std::string> WorkTypeRegistry::getWorkTypeNames() const {
		std::vector<std::string> names;
		names.reserve(workTypes.size());
		for (const auto& [name, _] : workTypes) {
			names.push_back(name);
		}
		return names;
	}

	size_t WorkTypeRegistry::categoryCount() const {
		return categories.size();
	}

	size_t WorkTypeRegistry::workTypeCount() const {
		return workTypes.size();
	}

	void WorkTypeRegistry::buildCapabilityIndex() {
		byCapability.clear();

		for (const auto& [_, workType] : workTypes) {
			if (!workType.triggerCapability.empty()) {
				byCapability[workType.triggerCapability].push_back(&workType);
			}
		}
	}

} // namespace assets
