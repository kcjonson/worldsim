#include "ThoughtTypes.h"

#include "World.h"
#include "components/Appearance.h"
#include "components/Colonist.h"
#include "systems/TimeSystem.h"

#include <conditions/ConditionVocabulary.h>

#include <assets/AssetRegistry.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace ecs {

	namespace {

		constexpr std::string_view kSubjectPlaceholder = "{subject}";
		constexpr double		   kGameMinutesPerHour = 60.0;

		bool startsCharacter(char byte) {
			return (static_cast<unsigned char>(byte) & 0xC0U) != 0x80U;
		}

		/// Characters, not bytes: every byte that does not continue a UTF-8 sequence starts one.
		std::size_t characterCount(std::string_view text) {
			return static_cast<std::size_t>(std::ranges::count_if(text, startsCharacter));
		}

		/// The line's length as shown, each {subject} counted as the longest name it can show.
		std::size_t shownLength(std::string_view text, std::size_t maxSubjectNameLength) {
			std::size_t length = characterCount(text);
			for (std::size_t at = text.find(kSubjectPlaceholder); at != std::string_view::npos;
				 at = text.find(kSubjectPlaceholder, at + kSubjectPlaceholder.size())) {
				length = length - kSubjectPlaceholder.size() + maxSubjectNameLength;
			}
			return length;
		}

		class Builder {
		  public:
			Builder(const conditions::ConditionVocabulary& newVocabulary, std::vector<std::string>& newErrors)
				: vocabulary(newVocabulary),
				  errors(newErrors) {}

			/// Nothing for empty text; an error, and nothing, for text that does not parse.
			std::optional<conditions::Condition> condition(const std::string& text, const std::string& where) {
				if (text.empty()) {
					return std::nullopt;
				}
				conditions::ParseResult parsed = vocabulary.parse(text);
				if (!parsed.ok()) {
					errors.push_back(where + ": " + parsed.error->describe());
					return std::nullopt;
				}
				return std::move(parsed.condition);
			}

			const conditions::ConditionVocabulary& vocabulary;
			std::vector<std::string>&			   errors;
		};

		std::optional<NeedType> needFromLabel(std::string_view label) {
			const auto found = std::ranges::find(kNeedLabels, label, [](const char* need) { return std::string_view(need); });
			return found == kNeedLabels.end() ? std::nullopt : std::optional(static_cast<NeedType>(found - kNeedLabels.begin()));
		}

		/// The loader checked the trigger's own attributes; the names that belong to the sim are checked here.
		std::optional<ThoughtSource> buildSource(
			const assets::ThoughtSourceDef& def, ThoughtEventTypeId thought, const std::string& where, std::vector<std::string>& errors
		) {
			ThoughtSource source{
				.thought = thought,
				.trigger = def.trigger,
				.threshold = def.threshold,
				.first = def.first,
				.rollEverySimulationSeconds = static_cast<double>(def.everyMinutes) * kSimulationSecondsPerGameHour / kGameMinutesPerHour,
				.chance = def.chance,
			};
			if (def.trigger == ThoughtTrigger::NeedFalls || def.trigger == ThoughtTrigger::NeedRises) {
				const std::optional<NeedType> need = needFromLabel(def.need);
				if (!need.has_value()) {
					errors.push_back(where + ": no need is called '" + def.need + "'");
					return std::nullopt;
				}
				source.need = *need;
			}
			if (def.thing == assets::kGroundName) {
				source.thing = ThingFilter::Ground;
			} else if (const std::optional<assets::CapabilityType> capability = assets::capabilityFromName(def.thing)) {
				source.thing = ThingFilter::Capability;
				source.capability = *capability;
			}
			return source;
		}

	} // namespace

	uint32_t specificityOf(const std::optional<conditions::Condition>& condition) {
		if (!condition.has_value()) {
			return 0;
		}
		return static_cast<uint32_t>(std::ranges::count(condition->nodes, conditions::NodeKind::Term, &conditions::ConditionNode::kind));
	}

	ThoughtTypes::ThoughtTypes(const conditions::ConditionVocabulary& vocabulary)
		: conditionVocabulary(vocabulary) {}

	std::vector<std::string> ThoughtTypes::load(const assets::ThoughtConfig& config) {
		std::vector<std::string> errors;
		Builder					 builder(conditionVocabulary, errors);

		std::vector<FeelingDefinition>			   newFeelings;
		std::unordered_map<std::string, FeelingId> newFeelingsByName;
		for (const assets::FeelingDef& def : config.feelings) {
			newFeelingsByName.emplace(def.id, static_cast<FeelingId>(newFeelings.size()));
			newFeelings.push_back({.name = def.id, .label = def.label});
		}

		std::unordered_map<std::string, ThoughtEventTypeId> newTypesByName;
		for (const assets::ThoughtTypeDef& def : config.types) {
			newTypesByName.emplace(def.id, static_cast<ThoughtEventTypeId>(newTypesByName.size()));
		}

		if (!config.tuning.has_value()) {
			errors.emplace_back("thoughts: no <tuning>");
			return errors;
		}
		const assets::ThoughtTuning& tuning = *config.tuning;

		std::vector<ThoughtType>   newTypes;
		std::vector<ThoughtSource> newSources;
		std::vector<ActionName>	   newActionNames;
		for (const assets::ThoughtTypeDef& def : config.types) {
			const std::string what = def.source + ": thought '" + def.id + "'";
			ThoughtType		  type{.name = def.id, .label = def.label, .importance = def.importance, .stacks = def.stacks};

			if (def.feelings.empty()) {
				errors.push_back(what + " lists no feeling, so no colonist could ever have it");
			}
			for (std::size_t index = 0; index < def.feelings.size(); ++index) {
				const assets::ThoughtFeelingDef& feelingDef = def.feelings[index];
				const std::string				 where = what + " feeling " + std::to_string(index + 1) + " ('" + feelingDef.feeling + "')";
				const auto						 found = newFeelingsByName.find(feelingDef.feeling);
				if (found == newFeelingsByName.end()) {
					errors.push_back(where + ": no feeling is called '" + feelingDef.feeling + "'");
					continue;
				}
				ThoughtFeeling feeling{.feeling = found->second, .when = builder.condition(feelingDef.when, where + " when")};
				feeling.specificity = specificityOf(feeling.when);
				if (feelingDef.mood.has_value()) {
					const assets::ThoughtMoodDef& mood = *feelingDef.mood;
					if (mood.hours.has_value()) {
						feeling.moodEffect = MoodEffect::timed(mood.amount, *mood.hours);
					} else {
						feeling.moodEffect = MoodEffect::whileCauseHolds(mood.amount);
						feeling.whileCondition = builder.condition(mood.whileCondition, where + " while");
						feeling.cause = mood.cause;
					}
				}
				type.feelings.push_back(std::move(feeling));
			}

			for (std::size_t index = 0; index < def.lines.size(); ++index) {
				const assets::ThoughtLineDef& lineDef = def.lines[index];
				const std::string			  where = what + " line " + std::to_string(index + 1);
				ThoughtLine					  line{
									  .text = lineDef.text,
									  .when = builder.condition(lineDef.when, where + " when"),
									  .namesSubject = lineDef.text.find(kSubjectPlaceholder) != std::string::npos,
				  };
				if (const std::size_t length = shownLength(lineDef.text, tuning.maxSubjectNameLength); length > tuning.maxLineLength) {
					errors.push_back(
						where + ": '" + lineDef.text + "' is " + std::to_string(length) + " characters" +
						(line.namesSubject ? " with {subject} as " + std::to_string(tuning.maxSubjectNameLength) : std::string{}) +
						", more than the " + std::to_string(tuning.maxLineLength) + " a line may be"
					);
				}
				if (!lineDef.feeling.empty()) {
					const auto found = newFeelingsByName.find(lineDef.feeling);
					if (found == newFeelingsByName.end()) {
						errors.push_back(where + ": no feeling is called '" + lineDef.feeling + "'");
					} else if (std::ranges::none_of(def.feelings, [&](const assets::ThoughtFeelingDef& feeling) {
								   return feeling.feeling == lineDef.feeling;
							   })) {
						errors.push_back(where + ": the thought never feels '" + lineDef.feeling + "', so the line could never be used");
					} else {
						line.feeling = found->second;
					}
				}
				line.specificity = specificityOf(line.when) + (lineDef.feeling.empty() ? 0U : 1U);
				type.lines.push_back(std::move(line));
			}

			for (std::size_t index = 0; index < def.sources.size(); ++index) {
				const assets::ThoughtSourceDef& sourceDef = def.sources[index];
				const std::string				where = what + " source " + std::to_string(index + 1);
				if (std::optional<ThoughtSource> source = buildSource(sourceDef, newTypesByName.at(def.id), where, errors)) {
					if (sourceDef.trigger == ThoughtTrigger::ActionStarts || sourceDef.trigger == ThoughtTrigger::ActionFinishes) {
						newActionNames.push_back({.source = newSources.size(), .written = sourceDef.action, .place = where});
					}
					newSources.push_back(*source);
				}
			}

			if (!def.ends.empty()) {
				const auto found = newTypesByName.find(def.ends);
				if (found == newTypesByName.end()) {
					errors.push_back(what + ": <ends> names '" + def.ends + "', which is no thought type");
				} else {
					type.ends = found->second;
				}
			}

			const auto mostSpecificFirst = [](const auto& left, const auto& right) {
				return left.specificity > right.specificity;
			};
			std::ranges::stable_sort(type.feelings, mostSpecificFirst);
			std::ranges::stable_sort(type.lines, mostSpecificFirst);
			newTypes.push_back(std::move(type));
		}

		// A cycle is reported once, from its type that comes first.
		for (std::size_t start = 0; start < newTypes.size(); ++start) {
			std::vector<std::size_t>		  path{start};
			std::optional<ThoughtEventTypeId> next = newTypes[start].ends;
			while (next.has_value() && std::ranges::find(path, *next) == path.end()) {
				path.push_back(*next);
				next = newTypes[*next].ends;
			}
			if (next.has_value() && *next == start && std::ranges::min(path) == start) {
				std::string chain;
				for (const std::size_t index : path) {
					chain += newTypes[index].name + " -> ";
				}
				errors.push_back(
					config.types[start].source + ": thought '" + newTypes[start].name + "': <ends> makes a cycle (" + chain +
					newTypes[start].name + ")"
				);
			}
		}

		const assets::ThoughtKeptTuning& kept = tuning.kept;
		const auto sizeAt = [&kept](float fraction) { return static_cast<std::size_t>(std::lround(static_cast<double>(kept.limit) * fraction)); };
		const ThoughtKeeping newKeeping{
			.pruneStartCount = sizeAt(kept.pruneStartsAt),
			.pruneStopCount = sizeAt(kept.pruneStopsAt),
			.recentSimulationSeconds = static_cast<double>(kept.recentDays) * kSimulationSecondsPerGameDay,
			.trivialWeight = kept.trivialWeight,
			.normalWeight = kept.normalWeight,
		};

		if (!errors.empty()) {
			return errors;
		}
		feelings = std::move(newFeelings);
		feelingsByName = std::move(newFeelingsByName);
		types = std::move(newTypes);
		thoughtSources = std::move(newSources);
		actionNames = std::move(newActionNames);
		typesByName = std::move(newTypesByName);
		whileCheckSeconds = static_cast<double>(tuning.whileCheckMinutes) * kSimulationSecondsPerGameHour / kGameMinutesPerHour;
		subjectNameLength = tuning.maxSubjectNameLength;
		thoughtKeeping = newKeeping;
		return errors;
	}

	std::vector<std::string> ThoughtTypes::resolveActions(const assets::EntryLibrary& entries) {
		std::vector<std::string> errors;
		for (const ActionName& name : actionNames) {
			const assets::EntryHandle<assets::ActionEntry> action = entries.findAction(name.written);
			if (!action.valid()) {
				errors.push_back(name.place + ": no action is called '" + name.written + "'");
			} else if (entries.actions[action.index].group) {
				errors.push_back(name.place + ": '" + name.written + "' is an action group; a source names one action");
			} else {
				thoughtSources[name.source].action = action;
			}
		}
		return errors;
	}

	std::optional<ThoughtEventTypeId> ThoughtTypes::findType(std::string_view name) const {
		const auto found = typesByName.find(std::string(name));
		return found == typesByName.end() ? std::nullopt : std::optional(found->second);
	}

	std::optional<FeelingId> ThoughtTypes::findFeeling(std::string_view name) const {
		const auto found = feelingsByName.find(std::string(name));
		return found == feelingsByName.end() ? std::nullopt : std::optional(found->second);
	}

	std::optional<std::string> thoughtSubjectName(const World& world, const ThoughtSubject& subject) {
		if (subject.kind == ThoughtSubjectKind::Kind) {
			const assets::AssetDefinition* definition = assets::AssetRegistry::Get().getDefinitionById(subject.definition);
			if (definition == nullptr) {
				return std::nullopt;
			}
			return definition->label.empty() ? definition->defName : definition->label;
		}
		if (subject.kind != ThoughtSubjectKind::Colonist && subject.kind != ThoughtSubjectKind::Entity) {
			return std::nullopt;
		}
		if (const Colonist* colonist = world.getComponent<Colonist>(subject.entity); colonist != nullptr) {
			return colonist->name.empty() ? std::nullopt : std::optional(colonist->name);
		}
		const Appearance* appearance = world.getComponent<Appearance>(subject.entity);
		if (appearance == nullptr || appearance->defName.empty()) {
			return std::nullopt;
		}
		const assets::AssetDefinition* definition = assets::AssetRegistry::Get().getDefinition(appearance->defName);
		return definition != nullptr && !definition->label.empty() ? definition->label : appearance->defName;
	}

	std::string shortenSubjectName(std::string_view name, std::size_t maxCharacters) {
		std::size_t characters = 0;
		std::size_t end = 0;
		for (; end < name.size(); ++end) {
			if (startsCharacter(name[end]) && characters++ == maxCharacters) {
				break;
			}
		}
		std::string_view shortened = name.substr(0, end);
		while (end < name.size() && shortened.ends_with(' ')) {
			shortened.remove_suffix(1);
		}
		return std::string(shortened);
	}

	std::string renderThoughtLine(const ThoughtTypes& types, const Thoughts& thoughts, const Thought& thought) {
		if (!thought.line.has_value()) {
			return {};
		}
		std::string text = types.type(thought.eventType).lines[*thought.line].text;
		if (text.find(kSubjectPlaceholder) == std::string::npos) {
			return text;
		}
		const std::string name = thought.subjectName.has_value()
									 ? shortenSubjectName(thoughts.subjectNames[*thought.subjectName], types.maxSubjectNameLength())
									 : std::string{};
		for (std::size_t at = text.find(kSubjectPlaceholder); at != std::string::npos;
			 at = text.find(kSubjectPlaceholder, at + name.size())) {
			text.replace(at, kSubjectPlaceholder.size(), name);
		}
		return text;
	}

} // namespace ecs
