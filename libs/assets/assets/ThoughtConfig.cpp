#include "assets/ThoughtConfig.h"
#include "assets/AssetDefinition.h"
#include "assets/ConfigFiles.h"
#include "assets/ConfigNumber.h"
#include "assets/ConfigXml.h"

#include <utils/Log.h>

#include <pugixml.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <set>
#include <string_view>
#include <utility>

namespace assets {

	namespace {

		std::optional<ThoughtImportance> importanceFromName(std::string_view name) {
			if (name == "trivial") {
				return ThoughtImportance::Trivial;
			}
			if (name == "normal") {
				return ThoughtImportance::Normal;
			}
			if (name == "major") {
				return ThoughtImportance::Major;
			}
			return std::nullopt;
		}

		std::optional<ThoughtTrigger> triggerFromName(std::string_view name) {
			constexpr std::array<std::pair<std::string_view, ThoughtTrigger>, 7> kTriggers = {{
				{"needFalls", ThoughtTrigger::NeedFalls},
				{"needRises", ThoughtTrigger::NeedRises},
				{"actionStarts", ThoughtTrigger::ActionStarts},
				{"actionFinishes", ThoughtTrigger::ActionFinishes},
				{"firstSight", ThoughtTrigger::FirstSight},
				{"recipeLearned", ThoughtTrigger::RecipeLearned},
				{"idle", ThoughtTrigger::Idle},
			}};
			const auto found = std::ranges::find(kTriggers, name, &std::pair<std::string_view, ThoughtTrigger>::first);
			return found == kTriggers.end() ? std::nullopt : std::optional(found->second);
		}

		class Loader {
		  public:
			explicit Loader(ThoughtConfigLoad& newResult)
				: result(newResult) {}

			void loadFile(const std::filesystem::path& path, const std::string& source) {
				pugi::xml_document			 document;
				const pugi::xml_parse_result parsed = document.load_file(path.string().c_str());
				if (!parsed) {
					fail(source, std::string("not readable XML: ") + parsed.description());
					return;
				}
				const pugi::xml_node root = document.child("thoughts");
				if (!root) {
					fail(source, "the root element is not <thoughts>");
					return;
				}
				std::set<std::string> feelingsInFile;
				std::set<std::string> typesInFile;
				for (const pugi::xml_node node : root.children()) {
					if (node.type() != pugi::node_element) {
						continue;
					}
					const std::string_view element = node.name();
					if (element == "feeling") {
						loadFeeling(node, source, feelingsInFile);
					} else if (element == "thought") {
						loadThought(node, source, typesInFile);
					} else if (element == "tuning") {
						loadTuning(node, source);
					} else {
						fail(source, "unknown element <" + std::string(element) + ">");
					}
				}
			}

			void check() {
				if (!result.config.tuning.has_value()) {
					fail("thoughts", "no file gives <tuning>");
				}
			}

		  private:
			void fail(const std::string& source, const std::string& message) { result.errors.push_back(source + ": " + message); }

			/// Moves what `reader` found into the errors; true when it found nothing.
			bool absorb(ConfigReader& reader) {
				const std::vector<std::string> problems = reader.takeProblems();
				result.errors.insert(result.errors.end(), problems.begin(), problems.end());
				return problems.empty();
			}

			/// Adds `declared`, or replaces the one with its id from an earlier file; one with its id from this
			/// file is an error.
			template <typename Def>
			void declare(std::vector<Def>& declared, Def def, const std::string& kind, std::set<std::string>& inFile) {
				const std::string source = def.source;
				if (!inFile.insert(def.id).second) {
					fail(source, kind + " '" + def.id + "' is declared twice in this file");
					return;
				}
				const auto existing = std::ranges::find(declared, def.id, &Def::id);
				if (existing == declared.end()) {
					declared.push_back(std::move(def));
					return;
				}
				LOG_INFO(
					Engine,
					"Thought config: %s '%s' in %s replaces the one in %s",
					kind.c_str(),
					def.id.c_str(),
					source.c_str(),
					existing->source.c_str()
				);
				*existing = std::move(def);
			}

