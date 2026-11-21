#pragma once

// Query evaluation: for one creature and one query entry, the target the query picks from what the creature
// remembers (and the boxes the colony knows), or why there is none. The chain runner asks it when a step that
// names a query starts, and the decision when it offers a chain, keeping the ranked alternates for a need's
// nearest-of route request and the claim the target takes. See /docs/technical/queries-and-chains.md
// (Queries).

#include "Facts.h"

#include <ecs/ClaimIndex.h>
#include <ecs/EntityID.h>

#include <assets/ConditionConfig.h>
#include <assets/Entries.h>

#include <glm/vec2.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace assets {
	class AssetRegistry;
}

namespace world {
	class ChunkManager;
}

namespace ecs {
	class LoosePileIndex;
	class NavigationSystem;
	class World;
	struct Memory;
	struct SourceReachMemo;
} // namespace ecs

namespace queries {

	/// A storage box the colony knows and one item it holds: each pair is a candidate of a `<from box>` row.
	struct KnownBox {
		ecs::EntityID box = ecs::kInvalidEntity;
		uint64_t	  key = 0; // the box's world key, as Memory files it
		glm::vec2	  position{0.0F, 0.0F};
		uint32_t	  item = 0;
		uint32_t	  quantity = 0;
		/// The box's storage priority for the item (ecs::StoragePriority), which `below_destination` reads.
		uint8_t priority = 0;
		/// A packaged box is no source.
		bool packaged = false;
	};

	/// The goal a work option serves, for `<from goal>` rows, the `goal` item argument, and
	/// `below_destination`.
	struct QueryGoal {
		uint64_t				   goalId = 0;
		ecs::EntityID			   destination = ecs::kInvalidEntity;
		glm::vec2				   destinationPosition{0.0F, 0.0F};
		uint32_t				   item = 0;
		uint8_t					   destinationPriority = 0;
		std::span<const glm::vec2> builderSpots;
	};

	/// How the claims of the step a query runs for are read. A holder of a candidate either takes it from this
	/// creature (the candidate is passed over) or only holds it (`held` reads 1). Until a claim holder carries
	/// the need its option serves, an eater is a need task (ecs::TaskType::FulfillNeed) not on the idle rung
	/// and on a rung no lower than this creature's option, as the decision's food search reads it.
	struct ClaimReading {
		/// The claim kinds the step names, as bits of assets::ClaimKind; none passes nothing over for claims.
		uint8_t kinds = 0;
		/// An idle chain: every holder takes.
		bool everyHolderTakes = false;
		/// This creature's option's rung and the idle rung, as positions in the loaded ladder
		/// (assets::DecisionRungs). Both are the caller's to give whenever claims are read by the eater rule.
		uint8_t tier = ecs::kUnrankedTier;
		uint8_t idleTier = ecs::kUnrankedTier;
		/// Units the step wants of any item, when it is not a meal.
		uint32_t wanted = 1;
		/// For a meal: the need points short of the need's seek level. The units wanted of an item are then
		/// what takes the need back to seek at the item's nutrition (x 100), at least one, as the meal amount
		/// is sized.
		std::optional<float> mealShortfall;
		ecs::ClaimExclusion	 self{};
	};

	/// Whether a remembered definition can serve the request at all (a tool to cut it, room to carry its yield,
	/// a box's room for it): asked once per definition per evaluation, before any of its things is visited, and
	/// never cached, since the answer follows the creature's inventory. It refers to a callable the caller keeps
	/// alive for the evaluation and owns nothing, so making one never allocates. An empty gate lets everything.
	class DefinitionGate {
	  public:
		DefinitionGate() = default;

		template <typename Callable>
		[[nodiscard]] static DefinitionGate of(const Callable& callable) {
			DefinitionGate gate;
			gate.context = &callable;
			gate.ask = [](const void* context, uint32_t defNameId) {
				return (*static_cast<const Callable*>(context))(defNameId);
			};
			return gate;
		}

		[[nodiscard]] bool admits(uint32_t defNameId) const { return ask == nullptr || ask(context, defNameId); }

	  private:
		const void* context = nullptr;
		bool (*ask)(const void*, uint32_t) = nullptr;
	};

