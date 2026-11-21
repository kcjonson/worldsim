#include "ActionEntries.h"

#include <conditions/ColonistTerms.h>

#include <algorithm>
#include <optional>
#include <string_view>
#include <variant>

namespace ecs {

	namespace {

		/// The one effect besides `<restore>` and `<spawn>` an action's factory performs, required exactly once.
		enum class Performs : uint8_t { Nothing, Consume, Take, Put, Craft, Install, WorkUp, WorkDown };

		/// How the action's `<restore>` rows are written.
		enum class RestoreForm : uint8_t { None, Amount, Nutrition, SleepQuality };

		/// Everything a factory reads from its entry, the only shape its entry may have.
		struct Expected {
			std::string_view id;
			std::string_view factory;
			std::string_view file;
			ActionValues ActionEntries::*		values;
			assets::ActionDuration				duration;
			assets::Pays						pays;
			bool								asleep;
			std::optional<assets::ActionTarget> on;
			Performs							performs;
			RestoreForm							restoreForm;
			/// The need the action serves, restored by its primary row; Count for none.
			NeedType primary;
			bool	 spawns;
		};

		using assets::ActionDuration;
		using assets::ActionTarget;
		using assets::CapabilityType;
		using assets::Pays;

		constexpr ActionTarget kLooseItem{.kind = ActionTarget::Kind::LooseItem};
		constexpr ActionTarget kPackaged{.kind = ActionTarget::Kind::Packaged};
		constexpr ActionTarget kStorage{.kind = ActionTarget::Kind::Capability, .capability = CapabilityType::Storage};
		constexpr ActionTarget kHarvestable{.kind = ActionTarget::Kind::Capability, .capability = CapabilityType::Harvestable};

		constexpr std::array<Expected, 16> kExpected = {{
			{"human/eat",
			 "Eat",
			 "behaviors/human/eating.xml",
			 &ActionEntries::eat,
			 ActionDuration::Seconds,
			 Pays::End,
			 false,
			 std::nullopt,
			 Performs::Consume,
			 RestoreForm::Nutrition,
			 NeedType::Hunger,
			 false},
			{"common/drink",
			 "Drink",
			 "behaviors/common/drinking_at_water.xml",
			 &ActionEntries::drink,
			 ActionDuration::Seconds,
			 Pays::End,
			 false,
			 std::nullopt,
			 Performs::Nothing,
			 RestoreForm::Amount,
			 NeedType::Thirst,
			 false},
			{"human/wash",
			 "Wash",
			 "behaviors/human/washing.xml",
			 &ActionEntries::wash,
			 ActionDuration::Seconds,
			 Pays::End,
			 false,
			 std::nullopt,
			 Performs::Nothing,
			 RestoreForm::Amount,
			 NeedType::Hygiene,
			 false},
			{"human/sleep",
			 "Sleep",
			 "behaviors/human/sleeping.xml",
			 &ActionEntries::sleep,
			 ActionDuration::Seconds,
			 Pays::End,
			 true,
			 std::nullopt,
			 Performs::Nothing,
			 RestoreForm::SleepQuality,
			 NeedType::Energy,
			 false},
			{"human/urinate",
			 "Toilet",
			 "behaviors/human/relieving.xml",
			 &ActionEntries::urinate,
			 ActionDuration::Seconds,
			 Pays::End,
			 false,
			 std::nullopt,
			 Performs::Nothing,
			 RestoreForm::Amount,
			 NeedType::Bladder,
			 false},
			{"human/defecate",
			 "Toilet",
			 "behaviors/human/relieving.xml",
			 &ActionEntries::defecate,
			 ActionDuration::Seconds,
			 Pays::End,
			 false,
			 std::nullopt,
			 Performs::Nothing,
			 RestoreForm::Amount,
			 NeedType::Digestion,
			 true},
			{"human/relieve_both",
			 "Toilet",
			 "behaviors/human/relieving.xml",
			 &ActionEntries::relieveBoth,
			 ActionDuration::Seconds,
			 Pays::End,
			 false,
			 std::nullopt,
			 Performs::Nothing,
			 RestoreForm::Amount,
			 NeedType::Bladder,
			 true},
			{"pickup",
			 "Pickup",
			 "actions/",
			 &ActionEntries::pickup,
			 ActionDuration::Seconds,
			 Pays::End,
			 false,
			 kLooseItem,
			 Performs::Take,
			 RestoreForm::None,
			 NeedType::Count,
			 false},
			{"withdraw",
			 "Withdraw",
			 "actions/",
			 &ActionEntries::withdraw,
			 ActionDuration::Seconds,
			 Pays::End,
			 false,
			 kStorage,
			 Performs::Take,
			 RestoreForm::None,
			 NeedType::Count,
			 false},
			{"harvest",
			 "Harvest",
			 "actions/",
			 &ActionEntries::harvest,
			 ActionDuration::FromDurability,
			 Pays::End,
			 false,
			 kHarvestable,
			 Performs::Take,
			 RestoreForm::None,
			 NeedType::Count,
			 false},
			{"craft",
			 "Craft",
			 "actions/",
			 &ActionEntries::craft,
			 ActionDuration::FromJob,
			 Pays::End,
			 false,
			 std::nullopt,
			 Performs::Craft,
			 RestoreForm::None,
			 NeedType::Count,
			 false},
			{"deposit",
			 "Deposit",
			 "actions/",
			 &ActionEntries::deposit,
			 ActionDuration::Seconds,
			 Pays::End,
			 false,
			 std::nullopt,
			 Performs::Put,
			 RestoreForm::None,
			 NeedType::Count,
			 false},
			{"build",
			 "Build",
			 "actions/",
			 &ActionEntries::build,
			 ActionDuration::OpenEnded,
			 Pays::Along,
			 false,
			 std::nullopt,
			 Performs::WorkUp,
			 RestoreForm::None,
			 NeedType::Count,
			 false},
			{"deconstruct",
			 "Deconstruct",
			 "actions/",
			 &ActionEntries::deconstruct,
			 ActionDuration::OpenEnded,
			 Pays::Along,
			 false,
			 std::nullopt,
			 Performs::WorkDown,
			 RestoreForm::None,
			 NeedType::Count,
			 false},
			{"pickup_packaged",
			 "PickupPackaged",
			 "actions/",
			 &ActionEntries::pickupPackaged,
			 ActionDuration::Seconds,
			 Pays::End,
			 false,
			 kPackaged,
			 Performs::Take,
			 RestoreForm::None,
			 NeedType::Count,
			 false},
			{"place_packaged",
			 "PlacePackaged",
			 "actions/",
			 &ActionEntries::placePackaged,
			 ActionDuration::Seconds,
			 Pays::End,
			 false,
			 std::nullopt,
			 Performs::Install,
			 RestoreForm::None,
			 NeedType::Count,
			 false},
		}};