			void loadFeeling(pugi::xml_node node, const std::string& source, std::set<std::string>& inFile) {
				FeelingDef feeling{.id = node.attribute("id").as_string(), .label = node.attribute("label").as_string(), .source = source};
				if (feeling.id.empty()) {
					fail(source, "a <feeling> has no id");
					return;
				}
				const std::string what = "feeling '" + feeling.id + "'";
				if (!onlyAttributes(node, {"id", "label"}, what, source, result.errors)) {
					return;
				}
				if (feeling.label.empty()) {
					fail(source, what + " has no label=\"<display name>\"");
					return;
				}
				if (feeling.label.ends_with('.')) {
					fail(source, what + ": label=\"" + feeling.label + "\" ends in a period; the log adds its own");
					return;
				}
				declare(result.config.feelings, std::move(feeling), "feeling", inFile);
			}

			std::optional<ThoughtMoodDef> moodOf(pugi::xml_node node, const std::string& what, const std::string& source, bool& valid) {
				const pugi::xml_attribute mood = node.attribute("mood");
				const pugi::xml_attribute hours = node.attribute("hours");
				const pugi::xml_attribute whileAttribute = node.attribute("while");
				if (node.attribute("cause") && !whileAttribute) {
					fail(source, what + ": cause names what holds a while effect, so it needs while");
					valid = false;
				}
				if (!mood) {
					if (hours || whileAttribute) {
						fail(source, what + ": hours and while need a mood amount");
						valid = false;
					}
					return std::nullopt;
				}
				ConfigReader reader(source);
				const float	 amount = reader.labeledNumber(what, node, "mood");
				if (amount == 0.0F) {
					reader.refuseAttribute(what, node, "mood", "a signed number other than 0 (leave mood out for no effect)");
				}
				if (!absorb(reader)) {
					valid = false;
					return std::nullopt;
				}
				if (static_cast<bool>(hours) == static_cast<bool>(whileAttribute)) {
					fail(source, what + ": a mood effect lasts either hours=\"<game hours>\" or while=\"<condition>\", one of the two");
					valid = false;
					return std::nullopt;
				}
				ThoughtMoodDef effect{.amount = amount};
				if (hours) {
					effect.hours = reader.labeledNumber(what, node, "hours");
					if (*effect.hours <= 0.0F) {
						reader.refuseAttribute(what, node, "hours", "a positive number of game hours");
					}
					if (!absorb(reader)) {
						valid = false;
						return std::nullopt;
					}
				} else {
					effect.whileCondition = whileAttribute.as_string();
					if (effect.whileCondition.empty()) {
						fail(source, what + ": while is empty");
						valid = false;
						return std::nullopt;
					}
					effect.cause = node.attribute("cause").as_string();
					if (effect.cause.empty()) {
						fail(source, what + ": a while effect has no cause=\"<what holds it>\"");
						valid = false;
						return std::nullopt;
					}
					if (effect.cause.ends_with('.')) {
						fail(source, what + ": cause=\"" + effect.cause + "\" ends in a period; the log adds its own");
						valid = false;
						return std::nullopt;
					}
				}
				return effect;
			}

			/// A capability name, or for an action's `on` also kGroundName; empty stays empty, for any.
			bool knownThing(const std::string& thing, bool groundAllowed) {
				return thing.empty() || capabilityFromName(thing).has_value() || (groundAllowed && thing == kGroundName);
			}

