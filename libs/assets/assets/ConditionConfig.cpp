#include "assets/ConditionConfig.h"

#include <pugixml.hpp>

#include <algorithm>
#include <array>
#include <sstream>
#include <string_view>
#include <utility>

namespace assets {

	namespace {

		constexpr const char* kKindSet = "kind";
		constexpr const char* kTaskSet = "task";

		std::optional<ConditionValueType> valueTypeFromName(std::string_view name) {
			if (name == "Truth") {
				return ConditionValueType::Truth;
			}
			if (name == "Number") {
				return ConditionValueType::Number;
			}
			if (name == "Symbol") {
				return ConditionValueType::Symbol;
			}
			if (name == "Target") {
				return ConditionValueType::Target;
			}
			return std::nullopt;
		}

		std::optional<CapabilityType> capabilityFromName(std::string_view name) {
			constexpr std::array<std::pair<std::string_view, CapabilityType>, 9> kCapabilities = {{
				{"Edible", CapabilityType::Edible},
				{"Drinkable", CapabilityType::Drinkable},
				{"Sleepable", CapabilityType::Sleepable},
				{"Toilet", CapabilityType::Toilet},
				{"Waste", CapabilityType::Waste},
				{"Carryable", CapabilityType::Carryable},
				{"Harvestable", CapabilityType::Harvestable},
				{"Craftable", CapabilityType::Craftable},
				{"Storage", CapabilityType::Storage},
			}};
			for (const auto& [capabilityName, capability] : kCapabilities) {
				if (capabilityName == name) {
					return capability;
				}
			}
			return std::nullopt;
		}

		std::vector<std::string> splitNames(const char* text) {
			std::vector<std::string> names;
			std::istringstream		 stream(text);
			std::string				 name;
			while (stream >> name) {
				names.push_back(std::move(name));
			}
			return names;
		}

		bool contains(const std::vector<std::string>& names, std::string_view name) {
			return std::ranges::find(names, name) != names.end();
		}

		class Loader {
		  public:
			explicit Loader(ConditionConfigLoad& newResult)
				: result(newResult) {}

			void loadFile(const std::filesystem::path& path, const std::string& source) {
				pugi::xml_document			 document;
				const pugi::xml_parse_result parsed = document.load_file(path.string().c_str());
				if (!parsed) {
					fail(source, std::string("not readable XML: ") + parsed.description());
					return;
				}
				const pugi::xml_node root = document.child("ConditionVocabulary");
				if (!root) {
					fail(source, "the root element is not <ConditionVocabulary>");
					return;
				}
				for (const pugi::xml_node node : root.children()) {
					if (node.type() != pugi::node_element) {
						continue;
					}
					const std::string_view element = node.name();
					if (element == "Term") {
						loadTerm(node, source);
					} else if (element == "Symbols") {
						loadSymbols(node, source);
					} else if (element == "Kind") {
						loadKind(node, source);
					} else if (element == "TaskGroup") {
						loadTaskGroup(node, source);
					} else if (element == "Night") {
						loadNight(node, source);
					} else {
						fail(source, "unknown element <" + std::string(element) + ">");
					}
				}
			}

			void check() {
				ConditionConfig& config = result.config;
				for (const ConditionKindDef& kind : config.kinds) {
					config.symbolSets[kKindSet].push_back(kind.name);
				}
				for (const ConditionTaskGroupDef& group : config.taskGroups) {
					const std::vector<std::string>& tasks = config.symbolSets[kTaskSet];
					for (const std::string& task : group.tasks) {
						if (!contains(tasks, task)) {
							fail("task group '" + group.name + "'", "'" + task + "' is not a name in the \"task\" symbols");
						}
					}
					if (contains(tasks, group.name)) {
						fail("task group '" + group.name + "'", "its name is already a name in the \"task\" symbols");
					}
				}
				for (const ConditionTaskGroupDef& group : config.taskGroups) {
					config.symbolSets[kTaskSet].push_back(group.name);
				}
				for (const ConditionTermDef& term : config.terms) {
					const auto checkSet = [&](const std::string& set) {
						if (!config.symbolSets.contains(set)) {
							fail(term.source, "term '" + term.name + "' takes names from \"" + set + "\", which no file declares");
						}
					};
					if (term.result == ConditionValueType::Symbol) {
						checkSet(term.resultSymbols);
					}
					for (const ConditionParameterDef& parameter : term.parameters) {
						if (parameter.type == ConditionValueType::Symbol) {
							checkSet(parameter.symbols);
						}
					}
				}
				if (!config.night.has_value()) {
					fail("conditions", "no file gives <Night> hours");
				}
			}

		  private:
			void fail(const std::string& source, const std::string& message) { result.errors.push_back(source + ": " + message); }

			std::optional<ConditionValueType> typeOf(pugi::xml_node node, const char* attribute, const std::string& source) {
				const std::string_view					name = node.attribute(attribute).as_string();
				const std::optional<ConditionValueType> type = valueTypeFromName(name);
				if (!type.has_value()) {
					fail(source, "'" + std::string(name) + "' is not a type (Truth, Number, Symbol, or Target)");
				}
				return type;
			}

			/// The symbol set a typed declaration names: required for a Symbol, refused for anything else.
			bool symbolsOf(
				pugi::xml_node	   node,
				ConditionValueType type,
				std::string&	   symbols,
				const std::string& what,
				const std::string& source
			) {
				symbols = node.attribute("symbols").as_string();
				if (type == ConditionValueType::Symbol && symbols.empty()) {
					fail(source, what + " is a Symbol and needs symbols=\"<set>\"");
					return false;
				}
				if (type != ConditionValueType::Symbol && !symbols.empty()) {
					fail(source, what + " is not a Symbol and takes no symbols");
					return false;
				}
				return true;
			}

