// Action Type Registry Implementation
// Handles XML parsing with pugixml for action type definitions.

#include "assets/ActionTypeRegistry.h"

#include "assets/ConfigNumber.h"
#include "assets/ConfigXml.h"

#include <utils/Log.h>

#include <pugixml.hpp>

#include <optional>
#include <sstream>
#include <string_view>
#include <utility>

namespace assets {

	namespace {

		std::optional<ActionTypeDef>
		actionFrom(pugi::xml_node node, const std::string& source, ConfigReader& reader, std::vector<std::string>& errors) {
			ActionTypeDef action;
			action.defName = node.attribute("defName").as_string();
			if (action.defName.empty()) {
				errors.push_back(source + ": an <Action> has no defName");
				return std::nullopt;
			}
			const std::size_t errorsBefore = errors.size();
			const std::size_t problemsBefore = reader.problemCount();
			const std::string what = ConfigReader::describe(node, "Action");
			onlyAttributes(node, {"defName", "needsHands"}, what, source, errors);

			for (const pugi::xml_node child : node.children()) {
				if (child.type() != pugi::node_element) {
					continue;
				}
				const std::string_view element = child.name();
				if (element == "description") {
					action.description = child.text().as_string();
				} else {
					errors.push_back(source + ": " + what + ": unknown element <" + std::string(element) + ">");
				}
			}
			action.needsHands = reader.attributeBoolean(node, "Action", "needsHands").value_or(false);
			if (errors.size() != errorsBefore || reader.problemCount() != problemsBefore) {
				return std::nullopt;
			}
			return action;
		}

	} // namespace

	ActionTypeRegistry& ActionTypeRegistry::Get() {
		static ActionTypeRegistry instance;
		return instance;
	}

	bool ActionTypeRegistry::loadFromFile(const std::filesystem::path& configRoot, const std::filesystem::path& file) {
		clear();

		const std::string	 source = file.generic_string();
		pugi::xml_document	 document;
		const pugi::xml_node root = readRootElement(document, configRoot / file, "ActionTypes", source, problemList);
		if (!root) {
			return false;
		}

		ConfigReader reader(source);
		for (const pugi::xml_node node : root.children()) {
			if (node.type() != pugi::node_element) {
				continue;
			}
			if (std::string_view(node.name()) != "Action") {
				problemList.push_back(source + ": unknown element <" + node.name() + ">");
				continue;
			}
			std::optional<ActionTypeDef> action = actionFrom(node, source, reader, problemList);
			if (!action.has_value()) {
				continue;
			}
			const std::string defName = action->defName;
			if (!actions.emplace(defName, std::move(*action)).second) {
				problemList.push_back(source + ": <Action defName=\"" + defName + "\"> is declared twice");
			}
		}

		std::vector<std::string> values = reader.takeProblems();
		problemList.insert(problemList.end(), values.begin(), values.end());
		if (actions.empty() && problemList.empty()) {
			problemList.push_back(source + ": no <Action>");
		}
		if (!problemList.empty()) {
			actions.clear();
			LOG_ERROR(Engine, "Action types %s have %zu problem(s)", source.c_str(), problemList.size());
			return false;
		}

		LOG_INFO(Engine, "Loaded %zu action types from %s", actions.size(), source.c_str());
		return true;
	}

	void ActionTypeRegistry::clear() {
		actions.clear();
		problemList.clear();
	}

	const ActionTypeDef* ActionTypeRegistry::getAction(const std::string& defName) const {
		auto it = actions.find(defName);
		if (it != actions.end()) {
			return &it->second;
		}
		return nullptr;
	}

	bool ActionTypeRegistry::hasAction(const std::string& defName) const {
		return actions.find(defName) != actions.end();
	}

	bool ActionTypeRegistry::actionNeedsHands(const std::string& defName) const {
		auto it = actions.find(defName);
		if (it != actions.end()) {
			return it->second.needsHands;
		}
		return false;
	}

	std::vector<std::string> ActionTypeRegistry::getActionNames() const {
		std::vector<std::string> names;
		names.reserve(actions.size());
		for (const auto& [name, _] : actions) {
			names.push_back(name);
		}
		return names;
	}

	std::string ActionTypeRegistry::getAvailableActionsString() const {
		std::ostringstream oss;
		bool			   first = true;
		for (const auto& [name, _] : actions) {
			if (!first) {
				oss << ", ";
			}
			oss << name;
			first = false;
		}
		return oss.str();
	}

	size_t ActionTypeRegistry::size() const {
		return actions.size();
	}

	void ActionTypeRegistry::registerTestAction(ActionTypeDef action) {
		const std::string defName = action.defName;
		actions.insert_or_assign(defName, std::move(action));
	}

} // namespace assets