			std::optional<ThoughtSourceDef> sourceOf(pugi::xml_node node, const std::string& what, const std::string& source) {
				const std::string_view				triggerName = node.attribute("trigger").as_string();
				const std::optional<ThoughtTrigger> trigger = triggerFromName(triggerName);
				if (!trigger.has_value()) {
					fail(
						source,
						what + ": trigger=\"" + std::string(triggerName) +
							"\" is not needFalls, needRises, actionStarts, actionFinishes, firstSight, recipeLearned, or idle"
					);
					return std::nullopt;
				}
				ThoughtSourceDef parsed{.trigger = *trigger};
				bool			 valid = true;
				switch (*trigger) {
					case ThoughtTrigger::NeedFalls:
					case ThoughtTrigger::NeedRises: {
						valid = onlyAttributes(node, {"trigger", "need", "threshold"}, what, source, result.errors);
						parsed.need = node.attribute("need").as_string();
						if (parsed.need.empty()) {
							fail(source, what + " has no need=\"<need>\"");
							valid = false;
						}
						const std::string threshold = node.attribute("threshold").as_string();
						parsed.threshold = needThresholdIndex(threshold);
						if (parsed.threshold == kNeedThresholdNames.size()) {
							fail(source, what + ": threshold=\"" + threshold + "\" is not seek, urgent, or critical");
							valid = false;
						}
						break;
					}
					case ThoughtTrigger::ActionStarts:
					case ThoughtTrigger::ActionFinishes: {
						valid = onlyAttributes(node, {"trigger", "action", "on", "first"}, what, source, result.errors);
						parsed.action = node.attribute("action").as_string();
						if (parsed.action.empty()) {
							fail(source, what + " has no action=\"<action>\"");
							valid = false;
						}
						parsed.thing = node.attribute("on").as_string();
						if (!knownThing(parsed.thing, true)) {
							fail(source, what + ": on=\"" + parsed.thing + "\" is not ground or a capability");
							valid = false;
						}
						if (const pugi::xml_attribute first = node.attribute("first")) {
							const std::string_view value = first.as_string();
							if (value != "true" && value != "false") {
								fail(source, what + ": first=\"" + std::string(value) + "\" is not \"true\" or \"false\"");
								valid = false;
							}
							parsed.first = value == "true";
						}
						break;
					}
					case ThoughtTrigger::FirstSight:
						valid = onlyAttributes(node, {"trigger", "of"}, what, source, result.errors);
						parsed.thing = node.attribute("of").as_string();
						if (!knownThing(parsed.thing, false)) {
							fail(source, what + ": of=\"" + parsed.thing + "\" is not a capability");
							valid = false;
						}
						break;
					case ThoughtTrigger::RecipeLearned:
						valid = onlyAttributes(node, {"trigger"}, what, source, result.errors);
						break;
					case ThoughtTrigger::Idle: {
						valid = onlyAttributes(node, {"trigger", "everyMinutes", "chance"}, what, source, result.errors);
						ConfigReader reader(source);
						parsed.everyMinutes = reader.labeledNumber(what, node, "everyMinutes");
						if (parsed.everyMinutes <= 0.0F) {
							reader.refuseAttribute(what, node, "everyMinutes", "a positive number of game minutes");
						}
						parsed.chance = reader.labeledNumber(what, node, "chance");
						if (parsed.chance <= 0.0F || parsed.chance > 1.0F) {
							reader.refuseAttribute(what, node, "chance", "above 0 and at most 1");
						}
						valid = absorb(reader) && valid;
						break;
					}
				}
				return valid ? std::optional(std::move(parsed)) : std::nullopt;
			}

