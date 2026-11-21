#include "QueryEvaluator.h"

#include <ecs/GoalTaskRegistry.h>
#include <ecs/LoosePileIndex.h>
#include <ecs/PlacedItemIndex.h>
#include <ecs/World.h>
#include <ecs/components/Memory.h>
#include <ecs/components/ResourceStack.h>
#include <ecs/components/SourceReachMemo.h>
#include <ecs/systems/NavigationSystem.h>

#include <assets/AssetRegistry.h>

#include <world/chunk/ChunkCoordinate.h>
#include <world/chunk/ChunkManager.h>
#include <world/chunk/TerrainPolygonQuery.h>

#include <core/Vec2i64.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <bit>
#include <cassert>
#include <cmath>
#include <numbers>
#include <string_view>

namespace queries {

	namespace {

		constexpr float kInfinity = std::numeric_limits<float>::infinity();

		/// A candidate this far past its horizon may still tie the score it would have to beat, so it is visited;
		/// covers the rounding of the horizon's division.
		constexpr float kHorizonSlackMeters = 1.0e-4F;

		/// The most candidates a weighted pick draws from.
		constexpr std::size_t kWeightedPool = 16;

		/// A definition with at most this many occupied cells has them all opened at once; one with more is
		/// walked outward from the creature in rings of cells.
		constexpr std::size_t kCellsOpenedAtOnce = 32;

		/// A draw in [0, 1) from the top 24 bits of one 32-bit output, the same on every platform (the standard
		/// distributions are not).
		[[nodiscard]] float unitDraw(std::mt19937& generator) {
			return static_cast<float>(generator() >> 8) * (1.0F / 16777216.0F);
		}

		[[nodiscard]] uint16_t capabilityBit(assets::CapabilityType capability) {
			return static_cast<uint16_t>(1U << static_cast<unsigned>(capability));
		}

		[[nodiscard]] bool namesClaim(uint8_t kinds, assets::ClaimKind kind) {
			return (kinds & (1U << static_cast<unsigned>(kind))) != 0;
		}

		/// 64-bit mixing for the stamps (splitmix64's finalizer).
		[[nodiscard]] uint64_t mixed(uint64_t value) {
			value += 0x9E3779B97F4A7C15ULL;
			value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ULL;
			value = (value ^ (value >> 27)) * 0x94D049BB133111EBULL;
			return value ^ (value >> 31);
		}

		[[nodiscard]] uint64_t combined(uint64_t stamp, uint64_t value) {
			return mixed(stamp ^ mixed(value));
		}

		/// The facts that describe a place; a thing reads 0 for each.
		[[nodiscard]] bool isPlaceFact(uint16_t factIndex) {
			return factIndex == fact::kOnShore || factIndex == fact::kWasteNearby || factIndex == fact::kFoodNearby ||
				   factIndex == fact::kIsPlace;
		}

		/// The total rank order: no fallback first, then the prefer class, the score, the distance, and the keys.
		[[nodiscard]] bool ranksAhead(const QueryCandidate& first, const QueryCandidate& second) {
			if (first.fallback != second.fallback) {
				return !first.fallback;
			}
			if (first.preferClass != second.preferClass) {
				return first.preferClass < second.preferClass;
			}
			if (first.score != second.score) {
				return first.score > second.score;
			}
			if (first.distance != second.distance) {
				return first.distance < second.distance;
			}
			if (first.key != second.key) {
				return first.key < second.key;
			}
			if (first.item != second.item) {
				return first.item < second.item;
			}
			if (first.sourceRow != second.sourceRow) {
				return first.sourceRow < second.sourceRow;
			}
			return static_cast<uint8_t>(first.kind) < static_cast<uint8_t>(second.kind);
		}

		/// How telling a pass-over reason is for the trace: higher shows first.
		[[nodiscard]] int tellingness(PassOverReason reason) {
			switch (reason) {
				case PassOverReason::Claimed:
					return 6;
				case PassOverReason::Regrowing:
					return 5;
				case PassOverReason::Exhausted:
					return 4;
				case PassOverReason::Packaged:
					return 3;
				case PassOverReason::Verdict:
					return 2;
				case PassOverReason::Unreachable:
					return 1;
				case PassOverReason::None:
					break;
			}
			return 0;
		}

		/// The smallest squared distance from `point` to the square of a memory cell.
		[[nodiscard]] float distanceSquaredToCell(glm::vec2 point, int32_t cellX, int32_t cellY) {
			const float minX = static_cast<float>(cellX) * ecs::kMemoryCellMeters;
			const float minY = static_cast<float>(cellY) * ecs::kMemoryCellMeters;
			const float gapX = std::max({minX - point.x, 0.0F, point.x - (minX + ecs::kMemoryCellMeters)});
			const float gapY = std::max({minY - point.y, 0.0F, point.y - (minY + ecs::kMemoryCellMeters)});
			return gapX * gapX + gapY * gapY;
		}

		/// The smallest squared distance from `point`, in cell (`centerX`, `centerY`), to any cell of the ring
		/// `ring` cells out from it.
		[[nodiscard]] float distanceSquaredToRing(glm::vec2 point, int32_t centerX, int32_t centerY, int32_t ring) {
			if (ring == 0) {
				return 0.0F;
			}
			const float inside = static_cast<float>(ring - 1);
			const float lowX = (static_cast<float>(centerX) - inside) * ecs::kMemoryCellMeters;
			const float highX = (static_cast<float>(centerX) + inside + 1.0F) * ecs::kMemoryCellMeters;
			const float lowY = (static_cast<float>(centerY) - inside) * ecs::kMemoryCellMeters;
			const float highY = (static_cast<float>(centerY) + inside + 1.0F) * ecs::kMemoryCellMeters;
			const float gap = std::max(0.0F, std::min({point.x - lowX, highX - point.x, point.y - lowY, highY - point.y}));
			return gap * gap;
		}

		/// Whether a row's line takes a candidate with `value`: above 0 by default, or above `above`, or below
		/// `below`.
		template <typename Row>
		[[nodiscard]] bool beyondLine(float value, const Row& row) {
			if (row.below.has_value()) {
				return value < *row.below;
			}
			return value > row.above.value_or(0.0F);
		}

		/// The value a fact has at every place, when it is the same at all of them; none when it depends on
		/// where the place is.
		[[nodiscard]] std::optional<float> placeConstant(uint16_t factIndex, const assets::EntryLibrary& library) {
			if (factIndex == fact::kDistance || factIndex == fact::kOnShore || factIndex == fact::kWasteNearby ||
				factIndex == fact::kFoodNearby) {
				return std::nullopt;
			}
			if (factIndex == fact::kIsPlace) {
				return 1.0F;
			}
			if (factIndex == fact::kSleepQuality) {
				return library.factTuning[factIndex];
			}
			return 0.0F;
		}

	} // namespace

	// --- Plans ---------------------------------------------------------------------------------------------------

	QueryEvaluator::QueryEvaluator(
		const assets::EntryLibrary&				  newLibrary,
		const assets::AssetRegistry&			  newRegistry,
		std::span<const assets::ConditionKindDef> declaredKinds
	)
		: library(newLibrary),
		  registry(newRegistry) {
		for (const assets::ConditionKindDef& kind : declaredKinds) {
			ResolvedKind resolved{.name = kind.name, .capability = kind.capability};
			if (!kind.mover.empty()) {
				resolved.mover = registry.getDefNameId(kind.mover);
			}
			kinds.push_back(std::move(resolved));
		}
		plans.reserve(library.queries.size());
		for (const assets::QueryEntry& entry : library.queries) {
			plans.push_back(planFor(entry));
		}
	}

