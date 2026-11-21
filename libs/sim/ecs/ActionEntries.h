#pragma once

// The action entries the hand-written action factories and the hands rule take their values from, each resolved
// once at load from the behavior library to its handle and plain values, so nothing reads the library when an
// action starts. See /docs/technical/queries-and-chains.md (Actions) and behavior-data-migration.md (ticket 11).
//
// An entry never says one thing while the sim does another: every factory reads a fixed shape, and anything else in
// its entry is a load error naming the file, the entry, and the element. Every entry gives `hands` and
// `interruptable`, which are read as written. Besides those, each reads:
//
// - human/eat (Eat): `seconds`; one `<consume capability="edible" count>`; a `<restore need="hunger"
//   from="nutrition" scale>` above 0, and at most one other `<restore from="nutrition">`, its side effect.
// - common/drink (Drink), human/wash (Wash), human/urinate, human/defecate, human/relieve_both (Toilet): `seconds`;
//   a `<restore amount>` above 0 of the need it serves (thirst, hygiene, bladder, digestion, bladder), and at most
//   one other `<restore amount>`, its side effect, of either sign. human/defecate and human/relieve_both each have
//   one `<spawn def="Misc_BioPile">`, the one definition the engine spawns for a relief.
// - human/sleep (Sleep): `seconds`; `asleep="true"`; one `<restore need="energy" amount scaleBy="sleep_quality">`
//   above 0.
// - pickup, withdraw, pickup_packaged (Pickup, Withdraw, PickupPackaged): `seconds`; one `<take>` without `clears`;
//   `on` of loose_item, storage, and packaged.
// - harvest (Harvest): `workFrom="durability"` with `skill`; one `<take>` without `clears`; `on="harvestable"`.
// - craft (Craft): `workFrom="job"`; one `<craft>`.
// - deposit (Deposit): `seconds`; one `<put>`.
// - place_packaged (PlacePackaged): `seconds`; one `<install>`.
// - build, deconstruct (Build, Deconstruct): `pays="along"`; one `<work rate direction>`, up and down, the rate the
//   skill whose level sets the work rate.
//
// Every other entry pays at its end, is not asleep, and has no `on`. Any other effect, attribute, or value is refused.

#include "components/Needs.h"

#include <assets/Entries.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace ecs {

	/// A `<restore>` row as a factory applies it: the need, and its amount, or for an action that consumes food, the
	/// scale of the food's nutrition.
	struct RestoreValue {
		NeedType need = NeedType::Count;
		float	 amount = 0.0F;
	};

	/// One action entry's values.
	struct ActionValues {
		assets::EntryHandle<assets::ActionEntry> entry;
		/// The fixed duration; 0 for a duration from the target's durability or the job, or an open-ended action.
		float seconds = 0.0F;
		bool  interruptable = false;
		bool  freeHands = false;
		/// The skill whose work rate sets the duration from durability or advances the work; empty otherwise.
		std::string skill;
		/// The need the action serves, then its side effect; `restoreCount` of them are set.
		std::array<RestoreValue, 2> restores{};
		uint8_t						restoreCount = 0;
		/// Units a `<consume>` row uses up; 0 for an action that consumes nothing.
		uint32_t consumes = 0;
		bool	 spawns = false;
	};

	struct ActionEntries {
		ActionValues eat;
		ActionValues drink;
		ActionValues wash;
		ActionValues sleep;
		ActionValues urinate;
		ActionValues defecate;
		ActionValues relieveBoth;
		ActionValues pickup;
		ActionValues withdraw;
		ActionValues harvest;
		ActionValues craft;
		ActionValues deposit;
		ActionValues build;
		ActionValues deconstruct;
		ActionValues pickupPackaged;
		ActionValues placePackaged;
		/// The bare ground's sleep quality (the sleep_quality fact's `ground` tuning), which scales a sleep's restore.
		float groundSleepQuality = 0.0F;
	};

	struct ActionEntriesLoad {
		ActionEntries			 actions;
		std::vector<std::string> errors;
	};

	/// The need ids of the sim's fixed list of needs, its labels as conditions write them (hunger), which the entry
	/// loader takes as needs an action's effect may name (behavior-data-migration.md, Seams).
	[[nodiscard]] std::vector<std::string> trackedNeedIds();

	/// Every factory's entry from `library`, one error for each entry missing or of a shape its factory does not read.
	[[nodiscard]] ActionEntriesLoad resolveActionEntries(const assets::EntryLibrary& library);

} // namespace ecs
