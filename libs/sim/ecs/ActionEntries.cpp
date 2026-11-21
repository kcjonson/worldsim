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
			/// The action's `<take>` may clear its source.
			bool clears = false;
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
			 .performs = Performs::Take,
			 .clears = true},
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

		/// The sim's need for a need id; none for a need it does not track.
		[[nodiscard]] std::optional<NeedType> trackedNeed(const assets::EntryLibrary& library, assets::NeedId id) {
			const std::string& needId = library.needIds[id.index];
			const auto		   tracked =
				std::ranges::find_if(kNeedLabels, [&needId](const char* label) { return conditions::conditionName(label) == needId; });
			if (tracked == kNeedLabels.end()) {
				return std::nullopt;
			}
			return static_cast<NeedType>(tracked - kNeedLabels.begin());
		}

		[[nodiscard]] std::string whereOf(const assets::ActionEntry& entry) {
			return entry.file + ": <" + (entry.group ? "actionGroup" : "action") + " id=\"" + std::string(shortOf(entry.id)) + "\">";
		}

		/// `row` as the engine applies it; none for a `<damage>` or a need the sim does not track.
		[[nodiscard]] std::optional<EffectRow> resolveRow(const assets::EffectRow& row, const assets::EntryLibrary& library) {
			if (const auto* restore = std::get_if<assets::RestoreEffect>(&row)) {
				const std::optional<NeedType> need = trackedNeed(library, restore->need);
				if (!need) {
					return std::nullopt;
				}
				return RestoreRow{
					.need = *need,
					.onTarget = restore->onTarget,
					.amount = restore->from ? restore->scale : restore->amount.value_or(0.0F),
					.fromNutrition = restore->from.has_value(),
					.bySleepQuality = restore->scaleBy.has_value() && restore->scaleBy->fact == kSleepQualityFact,
					.seconds = restore->seconds.value_or(0.0F),
					.upTo = restore->upTo.value_or(0.0F)
				};
			}
			if (const auto* consume = std::get_if<assets::ConsumeEffect>(&row)) {
				ConsumeRow resolved{.capability = consume->capability, .count = static_cast<uint32_t>(consume->count)};
				if (consume->upTo) {
					const std::optional<NeedType> need = trackedNeed(library, *consume->need);
					if (!need) {
						return std::nullopt;
					}
					resolved.seconds = *consume->seconds;
					resolved.upTo = *consume->upTo;
					resolved.need = *need;
				}
				return resolved;
			}
			if (const auto* take = std::get_if<assets::TakeEffect>(&row)) {
				return TakeRow{.clears = take->clears};
			}
			if (std::holds_alternative<assets::PutEffect>(row)) {
				return PutRow{};
			}
			if (const auto* work = std::get_if<assets::WorkEffect>(&row)) {
				return WorkRow{.skill = work->rate, .down = work->direction == assets::WorkDirection::Down};
			}
			if (std::holds_alternative<assets::CraftEffect>(row)) {
				return CraftRow{};
			}
			if (const auto* spawn = std::get_if<assets::SpawnEffect>(&row)) {
				return SpawnRow{.def = spawn->def, .intoHands = spawn->intoHands};
			}
			if (std::holds_alternative<assets::InstallEffect>(row)) {
				return InstallRow{};
			}
			return std::nullopt;
		}

		/// The bound an effect row sets on its action: none for a row that pays once.
		[[nodiscard]] ActionBound boundOf(const EffectRow& row) {
			if (std::holds_alternative<WorkRow>(row)) {
				return ActionBound::Work;
			}
			if (const auto* restore = std::get_if<RestoreRow>(&row); restore != nullptr && restore->seconds > 0.0F) {
				return ActionBound::Restore;
			}
			if (const auto* consume = std::get_if<ConsumeRow>(&row); consume != nullptr && consume->seconds > 0.0F) {
				return ActionBound::Consume;
			}
			return ActionBound::None;
		}

		/// What every action entry has, read as written: its timing, hands, payout, and effect rows.
		void readCommon(const assets::ActionEntry& entry, const assets::EntryLibrary& library, ActionValues& values) {
			values.duration = entry.duration;
			values.seconds = entry.seconds;
			values.interruptable = entry.interruptable;
			values.freeHands = entry.hands == assets::Hands::Free;
			values.pays = entry.pays;
			values.curve = entry.curve;
			values.asleep = entry.asleep;
			if (entry.duration == ActionDuration::FromDurability) {
				values.skill = entry.skill;
			}
			for (const assets::EffectRow& row : entry.effects) {
				std::optional<EffectRow> resolved = resolveRow(row, library);
				if (!resolved) {
					continue;
				}
				if (const auto* work = std::get_if<WorkRow>(&*resolved)) {
					values.skill = work->skill;
				}
				if (const ActionBound bound = boundOf(*resolved); bound != ActionBound::None && values.bound == ActionBound::None) {
					values.bound = bound;
					values.boundRow = static_cast<uint8_t>(values.effects.size());
				}
				values.effects.push_back(std::move(*resolved));
			}
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
				where = whereOf(entry);
				if (entry.group) {
					refuse("is an action group; the sim's " + factory + " action reads one action");
					return values;
				}
				values.entry = handle;
				readCommon(entry, library, values);
				readEntry(entry, expected);
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

			void readEntry(const assets::ActionEntry& entry, const Expected& expected) {
				if (entry.duration != expected.duration) {
					refuse("takes " + std::string(durationName(expected.duration)) + " in the sim's " + factory + " action");
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
					} else if (std::holds_alternative<assets::SpawnEffect>(row)) {
						if (!expected.spawns) {
							refuse(effect + " is not an effect the sim's " + factory + " action performs");
						} else if (++tally.spawns > 1) {
							refuse(effect + " is a second <spawn>; the sim's " + factory + " action spawns one bio pile");
						}
					} else if (performs == Performs::Nothing || performs != expected.performs) {
						refuse(effect + " is not an effect the sim's " + factory + " action performs");
					} else if (++tally.performed > 1) {
						refuse(effect + " is a second <" + performedElement(performs) + ">; the sim's " + factory + " action performs one");
					} else {
						readPerformed(row, expected, effect, values);
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

			void readPerformed(const assets::EffectRow& row, const Expected& expected, const std::string& effect, ActionValues& values) {
				if (const auto* consume = std::get_if<assets::ConsumeEffect>(&row)) {
					if (consume->capability != CapabilityType::Edible || consume->upTo) {
						refuse(
							effect + " must consume capability=\"edible\" with no seconds, upTo, or need; the sim's " + factory +
							" action eats one food at a time"
						);
					}
					values.consumes = static_cast<uint32_t>(consume->count);
				} else if (const auto* take = std::get_if<assets::TakeEffect>(&row); take != nullptr && take->clears && !expected.clears) {
					refuse(effect + " clears must be left out; the sim's " + factory + " action takes without clearing");
				}
			}

			void readRestore(
				const assets::RestoreEffect& restore,
				const Expected&				 expected,
				const std::string&			 effect,
				ActionValues&				 values,
				EffectTally&				 tally
			) {
				const std::string&			  needId = library.needIds[restore.need.index];
				const std::optional<NeedType> tracked = trackedNeed(library, restore.need);
				if (!tracked) {
					refuse(effect + " need \"" + needId + "\" is no need the sim tracks");
					return;
				}
				const NeedType need = *tracked;
				const bool	   primary = need == expected.primary;
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

	ActionValuesLoad resolveAction(const assets::EntryLibrary& library, assets::EntryHandle<assets::ActionEntry> action) {
		ActionValuesLoad		   load;
		const assets::ActionEntry& entry = library.actions[action.index];
		const std::string		   where = whereOf(entry);
		if (entry.group) {
			load.errors.push_back(where + " is an action group; the engine performs one action");
			return load;
		}
		load.values.entry = action;
		readCommon(entry, library, load.values);
		std::size_t bounds = 0;
		for (std::size_t index = 0; index < entry.effects.size(); ++index) {
			const assets::EffectRow& row = entry.effects[index];
			const std::string		 effect = where + " effect " + std::to_string(index + 1) + " <" + kEffectElements[row.index()] + ">";
			if (const auto* restore = std::get_if<assets::RestoreEffect>(&row)) {
				if (!trackedNeed(library, restore->need)) {
					load.errors.push_back(effect + " need \"" + library.needIds[restore->need.index] + "\" is no need the sim tracks");
				} else if (restore->scaleBy && restore->scaleBy->fact != kSleepQualityFact) {
					load.errors.push_back(
						effect + " scaleBy must be sleep_quality, the one fact of a target the engine scales an action by"
					);
				} else if (restore->upTo && restore->amount.value_or(0.0F) == 0.0F) {
					load.errors.push_back(effect + " amount must not be 0; an open-ended restore moves its need toward upTo");
				}
			} else if (const auto* consume = std::get_if<assets::ConsumeEffect>(&row); consume != nullptr && consume->need) {
				if (!trackedNeed(library, *consume->need)) {
					load.errors.push_back(effect + " need \"" + library.needIds[consume->need->index] + "\" is no need the sim tracks");
				}
			} else if (std::holds_alternative<assets::DamageEffect>(row)) {
				load.errors.push_back(effect + " lowers a condition, and nothing in the sim has one yet");
			}
			if (const std::optional<EffectRow> resolved = resolveRow(row, library);
				resolved && boundOf(*resolved) != ActionBound::None && ++bounds > 1) {
				load.errors.push_back(effect + " is a second bound; an open-ended action runs until one");
			}
		}
		if (entry.duration == ActionDuration::OpenEnded) {
			if (entry.pays != assets::Pays::Along) {
				load.errors.push_back(where + " pays must be along; an open-ended action pays at its rate as it goes");
			} else if (entry.curve != 1.0F) {
				load.errors.push_back(
					where + " curve must be left out; an open-ended action pays at its rate, with no fraction done to shape"
				);
			}
		}
		return load;
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