	QueryEvaluator::Plan QueryEvaluator::planFor(const assets::QueryEntry& entry) const {
		Plan	   plan;
		const auto itemsWith = [&](assets::CapabilityType capability) {
			if (capability == assets::CapabilityType::Edible) {
				return registry.edibleItemIds();
			}
			std::vector<uint32_t> items;
			for (const std::string& name : registry.getDefinitionNames()) {
				const uint32_t definition = registry.getDefNameId(name);
				if (registry.hasCapability(definition, capability)) {
					items.push_back(definition);
				}
			}
			std::ranges::sort(items);
			return items;
		};
		const auto itemsOf = [&](const assets::ItemArgument& argument) {
			return argument.kind == assets::ItemArgument::Kind::Definition ? std::vector<uint32_t>{argument.defNameId}
																		   : itemsWith(argument.capability);
		};
		for (std::size_t index = 0; index < entry.rows.size(); ++index) {
			const assets::FactRow& row = entry.rows[index];
			plan.rows.push_back({.kind = row.kind, .fact = row.fact.fact, .by = row.by, .above = row.above, .below = row.below});
			if (!row.when.empty() && !plan.conditionRow.has_value()) {
				plan.conditionRow = writtenPositionOfFactRow(entry, index);
			}
			if (row.kind != assets::FactRowKind::Weigh && !kFactSpecs[row.fact.fact].answered && !plan.unansweredRow.has_value()) {
				plan.unansweredRow = writtenPositionOfFactRow(entry, index);
			}
			if (row.kind == assets::FactRowKind::Exclude && row.fact.fact == fact::kDistance) {
				plan.excludesDistance = true;
				if (row.above.has_value()) {
					plan.distanceAbove = std::min(plan.distanceAbove, *row.above);
				}
			}
			plan.usesColonyStock = plan.usesColonyStock || row.fact.fact == fact::kColonyStock;
		}

		// A place row whose every prefer row reads a fact that is the same at every place has one class
		// wherever the place is; when that is the lower class, the row is walked only while nothing above
		// survives.
		bool placesBelow = false;
		bool placeClassKnown = true;
		bool anyPrefer = false;
		for (const FactRowPlan& row : plan.rows) {
			if (row.kind != assets::FactRowKind::Prefer) {
				continue;
			}
			anyPrefer = true;
			const std::optional<float> value = placeConstant(row.fact, library);
			if (!value.has_value()) {
				placeClassKnown = false;
			} else if (beyondLine(*value, row)) {
				placesBelow = true;
			}
		}
		placesBelow = anyPrefer && placeClassKnown && placesBelow;

		for (const assets::FromRow& from : entry.from) {
			SourcePlan source{.source = from.source, .row = from.row, .fallback = from.fallback};
			const bool goalItem = from.item.kind == assets::ItemArgument::Kind::Goal;
			switch (from.source) {
				case assets::CandidateSource::Capability:
					source.required = from.capability;
					break;
				case assets::CandidateSource::Kind:
					for (const ResolvedKind& kind : kinds) {
						if (kind.name == from.kind) {
							source.required = kind.capability;
							source.mover = kind.mover;
						}
					}
					plan.hasMoverRows = plan.hasMoverRows || source.mover != 0;
					break;
				case assets::CandidateSource::Item:
					source.itemIsGoal = goalItem;
					if (!goalItem) {
						source.definitions = itemsOf(from.item);
					}
					break;
				case assets::CandidateSource::Yields:
					source.required = assets::CapabilityType::Harvestable;
					source.itemIsGoal = goalItem;
					if (!goalItem) {
						for (const uint32_t item : itemsOf(from.item)) {
							const std::vector<uint32_t>& yielding = registry.harvestablesYielding(item);
							source.definitions.insert(source.definitions.end(), yielding.begin(), yielding.end());
						}
						std::ranges::sort(source.definitions);
						source.definitions.erase(
							std::unique(source.definitions.begin(), source.definitions.end()), source.definitions.end()
						);
					}
					break;
				case assets::CandidateSource::Pile:
					source.required = assets::CapabilityType::Carryable;
					source.itemIsGoal = goalItem;
					if (!goalItem) {
						source.definitions = itemsOf(from.item);
					}
					break;
				case assets::CandidateSource::Box:
					source.itemIsGoal = goalItem;
					if (!goalItem) {
						source.items = itemsOf(from.item);
					}
					break;
				case assets::CandidateSource::PlaceHere:
				case assets::CandidateSource::PlaceAround:
				case assets::CandidateSource::PlaceGrid:
					plan.hasPlaceRows = true;
					source.radius = from.radius;
					source.minRadius = from.minRadius;
					source.spacing = from.spacing;
					source.count = from.count;
					source.placesBelow = placesBelow;
					break;
				case assets::CandidateSource::GoalDestination:
				case assets::CandidateSource::GoalSpots:
					break;
			}
			plan.sources.push_back(std::move(source));
		}
		assert(plan.sources.size() <= 32 && "a seed names its rows in 32 bits");
		return plan;
	}

	QueryEvaluator::Record& QueryEvaluator::recordFor(ecs::EntityID creature, uint32_t query) {
		const auto found =
			std::lower_bound(records.begin(), records.end(), std::pair{creature, query}, [](const Record& record, const auto& key) {
				return std::pair{record.creature, record.query} < key;
			});
		if (found != records.end() && found->creature == creature && found->query == query) {
			return *found;
		}
		return *records.insert(found, Record{.creature = creature, .query = query});
	}

	void QueryEvaluator::forget(ecs::EntityID creature) {
		std::erase_if(records, [creature](const Record& record) { return record.creature == creature; });
	}

	// --- One evaluation ------------------------------------------------------------------------------------------

	class QueryEvaluator::Evaluation {
	  public:
		Evaluation(QueryEvaluator& newEvaluator, const QueryRequest& newRequest, const QueryWorld& newWorld)
			: evaluator(newEvaluator),
			  request(newRequest),
			  world(newWorld),
			  entry(newEvaluator.library.queries[newRequest.query.index]),
			  plan(newEvaluator.plans[newRequest.query.index]),
			  memory(*newRequest.memory) {
			haveMesh = world.navigation != nullptr && world.navigation->hasMesh();
			hisLabel = haveMesh ? world.navigation->componentAt(request.position) : geometry::nav::kNoComponent;
			centerX = ecs::memoryCellCoordinate(request.position.x);
			centerY = ecs::memoryCellCoordinate(request.position.y);
			weighRows();
			capacity = std::max<std::size_t>(request.alternates + 1, entry.pick == assets::QueryPick::Weighted ? kWeightedPool : 1);
			itemPreferRows = std::ranges::any_of(plan.rows, [](const FactRowPlan& row) {
				return row.kind == assets::FactRowKind::Prefer && kFactSpecs[row.fact].scope == FactScope::Item;
			});
			for (std::vector<QueryCandidate>* list : {&evaluator.upperClass, &evaluator.lowerClass, &evaluator.fallbackClass}) {
				list->clear();
				list->reserve(capacity + 1);
			}
		}

		QueryResult run() {
			QueryResult result;
			if (plan.conditionRow.has_value()) {
				result.outcome = QueryOutcome::ConditionNotEvaluated;
				result.row = *plan.conditionRow;
				return result;
			}
			if (plan.unansweredRow.has_value()) {
				result.outcome = QueryOutcome::FactNotAnswered;
				result.row = *plan.unansweredRow;
				return result;
			}
			adoptRecord();
			// A replayed wait gives only the trace row it recorded, no target to route to, so where a stand point
			// beside a target lies (which follows the creature's position) does not matter to it.
			if (record->wait.has_value() && waitHolds(*record->wait)) {
				result.outcome = QueryOutcome::AllPassedOver;
				result.waitingOn = record->waitingOn;
				result.waitingOnReason = record->waitingOnReason;
				result.reserved = record->reserved;
				result.reservedBy = record->reservedBy;
				result.fromWait = true;
				return result;
			}

			bool keptAlone = considerKept();
			// A kept target in the lower class waits on the walk: anything of the upper class wins over it.
			const std::optional<QueryCandidate> keptBelow = !keptAlone && keptConsidered.has_value() && !evaluator.lowerClass.empty()
																? std::optional(evaluator.lowerClass.front())
																: std::nullopt;
			if (!keptAlone) {
				walkPhase(false);
				if (evaluator.upperClass.empty() && evaluator.lowerClass.empty()) {
					walkPhase(true);
				}
			}
			// Nothing of the upper class survived: the kept target, met first, stays the pick among the lower.
			if (keptBelow.has_value() && evaluator.upperClass.empty()) {
				evaluator.lowerClass.assign(1, *keptBelow);
				keptAlone = true;
			}

			const std::vector<QueryCandidate>& chosen = !evaluator.upperClass.empty()	? evaluator.upperClass
														: !evaluator.lowerClass.empty() ? evaluator.lowerClass
																						: evaluator.fallbackClass;
			result.visits = visits;
			result.cellsTouched = cellsTouched;
			evaluator.ranked.clear();
			if (chosen.empty()) {
				result.outcome = !anyCandidate	 ? QueryOutcome::NothingKnown
								 : passedOverAny ? QueryOutcome::AllPassedOver
												 : QueryOutcome::AllExcluded;
			} else {
				result.outcome = QueryOutcome::Found;
				const std::size_t pick = !keptAlone && entry.pick == assets::QueryPick::Weighted ? weightedPick(chosen) : 0;
				evaluator.ranked.push_back(chosen[pick]);
				const std::size_t kept = keptAlone ? 1 : request.alternates + 1;
				for (std::size_t index = 0; index < chosen.size() && evaluator.ranked.size() < kept; ++index) {
					if (index != pick) {
						evaluator.ranked.push_back(chosen[index]);
					}
				}
			}
			result.ranked = evaluator.ranked;
			if (bestClaimed.has_value() && (chosen.empty() || ranksAhead(*bestClaimed, chosen.front()))) {
				result.reserved = bestClaimed;
				result.reservedBy = bestClaimedHolder;
			}
			if (result.outcome == QueryOutcome::AllPassedOver) {
				result.waitingOn = waitingOn;
				result.waitingOnReason = waitingOnReason;
			}

			record->wait.reset();
			// A gate's answer follows the creature's inventory, and a sighting moves no memory version: no stamp
			// covers either.
			if (result.outcome == QueryOutcome::AllPassedOver && waitable && !gated && !plan.hasMoverRows) {
				record->wait = stampNow();
				record->waitingOn = result.waitingOn;
				record->waitingOnReason = result.waitingOnReason;
				record->reserved = result.reserved;
				record->reservedBy = result.reservedBy;
			}
			return result;
		}