		/// The one definition the engine spawns for a relief (ticket 13 moves the spawn effect to data).
		constexpr std::string_view kSpawnedDefinition = "Misc_BioPile";

		constexpr std::size_t factIndex(std::string_view name) {
			for (std::size_t index = 0; index < assets::kFacts.size(); ++index) {
				if (assets::kFacts[index].name == name) {
					return index;
				}
			}
			return assets::kFacts.size();
		}

		constexpr std::size_t kSleepQualityFact = factIndex("sleep_quality");
		static_assert(kSleepQualityFact < assets::kFacts.size(), "the sim provides the sleep_quality fact");

		[[nodiscard]] std::string_view shortOf(std::string_view id) {
			const std::size_t slash = id.rfind('/');
			return slash == std::string_view::npos ? id : id.substr(slash + 1);
		}

		[[nodiscard]] std::string targetName(const ActionTarget& target) {
			switch (target.kind) {
				case ActionTarget::Kind::LooseItem:
					return "loose_item";
				case ActionTarget::Kind::Packaged:
					return "packaged";
				case ActionTarget::Kind::Capability:
					break;
			}
			return assets::capabilityName(target.capability);
		}

		[[nodiscard]] bool sameTarget(const ActionTarget& left, const ActionTarget& right) {
			return left.kind == right.kind && (left.kind != ActionTarget::Kind::Capability || left.capability == right.capability);
		}

		[[nodiscard]] const char* durationName(ActionDuration duration) {
			switch (duration) {
				case ActionDuration::Seconds:
					return "a fixed duration (seconds)";
				case ActionDuration::FromDurability:
					return "a duration from the target's durability (workFrom=\"durability\")";
				case ActionDuration::FromJob:
					return "a duration from the job's work amount (workFrom=\"job\")";
				case ActionDuration::OpenEnded:
					break;
			}
			return "no duration, running until its <work> reaches its bound";
		}

		[[nodiscard]] const char* performedElement(Performs performs) {
			switch (performs) {
				case Performs::Consume:
					return "consume";
				case Performs::Take:
					return "take";
				case Performs::Put:
					return "put";
				case Performs::Craft:
					return "craft";
				case Performs::Install:
					return "install";
				case Performs::WorkUp:
				case Performs::WorkDown:
					return "work";
				case Performs::Nothing:
					break;
			}
			return "";
		}