			void loadThought(pugi::xml_node node, const std::string& source, std::set<std::string>& inFile) {
				ThoughtTypeDef type{.id = node.attribute("id").as_string(), .label = node.attribute("label").as_string(), .source = source};
				if (type.id.empty()) {
					fail(source, "a <thought> has no id");
					return;
				}
				const std::string what = "thought '" + type.id + "'";
				bool			  valid = onlyAttributes(node, {"id", "label", "importance", "stacks"}, what, source, result.errors);
				if (type.label.empty()) {
					fail(source, what + " has no label=\"<display name>\"");
					valid = false;
				} else if (type.label.ends_with('.')) {
					fail(source, what + ": label=\"" + type.label + "\" ends in a period; the log adds its own");
					valid = false;
				}

				const std::optional<ThoughtImportance> importance = importanceFromName(node.attribute("importance").as_string());
				if (!importance.has_value()) {
					fail(
						source, what + ": importance=\"" + node.attribute("importance").as_string() + "\" is not trivial, normal, or major"
					);
					valid = false;
				} else {
					type.importance = *importance;
				}
				if (const pugi::xml_attribute stacks = node.attribute("stacks")) {
					const std::string_view value = stacks.as_string();
					if (value != "true" && value != "false") {
						fail(source, what + ": stacks=\"" + std::string(value) + "\" is not \"true\" or \"false\"");
						valid = false;
					}
					type.stacks = value == "true";
				}

				for (const pugi::xml_node child : node.children()) {
					if (child.type() != pugi::node_element) {
						continue;
					}
					const std::string_view element = child.name();
					if (element == "feeling") {
						const std::string feelingWhat = what + " feeling " + std::to_string(type.feelings.size() + 1);
						valid =
							onlyAttributes(child, {"is", "when", "mood", "hours", "while", "cause"}, feelingWhat, source, result.errors) &&
							valid;
						ThoughtFeelingDef feeling{
							.feeling = child.attribute("is").as_string(), .when = child.attribute("when").as_string()
						};
						if (feeling.feeling.empty()) {
							fail(source, feelingWhat + " has no is=\"<feeling>\"");
							valid = false;
						}
						feeling.mood = moodOf(child, feelingWhat, source, valid);
						type.feelings.push_back(std::move(feeling));
					} else if (element == "line") {
						const std::string lineWhat = what + " line " + std::to_string(type.lines.size() + 1);
						valid = onlyAttributes(child, {"feeling", "when"}, lineWhat, source, result.errors) && valid;
						ThoughtLineDef line{
							.text = child.text().as_string(),
							.feeling = child.attribute("feeling").as_string(),
							.when = child.attribute("when").as_string(),
						};
						if (line.text.empty()) {
							fail(source, lineWhat + " has no text");
							valid = false;
						}
						type.lines.push_back(std::move(line));
					} else if (element == "source") {
						const std::string sourceWhat = what + " source " + std::to_string(type.sources.size() + 1);
						if (std::optional<ThoughtSourceDef> parsed = sourceOf(child, sourceWhat, source)) {
							type.sources.push_back(std::move(*parsed));
						} else {
							valid = false;
						}
					} else if (element == "ends") {
						valid = onlyAttributes(child, {"thought"}, what + " <ends>", source, result.errors) && valid;
						if (!type.ends.empty()) {
							fail(source, what + " has more than one <ends>");
							valid = false;
						}
						type.ends = child.attribute("thought").as_string();
						if (type.ends.empty()) {
							fail(source, what + ": <ends> has no thought=\"<thought type>\"");
							valid = false;
						}
					} else {
						fail(source, what + ": unknown element <" + std::string(element) + ">");
						valid = false;
					}
				}
				if (valid) {
					declare(result.config.types, std::move(type), "thought", inFile);
				}
			}

			void loadTuning(pugi::xml_node node, const std::string& source) {
				if (!onlyAttributes(
						node, {"maxLineLength", "maxSubjectNameLength", "whileCheckMinutes"}, "<tuning>", source, result.errors
					)) {
					return;
				}
				ConfigReader reader(source);
				const int	 maxLineLength = reader.attributeInteger(node, "tuning", "maxLineLength", 1);
				const int	 maxSubjectNameLength = reader.attributeInteger(node, "tuning", "maxSubjectNameLength", 1);
				const float	 whileCheckMinutes = reader.attributeNumber(node, "tuning", "whileCheckMinutes");
				if (whileCheckMinutes <= 0.0F) {
					reader.refuseAttribute("<tuning>", node, "whileCheckMinutes", "a positive number of game minutes");
				}
				if (!absorb(reader)) {
					return;
				}
				const std::optional<ThoughtKeptTuning> kept = keptOf(node, source);
				if (!kept.has_value()) {
					return;
				}
				if (result.config.tuning.has_value()) {
					LOG_INFO(Engine, "Thought config: <tuning> in %s replaces the one in %s", source.c_str(), tuningSource.c_str());
				}
				result.config.tuning = ThoughtTuning{
					.maxLineLength = static_cast<std::size_t>(maxLineLength),
					.maxSubjectNameLength = static_cast<std::size_t>(maxSubjectNameLength),
					.whileCheckMinutes = whileCheckMinutes,
					.kept = *kept,
				};
				tuningSource = source;
			}