		QueryCount count() {
			QueryCount counted;
			if (plan.conditionRow.has_value()) {
				counted.outcome = QueryOutcome::ConditionNotEvaluated;
				counted.row = *plan.conditionRow;
				return counted;
			}
			if (plan.unansweredRow.has_value()) {
				counted.outcome = QueryOutcome::FactNotAnswered;
				counted.row = *plan.unansweredRow;
				return counted;
			}
			adoptRecord();
			counting = true;
			counted.survivors = countPhase(false);
			if (counted.survivors == 0) {
				counted.survivors = countPhase(true);
			}
			return counted;
		}

	  private:
		// --- Weights, stamps, and the definition cache ---

		void weighRows() {
			evaluator.rowWeights.assign(plan.rows.size(), 0.0F);
			uint64_t traits = 0;
			for (const std::string& trait : request.traits) {
				traits = combined(traits, std::hash<std::string_view>{}(trait));
			}
			definitionStamp = plan.usesColonyStock ? combined(traits, world.boxesStamp) : traits;
			for (std::size_t index = 0; index < plan.rows.size(); ++index) {
				const FactRowPlan& row = plan.rows[index];
				if (row.kind != assets::FactRowKind::Weigh) {
					continue;
				}
				float weight = row.by;
				for (const assets::ScaleRow& scale : entry.scales) {
					if (scale.fact.fact == row.fact && std::ranges::find(request.traits, scale.trait) != request.traits.end()) {
						weight *= scale.by;
					}
				}
				evaluator.rowWeights[index] = weight;
				if (row.fact == fact::kDistance) {
					distanceWeight += weight;
				} else if (kFactSpecs[row.fact].scope == FactScope::Item && !isPlaceFact(row.fact) && weight != 0.0F) {
					// The best a remembered thing could add apart from distance: place facts read 0 on a thing.
					const FactSpec& spec = kFactSpecs[row.fact];
					itemBound += std::max(weight * spec.low, weight * spec.high);
				}
			}
			// What the wait must be asked with again: the goal, the claim reading's kinds, tiers, and exclusion,
			// the traits, and the kept target. Not the units wanted: a candidate is passed over for claims only
			// when nothing is left once the takers' claims are counted, which reads quantities, never the units.
			const ClaimReading& reading = request.claims;
			uint64_t			stamp = combined(traits, reading.kinds);
			stamp =
				combined(stamp, (reading.everyHolderTakes ? 1U : 0U) | (uint64_t{reading.tier} << 8) | (uint64_t{reading.idleTier} << 16));
			stamp = combined(stamp, reading.self.current);
			stamp = combined(stamp, reading.self.suspended);
			stamp = combined(stamp, request.keepKey.has_value() ? *request.keepKey + 1 : 0);
			if (request.goal != nullptr) {
				const QueryGoal& goal = *request.goal;
				stamp = combined(stamp, goal.goalId);
				stamp = combined(stamp, goal.destination);
				stamp = combined(
					stamp,
					(uint64_t{std::bit_cast<uint32_t>(goal.destinationPosition.x)} << 32) |
						std::bit_cast<uint32_t>(goal.destinationPosition.y)
				);
				stamp = combined(stamp, (uint64_t{goal.item} << 8) | goal.destinationPriority);
				for (const glm::vec2 spot : goal.builderSpots) {
					stamp = combined(stamp, (uint64_t{std::bit_cast<uint32_t>(spot.x)} << 32) | std::bit_cast<uint32_t>(spot.y));
				}
			}
			requestStamp = stamp;
		}

		/// The record kept for this creature and query, made only for an evaluation that walks, its definition
		/// terms dropped when what they read has moved.
		void adoptRecord() {
			record = &evaluator.recordFor(request.creature, request.query.index);
			if (record->definitionStamp != definitionStamp) {
				record->definitions.clear();
				record->definitionStamp = definitionStamp;
			}
		}

		/// The definition-scoped fact `factIndex` of definition `definition`; a harvestable reads its yield's.
		[[nodiscard]] float definitionFact(uint16_t factIndex, uint32_t definition) const {
			const uint32_t yield = evaluator.registry.harvestYieldId(definition);
			const uint32_t subject = yield != 0 ? yield : definition;
			if (factIndex == fact::kQuality) {
				const assets::AssetDefinition* def = evaluator.registry.getDefinitionById(subject);
				if (def == nullptr || !def->itemProperties.has_value() || !def->itemProperties->edible.has_value()) {
					return 0.0F;
				}
				return static_cast<float>(static_cast<int>(def->itemProperties->edible->quality)) * 0.25F;
			}
			if (factIndex == fact::kColonyStock) {
				float stock = 0.0F;
				for (const KnownBox& box : world.boxes) {
					stock += box.item == subject ? static_cast<float>(box.quantity) : 0.0F;
				}
				return stock;
			}
			return 0.0F;
		}

		const DefinitionTerms& termsOf(uint32_t definition) {
			const auto found = record->definitions.find(definition);
			if (found != record->definitions.end()) {
				return found->second;
			}
			DefinitionTerms terms;
			for (std::size_t index = 0; index < plan.rows.size(); ++index) {
				const FactRowPlan& row = plan.rows[index];
				if (kFactSpecs[row.fact].scope != FactScope::Definition) {
					continue;
				}
				const float value = definitionFact(row.fact, definition);
				if (row.kind == assets::FactRowKind::Weigh) {
					terms.score += evaluator.rowWeights[index] * value;
				} else if (row.kind == assets::FactRowKind::Exclude) {
					terms.excluded = terms.excluded || beyondLine(value, row);
				} else {
					terms.preferredBelow = terms.preferredBelow || beyondLine(value, row);
				}
			}
			return record->definitions.emplace(definition, terms).first->second;
		}

		/// Whether the request's gate (tool, carry room, box room) lets `definition` serve; asked every
		/// evaluation, never cached.
		[[nodiscard]] bool gateLets(uint32_t definition) {
			if (request.gate.admits(definition)) {
				return true;
			}
			gated = true;
			return false;
		}

		/// Units the step wants of `item`: a meal's size at the item's nutrition, or the request's units.
		[[nodiscard]] uint32_t wantedOf(uint32_t item) const {
			const ClaimReading& reading = request.claims;
			if (!reading.mealShortfall.has_value()) {
				return reading.wanted;
			}
			const assets::AssetDefinition* def = evaluator.registry.getDefinitionById(item);
			const float restore = def != nullptr && def->itemProperties.has_value() && def->itemProperties->edible.has_value()
									  ? def->itemProperties->edible->nutrition * 100.0F
									  : 0.0F;
			const float shortfall = *reading.mealShortfall;
			return restore > 0.0F && shortfall > restore ? static_cast<uint32_t>(std::ceil(shortfall / restore)) : 1U;
		}

		// --- Item facts ---

		[[nodiscard]] bool chunkReady(glm::vec2 point) const {
			if (world.chunks == nullptr) {
				return true;
			}
			const world::Chunk* chunk = world.chunks->getChunk(world::worldToChunk(world::WorldPosition{point.x, point.y}));
			return chunk != nullptr && chunk->isReady();
		}

		[[nodiscard]] bool onShore(glm::vec2 point) const {
			if (world.chunks == nullptr) {
				return false;
			}
			const world::Chunk* chunk = world.chunks->getChunk(world::worldToChunk(world::WorldPosition{point.x, point.y}));
			if (chunk == nullptr || !chunk->isReady()) {
				return false;
			}
			const double					 withinMm = static_cast<double>(evaluator.library.factTuning[fact::kOnShore]) * 1000.0;
			const world::TerrainPolygonQuery water(chunk->terrainPolygons());
			return onShoreAtDistance(
				water.distanceToWaterMm(geometry::quantize(point), withinMm), evaluator.library.factTuning[fact::kOnShore]
			);
		}

		/// The sum over remembered things with `capability` within the tuned distance `r` of `point`, strictly,
		/// of 1 - d / r, added in memory-key order so the sum never depends on the order things were remembered.
		[[nodiscard]] float proximity(assets::CapabilityType capability, uint16_t factIndex, glm::vec2 point) {
			const float				radius = evaluator.library.factTuning[factIndex];
			const uint16_t			bit = capabilityBit(capability);
			std::vector<Proximity>& found = evaluator.proximities;
			found.clear();
			const int32_t minX = ecs::memoryCellCoordinate(point.x - radius);
			const int32_t maxX = ecs::memoryCellCoordinate(point.x + radius);
			const int32_t minY = ecs::memoryCellCoordinate(point.y - radius);
			const int32_t maxY = ecs::memoryCellCoordinate(point.y + radius);
			for (const uint32_t definition : memory.definitionsWith(capability)) {
				const ecs::RememberedOfDefinition& remembered = *memory.rememberedOfDefinition(definition);
				for (int32_t cellY = minY; cellY <= maxY; ++cellY) {
					for (int32_t cellX = minX; cellX <= maxX; ++cellX) {
						const ecs::MemoryCell* cell = remembered.cellAt(cellX, cellY);
						if (cell == nullptr) {
							continue;
						}
						for (const uint64_t key : cell->keys) {
							const ecs::KnownWorldEntity& known = *memory.getWorldEntity(key);
							if ((known.capabilityMask & bit) == 0) {
								continue;
							}
							const float distance = glm::length(point - known.position);
							if (distance < radius) {
								found.push_back({.key = key, .value = 1.0F - (distance / radius)});
							}
						}
					}
				}
			}
			std::ranges::sort(found, [](const Proximity& first, const Proximity& second) { return first.key < second.key; });
			float sum = 0.0F;
			for (const Proximity& each : found) {
				sum += each.value;
			}
			return sum;
		}

