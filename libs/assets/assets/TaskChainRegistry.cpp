#include "assets/TaskChainRegistry.h"

#include "assets/ConfigNumber.h"
#include "assets/ConfigXml.h"

#include <utils/Log.h>

#include <pugixml.hpp>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>

namespace assets {

	namespace {

		std::optional<TaskChainDef>
		chainFrom(pugi::xml_node node, const std::string& source, ConfigReader& reader, std::vector<std::string>& errors) {
			TaskChainDef chain;
			chain.defName = node.attribute("defName").as_string();
			if (chain.defName.empty()) {
				errors.push_back(source + ": a <Chain> has no defName");
				return std::nullopt;
			}
			const std::size_t errorsBefore = errors.size();
			const std::string what = ConfigReader::describe(node, "Chain");
			onlyAttributes(node, {"defName"}, what, source, errors);

			chain.label = chain.defName;
			pugi::xml_node steps;
			for (const pugi::xml_node child : node.children()) {
				if (child.type() != pugi::node_element) {
					continue;
				}
				const std::string_view element = child.name();
				if (element == "label") {
					chain.label = child.text().as_string();
				} else if (element == "description") {
					chain.description = child.text().as_string();
				} else if (element == "steps") {
					steps = child;
				} else {
					errors.push_back(source + ": " + what + ": unknown element <" + std::string(element) + ">");
				}
			}
			if (!steps) {
				errors.push_back(source + ": " + what + " has no <steps>");
				return std::nullopt;
			}

			for (const pugi::xml_node stepNode : steps.children()) {
				if (stepNode.type() != pugi::node_element) {
					continue;
				}
				const std::string stepWhat = what + " step " + std::to_string(chain.steps.size() + 1);
				if (std::string_view(stepNode.name()) != "Step") {
					errors.push_back(source + ": " + what + ": unknown element <" + stepNode.name() + "> in <steps>");
					continue;
				}
				onlyAttributes(stepNode, {"order", "action", "target", "optional", "requiresPreviousStep"}, stepWhat, source, errors);
				ChainStep step;
				step.order = static_cast<uint8_t>(reader.labeledInteger(stepWhat, stepNode, "order", 0, std::numeric_limits<uint8_t>::max()));
				step.actionDefName = stepNode.attribute("action").as_string();
				if (step.actionDefName.empty()) {
					errors.push_back(source + ": " + stepWhat + " has no action");
				}
				step.target = stepNode.attribute("target").as_string();
				step.optional = stepNode.attribute("optional").as_bool(false);
				step.requiresPreviousStep = stepNode.attribute("requiresPreviousStep").as_bool(true);
				chain.steps.push_back(std::move(step));
			}
			if (chain.steps.empty()) {
				errors.push_back(source + ": " + what + " has no <Step>");
			}
			if (errors.size() != errorsBefore) {
				return std::nullopt;
			}
			std::ranges::sort(chain.steps, {}, &ChainStep::order);
			return chain;
		}

	} // namespace

	TaskChainRegistry& TaskChainRegistry::Get() {
		static TaskChainRegistry instance;
		return instance;
	}

	bool TaskChainRegistry::loadFromFile(const std::filesystem::path& configRoot, const std::filesystem::path& file) {
		clear();

		const std::string	 source = file.generic_string();
		pugi::xml_document	 document;
		const pugi::xml_node root = readRootElement(document, configRoot / file, "TaskChains", source, problemList);
		if (!root) {
			return false;
		}

		ConfigReader reader(source);
		for (const pugi::xml_node node : root.children()) {
			if (node.type() != pugi::node_element) {
				continue;
			}
			if (std::string_view(node.name()) != "Chain") {
				problemList.push_back(source + ": unknown element <" + node.name() + ">");
				continue;
			}
			std::optional<TaskChainDef> chain = chainFrom(node, source, reader, problemList);
			if (!chain.has_value()) {
				continue;
			}
			const std::string defName = chain->defName;
			if (!chains.emplace(defName, std::move(*chain)).second) {
				problemList.push_back(source + ": <Chain defName=\"" + defName + "\"> is declared twice");
			}
		}

		std::vector<std::string> numbers = reader.takeProblems();
		problemList.insert(problemList.end(), numbers.begin(), numbers.end());
		if (chains.empty() && problemList.empty()) {
			problemList.push_back(source + ": no <Chain>");
		}
		if (!problemList.empty()) {
			chains.clear();
			LOG_ERROR(Engine, "Task chains %s have %zu problem(s)", source.c_str(), problemList.size());
			return false;
		}

		LOG_INFO(Engine, "Loaded %zu task chains from %s", chains.size(), source.c_str());
		return true;
	}

	void TaskChainRegistry::clear() {
		chains.clear();
		problemList.clear();
	}

	const TaskChainDef* TaskChainRegistry::getChain(const std::string& defName) const {
		auto it = chains.find(defName);
		if (it != chains.end()) {
			return &it->second;
		}
		return nullptr;
	}

	bool TaskChainRegistry::hasChain(const std::string& defName) const {
		return chains.find(defName) != chains.end();
	}

	std::vector<std::string> TaskChainRegistry::getChainNames() const {
		std::vector<std::string> names;
		names.reserve(chains.size());
		for (const auto& [name, _] : chains) {
			names.push_back(name);
		}
		return names;
	}

	const std::unordered_map<std::string, TaskChainDef>& TaskChainRegistry::getAllChains() const {
		return chains;
	}

	size_t TaskChainRegistry::size() const {
		return chains.size();
	}

} // namespace assets
