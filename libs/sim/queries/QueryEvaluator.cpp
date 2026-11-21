#include "QueryEvaluator.h"

#include <ecs/LoosePileIndex.h>
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

		/// The smallest squared distance from `point` to the square of a memory cell.
		[[nodiscard]] float distanceSquaredToCell(glm::vec2 point, int32_t cellX, int32_t cellY) {
			const float minX = static_cast<float>(cellX) * ecs::kMemoryCellMeters;
			const float minY = static_cast<float>(cellY) * ecs::kMemoryCellMeters;
			const float gapX = std::max({minX - point.x, 0.0F, point.x - (minX + ecs::kMemoryCellMeters)});
			const float gapY = std::max({minY - point.y, 0.0F, point.y - (minY + ecs::kMemoryCellMeters)});
			return gapX * gapX + gapY * gapY;
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
		[[nodiscard]] std::optional<float> placeConstant(uint16_t fact, const assets::EntryLibrary& library) {
			if (fact == fact::kDistance || fact == fact::kOnShore || fact == fact::kWasteNearby || fact == fact::kFoodNearby) {
				return std::nullopt;
			}
			if (fact == fact::kIsPlace) {
				return 1.0F;
			}
			if (fact == fact::kSleepQuality) {
				return library.factTuning[fact];
			}
			return 0.0F;
		}

		enum class PassOver : uint8_t { None, Verdict, Packaged, Exhausted, Regrowing, Unreachable, Claimed };

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
				const uint32_t id = registry.getDefNameId(name);
				if (registry.hasCapability(id, capability)) {
					items.push_back(id);
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
			if (row.kind == assets::FactRowKind::Exclude && row.fact.fact == fact::kDistance && row.above.has_value()) {
				plan.distanceAbove = std::min(plan.distanceAbove, *row.above);
			}
			plan.usesColonyStock = plan.usesColonyStock || row.fact.fact == fact::kColonyStock;
		}

		// A place row whose every prefer row reads a fact that is the same at every place has one class
		// wherever the place is; when that is the lower class, the row is walked only if nothing above survives.
		bool					placesBelow = false;
		bool					placeClassKnown = true;
		std::optional<uint16_t> anyPrefer;
		for (const FactRowPlan& row : plan.rows) {
			if (row.kind != assets::FactRowKind::Prefer) {
				continue;
			}
			anyPrefer = row.fact;
			const std::optional<float> value = placeConstant(row.fact, library);
			if (!value.has_value()) {
				placeClassKnown = false;
			} else if (beyondLine(*value, row)) {
				placesBelow = true;
			}
		}
		placesBelow = anyPrefer.has_value() && placeClassKnown && placesBelow;

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
		const auto at =
			std::lower_bound(records.begin(), records.end(), std::pair{creature, query}, [](const Record& record, const auto& key) {
				return std::pair{record.creature, record.query} < key;
			});
		if (at != records.end() && at->creature == creature && at->query == query) {
			return *at;
		}
		return *records.insert(at, Record{.creature = creature, .query = query});
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
			  memory(*newRequest.memory),
			  record(newEvaluator.recordFor(newRequest.creature, newRequest.query.index)) {
			haveMesh = world.navigation != nullptr && world.navigation->hasMesh();
			hisLabel = haveMesh ? world.navigation->componentAt(request.position) : geometry::nav::kNoComponent;
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
				result.conditionRow = *plan.conditionRow;
				return result;
			}
			if (record.wait.has_value() && waitHolds(*record.wait)) {
				result.outcome = QueryOutcome::AllPassedOver;
				result.waitingOn = record.waitingOn;
				result.reserved = record.reserved;
				result.reservedBy = record.reservedBy;
				result.fromWait = true;
				return result;
			}

			walkPhase(false);
			if (evaluator.upperClass.empty() && evaluator.lowerClass.empty()) {
				walkPhase(true);
			}

			const std::vector<QueryCandidate>& chosen = !evaluator.upperClass.empty()	? evaluator.upperClass
														: !evaluator.lowerClass.empty() ? evaluator.lowerClass
																						: evaluator.fallbackClass;
			result.visits = visits;
			evaluator.ranked.clear();
			if (chosen.empty()) {
				result.outcome = !anyCandidate	 ? QueryOutcome::NothingKnown
								 : passedOverAny ? QueryOutcome::AllPassedOver
												 : QueryOutcome::AllExcluded;
			} else {
				result.outcome = QueryOutcome::Found;
				const std::size_t pick = entry.pick == assets::QueryPick::Weighted ? weightedPick(chosen) : 0;
				evaluator.ranked.push_back(chosen[pick]);
				for (std::size_t index = 0; index < chosen.size() && evaluator.ranked.size() < request.alternates + 1; ++index) {
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
				result.waitingOn = bestPassedOver;
			}

			record.wait.reset();
			if (result.outcome == QueryOutcome::AllPassedOver && waitable) {
				record.wait = stampNow();
				record.waitingOn = result.waitingOn;
				record.reserved = result.reserved;
				record.reservedBy = result.reservedBy;
			}
			return result;
		}

		std::optional<uint32_t> count() {
			if (plan.conditionRow.has_value()) {
				return std::nullopt;
			}
			counting = true;
			uint32_t survivors = countPhase(false);
			if (survivors == 0) {
				survivors = countPhase(true);
			}
			return survivors;
		}

	  private:
		// --- Weights and the definition cache ---

		void weighRows() {
			evaluator.rowWeights.assign(plan.rows.size(), 0.0F);
			uint64_t stamp = plan.usesColonyStock ? world.boxesStamp : 0;
			for (const std::string& trait : request.traits) {
				stamp = stamp * 1099511628211ULL ^ std::hash<std::string_view>{}(trait);
			}
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
				} else if (kFactSpecs[row.fact].scope == FactScope::Item && weight != 0.0F) {
					const FactSpec& spec = kFactSpecs[row.fact];
					itemBound += std::max(weight * spec.low, weight * spec.high);
				}
			}
			if (record.definitionStamp != stamp) {
				record.definitions.clear();
				record.definitionStamp = stamp;
			}
		}

		/// The definition-scoped fact `fact` of definition `definition`; a harvestable reads its yield's.
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
			const auto found = record.definitions.find(definition);
			if (found != record.definitions.end()) {
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
			return record.definitions.emplace(definition, terms).first->second;
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
			return !(water.distanceToWaterMm(geometry::quantize(point), withinMm) > withinMm);
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
				return candidate.kind == CandidateKind::Place ? 1.0F : 0.0F;
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
				if (candidate.kind != CandidateKind::Box || request.goal == nullptr) {
					return 0.0F;
				}
				for (const KnownBox& box : world.boxes) {
					if (box.box == candidate.entity && box.item == candidate.item) {
						return box.priority < request.goal->destinationPriority ? 1.0F : 0.0F;
					}
				}
				return 0.0F;
			}
			return 0.0F;
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
			const geometry::nav::ComponentLabel label = world.navigation->componentAt(target);
			if (label != geometry::nav::kNoComponent) {
				return label == hisLabel ? std::optional<glm::vec2>(target) : std::nullopt;
			}
			if (hisLabel != geometry::nav::kNoComponent && world.navigation->isOnMesh(target)) {
				const std::optional<glm::vec2> stand = world.navigation->standPointBeside(target, request.position, request.agentRadius);
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
			return reading.everyHolderTakes ||
				   (holder.taskType == ecs::TaskType::FulfillNeed && holder.tier < reading.idleTier && holder.tier <= reading.tier);
		}

		/// Reads the claims on `candidate`: whether it is held, what a pile or a box has left, and the holder that
		/// takes it from this creature when the step names the claim that fits it.
		[[nodiscard]] PassOver readClaims(QueryCandidate& candidate, std::optional<ecs::ClaimHolder>& holder) const {
			const ClaimReading&	   reading = request.claims;
			const ecs::ClaimIndex* claims = world.claims;
			candidate.left = reading.wanted;
			if (candidate.kind == CandidateKind::Place) {
				return PassOver::None;
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
					return PassOver::Claimed;
				}
				return PassOver::None;
			}
			const bool fromPile =
				candidate.kind == CandidateKind::Thing && plan.sources[sourceIndexOf(candidate)].source == assets::CandidateSource::Pile;
			if (candidate.kind == CandidateKind::Box || fromPile) {
				candidate.claimKind = assets::ClaimKind::SourceQuantity;
				candidate.claimTarget = candidate.kind == CandidateKind::Box
											? ecs::ClaimTarget{.kind = ecs::TargetKind::Entity, .key = candidate.entity}
											: ecs::ClaimTarget{.kind = ecs::TargetKind::WorldKey, .key = candidate.key};
				std::optional<uint32_t> live;
				if (candidate.kind == CandidateKind::Box) {
					for (const KnownBox& box : world.boxes) {
						if (box.box == candidate.entity && box.item == candidate.item) {
							live = box.quantity;
						}
					}
				} else if (world.piles != nullptr && world.world != nullptr) {
					const ecs::EntityID pile = world.piles->find(*world.world, candidate.item, candidate.position);
					if (pile != ecs::kInvalidEntity) {
						live = world.world->getComponent<ecs::ResourceStack>(pile)->quantity;
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
				const uint32_t have = live.value_or(taken.quantity > 0 ? 0U : reading.wanted);
				candidate.left = have > taken.quantity ? have - taken.quantity : 0U;
				const uint32_t unclaimed = live.has_value() ? (have > claimed ? have - claimed : 0U) : (claimed > 0 ? 0U : reading.wanted);
				candidate.held = unclaimed < std::min(reading.wanted, candidate.left) ? 1.0F : 0.0F;
				if (live.has_value() && *live == 0) {
					return PassOver::Exhausted;
				}
				if (candidate.left == 0 && namesClaim(reading.kinds, assets::ClaimKind::SourceQuantity)) {
					holder = taken.holder;
					return taken.holder.has_value() ? PassOver::Claimed : PassOver::Exhausted;
				}
				return PassOver::None;
			}
			candidate.claimKind = assets::ClaimKind::Use;
			candidate.claimTarget = candidate.kind == CandidateKind::Thing
										? ecs::ClaimTarget{.kind = ecs::TargetKind::WorldKey, .key = candidate.key}
										: ecs::ClaimTarget{.kind = ecs::TargetKind::Entity, .key = candidate.entity};
			if (claims == nullptr) {
				return PassOver::None;
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
				return PassOver::Claimed;
			}
			candidate.held = anyHolder.has_value() ? 1.0F : 0.0F;
			return PassOver::None;
		}

		[[nodiscard]] std::size_t sourceIndexOf(const QueryCandidate& candidate) const {
			for (std::size_t index = 0; index < plan.sources.size(); ++index) {
				if (plan.sources[index].row == candidate.sourceRow) {
					return index;
				}
			}
			return 0;
		}

		[[nodiscard]] PassOver passOver(QueryCandidate& candidate, std::optional<ecs::ClaimHolder>& holder) {
			const SourcePlan& source = plan.sources[sourceIndexOf(candidate)];
			if (candidate.kind == CandidateKind::Place) {
				if (source.source != assets::CandidateSource::PlaceHere && hasVerdict(0, candidate.position)) {
					return PassOver::Verdict;
				}
				candidate.goal = candidate.position;
				return readClaims(candidate, holder);
			}
			const uint64_t verdictKey = candidate.kind == CandidateKind::Thing || candidate.kind == CandidateKind::Box ? candidate.key : 0;
			if (hasVerdict(verdictKey, candidate.position)) {
				return PassOver::Verdict;
			}
			if (candidate.kind == CandidateKind::Box) {
				for (const KnownBox& box : world.boxes) {
					if (box.box == candidate.entity && box.item == candidate.item && box.packaged) {
						return PassOver::Packaged;
					}
				}
			}
			if (candidate.kind == CandidateKind::Thing && world.regrowth != nullptr && *world.regrowth) {
				const ecs::KnownWorldEntity* known = memory.getWorldEntity(candidate.key);
				if ((known->capabilityMask & capabilityBit(assets::CapabilityType::Harvestable)) != 0) {
					if (const std::optional<double> readyAt = (*world.regrowth)(candidate.defNameId, candidate.position, world.now)) {
						firstReadyAt = std::min(firstReadyAt, *readyAt);
						return PassOver::Regrowing;
					}
				}
			}
			const std::optional<glm::vec2> end = routeEnd(candidate.position);
			if (!end.has_value()) {
				return PassOver::Unreachable;
			}
			candidate.goal = *end;
			return readClaims(candidate, holder);
		}

		// --- Considering one candidate ---

		void keepIn(std::vector<QueryCandidate>& list, const QueryCandidate& candidate) const {
			const auto at = std::upper_bound(list.begin(), list.end(), candidate, ranksAhead);
			if (static_cast<std::size_t>(at - list.begin()) >= capacity) {
				return;
			}
			list.insert(at, candidate);
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

		/// Exclusions, pass-overs, the score, and the prefer class; a survivor joins its class's ranking.
		void consider(QueryCandidate candidate, const DefinitionTerms* terms) {
			anyCandidate = true;
			if (counting) {
				directSurvivors += survives(candidate, terms) ? 1U : 0U;
				return;
			}
			if (!candidate.fallback && excludedBy(candidate, false)) {
				excludedAny = true;
				return;
			}
			++visits;
			std::optional<ecs::ClaimHolder> holder;
			const PassOver					reason = passOver(candidate, holder);
			if (reason == PassOver::None && !candidate.fallback && excludedBy(candidate, true)) {
				excludedAny = true;
				return;
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

			if (reason != PassOver::None) {
				passedOverAny = true;
				waitable = waitable && candidate.kind != CandidateKind::Mover;
				if (!bestPassedOver.has_value() || ranksAhead(candidate, *bestPassedOver)) {
					bestPassedOver = candidate;
				}
				if (reason == PassOver::Claimed && (!bestClaimed.has_value() || ranksAhead(candidate, *bestClaimed))) {
					bestClaimed = candidate;
					bestClaimedHolder = holder.has_value() ? holder->colonist : ecs::kInvalidEntity;
				}
				return;
			}
			keepIn(candidate.fallback ? evaluator.fallbackClass : below ? evaluator.lowerClass : evaluator.upperClass, candidate);
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

		/// The remembered definitions the phase's memory rows name, one seed each with every row naming it.
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

		[[nodiscard]] QueryCandidate
		thingCandidate(const ecs::KnownWorldEntity& known, uint64_t key, std::size_t sourceIndex, bool fallbackPhase) const {
			const SourcePlan& source = plan.sources[sourceIndex];
			QueryCandidate	  candidate{
				   .kind = CandidateKind::Thing, .sourceRow = source.row, .fallback = fallbackPhase, .key = key, .defNameId = known.defNameId
			   };
			candidate.item = source.source == assets::CandidateSource::Pile		? known.defNameId
							 : source.source == assets::CandidateSource::Yields ? evaluator.registry.harvestYieldId(known.defNameId)
																				: 0;
			candidate.position = known.position;
			candidate.goal = known.position;
			return candidate;
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
				if (!fallbackPhase && seed.terms->excluded) {
					excludedAny = true;
					continue;
				}
				seed.bound = seed.terms->score + itemBound;
				maxBound = std::max(maxBound, seed.bound);
				for (uint32_t cell = 0; cell < remembered->cells.size(); ++cell) {
					const ecs::MemoryCell& memoryCell = remembered->cells[cell];
					cells.push_back(
						{.distanceSquared = distanceSquaredToCell(request.position, memoryCell.x, memoryCell.y),
						 .seed = index,
						 .cell = cell}
					);
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
			if (world.generator == nullptr) {
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
							const bool wanted = source.itemIsGoal ? request.goal != nullptr && box.item == request.goal->item
																  : std::ranges::binary_search(source.items, box.item);
							if (!wanted) {
								continue;
							}
							QueryCandidate candidate = directCandidate(CandidateKind::Box, source, fallbackPhase, box.position);
							candidate.key = box.key;
							candidate.item = box.item;
							candidate.defNameId = box.item;
							candidate.entity = box.box;
							considerDefined(candidate, fallbackPhase);
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
						if (!placesBelow && request.goal != nullptr) {
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

		void considerDefined(const QueryCandidate& candidate, bool fallbackPhase) {
			const DefinitionTerms& terms = termsOf(candidate.defNameId);
			if (!fallbackPhase && terms.excluded) {
				anyCandidate = true;
				excludedAny = true;
				return;
			}
			consider(candidate, &terms);
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
				if (remembered == nullptr) {
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

		[[nodiscard]] Wait stampNow() const {
			return Wait{
				.memoryVersion = memory.worldEntityVersion,
				.readyAt = firstReadyAt,
				.claimsFingerprint = world.claims != nullptr ? world.claims->fingerprint() : 0,
				.verdictGeneration = request.reach != nullptr ? request.reach->verdictsChanged() : 0,
				.boxesStamp = world.boxesStamp,
				.connectivityEpoch = haveMesh ? world.navigation->connectivityEpoch() : 0,
				.position = request.position,
			};
		}

		[[nodiscard]] bool waitHolds(const Wait& wait) const {
			const Wait now = stampNow();
			return now.memoryVersion == wait.memoryVersion && world.now < wait.readyAt && now.claimsFingerprint == wait.claimsFingerprint &&
				   now.verdictGeneration == wait.verdictGeneration && now.boxesStamp == wait.boxesStamp &&
				   now.connectivityEpoch == wait.connectivityEpoch && now.position == wait.position;
		}

		QueryEvaluator&				  evaluator;
		const QueryRequest&			  request;
		const QueryWorld&			  world;
		const assets::QueryEntry&	  entry;
		const Plan&					  plan;
		const ecs::Memory&			  memory;
		Record&						  record;
		bool						  haveMesh = false;
		geometry::nav::ComponentLabel hisLabel = geometry::nav::kNoComponent;
		float						  distanceWeight = 0.0F;
		float						  itemBound = 0.0F;
		std::size_t					  capacity = 1;
		bool						  itemPreferRows = false;
		bool						  counting = false;
		std::size_t					  directSurvivors = 0;
		uint32_t					  visits = 0;
		bool						  anyCandidate = false;
		bool						  excludedAny = false;
		bool						  passedOverAny = false;
		bool						  waitable = true;
		double						  firstReadyAt = std::numeric_limits<double>::infinity();
		std::optional<QueryCandidate> bestPassedOver;
		std::optional<QueryCandidate> bestClaimed;
		ecs::EntityID				  bestClaimedHolder = ecs::kInvalidEntity;
	};

	QueryResult QueryEvaluator::evaluate(const QueryRequest& request, const QueryWorld& world) {
		assert(request.query.valid() && request.memory != nullptr);
		Evaluation evaluation(*this, request, world);
		return evaluation.run();
	}

	std::optional<uint32_t> QueryEvaluator::count(const QueryRequest& request, const QueryWorld& world) {
		assert(request.query.valid() && request.memory != nullptr);
		Evaluation evaluation(*this, request, world);
		return evaluation.count();
	}

} // namespace queries