	/// What one evaluation is for: the creature, where it stands, what it remembers, and the step's claims.
	struct QueryRequest {
		assets::EntryHandle<assets::QueryEntry> query;
		ecs::EntityID							creature = ecs::kInvalidEntity;
		glm::vec2								position{0.0F, 0.0F};
		float									agentRadius = 0.3F;
		const ecs::Memory*						memory = nullptr;
		/// Its route verdicts; none reads every target as having none.
		const ecs::SourceReachMemo* reach = nullptr;
		/// Its trait ids, for `<scale trait>` rows.
		std::span<const std::string> traits;
		ClaimReading				 claims{};
		/// How many ranked survivors to keep after the pick, in the pick's prefer class: a need's nearest-of
		/// round asks a route for them all at once.
		std::size_t		 alternates = 0;
		const QueryGoal* goal = nullptr;
		/// The memory key (or box key) of the target the creature is heading for: asked first, among the rows that
		/// are not fallback rows, and taken alone when it still survives in the upper prefer class.
		std::optional<uint64_t> keepKey;
		DefinitionGate			gate{};
	};

	/// When a remembered harvestable of a definition at a position is ready to give again, at a simulation
	/// time; none when it is ready.
	using RegrowthQuery = std::function<std::optional<double>(uint32_t defNameId, glm::vec2 position, double now)>;

	/// The world a query reads beyond the creature's memory, shared by every creature deciding in one update.
	/// Anything left null is read as absent: no nav system reaches everything, no chunks know no water, no
	/// claim index holds nothing.
	struct QueryWorld {
		const ecs::NavigationSystem* navigation = nullptr;
		const world::ChunkManager*	 chunks = nullptr;
		const ecs::ClaimIndex*		 claims = nullptr;
		/// With `piles`, a remembered pile's live count.
		const ecs::World*		   world = nullptr;
		const ecs::LoosePileIndex* piles = nullptr;
		std::span<const KnownBox>  boxes;
		/// Moves whenever `boxes` or what they hold changes; a recorded wait and colony_stock read it.
		uint64_t			 boxesStamp = 0;
		const RegrowthQuery* regrowth = nullptr;
		/// TimeSystem::simulationSeconds, the clock regrowth and sightings are stamped on.
		double now = 0.0;
		/// assets::MemoryConfig::recentSightingGameMinutes, for `<from kind>` rows naming a mover.
		double recentSightingGameMinutes = 0.0;
		/// The sim's seeded generator, for `place="around"` and `pick="weighted"`.
		std::mt19937* generator = nullptr;
	};

	enum class CandidateKind : uint8_t { Thing, Box, Mover, Place, Destination, Spot };

	/// Why a candidate that survived the rows was passed over.
	enum class PassOverReason : uint8_t { None, Verdict, Packaged, Exhausted, Unreachable, Regrowing, Claimed };

	struct QueryCandidate {
		CandidateKind kind = CandidateKind::Thing;
		/// The first `<from>` row that names it, by its position among the query's rows.
		uint16_t sourceRow = 0;
		bool	 fallback = false;
		/// 0 when every prefer row keeps it, 1 when one would exclude it.
		uint8_t preferClass = 0;
		/// A thing's memory key, a box's world key, a mover's entity id, a grid point's scan index, an `around`
		/// point's draw index, a spot's index.
		uint64_t	  key = 0;
		uint32_t	  defNameId = 0;
		/// What it is a source of: a pile's or a box's item, a harvestable's yield.
		uint32_t	  item = 0;
		ecs::EntityID entity = ecs::kInvalidEntity;
		glm::vec2	  position{0.0F, 0.0F};
		/// Where a route to it ends: itself, or the stand point beside the obstacle it lies in.
		glm::vec2 goal{0.0F, 0.0F};
		float	  distance = 0.0F;
		float	  score = 0.0F;
		float	  held = 0.0F;
		/// Units the step wants of its item (a meal at its nutrition, or the request's units).
		uint32_t wanted = 1;
		/// What a pile or a box has left for this creature once the holders that take it are counted.
		uint32_t left = 0;
		/// The claim the target takes when its step names it: use for a thing taken whole, source quantity for
		/// a pile or a box (the smaller of what is wanted and what is left), a slot for a builder spot.
		ecs::ClaimTarget  claimTarget{};
		assets::ClaimKind claimKind = assets::ClaimKind::Use;
		uint32_t		  claimQuantity = 1;
	};

	enum class QueryOutcome : uint8_t {
		Found,
		/// No candidate at all.
		NothingKnown,
		/// Every candidate was removed by an exclusion or a definition gate.
		AllExcluded,
		/// Every candidate that survived the exclusions was passed over (a route verdict, regrowing, not
		/// joined, a packaged box, nothing left, taken by a claim): the spec's wait.
		AllPassedOver,
		/// A row with a `when` is in the query, and conditions on query rows are not evaluated; nothing was
		/// walked.
		ConditionNotEvaluated,
		/// An `exclude` or `prefer` row reads a fact nothing answers, so its line would decide on a value the
		/// sim does not have; nothing was walked.
		FactNotAnswered
	};

