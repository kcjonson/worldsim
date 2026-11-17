#include "assets/ConditionConfig.h"
#include "assets/ConfigFiles.h"
#include "assets/ConfigNumber.h"
#include "assets/ConfigXml.h"

#include <utils/Log.h>

#include <pugixml.hpp>

#include <algorithm>
#include <string_view>
#include <utility>

namespace assets {

	namespace {

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

		bool sameSignature(const ConditionTermDef& first, const ConditionTermDef& second) {
			return first.result == second.result && first.resultSymbols == second.resultSymbols &&
				   std::ranges::equal(
					   first.parameters, second.parameters, [](const ConditionParameterDef& left, const ConditionParameterDef& right) {
						   return left.type == right.type && left.symbols == right.symbols;
					   }
				   );
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
				for (const OwnedSymbolSet& owned : kOwnedSymbolSets) {
					config.symbolSets.try_emplace(std::string(owned.name));
				}
				std::vector<std::string>& kinds = config.symbolSets.at(std::string(kKindSymbols)).names;
				for (const ConditionKindDef& kind : config.kinds) {
					kinds.push_back(kind.name);
				}
				std::vector<std::string>& tasks = config.symbolSets.at(std::string(kTaskSymbols)).names;
				for (const ConditionTaskGroupDef& group : config.taskGroups) {
					tasks.push_back(group.name);
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
				if (existing == result.config.terms.end()) {
					result.config.terms.push_back(std::move(term));
				} else if (!sameSignature(*existing, term)) {
					fail(source, what + " is declared in " + existing->source + " with a different signature");
				}
			}

			void loadSymbols(pugi::xml_node node, const std::string& source) {
				const std::string set = node.attribute("name").as_string();
				if (set.empty()) {
					fail(source, "a <Symbols> has no name");
					return;
				}
				if (const auto owned = std::ranges::find(kOwnedSymbolSets, set, &OwnedSymbolSet::name); owned != kOwnedSymbolSets.end()) {
					fail(
						source,
						"the \"" + set + "\" names come only from " + std::string(owned->owner) +
							"; a conditions file cannot declare <Symbols name=\"" + set + "\">"
					);
					return;
				}
				ConditionSymbolSetDef& declared = result.config.symbolSets[set];
				if (const pugi::xml_attribute open = node.attribute("open")) {
					const std::string_view value = open.as_string();
					if (value != "true" && value != "false") {
						fail(source, "symbol set \"" + set + "\": open=\"" + std::string(value) + "\" is not \"true\" or \"false\"");
					} else if (value == "true") {
						declared.open = true;
					}
				}
				for (std::string& name : splitNames(node.text().as_string())) {
					if (!contains(declared.names, name)) {
						declared.names.push_back(std::move(name));
					}
				}
			}

			void loadKind(pugi::xml_node node, const std::string& source) {
				ConditionKindDef kind;
				kind.name = node.attribute("name").as_string();
				if (kind.name.empty()) {
					fail(source, "a <Kind> has no name");
					return;
				}
				const std::string		  what = "kind '" + kind.name + "'";
				const pugi::xml_attribute capability = node.attribute("capability");
				const pugi::xml_attribute mover = node.attribute("mover");
				if (capability && mover) {
					fail(source, what + ": a kind is a capability or a mover, not both");
					return;
				}
				if (capability) {
					kind.capability = capabilityFromName(capability.as_string());
					if (!kind.capability.has_value()) {
						fail(source, what + ": '" + capability.as_string() + "' is not a capability");
						return;
					}
				}
				if (mover) {
					kind.mover = mover.as_string();
					if (kind.mover.empty()) {
						fail(source, what + ": mover=\"\" names no definition");
						return;
					}
				}
				std::string& declaredIn = kindSources[kind.name];
				const auto	 existing = std::ranges::find(result.config.kinds, kind.name, &ConditionKindDef::name);
				if (existing == result.config.kinds.end()) {
					result.config.kinds.push_back(std::move(kind));
				} else {
					LOG_INFO(Engine, "Condition %s: %s replaces the declaration in %s", what.c_str(), source.c_str(), declaredIn.c_str());
					*existing = std::move(kind);
				}
				declaredIn = source;
			}

			void loadTaskGroup(pugi::xml_node node, const std::string& source) {
				ConditionTaskGroupDef group{.name = node.attribute("name").as_string(), .tasks = splitNames(node.text().as_string())};
				if (group.name.empty()) {
					fail(source, "a <TaskGroup> has no name");
					return;
				}
				const auto [declared, added] = taskGroupSources.try_emplace(group.name, source);
				if (!added) {
					fail(source, "task group '" + group.name + "' is already declared in " + declared->second);
					return;
				}
				result.config.taskGroups.push_back(std::move(group));
			}

			void loadNight(pugi::xml_node node, const std::string& source) {
				if (!onlyAttributes(node, {"start", "end"}, "<Night>", source, result.errors)) {
					return;
				}
				ConfigReader reader(source);
				const auto	 hourOf = [&](const char* attribute) {
					  const float hour = reader.attributeNumber(node, "Night", attribute);
					  if (hour < 0.0F || hour >= 24.0F) {
						  reader.refuseAttribute("<Night>", node, attribute, "an hour from 0 up to 24");
					  }
					  return hour;
				};
				const float				 start = hourOf("start");
				const float				 end = hourOf("end");
				std::vector<std::string> problems = reader.takeProblems();
				if (!problems.empty()) {
					result.errors.insert(result.errors.end(), problems.begin(), problems.end());
					return;
				}
				if (result.config.night.has_value()) {
					LOG_INFO(Engine, "Condition <Night>: %s replaces the hours from %s", source.c_str(), nightSource.c_str());
				}
				result.config.night = NightHours{.start = start, .end = end};
				nightSource = source;
			}

			ConditionConfigLoad&			   result;
			std::map<std::string, std::string> kindSources;
			std::map<std::string, std::string> taskGroupSources;
			std::string						   nightSource;
		};

	} // namespace

	ConditionConfigLoad loadConditionConfig(const std::filesystem::path& folder) {
		ConditionConfigLoad result;
		const ConfigFiles	found = configFilesIn(folder);
		if (found.error.has_value()) {
			result.errors.push_back(*found.error);
			return result;
		}

		Loader loader(result);
		for (const std::filesystem::path& file : found.files) {
			loader.loadFile(file, configFileName(file, folder));
		}
		loader.check();
		return result;
	}

} // namespace assets