		[[nodiscard]] float itemFact(uint16_t factIndex, const QueryCandidate& candidate) {
			if (factIndex == fact::kDistance) {
				return candidate.distance;
			}
			if (factIndex == fact::kHeld) {
				return candidate.held;
			}
			if (isPlaceFact(factIndex) && candidate.kind != CandidateKind::Place) {
				return 0.0F;
			}
			if (factIndex == fact::kOnShore) {
				return onShore(candidate.position) ? 1.0F : 0.0F;
			}
			if (factIndex == fact::kWasteNearby) {
				return proximity(assets::CapabilityType::Waste, factIndex, candidate.position);
			}
			if (factIndex == fact::kFoodNearby) {
				return proximity(assets::CapabilityType::Edible, factIndex, candidate.position);
			}
			if (factIndex == fact::kIsPlace) {
				return 1.0F;
			}
			if (factIndex == fact::kSleepQuality) {
				if (candidate.kind == CandidateKind::Place) {
					return evaluator.library.factTuning[factIndex];
				}
				const ecs::KnownWorldEntity* known =
					candidate.kind == CandidateKind::Thing ? memory.getWorldEntity(candidate.key) : nullptr;
				return known != nullptr && (known->capabilityMask & capabilityBit(assets::CapabilityType::Sleepable)) != 0 ? 1.0F : 0.0F;
			}
			if (factIndex == fact::kBelowDestination) {
				const KnownBox* box = boxOf(candidate);
				return box != nullptr && request.goal != nullptr && box->priority < request.goal->destinationPriority ? 1.0F : 0.0F;
			}
			return 0.0F;
		}

		[[nodiscard]] const KnownBox* boxOf(const QueryCandidate& candidate) const {
			if (candidate.kind != CandidateKind::Box) {
				return nullptr;
			}
			for (const KnownBox& box : world.boxes) {
				if (box.box == candidate.entity && box.item == candidate.item) {
					return &box;
				}
			}
			return nullptr;
		}

		// --- Pass-overs ---

		[[nodiscard]] bool joined(glm::vec2 point) const {
			if (world.navigation == nullptr) {
				return true;
			}
			return hisLabel != geometry::nav::kNoComponent && world.navigation->componentAt(point) == hisLabel;
		}

		/// Where a route to `target` ends when the creature is joined to it: the target, or the stand point beside
		/// the placed obstacle it lies in. Ground with a label is clear; a target with none is in water, off the
		/// mesh, or in a placed obstacle, and only the last has a stand point. With no mesh nothing is known
		/// unreachable.
		[[nodiscard]] std::optional<glm::vec2> routeEnd(glm::vec2 target) const {
			if (!haveMesh) {
				return target;
			}
			// An item used from its use areas is reached at its stand point on his own component (rotatable-placed-items 12).
			if (const ecs::PlacedItemIndex::Entry* item = world.placedItems != nullptr ? world.placedItems->installedAt(target) : nullptr;
				item != nullptr && !item->world.useAreas.empty()) {
				return hisLabel != geometry::nav::kNoComponent ? world.navigation->standPointIn(item->standing, request.position, hisLabel) : std::nullopt;
			}
			const geometry::nav::ComponentLabel label = world.navigation->componentAt(target);
			if (label != geometry::nav::kNoComponent) {
				return label == hisLabel ? std::optional<glm::vec2>(target) : std::nullopt;
			}
			if (hisLabel != geometry::nav::kNoComponent && world.navigation->isOnMesh(target)) {
				const std::optional<glm::vec2> stand =
					world.navigation->standPointsBeside(target, request.position, request.agentRadius).nearest();
				return stand.has_value() && joined(*stand) ? stand : std::nullopt;
			}
			return std::nullopt;
		}

		[[nodiscard]] bool hasVerdict(uint64_t sourceKey, glm::vec2 target) const {
			return haveMesh && request.reach != nullptr &&
				   request.reach->hasVerdict(sourceKey != 0 ? sourceKey : ecs::hashWorldEntity(target, 0));
		}

		[[nodiscard]] bool takes(const ecs::ClaimHolder& holder) const {
			const ClaimReading& reading = request.claims;
			if (reading.everyHolderTakes) {
				return true;
			}
			assert(
				reading.tier != ecs::kUnrankedTier && reading.idleTier != ecs::kUnrankedTier && "the eater rule reads two ladder positions"
			);
			return holder.taskType == ecs::TaskType::FulfillNeed && holder.tier != reading.idleTier && holder.tier <= reading.tier;
		}

		/// A remembered thing is a loose pile when it was remembered as carryable, whichever row found it.
		[[nodiscard]] bool isPile(const QueryCandidate& candidate) const {
			if (candidate.kind != CandidateKind::Thing) {
				return false;
			}
			const ecs::KnownWorldEntity* known = memory.getWorldEntity(candidate.key);
			return known != nullptr && (known->capabilityMask & capabilityBit(assets::CapabilityType::Carryable)) != 0;
		}

		/// Reads the claims on `candidate`: whether it is held, what a pile or a box has left, the claim it would
		/// take, and the holder that takes it from this creature when the step names the claim that fits it.
		[[nodiscard]] PassOverReason readClaims(QueryCandidate& candidate, std::optional<ecs::ClaimHolder>& holder) {
			const ClaimReading&	   reading = request.claims;
			const ecs::ClaimIndex* claims = world.claims;
			candidate.wanted = wantedOf(candidate.item);
			candidate.left = candidate.wanted;
			candidate.claimQuantity = 1;
			if (candidate.kind == CandidateKind::Place) {
				return PassOverReason::None;
			}
			if (candidate.kind == CandidateKind::Spot) {
				candidate.claimKind = assets::ClaimKind::Slot;
				candidate.claimTarget = {.kind = ecs::TargetKind::Entity, .key = request.goal->destination};
				const auto slotHolder = claims != nullptr
											? claims->slotHolder(candidate.claimTarget, static_cast<uint16_t>(candidate.key), reading.self)
											: std::nullopt;
				candidate.held = slotHolder.has_value() ? 1.0F : 0.0F;
				if (slotHolder.has_value() && namesClaim(reading.kinds, assets::ClaimKind::Slot)) {
					holder = slotHolder;
					return PassOverReason::Claimed;
				}
				return PassOverReason::None;
			}
			if (candidate.kind == CandidateKind::Box || isPile(candidate)) {
				candidate.claimKind = assets::ClaimKind::SourceQuantity;
				candidate.claimTarget = candidate.kind == CandidateKind::Box
											? ecs::ClaimTarget{.kind = ecs::TargetKind::Entity, .key = candidate.entity}
											: ecs::ClaimTarget{.kind = ecs::TargetKind::WorldKey, .key = candidate.key};
				std::optional<uint32_t> live;
				if (const KnownBox* box = boxOf(candidate)) {
					live = box->quantity;
				} else if (candidate.kind == CandidateKind::Thing && world.piles != nullptr && world.world != nullptr) {
					const ecs::EntityID pile = world.piles->find(*world.world, candidate.item, candidate.position);
					if (pile != ecs::kInvalidEntity) {
						live = world.world->getComponent<ecs::ResourceStack>(pile)->quantity;
						liveCountRead = true;
					}
				}
				ecs::ClaimIndex::SourceShare taken;
				uint32_t					 claimed = 0;
				if (claims != nullptr) {
					taken = claims->sourceClaimedWhere(
						candidate.claimTarget, candidate.item, reading.self, [this](const ecs::ClaimHolder& each) { return takes(each); }
					);
					claimed = claims->sourceClaimed(candidate.claimTarget, candidate.item, reading.self);
				}
				const uint32_t wanted = candidate.wanted;
				const uint32_t have = live.value_or(taken.quantity > 0 ? 0U : wanted);
				candidate.left = have > taken.quantity ? have - taken.quantity : 0U;
				const uint32_t unclaimed = live.has_value() ? (have > claimed ? have - claimed : 0U) : (claimed > 0 ? 0U : wanted);
				candidate.held = unclaimed < std::min(wanted, candidate.left) ? 1.0F : 0.0F;
				candidate.claimQuantity = std::min(wanted, candidate.left);
				if (live.has_value() && *live == 0) {
					return PassOverReason::Exhausted;
				}
				if (candidate.left == 0 && namesClaim(reading.kinds, assets::ClaimKind::SourceQuantity)) {
					holder = taken.holder;
					return taken.holder.has_value() ? PassOverReason::Claimed : PassOverReason::Exhausted;
				}
				return PassOverReason::None;
			}
			candidate.claimKind = assets::ClaimKind::Use;
			candidate.claimTarget = candidate.kind == CandidateKind::Thing
										? ecs::ClaimTarget{.kind = ecs::TargetKind::WorldKey, .key = candidate.key}
										: ecs::ClaimTarget{.kind = ecs::TargetKind::Entity, .key = candidate.entity};
			if (claims == nullptr) {
				return PassOverReason::None;
			}
			const std::optional<ecs::ClaimHolder> anyHolder = claims->useHolder(candidate.claimTarget, reading.self);
			const std::optional<ecs::ClaimHolder> taker =
				reading.everyHolderTakes
					? anyHolder
					: claims->useHolderWhere(candidate.claimTarget, reading.self, [this](const ecs::ClaimHolder& each) {
						  return takes(each);
					  });
			if (taker.has_value() && namesClaim(reading.kinds, assets::ClaimKind::Use)) {
				holder = taker;
				return PassOverReason::Claimed;
			}
			candidate.held = anyHolder.has_value() ? 1.0F : 0.0F;
			return PassOverReason::None;
		}