		[[nodiscard]] Performs performedBy(const assets::EffectRow& row) {
			if (std::holds_alternative<assets::ConsumeEffect>(row)) {
				return Performs::Consume;
			}
			if (std::holds_alternative<assets::TakeEffect>(row)) {
				return Performs::Take;
			}
			if (std::holds_alternative<assets::PutEffect>(row)) {
				return Performs::Put;
			}
			if (std::holds_alternative<assets::CraftEffect>(row)) {
				return Performs::Craft;
			}
			if (std::holds_alternative<assets::InstallEffect>(row)) {
				return Performs::Install;
			}
			if (const auto* work = std::get_if<assets::WorkEffect>(&row)) {
				return work->direction == assets::WorkDirection::Down ? Performs::WorkDown : Performs::WorkUp;
			}
			return Performs::Nothing;
		}

		[[nodiscard]] const char* elementOf(const assets::EffectRow& row) {
			static constexpr std::array<const char*, 9> kElements = {
				"restore", "consume", "take", "put", "work", "craft", "spawn", "install", "damage"
			};
			return kElements[row.index()];
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
					errors.push_back(
						"action \"" + std::string(expected.id) + "\" is missing; the sim's " + std::string(expected.factory) +
						" action reads it from " + std::string(expected.file)
					);
					return values;
				}
				const assets::ActionEntry& entry = library.actions[handle.index];
				factory = expected.factory;
				where = entry.file + ": <" + (entry.group ? "actionGroup" : "action") + " id=\"" + std::string(shortOf(entry.id)) + "\">";
				if (entry.group) {
					refuse("is an action group; the sim's " + factory + " action reads one action");
					return values;
				}
				values.entry = handle;
				values.interruptable = entry.interruptable;
				values.freeHands = entry.hands == assets::Hands::Free;
				readEntry(entry, expected, values);
				readEffects(entry, expected, values);
				return values;
			}

		  private:
			void refuse(const std::string& problem) { errors.push_back(where + " " + problem); }

			void readEntry(const assets::ActionEntry& entry, const Expected& expected, ActionValues& values) {
				if (entry.duration != expected.duration) {
					refuse("takes " + std::string(durationName(expected.duration)) + " in the sim's " + factory + " action");
				} else if (entry.duration == ActionDuration::Seconds) {
					values.seconds = entry.seconds;
				} else if (entry.duration == ActionDuration::FromDurability) {
					values.skill = entry.skill;
				}
				if (entry.pays != expected.pays) {
					refuse(
						expected.pays == Pays::End ? "pays must be end; the sim's " + factory + " action pays when it completes"
												   : "pays must be along; the sim's " + factory + " action advances its work as it goes"
					);
				}
				if (entry.asleep != expected.asleep) {
					refuse(
						expected.asleep ? "asleep must be true; the sim's " + factory + " action is sleep"
										: "asleep must be left out; the sim's " + factory + " action is not sleep"
					);
				}
				if (expected.on && (!entry.on || !sameTarget(*entry.on, *expected.on))) {
					refuse("on must be " + targetName(*expected.on) + ", what the sim's " + factory + " action works on");
				} else if (!expected.on && entry.on) {
					refuse("on must be left out; the sim's " + factory + " action is chosen by no target");
				}
			}

			void readEffects(const assets::ActionEntry& entry, const Expected& expected, ActionValues& values) {
				std::size_t performed = 0;
				bool		primaryFound = false;
				std::size_t sideRows = 0;
				std::size_t spawns = 0;
				for (std::size_t index = 0; index < entry.effects.size(); ++index) {
					const assets::EffectRow& row = entry.effects[index];
					const std::string		 effect = "effect " + std::to_string(index + 1) + " <" + elementOf(row) + ">";
					if (const auto* restore = std::get_if<assets::RestoreEffect>(&row)) {
						if (expected.restoreForm == RestoreForm::None) {
							refuse(effect + " is not an effect the sim's " + factory + " action performs");
						} else if (readRestore(*restore, expected, effect, values, primaryFound, sideRows)) {
							continue;
						}
					} else if (const auto* spawn = std::get_if<assets::SpawnEffect>(&row)) {
						if (!expected.spawns) {
							refuse(effect + " is not an effect the sim's " + factory + " action performs");
						} else if (spawn->def != kSpawnedDefinition || spawn->intoHands) {
							refuse(
								effect + " must be def=\"" + std::string(kSpawnedDefinition) +
								"\" with no into: the engine spawns only that definition here (ticket 13 moves the effect to data)"
							);
						}
						++spawns;
					} else if (performedBy(row) == Performs::Nothing || performedBy(row) != expected.performs) {
						refuse(effect + " is not an effect the sim's " + factory + " action performs");
					} else {
						++performed;
						readPerformed(row, effect, values);
					}
				}
				if (expected.performs != Performs::Nothing && performed != 1) {
					refuse(
						"has " + std::to_string(performed) + " <" + performedElement(expected.performs) + "> effects; the sim's " +
						factory + " action performs one"
					);
				}
				if (expected.primary != NeedType::Count && !primaryFound) {
					refuse(
						"has no <restore> of " + conditions::conditionName(needLabel(expected.primary)) + ", the need the sim's " +
						factory + " action serves"
					);
				}
				if (expected.spawns && spawns != 1) {
					refuse("has " + std::to_string(spawns) + " <spawn> effects; the sim's " + factory + " action spawns one bio pile");
				}
				values.spawns = expected.spawns;
			}