			std::optional<ThoughtKeptTuning> keptOf(pugi::xml_node tuning, const std::string& source) {
				pugi::xml_node kept;
				bool		   valid = true;
				for (const pugi::xml_node child : tuning.children()) {
					if (child.type() != pugi::node_element) {
						continue;
					}
					if (std::string_view(child.name()) != "kept") {
						fail(source, "<tuning>: unknown element <" + std::string(child.name()) + ">");
						valid = false;
					} else if (kept) {
						fail(source, "<tuning> has more than one <kept>");
						valid = false;
					} else {
						kept = child;
					}
				}
				if (!kept) {
					fail(source, "<tuning> has no <kept>");
					return std::nullopt;
				}
				valid = onlyAttributes(
							kept,
							{"limit", "pruneStartsAt", "pruneStopsAt", "recentDays", "trivialWeight", "normalWeight"},
							"<kept>",
							source,
							result.errors
						) &&
						valid;
				ConfigReader			reader(source);
				const ThoughtKeptTuning parsed{
					.limit = static_cast<std::size_t>(reader.attributeInteger(kept, "kept", "limit", 1)),
					.pruneStartsAt = reader.attributeNumber(kept, "kept", "pruneStartsAt"),
					.pruneStopsAt = reader.attributeNumber(kept, "kept", "pruneStopsAt"),
					.recentDays = reader.attributeNumber(kept, "kept", "recentDays"),
					.trivialWeight = reader.attributeNumber(kept, "kept", "trivialWeight"),
					.normalWeight = reader.attributeNumber(kept, "kept", "normalWeight"),
				};
				if (!absorb(reader) || !valid) {
					return std::nullopt;
				}
				if (!(parsed.pruneStopsAt > 0.0F && parsed.pruneStopsAt < parsed.pruneStartsAt && parsed.pruneStartsAt <= 1.0F)) {
					fail(
						source,
						"<kept> pruneStopsAt and pruneStartsAt must be fractions of the limit with 0 < pruneStopsAt < pruneStartsAt <= 1"
					);
					valid = false;
				}
				if (parsed.recentDays <= 0.0F) {
					fail(source, "<kept> recentDays must be a positive number of game days");
					valid = false;
				}
				if (parsed.trivialWeight <= 0.0F || parsed.normalWeight <= 0.0F) {
					fail(source, "<kept> trivialWeight and normalWeight must be positive numbers");
					valid = false;
				}
				if (valid) {
					const long startCount = std::lround(static_cast<double>(parsed.limit) * parsed.pruneStartsAt);
					const long stopCount = std::lround(static_cast<double>(parsed.limit) * parsed.pruneStopsAt);
					if (stopCount < 1 || stopCount >= startCount) {
						fail(
							source,
							"<kept> limit=\"" + std::to_string(parsed.limit) + "\" is too small: pruneStartsAt and pruneStopsAt round to " +
								std::to_string(startCount) + " and " + std::to_string(stopCount) +
								" thoughts, which must differ with the stop at least 1"
						);
						valid = false;
					}
				}
				return valid ? std::optional(parsed) : std::nullopt;
			}

			ThoughtConfigLoad& result;
			std::string		   tuningSource;
		};

	} // namespace

	ThoughtConfigLoad loadThoughtConfig(const std::filesystem::path& folder) {
		ThoughtConfigLoad result;
		const ConfigFiles found = configFilesIn(folder);
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
