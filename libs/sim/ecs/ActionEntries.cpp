#include "ActionEntries.h"

#include <conditions/ColonistTerms.h>

#include <algorithm>
#include <optional>
#include <string_view>
#include <variant>

namespace ecs {

	namespace {

		/// What a factory reads from its entry.
		enum class Shape : uint8_t {
			/// A fixed duration and nothing else read.
			Fixed,
			/// A fixed duration and one or two restores by an amount.
			Restores,
			/// A fixed duration, a consume, and one or two restores scaled by the food's nutrition.
			Meal,
			/// A fixed duration and one restore by an amount scaled by the sleep quality.
			Sleep,
			/// A duration from the target's durability at a skill's work rate.
			FromDurability,
			/// A duration from the job's work amount at a skill's work rate.
			FromJob,
			/// Open-ended work at a skill's work rate, counting the work up or down.
			WorkUp,
			WorkDown,
		};

		struct Expected {
			std::string_view id;
			ActionValues ActionEntries::* values;
			Shape						  shape;
		};

		constexpr std::array<Expected, 16> kExpected = {{
			{.id = "human/eat", .values = &ActionEntries::eat, .shape = Shape::Meal},
			{.id = "common/drink", .values = &ActionEntries::drink, .shape = Shape::Restores},
			{.id = "human/wash", .values = &ActionEntries::wash, .shape = Shape::Restores},
			{.id = "human/sleep", .values = &ActionEntries::sleep, .shape = Shape::Sleep},
			{.id = "human/urinate", .values = &ActionEntries::urinate, .shape = Shape::Restores},
			{.id = "human/defecate", .values = &ActionEntries::defecate, .shape = Shape::Restores},
			{.id = "human/relieve_both", .values = &ActionEntries::relieveBoth, .shape = Shape::Restores},
			{.id = "pickup", .values = &ActionEntries::pickup, .shape = Shape::Fixed},
			{.id = "withdraw", .values = &ActionEntries::withdraw, .shape = Shape::Fixed},
			{.id = "harvest", .values = &ActionEntries::harvest, .shape = Shape::FromDurability},
			{.id = "craft", .values = &ActionEntries::craft, .shape = Shape::FromJob},
			{.id = "deposit", .values = &ActionEntries::deposit, .shape = Shape::Fixed},
			{.id = "build", .values = &ActionEntries::build, .shape = Shape::WorkUp},
			{.id = "deconstruct", .values = &ActionEntries::deconstruct, .shape = Shape::WorkDown},
			{.id = "pickup_packaged", .values = &ActionEntries::pickupPackaged, .shape = Shape::Fixed},
			{.id = "place_packaged", .values = &ActionEntries::placePackaged, .shape = Shape::Fixed},
		}};

		[[nodiscard]] std::optional<std::size_t> sleepQualityFact() {
			const auto found = std::ranges::find(assets::kFacts, std::string_view("sleep_quality"), &assets::FactDeclaration::name);
			return found == assets::kFacts.end() ? std::nullopt : std::optional<std::size_t>(found - assets::kFacts.begin());
		}

		class Resolver {
		  public:
			Resolver(const assets::EntryLibrary& newLibrary, std::vector<std::string>& newErrors)
				: library(newLibrary),
				  errors(newErrors) {}

			ActionValues resolve(const Expected& expected) {
				ActionValues								   values;
				const assets::EntryHandle<assets::ActionEntry> handle = library.findAction(expected.id);
				if (!handle.valid()) {
					errors.push_back("[actions] no action \"" + std::string(expected.id) + "\": the sim's action factories read it");
					return values;
				}
				const assets::ActionEntry& entry = library.actions[handle.index];
				where = entry.file + ": action \"" + entry.id + "\"";
				if (entry.group) {
					errors.push_back(where + " is an action group; the sim's action factories read an action");
					return values;
				}
				values.entry = handle;
				values.interruptable = entry.interruptable;
				values.freeHands = entry.hands == assets::Hands::Free;
				readDuration(entry, expected.shape, values);
				readEffects(entry, expected.shape, values);
				return values;
			}

		  private:
			void refuse(const std::string& problem) { errors.push_back(where + " " + problem); }