	struct QueryResult {
		QueryOutcome outcome = QueryOutcome::NothingKnown;
		/// The pick first, then up to the request's alternates of its prefer class in rank order. Valid until
		/// the evaluator's next call.
		std::span<const QueryCandidate> ranked;
		/// The best candidate passed over because a claim takes it, when it ranks ahead of the pick (or there
		/// is none): the option's Reserved row, and who holds it.
		std::optional<QueryCandidate> reserved;
		ecs::EntityID				  reservedBy = ecs::kInvalidEntity;
		/// With AllPassedOver, for the trace: of the candidates passed over, the best ranked with the most
		/// telling reason (taken by a claim, then regrowing, then nothing left, a packaged box, a verdict, not
		/// joined), and that reason.
		std::optional<QueryCandidate> waitingOn;
		PassOverReason				  waitingOnReason = PassOverReason::None;
		/// With ConditionNotEvaluated or FactNotAnswered: the row, by its position among the query's rows.
		uint16_t row = 0;
		/// Candidates whose pass-overs were asked (verdicts, regrowth, reach, claims).
		uint32_t visits = 0;
		/// Memory cells the walk opened or looked for while walking outward in rings.
		uint32_t cellsTouched = 0;
		/// The answer is the creature's recorded wait: nothing it read has changed, so nothing was walked.
		bool fromWait = false;

		[[nodiscard]] const QueryCandidate* target() const { return ranked.empty() ? nullptr : &ranked.front(); }
	};

	/// What `count_of` reads: Found with the survivors, or why the count cannot be made.
	struct QueryCount {
		QueryOutcome outcome = QueryOutcome::Found;
		uint32_t	 survivors = 0;
		/// With ConditionNotEvaluated or FactNotAnswered: the row, by its position among the query's rows.
		uint16_t row = 0;
	};

	/// Runs queries. One evaluator serves every creature: it owns the scratch every evaluation reuses, so none
	/// allocates once warm, and per creature and query the definition cache and the recorded wait.
	class QueryEvaluator {
	  public:
		/// `kinds` are the condition vocabulary's `<Kind>` declarations, which `<from kind>` rows name.
		QueryEvaluator(
			const assets::EntryLibrary&				  library,
			const assets::AssetRegistry&			  registry,
			std::span<const assets::ConditionKindDef> kinds
		);

		[[nodiscard]] QueryResult evaluate(const QueryRequest& request, const QueryWorld& world);

		/// How many candidates survive the query's exclusions, scoring and passing over none: `count_of`. A
		/// fallback row's candidates count only when no other row's survives; `place="around"` rows draw
		/// nothing here (the loader refuses them in a counted query).
		[[nodiscard]] QueryCount count(const QueryRequest& request, const QueryWorld& world);

		/// Drop what is kept for `creature` (its definition caches and waits) once it is gone; the owner of the
		/// evaluator calls it when a creature is destroyed.
		void forget(ecs::EntityID creature);

	  private:
		struct SourcePlan {
			assets::CandidateSource source = assets::CandidateSource::Capability;
			uint16_t				row = 0;
			bool					fallback = false;
			/// The capability a remembered thing of the row must carry; none for `<from item>`.
			std::optional<assets::CapabilityType> required;
			/// `<from kind>` naming a mover: its definition.
			uint32_t mover = 0;
			/// The definitions a row's remembered things are of, sorted; empty when they come from the
			/// capability (Capability, Kind) or from the goal.
			std::vector<uint32_t> definitions;
			/// A `<from box>` row's items, sorted; empty when the item is the goal's.
			std::vector<uint32_t> items;
			bool				  itemIsGoal = false;
			float				  radius = 0.0F;
			float				  minRadius = 0.0F;
			float				  spacing = 0.0F;
			int					  count = 0;
			/// A place row every prefer row ranks below what they keep, whatever the place: walked only while
			/// nothing of the upper class survives.
			bool placesBelow = false;
		};

		struct FactRowPlan {
			assets::FactRowKind	 kind = assets::FactRowKind::Weigh;
			uint16_t			 fact = 0;
			float				 by = 0.0F;
			std::optional<float> above;
			std::optional<float> below;
		};