		[[nodiscard]] const SourcePlan& sourceOf(const QueryCandidate& candidate) const {
			for (const SourcePlan& source : plan.sources) {
				if (source.row == candidate.sourceRow) {
					return source;
				}
			}
			return plan.sources.front();
		}

		/// The pass-overs in order: a route verdict, a packaged box, not joined, regrowing (so the regrowing
		/// source shown is the nearest the creature can reach), and the claims.
		[[nodiscard]] PassOverReason passOver(QueryCandidate& candidate, std::optional<ecs::ClaimHolder>& holder) {
			if (candidate.kind == CandidateKind::Place) {
				if (sourceOf(candidate).source != assets::CandidateSource::PlaceHere && hasVerdict(0, candidate.position)) {
					return PassOverReason::Verdict;
				}
				candidate.goal = candidate.position;
				return readClaims(candidate, holder);
			}
			const uint64_t verdictKey = candidate.kind == CandidateKind::Thing || candidate.kind == CandidateKind::Box ? candidate.key : 0;
			if (hasVerdict(verdictKey, candidate.position)) {
				return PassOverReason::Verdict;
			}
			if (const KnownBox* box = boxOf(candidate); box != nullptr && box->packaged) {
				return PassOverReason::Packaged;
			}
			const std::optional<glm::vec2> end = routeEnd(candidate.position);
			if (!end.has_value()) {
				return PassOverReason::Unreachable;
			}
			candidate.goal = *end;
			if (candidate.kind == CandidateKind::Thing && world.regrowth != nullptr && *world.regrowth) {
				const ecs::KnownWorldEntity* known = memory.getWorldEntity(candidate.key);
				if ((known->capabilityMask & capabilityBit(assets::CapabilityType::Harvestable)) != 0) {
					if (const std::optional<double> readyAt = (*world.regrowth)(candidate.defNameId, candidate.position, world.now)) {
						firstReadyAt = std::min(firstReadyAt, *readyAt);
						return PassOverReason::Regrowing;
					}
				}
			}
			return readClaims(candidate, holder);
		}

		// --- Considering one candidate ---

		void keepIn(std::vector<QueryCandidate>& list, const QueryCandidate& candidate) const {
			const auto place = std::upper_bound(list.begin(), list.end(), candidate, ranksAhead);
			if (static_cast<std::size_t>(place - list.begin()) >= capacity) {
				return;
			}
			list.insert(place, candidate);
			if (list.size() > capacity) {
				list.pop_back();
			}
		}

		[[nodiscard]] bool excludedBy(const QueryCandidate& candidate, bool heldRows) {
			for (const FactRowPlan& row : plan.rows) {
				if (row.kind == assets::FactRowKind::Exclude && kFactSpecs[row.fact].scope == FactScope::Item &&
					(row.fact == fact::kHeld) == heldRows && beyondLine(itemFact(row.fact, candidate), row)) {
					return true;
				}
			}
			return false;
		}

		/// Exclusions, pass-overs, the score, and the prefer class; a survivor joins its class's ranking. True
		/// when it survived.
		bool consider(QueryCandidate candidate, const DefinitionTerms* terms) {
			anyCandidate = true;
			if (counting) {
				directSurvivors += survives(candidate, terms) ? 1U : 0U;
				return false;
			}
			// The kept target was asked first, in this phase, as this kind of candidate: it is not asked again.
			if (keptConsidered.has_value() && !candidate.fallback && candidate.kind == keptKind && candidate.key == *keptConsidered) {
				return false;
			}
			if (!candidate.fallback && excludedBy(candidate, false)) {
				return false;
			}
			++visits;
			std::optional<ecs::ClaimHolder> holder;
			liveCountRead = false;
			const PassOverReason reason = passOver(candidate, holder);
			if (reason == PassOverReason::None && !candidate.fallback && excludedBy(candidate, true)) {
				// `held` read the units wanted and a live count, which no stamp covers.
				waitable = false;
				return false;
			}
			float score = terms != nullptr ? terms->score : 0.0F;
			bool  below = !candidate.fallback && terms != nullptr && terms->preferredBelow;
			for (std::size_t index = 0; index < plan.rows.size(); ++index) {
				const FactRowPlan& row = plan.rows[index];
				if (kFactSpecs[row.fact].scope != FactScope::Item) {
					continue;
				}
				if (row.kind == assets::FactRowKind::Weigh) {
					score += evaluator.rowWeights[index] * itemFact(row.fact, candidate);
				} else if (row.kind == assets::FactRowKind::Prefer && !candidate.fallback && !below) {
					below = beyondLine(itemFact(row.fact, candidate), row);
				}
			}
			candidate.score = score;
			candidate.preferClass = below ? 1 : 0;

			if (reason != PassOverReason::None) {
				notePassedOver(candidate, reason, holder);
				return false;
			}
			keepIn(candidate.fallback ? evaluator.fallbackClass : below ? evaluator.lowerClass : evaluator.upperClass, candidate);
			return true;
		}

		void notePassedOver(const QueryCandidate& candidate, PassOverReason reason, const std::optional<ecs::ClaimHolder>& holder) {
			passedOverAny = true;
			// What no stamp covers: where a mover is now, and a pile's live count.
			waitable = waitable && candidate.kind != CandidateKind::Mover && !(isPile(candidate) && liveCountRead);
			passedOverUnreachable = passedOverUnreachable || reason == PassOverReason::Unreachable;
			passedOverClaimed = passedOverClaimed || reason == PassOverReason::Claimed;
			if (!waitingOn.has_value() || tellingness(reason) > tellingness(waitingOnReason) ||
				(reason == waitingOnReason && ranksAhead(candidate, *waitingOn))) {
				waitingOn = candidate;
				waitingOnReason = reason;
			}
			if (reason == PassOverReason::Claimed && (!bestClaimed.has_value() || ranksAhead(candidate, *bestClaimed))) {
				bestClaimed = candidate;
				bestClaimedHolder = holder.has_value() ? holder->colonist : ecs::kInvalidEntity;
			}
		}

		// --- The kept target ---

		/// The target the creature is heading for is asked first, among the rows that are not fallback rows: a
		/// remembered thing when one of those rows takes it, else a box the colony knows under that key. It is
		/// taken alone when it survives in the upper prefer class, the best a candidate can reach; otherwise the
		/// walk goes on and passes it by, so it is asked once, and a kept target in the lower class is still the
		/// pick when nothing of the upper class survives (run()).
		bool considerKept() {
			if (!request.keepKey.has_value()) {
				return false;
			}
			const uint64_t keepKey = *request.keepKey;
			if (const ecs::KnownWorldEntity* known = memory.getWorldEntity(keepKey)) {
				collectSeeds(false);
				const auto seed = std::ranges::lower_bound(evaluator.seeds, known->defNameId, {}, &Seed::definition);
				const std::optional<std::size_t> row = seed != evaluator.seeds.end() && seed->definition == known->defNameId
														   ? rowAccepting(seed->rows, known->capabilityMask)
														   : std::nullopt;
				if (row.has_value()) {
					const DefinitionTerms& terms = termsOf(known->defNameId);
					if (!terms.excluded && gateLets(known->defNameId)) {
						QueryCandidate candidate = thingCandidate(*known, keepKey, *row, false);
						candidate.distance = glm::length(known->position - request.position);
						consider(candidate, &terms);
					}
					keptConsidered = keepKey;
					keptKind = CandidateKind::Thing;
					return !evaluator.upperClass.empty();
				}
			}
			bool boxAsked = false;
			for (const SourcePlan& source : plan.sources) {
				if (source.source != assets::CandidateSource::Box || source.fallback) {
					continue;
				}
				for (const KnownBox& box : world.boxes) {
					if (box.key != keepKey || !boxWanted(source, box) || !evaluator.upperClass.empty()) {
						continue;
					}
					considerBox(source, box, false);
					boxAsked = true;
				}
			}
			if (boxAsked) {
				keptConsidered = keepKey;
				keptKind = CandidateKind::Box;
			}
			return !evaluator.upperClass.empty();
		}