			void readPerformed(const assets::EffectRow& row, const std::string& effect, ActionValues& values) {
				if (const auto* consume = std::get_if<assets::ConsumeEffect>(&row)) {
					if (consume->capability != CapabilityType::Edible || consume->upTo) {
						refuse(
							effect + " must consume capability=\"edible\" with no seconds, upTo, or need; the sim's " + factory +
							" action eats one food at a time"
						);
					}
					values.consumes = static_cast<uint32_t>(consume->count);
				} else if (const auto* take = std::get_if<assets::TakeEffect>(&row); take != nullptr && take->clears) {
					refuse(effect + " clears must be left out; the sim's " + factory + " action takes without clearing");
				} else if (const auto* work = std::get_if<assets::WorkEffect>(&row)) {
					values.skill = work->rate;
				}
			}

			/// True when the row is read as the primary or the side-effect restore.
			bool readRestore(
				const assets::RestoreEffect& restore,
				const Expected&				 expected,
				const std::string&			 effect,
				ActionValues&				 values,
				bool&						 primaryFound,
				std::size_t&				 sideRows
			) {
				const std::string& needId = library.needIds[restore.need.index];
				const auto		   tracked =
					std::ranges::find_if(kNeedLabels, [&needId](const char* label) { return conditions::conditionName(label) == needId; });
				if (tracked == kNeedLabels.end()) {
					refuse(effect + " need \"" + needId + "\" is no need the sim tracks");
					return false;
				}
				const auto need = static_cast<NeedType>(tracked - kNeedLabels.begin());
				const bool fromNutrition = restore.from.has_value();
				const bool scaled = restore.scaleBy.has_value();
				const bool wantsNutrition = expected.restoreForm == RestoreForm::Nutrition;
				const bool wantsSleep = expected.restoreForm == RestoreForm::SleepQuality;
				if (restore.onTarget || restore.upTo || fromNutrition != wantsNutrition || scaled != wantsSleep ||
					(scaled && restore.scaleBy->fact != kSleepQualityFact)) {
					refuse(
						effect + " must restore " +
						(wantsNutrition ? std::string("from=\"nutrition\" with a scale")
						 : wantsSleep	? std::string("an amount with scaleBy=\"sleep_quality\"")
										: std::string("a plain amount")) +
						", the creature's own, with no seconds or upTo; that is what the sim's " + factory + " action applies"
					);
					return false;
				}
				const float amount = fromNutrition ? restore.scale : *restore.amount;
				const bool	primary = need == expected.primary;
				if (primary && primaryFound) {
					refuse(effect + " restores " + needId + " a second time; the sim's " + factory + " action restores it once");
					return false;
				}
				if (primary && amount <= 0.0F) {
					refuse(
						effect + " restores " + needId + ", the need the sim's " + factory +
						" action serves, so its amount must be above 0; only its side effect may lower a need"
					);
					return false;
				}
				if (!primary && (wantsSleep || sideRows > 0)) {
					refuse(
						effect + " restores " + needId + "; the sim's " + factory + " action restores " +
						conditions::conditionName(needLabel(expected.primary)) + (wantsSleep ? " alone" : " and at most one side effect")
					);
					return false;
				}
				const std::size_t slot = primary ? 0 : 1;
				values.restores[slot] = RestoreValue{.need = need, .amount = amount};
				if (primary) {
					primaryFound = true;
				} else {
					++sideRows;
				}
				values.restoreCount = static_cast<uint8_t>(primaryFound ? 1 + sideRows : 0);
				return true;
			}

			const assets::EntryLibrary& library;
			std::vector<std::string>&	errors;
			std::string					where;
			std::string					factory;
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
		load.actions.groundSleepQuality = library.factTuning[kSleepQualityFact];
		return load;
	}

} // namespace ecs