			void loadTerm(pugi::xml_node node, const std::string& source) {
				ConditionTermDef term;
				term.name = node.attribute("name").as_string();
				term.source = source;
				if (term.name.empty()) {
					fail(source, "a <Term> has no name");
					return;
				}
				const std::string						what = "term '" + term.name + "'";
				const std::optional<ConditionValueType> resultType = typeOf(node, "result", source);
				if (!resultType.has_value() || !symbolsOf(node, *resultType, term.resultSymbols, what, source)) {
					return;
				}
				term.result = *resultType;
				for (const pugi::xml_node parameterNode : node.children("Parameter")) {
					ConditionParameterDef parameter;
					const std::string	  parameterWhat = what + " parameter " + std::to_string(term.parameters.size() + 1);
					const std::optional<ConditionValueType> type = typeOf(parameterNode, "type", source);
					if (!type.has_value() || !symbolsOf(parameterNode, *type, parameter.symbols, parameterWhat, source)) {
						return;
					}
					parameter.type = *type;
					term.parameters.push_back(std::move(parameter));
				}
				const auto existing = std::ranges::find(result.config.terms, term.name, &ConditionTermDef::name);
				if (existing != result.config.terms.end()) {
					fail(source, what + " is already declared in " + existing->source);
					return;
				}
				result.config.terms.push_back(std::move(term));
			}

			void loadSymbols(pugi::xml_node node, const std::string& source) {
				const std::string set = node.attribute("name").as_string();
				if (set.empty()) {
					fail(source, "a <Symbols> has no name");
					return;
				}
				if (set == kKindSet) {
					fail(source, "kinds are declared with <Kind>, not <Symbols name=\"kind\">");
					return;
				}
				std::vector<std::string>& names = result.config.symbolSets[set];
				for (std::string& name : splitNames(node.text().as_string())) {
					if (contains(names, name)) {
						fail(source, "'" + name + "' is already in the \"" + set + "\" symbols");
						continue;
					}
					names.push_back(std::move(name));
				}
			}

			void loadKind(pugi::xml_node node, const std::string& source) {
				ConditionKindDef kind;
				kind.name = node.attribute("name").as_string();
				if (kind.name.empty()) {
					fail(source, "a <Kind> has no name");
					return;
				}
				const std::string what = "kind '" + kind.name + "'";
				if (const pugi::xml_attribute capability = node.attribute("capability")) {
					kind.capability = capabilityFromName(capability.as_string());
					if (!kind.capability.has_value()) {
						fail(source, what + ": '" + capability.as_string() + "' is not a capability");
						return;
					}
				}
				if (const pugi::xml_attribute remembered = node.attribute("remembered")) {
					if (std::string_view(remembered.as_string()) != "colonists") {
						fail(source, what + ": remembered=\"" + remembered.as_string() + "\" is not \"colonists\"");
						return;
					}
					kind.colonists = true;
				}
				if (kind.capability.has_value() && kind.colonists) {
					fail(source, what + " has both a capability and remembered=\"colonists\"");
					return;
				}
				if (std::ranges::find(result.config.kinds, kind.name, &ConditionKindDef::name) != result.config.kinds.end()) {
					fail(source, what + " is already declared");
					return;
				}
				result.config.kinds.push_back(std::move(kind));
			}

			void loadTaskGroup(pugi::xml_node node, const std::string& source) {
				ConditionTaskGroupDef group{.name = node.attribute("name").as_string(), .tasks = splitNames(node.text().as_string())};
				if (group.name.empty()) {
					fail(source, "a <TaskGroup> has no name");
					return;
				}
				if (std::ranges::find(result.config.taskGroups, group.name, &ConditionTaskGroupDef::name) !=
					result.config.taskGroups.end()) {
					fail(source, "task group '" + group.name + "' is already declared");
					return;
				}
				result.config.taskGroups.push_back(std::move(group));
			}

			void loadNight(pugi::xml_node node, const std::string& source) {
				if (result.config.night.has_value()) {
					fail(source, "<Night> is already given");
					return;
				}
				const pugi::xml_attribute start = node.attribute("start");
				const pugi::xml_attribute end = node.attribute("end");
				const auto				  isHour = [](pugi::xml_attribute hour) {
					   return hour && hour.as_float(-1.0F) >= 0.0F && hour.as_float() < 24.0F;
				};
				if (!isHour(start) || !isHour(end)) {
					fail(source, "<Night> needs start and end hours from 0 up to 24");
					return;
				}
				result.config.night = NightHours{.start = start.as_float(), .end = end.as_float()};
			}

			ConditionConfigLoad& result;
		};

	} // namespace

	ConditionConfigLoad loadConditionConfig(const std::filesystem::path& folder) {
		ConditionConfigLoad result;
		std::error_code		error;
		if (!std::filesystem::is_directory(folder, error)) {
			result.errors.push_back(folder.generic_string() + ": not a folder");
			return result;
		}

		std::vector<std::filesystem::path> files;
		for (const auto& entry : std::filesystem::recursive_directory_iterator(folder, error)) {
			if (entry.is_regular_file() && entry.path().extension() == ".xml") {
				files.push_back(entry.path());
			}
		}
		if (error) {
			result.errors.push_back(folder.generic_string() + ": " + error.message());
			return result;
		}
		std::ranges::sort(files);

		Loader loader(result);
		for (const std::filesystem::path& file : files) {
			loader.loadFile(file, std::filesystem::relative(file, folder).generic_string());
		}
		loader.check();
		return result;
	}

} // namespace assets