		// --- Horizons ---

		/// The score the K-th survivor of a class has, below which nothing new matters; -infinity until K are
		/// known.
		[[nodiscard]] float thresholdOf(const std::vector<QueryCandidate>& list) const {
			return list.size() >= capacity ? list[capacity - 1].score : -kInfinity;
		}

		/// The score an item of a definition with `definitelyBelow` would have to beat to matter.
		[[nodiscard]] float thresholdFor(bool fallbackPhase, bool definitelyBelow) const {
			if (fallbackPhase) {
				return thresholdOf(evaluator.fallbackClass);
			}
			const bool haveUpper = !evaluator.upperClass.empty();
			if (definitelyBelow) {
				return haveUpper ? kInfinity : thresholdOf(evaluator.lowerClass);
			}
			if (itemPreferRows && !haveUpper) {
				return -kInfinity;
			}
			return thresholdOf(evaluator.upperClass);
		}

		/// How far from the creature an item whose best score apart from distance is `bound` could still beat
		/// `threshold`.
		[[nodiscard]] float horizon(float bound, float threshold) const {
			if (threshold == kInfinity) {
				return -kInfinity;
			}
			float byScore = kInfinity;
			if (distanceWeight < 0.0F && threshold != -kInfinity && bound != kInfinity) {
				byScore = (bound - threshold) / -distanceWeight;
			}
			return std::min(byScore, plan.distanceAbove);
		}

		[[nodiscard]] float seedHorizon(const Seed& seed, bool fallbackPhase) const {
			return horizon(seed.bound, thresholdFor(fallbackPhase, !fallbackPhase && seed.terms->preferredBelow));
		}

		// --- Candidates from memory ---

		/// The first of `rows` (bits by source index) whose requirement a thing remembered with `mask` meets.
		[[nodiscard]] std::optional<std::size_t> rowAccepting(uint32_t rows, uint16_t mask) const {
			for (uint32_t remaining = rows; remaining != 0; remaining &= remaining - 1) {
				const auto		  index = static_cast<std::size_t>(std::countr_zero(remaining));
				const SourcePlan& source = plan.sources[index];
				if (!source.required.has_value() || (mask & capabilityBit(*source.required)) != 0) {
					return index;
				}
			}
			return std::nullopt;
		}

		void seedDefinition(uint32_t definition, std::size_t sourceIndex) {
			evaluator.seeds.push_back(Seed{.definition = definition, .rows = 1U << sourceIndex});
		}

		/// The remembered definitions the phase's memory rows name, one seed each with every row naming it,
		/// sorted by definition.
		void collectSeeds(bool fallbackPhase) {
			std::vector<Seed>& seeds = evaluator.seeds;
			seeds.clear();
			for (std::size_t index = 0; index < plan.sources.size(); ++index) {
				const SourcePlan& source = plan.sources[index];
				if (source.fallback != fallbackPhase) {
					continue;
				}
				switch (source.source) {
					case assets::CandidateSource::Capability:
					case assets::CandidateSource::Kind:
						if (source.mover == 0 && source.required.has_value()) {
							for (const uint32_t definition : memory.definitionsWith(*source.required)) {
								seedDefinition(definition, index);
							}
						}
						break;
					case assets::CandidateSource::Item:
					case assets::CandidateSource::Pile:
						if (source.itemIsGoal) {
							if (request.goal != nullptr) {
								seedDefinition(request.goal->item, index);
							}
						} else {
							for (const uint32_t definition : source.definitions) {
								seedDefinition(definition, index);
							}
						}
						break;
					case assets::CandidateSource::Yields:
						if (source.itemIsGoal) {
							if (request.goal != nullptr) {
								for (const uint32_t definition : evaluator.registry.harvestablesYielding(request.goal->item)) {
									seedDefinition(definition, index);
								}
							}
						} else {
							for (const uint32_t definition : source.definitions) {
								seedDefinition(definition, index);
							}
						}
						break;
					default:
						break;
				}
			}
			std::ranges::sort(seeds, [](const Seed& first, const Seed& second) { return first.definition < second.definition; });
			std::size_t kept = 0;
			for (std::size_t index = 0; index < seeds.size(); ++index) {
				if (kept > 0 && seeds[kept - 1].definition == seeds[index].definition) {
					seeds[kept - 1].rows |= seeds[index].rows;
				} else {
					seeds[kept++] = seeds[index];
				}
			}
			seeds.resize(kept);
		}

		/// A remembered thing as a candidate. What it is a source of comes from how it was remembered, not from
		/// which row found it: a carryable thing is a pile of itself, a harvestable a source of its yield.
		[[nodiscard]] QueryCandidate
		thingCandidate(const ecs::KnownWorldEntity& known, uint64_t key, std::size_t sourceIndex, bool fallbackPhase) const {
			QueryCandidate candidate{.kind = CandidateKind::Thing, .sourceRow = plan.sources[sourceIndex].row, .fallback = fallbackPhase};
			candidate.key = key;
			candidate.defNameId = known.defNameId;
			if ((known.capabilityMask & capabilityBit(assets::CapabilityType::Carryable)) != 0) {
				candidate.item = known.defNameId;
			} else if ((known.capabilityMask & capabilityBit(assets::CapabilityType::Harvestable)) != 0) {
				candidate.item = evaluator.registry.harvestYieldId(known.defNameId);
			}
			candidate.position = known.position;
			candidate.goal = known.position;
			return candidate;
		}

		void pushCell(const ecs::RememberedOfDefinition& remembered, uint32_t seedIndex, uint32_t cell) {
			const ecs::MemoryCell& memoryCell = remembered.cells[cell];
			evaluator.cellSteps.push_back(
				{.distanceSquared = distanceSquaredToCell(request.position, memoryCell.x, memoryCell.y), .seed = seedIndex, .cell = cell}
			);
			++cellsTouched;
		}

		/// The ring `ring` of cells around the creature's cell, for `seed`: the occupied ones become cell steps
		/// and the next ring is queued, until looking costs more than opening every occupied cell left, which
		/// are then opened at once.
		void openRing(uint32_t seedIndex, int32_t ring, std::vector<CellStep>& cells) {
			Seed&							   seed = evaluator.seeds[seedIndex];
			const ecs::RememberedOfDefinition& remembered = *memory.rememberedOfDefinition(seed.definition);
			const uint32_t					   ringCells = ring == 0 ? 1U : 8U * static_cast<uint32_t>(ring);
			const auto						   cellFarther = [](const CellStep& first, const CellStep& second) {
				return first.distanceSquared > second.distanceSquared;
			};
			const auto ringOf = [&](int32_t cellX, int32_t cellY) {
				return std::max(std::abs(static_cast<int64_t>(cellX) - centerX), std::abs(static_cast<int64_t>(cellY) - centerY));
			};
			if (seed.ringLookups + ringCells > remembered.cells.size()) {
				for (uint32_t cell = 0; cell < remembered.cells.size(); ++cell) {
					if (ringOf(remembered.cells[cell].x, remembered.cells[cell].y) >= ring) {
						pushCell(remembered, seedIndex, cell);
						std::push_heap(cells.begin(), cells.end(), cellFarther);
					}
				}
				return;
			}
			seed.ringLookups += ringCells;
			cellsTouched += ringCells;
			const auto lookIn = [&](int32_t cellX, int32_t cellY) {
				if (const ecs::MemoryCell* found = remembered.cellAt(cellX, cellY)) {
					const auto cell = static_cast<uint32_t>(found - remembered.cells.data());
					cells.push_back(
						{.distanceSquared = distanceSquaredToCell(request.position, cellX, cellY), .seed = seedIndex, .cell = cell}
					);
					std::push_heap(cells.begin(), cells.end(), cellFarther);
				}
			};
			if (ring == 0) {
				lookIn(centerX, centerY);
			} else {
				for (int32_t cellX = centerX - ring; cellX <= centerX + ring; ++cellX) {
					lookIn(cellX, centerY - ring);
					lookIn(cellX, centerY + ring);
				}
				for (int32_t cellY = centerY - ring + 1; cellY <= centerY + ring - 1; ++cellY) {
					lookIn(centerX - ring, cellY);
					lookIn(centerX + ring, cellY);
				}
			}
			const int64_t outermost = std::max(
				{ringOf(remembered.minCellX, remembered.minCellY),
				 ringOf(remembered.maxCellX, remembered.maxCellY),
				 ringOf(remembered.minCellX, remembered.maxCellY),
				 ringOf(remembered.maxCellX, remembered.minCellY)}
			);
			if (ring < outermost) {
				cells.push_back(
					{.distanceSquared = distanceSquaredToRing(request.position, centerX, centerY, ring + 1),
					 .seed = seedIndex,
					 .ring = ring + 1}
				);
				std::push_heap(cells.begin(), cells.end(), cellFarther);
			}
		}

