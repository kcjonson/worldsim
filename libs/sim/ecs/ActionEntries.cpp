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

		using assets::ActionDuration;
		using assets::ActionTarget;
		using assets::CapabilityType;
		using assets::Pays;

		/// Everything a factory reads from its entry, the only shape its entry may have.
		struct Expected {
			std::string_view id;
			std::string_view factory;
			std::string_view file;
			ActionValues ActionEntries::* values = nullptr;
			ActionDuration				  duration = ActionDuration::Seconds;
			Pays						  pays = Pays::End;
			bool						  asleep = false;
			std::optional<ActionTarget>	  on;
			Performs					  performs = Performs::Nothing;
			RestoreForm					  restoreForm = RestoreForm::None;
			/// The need the action serves, restored by its primary row; Count for none.
			NeedType primary = NeedType::Count;
			bool	 spawns = false;
		};

		constexpr ActionTarget kLooseItem{.kind = ActionTarget::Kind::LooseItem};
		constexpr ActionTarget kPackaged{.kind = ActionTarget::Kind::Packaged};
		constexpr ActionTarget kStorage{.kind = ActionTarget::Kind::Capability, .capability = CapabilityType::Storage};
		constexpr ActionTarget kHarvestable{.kind = ActionTarget::Kind::Capability, .capability = CapabilityType::Harvestable};

		constexpr std::array<Expected, 16> kExpected = {{
			{.id = "human/eat",
			 .factory = "Eat",
			 .file = "behaviors/human/eating.xml",
			 .values = &ActionEntries::eat,
			 .performs = Performs::Consume,
			 .restoreForm = RestoreForm::Nutrition,
			 .primary = NeedType::Hunger},
			{.id = "common/drink",
			 .factory = "Drink",
			 .file = "behaviors/common/drinking_at_water.xml",
			 .values = &ActionEntries::drink,
			 .restoreForm = RestoreForm::Amount,
			 .primary = NeedType::Thirst},
			{.id = "human/wash",
			 .factory = "Wash",
			 .file = "behaviors/human/washing.xml",
			 .values = &ActionEntries::wash,
			 .restoreForm = RestoreForm::Amount,
			 .primary = NeedType::Hygiene},
			{.id = "human/sleep",
			 .factory = "Sleep",
			 .file = "behaviors/human/sleeping.xml",
			 .values = &ActionEntries::sleep,
			 .asleep = true,
			 .restoreForm = RestoreForm::SleepQuality,
			 .primary = NeedType::Energy},
			{.id = "human/urinate",
			 .factory = "Toilet",
			 .file = "behaviors/human/relieving.xml",
			 .values = &ActionEntries::urinate,
			 .restoreForm = RestoreForm::Amount,
			 .primary = NeedType::Bladder},
			{.id = "human/defecate",
			 .factory = "Toilet",
			 .file = "behaviors/human/relieving.xml",
			 .values = &ActionEntries::defecate,
			 .restoreForm = RestoreForm::Amount,
			 .primary = NeedType::Digestion,
			 .spawns = true},
			{.id = "human/relieve_both",
			 .factory = "Toilet",
			 .file = "behaviors/human/relieving.xml",
			 .values = &ActionEntries::relieveBoth,
			 .restoreForm = RestoreForm::Amount,
			 .primary = NeedType::Bladder,
			 .spawns = true},
			{.id = "pickup",
			 .factory = "Pickup",
			 .file = "actions/carrying.xml",
			 .values = &ActionEntries::pickup,
			 .on = kLooseItem,
			 .performs = Performs::Take},
			{.id = "withdraw",
			 .factory = "Withdraw",
			 .file = "actions/carrying.xml",
			 .values = &ActionEntries::withdraw,
			 .on = kStorage,
			 .performs = Performs::Take},
			{.id = "harvest",
			 .factory = "Harvest",
			 .file = "actions/carrying.xml",
			 .values = &ActionEntries::harvest,
			 .duration = ActionDuration::FromDurability,
			 .on = kHarvestable,
			 .performs = Performs::Take},
			{.id = "craft",
			 .factory = "Craft",
			 .file = "actions/making.xml",
			 .values = &ActionEntries::craft,
			 .duration = ActionDuration::FromJob,
			 .performs = Performs::Craft},
			{.id = "deposit",
			 .factory = "Deposit",
			 .file = "actions/carrying.xml",
			 .values = &ActionEntries::deposit,
			 .performs = Performs::Put},
			{.id = "build",
			 .factory = "Build",
			 .file = "actions/making.xml",
			 .values = &ActionEntries::build,
			 .duration = ActionDuration::OpenEnded,
			 .pays = Pays::Along,
			 .performs = Performs::WorkUp},
			{.id = "deconstruct",
			 .factory = "Deconstruct",
			 .file = "actions/making.xml",
			 .values = &ActionEntries::deconstruct,
			 .duration = ActionDuration::OpenEnded,
			 .pays = Pays::Along,
			 .performs = Performs::WorkDown},
			{.id = "pickup_packaged",
			 .factory = "PickupPackaged",
			 .file = "actions/making.xml",
			 .values = &ActionEntries::pickupPackaged,
			 .on = kPackaged,
			 .performs = Performs::Take},
			{.id = "place_packaged",
			 .factory = "PlacePackaged",
			 .file = "actions/making.xml",
			 .values = &ActionEntries::placePackaged,
			 .performs = Performs::Install},
		}};

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

		/// Each effect's element, in the effect variant's order.
		constexpr std::array<const char*, 9> kEffectElements =
			{"restore", "consume", "take", "put", "work", "craft", "spawn", "install", "damage"};
		static_assert(kEffectElements.size() == std::variant_size_v<assets::EffectRow>, "an element for every effect");

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

		[[nodiscard]] std::string needIdOf(NeedType need) {
			return conditions::conditionName(needLabel(need));
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
			/// What the effects of one entry have shown so far.
			struct EffectTally {
				std::size_t performed = 0;
				std::size_t spawns = 0;
				std::size_t sideRows = 0;
				/// A row names the need the action serves, read or refused.
				bool primarySeen = false;
				bool primaryRead = false;
			};

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
				} else if (entry.curve != 1.0F) {
					refuse("curve must be left out; the sim's " + factory + " action advances its work evenly");
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
				EffectTally tally;
				for (std::size_t index = 0; index < entry.effects.size(); ++index) {
					const assets::EffectRow& row = entry.effects[index];
					const std::string		 effect = "effect " + std::to_string(index + 1) + " <" + kEffectElements[row.index()] + ">";
					const Performs			 performs = performedBy(row);
					if (const auto* restore = std::get_if<assets::RestoreEffect>(&row)) {
						if (expected.restoreForm == RestoreForm::None) {
							refuse(effect + " is not an effect the sim's " + factory + " action performs");
						} else {
							readRestore(*restore, expected, effect, values, tally);
						}
					} else if (const auto* spawn = std::get_if<assets::SpawnEffect>(&row)) {
						if (!expected.spawns) {
							refuse(effect + " is not an effect the sim's " + factory + " action performs");
						} else if (++tally.spawns > 1) {
							refuse(effect + " is a second <spawn>; the sim's " + factory + " action spawns one bio pile");
						} else if (spawn->def != kReliefSpawnDefinition || spawn->intoHands) {
							refuse(
								effect + " must be def=\"" + std::string(kReliefSpawnDefinition) +
								"\" with no into: the engine spawns only that definition for a relief, where the creature stands"
							);
						}
					} else if (performs == Performs::Nothing || performs != expected.performs) {
						refuse(effect + " is not an effect the sim's " + factory + " action performs");
					} else if (++tally.performed > 1) {
						refuse(effect + " is a second <" + performedElement(performs) + ">; the sim's " + factory + " action performs one");
					} else {
						readPerformed(row, effect, values);
					}
				}
				if (expected.performs != Performs::Nothing && tally.performed == 0) {
					refuse(
						"has no <" + std::string(performedElement(expected.performs)) + "> effect; the sim's " + factory +
						" action performs one"
					);
				}
				if (expected.primary != NeedType::Count && !tally.primarySeen) {
					refuse("has no <restore> of " + needIdOf(expected.primary) + ", the need the sim's " + factory + " action serves");
				}
				if (expected.spawns && tally.spawns == 0) {
					refuse("has no <spawn> effect; the sim's " + factory + " action spawns one bio pile");
				}
				values.restoreCount = static_cast<uint8_t>(tally.primaryRead ? 1 + tally.sideRows : 0);
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

			void readRestore(
				const assets::RestoreEffect& restore,
				const Expected&				 expected,
				const std::string&			 effect,
				ActionValues&				 values,
				EffectTally&				 tally
			) {
				const std::string& needId = library.needIds[restore.need.index];
				const auto		   tracked =
					std::ranges::find_if(kNeedLabels, [&needId](const char* label) { return conditions::conditionName(label) == needId; });
				if (tracked == kNeedLabels.end()) {
					refuse(effect + " need \"" + needId + "\" is no need the sim tracks");
					return;
				}
				const auto need = static_cast<NeedType>(tracked - kNeedLabels.begin());
				const bool primary = need == expected.primary;
				if (primary && tally.primarySeen) {
					refuse(effect + " restores " + needId + " a second time; the sim's " + factory + " action restores it once");
					return;
				}
				tally.primarySeen = tally.primarySeen || primary;
				const bool wantsNutrition = expected.restoreForm == RestoreForm::Nutrition;
				const bool wantsSleep = expected.restoreForm == RestoreForm::SleepQuality;
				if (!primary && (wantsSleep || tally.sideRows > 0)) {
					refuse(
						effect + " restores " + needId + "; the sim's " + factory + " action restores " + needIdOf(expected.primary) +
						(wantsSleep ? " alone" : " and at most one side effect")
					);
					return;
				}
				const bool scaled = restore.scaleBy.has_value();
				if (restore.onTarget || restore.upTo || restore.from.has_value() != wantsNutrition || scaled != wantsSleep ||
					(scaled && restore.scaleBy->fact != kSleepQualityFact)) {
					refuse(
						effect + " must restore " +
						(wantsNutrition ? std::string("from=\"nutrition\" with a scale")
						 : wantsSleep	? std::string("an amount with scaleBy=\"sleep_quality\"")
										: std::string("a plain amount")) +
						", the creature's own, with no seconds or upTo; that is what the sim's " + factory + " action applies"
					);
					return;
				}
				const float amount = wantsNutrition ? restore.scale : *restore.amount;
				if (primary && amount <= 0.0F) {
					refuse(
						effect + " restores " + needId + ", the need the sim's " + factory +
						" action serves, so its amount must be above 0; only its side effect may lower a need"
					);
					return;
				}
				values.restores[primary ? 0 : 1] = RestoreValue{.need = need, .amount = amount};
				if (primary) {
					tally.primaryRead = true;
				} else {
					++tally.sideRows;
				}
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
