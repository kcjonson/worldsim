#pragma once

// Action entries resolved once at load from the behavior library to the handles and plain values the engine applies,
// so nothing reads the library or resolves an id when an action runs: resolveAction for any entry, and
// resolveActionEntries for the ones the hand-written action factories and the hands rule start. See
// /docs/technical/queries-and-chains.md (Actions) and behavior-data-migration.md (Seams).
//
// Every action, factory-started or not, applies its effect rows through the one primitive per effect kind
// (ActionSystem), in the order written. A factory supplies only the context its rows name and do not carry: the item a
// `<consume>` uses up and its nutrition, the place's sleep quality, and the target a `<take>`, `<put>`, `<work>`,
// `<craft>`, or `<install>` acts on.
//
// An entry never says one thing while the sim does another: every factory reads a fixed shape, and anything else in
// its entry is a load error naming the file, the entry, and the element. Every entry's `interruptable` and `hands`
// (left out, any) are read as written. Besides those, each reads:
//
// - human/eat (Eat): `seconds`; one `<consume capability="edible" count>`; a `<restore need="hunger"
//   from="nutrition" scale>` above 0, and at most one other `<restore from="nutrition">`, its side effect.
// - common/drink (Drink), human/wash (Wash), human/urinate, human/defecate, human/relieve_both (Toilet): `seconds`;
//   a `<restore amount>` above 0 of the need it serves (thirst, hygiene, bladder, digestion, bladder), and at most
//   one other `<restore amount>`, its side effect, of either sign. human/defecate and human/relieve_both each have
//   one `<spawn def into>`.
// - human/sleep (Sleep): `seconds`; `asleep="true"`; one `<restore need="energy" amount scaleBy="sleep_quality">`
//   above 0.
// - pickup, withdraw, pickup_packaged (Pickup, Withdraw, PickupPackaged): `seconds`; one `<take>` without `clears`;
//   `on` of loose_item, storage, and packaged.
// - harvest (Harvest): `workFrom="durability"` with `skill`; one `<take clears>`; `on="harvestable"`.
// - craft (Craft): `workFrom="job"`; one `<craft>`.
// - deposit (Deposit): `seconds`; one `<put>`.
// - place_packaged (PlacePackaged): `seconds`; one `<install>`.
// - build, deconstruct (Build, Deconstruct): `pays="along"` with no `curve`; one `<work rate direction>`, up and
//   down, the rate the skill whose level sets the work rate.
//
// Every other entry pays at its end, is not asleep, and has no `on`. Any other effect, attribute, or value is refused.

#include "components/Needs.h"

#include <assets/Entries.h>

#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <variant>
#include <vector>

namespace ecs {

	/// A `<restore>` row.
	struct RestoreRow {
		NeedType need = NeedType::Count;
		/// The step's target creature's need (`who="target"`), not the creature's own.
		bool onTarget = false;
		/// The amount, signed; for a row restoring from what the action consumes, the scale of its nutrition.
		float amount = 0.0F;
		bool  fromNutrition = false;
		/// Scaled by the target's sleep quality (`scaleBy="sleep_quality"`).
		bool bySleepQuality = false;
		/// Open-ended: `amount` per this many seconds until the need reaches `upTo`; 0 for a row that pays its amount once.
		float seconds = 0.0F;
		float upTo = 0.0F;
	};

	/// A `<consume>` row: `count` held units with the capability; open-ended, that many every `seconds` until `need`
	/// reaches `upTo`.
	struct ConsumeRow {
		assets::CapabilityType capability = assets::CapabilityType::Edible;
		uint32_t			   count = 1;
		float				   seconds = 0.0F;
		float				   upTo = 0.0F;
		NeedType			   need = NeedType::Count;
	};

	/// A `<take>` row; `clears` removes the source whatever it is.
	struct TakeRow {
		bool clears = false;
	};

	struct PutRow {};

	/// A `<work>` row: the work saved on the target advanced up, or down, at the rate the skill gives.
	struct WorkRow {
		std::string skill;
		bool		down = false;
	};

	struct CraftRow {};

	/// A `<spawn>` row: a thing of the definition where the creature stands, or in its hands.
	struct SpawnRow {
		std::string def;
		bool		intoHands = false;
	};

	struct InstallRow {};

	/// One effect row as the engine applies it. `<damage>` has no row: the sim has no condition for it to lower.
	using EffectRow = std::variant<RestoreRow, ConsumeRow, TakeRow, PutRow, WorkRow, CraftRow, SpawnRow, InstallRow>;

	/// What an open-ended action runs until: its `<work>` reaching its bound, or the `<restore>` or `<consume>` with
	/// an upTo bringing its need there.
	enum class ActionBound : uint8_t { None, Work, Restore, Consume };

	/// A `<restore>` row as a factory reports it: the need, and its amount, or for an action that consumes food, the
	/// scale of the food's nutrition.
	struct RestoreValue {
		NeedType need = NeedType::Count;
		float	 amount = 0.0F;
	};

	/// One action entry's values.
	struct ActionValues {
		assets::EntryHandle<assets::ActionEntry> entry;
		assets::ActionDuration					 duration = assets::ActionDuration::Seconds;
		/// The fixed duration; 0 for a duration from the target's durability or the job, or an open-ended action.
		float		 seconds = 0.0F;
		bool		 interruptable = false;
		bool		 freeHands = false;
		assets::Pays pays = assets::Pays::End;
		float		 curve = 1.0F;
		bool		 asleep = false;
		/// The skill whose work rate sets the duration from durability or advances the work; empty otherwise.
		std::string skill;
		/// The effect rows, in the order written.
		std::vector<EffectRow> effects;
		ActionBound			   bound = ActionBound::None;
		/// The index in `effects` of the row an open-ended action runs until.
		uint8_t boundRow = std::numeric_limits<uint8_t>::max();
		/// For a factory: the need the action serves, then its side effect; `restoreCount` of them are set.
		std::array<RestoreValue, 2> restores{};
		uint8_t						restoreCount = 0;
		/// For a factory: units a `<consume>` row uses up; 0 for an action that consumes nothing.
		uint32_t consumes = 0;
		/// For a factory: the action has a `<spawn>`.
		bool spawns = false;
	};

	struct ActionValuesLoad {
		ActionValues			 values;
		std::vector<std::string> errors;
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

	/// The action `action` of `library` as the engine runs it, with one error for each thing in it the engine cannot do:
	/// an action group, a need the sim does not track, a `scaleBy` fact other than sleep_quality, a `<damage>`, an
	/// open-ended action that does not pay along, gives a curve, or has more than one bound, and an open-ended
	/// `<restore>` that moves its need by nothing.
	[[nodiscard]] ActionValuesLoad resolveAction(const assets::EntryLibrary& library, assets::EntryHandle<assets::ActionEntry> action);

	/// Every factory's entry from `library`, one error for each entry missing or of a shape its factory does not read.
	[[nodiscard]] ActionEntriesLoad resolveActionEntries(const assets::EntryLibrary& library);

} // namespace ecs
