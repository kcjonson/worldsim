#pragma once

// The action entries the hand-written action factories and the hands rule take their values from, each resolved
// once at load from the behavior library to its handle and plain values, so nothing reads the library when an
// action starts. See /docs/technical/queries-and-chains.md (Actions) and behavior-data-migration.md (ticket 11).

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
		/// The skill whose work rate sets the duration or advances the work; empty for a fixed duration.
		std::string skill;
		/// The `<restore>` rows in the order written: a need and its side effect.
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

	/// The need ids of the sim's fixed list of needs, its labels as conditions write them (hunger), given to the entry
	/// loader as the needs no behavior brings yet.
	[[nodiscard]] std::vector<std::string> trackedNeedIds();

	/// Every factory's entry from `library`, one error for each entry missing or of a shape its factory cannot read,
	/// naming the entry's file and id.
	[[nodiscard]] ActionEntriesLoad resolveActionEntries(const assets::EntryLibrary& library);

} // namespace ecs
