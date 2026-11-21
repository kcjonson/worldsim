#pragma once

// The behavior library's entries (behaviors, needs, chains, queries, actions, colony work, orders, fact
// tuning) and the creatures built from them, as loadEntries reads them from assets/config and the creature
// folders. Every reference between entries is resolved at load to a handle, an index into the library's
// list of that type, so nothing reads an id string at decision time. See
// /docs/technical/queries-and-chains.md.

#include "assets/AssetDefinition.h"

#include <array>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace assets {

	inline constexpr uint32_t kNoEntry = std::numeric_limits<uint32_t>::max();

	/// An index into the library's list of `Entry`.
	template <typename Entry>
	struct EntryHandle {
		uint32_t					 index = kNoEntry;
		[[nodiscard]] constexpr bool valid() const { return index != kNoEntry; }
		friend constexpr bool		 operator==(EntryHandle, EntryHandle) = default;
	};

	/// A reference to an entry as the file writes it, and the entry it resolves to.
	template <typename Entry>
	struct EntryReference {
		std::string		   written;
		EntryHandle<Entry> handle;
	};

	/// A need id, interned: an index into EntryLibrary::needIds. Need ids are plain names, the same for every
	/// creature that has the need.
	struct NeedId {
		uint16_t			  index = std::numeric_limits<uint16_t>::max();
		friend constexpr bool operator==(NeedId, NeedId) = default;
	};

	// --- Facts -------------------------------------------------------------------------------------------

	/// A fact the sim provides: its name, whether it takes one argument (`contains(tag)`), and the one tuning
	/// value it reads from assets/config/facts/, if any.
	struct FactDeclaration {
		std::string_view name;
		bool			 takesArgument;
		std::string_view tuning;
	};

	inline constexpr std::array<FactDeclaration, 26> kFacts = {{
		{.name = "distance", .takesArgument = false, .tuning = {}},
		{.name = "held", .takesArgument = false, .tuning = {}},
		{.name = "on_shore", .takesArgument = false, .tuning = "within"},
		{.name = "waste_nearby", .takesArgument = false, .tuning = "within"},
		{.name = "food_nearby", .takesArgument = false, .tuning = "within"},
		{.name = "is_place", .takesArgument = false, .tuning = {}},
		{.name = "sleep_quality", .takesArgument = false, .tuning = "ground"},
		{.name = "below_destination", .takesArgument = false, .tuning = {}},
		{.name = "quality", .takesArgument = false, .tuning = {}},
		{.name = "liking", .takesArgument = false, .tuning = {}},
		{.name = "against_diet", .takesArgument = false, .tuning = {}},
		{.name = "contains", .takesArgument = true, .tuning = {}},
		{.name = "eaten_recently", .takesArgument = false, .tuning = {}},
		{.name = "freshness", .takesArgument = false, .tuning = {}},
		{.name = "maker_opinion", .takesArgument = false, .tuning = {}},
		{.name = "fits_meal_time", .takesArgument = false, .tuning = {}},
		{.name = "comfort", .takesArgument = false, .tuning = {}},
		{.name = "opinion", .takesArgument = false, .tuning = {}},
		{.name = "mood_source", .takesArgument = false, .tuning = {}},
		{.name = "owner_opinion", .takesArgument = false, .tuning = {}},
		{.name = "has_owner", .takesArgument = false, .tuning = {}},
		{.name = "owned_by_self", .takesArgument = false, .tuning = {}},
		{.name = "owner_present", .takesArgument = false, .tuning = {}},
		{.name = "built_by_self", .takesArgument = false, .tuning = {}},
		{.name = "flammable", .takesArgument = false, .tuning = {}},
		{.name = "colony_stock", .takesArgument = false, .tuning = {}},
	}};

	/// The providers the sim implements that set a need's level each tick (`<setBy provider>`).
	inline constexpr std::array<std::string_view, 1> kNeedProviders = {"thermal_comfort"};

	/// One use of a fact: its index in kFacts, and its argument when it takes one.
	struct FactUse {
		uint16_t	fact = 0;
		std::string argument;
	};

	// --- Queries -----------------------------------------------------------------------------------------

	/// An item argument of a query row or a `have` step: one asset definition, any item with a capability, or
	/// the item the work option is for (`goal`, in a work entry's queries).
	struct ItemArgument {
		enum class Kind : uint8_t { Definition, Capability, Goal };
		Kind		   kind = Kind::Definition;
		uint32_t	   defNameId = 0;
		CapabilityType capability = CapabilityType::Edible;
		std::string	   written;
	};

	enum class CandidateSource : uint8_t {
		Capability,
		Kind,
		Item,
		Yields,
		Pile,
		Box,
		PlaceHere,
		PlaceAround,
		PlaceGrid,
		GoalDestination,
		GoalSpots
	};

	struct FromRow {
		CandidateSource source = CandidateSource::Capability;
		CapabilityType	capability = CapabilityType::Edible;
		ItemArgument	item;
		/// A `<Kind>` of the condition vocabulary, by name.
		std::string kind;
		float		radius = 0.0F;
		float		minRadius = 0.0F;
		float		spacing = 0.0F;
		int			count = 0;
		bool		fallback = false;
		/// The row's position among the query's rows, for messages.
		uint16_t row = 0;
	};

	enum class FactRowKind : uint8_t { Weigh, Exclude, Prefer };

	struct FactRow {
		FactRowKind			 kind = FactRowKind::Weigh;
		FactUse				 fact;
		float				 by = 0.0F;
		std::optional<float> above;
		std::optional<float> below;
		/// The row's one condition, as written; empty when it has none.
		std::string when;
	};

	struct ScaleRow {
		std::string trait;
		FactUse		fact;
		float		by = 0.0F;
		/// The row's position among the query's rows, for messages.
		uint16_t row = 0;
	};

	enum class QueryPick : uint8_t { Best, Weighted };

	struct QueryEntry {
		std::string			  id;
		std::string			  file;
		QueryPick			  pick = QueryPick::Best;
		std::vector<FromRow>  from;
		std::vector<FactRow>  rows;
		std::vector<ScaleRow> scales;
	};

	// --- Actions -----------------------------------------------------------------------------------------

	enum class ConsumedProperty : uint8_t { Nutrition };

	struct RestoreEffect {
		NeedId							need;
		bool							onTarget = false;
		std::optional<float>			amount;
		std::optional<ConsumedProperty> from;
		float							scale = 1.0F;
		std::optional<FactUse>			scaleBy;
		std::optional<float>			seconds;
		std::optional<float>			upTo;
	};

	struct ConsumeEffect {
		CapabilityType		  capability = CapabilityType::Edible;
		int					  count = 1;
		std::optional<float>  seconds;
		std::optional<float>  upTo;
		std::optional<NeedId> need;
	};

	struct TakeEffect {
		bool clears = false;
	};

	struct PutEffect {};

	enum class WorkDirection : uint8_t { Up, Down };

	struct WorkEffect {
		/// The skill whose work rate advances the work.
		std::string	  rate;
		WorkDirection direction = WorkDirection::Up;
	};

	struct CraftEffect {};

	struct SpawnEffect {
		uint32_t	defNameId = 0;
		std::string def;
		bool		intoHands = false;
	};

	struct InstallEffect {};

	struct DamageEffect {
		float amount = 0.0F;
	};

	using EffectRow = std::
		variant<RestoreEffect, ConsumeEffect, TakeEffect, PutEffect, WorkEffect, CraftEffect, SpawnEffect, InstallEffect, DamageEffect>;

	/// What an action works on, for matching an action group's member to its step's target.
	struct ActionTarget {
		enum class Kind : uint8_t { Capability, LooseItem, Packaged };
		Kind		   kind = Kind::Capability;
		CapabilityType capability = CapabilityType::Edible;
	};

	enum class ActionDuration : uint8_t { Seconds, FromDurability, OpenEnded };
	enum class Hands : uint8_t { Any, Free };
	enum class Pays : uint8_t { End, Along };

	/// An action, or an action group (`members` set, every other field unused). Actions and groups share one id
	/// space.
	struct ActionEntry {
		std::string	   id;
		std::string	   file;
		ActionDuration duration = ActionDuration::Seconds;
		float		   seconds = 0.0F;
		/// The skill whose work rate a durability-based duration uses.
		std::string								 skill;
		Hands									 hands = Hands::Any;
		bool									 interruptable = false;
		Pays									 pays = Pays::End;
		float									 curve = 1.0F;
		bool									 asleep = false;
		std::optional<ActionTarget>				 on;
		std::vector<EffectRow>					 effects;
		bool									 group = false;
		std::vector<EntryReference<ActionEntry>> members;
	};

	// --- Chains ------------------------------------------------------------------------------------------

	struct ChainEntry;

	enum class StepTarget : uint8_t { Target, Source, Destination };
	enum class StepAmount : uint8_t { ForGoal, Meal, Carried };
	enum class ThoughtRecipient : uint8_t { Self, Target, Witnesses };

	/// The claim kinds a `do` step names, as bits.
	enum class ClaimKind : uint8_t { Use, SourceQuantity, Slot, Inbound, InFlight };
	inline constexpr std::array<std::string_view, 5> kClaimKindNames = {"use", "source_quantity", "slot", "inbound", "in_flight"};

	/// The events a step ends on (`<ends on>`), as bits.
	enum class EndEvent : uint8_t { HeldGone, TargetGone, TargetLeft };
	inline constexpr std::array<std::string_view, 3> kEndEventNames = {"held_gone", "target_gone", "target_left"};

	struct GoStep {
		std::optional<EntryReference<QueryEntry>> query;
		std::optional<StepTarget>				  to;
	};

	struct DoStep {
		EntryReference<ActionEntry> action;
		std::optional<StepTarget>	on;
		uint8_t						claims = 0;
		std::optional<StepAmount>	amount;
	};

	struct HaveStep {
		std::optional<ItemArgument>	  item;
		std::optional<CapabilityType> capability;
		EntryReference<ChainEntry>	  fetch;
	};

	struct ThinkStep {
		/// A thought type, by id.
		std::string		 thought;
		ThoughtRecipient to = ThoughtRecipient::Self;
	};

	struct NeedStep {
		NeedId need;
		float  change = 0.0F;
	};

	struct DropStep {};

	struct RunStep {
		EntryReference<ChainEntry> chain;
	};

	struct LeaveColonyStep {};

	struct ChainStep {
		std::variant<GoStep, DoStep, HaveStep, ThinkStep, NeedStep, DropStep, RunStep, LeaveColonyStep> step;
		uint8_t																							ends = 0;
	};

	struct ChainEntry {
		std::string			   id;
		std::string			   file;
		std::vector<ChainStep> steps;
	};

	// --- Needs, behaviors ----------------------------------------------------------------------------------

	struct WorkEntry;

	struct RateRow {
		std::string when;
		float		multiply = 1.0F;
	};

	/// How a need's level moves: it drains at a rate, or a provider the sim implements sets it.
	struct NeedMove {
		enum class Kind : uint8_t { Drains, SetBy };
		Kind				 kind = Kind::Drains;
		float				 perGameMinute = 0.0F;
		std::vector<RateRow> rates;
		std::string			 provider;
	};

	struct SatisfiedBy {
		std::optional<EntryReference<ChainEntry>> chain;
		std::optional<EntryReference<WorkEntry>>  work;
		std::string								  when;
	};

	struct BehaviorEntry;

	struct NeedEntry {
		NeedId					   id;
		std::string				   file;
		std::string				   label;
		float					   seek = 0.0F;
		float					   urgent = 0.0F;
		float					   critical = 0.0F;
		NeedMove				   move;
		std::vector<SatisfiedBy>   satisfiedBy;
		EntryHandle<BehaviorEntry> behavior;
	};

	struct IdleRow {
		EntryReference<ChainEntry> chain;
		std::string				   when;
	};

	struct WeightRow {
		std::string when;
		/// A number term the multiplier is raised to.
		std::string per;
		float		multiply = 1.0F;
	};

	enum class Relief : uint8_t { None, Small, Medium, Large };

	struct LowMoodEntry {
		float									  rampStart = 0.0F;
		float									  rampFull = 0.0F;
		float									  base = 0.0F;
		std::optional<EntryReference<QueryEntry>> target;
		std::vector<WeightRow>					  weights;
		/// The `<requires when>` rows, each a condition as written.
		std::vector<std::string>   requirements;
		EntryReference<ChainEntry> effect;
		/// `<ends after="task">`: the run ends with its chain.
		bool   endsAfterTask = false;
		Relief relief = Relief::None;
	};

	struct BehaviorEntry {
		std::string							id;
		std::string							file;
		std::string							label;
		std::vector<EntryHandle<NeedEntry>> needs;
		std::vector<IdleRow>				idle;
		std::optional<LowMoodEntry>			lowMood;
	};

	// --- Colony work and orders ------------------------------------------------------------------------

	struct CategoryEntry {
		std::string id;
		std::string file;
		std::string label;
		int			order = 0;
		bool		canDisable = true;
	};

	enum class WorkGoal : uint8_t { Haul, Harvest, Clear, Craft, Build, Deconstruct, PlacePackaged, Care, Root };

	struct WorkEntry {
		std::string					  id;
		std::string					  file;
		WorkGoal					  goal = WorkGoal::Haul;
		std::optional<NeedId>		  need;
		EntryReference<CategoryEntry> category;
		/// The entry's base rung, as its position in the arbitration ladder.
		uint8_t									  rung = 0;
		bool									  gathers = false;
		std::optional<EntryReference<QueryEntry>> source;
		bool									  sourceCarried = false;
		EntryReference<ChainEntry>				  chain;
	};

	struct OrderTarget {
		enum class Kind : uint8_t { Capability, Ground };
		Kind		   kind = Kind::Ground;
		CapabilityType capability = CapabilityType::Edible;
	};

	struct OrderEntry {
		std::string				   id;
		std::string				   file;
		std::string				   label;
		OrderTarget				   on;
		EntryReference<ChainEntry> chain;
	};

	// --- Creatures -----------------------------------------------------------------------------------------

	/// One need of a creature, with every value settled from its behavior and the creature's own rows.
	struct CreatureNeed {
		NeedId					 id;
		std::string				 label;
		float					 seek = 0.0F;
		float					 urgent = 0.0F;
		float					 critical = 0.0F;
		NeedMove				 move;
		std::vector<SatisfiedBy> satisfiedBy;
		/// The most mood the need takes off at its worst; set exactly when the creature has a mood.
		std::optional<float> moodCost;
		/// The behavior that brings it; none for the creature's own need.
		EntryHandle<BehaviorEntry> behavior;
	};

	struct CreatureEntry {
		/// The creature's definition name, its folder's name.
		std::string name;
		bool		mood = false;
		/// The behaviors it includes, in the creature's order.
		std::vector<EntryHandle<BehaviorEntry>> behaviors;
		std::vector<CreatureNeed>				needs;
	};

	// --- The library ---------------------------------------------------------------------------------------

	namespace entry_loading {
		class Loader;
	}

	/// Everything loadEntries read, with every reference resolved. Empty when the load had any error.
	struct EntryLibrary {
		std::vector<std::string>   needIds;
		std::vector<BehaviorEntry> behaviors;
		std::vector<NeedEntry>	   needs;
		std::vector<ChainEntry>	   chains;
		std::vector<QueryEntry>	   queries;
		std::vector<ActionEntry>   actions;
		std::vector<CategoryEntry> categories;
		std::vector<WorkEntry>	   work;
		std::vector<OrderEntry>	   orders;
		std::vector<CreatureEntry> creatures;
		/// Each fact's tuning value by its index in kFacts; NaN for a fact that reads none.
		std::array<float, kFacts.size()> factTuning{};

		[[nodiscard]] EntryHandle<BehaviorEntry> findBehavior(std::string_view id) const { return find<BehaviorEntry>(behaviorIds, id); }
		[[nodiscard]] EntryHandle<ChainEntry>	 findChain(std::string_view id) const { return find<ChainEntry>(chainIds, id); }
		[[nodiscard]] EntryHandle<QueryEntry>	 findQuery(std::string_view id) const { return find<QueryEntry>(queryIds, id); }
		[[nodiscard]] EntryHandle<ActionEntry>	 findAction(std::string_view id) const { return find<ActionEntry>(actionIds, id); }
		[[nodiscard]] EntryHandle<CategoryEntry> findCategory(std::string_view id) const { return find<CategoryEntry>(categoryIds, id); }
		[[nodiscard]] EntryHandle<WorkEntry>	 findWork(std::string_view id) const { return find<WorkEntry>(workIds, id); }
		[[nodiscard]] EntryHandle<OrderEntry>	 findOrder(std::string_view id) const { return find<OrderEntry>(orderIds, id); }
		[[nodiscard]] const CreatureEntry*		 findCreature(std::string_view name) const;
		/// The interned need id, or none when no behavior or creature has a need of that name.
		[[nodiscard]] std::optional<NeedId> findNeed(std::string_view id) const;

		using IdIndex = std::map<std::string, uint32_t, std::less<>>;

	  private:
		/// The loader fills the indexes; everything else reads them through the find functions.
		friend class entry_loading::Loader;

		IdIndex behaviorIds;
		IdIndex chainIds;
		IdIndex queryIds;
		IdIndex actionIds;
		IdIndex categoryIds;
		IdIndex workIds;
		IdIndex orderIds;
		IdIndex needIndex;

		template <typename Entry>
		static EntryHandle<Entry> find(const IdIndex& index, std::string_view id) {
			const auto found = index.find(id);
			return found == index.end() ? EntryHandle<Entry>{} : EntryHandle<Entry>{found->second};
		}
	};

} // namespace assets