		/// The phase's remembered things nearest first, each definition only out to its horizon, ending when the
		/// next is beyond every horizon.
		void walkMemory(bool fallbackPhase) {
			collectSeeds(fallbackPhase);
			std::vector<Seed>&		seeds = evaluator.seeds;
			std::vector<CellStep>&	cells = evaluator.cellSteps;
			std::vector<ThingStep>& things = evaluator.thingSteps;
			cells.clear();
			things.clear();
			float maxBound = -kInfinity;
			for (uint32_t index = 0; index < seeds.size(); ++index) {
				Seed&							   seed = seeds[index];
				const ecs::RememberedOfDefinition* remembered = memory.rememberedOfDefinition(seed.definition);
				if (remembered == nullptr || remembered->keys.empty()) {
					continue;
				}
				anyCandidate = true;
				seed.terms = &termsOf(seed.definition);
				if ((!fallbackPhase && seed.terms->excluded) || !gateLets(seed.definition)) {
					continue;
				}
				seed.bound = seed.terms->score + itemBound;
				maxBound = std::max(maxBound, seed.bound);
				if (remembered->cells.size() <= kCellsOpenedAtOnce) {
					for (uint32_t cell = 0; cell < remembered->cells.size(); ++cell) {
						pushCell(*remembered, index, cell);
					}
				} else {
					cells.push_back({.distanceSquared = 0.0F, .seed = index, .ring = 0});
				}
			}
			const auto cellFarther = [](const CellStep& first, const CellStep& second) {
				return first.distanceSquared > second.distanceSquared;
			};
			const auto thingFarther = [](const ThingStep& first, const ThingStep& second) {
				return first.distanceSquared != second.distanceSquared ? first.distanceSquared > second.distanceSquared
																	   : first.key > second.key;
			};
			std::make_heap(cells.begin(), cells.end(), cellFarther);

			while (!cells.empty() || !things.empty()) {
				const float nextCell = cells.empty() ? kInfinity : cells.front().distanceSquared;
				const float nextThing = things.empty() ? kInfinity : things.front().distanceSquared;
				const float everyHorizon =
					std::max(horizon(maxBound, thresholdFor(fallbackPhase, false)), horizon(maxBound, thresholdFor(fallbackPhase, true)));
				if (std::sqrt(std::min(nextCell, nextThing)) > everyHorizon + kHorizonSlackMeters) {
					break;
				}
				if (nextThing < nextCell) {
					std::pop_heap(things.begin(), things.end(), thingFarther);
					const ThingStep step = things.back();
					things.pop_back();
					const Seed& seed = seeds[step.seed];
					const float distance = std::sqrt(step.distanceSquared);
					if (distance > seedHorizon(seed, fallbackPhase) + kHorizonSlackMeters) {
						continue;
					}
					const ecs::KnownWorldEntity&	 known = *memory.getWorldEntity(step.key);
					const std::optional<std::size_t> row = rowAccepting(seed.rows, known.capabilityMask);
					QueryCandidate					 candidate = thingCandidate(known, step.key, *row, fallbackPhase);
					candidate.distance = distance;
					consider(candidate, seed.terms);
					continue;
				}
				std::pop_heap(cells.begin(), cells.end(), cellFarther);
				const CellStep step = cells.back();
				cells.pop_back();
				const Seed& seed = seeds[step.seed];
				if (std::sqrt(step.distanceSquared) > seedHorizon(seed, fallbackPhase) + kHorizonSlackMeters) {
					continue;
				}
				if (step.ring >= 0) {
					openRing(step.seed, step.ring, cells);
					continue;
				}
				const ecs::MemoryCell& cell = memory.rememberedOfDefinition(seed.definition)->cells[step.cell];
				for (const uint64_t key : cell.keys) {
					const ecs::KnownWorldEntity& known = *memory.getWorldEntity(key);
					if (!rowAccepting(seed.rows, known.capabilityMask).has_value()) {
						continue;
					}
					const glm::vec2 offset = known.position - request.position;
					things.push_back({.distanceSquared = glm::dot(offset, offset), .key = key, .seed = step.seed});
					std::push_heap(things.begin(), things.end(), thingFarther);
				}
			}
		}

		// --- Candidates from elsewhere ---

		[[nodiscard]] QueryCandidate
		directCandidate(CandidateKind kind, const SourcePlan& source, bool fallbackPhase, glm::vec2 position) const {
			QueryCandidate candidate{.kind = kind, .sourceRow = source.row, .fallback = fallbackPhase};
			candidate.position = position;
			candidate.goal = position;
			candidate.distance = glm::length(position - request.position);
			return candidate;
		}

		/// Walkable ground the creature is joined to, in a chunk that is ready.
		[[nodiscard]] bool walkable(glm::vec2 point) const {
			if (world.navigation == nullptr) {
				return true;
			}
			return world.navigation->isValidPosition(point) && chunkReady(point) && joined(point);
		}

		/// Walkable at `point`, and at the rim of the creature's disc there every 45 degrees.
		[[nodiscard]] bool discFits(glm::vec2 point) const {
			if (!walkable(point)) {
				return false;
			}
			if (world.navigation == nullptr) {
				return true;
			}
			constexpr float kDiagonal = 0.70710678F;
			for (const glm::vec2 rim :
				 {glm::vec2{1.0F, 0.0F},
				  glm::vec2{-1.0F, 0.0F},
				  glm::vec2{0.0F, 1.0F},
				  glm::vec2{0.0F, -1.0F},
				  glm::vec2{kDiagonal, kDiagonal},
				  glm::vec2{-kDiagonal, kDiagonal},
				  glm::vec2{kDiagonal, -kDiagonal},
				  glm::vec2{-kDiagonal, -kDiagonal}}) {
				if (!world.navigation->isValidPosition(point + rim * request.agentRadius)) {
					return false;
				}
			}
			return true;
		}

		void considerPlaces(const SourcePlan& source, bool fallbackPhase) {
			if (source.source == assets::CandidateSource::PlaceHere) {
				consider(directCandidate(CandidateKind::Place, source, fallbackPhase, request.position), nullptr);
				return;
			}
			if (source.source == assets::CandidateSource::PlaceGrid) {
				// The toilet finder's grid: from the creature, every spacing out to the radius, row by row.
				const int gridSize = static_cast<int>(std::ceil(source.radius / source.spacing));
				uint64_t  order = 0;
				for (int dy = -gridSize; dy <= gridSize; ++dy) {
					for (int dx = -gridSize; dx <= gridSize; ++dx, ++order) {
						const glm::vec2 point =
							request.position + glm::vec2(static_cast<float>(dx) * source.spacing, static_cast<float>(dy) * source.spacing);
						if (glm::length(point - request.position) > source.radius || !walkable(point)) {
							continue;
						}
						QueryCandidate candidate = directCandidate(CandidateKind::Place, source, fallbackPhase, point);
						candidate.key = order;
						consider(candidate, nullptr);
					}
				}
				return;
			}
			// An angle, then a distance, for each point; a point whose disc does not fit is skipped. Counting draws
			// nothing.
			if (world.generator == nullptr || counting) {
				return;
			}
			for (int index = 0; index < source.count; ++index) {
				const float		angle = unitDraw(*world.generator) * 2.0F * std::numbers::pi_v<float>;
				const float		distance = source.minRadius + unitDraw(*world.generator) * (source.radius - source.minRadius);
				const glm::vec2 point = request.position + glm::vec2{std::cos(angle) * distance, std::sin(angle) * distance};
				if (!discFits(point)) {
					continue;
				}
				QueryCandidate candidate = directCandidate(CandidateKind::Place, source, fallbackPhase, point);
				candidate.key = static_cast<uint64_t>(index);
				consider(candidate, nullptr);
			}
		}

		[[nodiscard]] bool boxWanted(const SourcePlan& source, const KnownBox& box) const {
			return source.itemIsGoal ? request.goal != nullptr && box.item == request.goal->item
									 : std::ranges::binary_search(source.items, box.item);
		}

		bool considerBox(const SourcePlan& source, const KnownBox& box, bool fallbackPhase) {
			QueryCandidate candidate = directCandidate(CandidateKind::Box, source, fallbackPhase, box.position);
			candidate.key = box.key;
			candidate.item = box.item;
			candidate.defNameId = box.item;
			candidate.entity = box.box;
			return considerDefined(candidate, fallbackPhase);
		}