			void readDuration(const assets::ActionEntry& entry, Shape shape, ActionValues& values) {
				const bool fromDurability = shape == Shape::FromDurability;
				const bool fromJob = shape == Shape::FromJob;
				const bool work = shape == Shape::WorkUp || shape == Shape::WorkDown;
				if (fromDurability || fromJob) {
					const assets::ActionDuration wanted =
						fromDurability ? assets::ActionDuration::FromDurability : assets::ActionDuration::FromJob;
					if (entry.duration != wanted) {
						refuse(
							std::string("takes its duration from ") + (fromDurability ? "workFrom=\"durability\"" : "workFrom=\"job\"") +
							" in the sim"
						);
					}
					values.skill = entry.skill;
				} else if (work) {
					if (entry.duration != assets::ActionDuration::OpenEnded) {
						refuse("is open-ended work in the sim; it takes a <work> effect and no duration");
					}
				} else if (entry.duration != assets::ActionDuration::Seconds) {
					refuse("takes a fixed duration (seconds) in the sim");
				} else {
					values.seconds = entry.seconds;
				}
			}

			void readEffects(const assets::ActionEntry& entry, Shape shape, ActionValues& values) {
				std::size_t restores = 0;
				for (const assets::EffectRow& row : entry.effects) {
					if (const auto* restore = std::get_if<assets::RestoreEffect>(&row)) {
						readRestore(*restore, shape, restores++, values);
					} else if (const auto* consume = std::get_if<assets::ConsumeEffect>(&row)) {
						if (shape != Shape::Meal || consume->upTo) {
							refuse("consumes in a way the sim's factory does not read");
						}
						values.consumes = static_cast<uint32_t>(consume->count);
					} else if (const auto* work = std::get_if<assets::WorkEffect>(&row)) {
						const assets::WorkDirection wanted =
							shape == Shape::WorkDown ? assets::WorkDirection::Down : assets::WorkDirection::Up;
						if ((shape != Shape::WorkUp && shape != Shape::WorkDown) || work->direction != wanted) {
							refuse("has a <work> effect the sim's factory does not read");
						}
						values.skill = work->rate;
					} else if (std::holds_alternative<assets::SpawnEffect>(row)) {
						values.spawns = true;
					}
				}
				const bool		  takesRestores = shape == Shape::Restores || shape == Shape::Meal || shape == Shape::Sleep;
				const std::size_t most = shape == Shape::Sleep ? 1 : values.restores.size();
				if (takesRestores && (restores == 0 || restores > most)) {
					refuse(
						"has " + std::to_string(restores) + " <restore> rows; the sim's factory reads " + (most == 1 ? "one" : "one or two")
					);
				} else if (!takesRestores && restores > 0) {
					refuse("restores a need; the sim's factory restores none");
				}
				if (shape == Shape::Meal && values.consumes == 0) {
					refuse("consumes nothing; the sim's factory eats what it consumes");
				}
				if ((shape == Shape::WorkUp || shape == Shape::WorkDown) && values.skill.empty()) {
					refuse("has no <work> effect; the sim's factory advances work");
				}
			}

			void readRestore(const assets::RestoreEffect& restore, Shape shape, std::size_t index, ActionValues& values) {
				if (index >= values.restores.size()) {
					return;
				}
				const std::string& needId = library.needIds[restore.need.index];
				const auto		   tracked =
					std::ranges::find_if(kNeedLabels, [&needId](const char* label) { return conditions::conditionName(label) == needId; });
				if (tracked == kNeedLabels.end()) {
					refuse("restores need \"" + needId + "\", which the sim does not track");
					return;
				}
				const bool fromNutrition = restore.from.has_value();
				const bool scaledBySleep = restore.scaleBy.has_value() && restore.scaleBy->fact == sleepQualityFact();
				if (restore.onTarget || restore.upTo || fromNutrition != (shape == Shape::Meal) ||
					(restore.scaleBy.has_value() != (shape == Shape::Sleep)) || (shape == Shape::Sleep && !scaledBySleep)) {
					refuse("restores \"" + needId + "\" in a way the sim's factory does not read");
					return;
				}
				values.restores[index] = RestoreValue{
					.need = static_cast<NeedType>(tracked - kNeedLabels.begin()),
					.amount = fromNutrition ? restore.scale : restore.amount.value_or(0.0F),
				};
				values.restoreCount = static_cast<uint8_t>(index + 1);
			}

			const assets::EntryLibrary& library;
			std::vector<std::string>&	errors;
			std::string					where;
		};

	} // namespace

	std::vector<std::string> trackedNeedIds() {
		std::vector<std::string> ids;
		for (const char* label : kNeedLabels) {
			ids.push_back(conditions::conditionName(label));
		}
		return ids;
	}

	ActionEntriesLoad resolveActionEntries(const assets::EntryLibrary& library) {
		ActionEntriesLoad load;
		Resolver		  resolver(library, load.errors);
		for (const Expected& expected : kExpected) {
			load.actions.*(expected.values) = resolver.resolve(expected);
		}
		if (const std::optional<std::size_t> fact = sleepQualityFact()) {
			load.actions.groundSleepQuality = library.factTuning[*fact];
		}
		return load;
	}

} // namespace ecs