		struct Plan {
			std::vector<SourcePlan>	 sources;
			std::vector<FactRowPlan> rows;
			std::optional<uint16_t>	 conditionRow;
			std::optional<uint16_t>	 unansweredRow;
			/// The smallest `<exclude fact="distance" above>`.
			float distanceAbove = std::numeric_limits<float>::infinity();
			/// An `<exclude fact="distance">` row, above or below, so where the creature stands decides it.
			bool excludesDistance = false;
			bool usesColonyStock = false;
			bool hasPlaceRows = false;
			/// A `<from kind>` row naming a mover, whose sightings move no memory version.
			bool hasMoverRows = false;
		};

		/// A remembered definition's terms for one creature and query: its definition-scoped facts weighed,
		/// and whether an exclusion removes all of it or a prefer row ranks all of it below.
		struct DefinitionTerms {
			float score = 0.0F;
			bool  excluded = false;
			bool  preferredBelow = false;
		};

		/// What a recorded wait read, so it is walked again the moment any of it changes.
		struct Wait {
			uint64_t memoryVersion = 0;
			double	 readyAt = 0.0;
			uint64_t verdictGeneration = 0;
			uint64_t boxesStamp = 0;
			/// The goal, the claim reading's kinds, tiers, and exclusion, the traits, and the keep key it was asked
			/// with.
			uint64_t requestStamp = 0;
			/// Stamped only when a candidate was passed over as taken by a claim: the fingerprint is colony-wide.
			std::optional<uint64_t> claimsFingerprint;
			/// Stamped only when a candidate was passed over as not joined.
			std::optional<uint64_t> connectivityEpoch;
			/// Where the creature stood, when the query has place rows or a distance bound; otherwise the nav
			/// component it stood on.
			std::optional<glm::vec2> position;
			int64_t					 component = 0;
		};

		/// What is kept for one creature and query between evaluations.
		struct Record {
			ecs::EntityID								  creature = ecs::kInvalidEntity;
			uint32_t									  query = 0;
			uint64_t									  definitionStamp = 0;
			std::unordered_map<uint32_t, DefinitionTerms> definitions;
			std::optional<Wait>							  wait;
			std::optional<QueryCandidate>				  waitingOn;
			PassOverReason								  waitingOnReason = PassOverReason::None;
			std::optional<QueryCandidate>				  reserved;
			ecs::EntityID								  reservedBy = ecs::kInvalidEntity;
		};

		/// One remembered definition a walk opens, and the rows that name it.
		struct Seed {
			uint32_t			   definition = 0;
			uint32_t			   rows = 0; // bits by index in Plan::sources
			float				   bound = 0.0F;
			const DefinitionTerms* terms = nullptr;
			/// Cells looked for while walking outward in rings, which stops once it would cost more than
			/// opening every occupied cell.
			uint32_t ringLookups = 0;
		};

		/// An occupied cell of a seed's definition (`ring` negative), or the next ring of cells around the
		/// creature to look in, keyed by the nearest any of it can be.
		struct CellStep {
			float	 distanceSquared = 0.0F;
			uint32_t seed = 0;
			uint32_t cell = 0;
			int32_t	 ring = -1;
		};

		struct ThingStep {
			float	 distanceSquared = 0.0F;
			uint64_t key = 0;
			uint32_t seed = 0;
		};

		struct Proximity {
			uint64_t key = 0;
			float	 value = 0.0F;
		};

		class Evaluation;

		[[nodiscard]] Plan	  planFor(const assets::QueryEntry& entry) const;
		[[nodiscard]] Record& recordFor(ecs::EntityID creature, uint32_t query);

		const assets::EntryLibrary&	 library;
		const assets::AssetRegistry& registry;
		/// Each `<Kind>`: its capability, or its mover's definition id.
		struct ResolvedKind {
			std::string							  name;
			std::optional<assets::CapabilityType> capability;
			uint32_t							  mover = 0;
		};
		std::vector<ResolvedKind> kinds;
		std::vector<Plan>		  plans;
		std::vector<Record>		  records; // sorted by (creature, query)

		// Scratch, reused by every evaluation.
		std::vector<Seed>			seeds;
		std::vector<CellStep>		cellSteps;
		std::vector<ThingStep>		thingSteps;
		std::vector<QueryCandidate> upperClass;
		std::vector<QueryCandidate> lowerClass;
		std::vector<QueryCandidate> fallbackClass;
		std::vector<QueryCandidate> ranked;
		std::vector<Proximity>		proximities;
		std::vector<float>			rowWeights;
	};

} // namespace queries