		/// The phase's candidates that are not remembered things: boxes, movers, the goal's, and places (only
		/// those of `placesBelow`).
		void considerDirect(bool fallbackPhase, bool placesBelow) {
			for (const SourcePlan& source : plan.sources) {
				if (source.fallback != fallbackPhase) {
					continue;
				}
				switch (source.source) {
					case assets::CandidateSource::Box:
						if (placesBelow) {
							break;
						}
						for (const KnownBox& box : world.boxes) {
							if (boxWanted(source, box)) {
								considerBox(source, box, fallbackPhase);
							}
						}
						break;
					case assets::CandidateSource::Kind:
						if (placesBelow || source.mover == 0) {
							break;
						}
						memory.forEachDynamicEntitySeenWithin(
							source.mover, world.now, world.recentSightingGameMinutes, [&](const ecs::KnownDynamicEntity& known) {
								QueryCandidate candidate =
									directCandidate(CandidateKind::Mover, source, fallbackPhase, known.lastKnownPosition);
								candidate.key = known.entityId;
								candidate.entity = known.entityId;
								candidate.defNameId = known.defNameId;
								considerDefined(candidate, fallbackPhase);
								return true;
							}
						);
						break;
					case assets::CandidateSource::GoalDestination:
						// An item with nowhere to stand is no destination (rotatable-placed-items 12).
						if (!placesBelow && request.goal != nullptr && !ecs::GoalTaskRegistry::Get().nowhereToStand(request.goal->goalId)) {
							QueryCandidate candidate =
								directCandidate(CandidateKind::Destination, source, fallbackPhase, request.goal->destinationPosition);
							candidate.key = request.goal->destination;
							candidate.entity = request.goal->destination;
							consider(candidate, nullptr);
						}
						break;
					case assets::CandidateSource::GoalSpots:
						if (!placesBelow && request.goal != nullptr) {
							for (std::size_t spot = 0; spot < request.goal->builderSpots.size(); ++spot) {
								QueryCandidate candidate =
									directCandidate(CandidateKind::Spot, source, fallbackPhase, request.goal->builderSpots[spot]);
								candidate.key = spot;
								consider(candidate, nullptr);
							}
						}
						break;
					case assets::CandidateSource::PlaceHere:
					case assets::CandidateSource::PlaceAround:
					case assets::CandidateSource::PlaceGrid:
						if (fallbackPhase || source.placesBelow == placesBelow) {
							considerPlaces(source, fallbackPhase);
						}
						break;
					default:
						break;
				}
			}
		}

		bool considerDefined(const QueryCandidate& candidate, bool fallbackPhase) {
			const DefinitionTerms& terms = termsOf(candidate.defNameId);
			if ((!fallbackPhase && terms.excluded) || !gateLets(candidate.defNameId)) {
				anyCandidate = true;
				return false;
			}
			return consider(candidate, &terms);
		}

		void walkPhase(bool fallbackPhase) {
			considerDirect(fallbackPhase, false);
			walkMemory(fallbackPhase);
			if (!fallbackPhase && evaluator.upperClass.empty()) {
				considerDirect(false, true);
			}
		}

		// --- Counting ---

		[[nodiscard]] bool survives(QueryCandidate candidate, const DefinitionTerms* terms) {
			if (candidate.fallback) {
				return true;
			}
			if (terms != nullptr && terms->excluded) {
				return false;
			}
			if (excludedBy(candidate, false)) {
				return false;
			}
			std::optional<ecs::ClaimHolder> unused;
			(void)readClaims(candidate, unused);
			return !excludedBy(candidate, true);
		}

		uint32_t countPhase(bool fallbackPhase) {
			uint32_t survivors = 0;
			collectSeeds(fallbackPhase);
			for (const Seed& seed : evaluator.seeds) {
				const ecs::RememberedOfDefinition* remembered = memory.rememberedOfDefinition(seed.definition);
				if (remembered == nullptr || !gateLets(seed.definition)) {
					continue;
				}
				const DefinitionTerms& terms = termsOf(seed.definition);
				for (const uint64_t key : remembered->keys) {
					const ecs::KnownWorldEntity&	 known = *memory.getWorldEntity(key);
					const std::optional<std::size_t> row = rowAccepting(seed.rows, known.capabilityMask);
					if (!row.has_value()) {
						continue;
					}
					QueryCandidate candidate = thingCandidate(known, key, *row, fallbackPhase);
					candidate.distance = glm::length(known.position - request.position);
					survivors += survives(candidate, &terms) ? 1U : 0U;
				}
			}
			// The other candidates are counted as consider() is handed them.
			directSurvivors = 0;
			considerDirect(fallbackPhase, false);
			if (!fallbackPhase) {
				considerDirect(false, true);
			}
			return survivors + static_cast<uint32_t>(directSurvivors);
		}

		// --- The pick ---

		[[nodiscard]] std::size_t weightedPick(const std::vector<QueryCandidate>& chosen) const {
			const std::size_t pool = std::min(chosen.size(), kWeightedPool);
			if (world.generator == nullptr || pool <= 1) {
				return 0;
			}
			float lowest = chosen[0].score;
			for (std::size_t index = 1; index < pool; ++index) {
				lowest = std::min(lowest, chosen[index].score);
			}
			float total = 0.0F;
			for (std::size_t index = 0; index < pool; ++index) {
				total += chosen[index].score - lowest + 1.0F;
			}
			const float draw = unitDraw(*world.generator) * total;
			float		reached = 0.0F;
			for (std::size_t index = 0; index < pool; ++index) {
				reached += chosen[index].score - lowest + 1.0F;
				if (draw < reached) {
					return index;
				}
			}
			return pool - 1;
		}

		// --- The wait ---

		[[nodiscard]] uint64_t claimsNow() const { return world.claims != nullptr ? world.claims->fingerprint() : 0; }

		/// What the walk read that can change: the creature's memory, its verdicts, the boxes, the request, the
		/// claims (colony-wide, so only when a candidate was passed over as taken), the mesh's connectivity (only
		/// when one was passed over as not joined), and where it stands (its exact position when the query has
		/// places or a distance exclusion, else the component it stands on).
		[[nodiscard]] Wait stampNow() const {
			Wait wait{
				.memoryVersion = memory.worldEntityVersion,
				.readyAt = firstReadyAt,
				.verdictGeneration = request.reach != nullptr ? request.reach->verdictsChanged() : 0,
				.boxesStamp = world.boxesStamp,
				.requestStamp = requestStamp,
			};
			if (passedOverClaimed) {
				wait.claimsFingerprint = claimsNow();
			}
			if (passedOverUnreachable && haveMesh) {
				wait.connectivityEpoch = world.navigation->connectivityEpoch();
			}
			if (plan.hasPlaceRows || plan.excludesDistance) {
				wait.position = request.position;
			} else {
				wait.component = hisLabel;
			}
			return wait;
		}

		[[nodiscard]] bool waitHolds(const Wait& wait) const {
			const bool epochHolds =
				!wait.connectivityEpoch.has_value() || (haveMesh && world.navigation->connectivityEpoch() == *wait.connectivityEpoch);
			const bool claimsHold = !wait.claimsFingerprint.has_value() || claimsNow() == *wait.claimsFingerprint;
			const bool placeHolds = wait.position.has_value() ? *wait.position == request.position : wait.component == hisLabel;
			return memory.worldEntityVersion == wait.memoryVersion && world.now < wait.readyAt && claimsHold &&
				   (request.reach != nullptr ? request.reach->verdictsChanged() : 0) == wait.verdictGeneration &&
				   world.boxesStamp == wait.boxesStamp && requestStamp == wait.requestStamp && epochHolds && placeHolds;
		}

		QueryEvaluator&				  evaluator;
		const QueryRequest&			  request;
		const QueryWorld&			  world;
		const assets::QueryEntry&	  entry;
		const Plan&					  plan;
		const ecs::Memory&			  memory;
		Record*						  record = nullptr;
		uint64_t					  definitionStamp = 0;
		bool						  haveMesh = false;
		geometry::nav::ComponentLabel hisLabel = geometry::nav::kNoComponent;
		int32_t						  centerX = 0;
		int32_t						  centerY = 0;
		float						  distanceWeight = 0.0F;
		float						  itemBound = 0.0F;
		uint64_t					  requestStamp = 0;
		std::size_t					  capacity = 1;
		bool						  itemPreferRows = false;
		bool						  counting = false;
		std::size_t					  directSurvivors = 0;
		uint32_t					  visits = 0;
		uint32_t					  cellsTouched = 0;
		bool						  anyCandidate = false;
		bool						  passedOverClaimed = false;
		bool						  passedOverAny = false;
		bool						  passedOverUnreachable = false;
		bool						  waitable = true;
		bool						  gated = false;
		bool						  liveCountRead = false;
		double						  firstReadyAt = std::numeric_limits<double>::infinity();
		std::optional<uint64_t>		  keptConsidered;
		CandidateKind				  keptKind = CandidateKind::Thing;
		std::optional<QueryCandidate> waitingOn;
		PassOverReason				  waitingOnReason = PassOverReason::None;
		std::optional<QueryCandidate> bestClaimed;
		ecs::EntityID				  bestClaimedHolder = ecs::kInvalidEntity;
	};

	QueryResult QueryEvaluator::evaluate(const QueryRequest& request, const QueryWorld& world) {
		assert(request.query.valid() && request.memory != nullptr);
		Evaluation evaluation(*this, request, world);
		return evaluation.run();
	}

	QueryCount QueryEvaluator::count(const QueryRequest& request, const QueryWorld& world) {
		assert(request.query.valid() && request.memory != nullptr);
		Evaluation evaluation(*this, request, world);
		return evaluation.count();
	}

} // namespace queries
