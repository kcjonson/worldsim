#pragma once

// Every action entry resolved once at load from the behavior library to the handles and plain values the engine
// applies, so nothing reads the library or resolves an id when an action runs. See
// /docs/technical/queries-and-chains.md (Actions) and behavior-data-migration.md (Seams).
//
// resolveAction holds the engine's rules, the ones every action is checked against: a need the sim tracks, scaleBy only
// sleep_quality, no `<damage>`, a `<consume>` of edible units, one unit when a restore credits its nutrition, the rows
// that can take nothing (consume, take, put, craft, install) written before every restore and spawn, none of them in a
// fixed-duration action paying along, one bound for an open-ended action, an open-ended restore that moves its need,
// and a ground `<spawn>` only of a definition with no capability that needs components.
//
// resolveActionEntries resolves every entry with it, then checks the entries the hand-written action factories and the
// hands rule start against the one shape each factory reads, a layer over those rules. An entry never says one thing
// while the sim does another: anything else in a factory's entry is a load error naming the file, the entry, and the
// element. Every entry's `interruptable` and `hands` (left out, any) are read as written. Besides those, each reads:
//
// - human/eat (Eat): `seconds`; one `<consume count>`; a `<restore need="hunger" from="nutrition" scale>` above 0, and
//   at most one other `<restore from="nutrition">`, its side effect.
// - common/drink (Drink), human/wash (Wash), human/urinate, human/defecate, human/relieve_both (Toilet): `seconds`;
//   a `<restore amount>` above 0 of the need it serves (thirst, hygiene, bladder, digestion, bladder), and at most
//   one other `<restore amount>`, its side effect, of either sign. human/defecate and human/relieve_both each have
//   one `<spawn def into>`.
// - human/sleep (Sleep): `seconds`; `asleep="true"`; one `<restore need="energy" amount scaleBy="sleep_quality">`
//   above 0.
// - pickup, withdraw, pickup_packaged (Pickup, Withdraw, PickupPackaged): `seconds`; one `<take>` without `clears`;
//   `on` of loose_item, storage, and packaged.
// - harvest (Harvest): `workFrom="durability"` with `skill`; one `<take>` without `clears` (the site clear's own entry
//   carries it); `on="harvestable"`.
// - craft (Craft): `workFrom="job"`; one `<craft>`.
// - deposit (Deposit): `seconds`; one `<put>`.
// - place_packaged (PlacePackaged): `seconds`; one `<install>`.
// - build, deconstruct (Build, Deconstruct): open-ended; one `<work rate direction>`, up and down, the rate the skill
//   whose level sets the work rate.
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

	/// A `<consume>` row: `count` held units with the capability, chosen each time it applies; open-ended, a round of
	/// every row every `seconds` until `need` reaches `upTo`.
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

	inline constexpr uint8_t kNoRow = std::numeric_limits<uint8_t>::max();

	/// One action entry's values.
	struct ActionValues {
		assets::EntryHandle<assets::ActionEntry> entry;
		/// The entry's id, for warnings.
		std::string			   id;
		assets::ActionDuration duration = assets::ActionDuration::Seconds;
		/// The fixed duration; 0 for a duration from the target's durability or the job, or an open-ended action.
		float		 seconds = 0.0F;
		bool		 interruptable = false;
		bool		 freeHands = false;
		assets::Pays pays = assets::Pays::End;
		float		 curve = 1.0F;
		/// The skill whose work rate sets the duration from durability or advances the work; empty otherwise.
		std::string skill;
		/// The effect rows, in the order written.
		std::vector<EffectRow> effects;
		ActionBound			   bound = ActionBound::None;
		/// The index in `effects` of the row an open-ended action runs until.
		uint8_t boundRow = kNoRow;
		/// The index in `effects` of the first `<restore>` that raises a need, the need the action serves; what it pays
		/// is what the completion log reports.
		uint8_t primaryRow = kNoRow;
	};

	struct ActionValuesLoad {
		ActionValues			 values;
		std::vector<std::string> errors;
	};

	/// The actions a hand-written factory, or the hands rule, starts.
	enum class FactoryAction : uint8_t {
		Eat,
		Drink,
		Wash,
		Sleep,
		Urinate,
		Defecate,
		RelieveBoth,
		Pickup,
		Withdraw,
		Harvest,
		Craft,
		Deposit,
		Build,
		Deconstruct,
		PickupPackaged,
		PlacePackaged,
		Count
	};

	/// Every action entry of the library, resolved, and the ones the factories start.
	struct ActionEntries {
		/// Each action entry's values, by its handle's index; an action group's slot holds nothing.
		std::vector<ActionValues> entries;
		/// The entry each factory starts.
		std::array<assets::EntryHandle<assets::ActionEntry>, static_cast<std::size_t>(FactoryAction::Count)> factories{};
		/// The bare ground's sleep quality (the sleep_quality fact's `ground` tuning), which scales a sleep's restore.
		float groundSleepQuality = 0.0F;

		[[nodiscard]] const ActionValues& of(assets::EntryHandle<assets::ActionEntry> action) const { return entries[action.index]; }
		[[nodiscard]] const ActionValues& of(FactoryAction action) const { return of(factories[static_cast<std::size_t>(action)]); }

		[[nodiscard]] const ActionValues& eat() const { return of(FactoryAction::Eat); }
		[[nodiscard]] const ActionValues& drink() const { return of(FactoryAction::Drink); }
		[[nodiscard]] const ActionValues& wash() const { return of(FactoryAction::Wash); }
		[[nodiscard]] const ActionValues& sleep() const { return of(FactoryAction::Sleep); }
		[[nodiscard]] const ActionValues& urinate() const { return of(FactoryAction::Urinate); }
		[[nodiscard]] const ActionValues& defecate() const { return of(FactoryAction::Defecate); }
		[[nodiscard]] const ActionValues& relieveBoth() const { return of(FactoryAction::RelieveBoth); }
		[[nodiscard]] const ActionValues& pickup() const { return of(FactoryAction::Pickup); }
		[[nodiscard]] const ActionValues& withdraw() const { return of(FactoryAction::Withdraw); }
		[[nodiscard]] const ActionValues& harvest() const { return of(FactoryAction::Harvest); }
		[[nodiscard]] const ActionValues& craft() const { return of(FactoryAction::Craft); }
		[[nodiscard]] const ActionValues& deposit() const { return of(FactoryAction::Deposit); }
		[[nodiscard]] const ActionValues& build() const { return of(FactoryAction::Build); }
		[[nodiscard]] const ActionValues& deconstruct() const { return of(FactoryAction::Deconstruct); }
		[[nodiscard]] const ActionValues& pickupPackaged() const { return of(FactoryAction::PickupPackaged); }
		[[nodiscard]] const ActionValues& placePackaged() const { return of(FactoryAction::PlacePackaged); }
	};

	struct ActionEntriesLoad {
		ActionEntries			 actions;
		std::vector<std::string> errors;
	};

	/// The need ids of the sim's fixed list of needs, its labels as conditions write them (hunger), which the entry
	/// loader takes as needs an action's effect may name (behavior-data-migration.md, Seams).
	[[nodiscard]] std::vector<std::string> trackedNeedIds();

	/// The action `action` of `library` as the engine runs it, with one error for each thing in it the engine's rules
	/// (above) refuse.
	[[nodiscard]] ActionValuesLoad resolveAction(const assets::EntryLibrary& library, assets::EntryHandle<assets::ActionEntry> action);

	/// Every action entry of `library` by resolveAction, and every factory's entry checked against its shape: one error
	/// for each fault, a factory's entry missing included.
	[[nodiscard]] ActionEntriesLoad resolveActionEntries(const assets::EntryLibrary& library);

} // namespace ecs
