#include "AIDecisionSystem.h"

#include "GoalSystemHelpers.h"

#include "../GoalTaskRegistry.h"
#include "../InventoryMass.h"
#include "../LoosePileIndex.h"
#include "../StoreOrDrop.h"
#include "../World.h"
#include "../components/Action.h"
#include "../components/AgentRadius.h"
#include "../components/Appearance.h"
#include "../components/DecisionTrace.h"
#include "../components/NavPath.h"
#include "../components/Inventory.h"
#include "../components/Memory.h"
#include "../components/MemoryQueries.h"
#include "../components/Movement.h"
#include "../components/Needs.h"
#include "../components/Packaged.h"
#include "../components/PlayerControlled.h"
#include "../components/ResourceStack.h"
#include "../components/Skills.h"
#include "../components/SourceReachMemo.h"
#include "../components/StorageConfiguration.h"
#include "../components/StructureBlueprint.h"
#include "../components/Task.h"
#include "../components/ToiletLocationFinder.h"
#include "../components/Transform.h"
#include "../components/WorkQueue.h"
#include "ActionSystem.h"
#include "ChainClaims.h"
#include "NavigationSystem.h"
#include "TimeSystem.h"
#include "WorkSources.h"

#include "assets/ActionTypeRegistry.h"
#include "assets/AssetDefinition.h"
#include "assets/AssetRegistry.h"
#include "assets/ItemProperties.h"
#include "assets/PriorityConfig.h"
#include "assets/RecipeDef.h"
#include "assets/RecipeRegistry.h"
#include "world/chunk/ChunkManager.h"

#include <utils/Log.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <numbers>
#include <span>

namespace ecs {

	namespace {

		/// The actionable needs that are critical now, one bit per NeedType (Task::criticalNeedsAtDecision).
		[[nodiscard]] uint8_t criticalNeedMask(const NeedsComponent& needs) {
			uint8_t mask = 0;
			for (const NeedType needType : NeedsComponent::kActionableNeeds) {
				if (needs.get(needType).isCritical()) {
					mask = static_cast<uint8_t>(mask | (1U << static_cast<unsigned>(needType)));
				}
			}
			return mask;
		}

		/// Map NeedType to the CapabilityType that fulfills it
		[[nodiscard]] assets::CapabilityType needToCapability(NeedType need) {
			switch (need) {
				case NeedType::Hunger:
					return assets::CapabilityType::Edible;
				case NeedType::Thirst:
					return assets::CapabilityType::Drinkable;
				case NeedType::Energy:
					return assets::CapabilityType::Sleepable;
				case NeedType::Bladder:
				case NeedType::Digestion:
					// Both bladder and digestion use Toilet capability
					return assets::CapabilityType::Toilet;
				case NeedType::Hygiene:
				case NeedType::Recreation:
				case NeedType::Temperature:
					break; // Non-actionable for now
				case NeedType::Count:
					break;
			}
			// All valid NeedTypes must be handled above - hitting this is a bug
			LOG_ERROR(Engine, "needToCapability: unhandled NeedType %d", static_cast<int>(need));
			return assets::CapabilityType::Edible;
		}

		/// Get the first action defName for a task type (for chain interruption checks)
		/// Maps TaskType (+ NeedType for FulfillNeed) to the action that will be triggered first.
		/// Returns string_view to avoid allocation - all values are compile-time constants.
		[[nodiscard]] std::string_view getFirstActionDefName(TaskType taskType, NeedType needType) {
			switch (taskType) {
				case TaskType::Haul:
				case TaskType::PlacePackaged:
					return "Pickup"; // Both start with picking something up
				case TaskType::Craft:
					return "Craft";
				case TaskType::Harvest:
					return "Harvest";
				case TaskType::Build:
					return "Build";
				case TaskType::Deconstruct:
					return "Deconstruct";
				case TaskType::FulfillNeed:
					switch (needType) {
						case NeedType::Hunger:
							return "Eat";
						case NeedType::Thirst:
							return "Drink";
						case NeedType::Energy:
							return "Sleep";
						case NeedType::Bladder:
						case NeedType::Digestion:
							return "Toilet";
						case NeedType::Hygiene:
						case NeedType::Recreation:
						case NeedType::Temperature:
						case NeedType::Count:
							return ""; // Non-actionable needs
					}
					return ""; // Unreachable but satisfies compiler
				case TaskType::Wander:
					return "Wander";
				case TaskType::None:
					return "";
			}
			return "";
		}

		/// Check if a task's first action requires free hands
		/// Uses ActionTypeRegistry for config-driven behavior per task-chains.md spec.
		[[nodiscard]] bool taskFirstActionNeedsHands(TaskType taskType, NeedType needType) {
			std::string_view actionDefName = getFirstActionDefName(taskType, needType);
			if (actionDefName.empty()) {
				// No first action or unknown task/need combination; assume it does not require hands.
				// Log a warning because this may indicate a missing case in getFirstActionDefName.
				LOG_WARNING(
					Engine,
					"taskFirstActionNeedsHands: empty first action for TaskType %d, NeedType %d; assuming no "
					"hands needed",
					static_cast<int>(taskType),
					static_cast<int>(needType)
				);
				return false;
			}
			return assets::ActionTypeRegistry::Get().actionNeedsHands(std::string(actionDefName));
		}

		/// Check if inventory contains any edible food item
		[[nodiscard]] bool hasEdibleFood(const assets::AssetRegistry& registry, const Inventory& inventory) {
			for (const auto& edibleItemName : registry.edibleItemNames()) {
				if (inventory.hasItem(edibleItemName)) {
					return true;
				}
			}
			return false;
		}

		/// The item a harvestable gives when it is food, by id; 0 for anything else. Not every harvestable
		/// is food (WoodyBush yields Stick, Reed yields PlantFiber).
		[[nodiscard]] uint32_t edibleYieldOf(const assets::AssetRegistry& registry, uint32_t defNameId) {
			return registry.yieldsEdible(defNameId) ? registry.harvestYieldId(defNameId) : 0U;
		}

		/// The key a route's verdict is held under in SourceReachMemo: a harvest's source by its Memory key,
		/// any other remembered source by its own, else the target point itself (a pickup, a deposit, a
		/// toilet spot, a work site), quantized like Memory keys so the same point picked again maps to the
		/// same verdict.
		[[nodiscard]] std::uint64_t verdictKey(std::uint64_t sourceKey, const glm::vec2& target) {
			return sourceKey != 0 ? sourceKey : hashWorldEntity(target, 0);
		}

		/// The verdict key of what a task is heading for.
		[[nodiscard]] std::uint64_t verdictKeyOf(const Task& task) {
			return verdictKey(task.type == TaskType::Harvest ? task.harvestTargetKey : task.needSourceKey, task.workTarget());
		}


		/// Skill names for work types
		constexpr const char* kSkillFarming = "Farming";
		constexpr const char* kSkillCrafting = "Crafting";
		constexpr const char* kSkillConstruction = "Construction";

		/// Skill bonus calculation constants (from priority-config.md)
		constexpr float	  kSkillBonusMultiplier = 10.0F;
		constexpr int16_t kSkillBonusMax = 100;

		/// A colonist's level in one skill and the score bonus it gives.
		struct SkillBonus {
			float	level = 0.0F;
			int16_t bonus = 0;

			[[nodiscard]] static SkillBonus of(const Skills* skills, const std::string& skillName) {
				if (skills == nullptr) {
					return {};
				}
				const float level = skills->getLevel(skillName);
				return {level, static_cast<int16_t>(std::min(level * kSkillBonusMultiplier, static_cast<float>(kSkillBonusMax)))};
			}
		};

		/// The bonus of each skill a work option scores with, looked up once per decision.
		struct WorkSkills {
			SkillBonus farming;
			SkillBonus crafting;
			SkillBonus construction;

			[[nodiscard]] static WorkSkills of(const Skills* skills) {
				static const std::string kFarming = kSkillFarming;
				static const std::string kCrafting = kSkillCrafting;
				static const std::string kConstruction = kSkillConstruction;
				return {SkillBonus::of(skills, kFarming), SkillBonus::of(skills, kCrafting), SkillBonus::of(skills, kConstruction)};
			}
		};

		void applySkill(EvaluatedOption& option, const SkillBonus& skill) {
			option.skillLevel = skill.level;
			option.skillBonus = skill.bonus;
		}

		/// Config name for a TaskType, used to look up its BASE tier in PriorityConfig's TaskTiers
		/// table. Must match the names in priority-tuning.xml's <TaskTiers> block.
		[[nodiscard]] const char* taskTypeConfigName(TaskType type) {
			switch (type) {
				case TaskType::FulfillNeed:
					return "FulfillNeed";
				case TaskType::Harvest:
					return "Harvest";
				case TaskType::Craft:
					return "Craft";
				case TaskType::Haul:
					return "Haul";
				case TaskType::PlacePackaged:
					return "PlacePackaged";
				case TaskType::Build:
					return "Build";
				case TaskType::Deconstruct:
					return "Deconstruct";
				case TaskType::Wander:
					return "Wander";
				case TaskType::None:
					return "None";
			}
			return "None";
		}

		/// PriorityConfig's base tier of every TaskType, by its config name.
		void readBaseTiers(AIDecisionSystem::BaseTiers& tiers) {
			const auto& config = assets::PriorityConfig::Get();
			for (size_t type = 0; type < tiers.size(); ++type) {
				tiers[type] = config.getTaskTier(taskTypeConfigName(static_cast<TaskType>(type)));
			}
		}

		/// Arbitration tier promotion thresholds. Named so the classifier rules mirror the spec
		/// table directly rather than hiding magic numbers.
		constexpr int	kTierCriticalNeed = 2; // a need past its critical threshold
		constexpr int	kTierActiveWorkOrder = 4; // committed/queued work + its provisioning chain
		constexpr int	kTierIdle = 7;			  // gathering food and Wander
		constexpr float kCriticalNeedThreshold = 10.0F; // needValue below this is critical

		/// Classify an option's arbitration tier: the config BASE tier for its task type, with a
		/// small set of named runtime promotions layered on (mirrors the spec's classification
		/// table exactly). Lower tier = higher priority.
		///
		///   FulfillNeed: needValue < 10%        -> tier 2 (critical need)
		///                gather food             -> tier 7 (idle)
		///                else (actionable)       -> config base (5)
		///   Haul/Harvest: servesActiveWorkOrder  -> tier 4 (work order, incl. mid-chain provisioning)
		///                 else                    -> config base (6, covers stocking + opportunistic)
		///   PlacePackaged: serving a work order OR already carrying (in-transit) -> tier 4
		///                  else                   -> config base (6)
		///   Craft/Build/Deconstruct               -> config base (4)
		///   Wander                                -> config base (7)
		[[nodiscard]] int classifyTier(const EvaluatedOption& option, const AIDecisionSystem::BaseTiers& baseTiers) {
			const int baseTier = baseTiers[static_cast<size_t>(option.taskType)];

			switch (option.taskType) {
				case TaskType::FulfillNeed: {
					// Gathering food for the pack is idle filler, tier 7.
					if (option.gathersFood) {
						return kTierIdle;
					}
					// A satisfied need needs no action; sort it to the idle tier so it can't outrank
					// real work in the trace (it's filtered from selection anyway by its status).
					if (option.status == OptionStatus::Satisfied) {
						return kTierIdle;
					}
					return option.needValue < kCriticalNeedThreshold ? kTierCriticalNeed : baseTier;
				}
				case TaskType::Haul:
				case TaskType::Harvest:
					return option.servesActiveWorkOrder ? kTierActiveWorkOrder : baseTier;
				case TaskType::PlacePackaged:
					// Serving a work order, or already carrying the item: either way it's committed
					// delivery -> tier 4, so a carrying colonist isn't rerouted mid-carry.
					return (option.servesActiveWorkOrder || option.carryingPackaged) ? kTierActiveWorkOrder : baseTier;
				case TaskType::Craft:
				case TaskType::Build:
				case TaskType::Deconstruct:
				case TaskType::Wander:
				case TaskType::None:
					return baseTier;
			}
			return baseTier;
		}

		/// The tier a need's option takes (classifyTier's FulfillNeed rule): critical, actionable, or idle
		/// while the need is met.
		[[nodiscard]] int needTier(const Need& need, const AIDecisionSystem::BaseTiers& baseTiers) {
			if (!need.needsAttention()) {
				return kTierIdle;
			}
			return need.value < kCriticalNeedThreshold ? kTierCriticalNeed : baseTiers[static_cast<size_t>(TaskType::FulfillNeed)];
		}

		/// Check if an option matches the current task (for the hysteresis margin)
		[[nodiscard]] bool isOptionCurrentTask(const EvaluatedOption& option, const Task& currentTask, uint32_t currentHaulItemId) {
			if (!currentTask.isActive() || option.status != OptionStatus::Available) {
				return false;
			}
			if (option.taskType != currentTask.type) {
				return false;
			}

			switch (option.taskType) {
				case TaskType::FulfillNeed:
					return option.needType == currentTask.needToFulfill;
				case TaskType::Craft:
					return option.craftRecipeDefName == currentTask.craftRecipeDefName &&
						   option.stationEntityId == currentTask.targetStationId;
				case TaskType::Haul:
					return option.haulItemDefNameId == currentHaulItemId &&
						   option.haulDestinationId == currentTask.haulDestinationId;
				case TaskType::PlacePackaged:
					return option.placePackagedEntityId == currentTask.placePackagedEntityId;
				case TaskType::Harvest:
					return option.harvestTargetKey == currentTask.harvestTargetKey &&
						   option.harvestGoalId == currentTask.harvestGoalId;
				case TaskType::Build:
				case TaskType::Deconstruct:
					return option.buildBlueprintEntityId == currentTask.buildBlueprintEntityId;
				default:
					return false;
			}
		}

		/// The in-progress margin of an option that matches the current task, else 0.
		[[nodiscard]] int16_t hysteresisBonusOf(const EvaluatedOption& option, const Task& currentTask, uint32_t currentHaulItemId) {
			return isOptionCurrentTask(option, currentTask, currentHaulItemId) ? assets::PriorityConfig::Get().getInProgressBonus() : int16_t{0};
		}

		/// Within a tier, where an option stands in a gathering trip (EvaluatedOption::gatheringRank).
		constexpr int8_t kRankDemotedCut = -1; // a fresh cut for a root and item a known pile also serves
		constexpr int8_t kRankFurtherCut = 1;  // a cut toward the root the colonist gathers for
		constexpr int8_t kRankLoosePile = 2;   // a pile toward the root the colonist gathers for
		constexpr int8_t kRankChosenDelivery = 3; // the carry-in the colonist has chosen to walk

		/// The cargo `task` hands to `selected` when it is the next leg of the same gathering: a cut, a
		/// pile pickup, or the carry-in, for the root and item the task carries goods toward.
		[[nodiscard]] std::optional<SuspendedWork> nextGatheringLeg(const EvaluatedOption& selected, const Task& task) {
			const auto& goals = GoalTaskRegistry::Get();
			if (selected.taskType == TaskType::Harvest) {
				// A clear brings nothing toward a need, so it is never a leg of a gathering.
				const GoalTask* goal = goals.getGoal(selected.harvestGoalId);
				if (goal == nullptr || goal->clearFootprint.has_value()) {
					return std::nullopt;
				}
				return handOverCargo(task, goals.rootOf(selected.harvestGoalId), selected.harvestYieldDefNameId);
			}
			const bool pileLeg = selected.haulSourceStorageId == 0 && selected.gatheringRank == kRankLoosePile;
			if (selected.taskType == TaskType::Haul && (selected.haulFromInventory || pileLeg)) {
				return handOverCargo(task, goals.rootOf(selected.haulGoalId), selected.haulItemDefNameId);
			}
			return std::nullopt;
		}

		/// Does the task hold goods of its chain: a haul or placement past its pickup, or a cut or pile
		/// pickup carrying what earlier legs of its gathering landed?
		[[nodiscard]] bool carriesChainGoods(const Task& task) {
			return task.chainStep > 0 || taskCargo(task) > 0;
		}

		/// Populate the (tier, score) arbitration key for an evaluated option.
		/// Sets option.tier (classifyTier) and the within-tier score breakdown (distanceFactor,
		/// taskAgeBonus, hysteresisBonus), then composes option.score. skillBonus is set by the
		/// evaluators. The chain bonus is GONE: a mid-chain provisioning task is tier 4 instead.
		/// @param world ECS world (to read a stocking haul's destination box priority)
		/// @param boxes This update's box cache, holding each (box, item)'s priority
		/// @param option The option to populate (skillBonus already set by its evaluator)
		/// @param currentTask Current colonist task (for the hysteresis margin)
		/// @param currentHaulItemId The current task's haul item, interned (0 when not hauling)
		/// @param currentTime Current game time (for task age calculation)
		/// @param baseTiers Each task type's base tier
		void populatePriorityBonuses(
			World& world, BoxRoomCache& boxes, const assets::AssetRegistry& registry, EvaluatedOption& option, const Task& currentTask,
			uint32_t currentHaulItemId, float currentTime, const AIDecisionSystem::BaseTiers& baseTiers
		) {
			const auto& priorityConfig = assets::PriorityConfig::Get();

			// Categorical tier first: it is compared before any score term.
			option.tier = classifyTier(option, baseTiers);

			// Wander is the idle floor; no nearest-source preference applies. It never sets
			// distanceToTarget, so the distance factor would otherwise compute to its MAX (~300) and
			// make wander outrank a same-tier gather-food option. Pin its whole within-tier score to 0
			// (no distance, skill, age, or hysteresis) so any reachable gather-food ranks above it.
			if (option.taskType == TaskType::Wander) {
				option.distanceFactor = 0.0F;
				option.skillBonus = 0;
				option.taskAgeBonus = 0;
				option.hysteresisBonus = 0;
				option.score = 0.0F;
				return;
			}

			// Within-tier distance factor: the dominant score term, decreasing with distance, so the
			// nearest reachable source wins within a tier.
			option.distanceFactor = priorityConfig.calculateDistanceFactor(option.distanceToTarget);

			// Hysteresis margin: stick the in-progress option so a same-tier challenger must beat it
			// by more than this to win (prevents thrashing). Only matters within a tier. Assigned, not
			// only set: an option the walk moves to another source loses it.
			option.hysteresisBonus = hysteresisBonusOf(option, currentTask, currentHaulItemId);

			// Storage-priority within-tier bias: a storage-stocking option (a loose haul, a pull from a
			// lower-priority box, a stocking carry-in, or a stocking cut) is ordered within tier 6 by its
			// DESTINATION box's priority for the item. Rank the dest box's StoragePriority (Low=0..Critical=3)
			// and weight it to dominate distance, so a Critical box farther away beats a Low box nearer, and
			// goods never go first to a box a higher one would pull them from. Tier is compared first, so
			// this never lets stocking preempt a tier-5 need or tier-4 work order. 0 for every non-stocking
			// option.
			const GoalTask* stockedBy = option.taskType == TaskType::Harvest && option.servesStorageStocking
											? GoalTaskRegistry::Get().getGoal(option.harvestGoalId)
											: nullptr;
			const uint64_t	destBox = stockedBy != nullptr ? static_cast<uint64_t>(stockedBy->destinationEntity) : option.haulDestinationId;
			const uint32_t	stockedItem = stockedBy != nullptr ? option.harvestYieldDefNameId : option.haulItemDefNameId;
			if (option.servesStorageStocking && destBox != 0) {
				if (const auto destPriority = boxes.priority(world, registry, static_cast<EntityID>(destBox), stockedItem)) {
					const auto rank = static_cast<int16_t>(static_cast<uint8_t>(*destPriority)); // 0..3
					option.storagePriorityBias = static_cast<int16_t>(rank * priorityConfig.getStoragePriorityWeight());
				}
			}

			// Task age bonus: old unclaimed goals rise within their tier.
			// For Haul tasks, look up the goal directly by ID
			if (option.taskType == TaskType::Haul && option.status == OptionStatus::Available && option.haulGoalId != 0) {
				const auto* goal = GoalTaskRegistry::Get().getGoal(option.haulGoalId);
				if (goal != nullptr) {
					float goalAge = currentTime - goal->createdAt;
					option.taskAgeBonus = priorityConfig.calculateTaskAgeBonus(goalAge);
				}
			}
			// For Harvest tasks, also apply task age bonus
			if (option.taskType == TaskType::Harvest && option.status == OptionStatus::Available && option.harvestGoalId != 0) {
				const auto* goal = GoalTaskRegistry::Get().getGoal(option.harvestGoalId);
				if (goal != nullptr) {
					float goalAge = currentTime - goal->createdAt;
					option.taskAgeBonus = priorityConfig.calculateTaskAgeBonus(goalAge);
				}
			}

			// Compose the within-tier score from the breakdown fields (kept for the UI display).
			option.score = option.computeScore();
		}

		/// populatePriorityBonuses with one deciding colonist's context.
		struct OptionPriority {
			World&							   world;
			BoxRoomCache&					   boxes;
			const assets::AssetRegistry&	   registry;
			const Task&						   currentTask;
			uint32_t						   currentHaulItemId = 0;
			float							   currentTime = 0.0F;
			const AIDecisionSystem::BaseTiers& baseTiers;

			void operator()(EvaluatedOption& option) const {
				populatePriorityBonuses(world, boxes, registry, option, currentTask, currentHaulItemId, currentTime, baseTiers);
			}
			/// The score `option` would have with a source `trip` metres away: the rank of a work group's
			/// candidate, set on the option before the candidate is one.
			[[nodiscard]] float rankOf(const EvaluatedOption& option, float trip) const {
				return EvaluatedOption::scoreOf(
					assets::PriorityConfig::Get().calculateDistanceFactor(trip), option.skillBonus, option.taskAgeBonus, hysteresisOf(option),
					option.storagePriorityBias
				);
			}
			[[nodiscard]] int16_t hysteresisOf(const EvaluatedOption& option) const {
				return hysteresisBonusOf(option, currentTask, currentHaulItemId);
			}
		};

		/// The trip a cut makes: to its source, then on with the yield to where the goal wants it. Scored by
		/// the whole trip, so the source nearest where the yield is needed wins, as a fetch's source does;
		/// otherwise a colonist chops the tree nearest itself even when one sits beside the station.
		[[nodiscard]] float cutTrip(const HarvestSource& source, const WorkGroup& group) {
			return source.distance + glm::distance(source.entity->position, group.destination);
		}

		/// The trip a fetch makes: `toSource` metres to the pile or box at `source`, then on with the goods to
		/// where the goal wants them.
		[[nodiscard]] float fetchTrip(float toSource, glm::vec2 source, const WorkGroup& group) {
			return toSource + glm::distance(source, group.destination);
		}

		/// Two equidistant sources of one goal resolve by (goal, source), not by memory's hash or view order.
		[[nodiscard]] uint64_t sourceTiebreak(const WorkGroup& group, uint64_t key) {
			return group.goalId ^ (key << 1);
		}

		/// A cut option's source fields, from one of its group's candidates.
		void fillCut(EvaluatedOption& option, const HarvestSource& source, const WorkGroup& group) {
			option.targetPosition = source.entity->position;
			option.targetDefNameId = source.entity->defNameId;
			option.distanceToTarget = cutTrip(source, group);
			option.targetReachable = false;
			option.harvestTargetKey = source.key; // the Memory key, not an ECS id
			option.harvestYieldDefNameId = source.yieldDefNameId;
			option.tiebreakId = sourceTiebreak(group, source.key);
		}

		/// The colonist whose Use claim holds the source: Use is exclusive, so two colonists never cut one plant.
		[[nodiscard]] EntityID holderOf(HarvestSource& source, const ClaimIndex& claims, ClaimExclusion self) {
			return source.holder.get([&] {
				const auto held = claims.useHolder({.kind = TargetKind::WorldKey, .key = source.key}, self);
				return held.has_value() ? held->colonist : kInvalidEntity;
			});
		}

		/// What a work group does with one of its candidates: offer it, pass it over because other colonists'
		/// claims take it whole (a Reserved row when it ranks ahead of every one offered), or pass it over.
		enum class Admission : uint8_t { Offered, Held, Passed };

		/// The group's candidate ranked next after the one at `from` (its place in `ranked`; group.end before
		/// the first), popped from the heap when it has not been yet. nullopt when none is left.
		[[nodiscard]] std::optional<uint32_t> nextRanked(WorkGroup& group, std::vector<RankedSource>& ranked, uint32_t from) {
			if (from == group.begin) {
				return std::nullopt;
			}
			if (from == group.unpopped) {
				std::pop_heap(ranked.begin() + group.begin, ranked.begin() + group.unpopped, RankedSource::ranksBelow);
				--group.unpopped;
			}
			return from - 1;
		}

		/// How much a root still wants, as seen by one deciding colonist: the root's remaining need net
		/// of every other task's in-flight claims (its own current task and suspended record excluded,
		/// so its own work never reads as taken). What it carries is on its way already, so it is
		/// netted out before the colonist goes to fetch or cut more.
		///
		/// It also answers what a source and a destination have left for this colonist once every other
		/// task's claims are taken out (task-claims.md, Availability). Each answer is one probe of the
		/// claim index plus the claims on that one target. The remaining need behind it is worked out
		/// once per update for every colonist (RemainingNeedCache); only the in-flight exclusion and the
		/// colonist's own goods are this decision's.
		struct RootSupply {
			World&						 world;
			RemainingNeedCache&			 needs;
			const ClaimIndex&			 claims;
			ClaimExclusion				 self;
			const Task&					 task;
			const Inventory&			 inventory;
			const assets::AssetRegistry& registry;
			const LoosePileIndex*		 piles;
			BoxRoomCache&				 boxRooms;

			[[nodiscard]] uint32_t available(uint64_t root, uint32_t item, NeedBasis basis) const {
				return rootAvailable(needs.get(world, root, item, basis), claims, root, item, self);
			}
			[[nodiscard]] uint32_t toAcquire(uint64_t root, uint32_t item, NeedBasis basis) const {
				const uint32_t need = available(root, item, basis);
				const uint32_t carried = need > 0 ? cargo(root, item) : 0U;
				return need > carried ? need - carried : 0U;
			}
			/// Is the colonist gathering for this root and item: does its task or suspended record carry
			/// goods toward it?
			/// Once it has chosen to carry the goods in, it is delivering them, not gathering.
			[[nodiscard]] bool gathersFor(uint64_t root, uint32_t item) const {
				return !delivering(root, item) && claimedCargo(task, root, item) > 0;
			}
			/// Has the colonist chosen to walk its goods of this item to this root?
			[[nodiscard]] bool delivering(uint64_t root, uint32_t item) const {
				return task.deliveryChosen && task.type == TaskType::Haul && GoalTaskRegistry::Get().rootOf(task.haulGoalId) == root &&
					   task.haulItemDefNameId == item;
			}
			/// What the colonist carries of the item that it would deliver to this root; its own food is not.
			[[nodiscard]] uint32_t cargo(uint64_t root, uint32_t item) const {
				const uint32_t held = ecs::availableQuantity(inventory, registry.getDefName(item));
				if (held == 0) {
					return 0;
				}
				const auto* def = registry.getDefinitionById(item);
				return cargoToward(task, root, item, held, def != nullptr && def->isEdible());
			}

			/// What a box's rules still ask of an item to stock it by cutting, whatever room it has: the named
			/// minimum less what it holds, net of every other task's in-flight and of what the colonist carries
			/// toward it.
			[[nodiscard]] uint32_t stockingAsk(const GoalTask& root, uint32_t item) const {
				const auto*		   config = world.getComponent<StorageConfiguration>(root.destinationEntity);
				const auto*		   store = world.getComponent<Inventory>(root.destinationEntity);
				const auto*		   def = registry.getDefinitionById(item);
				if (config == nullptr || store == nullptr || def == nullptr) {
					return 0;
				}
				const std::string& name = registry.getDefName(item);
				const uint32_t	   minimum = config->getNamedMinAmountFor(name, def->category);
				const uint32_t	   held = store->getQuantity(name);
				const uint32_t	   asked = rootAvailable(minimum > held ? minimum - held : 0U, claims, root.id, item, self);
				const uint32_t	   carried = asked > 0 ? cargo(root.id, item) : 0U;
				return asked > carried ? asked - carried : 0U;
			}

			/// What a remembered pile of the item holds for this colonist: what others will take of it, and
			/// its live count when the world can find it.
			[[nodiscard]] PileStock pileStock(uint64_t key, uint32_t item, glm::vec2 position) const {
				PileStock	   stock{.claimed = claims.sourceClaimed({.kind = TargetKind::WorldKey, .key = key}, item, self)};
				const EntityID pile = piles != nullptr ? piles->find(world, item, position) : kInvalidEntity;
				if (pile != kInvalidEntity) {
					stock.live = world.getComponent<ResourceStack>(pile)->quantity;
				}
				return stock;
			}

			/// Units of the item a storage box can still take, net of every other task's inbound: the room
			/// in its own stacks (existing ones, and the stacks its inbound already opens) plus the free
			/// slots no inbound item has been promised, in stacks of the item. Three hauls of three items
			/// can't all count on one last slot.
			[[nodiscard]] uint32_t storageRoom(EntityID box, uint32_t item) const {
				const auto room = boxRooms.get(world, registry, box, item);
				if (!room.has_value()) {
					return 0;
				}
				const auto stackRoom = [&](uint32_t id) { return boxRooms.get(world, registry, box, id)->stack; };
				const InboundStackRoom own = room->stack;
				const uint64_t		   stack = own.stackSize == 0 ? UINT32_MAX : own.stackSize;
				const uint64_t		   inbound = claims.inboundItems(box, item, self);
				const uint64_t		   ownSlots = inbound > own.partialRoom ? (inbound - own.partialRoom + stack - 1) / stack : 0;
				const uint64_t		   ownRoom = own.partialRoom + ownSlots * stack - inbound;
				const uint32_t		   promised = claims.inboundSlots(box, self, stackRoom);
				const uint64_t		   free = room->freeSlots > promised ? room->freeSlots - promised : 0U;
				return static_cast<uint32_t>(std::min<uint64_t>(ownRoom + free * stack, UINT32_MAX));
			}
		};

		/// What one pickup of an item would lift now (InventoryMass's pickupUnitsThatFit, the rule the
		/// Pickup and Withdraw effects size their take with), with what the colonist carries summed once,
		/// on the first item asked about in a decision. Zero means going to get the item gains nothing.
		class PickupRoom {
		  public:
			PickupRoom(const Inventory& inventory, const assets::AssetRegistry& registry)
				: inventory(inventory),
				  registry(registry) {}

			[[nodiscard]] uint32_t operator()(uint32_t item) { return ecs::pickupUnitsThatFit(inventory, registry, item, carried()); }

			/// What the colonist carries, kilograms (carriedCargoMassKg), summed on the first ask.
			[[nodiscard]] float carried() {
				if (!carriedKg.has_value()) {
					carriedKg = ecs::carriedCargoMassKg(inventory, registry);
				}
				return *carriedKg;
			}

		  private:
			const Inventory&			 inventory;
			const assets::AssetRegistry& registry;
			std::optional<float>		 carriedKg;
		};

		/// What the colonist knows of the way to a remembered pile: a route found no known way to it (a
		/// verdict, a lookup), or it is connected to him by meshed ground with none (a label check, kept on
		/// the pile for the decision).
		struct PileReach {
			const SourceReachMemo*		   memo = nullptr;
			std::function<bool(glm::vec2)> connected;

			[[nodiscard]] bool knownUnreachable(glm::vec2 at) const { return memo->hasVerdict(verdictKey(0, at)); }
			[[nodiscard]] bool connectedTo(CarrySource& pile) const {
				return pile.connected.get([&] { return connected(pile.entity->position); });
			}
		};

		/// What a work group's candidates need to be admitted and shown, for one deciding colonist: the
		/// evaluators that build the groups and the walk that advances them ask through it alike.
		struct WorkCandidates {
			std::vector<WorkGroup>&	   groups;
			std::vector<RankedSource>& ranked;
			WorkSources&			   sources;
			SharedWork&				   shared;
			const RootSupply&		   supply;
			const PileReach&		   reach;
			glm::vec2				   position{0.0F, 0.0F};

			/// A pile's stock, read the first time any group asks.
			[[nodiscard]] const PileStock& stockOf(CarrySource& pile, uint32_t item) const {
				return pile.stock.get([&] { return supply.pileStock(pile.key, item, pile.entity->position); });
			}

			/// Units of the item a box still gives above its own minimum: other pulls' claims count against
			/// it, so two pulls can't drain it below that together.
			[[nodiscard]] uint32_t pullLeft(const SharedWork::PullSource& source, uint32_t item) const {
				const uint32_t claimed =
					supply.claims.sourceClaimed({.kind = TargetKind::Entity, .key = static_cast<uint64_t>(source.box)}, item, supply.self);
				return source.drainable > claimed ? source.drainable - claimed : 0U;
			}

			[[nodiscard]] Admission admit(const WorkGroup& group, uint32_t at) const {
				const uint32_t index = ranked[at].index;
				switch (group.kind) {
					case WorkKind::Cut:
					case WorkKind::Clear:
						return holderOf(sources.harvest(index), supply.claims, supply.self) == kInvalidEntity ? Admission::Offered : Admission::Held;
					case WorkKind::Pile: {
						CarrySource&   pile = sources.carry(index);
						const uint32_t left = stockOf(pile, group.item).left(group.quantity);
						if (left == 0) {
							return Admission::Held;
						}
						// A pile toward a root he gathers for is a leg of that trip, taken up only with room for the
						// smaller of what it has left for him and what the root still wants (the top-up cut's rule),
						// and while no route found no known way to it.
						if (group.gathering &&
							(group.room < std::min(group.quantity, left) || reach.knownUnreachable(pile.entity->position))) {
							return Admission::Passed;
						}
						return Admission::Offered;
					}
					case WorkKind::Pull:
						return pullLeft(shared.pullSource(index), group.item) == 0 ? Admission::Held : Admission::Offered;
				}
				return Admission::Passed;
			}

			/// The group's next candidate offered after the one at `from` (group.end before the first); the
			/// first passed over because claims take it whole is kept in `firstHeld` when it is asked for and
			/// holds none yet. nullopt when none is left.
			[[nodiscard]] std::optional<uint32_t> nextOffered(WorkGroup& group, uint32_t from, std::optional<uint32_t>* firstHeld = nullptr) {
				for (std::optional<uint32_t> at = nextRanked(group, ranked, from); at.has_value(); at = nextRanked(group, ranked, *at)) {
					const Admission admission = admit(group, *at);
					if (admission == Admission::Offered) {
						return at;
					}
					if (admission == Admission::Held && firstHeld != nullptr && !firstHeld->has_value()) {
						*firstHeld = *at;
					}
				}
				return std::nullopt;
			}

			/// An option's source fields, from the candidate at `at`, sized to what it gives now.
			void fill(EvaluatedOption& option, const WorkGroup& group, uint32_t at) const {
				const uint32_t index = ranked[at].index;
				switch (group.kind) {
					case WorkKind::Cut:
					case WorkKind::Clear:
						fillCut(option, sources.harvest(index), group);
						return;
					case WorkKind::Pile: {
						CarrySource&	pile = sources.carry(index);
						const glm::vec2 pileAt = pile.entity->position;
						option.targetPosition = pileAt;
						option.targetDefNameId = pile.entity->defNameId;
						option.distanceToTarget = fetchTrip(pile.distance, pileAt, group);
						option.targetReachable = false;
						option.haulQuantity = std::min({group.quantity, stockOf(pile, group.item).left(group.quantity), group.room});
						option.haulSourcePosition = pileAt;
						option.haulSourceKey = pile.key;
						option.tiebreakId = sourceTiebreak(group, pile.key);
						return;
					}
					case WorkKind::Pull: {
						const SharedWork::PullSource& source = shared.pullSource(index);
						option.targetPosition = source.position;
						option.distanceToTarget = fetchTrip(glm::distance(position, source.position), source.position, group);
						option.targetReachable = false;
						option.haulQuantity = std::min({pullLeft(source, group.item), group.quantity, group.room});
						option.haulSourcePosition = source.position;
						option.haulSourceStorageId = static_cast<uint64_t>(source.box);
						option.tiebreakId = sourceTiebreak(group, static_cast<uint64_t>(source.box));
						return;
					}
				}
			}

			/// What a group shows: the best candidate offered, and the one taken whole that ranks ahead of it
			/// (the Reserved row), by their places in `ranked`.
			struct Shown {
				std::optional<uint32_t> offered;
				std::optional<uint32_t> held;
			};
			/// Close a group whose candidates were pushed from `group.begin` on: heap them, and show the best
			/// one offered on `option`.
			[[nodiscard]] Shown show(WorkGroup& group, EvaluatedOption& option) {
				group.end = static_cast<uint32_t>(ranked.size());
				group.unpopped = group.end;
				std::make_heap(ranked.begin() + group.begin, ranked.begin() + group.end, RankedSource::ranksBelow);
				Shown shown;
				shown.offered = nextOffered(group, group.end, &shown.held);
				if (shown.offered.has_value()) {
					group.first = *shown.offered;
					group.current = *shown.offered;
					fill(option, group, *shown.offered);
				}
				return shown;
			}
		};

		/// Evaluate haul options that go and get goods: loose piles and lower-priority boxes for a
		/// storage umbrella, and remembered loose stock for a craft's fetch child. Each is sized to
		/// what the root still wants (RootSupply::toAcquire) and what the colonist can lift now (the
		/// rule the pickup and withdraw take by). Goods already carried are offered by
		/// evaluateCarryInOptions. One option stands for each goal's piles of an item, and one for its
		/// pulls of an item, ranked as each source would rank as its own option.
		void evaluateHaulOptions(
			World*						 world,
			const assets::AssetRegistry& registry,
			ColonyStorageKnowledge&		 storageKnowledge,
			const RootSupply&			 supply,
			PickupRoom&					 pickupRoom,
			const Inventory&			 inventory,
			const OptionPriority&		 priority,
			WorkCandidates&				 candidates,
			std::vector<RootItem>&		 offeredPiles,
			DecisionTrace&				 trace
		) {
			auto&		 goalRegistry = GoalTaskRegistry::Get();
			WorkSources& sources = candidates.sources;

			// A haul whose pickup would lift nothing right now (pack full, hands busy with another load):
			// not available, so the colonist doesn't repeat a pickup that collects nothing. The trace
			// shows the item and why from haulItemDefNameId and the status, so the reason is a fixed string.
			const auto pushNoRoom = [&](const GoalTask& goal, uint32_t itemDefNameId, const char* reason) {
				EvaluatedOption option;
				option.taskType = TaskType::Haul;
				option.needType = NeedType::Count;
				option.needValue = 100.0F;
				option.threshold = 0.0F;
				option.haulItemDefNameId = itemDefNameId;
				option.haulGoalId = goal.id;
				option.status = OptionStatus::NoRoom;
				option.reason = reason;
				trace.options.push_back(std::move(option));
			};

			// Every remembered pile of the item toward `rootId`, as one option showing the best ranked, sized to
			// what it still has for this colonist and what he can lift now, so a fetch never claims more than
			// its source can give or than he takes off it. A pile toward a root the colonist gathers for is a
			// leg of that trip, taken up before another plant is cut (WorkCandidates::admit); selection tests
			// its reach as it tests any leg. Only a pile connected to him holds a fresh cut for the same root
			// and item back (offeredPiles), so a remembered pile across water leaves cutting where distance
			// puts it. Piles are asked in rank order only until one is.
			const auto pushPileHauls = [&](const GoalTask& goal, uint64_t rootId, uint32_t itemDefNameId, uint32_t quantity, bool craftFetch) {
				const uint32_t room = pickupRoom(itemDefNameId);
				if (room == 0) {
					return;
				}
				const std::span<CarrySource> piles = sources.carryablesOf(itemDefNameId);
				if (piles.empty()) {
					return;
				}
				const bool gathering = supply.gathersFor(rootId, itemDefNameId);

				EvaluatedOption haul;
				haul.taskType = TaskType::Haul;
				haul.needType = NeedType::Count;
				haul.needValue = 100.0F;
				haul.threshold = 0.0F;
				haul.haulItemDefNameId = itemDefNameId;
				haul.haulDestinationId = static_cast<uint64_t>(goal.destinationEntity);
				haul.haulTargetPosition = goal.destinationPosition;
				haul.haulGoalId = goal.id;
				// Fetching loose stock to provision a started craft is committed work (tier 4), so a far
				// pile can't strand a half-provisioned craft.
				haul.servesActiveWorkOrder = craftFetch;
				haul.servesStorageStocking = goal.owner == GoalOwner::StorageGoalSystem;
				haul.gatheringRank = gathering ? kRankLoosePile : int8_t{0};
				haul.status = OptionStatus::Available;
				haul.reason = goal.type == TaskType::Build ? "Fetching {} for a build site"
							  : craftFetch				   ? "Fetching {} for crafting"
														   : "Hauling {} to storage";
				priority(haul);

				WorkGroup group;
				group.kind = WorkKind::Pile;
				group.goalId = goal.id;
				group.item = itemDefNameId;
				group.destination = goal.destinationPosition;
				group.begin = static_cast<uint32_t>(candidates.ranked.size());
				group.quantity = quantity;
				group.room = room;
				group.gathering = gathering;
				for (CarrySource& pile : piles) {
					candidates.ranked.push_back(
						{.score = priority.rankOf(haul, fetchTrip(pile.distance, pile.entity->position, group)),
						 .tiebreakId = sourceTiebreak(group, pile.key),
						 .key = pile.key,
						 .index = sources.indexOf(pile)}
					);
				}
				const WorkCandidates::Shown shown = candidates.show(group, haul);
				if (shown.held.has_value()) {
					const uint64_t heldKey = candidates.ranked[*shown.held].key;
					const auto	   holder = supply.claims.sourceHolder({.kind = TargetKind::WorldKey, .key = heldKey}, itemDefNameId, supply.self);
					if (holder.has_value()) {
						EvaluatedOption reserved = haul;
						candidates.fill(reserved, group, *shown.held);
						reserved.haulQuantity = std::min(quantity, room);
						reserved.status = OptionStatus::Reserved;
						reserved.reservedBy = holder->colonist;
						trace.options.push_back(std::move(reserved));
					}
				}
				if (shown.offered.has_value()) {
					for (std::optional<uint32_t> at = shown.offered; at.has_value(); at = candidates.nextOffered(group, *at)) {
						CarrySource& pile = sources.carry(candidates.ranked[*at].index);
						if (!candidates.reach.knownUnreachable(pile.entity->position) && candidates.reach.connectedTo(pile)) {
							offeredPiles.push_back({.root = rootId, .item = itemDefNameId});
							break;
						}
					}
					trace.options.push_back(std::move(haul));
				}
				candidates.groups.push_back(group);
			};

			// The pull from strictly lower-priority boxes for one item a storage umbrella names, wanting
			// `destWant()` more of it (storage priority, Phase 2), as one option showing the best ranked box.
			// The strict-< gate is the load-bearing invariant: items flow monotonically up the 4-level ladder
			// (Low<Medium<High<Critical), never sideways or down, which makes A->B->A cycles impossible.
			const auto emitPulls = [&](const GoalTask& goal, const StorageConfiguration& destConfig, uint32_t acceptedId, const auto& destWant) {
				const auto& itemDefName = registry.getDefName(acceptedId);
				const auto* itemDef = registry.getDefinitionById(acceptedId);
				if (itemDef == nullptr) {
					return;
				}
				const StoragePriority destPriority = destConfig.getPriorityFor(itemDefName, itemDef->category);
				const uint32_t		  perTrip = ecs::cargoUnitsPerTrip(registry, acceptedId, inventory.carryCapacityKg);
				if (perTrip == 0) {
					return; // colonist can't carry a single unit of this item
				}
				const std::span<const SharedWork::PullSource> boxes =
					candidates.shared.pullSources(*world, registry, storageKnowledge, goal, acceptedId, *itemDef, destPriority);
				// No room left: every slot the box has is promised to goods already on their way.
				if (boxes.empty() || destWant() == 0) {
					return;
				}

				// The source is the box: walk to it, Withdraw, then the deposit leg stocks the dest.
				EvaluatedOption pull;
				pull.taskType = TaskType::Haul;
				pull.needType = NeedType::Count;
				pull.needValue = 100.0F;
				pull.threshold = 0.0F;
				pull.targetDefNameId = acceptedId;
				pull.haulItemDefNameId = acceptedId;
				pull.haulDestinationId = static_cast<uint64_t>(goal.destinationEntity);
				pull.haulTargetPosition = goal.destinationPosition;
				pull.haulGoalId = goal.id;
				// A pull tops up a box toward its config: opportunistic stocking (tier 6), ordered
				// within-tier by the dest box priority (populatePriorityBonuses).
				pull.servesStorageStocking = true;
				pull.status = OptionStatus::Available;
				pull.reason = "Pulling {} from lower-priority storage";
				priority(pull);

				WorkGroup group;
				group.kind = WorkKind::Pull;
				group.goalId = goal.id;
				group.item = acceptedId;
				group.destination = goal.destinationPosition;
				group.begin = static_cast<uint32_t>(candidates.ranked.size());
				group.quantity = destWant();
				for (const SharedWork::PullSource& box : boxes) {
					const auto key = static_cast<uint64_t>(box.box);
					candidates.ranked.push_back(
						{.score = priority.rankOf(pull, fetchTrip(glm::distance(candidates.position, box.position), box.position, group)),
						 .tiebreakId = sourceTiebreak(group, key),
						 .key = key,
						 .index = candidates.shared.indexOf(box)}
					);
				}
				// What a withdraw would lift now, asked once a box offers some: with none, one line says so for
				// every box. The room is the option's, so it is set before the best box sizes it.
				const WorkCandidates::Shown shown = candidates.show(group, pull);
				if (shown.offered.has_value()) {
					group.room = pickupRoom(acceptedId);
					if (group.room == 0) {
						pushNoRoom(goal, acceptedId, "Pulling from lower-priority storage ({}: no room to carry it)");
						return;
					}
					candidates.fill(pull, group, *shown.offered);
				}
				if (shown.held.has_value()) {
					const auto holder = supply.claims.sourceHolder(
						{.kind = TargetKind::Entity, .key = candidates.ranked[*shown.held].key}, acceptedId, supply.self
					);
					if (holder.has_value()) {
						EvaluatedOption reserved = pull;
						candidates.fill(reserved, group, *shown.held);
						reserved.haulQuantity = perTrip;
						reserved.status = OptionStatus::Reserved;
						reserved.reservedBy = holder->colonist;
						trace.options.push_back(std::move(reserved));
					}
				}
				if (shown.offered.has_value()) {
					trace.options.push_back(std::move(pull));
				}
				candidates.groups.push_back(group);
			};

			goalRegistry.forEachGoalOfType(TaskType::Haul, [&](const GoalTask& goal) {
				// A craft's fetch child: bring remembered loose stock to the station.
				if (goal.parentGoalId.has_value()) {
					const auto* craft = goalRegistry.getGoal(goal.parentGoalId.value());
					if (craft == nullptr || craft->type != TaskType::Craft || goal.status != GoalStatus::Available) {
						return;
					}
					for (auto acceptedIt = goal.acceptedDefNameIds.begin(); acceptedIt != goal.acceptedDefNameIds.end(); ++acceptedIt) {
						if (std::find(goal.acceptedDefNameIds.begin(), acceptedIt, *acceptedIt) != acceptedIt) {
							continue; // a repeated id names the same sources
						}
						const auto* itemDef = sources.knownCarryable(*acceptedIt);
						if (itemDef == nullptr || !itemDef->capabilities.carryable.has_value()) {
							continue;
						}
						const uint32_t need = supply.toAcquire(craft->id, *acceptedIt, NeedBasis::Stock);
						if (need == 0) {
							continue;
						}
						// Only fetch what the pickup can actually lift; one that adds 0 would be
						// re-issued every tick. The craft stays pending until the colonist has room.
						if (pickupRoom(*acceptedIt) == 0) {
							pushNoRoom(goal, *acceptedIt, "Fetching for crafting ({}: no room to carry it)");
							continue;
						}
						pushPileHauls(goal, craft->id, *acceptedIt, need, /*craftFetch=*/true);
					}
					return;
				}
				if (goal.owner != GoalOwner::StorageGoalSystem) {
					return;
				}
				const auto* destConfig = world->getComponent<StorageConfiguration>(goal.destinationEntity);
				if (destConfig == nullptr) {
					return;
				}

				// What the box still wants of an item and has room for, net of what is on its way. The want
				// is asked first; the room only once a source of the item is known, and then once, for its
				// loose piles and its pulls alike.
				struct DestWant {
					const RootSupply& supply;
					EntityID		  box;
					uint32_t		  item;
					uint32_t		  want;
					mutable std::optional<uint32_t> room;
					uint32_t operator()() const {
						if (!room.has_value()) {
							room = std::min(want, supply.storageRoom(box, item));
						}
						return *room;
					}
				};
				// Loose piles the colonist remembers of one accepted item. The need and carry gates come
				// first, so a full box or a full pack visits none of them.
				const auto emitLooseHauls = [&](uint32_t itemDefNameId, const DestWant& destWant) {
					const uint32_t want = destWant();
					if (want == 0) {
						return;
					}
					if (pickupRoom(itemDefNameId) == 0) {
						pushNoRoom(goal, itemDefNameId, "Hauling to storage ({}: no room to carry it)");
						return;
					}
					pushPileHauls(goal, goal.id, itemDefNameId, want, /*craftFetch=*/false);
				};

				// The items it names: their loose piles and their pulls from lower-priority boxes.
				const auto& named = goal.acceptedDefNameIds;
				for (auto acceptedIt = named.begin(); acceptedIt != named.end(); ++acceptedIt) {
					if (std::find(named.begin(), acceptedIt, *acceptedIt) != acceptedIt) {
						continue; // a repeated id names the same sources
					}
					if (!registry.hasCapability(*acceptedIt, assets::CapabilityType::Carryable)) {
						continue;
					}
					const DestWant destWant{supply, goal.destinationEntity, *acceptedIt, supply.toAcquire(goal.id, *acceptedIt, NeedBasis::Stock)};
					if (destWant.want == 0) {
						continue;
					}
					if (sources.knownCarryable(*acceptedIt) != nullptr) {
						emitLooseHauls(*acceptedIt, destWant);
					}
					emitPulls(goal, *destConfig, *acceptedIt, destWant);
				}
				// A category goal also takes loose piles of any other carryable of its category.
				if (goal.acceptedCategory != assets::ItemCategory::None) {
					for (uint32_t defNameId : sources.knownCarryableDefs()) {
						if (sources.knownCarryable(defNameId)->category != goal.acceptedCategory ||
							std::find(named.begin(), named.end(), defNameId) != named.end()) {
							continue;
						}
						const DestWant destWant{supply, goal.destinationEntity, defNameId, supply.toAcquire(goal.id, defNameId, NeedBasis::Stock)};
						if (destWant.want > 0) {
							emitLooseHauls(defNameId, destWant);
						}
					}
				}
			});

			// A construction site takes the known loose piles of a material it is due, the overflow of a
			// fell among them, so a pile is carried in before another plant is cut for the site.
			goalRegistry.forEachGoalOfType(TaskType::Build, [&](const GoalTask& site) {
				if (site.owner != GoalOwner::ConstructionGoalSystem) {
					return;
				}
				const auto& due = site.acceptedDefNameIds;
				for (auto dueIt = due.begin(); dueIt != due.end(); ++dueIt) {
					if (std::find(due.begin(), dueIt, *dueIt) != dueIt || sources.knownCarryable(*dueIt) == nullptr) {
						continue;
					}
					const uint32_t need = supply.toAcquire(site.id, *dueIt, NeedBasis::Stock);
					if (need == 0) {
						continue;
					}
					if (pickupRoom(*dueIt) == 0) {
						pushNoRoom(site, *dueIt, "Fetching {} for a build site (no room to carry it)");
						continue;
					}
					pushPileHauls(site, site.id, *dueIt, need, /*craftFetch=*/true);
				}
			});
		}

		/// Carry-in from possession (task-claims.md): for every root goal that still wants an item the
		/// colonist carries, offer to deliver it, sized to what the root still wants net of others'
		/// in-flight. One path for crafts, construction sites, and storage boxes alike, and for goods
		/// left over after a cut, an interruption, or a switch. Tools are equipment, and the colonist's
		/// food is its own provisions, offered to no root, unless its task or suspended record carries it
		/// toward that root (food it was sent to fetch or cut). Costs nothing unless the colonist carries
		/// cargo; what it carries is read once, into `carried`, reused storage.
		void evaluateCarryInOptions(const RootSupply& supply, std::vector<CarriedKind>& carried, const glm::vec2& position, DecisionTrace& trace) {
			const Inventory&			 inventory = supply.inventory;
			const assets::AssetRegistry& registry = supply.registry;
			carried.clear();
			const auto note = [&](const std::optional<ItemStack>& stack) {
				if (stack.has_value() && stack->quantity > 0) {
					carried.push_back({.item = registry.getDefNameId(stack->defName)});
				}
			};
			note(inventory.leftHand);
			note(inventory.rightHand);
			for (const ItemStack& stack : inventory.items) {
				if (stack.quantity > 0) {
					carried.push_back({.item = registry.getDefNameId(stack.defName)});
				}
			}
			std::sort(carried.begin(), carried.end(), [](const CarriedKind& a, const CarriedKind& b) { return a.item < b.item; });
			carried.erase(
				std::unique(carried.begin(), carried.end(), [](const CarriedKind& a, const CarriedKind& b) { return a.item == b.item; }), carried.end()
			);
			std::erase_if(carried, [&](CarriedKind& kind) {
				const auto* def = registry.getDefinitionById(kind.item);
				if (kind.item == 0 || def == nullptr || def->category == assets::ItemCategory::Tool) {
					return true;
				}
				kind.quantity = ecs::availableQuantity(inventory, registry.getDefName(kind.item));
				kind.edible = def->isEdible();
				return kind.quantity == 0;
			});
			if (carried.empty()) {
				return;
			}

			const auto offer = [&](const GoalTask& root) {
				const bool storage = root.owner == GoalOwner::StorageGoalSystem;
				for (const CarriedKind& kind : carried) {
					const uint32_t cargo = cargoToward(supply.task, root.id, kind.item, kind.quantity, kind.edible);
					if (cargo == 0) {
						continue;
					}
					uint32_t quantity = std::min(cargo, supply.available(root.id, kind.item, NeedBasis::Stock));
					if (storage && quantity > 0) {
						quantity = std::min(quantity, supply.storageRoom(root.destinationEntity, kind.item));
					}
					if (quantity == 0) {
						continue;
					}
					EvaluatedOption option;
					option.taskType = TaskType::Haul;
					option.needType = NeedType::Count;
					option.needValue = 100.0F;
					option.threshold = 0.0F;
					option.targetPosition = root.destinationPosition;
					option.targetDefNameId = kind.item;
					option.distanceToTarget = glm::distance(position, root.destinationPosition);
					option.haulItemDefNameId = kind.item;
					option.haulQuantity = quantity;
					option.haulSourcePosition = position; // already carrying
					option.haulDestinationId = static_cast<uint64_t>(root.destinationEntity);
					option.haulTargetPosition = root.destinationPosition;
					option.haulGoalId = root.id;
					option.haulFromInventory = true;
					if (supply.delivering(root.id, kind.item) && supply.task.haulDestinationId == static_cast<uint64_t>(root.destinationEntity)) {
						option.gatheringRank = kRankChosenDelivery;
					}
					// Craft and construction deliveries provision an active work order (tier 4); filling a
					// box is stocking, strictly below work orders (tier 6).
					option.servesActiveWorkOrder = !storage;
					option.servesStorageStocking = storage;
					option.tiebreakId = root.id ^ (static_cast<uint64_t>(kind.item) << 1);
					option.status = OptionStatus::Available;
					option.reason = root.type == TaskType::Build ? "Delivering {} to build site"
									: storage					   ? "Stocking {} into storage"
																   : "Delivering {} to crafting station";
					trace.options.push_back(std::move(option));
				}
			};
			const auto& goalRegistry = GoalTaskRegistry::Get();
			goalRegistry.forEachGoalOfType(TaskType::Craft, offer);
			goalRegistry.forEachGoalOfType(TaskType::Build, [&](const GoalTask& root) {
				if (root.owner == GoalOwner::ConstructionGoalSystem) {
					offer(root);
				}
			});
			goalRegistry.forEachGoalOfType(TaskType::Haul, [&](const GoalTask& root) {
				if (root.owner == GoalOwner::StorageGoalSystem && !root.parentGoalId.has_value()) {
					offer(root);
				}
			});
		}

		/// Evaluate harvest options by querying Harvest goals from GoalTaskRegistry.
		/// Goal-driven: a goal names the item it needs (yieldDefNameId), or for a construction clear the
		/// footprint whose harvestables it removes (clearFootprint); Memory provides the sources. A goal
		/// that names an item is offered while its root still wants that item (RootSupply::toAcquire).
		/// @param supply What each root still wants, for this colonist
		/// @param pickupRoom What a pickup of the yield would lift now
		/// @param skills The colonist's skill bonuses
		/// @param priority The colonist's arbitration key, which ranks a goal's sources
		/// @param candidates The colonist's remembered harvestables, by yield, and the work groups they rank into
		/// @param trace Output decision trace
		void evaluateHarvestOptions(
			const RootSupply&			 supply,
			PickupRoom&					 pickupRoom,
			const WorkSkills&			 skills,
			std::span<const RootItem>	 offeredPiles,
			const OptionPriority&		 priority,
			WorkCandidates&				 candidates,
			DecisionTrace&				 trace
		) {
			auto&		 goalRegistry = GoalTaskRegistry::Get();
			WorkSources& sources = candidates.sources;

			goalRegistry.forEachGoalOfType(TaskType::Harvest, [&](const GoalTask& harvestGoal) {
				const GoalTask* goal = &harvestGoal;
				// Skip goals that aren't workable yet.
				if (goal->status == GoalStatus::Blocked) {
					return;
				}

				// A clear goal is served by any harvestable inside its footprint, whatever it yields.
				// Every other Harvest goal names the item it needs (yieldDefNameId) and is served by a
				// harvestable that yields it, while its root wants more than is already on its way.
				const bool clearing = goal->clearFootprint.has_value();
				if (!clearing && goal->yieldDefNameId == 0) {
					return; // No yield specified
				}
				const GoalTask* root = goalRegistry.getGoal(goalRegistry.rootOf(goal->id));
				if (root == nullptr) {
					return;
				}
				// A cut that would land nothing in the colonist's hands or pack (a full armful of something
				// else, a full pack) brings nothing toward the root, and standing at the source it would win
				// again every decision. A clear is exempt: it removes its blocker and drops what doesn't fit.
				const uint32_t room = clearing ? 0U : pickupRoom(goal->yieldDefNameId);
				const bool	   roomToCarry = clearing || room > 0;
				// What it could cut, before asking what the root needs: most goals have no known source.
				// With no room to carry the yield no source is listed (WorkSources gates on the fit), so the
				// need is asked to show why the cut is not taken.
				const auto harvestables = clearing	  ? sources.clearableHarvestables(*goal->clearFootprint)
										  : roomToCarry ? sources.harvestablesYielding(goal->yieldDefNameId)
														: std::span<HarvestSource>{};
				if (roomToCarry && harvestables.empty()) {
					return;
				}
				const uint32_t want = clearing ? 0U : supply.toAcquire(root->id, goal->yieldDefNameId, harvestBasis(*root));
				if (!clearing && want == 0) {
					return;
				}
				// What the box has room for beside the goods the colonist already carries toward it, net of
				// what others bring, and what its rules still ask for whatever its room: a cut is taken only if
				// the box can take what it lands.
				uint32_t boxRoom = UINT32_MAX;
				uint32_t boxAsks = 0;
				if (!clearing && root->owner == GoalOwner::StorageGoalSystem) {
					const uint32_t spare = supply.storageRoom(root->destinationEntity, goal->yieldDefNameId);
					const uint32_t carried = supply.cargo(root->id, goal->yieldDefNameId);
					boxRoom = spare > carried ? spare - carried : 0U;
					if (boxRoom == 0) {
						return; // every slot it has left is promised to goods already on their way
					}
					boxAsks = supply.stockingAsk(*root, goal->yieldDefNameId);
				}
				const auto pushNoRoom = [&](const char* reason) {
					EvaluatedOption noRoom;
					noRoom.taskType = TaskType::Harvest;
					noRoom.needType = NeedType::Count;
					noRoom.needValue = 100.0F;
					noRoom.threshold = 0.0F;
					noRoom.harvestGoalId = goal->id;
					noRoom.harvestYieldDefNameId = goal->yieldDefNameId;
					noRoom.tiebreakId = goal->id;
					noRoom.status = OptionStatus::NoRoom;
					noRoom.reason = reason;
					trace.options.push_back(noRoom);
				};
				if (!roomToCarry) {
					pushNoRoom("Cutting for {1} (no room to carry it)");
					return;
				}

				// A Harvest that feeds a Craft or Build goal provisions an active work order: tier 4, so a
				// far harvestable can't strand a started craft or construction. One owned by
				// StorageGoalSystem is stocking: above Wander but strictly below work orders.
				const bool servesWorkOrder = root->type == TaskType::Craft || root->type == TaskType::Build;
				const bool servesStocking = goal->owner == GoalOwner::StorageGoalSystem;
				// A cut toward a root the colonist gathers for tops its trip up; one toward a root and item a
				// known pile also serves waits for the pile to be taken up first.
				const bool gathering = !clearing && supply.gathersFor(root->id, goal->yieldDefNameId);
				const bool pileServes = !clearing && std::any_of(offeredPiles.begin(), offeredPiles.end(), [&](const RootItem& pile) {
					return pile.root == root->id && pile.item == goal->yieldDefNameId;
				});
				const int8_t rank = gathering ? kRankFurtherCut : pileServes ? kRankDemotedCut : int8_t{0};

				// One option stands for the goal's sources (a clear's blockers inside its footprint, the same
				// test the build gate applies, or the harvestables that yield its item), ranked as each would
				// rank as its own option: everything the arbitration compares but the trip and the hysteresis
				// is the goal's, worked out once on the option itself.
				EvaluatedOption cut;
				cut.taskType = TaskType::Harvest;
				cut.needType = NeedType::Count;
				cut.needValue = 100.0F;
				cut.threshold = 0.0F;
				cut.harvestGoalId = goal->id;
				cut.harvestYieldDefNameId = goal->yieldDefNameId;
				cut.servesActiveWorkOrder = servesWorkOrder;
				cut.servesStorageStocking = servesStocking;
				cut.gatheringRank = rank;
				cut.status = OptionStatus::Available;
				applySkill(cut, skills.farming);
				cut.reason = clearing ? "Clearing {} from build site" : "Cutting {} for {}";
				priority(cut);

				WorkGroup group;
				group.kind = clearing ? WorkKind::Clear : WorkKind::Cut;
				group.goalId = goal->id;
				group.item = clearing ? 0U : goal->yieldDefNameId;
				group.destination = goal->destinationPosition;
				group.begin = static_cast<uint32_t>(candidates.ranked.size());

				// A top-up cut lands only what still fits: one is cut only while the room left takes the most
				// the source gives, or all the root still wants beyond what is carried, so no yield is lost to
				// a trip with too little room for it. A cut for a box, the first of a trip included, also needs
				// that much room in the box, or the surplus would only be carried about.
				bool tooLittleRoom = false;
				bool tooLittleBoxRoom = false;
				for (HarvestSource& source : harvestables) {
					const uint32_t fullCut = std::min(source.def->capabilities.harvestable->amountMax, want);
					if (gathering && room < fullCut) {
						tooLittleRoom = true;
						continue;
					}
					if (boxRoom < std::min(source.def->capabilities.harvestable->amountMax, boxAsks)) {
						tooLittleBoxRoom = true;
						continue;
					}
					cut.harvestTargetKey = source.key;
					candidates.ranked.push_back(
						{.score = priority.rankOf(cut, cutTrip(source, group)),
						 .tiebreakId = sourceTiebreak(group, source.key),
						 .key = source.key,
						 .index = sources.indexOf(source)}
					);
				}
				if (group.begin == candidates.ranked.size()) {
					if (tooLittleRoom) {
						pushNoRoom("Cutting for {1} (too little room left for another cut)");
					} else if (tooLittleBoxRoom) {
						pushNoRoom("Cutting for {1} (too little room in the box for another cut)");
					}
					return;
				}

				// A source another colonist is cutting is its alone: Use is exclusive, so two colonists never
				// cut one plant. Several may still clear one site, each its own plant. One held ahead of
				// every source offered is shown as the goal's Reserved row.
				const WorkCandidates::Shown shown = candidates.show(group, cut);
				if (shown.held.has_value()) {
					EvaluatedOption reserved = cut;
					candidates.fill(reserved, group, *shown.held);
					reserved.status = OptionStatus::Reserved;
					reserved.reservedBy = holderOf(sources.harvest(candidates.ranked[*shown.held].index), supply.claims, supply.self);
					trace.options.push_back(std::move(reserved));
				}
				if (shown.offered.has_value()) {
					trace.options.push_back(std::move(cut));
				}
				candidates.groups.push_back(group);
			});
		}

		/// Evaluate build options by querying Build goals from GoalTaskRegistry.
		/// Goal-driven: ConstructionSystem emits a Build goal once a blueprint's materials are
		/// staged. We surface it only while the blueprint is actually UnderConstruction (the
		/// phase ActionSystem's Build action requires), targeting the goal's work slot.
		void evaluateBuildOptions(std::span<const SharedWork::Site> sites, const glm::vec2& position, const WorkSkills& skills, DecisionTrace& trace) {
			for (const SharedWork::Site& site : sites) {
				EvaluatedOption buildOption;
				buildOption.taskType = TaskType::Build;
				buildOption.needType = NeedType::Count;
				buildOption.needValue = 100.0F;
				buildOption.threshold = 0.0F;
				buildOption.targetPosition = site.position;
				buildOption.distanceToTarget = glm::distance(position, site.position);
				buildOption.buildBlueprintEntityId = site.blueprint;
				buildOption.tiebreakId = site.goalId;
				// A blueprint reaches this option only when fully provisioned and UnderConstruction:
				// the build is committed work. Floor its priority above idle Wander so a far site's
				// distance penalty can't drop it below Wander and strand the ready foundation
				// (the abandonment bug: provision 18/18 wood, then wander off and never build).
				buildOption.servesActiveWorkOrder = true;
				buildOption.status = OptionStatus::Available;

				applySkill(buildOption, skills.construction);
				buildOption.reason = "Building structure";

				trace.options.push_back(std::move(buildOption));
			}
		}

		/// Evaluate deconstruct options by querying Deconstruct goals from GoalTaskRegistry.
		/// Mirror of evaluateBuildOptions: ConstructionSystem emits a Deconstruct goal for a
		/// demolishing structure once its dependents are cleared (the cascade gate). We surface it
		/// only while the blueprint still has work to undo (workDone > 0, the bound ActionSystem's
		/// Deconstruct action counts down). Deconstruct is Construction work, so it uses the same
		/// skill and priority tier as Build.
		void evaluateDeconstructOptions(
			std::span<const SharedWork::Site> sites, const glm::vec2& position, const WorkSkills& skills, DecisionTrace& trace
		) {
			for (const SharedWork::Site& site : sites) {
				EvaluatedOption deconstructOption;
				deconstructOption.taskType = TaskType::Deconstruct;
				deconstructOption.needType = NeedType::Count;
				deconstructOption.needValue = 100.0F;
				deconstructOption.threshold = 0.0F;
				deconstructOption.targetPosition = site.position;
				deconstructOption.distanceToTarget = glm::distance(position, site.position);
				deconstructOption.buildBlueprintEntityId = site.blueprint;
				deconstructOption.tiebreakId = site.goalId;
				// Committed construction work (mirrors Build): floor above Wander so a far site's
				// distance penalty can't strand an in-progress demolish.
				deconstructOption.servesActiveWorkOrder = true;
				deconstructOption.status = OptionStatus::Available;

				applySkill(deconstructOption, skills.construction);
				deconstructOption.reason = "Deconstructing structure";

				trace.options.push_back(std::move(deconstructOption));
			}
		}

		/// Evaluate place packaged options for furniture delivery
		/// @param packages The packaged items awaiting placement
		/// @param position Colonist position
		/// @param inventory Colonist inventory (to check if carrying)
		/// @param trace Output decision trace
		void evaluatePlacePackagedOptions(
			std::span<const SharedWork::Package> packages, const RootSupply& supply, const glm::vec2& position, const Inventory& inventory,
			DecisionTrace& trace
		) {
			for (const SharedWork::Package& package : packages) {
				const EntityID packagedEntity = package.entity;
				// Skip items being carried by a DIFFERENT colonist
				const bool isCarryingThis = inventory.carryingPackagedEntity == static_cast<uint64_t>(packagedEntity);
				if (package.beingCarried && !isCarryingThis) {
					continue;
				}

				const glm::vec2& targetPos = package.target;

				// Create place packaged option
				EvaluatedOption placeOption;
				placeOption.taskType = TaskType::PlacePackaged;
				placeOption.needType = NeedType::Count;
				placeOption.needValue = 100.0F;
				placeOption.threshold = 0.0F;

				if (isCarryingThis) {
					// Phase 2: Already carrying - go to placement target
					placeOption.targetPosition = targetPos;
					placeOption.distanceToTarget = glm::distance(position, targetPos);
					placeOption.carryingPackaged = true;
				} else {
					// Phase 1: Need to pick up - go to source
					placeOption.targetPosition = package.position;
					placeOption.distanceToTarget = glm::distance(position, package.position);
				}

				placeOption.placePackagedEntityId = static_cast<uint64_t>(packagedEntity);
				placeOption.placeSourcePosition = package.position;
				placeOption.placeTargetPosition = targetPos;
				placeOption.tiebreakId = static_cast<uint64_t>(packagedEntity);
				placeOption.status = OptionStatus::Available;
				placeOption.reason = isCarryingThis ? "Delivering {}" : "Placing {}";
				// Another colonist on its way to pick it up holds it (Use) until its pickup.
				if (!isCarryingThis) {
					if (const auto holder = supply.claims.useHolder(
							{.kind = TargetKind::Entity, .key = static_cast<uint64_t>(packagedEntity)}, supply.self
						)) {
						placeOption.status = OptionStatus::Reserved;
						placeOption.reservedBy = holder->colonist;
					}
				}
				trace.options.push_back(std::move(placeOption));
			}
		}

	} // namespace

	BoxRoomCache::Slot& BoxRoomCache::slotFor(EntityID box, uint32_t item) {
		if ((used + 1) * 2 > slots.size()) {
			std::vector<Slot> old;
			old.swap(slots);
			slots.assign(std::max<size_t>(64, old.size() * 2), Slot{});
			const size_t oldMask = slots.size() - 1;
			for (const Slot& slot : old) {
				if (slot.pass != pass) {
					continue;
				}
				size_t at = std::hash<uint64_t>{}((static_cast<uint64_t>(slot.box) * 0x9E3779B97F4A7C15ULL) ^ slot.item) & oldMask;
				while (slots[at].pass == pass) {
					at = (at + 1) & oldMask;
				}
				slots[at] = slot;
			}
		}
		const size_t mask = slots.size() - 1;
		size_t		 index = std::hash<uint64_t>{}((static_cast<uint64_t>(box) * 0x9E3779B97F4A7C15ULL) ^ item) & mask;
		for (; slots[index].pass == pass; index = (index + 1) & mask) {
			if (slots[index].box == box && slots[index].item == item) {
				return slots[index];
			}
		}
		Slot& slot = slots[index];
		slot = {.pass = pass, .box = box, .item = item};
		++used;
		return slot;
	}

	std::optional<BoxRoomCache::Room> BoxRoomCache::get(World& world, const assets::AssetRegistry& registry, EntityID box, uint32_t item) {
		Slot& slot = slotFor(box, item);
		if (!slot.roomRead) {
			read(world, registry, slot);
		}
		return slot.hasStore ? std::optional<Room>{slot.room} : std::nullopt;
	}

	std::optional<StoragePriority> BoxRoomCache::priority(World& world, const assets::AssetRegistry& registry, EntityID box, uint32_t item) {
		Slot& slot = slotFor(box, item);
		if (!slot.priorityRead) {
#ifdef DEVELOPMENT_BUILD
			++priorityReadOut;
#endif
			slot.priorityRead = true;
			const auto* config = world.getComponent<StorageConfiguration>(box);
			const auto* itemDef = registry.getDefinitionById(item);
			if (config != nullptr && itemDef != nullptr) {
				slot.priority = config->getPriorityFor(itemDef->defName, itemDef->category);
			}
		}
		return slot.priority;
	}

	void BoxRoomCache::refresh(World& world, const assets::AssetRegistry& registry, EntityID box) {
		for (Slot& slot : slots) {
			if (slot.pass == pass && slot.box == box && slot.roomRead) {
				read(world, registry, slot);
			}
		}
	}

	void BoxRoomCache::read(World& world, const assets::AssetRegistry& registry, Slot& slot) {
#ifdef DEVELOPMENT_BUILD
		++readOut;
#endif
		slot.roomRead = true;
		const auto* store = world.getComponent<Inventory>(slot.box);
		slot.hasStore = store != nullptr;
		if (store == nullptr) {
			return;
		}
		const uint32_t stackSize = Inventory::stackSizeOf(registry.getDefinitionById(slot.item));
		slot.room = {.stack = {.partialRoom = store->partialStackRoom(registry.getDefName(slot.item), stackSize), .stackSize = stackSize},
					 .freeSlots = store->freeSlots()};
	}

	void SharedWork::invalidate() {
		stationsBuilt = false;
		buildSitesBuilt = false;
		teardownSitesBuilt = false;
		packagesBuilt = false;
		pullEntries.clear();
		pullList.clear();
	}

	void SharedWork::boxChanged() {
		stationsBuilt = false;
		pullEntries.clear();
		pullList.clear();
	}

	const std::vector<SharedWork::Station>& SharedWork::stations(World& world, const assets::RecipeRegistry& recipes) {
		if (stationsBuilt) {
			return stationList;
		}
		stationsBuilt = true;
		stationList.clear();
		for (auto [stationEntity, stationPos, workQueue] : world.view<Position, WorkQueue>()) {
			if (!workQueue.hasPendingWork()) {
				continue;
			}
			const CraftingJob* nextJob = workQueue.getNextJob();
			if (nextJob == nullptr) {
				continue;
			}
			const auto* recipe = recipes.getRecipe(nextJob->recipeDefName);
			if (recipe == nullptr) {
				continue;
			}
			// Does the STATION's store already hold every input? Materials are hauled into the
			// station during provisioning (mirroring construction); the craft work is only
			// available once they're all physically present in the station, not in the colonist.
			const auto* stationStore = world.getComponent<Inventory>(stationEntity);
			bool		hasAllInputs = stationStore != nullptr;
			if (hasAllInputs) {
				for (const auto& input : recipe->inputs) {
					if (stationStore->getQuantity(input.defName) < input.count) {
						hasAllInputs = false;
						break;
					}
				}
			}
			stationList.push_back({.entity = stationEntity, .position = stationPos.value, .recipeDefName = nextJob->recipeDefName, .hasAllInputs = hasAllInputs});
		}
		return stationList;
	}

	const std::vector<SharedWork::Site>& SharedWork::buildSites(World& world) {
		if (buildSitesBuilt) {
			return buildList;
		}
		buildSitesBuilt = true;
		buildList.clear();
		// Surfaced only while the blueprint is UnderConstruction, the phase the Build action requires.
		GoalTaskRegistry::Get().forEachGoalOfType(TaskType::Build, [&](const GoalTask& goal) {
			if (goal.status != GoalStatus::Available) {
				return;
			}
			const auto* blueprint = world.getComponent<StructureBlueprint>(goal.destinationEntity);
			if (blueprint == nullptr || blueprint->phase != StructureBlueprint::BuildPhase::UnderConstruction) {
				return; // not ready to build (cleared/awaiting materials/complete)
			}
			buildList.push_back({.goalId = goal.id, .blueprint = goal.destinationEntity, .position = goal.destinationPosition});
		});
		return buildList;
	}

	const std::vector<SharedWork::Site>& SharedWork::teardownSites(World& world) {
		if (teardownSitesBuilt) {
			return teardownList;
		}
		teardownSitesBuilt = true;
		teardownList.clear();
		GoalTaskRegistry::Get().forEachGoalOfType(TaskType::Deconstruct, [&](const GoalTask& goal) {
			if (goal.status != GoalStatus::Available) {
				return;
			}
			const auto* blueprint = world.getComponent<StructureBlueprint>(goal.destinationEntity);
			if (blueprint == nullptr || blueprint->workDone <= 0.0F) {
				return; // nothing to tear down (already removed, or no work invested)
			}
			teardownList.push_back({.goalId = goal.id, .blueprint = goal.destinationEntity, .position = goal.destinationPosition});
		});
		return teardownList;
	}

	const std::vector<SharedWork::Package>& SharedWork::packages(World& world) {
		if (packagesBuilt) {
			return packageList;
		}
		packagesBuilt = true;
		packageList.clear();
		for ([[maybe_unused]] auto [entity, position, packaged, appearance] : world.view<Position, Packaged, Appearance>()) {
			if (packaged.targetPosition.has_value()) {
				packageList.push_back(
					{.entity = entity, .position = position.value, .target = *packaged.targetPosition, .beingCarried = packaged.beingCarried}
				);
			}
		}
		return packageList;
	}

	std::span<const SharedWork::PullSource> SharedWork::pullSources(
		World& world, const assets::AssetRegistry& registry, ColonyStorageKnowledge& storageKnowledge, const GoalTask& goal, uint32_t item,
		const assets::AssetDefinition& itemDef, StoragePriority destPriority
	) {
		for (const PullEntry& entry : pullEntries) {
			if (entry.goalId == goal.id && entry.item == item) {
				return std::span<const PullSource>(pullList).subspan(entry.begin, entry.end - entry.begin);
			}
		}
		const std::string&		   itemDefName = registry.getDefName(item);
		const assets::ItemCategory cat = itemDef.category;
		const auto				   begin = static_cast<uint32_t>(pullList.size());
		for (auto [srcEntity, srcConfig, srcInv, srcPos] : world.view<StorageConfiguration, Inventory, Position>()) {
			if (srcEntity == goal.destinationEntity || world.hasComponent<Packaged>(srcEntity)) {
				continue; // never pull from the destination itself, or from a box being moved
			}
			const uint32_t srcHas = srcInv.getQuantity(itemDefName);
			if (srcHas == 0) {
				continue; // source holds none of this item
			}
			// Strict-< gate: pull only from a STRICTLY lower-priority box. Never same, never
			// higher. getPriorityFor returns Low when the source box has no rule for this item, so
			// an unconfigured box holding it is a valid pull source for any Medium+ dest; Low is
			// the floor, so the gate still never forms a cycle.
			const StoragePriority srcPriority = srcConfig.getPriorityFor(itemDefName, cat);
			if (!(static_cast<uint8_t>(srcPriority) < static_cast<uint8_t>(destPriority))) {
				continue;
			}
			// Discovery-gating: the colony must KNOW the source box (no god-view), mirroring how
			// a loose pickup is gated on the pile being in memory.
			if (!storageKnowledge.knows(world, registry, srcEntity)) {
				continue;
			}
			// Don't drain the source below ITS OWN configured minimum for this item.
			const uint32_t srcMin = srcConfig.getMinAmountFor(itemDefName, cat);
			if (srcHas > srcMin) {
				pullList.push_back({.box = srcEntity, .position = srcPos.value, .drainable = srcHas - srcMin});
			}
		}
		pullEntries.push_back({.goalId = goal.id, .item = item, .begin = begin, .end = static_cast<uint32_t>(pullList.size())});
		return std::span<const PullSource>(pullList).subspan(begin);
	}

	std::vector<glm::vec2> joinRoute(std::vector<glm::vec2> waypoints, glm::vec2 at) {
		if (waypoints.size() < 2) {
			return waypoints;
		}
		constexpr float kJoinMarginMeters = 1.0F;
		constexpr float kOnLegMeters	  = 0.01F;
		const float		reach			  = glm::distance(waypoints.front(), at) + kJoinMarginMeters;
		std::size_t		leg				  = 0;
		glm::vec2		joinAt			  = waypoints.front();
		float			nearest			  = std::numeric_limits<float>::max();
		float			along			  = 0.0F;
		for (std::size_t i = 0; i + 1 < waypoints.size() && along <= reach; ++i) {
			const glm::vec2 d	 = waypoints[i + 1] - waypoints[i];
			const float		len2 = glm::dot(d, d);
			const float		t	 = len2 > 0.0F ? std::clamp(glm::dot(at - waypoints[i], d) / len2, 0.0F, 1.0F) : 0.0F;
			const glm::vec2 p	 = waypoints[i] + d * t;
			const float		off	 = glm::distance(at, p);
			if (off <= nearest) {
				nearest = off;
				leg		= i;
				joinAt	= p;
			}
			along += std::sqrt(len2);
		}
		// [at, joinAt, end of the leg, ...]: joinAt is dropped when he stands on it or it is the leg's end.
		waypoints.erase(waypoints.begin(), waypoints.begin() + static_cast<std::ptrdiff_t>(leg + 1));
		if (nearest > kOnLegMeters && glm::distance(joinAt, waypoints.front()) > kOnLegMeters) {
			waypoints.insert(waypoints.begin(), joinAt);
		}
		waypoints.insert(waypoints.begin(), at);
		return waypoints;
	}

	AIDecisionSystem::AIDecisionSystem(
		const assets::AssetRegistry&  registry,
		const assets::RecipeRegistry& recipeRegistry,
		uint32_t					  rngSeed
	)
		: m_registry(registry),
		  m_recipeRegistry(recipeRegistry),
		  m_rng(rngSeed) {}

	void AIDecisionSystem::update(float deltaTime) {
		// Advance this system's monotonic clock (used for lastEvaluationTime + within-tier task age).
		m_elapsedTime += deltaTime;
		++m_tick;
		// Only the regrowth callback reports a regrowing source, so without it no food wait is set and
		// the time goes unread; a world that wires it has a TimeSystem.
		m_simulationNow = onSourceRegrowth ? simulationSeconds(*world) : 0.0;
		if (const uint64_t generation = assets::PriorityConfig::Get().generation(); generation != m_baseTiersGeneration) {
			readBaseTiers(m_baseTiers);
			m_baseTiersGeneration = generation;
		}
		m_colonyStorage.invalidate();
		m_needs.invalidate();
		m_boxRooms.invalidate();
		m_sharedWork.invalidate();
		m_claimsBuilt = false;
		m_foodBoxesBuilt = false;
		auto* actionSystem = world->tryGetSystem<ActionSystem>();
		m_loosePiles = actionSystem != nullptr ? &actionSystem->loosePiles() : nullptr;

		// A suspended record holds goods the colonist may since have eaten, used, or dropped, or toward a
		// goal that is gone; true it up before this update's index is built from it. Only colonists
		// interrupted by a need hold one.
		for (auto [entity, task, inventory] : world->view<Task, Inventory>()) {
			(void)entity;
			refreshSuspended(m_registry, inventory, task);
		}

		// Process all entities with the required components
		for (auto [entity, position, needs, memory, task, movementTarget, inventory] :
			 world->view<Position, NeedsComponent, Memory, Task, MovementTarget, Inventory>()) {

			// Stranded-colonist recovery. A colonist standing off the walkable mesh -- it beelined
			// into a water hole before the async mesh finished building, spawned on a riverbank
			// edge, was teleported there, or terrain shifted under it -- can never plan a route
			// from where it stands, and freezes. Snap it to the nearest pathable point and clear
			// its task so it re-decides from solid ground.
			//
			// GATE: only recover a colonist that is genuinely STRANDED -- on meshed ground but off a
			// face a mover can cross (in water). A colonist whose ground isn't meshed is left exactly
			// where it stands: there is no mesh to judge it against. Its own anchor keeps its ground
			// loaded whatever the camera does, so that is only ever a moment before its chunk lands.
			// inSimArea && !isOnMesh is exactly "on meshed ground, off its walkable faces".
			//
			// A "fresh route" (a valid NavPath stamped to the current mesh generation) does NOT
			// prove on-mesh: a teleport can leave a current-version path under a colonist that is
			// itself off-mesh. So recovery keys ONLY on isOnMesh.
			if (m_navSystem != nullptr && m_navSystem->inSimArea(position.value)) {
				if (!m_navSystem->isOnMesh(position.value)) {
					// Snap to the nearest walkable point that ALSO clears the colonist's wall
					// collision band. The bare nearest-walkable point sits on the walkable-face
					// edge, inside the band, so WallCollisionSystem would shove the colonist back
					// off-mesh and the two would oscillate forever (the colonist-wall-trap bug).
					float recoverRadius = 0.3F;
					if (const auto* ar = world->getComponent<AgentRadius>(entity)) {
						recoverRadius = ar->radiusMeters;
					}
					const auto snapped = m_navSystem->nearestPathablePoint(position.value, recoverRadius);
					// Permanent stranded-recovery diagnostic (DEBUG, gated to the off-mesh branch):
					// records the snap decision for the colonist-freeze bug class. Keep on cleanup.
					LOG_DEBUG(Engine,
						"[NavDiag] reconcile %llu offMesh at (%.2f, %.2f): nearestFloor=%d (%.2f, %.2f)",
						static_cast<unsigned long long>(entity), position.value.x, position.value.y,
						snapped.has_value() ? 1 : 0, snapped.has_value() ? snapped->x : -999.0F,
						snapped.has_value() ? snapped->y : -999.0F);
					if (snapped) {
						position.value = *snapped;
						if (auto* np = world->getComponent<NavPath>(entity)) {
							np->valid = false;
						}
						m_claims.retire(task.instanceId); // later deciders this update see the work free
						dropRoute(task);
						// The action goes with its task.
						if (auto* stale = world->getComponent<Action>(entity)) {
							stale->clear();
						}
						task.clear();
					} else if (m_colonyOrigin.has_value()) {
						// Last-resort guarantee: the mesh has NO walkable face anywhere in range
						// (nearestPathablePoint found nothing -- a degenerate or all-blocked local
						// mesh), so there is nowhere nearer to snap. Teleport back to the colony
						// origin (the home clearing), which the spawn path guarantees is an open,
						// on-mesh walkable spot. Without this the colonist would be permanently stranded.
						LOG_DEBUG(Engine,
							"[NavDiag] reconcile %llu offMesh at (%.2f, %.2f): no nearestFloor; snapping to colony origin (%.2f, %.2f)",
							static_cast<unsigned long long>(entity), position.value.x, position.value.y,
							m_colonyOrigin->x, m_colonyOrigin->y);
						position.value = *m_colonyOrigin;
						if (auto* np = world->getComponent<NavPath>(entity)) {
							np->valid = false;
						}
						m_claims.retire(task.instanceId);
						dropRoute(task);
						if (auto* stale = world->getComponent<Action>(entity)) {
							stale->clear();
						}
						task.clear();
					}
				}
			}

			// Get optional Action component (may be nullptr if entity doesn't have one)
			auto* action = world->getComponent<Action>(entity);

			// His route's answer, once nav has it: applied without a decision. A ticket whose task ended
			// outside this system (an action finished or replaced it) is cancelled.
			if (task.routeTicket != 0) {
				if (task.state == TaskState::Moving) {
					takeRouteAnswer(entity, task, position, memory, movementTarget);
				} else {
					dropRoute(task);
				}
			}

			// A Moving task with no route and none asked for: a chain leg ActionSystem handed on
			// (Haul pickup->deposit, PlacePackaged pickup->place: same task, so the re-eval below keeps
			// it and never routes it, and with its target active and no route MovementSystem would steer
			// straight at the goal), or one the snap held, once it lets go or the mesh moves under him.
			// Asked here, before the requests below, so a route asked later this update is not asked twice.
			if (auto* navPath = world->getComponent<NavPath>(entity);
				task.state == TaskState::Moving && task.routeTicket == 0 && (navPath == nullptr || !navPath->valid) &&
				(task.navState == NavState::AwaitingRoute || movementTarget.active) &&
				(!task.awaitingSnap || m_navSystem == nullptr || !m_navSystem->snapHolds(entity) || awaitedGroundDue(task))) {
				requestRoute(entity, task, position, memory, movementTarget, false);
			}

			// Replan-on-discovery: this is the free-space-assumption loop. A colonist
			// plans over what it remembers, walks, and when vision reveals a wall that
			// cuts its corridor (Memory::beliefVersion moves) or the navmesh rebuilds
			// under it (NavigationSystem::generation() moves, or nav marks the route stale),
			// ask again for the SAME goal with current belief, and walk on meanwhile. Not a re-task --
			// the destination is unchanged. Runs before the shouldReEvaluate gate so it isn't skipped
			// between re-evals, and only when a version changed and no replacement is already out, so
			// it can't thrash: a change while one is out is caught when its route lands, stamped with
			// the versions it was asked under.
			if (auto* navPath = world->getComponent<NavPath>(entity);
				navPath != nullptr && navPath->valid && task.state == TaskState::Moving && task.routeTicket == 0) {
				const bool beliefMoved = memory.beliefVersion != navPath->builtBeliefVersion;
				const bool navMoved =
					(m_navSystem != nullptr) && (m_navSystem->generation() != navPath->builtNavVersion);
				if (beliefMoved || navMoved) {
					requestRoute(entity, task, position, memory, movementTarget, true);
				}
			}

			// Waiting for ground on its way (AwaitingMesh): nothing else asks again.
			if (task.state == TaskState::Moving && task.navState == NavState::AwaitingMesh && m_navSystem != nullptr && awaitedGroundDue(task)) {
				requestRoute(entity, task, position, memory, movementTarget, false);
			}

			// Standing, the panel says "Finding a way" once the wait is noticeable; walking his old route
			// while the replacement is that late, "Re-routing".
			if (task.state == TaskState::Moving && (task.navState == NavState::AwaitingRoute || task.routeTicket != 0) &&
				task.routeWaitTicks < std::numeric_limits<std::uint16_t>::max()) {
				++task.routeWaitTicks;
				if (task.navState == NavState::Traveling && task.routeWaitTicks >= Task::kFindingWayTicks) {
					setNavState(entity, task, NavState::Rerouting);
				}
			}

			// Direct player control: skip autonomous task selection entirely. The player drives this
			// colonist via issuePlayerMoveOrder, which sets a Moving task that the route blocks above
			// (answers, asking again, replan-on-discovery) carry exactly like an AI route;
			// the colonist otherwise stands and waits. Only buildDecisionTrace/selectTaskFromTrace are
			// suppressed -- off-mesh recovery and repath still run, and VisionSystem is independent, so
			// discovery keeps working.
			if (world->getComponent<PlayerControlled>(entity) != nullptr) {
				task.timeSinceEvaluation = 0.0F;
				continue;
			}

			// Check if we should re-evaluate (uses current timer value)
			if (!shouldReEvaluate(task, needs, movementTarget.active)) {
				// Only increment timer when NOT re-evaluating (timer tracks time since last eval)
				task.timeSinceEvaluation += deltaTime;
				continue;
			}
			// Held by the snap inside a placed obstacle he is connected to nothing, so a decision there could
			// only drop his task: each interval passes as a wait on the snap instead (a decision asked for
			// sooner waits for the interval). After kSnapWaitDecisions the decision runs, and a route that
			// waited on the snap fails, with no verdict on its target: his start was the problem.
			if (m_navSystem != nullptr && m_navSystem->snapHolds(entity)) {
				if (task.snapWaitDecisions < Task::kSnapWaitDecisions) {
					if (task.timeSinceEvaluation < kReEvalInterval) {
						task.timeSinceEvaluation += deltaTime;
						continue;
					}
					task.timeSinceEvaluation = 0.0F;
					if (++task.snapWaitDecisions < Task::kSnapWaitDecisions) {
						continue;
					}
				}
				if (task.awaitingSnap && task.routeTicket == 0) {
					failRoute(entity, task, memory, movementTarget, RouteFailure::SnapHeld);
				}
			} else {
				task.snapWaitDecisions = 0;
			}
			task.criticalNeedsAtDecision = criticalNeedMask(needs);
			const bool decidingOnFailedRoute = task.navState == NavState::CantFindWayTo;

			// Store current task state for the (tier, score) interruption comparison
			int		  currentTier = task.priorityTier;
			bool	  hasActiveAction = (action != nullptr && action->isActive());
			TaskState previousState = task.state;

			// Check if entity has the decision components for trace-based selection
			auto* trace = world->getComponent<DecisionTrace>(entity);
			auto* sourceReach = world->getComponent<SourceReachMemo>(entity);
			if (trace != nullptr && sourceReach != nullptr) {
				// Get optional Skills component for skill bonus calculation
				auto* skills = world->getComponent<Skills>(entity);

				ensureClaimIndex();

				// Build full decision trace (always, for UI updates)
				const bool wanderUnderway = task.type == TaskType::Wander && task.state == TaskState::Moving &&
											(movementTarget.active || task.navState == NavState::AwaitingRoute);
				buildDecisionTrace(entity, position, needs, memory, task, inventory, skills, *sourceReach, wanderUnderway, *trace);

				// Get the best option's (tier, score) key
				const auto* selected = trace->getSelected();
				int			newTier = (selected != nullptr) ? selected->tier : kTierIdle;
				float		newScore = (selected != nullptr) ? selected->score : 0.0F;

				// Check if the new task is actually different from current task
				bool isSameTask = false;
				if (selected != nullptr && task.isActive()) {
					// Compare task type
					bool sameType = (task.type == selected->taskType);

					// A wander that is walking stays the same task whatever point the re-eval rolled:
					// don't interrupt it just because the random target changed. One whose movement
					// is off is going nowhere (the no-option hold, a route nav denied or deferred), so
					// it MUST be free to take the fresh option, or it stays parked forever. One awaiting
					// its route is going somewhere.
					if (sameType && task.type == TaskType::Wander) {
						isSameTask = movementTarget.active || task.navState == NavState::AwaitingRoute;
					} else {
						bool sameTarget = false;
						if (selected->targetPosition.has_value()) {
							// Within 0.5 m is the same target; a target moved to a stand point is the same as the
							// target it stands beside. The world origin is a target like any other: a sleeper
							// lying there must keep its Sleep, which a switch would clear.
							const auto& selectedPos = selected->targetPosition.value();
							sameTarget = glm::distance(task.targetPosition, selectedPos) < 0.5F ||
										 glm::distance(task.workTarget(), selectedPos) < 0.5F;
						}
						// For PlacePackaged tasks, check entity ID instead of position
						// (position changes mid-task from source to target after phase 1)
						bool samePlaceTarget = true;
						if (selected->taskType == TaskType::PlacePackaged) {
							if (task.placePackagedEntityId != 0U && selected->placePackagedEntityId != 0U) {
								samePlaceTarget = (task.placePackagedEntityId == selected->placePackagedEntityId);
							} else {
								samePlaceTarget = false;
							}
							// For PlacePackaged, entity ID match is sufficient - skip position check
							if (samePlaceTarget) {
								sameTarget = true;
							}
						}

						// A task whose route failed is always a fresh pick. Its target holds a verdict, so it is
						// offered again only once the verdict is forgotten (SourceReachMemo::keep), and keeping
						// it then would leave him stopped with a stale failure and no new route request.
						const bool failed = task.navState == NavState::CantFindWayTo;
						// Needs met in place share the colonist's spot as their target.
						const bool sameNeed = task.type != TaskType::FulfillNeed || selected->needType == task.needToFulfill;
						isSameTask = sameType && sameTarget && samePlaceTarget && sameNeed && !failed;
					}
				}

				// Decision: Should we switch tasks?
				// Don't switch if it's the same task we're already doing
				bool shouldSwitch = !isSameTask;

				// The next leg of the same gathering: a further cut toward the root the colonist carries
				// goods for, or their carry-in. The goods ride along under the same claims, so nothing is
				// stowed or dropped and they are never uncommitted between two cuts.
				const std::optional<SuspendedWork> handedCargo =
					shouldSwitch && selected != nullptr ? nextGatheringLeg(*selected, task) : std::nullopt;

				// Two-hand-carry-must-finish-deposit invariant. A two-hand armful (e.g. Wood) rides in
				// BOTH hands and can't be stowed to belt/backpack, so ANY switch whose first action needs
				// the hands makes the chain-interruption below DROP the whole load on the ground. Protect
				// the in-flight delivery against any same-or-lower-priority challenger that would otherwise
				// drop it -- whether to fetch MORE of the very item it's already carrying (a withdraw/drop
				// loop that never provisions the destination) or to wander off idle with the load still in
				// hand. Hold the delivery so it completes; the colonist
				// re-evaluates with empty hands once the load is deposited. The armful is what is guarded,
				// whatever leg carries it: a haul, or a cut between two others of a gathering, which holds the
				// earlier cuts' goods under its cargo claims.
				//
				// Two escapes/limits keep this from ever pinning a colonist on a stuck haul:
				//   - It never fires when the delivery itself has no believed route to its destination
				//     (navState CantFindWayTo) -- holding an undeliverable load would strand it.
				//   - A strictly-higher-tier challenger (critical need, active work order) still preempts;
				//     dropping for an emergency is acceptable. Only same-or-lower-priority challengers
				//     (the opportunistic pull, idle Wander) are blocked.
				const bool currentHaulUnreachable = (task.navState == NavState::CantFindWayTo);
				const uint32_t chainGoods = chainItem(task);
				const bool	   carryingTwoHandLoad = chainGoods != 0 && (task.type == TaskType::Haul || taskCargo(task) > 0) &&
											 ecs::itemIsTwoHand(m_registry, chainGoods) && inventory.isHolding(m_registry.getDefName(chainGoods));
				// A root that no longer wants the goods (no carry-in of them to it is offered) is no delivery
				// to protect: the armful is let go like any other the colonist carries.
				const auto rootStillWants = [&] {
					return std::any_of(trace->options.begin(), trace->options.end(), [&](const EvaluatedOption& option) {
						return option.taskType == TaskType::Haul && option.haulFromInventory && option.haulItemDefNameId == chainGoods &&
							   claimedCargo(task, GoalTaskRegistry::Get().rootOf(option.haulGoalId), chainGoods) > 0;
					});
				};
				if (shouldSwitch && !handedCargo && !currentHaulUnreachable && carryingTwoHandLoad && selected != nullptr && rootStillWants()) {
					// Case 1: a Harvest of the same yield (e.g. a construction material Harvest for Wood). Held regardless of tier -- the chop->drop->chop loop it forms is the
					// original two-hand-drop bug this guard was built for.
					const bool sameYieldHarvest = selected->taskType == TaskType::Harvest && selected->harvestYieldDefNameId == chainGoods;
					// Case 2: ANY same-or-lower-priority challenger. A two-hand carry must finish its deposit
					// before taking on any task that does not outrank it -- idle Wander (tier 7) included. Two
					// ways this fires: (a) the storage-priority pull, where mid-carry evaluateHaulOptions'
					// pull-source scan re-emits a fresh withdraw option still pointing at the SOURCE box (it
					// still has stock) with a high score (distance 0, the colonist is standing on it), whose
					// targetPosition is the source rather than the in-flight deposit target, so the isSameTask
					// check above sees a different target and treats it as a switch -- dropping the un-stowable
					// armful to withdraw again, then dropping again, in a loop; (b) the source is drained to
					// its min so no pull is offered and the top option is Wander, which -- not being a Haul
					// -- the old taskType==Haul restriction let slip past, so the colonist walked off
					// carrying the load indefinitely. A strictly-higher-tier challenger (critical need,
					// active work order) still preempts; dropping for an emergency is acceptable.
					const bool sameOrLowerPriorityChallenger = selected->tier >= task.priorityTier;
					if (sameYieldHarvest || sameOrLowerPriorityChallenger) {
						shouldSwitch = false;
						task.timeSinceEvaluation = 0.0F; // we did evaluate; hold the delivery
					}
				}

				if (isSameTask) {
					task.timeSinceEvaluation = 0.0F; // Reset timer, we did evaluate
					// Keeping a delivery leg is choosing to deliver: a top-up cut no longer turns it back.
					task.deliveryChosen = task.deliveryChosen || (task.type == TaskType::Haul && task.chainStep == 1);
					// Update the (tier, score) key even when staying on the same task (the score can
					// change, e.g. PlacePackaged promotes to tier 4 when the colonist starts carrying).
					task.priorityTier = newTier;
					task.priority = newScore;
					// A meal is sized to the hunger at each decision: one who turns critical on the way takes
					// the larger meal, and the source share it claims follows.
					if (task.type == TaskType::FulfillNeed && task.needToFulfill == NeedType::Hunger &&
						task.needQuantity != selected->needQuantity) {
						task.needQuantity = selected->needQuantity;
						for (Claim& claim : task.claims) {
							if (claim.kind() == ClaimKind::SourceQuantity) {
								claim.setQuantity(task.needQuantity);
							}
						}
						m_claims.append(entity, task);
					}
				}

				// If action in progress, check if we can/should interrupt. Tier is inviolable; the
				// within-tier anti-thrash margin is already applied at SELECTION (the in-progress
				// option carries the hysteresis bonus), so re-applying a score gap here would
				// double-count it. The gate's remaining job is the tier rule and the
				// non-interruptable block.
				if (shouldSwitch && hasActiveAction && previousState == TaskState::Arrived) {
					if (!action->interruptable) {
						// Biological necessities (Eat, Drink, Toilet) cannot be interrupted
						shouldSwitch = false;
						task.timeSinceEvaluation = 0.0F; // Reset timer, we did evaluate
					} else if (newTier > currentTier) {
						// A lower-tier challenger never preempts in-progress work. (A higher tier
						// always interrupts; a same-tier winner already beat the hysteresis margin at
						// selection, so it is honored.)
						shouldSwitch = false;
						task.timeSinceEvaluation = 0.0F; // Reset timer, we did evaluate
					}
				}

				if (shouldSwitch) {
					// Handle chain interruption if the abandoned task carries goods and the new task needs hands
					if (!handedCargo && carriesChainGoods(task) && selected != nullptr) {
						handleChainInterruption(entity, task, inventory, position, *selected);
					}
					if (selected != nullptr) {
						setDownUnplacedFurniture(entity, inventory, position, *selected);
					}

					// The abandoned task's claims leave the index now, unless it just became the
					// suspended record or hands its cargo on, so a colonist deciding later this update
					// sees them free.
					if (!handedCargo && (!task.suspended || task.suspended->instanceId != task.instanceId)) {
						m_claims.retire(task.instanceId);
					}

					// The action goes with its task: ActionSystem starts one only when none is active, so a
					// woken sleeper's Sleep would otherwise run on at the new target and its completion would
					// clear the new task undone. Only an interruptable action, or a stale one whose task
					// already ended, gets past the gate above.
					if (action != nullptr) {
						action->clear();
					}
					dropRoute(task);
					task.clear();
					task.timeSinceEvaluation = 0.0F;
					selectTaskFromTrace(entity, task, movementTarget, *trace, position, memory);
					task.priorityTier = newTier; // Store the (tier, score) key for future comparisons
					task.priority = newScore;
					acceptClaims(entity, task, memory, inventory, handedCargo);
					task.deliveryChosen = task.type == TaskType::Haul && task.chainStep == 1;
					// Both idle outcomes, a wander and gathering food, let go of an armful nobody wants.
					if (task.type == TaskType::Wander || (selected != nullptr && selected->gathersFood)) {
						putDownUnwantedArmful(inventory, position, *trace);
					}

					LOG_INFO(
						Engine,
						"[AI] Entity %llu: %s (tier %d, score %.0f) → (%.1f, %.1f)",
						static_cast<unsigned long long>(entity),
						task.reason.c_str(),
						task.priorityTier,
						task.priority,
						task.targetPosition.x,
						task.targetPosition.y
					);
				}
			}
			task.routeFailureDecided = task.routeFailureDecided || decidingOnFailedRoute;
		}

#ifdef DEVELOPMENT_BUILD
		if (m_claimsBuilt) {
			if (const auto violation = m_claims.findViolation(*world, m_claimsCheck)) {
				LOG_ERROR(Engine, "[AI] Claim index violation: %s", violation->c_str());
				std::abort();
			}
		}
#endif
	}

	void AIDecisionSystem::ensureClaimIndex() {
		if (!m_claimsBuilt) {
			m_claims.rebuild(*world);
			m_claimsBuilt = true;
		}
	}

	const std::vector<AIDecisionSystem::FoodBox>& AIDecisionSystem::foodBoxes() {
		if (m_foodBoxesBuilt) {
			return m_foodBoxes;
		}
		m_foodBoxesBuilt = true;
		m_foodBoxes.clear();
		const std::vector<uint32_t>& edibleItems = m_registry.edibleItemIds();
		for (auto [box, config, store, boxPos] : world->view<StorageConfiguration, Inventory, Position>()) {
			(void)config;
			if (world->hasComponent<Packaged>(box) || store.items.empty()) {
				continue;
			}
			const size_t first = m_foodBoxes.size();
			for (const ItemStack& stack : store.items) {
				const uint32_t item = m_registry.getDefNameId(stack.defName);
				if (!std::binary_search(edibleItems.begin(), edibleItems.end(), item)) {
					continue;
				}
				const auto same = std::find_if(m_foodBoxes.begin() + static_cast<std::ptrdiff_t>(first), m_foodBoxes.end(),
											   [item](const FoodBox& food) { return food.item == item; });
				if (same != m_foodBoxes.end()) {
					same->quantity += stack.quantity;
				} else {
					m_foodBoxes.push_back({.box = box, .position = boxPos.value, .item = item, .quantity = stack.quantity});
				}
			}
			if (m_foodBoxes.size() == first) {
				continue;
			}
			// Colony-placed storage is common colony knowledge, the rule the box-to-box pull uses. A box with
			// no appearance has no world key, so no colonist can know it.
			const auto* appearance = world->getComponent<Appearance>(box);
			if (appearance == nullptr || !m_colonyStorage.knows(*world, m_registry, box)) {
				m_foodBoxes.resize(first);
				continue;
			}
			const uint64_t key = hashWorldEntity(boxPos.value, m_registry.getDefNameId(appearance->defName));
			for (size_t index = first; index < m_foodBoxes.size(); ++index) {
				m_foodBoxes[index].key = key;
			}
		}
		return m_foodBoxes;
	}

	void AIDecisionSystem::acceptClaims(
		EntityID entity, Task& task, const Memory& memory, const Inventory& inventory, const std::optional<SuspendedWork>& handedCargo
	) {
		if (handedCargo) {
			takeOverCargo(task, *handedCargo);
		} else {
			const uint64_t seq = GoalTaskRegistry::Get().allocateClaimSeq();
			task.instanceId = seq;
			task.claimSeq = seq;
		}

		// The record's claims are already indexed under its own instance id, so adopting or discarding
		// it goes through the index rather than appending it again.
		if (task.suspended && task.type != TaskType::FulfillNeed) {
			const uint64_t recordId = task.suspended->instanceId;
			if (!handedCargo && adoptsSuspended(task)) {
				takeOverCargo(task, *task.suspended);
			} else {
				m_claims.retire(recordId);
			}
			task.discardSuspended();
		}
		// A record kept by a need is indexed again: it holds nothing while the colonist sleeps, and holds
		// again once a need other than sleep keeps it.
		if (task.suspended) {
			m_claims.suspend(entity, task);
		}
		recordChainClaims(*world, m_needs, m_claims, m_registry, memory, inventory, task);
		m_claims.append(entity, task);
	}

	bool AIDecisionSystem::shouldReEvaluate(const Task& task, const NeedsComponent& needs, bool movementTargetActive) {
		// Always re-evaluate if no active task
		if (!task.isActive()) {
			return true;
		}

		// Re-evaluate if task has arrived (completed movement)
		// Note: We still re-evaluate even with action in progress to update DecisionTrace for UI
		// The actual task switch decision is made AFTER re-evaluation based on priority gap
		if (task.state == TaskState::Arrived) {
			return true;
		}

		// Nav has stopped the colonist: no believed route to its target (a wander point or work
		// site across water, or a route the freshly-built mesh invalidated). Decide once at once so it
		// picks a reachable target instead of standing frozen. Until a route is found again, every later
		// failure (the picks that decision and the next make) waits for the interval below.
		if (task.navState == NavState::CantFindWayTo && !task.routeFailureDecided) {
			return true;
		}

		// Re-evaluate periodically
		if (task.timeSinceEvaluation >= kReEvalInterval) {
			return true;
		}

		// A need crossing into critical decides at once (tier 2 interrupts all lower tiers); one that
		// stays critical waits for the interval, so a colonist with nothing reachable doesn't decide
		// every frame. Already serving a critical need, he isn't interrupted for another.
		const uint8_t newlyCritical = static_cast<uint8_t>(criticalNeedMask(needs) & ~task.criticalNeedsAtDecision);
		if (newlyCritical != 0) {
			const bool servingCriticalNeed =
				task.type == TaskType::FulfillNeed && needs.get(task.needToFulfill).isCritical();
			if (!servingCriticalNeed) {
				return true;
			}
		}

		// Idle Wander with a LIVE target is freely preemptible: re-evaluate every tick so any higher-tier
		// task is taken the instant it appears, with no commitment lag. isSameTask keeps the wander target
		// from thrashing (nothing better -> stays on the same leg), so discovery is preserved; this trades
		// a per-tick decision-trace rebuild for that responsiveness.
		// Real work (tiers 1-6) keeps its periodic throttle above. Gate on an ACTIVE target so the
		// no-option HOLD (selectTaskFromTrace parks a Wander with movement OFF when nothing is reachable)
		// stays on the periodic throttle instead of rebuilding the trace every frame while standing still.
		if (task.type == TaskType::Wander && task.state == TaskState::Moving) {
			return movementTargetActive;
		}

		return false;
	}

	glm::vec2 AIDecisionSystem::generateWanderTarget(const glm::vec2& currentPos) {
		// Generate random angle and distance
		std::uniform_real_distribution<float> angleDist(0.0F, 2.0F * std::numbers::pi_v<float>);
		std::uniform_real_distribution<float> distDist(kWanderRadius * 0.3F, kWanderRadius);

		float angle = angleDist(m_rng);
		float distance = distDist(m_rng);

		return currentPos + glm::vec2{std::cos(angle) * distance, std::sin(angle) * distance};
	}

	void AIDecisionSystem::buildDecisionTrace(
		EntityID			  entity,
		const Position&		  position,
		const NeedsComponent& needs,
		const Memory&		  memory,
		const Task&			  currentTask,
		const Inventory&	  inventory,
		const Skills*		  skills,
		SourceReachMemo&	  sourceReach,
		bool				  wanderUnderway,
		DecisionTrace&		  trace
	) {
		trace.clear();
		const WorkSkills workSkills = WorkSkills::of(skills);
		PickupRoom		 pickupRoom(inventory, m_registry);

		// Agent footprint for the stand points below (need sources and the option filter).
		float agentRadius = 0.3F;
		if (const auto* ar = world->getComponent<AgentRadius>(entity)) {
			agentRadius = ar->radiusMeters;
		}
		const bool haveMesh = (m_navSystem != nullptr) && m_navSystem->hasMesh();

		// What he can get to, with no search in the decision. His component label, found once, against each
		// target's (one locate a target) is the filter: a different label is no known way, never stored. A
		// route's "no known way" is the truth for him (his width, the walls he believes in): a verdict on its
		// target (SourceReachMemo), held as SourceReachMemo::keep says, and a target with one is not asked
		// about again. With no mesh nothing is known unreachable.
		if (haveMesh) {
			sourceReach.keep({.connectivityEpoch = m_navSystem->connectivityEpoch(), .beliefVersion = memory.beliefVersion}, m_tick);
		}
		const geometry::nav::ComponentLabel hisLabel = haveMesh ? m_navSystem->componentAt(position.value) : geometry::nav::kNoComponent;
		const auto connectedTo = [&](const glm::vec2& point) {
			return hisLabel != geometry::nav::kNoComponent && m_navSystem->componentAt(point) == hisLabel;
		};
		const auto hasVerdict = [&](std::uint64_t sourceKey, const glm::vec2& target) {
			return haveMesh && sourceReach.hasVerdict(verdictKey(sourceKey, target));
		};
		// Where a route to `target` ends when he is connected to it: the target, or the stand point beside
		// the placed obstacle it lies in. nullopt: no known way, or nowhere to stand. Ground with a label is
		// clear, so the target is its own end; a target with none is in water, off meshed ground, or in a
		// placed obstacle, and only the last is asked for a stand point. Each target once a decision.
		// A cut's or a pile's candidate keeps its answer on its own entry (WorkSources), so it asks the core;
		// every other target is kept in m_routeEnds.
		const auto routeEndOf = [&](const glm::vec2& target) -> std::optional<glm::vec2> {
			if (!haveMesh) {
				return target;
			}
			const geometry::nav::ComponentLabel label = m_navSystem->componentAt(target);
			if (label != geometry::nav::kNoComponent) {
				return label == hisLabel ? std::optional<glm::vec2>(target) : std::nullopt;
			}
			if (hisLabel != geometry::nav::kNoComponent && m_navSystem->isOnMesh(target)) {
				const std::optional<glm::vec2> stand = m_navSystem->standPointBeside(target, position.value, agentRadius);
				return stand.has_value() && connectedTo(*stand) ? stand : std::nullopt;
			}
			return std::nullopt;
		};
		m_routeEnds.clear();
		const auto routeEnd = [&](const glm::vec2& target) -> std::optional<glm::vec2> {
			if (!haveMesh) {
				return target;
			}
			for (const auto& [asked, end] : m_routeEnds) {
				if (asked == target) {
					return end;
				}
			}
			const std::optional<glm::vec2> end = routeEndOf(target);
			m_routeEnds.emplace_back(target, end);
			return end;
		};
		const auto canGetTo = [&](const glm::vec2& target) { return routeEnd(target).has_value(); };
		// A remembered source he may be sent to: no verdict, and connected to where a route to it ends,
		// which becomes its goal.
		const auto admitSource = [&](SourceCandidate& candidate) {
			if (hasVerdict(candidate.key, candidate.position)) {
				return false;
			}
			const std::optional<glm::vec2> end = routeEnd(candidate.position);
			if (!end.has_value()) {
				return false;
			}
			candidate.goal = *end;
			return true;
		};

		// A need asks one route for the nearest kNearestOfCount sources by straight line he may be sent to,
		// and the answer picks among them (requestRoute), so a sealed nearer source loses to the next in the
		// same request. The source he is heading for is kept while he may still be sent to it.
		const std::optional<std::uint64_t> headingFor =
			currentTask.needSourceKey != 0 ? std::optional<std::uint64_t>(currentTask.needSourceKey) : std::nullopt;
		for (SourceChoices& choices : m_needChoices) {
			choices.clear();
		}
		const auto choiceOf = [](const SourceCandidate& source) {
			return SourceChoice{.target = source.position, .goal = source.goal, .sourceKey = source.key};
		};
		// How many sources a need option's round names: kNearestOfCount, doubled for each round of it in a
		// row that found no known way.
		const auto roundSize = [&](std::size_t slot) -> std::size_t { return kNearestOfCount << sourceReach.roundWidening[slot]; };

		// With an empty pack, one search finds both what the Hunger option eats from and the bush the
		// gather-food option goes to, over every edible source the colonist knows, nearest by straight line
		// first: a ready harvestable whose yield is edible, a loose pile of an edible item it remembers,
		// and a storage box the colony knows that holds one. Piles and boxes only feed a colonist that is
		// hungry; the gather option, on offer whenever the pack is empty, takes bushes only. Candidates
		// are asked nearest first, only until each option has kNearestOfCount it may be sent to (its route
		// request picks among them), or the one he is heading for; the nearest connected bush passed over
		// for regrowing is kept to show why the colonist waits. When every edible harvestable it
		// knows is regrowing, they aren't searched again until the first is ready or its memory of world
		// entities changes (SourceReachMemo::FoodWait); piles and boxes are never part of that wait, so a
		// box that gains berries while it waits is seen at the next decision.
		//
		// A source another colonist is going to eat from (its Hunger task holds the Use, or a share of a
		// pile or box) is passed over like a regrowing one, so two hungry colonists don't converge on one
		// meal; the nearest is kept to show who holds it. Only an eater at least as urgent holds it: a
		// critically hungry colonist ignores a routinely hungry one's claim, and between equal tiers the
		// first to claim keeps it. Work claims (a cut for a goal, a haul, the idle gather) don't hold food
		// against a hungry colonist: a need's claim is one meal it will eat, and nothing else may starve
		// it. Still, a source nobody holds is preferred to one held by such a claim when one is known,
		// connected, and ready, so the search goes on past a held one and comes back to it only when
		// nothing free is left. The gather option is idle work and gives way to any holder: it takes the
		// nearest free bush.
		const ClaimExclusion self = ClaimExclusion::of(currentTask);
		const Need&			 hunger = needs.get(NeedType::Hunger);
		const int			 hungerTier = needTier(hunger, m_baseTiers);
		const auto			 eats = [hungerTier](const ClaimHolder& holder) {
			return holder.taskType == TaskType::FulfillNeed && holder.tier < kTierIdle && holder.tier <= hungerTier;
		};
		const bool packHasFood = hasEdibleFood(m_registry, inventory);
		const bool seeksFood = !packHasFood && hunger.needsAttention();
		struct FoodChoice {
			SourceCandidate source;
			uint32_t		left = 0; // what a pile or box has left for this colonist
		};
		std::optional<FoodChoice>		foodSource;
		std::optional<FoodChoice>		heldFood;
		std::optional<SourceCandidate>	gatherSource;
		std::optional<SourceCandidate>	heldBush;
		EntityID						heldBushHolder = kInvalidEntity;
		std::optional<KnownWorldEntity> regrowingFood;
		std::optional<SourceCandidate>	reservedFood;
		EntityID						reservedFoodHolder = kInvalidEntity;
		bool							searchExhausted = true;
		const auto						isBush = [](const SourceCandidate& source) { return source.item == 0; };
		const auto						mealOf = [&](uint32_t item) {
			const auto* def = m_registry.getDefinitionById(item);
			const float restore = def != nullptr && def->itemProperties.has_value() && def->itemProperties->edible.has_value()
									  ? def->itemProperties->edible->nutrition * 100.0F
									  : 0.0F;
			const float shortfall = hunger.seekThreshold - hunger.value;
			return restore > 0.0F && shortfall > restore ? static_cast<uint32_t>(std::ceil(shortfall / restore)) : 1U;
		};
		if (!packHasFood) {
			const double now = m_simulationNow;
			const auto*	 wait = sourceReach.waitingForFood(memory.worldEntityVersion, now);
			bool		 everyBushRegrowing = true;
			double		 firstReadyAt = std::numeric_limits<double>::infinity();
			sourceScratch.clear();
			if (wait != nullptr) {
				regrowingFood = wait->nearest;
			} else {
				// Only the defs whose yield is edible: the candidates, and so the heap's pick, are every
				// remembered harvestable that is food.
				const auto harvestable = static_cast<uint16_t>(1U << static_cast<size_t>(assets::CapabilityType::Harvestable));
				for (uint32_t food : m_registry.edibleItemIds()) {
					for (uint32_t bush : m_registry.harvestablesYielding(food)) {
						for (uint64_t key : memory.worldEntitiesOfDef(bush)) {
							const KnownWorldEntity* known = memory.getWorldEntity(key);
							if ((known->capabilityMask & harvestable) == 0) {
								continue;
							}
							if (hasVerdict(key, known->position)) {
								everyBushRegrowing = false; // never asked, so it may be ready
								continue;
							}
							SourceCandidate candidate = sourceCandidate(position.value, key, known->position);
							candidate.entity = known;
							sourceScratch.push_back(candidate);
						}
					}
				}
			}
			if (seeksFood) {
				const std::vector<FoodBox>& boxes = foodBoxes();
				const auto					carryable = static_cast<uint16_t>(1U << static_cast<size_t>(assets::CapabilityType::Carryable));
				for (uint32_t item : m_registry.edibleItemIds()) {
					for (uint64_t key : memory.worldEntitiesOfDef(item)) {
						const KnownWorldEntity* known = memory.getWorldEntity(key);
						if (known == nullptr || (known->capabilityMask & carryable) == 0) {
							continue;
						}
						if (hasVerdict(key, known->position)) {
							continue;
						}
						SourceCandidate candidate = sourceCandidate(position.value, key, known->position);
						candidate.entity = known;
						candidate.item = item;
						sourceScratch.push_back(candidate);
					}
				}
				for (size_t index = 0; index < boxes.size(); ++index) {
					SourceCandidate candidate = sourceCandidate(position.value, boxes[index].key, boxes[index].position);
					candidate.item = boxes[index].item;
					candidate.storage = boxes[index].box;
					candidate.storageIndex = static_cast<uint32_t>(index);
					sourceScratch.push_back(candidate);
				}
			}
			// The sources each option's route request names, nearest first: a meal for the Hunger option, a
			// bush for the gather option. Either is complete at kNearestOfCount, or at the one he is heading
			// for when it comes first (it comes off first when it is a candidate).
			SourceChoices&	  foodChoices	= m_needChoices[needChoiceSlot(NeedType::Hunger, false)];
			SourceChoices&	  gatherChoices = m_needChoices[needChoiceSlot(NeedType::Hunger, true)];
			const std::size_t foodRound		= roundSize(needChoiceSlot(NeedType::Hunger, false));
			const std::size_t gatherRound	= roundSize(needChoiceSlot(NeedType::Hunger, true));
			const auto		  complete		= [&](const SourceChoices& choices) {
				 const std::size_t round = &choices == &foodChoices ? foodRound : gatherRound;
				 return choices.size() >= round || (choices.size() == 1 && headingFor == choices[0].sourceKey);
			};
			const auto mealChoice = [&](const SourceCandidate& source, uint32_t left) {
				SourceChoice choice = choiceOf(source);
				if (isBush(source)) {
					choice.quantity = mealOf(edibleYieldOf(m_registry, source.entity->defNameId));
				} else {
					choice.itemDefNameId = source.item;
					choice.quantity = std::min(mealOf(source.item), left);
					choice.storageId = static_cast<uint64_t>(source.storage);
				}
				return choice;
			};
			const auto offerFood = [&](const SourceCandidate& source, uint32_t left) {
				if (!foodSource.has_value()) {
					foodSource = FoodChoice{source, left};
				}
				foodChoices.push(mealChoice(source, left));
			};
			const auto answered = [&] { return seeksFood ? complete(foodChoices) : complete(gatherChoices); };
			putKeptFirst(sourceScratch, headingFor);
			searchExhausted = visitNearestFirst(sourceScratch, [&](SourceCandidate& source) {
				if (isBush(source)) {
					if (const auto readyAt = sourceRegrowingUntil(source.entity->defNameId, source.position)) {
						firstReadyAt = std::min(firstReadyAt, *readyAt);
						if (!regrowingFood.has_value() && admitSource(source)) {
							regrowingFood = *source.entity;
						}
						return true;
					}
					everyBushRegrowing = false;
				}
				if (!admitSource(source)) {
					return true;
				}
				if (isBush(source)) {
					const ClaimTarget bush{.kind = TargetKind::WorldKey, .key = source.key};
					const auto		  holder = m_claims.useHolder(bush, self);
					if (!holder.has_value()) {
						gatherSource = gatherSource.value_or(source);
						if (!complete(gatherChoices)) {
							gatherChoices.push(choiceOf(source));
						}
					} else if (!heldBush.has_value()) {
						heldBush = source;
						heldBushHolder = holder->colonist;
					}
					if (seeksFood && !complete(foodChoices)) {
						if (const auto eater = m_claims.useHolderWhere(bush, self, eats)) {
							if (!reservedFood.has_value()) {
								reservedFood = source;
								reservedFoodHolder = eater->colonist;
							}
						} else if (holder.has_value()) {
							heldFood = heldFood.value_or(FoodChoice{source});
						} else {
							offerFood(source, 0);
						}
					}
					return !answered();
				}
				// A pile or a box: what it has left once eaters at least as urgent take theirs. A remembered
				// pile the world can't find has no count; it is offered while no eater claims it, so the
				// colonist goes to look and its memory is corrected on arrival.
				const ClaimTarget target = source.storage != kInvalidEntity
											   ? ClaimTarget{.kind = TargetKind::Entity, .key = static_cast<uint64_t>(source.storage)}
											   : ClaimTarget{.kind = TargetKind::WorldKey, .key = source.key};
				const uint32_t			meal = mealOf(source.item);
				std::optional<uint32_t> live;
				if (source.storage != kInvalidEntity) {
					live = m_foodBoxes[source.storageIndex].quantity;
				} else if (const EntityID pile = m_loosePiles != nullptr ? m_loosePiles->find(*world, source.item, source.position) : kInvalidEntity;
						   pile != kInvalidEntity) {
					live = world->getComponent<ResourceStack>(pile)->quantity;
				}
				const auto	   eaten = m_claims.sourceClaimedWhere(target, source.item, self, eats);
				const uint32_t claimed = m_claims.sourceClaimed(target, source.item, self);
				const uint32_t have = live.value_or(eaten.quantity > 0 ? 0U : meal);
				const uint32_t left = have > eaten.quantity ? have - eaten.quantity : 0U;
				if (left == 0) {
					if (!reservedFood.has_value() && eaten.holder.has_value()) {
						reservedFood = source;
						reservedFoodHolder = eaten.holder->colonist;
					}
					return true;
				}
				const uint32_t unclaimed = live.has_value() ? (have > claimed ? have - claimed : 0U) : (claimed > 0 ? 0U : meal);
				if (unclaimed < std::min(meal, left)) {
					heldFood = heldFood.value_or(FoodChoice{source, left});
					return true;
				}
				offerFood(source, left);
				return !answered();
			});
			if (seeksFood && !foodSource.has_value() && heldFood.has_value()) {
				offerFood(heldFood->source, heldFood->left);
			}
			if (wait == nullptr) {
				sourceReach.foodWait.reset();
				if (searchExhausted && everyBushRegrowing && firstReadyAt > now && firstReadyAt < std::numeric_limits<double>::infinity()) {
					sourceReach.foodWait = SourceReachMemo::FoodWait{memory.worldEntityVersion, firstReadyAt, regrowingFood};
				}
			}
		}

		// Evaluate each actionable need type
		for (auto needType : NeedsComponent::kActionableNeeds) {
			const auto& need = needs.get(needType);

			// Check if we're already pursuing this need - if so, preserve the target
			// This prevents "chasing a moving target" when toilet/sleep location is recalculated
			// Also preserve when Arrived to prevent recalculation while action is starting
			bool alreadyPursuingThisNeed = currentTask.isActive() && currentTask.type == TaskType::FulfillNeed &&
										   currentTask.needToFulfill == needType &&
										   (currentTask.state == TaskState::Moving || currentTask.state == TaskState::Arrived);

			EvaluatedOption option;
			option.taskType = TaskType::FulfillNeed;
			option.needType = needType;
			option.needValue = need.value;
			option.threshold = need.seekThreshold;

			// Special handling for hunger: check inventory first
			if (needType == NeedType::Hunger) {
				// First priority: eat from inventory if we have any edible food
				if (packHasFood) {
					// Food in inventory - eat at current position (no movement needed)
					option.targetPosition = position.value;
					option.distanceToTarget = 0.0F;
					option.status = need.needsAttention() ? OptionStatus::Available : OptionStatus::Satisfied;
					option.reason = "from inventory";
					trace.options.push_back(option);
					continue;
				}

				// Second priority: the known food source, by kind. A pile or box gives one meal, no more:
				// stored food is the colony's, and what goes into the pack is the colonist's own.
				if (foodSource.has_value()) {
					const SourceCandidate& source = foodSource->source;
					option.targetPosition = source.position;
					option.targetDefNameId = isBush(source) ? source.entity->defNameId : source.item;
					option.distanceToTarget = glm::distance(position.value, source.position);
					option.targetReachable = true;
					option.sourceKey = source.key;
					option.status = OptionStatus::Available;
					option.reason = isBush(source) ? "harvest" : source.storage != kInvalidEntity ? "from storage" : "from a pile";
					// The meal is sized whatever the pack holds: a full pack still eats, because the take
					// starts by making room for the meal (ActionSystem::makeRoomToEat), for every kind of source.
					if (isBush(source)) {
						option.needQuantity = mealOf(edibleYieldOf(m_registry, source.entity->defNameId));
					} else {
						option.needItemDefNameId = source.item;
						option.needQuantity = std::min(mealOf(source.item), foodSource->left);
						option.needSourceStorageId = static_cast<uint64_t>(source.storage);
					}
					trace.options.push_back(option);
					continue;
				}

				// Met: nothing to walk to; the nearest free bush is shown as where it would go
				if (!need.needsAttention()) {
					if (gatherSource.has_value()) {
						option.targetPosition = gatherSource->position;
						option.targetDefNameId = gatherSource->entity->defNameId;
						option.distanceToTarget = glm::distance(position.value, gatherSource->position);
					}
					option.status = OptionStatus::Satisfied;
					trace.options.push_back(option);
					continue;
				}

				// The food it knows is someone else's next meal: wait for it, taking other options meanwhile
				if (reservedFood.has_value()) {
					option.targetPosition = reservedFood->position;
					option.targetDefNameId = isBush(*reservedFood) ? reservedFood->entity->defNameId : reservedFood->item;
					option.distanceToTarget = glm::distance(position.value, reservedFood->position);
					option.status = OptionStatus::Reserved;
					option.reservedBy = reservedFoodHolder;
					trace.options.push_back(option);
					continue;
				}

				// The only reachable food known is regrowing: wait on it, taking other options meanwhile
				if (regrowingFood.has_value()) {
					option.targetPosition = regrowingFood->position;
					option.targetDefNameId = regrowingFood->defNameId;
					option.distanceToTarget = glm::distance(position.value, regrowingFood->position);
					option.status = OptionStatus::Regrowing;
					trace.options.push_back(option);
					continue;
				}

				// No food in inventory and no reachable food source known
				option.status = OptionStatus::NoSource;
				trace.options.push_back(option);
				continue;
			}

			// Standard handling for other needs
			auto capability = needToCapability(needType);
			// A satisfied need only shows its nearest source (its row and its place among the idle options),
			// so it spends no reachability tests and builds no candidate list.
			std::optional<KnownWorldEntity> nearest;
			if (need.needsAttention()) {
				SourceChoices& choices = m_needChoices[needChoiceSlot(needType, false)];
				collectWithCapability(
					memory, capability, position.value,
					[&](uint64_t key, const KnownWorldEntity& known) { return !hasVerdict(key, known.position); }, sourceScratch
				);
				nearestAdmitted(sourceScratch, m_chosenScratch, admitSource, roundSize(needChoiceSlot(needType, false)), headingFor);
				for (const SourceCandidate& source : m_chosenScratch) {
					choices.push(choiceOf(source));
				}
				if (!m_chosenScratch.empty()) {
					nearest = *m_chosenScratch.front().entity;
				}
			} else {
				nearest = findNearestWithCapability(memory, capability, position.value);
			}

			if (nearest.has_value()) {
				option.targetPosition = nearest->position;
				option.targetDefNameId = nearest->defNameId;
				option.distanceToTarget = glm::distance(position.value, nearest->position);
				option.targetReachable = need.needsAttention();
				option.sourceKey = hashWorldEntity(nearest->position, nearest->defNameId);

				if (need.needsAttention()) {
					option.status = OptionStatus::Available;
				} else {
					option.status = OptionStatus::Satisfied;
				}
			} else if (needType == NeedType::Energy || needType == NeedType::Bladder || needType == NeedType::Digestion) {
				// Ground fallback for sleep and toilet
				// Only do expensive location finding if the need actually needs attention
				if (!need.needsAttention()) {
					option.status = OptionStatus::Satisfied;
					option.targetPosition = position.value;
					option.distanceToTarget = 0.0F;
				} else if (alreadyPursuingThisNeed && !hasVerdict(0, currentTask.workTarget())) {
					// Preserve existing target to avoid "chasing" a moving target
					option.targetPosition = currentTask.targetPosition;
					option.distanceToTarget = glm::distance(position.value, currentTask.targetPosition);
					option.status = OptionStatus::Available;
				} else if ((needType == NeedType::Bladder || needType == NeedType::Digestion) && m_chunkManager != nullptr &&
						   m_navSystem != nullptr) {
					// For toilet needs, try smart location finder
					// A spot whose route already failed is no spot: go where the colonist stands instead.
					auto location = findToiletLocation(position.value, *m_chunkManager, *m_navSystem, memory);
					// Asked at every decision while the need waits behind other work: say only when the answer moves.
					if (location != sourceReach.lastToiletSpot) {
						sourceReach.lastToiletSpot = location;
						if (location.has_value()) {
							LOG_DEBUG(Engine, "[ToiletLocation] Entity %llu: found at (%.1f, %.1f)", static_cast<unsigned long long>(entity), location->x, location->y);
						}
					}
					if (location.has_value() && !hasVerdict(0, *location)) {
						option.targetPosition = *location;
						option.distanceToTarget = glm::distance(position.value, *location);
						option.status = OptionStatus::Available;
					} else {
						// Fallback to current position
						option.targetPosition = position.value;
						option.distanceToTarget = 0.0F;
						option.status = OptionStatus::Available;
					}
				} else {
					option.targetPosition = position.value;
					option.distanceToTarget = 0.0F;
					option.status = OptionStatus::Available;
				}
			} else {
				option.status = !need.needsAttention() ? OptionStatus::Satisfied : OptionStatus::NoSource;
			}

			trace.options.push_back(option);
		}

		// Add "Gather Food" work option (tier 7): the nearest free bush, while the pack has no food. Not
		// known when the search stopped at stored food a hungry colonist will eat first; it is not shown then.
		if (!packHasFood && (gatherSource.has_value() || !seeksFood || searchExhausted)) {
			EvaluatedOption gatherOption;
			gatherOption.taskType = TaskType::FulfillNeed; // a Hunger task: cut the bush into the pack
			gatherOption.needType = NeedType::Hunger;
			gatherOption.gathersFood = true;

			// Harvesting uses Farming skill
			applySkill(gatherOption, workSkills.farming);

			// Gathering is idle work, not a meal: it makes no room, so a pack with none for the yield
			// would cut nothing and come back to the same bush.
			const auto gatherHasRoom = [&] { return pickupRoom(edibleYieldOf(m_registry, gatherSource->entity->defNameId)) > 0; };
			if (gatherSource.has_value()) {
				gatherOption.targetPosition = gatherSource->position;
				gatherOption.distanceToTarget = glm::distance(position.value, gatherSource->position);
				gatherOption.targetReachable = true;
				gatherOption.sourceKey = gatherSource->key;
				if (gatherHasRoom()) {
					gatherOption.status = OptionStatus::Available;
					gatherOption.reason = "Gathering food (inventory empty)";
				} else {
					gatherOption.status = OptionStatus::NoRoom;
					gatherOption.reason = "Gathering food (no room to carry it)";
				}
			} else if (heldBush.has_value()) {
				gatherOption.targetPosition = heldBush->position;
				gatherOption.distanceToTarget = glm::distance(position.value, heldBush->position);
				gatherOption.status = OptionStatus::Reserved;
				gatherOption.reservedBy = heldBushHolder;
				gatherOption.reason = "Food source";
			} else if (regrowingFood.has_value()) {
				gatherOption.targetPosition = regrowingFood->position;
				gatherOption.distanceToTarget = glm::distance(position.value, regrowingFood->position);
				gatherOption.status = OptionStatus::Regrowing;
				gatherOption.reason = "Food source regrowing";
			} else {
				gatherOption.status = OptionStatus::NoSource;
				gatherOption.reason = "No food source known";
			}

			trace.options.push_back(gatherOption);
		}

		// Add "Crafting Work" options and "Gather" options (tier classification set by the evaluators)
		// Find all stations with pending work that colonist can do
		for (const SharedWork::Station& station : m_sharedWork.stations(*world, m_recipeRegistry)) {
			const EntityID	 stationEntity = station.entity;
			const glm::vec2& stationPos = station.position;

			// Add craft option with skill bonus
			EvaluatedOption craftOption;
			craftOption.taskType = TaskType::Craft;
			craftOption.needType = NeedType::Count; // N/A for crafting
			craftOption.needValue = 100.0F;			// Not a need
			craftOption.threshold = 0.0F;
			craftOption.targetPosition = stationPos;
			craftOption.distanceToTarget = glm::distance(position.value, stationPos);
			craftOption.craftRecipeDefName = station.recipeDefName;
			craftOption.stationEntityId = static_cast<uint64_t>(stationEntity);
			craftOption.tiebreakId = static_cast<uint64_t>(stationEntity);

			// Calculate skill bonus for crafting
			applySkill(craftOption, workSkills.crafting);

			// Provisioning the materials is the goal-driven Harvest/Haul options' job; the Craft
			// option is the crafting WORK, workable only once the station holds every input.
			if (station.hasAllInputs) {
				craftOption.status = OptionStatus::Available;
				craftOption.reason = "Crafting {}";
				// Another crafter on its way holds the station's slot from its Craft selection.
				if (const auto holder = m_claims.slotHolder(
						{.kind = TargetKind::Entity, .key = static_cast<uint64_t>(stationEntity)}, kAnySlot, self
					)) {
					craftOption.status = OptionStatus::Reserved;
					craftOption.reservedBy = holder->colonist;
				}
			} else {
				craftOption.status = OptionStatus::NoSource;
				craftOption.reason = "Crafting {} (awaiting materials)";
			}

			trace.options.push_back(std::move(craftOption));
		}
		// Goal-driven work: fetch goods for storage and crafts, carry in what the colonist holds, cut
		// for what a root still needs (tier 4 when serving an active work order, else tier 6).
		const RootSupply supply{*world, m_needs, m_claims, self, currentTask, inventory, m_registry, m_loosePiles, m_boxRooms};
		// Goal createdAt is always 0, so a goal's age is m_elapsedTime, monotonic, which is all the
		// within-tier age term needs.
		const OptionPriority priority{
			.world = *world,
			.boxes = m_boxRooms,
			.registry = m_registry,
			.currentTask = currentTask,
			.currentHaulItemId = currentTask.type == TaskType::Haul ? currentTask.haulItemDefNameId : 0U,
			.currentTime = m_elapsedTime,
			.baseTiers = m_baseTiers,
		};
		m_workSources.begin(memory, m_registry, inventory, position.value);
		m_workGroups.clear();
		m_ranked.clear();
		m_offeredPiles.clear();
		const PileReach pileReach{.memo = &sourceReach, .connected = [&](glm::vec2 at) { return routeEndOf(at).has_value(); }};
		WorkCandidates	candidates{
			 .groups = m_workGroups,
			 .ranked = m_ranked,
			 .sources = m_workSources,
			 .shared = m_sharedWork,
			 .supply = supply,
			 .reach = pileReach,
			 .position = position.value,
		 };
		evaluateHaulOptions(world, m_registry, m_colonyStorage, supply, pickupRoom, inventory, priority, candidates, m_offeredPiles, trace);
		evaluateCarryInOptions(supply, m_carried, position.value, trace);
		evaluateHarvestOptions(supply, pickupRoom, workSkills, m_offeredPiles, priority, candidates, trace);

		// Build staged construction blueprints (goal-driven)
		evaluateBuildOptions(m_sharedWork.buildSites(*world), position.value, workSkills, trace);

		// Deconstruct demolishing structures (goal-driven, same tier as Build)
		evaluateDeconstructOptions(m_sharedWork.teardownSites(*world), position.value, workSkills, trace);

		// Place packaged items at target locations
		evaluatePlacePackagedOptions(m_sharedWork.packages(*world), supply, position.value, inventory, trace);

		// Firm "can I actually navigate right now?" gate. An idle wander picks a random point;
		// with no navmesh yet (the costly startup window) the colonist has no idea what's walkable,
		// and the connected check can't help: with no mesh it passes every point, so a wander offered then is
		// one the colonist can't follow. So only OFFER the wander when navigation is possible: a mesh
		// exists, or there's no nav system at all (headless/tests, direct movement). With a nav
		// system but no mesh, emit no idle option; the colonist holds (see the no-option hold in
		// selectTaskFromTrace) until the mesh lands. Needs and work keep their own options, but they
		// too only MOVE once their route is answered -- a leg asked from off the mesh HOLDS, it does
		// not slide blind. (Off-mesh stranding is handled separately by the snap-to-mesh
		// recovery at the top of update().)
		const bool canNavigate = (m_navSystem == nullptr) || haveMesh;
		if (canNavigate) {
			// Only offer a wander point on meshed ground connected to him that his disc fits on, within the
			// wander radius by straight line. Probe a few random points; if none is -- a colonist hemmed in by
			// water/obstacles where every point lands somewhere he has no known way to -- emit no idle option
			// at all. The no-option hold then makes it stand and look around instead of committing to a point
			// it would thrash on. A wander already going somewhere keeps its point and probes none: a moving
			// wander decides every frame, and that decision keeps the wander whatever point it rolls.
			const auto standable = [&](const glm::vec2& target) {
				if (!haveMesh) {
					return true;
				}
				if (!connectedTo(target)) {
					return false;
				}
				// His disc's rim, every 45 degrees.
				constexpr float kDiagonal = 0.70710678F;
				for (const glm::vec2 rim : {glm::vec2{1.0F, 0.0F}, glm::vec2{-1.0F, 0.0F}, glm::vec2{0.0F, 1.0F}, glm::vec2{0.0F, -1.0F},
											glm::vec2{kDiagonal, kDiagonal}, glm::vec2{-kDiagonal, kDiagonal}, glm::vec2{kDiagonal, -kDiagonal},
											glm::vec2{-kDiagonal, -kDiagonal}}) {
					if (!m_navSystem->isValidPosition(target + rim * agentRadius)) {
						return false;
					}
				}
				return true;
			};
			glm::vec2 wanderTarget	 = wanderUnderway ? currentTask.targetPosition : generateWanderTarget(position.value);
			bool	  foundReachable = wanderUnderway || standable(wanderTarget);
			for (int attempt = 0; !foundReachable && attempt < 5; ++attempt) {
				const glm::vec2 candidate = generateWanderTarget(position.value);
				if (standable(candidate)) {
					wanderTarget   = candidate;
					foundReachable = true;
				}
			}
			if (foundReachable) {
				EvaluatedOption wanderOption;
				wanderOption.taskType		= TaskType::Wander;
				wanderOption.needType		= NeedType::Count; // N/A
				wanderOption.status			= OptionStatus::Available;
				wanderOption.reason			= "All needs satisfied";
				wanderOption.targetPosition = wanderTarget;
				trace.options.push_back(wanderOption);
			}
		}

		// Populate the (tier, score) key for all options: classify tier, compute the within-tier
		// score (distance factor, skill, age, hysteresis).
		for (auto& option : trace.options) {
			priority(option);
		}

		// Sort by the lexicographic (tier asc, score desc, tiebreakId asc) key. Tier is categorical
		// and compared first: a far tier-4 option always beats any tier-5 option, with no floor
		// constants. The tiebreak id gives a deterministic total order independent of container
		// iteration (the goal registry is hash-bucket ordered), so two equal-(tier,score) options
		// can't route colonists nondeterministically (a multiplayer desync). higherPriority()
		// encodes the full ordering.
		std::sort(trace.options.begin(), trace.options.end(), EvaluatedOption::higherPriority);

		// A work option stands for its group's sources (WorkGroup). When one fails a test that depends on
		// the source, the group's next source no other colonist holds takes its place, the option moves to
		// where that source ranks among the options not yet tested, and the same index is tested again: the
		// options are tried in the order one option per source would be. With none left, the option shows
		// the best-ranked source it tried that was regrowing, as Regrowing, else its first source, as
		// NoSource. True when there is a next source.
		const auto advanceWorkOption = [&](WorkGroup& group, std::size_t at) {
			EvaluatedOption&			  option = trace.options[at];
			const std::optional<uint32_t> next = candidates.nextOffered(group, group.current);
			candidates.fill(option, group, next.value_or(group.regrowing.value_or(group.first)));
			if (!next.has_value()) {
				option.status = group.regrowing.has_value() ? OptionStatus::Regrowing : OptionStatus::NoSource;
				priority(option);
				return false;
			}
			group.current = *next;
			priority(option);
			const auto untested = trace.options.begin() + static_cast<std::ptrdiff_t>(at) + 1;
			const auto ranksAt = std::upper_bound(untested, trace.options.end(), option, EvaluatedOption::higherPriority);
			std::rotate(trace.options.begin() + static_cast<std::ptrdiff_t>(at), untested, ranksAt);
			return true;
		};

		// Select the first actionable option the colonist may be sent to, scanning in priority order. The
		// test is cheap, a verdict lookup and a label check: false means no known way (off meshed ground,
		// or a different component -- e.g. a reed or water across a river with no land bridge, or a
		// target a route already found sealed for him). Probing in priority order means we only test down
		// to the best option, not the whole list. A higher-priority option with no known way drops to
		// NoSource so the colonist won't commit to it and freeze at the bank; Wander is never filtered,
		// so there is always a fallback that keeps it moving and exploring for new sources. No mesh yet
		// -> the test passes, so startup / outdoor beelining is unaffected. A cut's or a pile's source answers
		// each test once a decision, whichever group asks.
		const auto canTake = [&](std::uint64_t sourceKey, const glm::vec2& target) { return !hasVerdict(sourceKey, target) && canGetTo(target); };
		std::size_t at = 0;
		while (at < trace.options.size()) {
			EvaluatedOption& option = trace.options[at];
			if (option.status != OptionStatus::Available) {
				++at;
				continue;
			}
			WorkGroup* const	 group = workGroupOf(option);
			const bool			 harvests = group != nullptr && (group->kind == WorkKind::Cut || group->kind == WorkKind::Clear);
			HarvestSource* const cut = harvests ? &m_workSources.harvest(m_ranked[group->current].index) : nullptr;
			CarrySource* const	 pile = group != nullptr && group->kind == WorkKind::Pile ? &m_workSources.carry(m_ranked[group->current].index) : nullptr;
			bool				 failsEverySource = false; // the failure holds for every source of the group alike
			const OptionStatus	 tested = [&] {
				const bool needsTarget = option.taskType != TaskType::Wander && option.targetPosition.has_value();
				// A harvest of a regrowing source would pay nothing at completion: pass it over for the next
				// source, and it is chosen again once ready. A clear removes its source whatever its state.
				// Asked before the snap moves the target off the source's own position.
				if (cut != nullptr && group->kind == WorkKind::Cut && cut->regrowing.get([&] {
						return sourceRegrowingUntil(cut->entity->defNameId, cut->entity->position).has_value();
					})) {
					return OptionStatus::Regrowing;
				}
				// Snap only the task types whose action looks its target up by entity/footprint and so
				// is satisfied by arriving ADJACENT: Harvest (incl. footprint clearing), Build, and
				// Deconstruct. Their stored target is a CENTER that can sit inside a tree's collider or a
				// wall, which no route ends in. A harvest stands just outside the target's collider on
				// the colonist's side, read from the placement data so it is the same spot on camera and
				// off; Build and Deconstruct take the nearest walkable point. Any other target (a pile, a
				// need source, a box, a station) keeps its own position on the option and the task, and is
				// moved to a stand point beside the placed obstacle it lies in when its route is asked
				// (requestRoute); with no stand point there, it is no source.
				const bool snapEligible = option.taskType == TaskType::Harvest ||
										  option.taskType == TaskType::Build || option.taskType == TaskType::Deconstruct;
				if (haveMesh && needsTarget && snapEligible) {
					// nullopt means the target is off meshed ground, or every spot round it is water or a
					// wall: the colonist prefers a reachable nearby source and never commits to a target it
					// would hold on forever. Harvest finds its tree by the memory key, not by where it stands.
					// The harvest he is on keeps the stand point it chose: worked out again from where he stands
					// as he rounds an obstacle, it would move and read as another task. One decided before any
					// mesh holds the centre itself, which no route ends in once the collider is meshed.
					const glm::vec2 centre = option.targetPosition.value();
					const auto		snapped = cut != nullptr ? cut->standPoint.get([&] {
						 const bool sameHarvest = currentTask.type == TaskType::Harvest && currentTask.harvestTargetKey != 0 &&
												  currentTask.harvestTargetKey == cut->key && currentTask.targetPosition != centre;
						 return sameHarvest ? std::optional<glm::vec2>(currentTask.targetPosition)
											: standPointFor(centre, position.value, agentRadius, true);
					 })
															 : m_navSystem->nearestPathablePoint(centre);
					if (!snapped.has_value()) {
						return OptionStatus::NoSource; // nowhere to stand: don't commit
					}
					option.targetPosition  = snapped;
					option.targetReachable = false; // a moved target is tested where it now is
				}
				// A target is offered when no route found no known way to it and he is connected to where a route
				// to it ends (beside the placed obstacle it lies in, if it does); a need source the builder
				// already admitted (targetReachable), and one where he stands, aren't tested again. The route
				// asked for the chosen option settles the rest.
				if (needsTarget && !option.targetReachable && *option.targetPosition != position.value) {
					const glm::vec2 target = *option.targetPosition;
					const bool		takes = cut != nullptr	  ? cut->reachable.get([&] {
							  return !hasVerdict(cut->key, target) && routeEndOf(target).has_value();
						  })
											: pile != nullptr ? !hasVerdict(option.sourceKey, target) && pileReach.connectedTo(*pile)
															  : canTake(option.sourceKey, target);
					if (!takes) {
						return OptionStatus::NoSource; // no known way: skip it rather than freeze on it
					}
				}
				// A haul is chosen only when its deposit leg can route too. Every source of a fetch delivers to
				// the same place, so none of the others is tried.
				if (option.taskType == TaskType::Haul && option.haulTargetPosition.has_value() && option.haulTargetPosition != option.targetPosition &&
					!canTake(0, *option.haulTargetPosition)) {
					failsEverySource = true;
					return OptionStatus::NoSource;
				}
				// Picking up furniture whose spot he can't reach would only carry it off and set it down again.
				if (option.taskType == TaskType::PlacePackaged && option.placeTargetPosition.has_value() &&
					option.placeTargetPosition != option.targetPosition && !canTake(0, *option.placeTargetPosition)) {
					return OptionStatus::NoSource;
				}
				return OptionStatus::Selected;
			}();
			if (tested == OptionStatus::Selected) {
				option.status = OptionStatus::Selected;
				break;
			}
			if (group == nullptr || failsEverySource) {
				option.status = tested;
				++at;
				continue;
			}
			if (tested == OptionStatus::Regrowing && !group->regrowing.has_value()) {
				group->regrowing = group->current;
			}
			if (!advanceWorkOption(*group, at)) {
				++at;
			}
		}

		// Stamp the evaluation time so the inspector can detect a changed trace: any monotonic stamp
		// does, and m_elapsedTime is one.
		trace.lastEvaluationTime = m_elapsedTime;
	}

	void AIDecisionSystem::selectTaskFromTrace(
		EntityID			 entity,
		Task&				 task,
		MovementTarget&		 movementTarget,
		const DecisionTrace& trace,
		const Position&		 position,
		const Memory&		 memory
	) {
		const auto* selected = trace.getSelected();
		if (selected == nullptr) {
			// Nothing to navigate to right now -- typically all needs are met and the navmesh isn't
			// built yet, so the idle wander was withheld on purpose (can't navigate). Hold: stand in
			// place, movement off. state=Moving (not Arrived) keeps shouldReEvaluate
			// from re-deciding every frame; the periodic re-eval still fires, and a Wander with
			// movement off never counts as the same task, so the colonist takes the first real
			// option that re-eval offers once the mesh lands.
			task.type			  = TaskType::Wander;
			task.state			  = TaskState::Moving;
			task.navState		  = NavState::Traveling;
			task.targetPosition	  = position.value;
			task.reason			  = "Waiting for the area to settle";
			movementTarget.active = false;
			return;
		}

		task.type = selected->taskType;
		task.needToFulfill = selected->needType;
		task.targetPosition = selected->targetPosition.value_or(position.value);
		task.reason = describeOption(*world, *selected);
		task.needSourceKey = selected->sourceKey;
		task.needItemDefNameId = selected->needItemDefNameId;
		task.needQuantity = selected->needQuantity;
		task.needSourceStorageId = selected->needSourceStorageId;
		// A need with several sources it may be sent to asks one route for all of them; the task holds the
		// nearest by straight line until the answer picks one.
		if (selected->taskType == TaskType::FulfillNeed) {
			const SourceChoices& choices = m_needChoices[needChoiceSlot(selected->needType, selected->gathersFood)];
			if (choices.size() > 1 && choices[0].target == task.targetPosition) {
				task.sourceChoices = choices;
				takeSourceChoice(task, 0);
			}
		}

		// Copy crafting-specific fields for Craft tasks
		if (selected->taskType == TaskType::Craft) {
			task.craftRecipeDefName = selected->craftRecipeDefName;
			task.targetStationId = selected->stationEntityId;
		}

		// Copy harvest-specific fields for Harvest tasks. A cut toward a root becomes its own deposit leg
		// when it lands (ActionSystem::carryHarvestIn).
		if (selected->taskType == TaskType::Harvest) {
			task.harvestTargetKey = selected->harvestTargetKey;
			task.harvestGoalId = selected->harvestGoalId;
			task.harvestYieldDefNameId = selected->harvestYieldDefNameId;
		}

		// Copy build-specific fields for Build/Deconstruct tasks. ActionSystem reads the blueprint
		// entity from the task and advances (Build) or unwinds (Deconstruct) its workDone;
		// targetPosition is the work slot.
		if (selected->taskType == TaskType::Build || selected->taskType == TaskType::Deconstruct) {
			task.buildBlueprintEntityId = selected->buildBlueprintEntityId;
		}

		// Copy hauling-specific fields for Haul tasks
		if (selected->taskType == TaskType::Haul) {
			task.haulItemDefNameId = selected->haulItemDefNameId;
			task.haulQuantity = selected->haulQuantity;
			task.haulSourcePosition = selected->haulSourcePosition.value_or(glm::vec2{0.0F, 0.0F});
			task.haulSourceStorageId = selected->haulSourceStorageId;
			task.haulDestinationId = selected->haulDestinationId;
			task.haulGoalId = selected->haulGoalId;
			task.haulTargetPosition = selected->haulTargetPosition.value_or(glm::vec2{0.0F, 0.0F});
			task.haulFromInventory = selected->haulFromInventory;
			// Craft-material hauls already carry the items, so head straight to the station.
			// Standard hauls go to the source first (pickup), then the destination.
			task.targetPosition = task.haulFromInventory ? task.haulTargetPosition : task.haulSourcePosition;
			// Pickup (step 0) then Deposit (step 1); goods already carried start at the deposit leg.
			task.chainStep = task.haulFromInventory ? 1 : 0;
		}

		// Copy placement-specific fields for PlacePackaged tasks
		// PlacePackaged is a two-step chain: PickupPackaged (step 0) → Place (step 1)
		if (selected->taskType == TaskType::PlacePackaged) {
			task.placePackagedEntityId = selected->placePackagedEntityId;
			task.placeSourcePosition = selected->placeSourcePosition.value_or(glm::vec2{0.0F, 0.0F});
			task.placeTargetPosition = selected->placeTargetPosition.value_or(glm::vec2{0.0F, 0.0F});
			// The pickup leg first, unless he already carries it: then his pickup spot is stale and he
			// goes straight to the placement.
			task.targetPosition = selected->carryingPackaged ? task.placeTargetPosition : task.placeSourcePosition;
			task.chainStep = selected->carryingPackaged ? 1 : 0;
		}

		movementTarget.target = task.targetPosition;

		// Check if ground fallback (already at target)
		bool isGroundFallback = (task.targetPosition == position.value);
		if (isGroundFallback) {
			movementTarget.active = false;
			task.state = TaskState::Arrived;
			// Already at the goal: drop any stale route from a prior task.
			if (auto* navPath = world->getComponent<NavPath>(entity)) {
				navPath->valid = false;
			}
		} else {
			movementTarget.active = true;
			task.state = TaskState::Moving;
			requestRoute(entity, task, position, memory, movementTarget, false);
		}
	}

	void AIDecisionSystem::requestRoute(EntityID entity, Task& task, const Position& position, const Memory& memory,
										MovementTarget& movementTarget, bool keepWalking) {
		auto* navPath = world->getComponent<NavPath>(entity);
		const bool walking = keepWalking && navPath != nullptr && !navPath->done() && movementTarget.active;
		if (!walking && navPath != nullptr) {
			navPath->valid = false;
		}
		if (m_navSystem == nullptr) {
			setNavState(entity, task, NavState::Traveling);
			return;
		}
		dropRoute(task);
		task.awaitingSnap = false;
		float radius = 0.3F;
		if (const auto* agentRadius = world->getComponent<AgentRadius>(entity)) {
			radius = agentRadius->radiusMeters;
		}
		// A target lying in a placed obstacle is reached from beside it; nowhere to stand there is no way.
		// Several sources' goals were found when they were chosen.
		if (task.sourceChoices.empty() && !task.obstructedTarget.has_value()) {
			const glm::vec2				   target = task.targetPosition;
			const std::optional<glm::vec2> stand  = standPointFor(target, position.value, radius, false);
			if (!stand.has_value()) {
				failRoute(entity, task, memory, movementTarget, RouteFailure::NoStandPoint);
				return;
			}
			if (*stand != target) {
				task.obstructedTarget  = target;
				task.targetPosition	   = *stand;
				movementTarget.target = *stand;
			}
		}
		// Planned over what he remembers: unseen walls are absent, seen walls block, known doors pass.
		// requestRoute copies the sets.
		// A need with several sources asks for all of them at once: the answer's goal is the choice.
		const geometry::nav::BeliefFilter				   belief{&memory.knownSegments, &memory.knownOpenings};
		std::array<glm::vec2, SourceChoices::kCapacity> goals{task.targetPosition};
		std::size_t										   goalCount = 1;
		if (!task.sourceChoices.empty()) {
			goalCount = task.sourceChoices.size();
			for (std::uint8_t index = 0; index < task.sourceChoices.size(); ++index) {
				goals[index] = task.sourceChoices[index].goal;
			}
		}
		task.routeTicket		= m_navSystem->requestRoute(position.value, std::span<const glm::vec2>(goals.data(), goalCount), radius, belief);
		task.routeGoal			= task.targetPosition;
		task.routeBeliefVersion = memory.beliefVersion;
		task.routeWaitTicks		= 0;
		task.routeAskedAt		= m_elapsedTime;
		task.routeAskedGroundEpoch = m_navSystem->awaitedGroundEpoch();
		if (!walking) {
			movementTarget.active = false;
			setNavState(entity, task, NavState::AwaitingRoute);
		}
		takeRouteAnswer(entity, task, position, memory, movementTarget);
	}

	void AIDecisionSystem::takeRouteAnswer(EntityID entity, Task& task, const Position& position, const Memory& memory,
										   MovementTarget& movementTarget) {
		if (task.routeTicket == 0 || m_navSystem == nullptr) {
			return;
		}
		std::optional<NavigationSystem::RouteAnswer> answer = m_navSystem->takeRouteAnswer(task.routeTicket);
		if (!answer.has_value()) {
			// Nav no longer holds it (dropped, or never issued): it will never answer.
			if (!m_navSystem->routeTicketLive(task.routeTicket)) {
				task.routeTicket = 0;
				requestRoute(entity, task, position, memory, movementTarget, true);
			}
			return;
		}
		task.routeTicket = 0;
		if (task.routeGoal != task.targetPosition) {
			requestRoute(entity, task, position, memory, movementTarget, false);
			return;
		}
		auto* navPath = world->getComponent<NavPath>(entity);
		switch (answer->outcome) {
			case nav::RouteOutcome::Routed: {
				// Of several sources, the one the route reached is the choice: it ends there. One another
				// colonist claimed while the request was out, and holds against him, is not taken: he decides
				// again at once, and that decision passes over it.
				if (!task.sourceChoices.empty()) {
					if (answer->goalIndex > 0 && answer->goalIndex < static_cast<std::int32_t>(task.sourceChoices.size()) &&
						sourceHeldAgainst(entity, task, task.sourceChoices[static_cast<std::uint8_t>(answer->goalIndex)])) {
						if (navPath != nullptr) {
							navPath->valid = false;
						}
						movementTarget.active = false;
						m_claims.retire(task.instanceId);
						task.clear();
						return;
					}
					if (answer->goalIndex > 0 && answer->goalIndex < static_cast<std::int32_t>(task.sourceChoices.size())) {
						takeSourceChoice(task, static_cast<std::uint8_t>(answer->goalIndex));
						movementTarget.target = task.targetPosition;
						task.claims.clear();
						auto* inventory = world->getComponent<Inventory>(entity);
						if (inventory != nullptr) {
							recordChainClaims(*world, m_needs, m_claims, m_registry, memory, *inventory, task);
						}
						if (m_claimsBuilt) {
							m_claims.append(entity, task);
						}
					}
					task.sourceChoices.clear();
				}
				NavPath route;
				route.waypoints			 = joinRoute(std::move(answer->waypoints), position.value);
				route.current			 = route.waypoints.size() >= 2 ? 1 : 0;
				route.valid				 = true;
				route.builtBeliefVersion = task.routeBeliefVersion;
				route.builtNavVersion	 = m_navSystem->generation();
				LOG_DEBUG(Engine, "[Nav] Entity %llu: route to (%.2f, %.2f), %zu waypoints", static_cast<unsigned long long>(entity),
						  task.targetPosition.x, task.targetPosition.y, route.waypoints.size());
				if (navPath != nullptr) {
					*navPath = std::move(route);
				} else {
					world->addComponent<NavPath>(entity, std::move(route));
				}
				movementTarget.active	 = true;
				task.snapWaitDecisions	 = 0;
				task.routeFailureDecided = false;
				if (auto* memo = world->getComponent<SourceReachMemo>(entity); memo != nullptr && task.type == TaskType::FulfillNeed) {
					memo->roundWidening[needSlotOf(task)] = 0;
				}
				setNavState(entity, task, NavState::Traveling);
				return;
			}
			case nav::RouteOutcome::AwaitingMesh:
				if (navPath != nullptr) {
					navPath->valid = false;
				}
				movementTarget.active = false;
				setNavState(entity, task, NavState::AwaitingMesh);
				LOG_DEBUG(Engine, "[Nav] Entity %llu: his route's ground is on its way, waiting for it", static_cast<unsigned long long>(entity));
				return;
			case nav::RouteOutcome::NoKnownWay:
				break;
		}
		// The snap holds him inside a placed obstacle and moves him out: no way from there is "not now".
		// He stands with no ticket until the snap lets go or the mesh moves (update() asks then), or
		// kSnapWaitDecisions decisions pass.
		if (m_navSystem->snapHolds(entity)) {
			if (navPath != nullptr) {
				navPath->valid = false;
			}
			movementTarget.active	   = false;
			task.awaitingSnap		 = true;
			setNavState(entity, task, NavState::AwaitingRoute);
			return;
		}
		failRoute(entity, task, memory, movementTarget, RouteFailure::NoKnownWay);
	}

	bool AIDecisionSystem::awaitedGroundDue(const Task& task) const {
		return m_navSystem->awaitedGroundEpoch() != task.routeAskedGroundEpoch && m_elapsedTime - task.routeAskedAt >= kReEvalInterval;
	}

	std::optional<glm::vec2> AIDecisionSystem::standPointFor(glm::vec2 target, glm::vec2 from, float radius, bool placedTarget) const {
		if (m_navSystem == nullptr) {
			return target;
		}
		// Inside a placed obstacle in the mesh: on a face a mover may stand on (isOnMesh) that is not clear
		// ground (isValidPosition). Water and unmeshed ground are left to the route.
		if (!placedTarget && (m_navSystem->isValidPosition(target) || !m_navSystem->isOnMesh(target))) {
			return target;
		}
		return m_navSystem->standPointBeside(target, from, radius);
	}

	bool AIDecisionSystem::sourceHeldAgainst(EntityID entity, const Task& task, const SourceChoice& choice) {
		const auto* needs = world->getComponent<NeedsComponent>(entity);
		if (task.needToFulfill != NeedType::Hunger || needs == nullptr) {
			return false;
		}
		ensureClaimIndex();
		const ClaimExclusion self		= ClaimExclusion::of(task);
		const int			 hungerTier = needTier(needs->get(NeedType::Hunger), m_baseTiers);
		const auto			 eats		= [hungerTier](const ClaimHolder& holder) {
			  return holder.taskType == TaskType::FulfillNeed && holder.tier < kTierIdle && holder.tier <= hungerTier;
		};
		if (choice.itemDefNameId == 0) {
			const ClaimTarget bush{.kind = TargetKind::WorldKey, .key = choice.sourceKey};
			return choice.quantity == 0 ? m_claims.useHolder(bush, self).has_value() : m_claims.useHolderWhere(bush, self, eats).has_value();
		}
		// A pile or box: held when what eaters at least as urgent leave of it is short of his meal.
		const ClaimTarget target = choice.storageId != 0 ? ClaimTarget{.kind = TargetKind::Entity, .key = choice.storageId}
														 : ClaimTarget{.kind = TargetKind::WorldKey, .key = choice.sourceKey};
		const auto		  eaten	 = m_claims.sourceClaimedWhere(target, choice.itemDefNameId, self, eats);
		if (!eaten.holder.has_value()) {
			return false;
		}
		std::optional<uint32_t> live;
		if (choice.storageId != 0) {
			if (const auto* store = world->getComponent<Inventory>(static_cast<EntityID>(choice.storageId))) {
				live = store->getQuantity(m_registry.getDefName(choice.itemDefNameId));
			}
		} else if (const EntityID pile = m_loosePiles != nullptr ? m_loosePiles->find(*world, choice.itemDefNameId, choice.target) : kInvalidEntity;
				   pile != kInvalidEntity) {
			live = world->getComponent<ResourceStack>(pile)->quantity;
		}
		const uint32_t have = live.value_or(0U);
		return have < eaten.quantity + choice.quantity;
	}

	void AIDecisionSystem::takeSourceChoice(Task& task, std::uint8_t index) {
		const SourceChoice& choice = task.sourceChoices[index];
		task.targetPosition		   = choice.goal;
		task.obstructedTarget	   = choice.goal != choice.target ? std::optional<glm::vec2>(choice.target) : std::nullopt;
		task.needSourceKey		   = choice.sourceKey;
		task.needItemDefNameId	   = choice.itemDefNameId;
		task.needQuantity		   = choice.quantity;
		task.needSourceStorageId   = choice.storageId;
	}

	void AIDecisionSystem::failRoute(EntityID entity, Task& task, const Memory& memory, MovementTarget& movementTarget, RouteFailure why) {
		if (auto* navPath = world->getComponent<NavPath>(entity)) {
			navPath->valid = false;
		}
		movementTarget.active = false;
		task.awaitingSnap	  = false;
		// A verdict on every target the route was asked for, so none is asked about again until it may no
		// longer hold (SourceReachMemo::keep). Recorded under the epoch as it stands now, however long the
		// search took: the epoch moving during it says nothing of these targets for him. An answer asked
		// under a belief he has since left says nothing of them as he believes now. A wander point is
		// random: nothing to remember.
		auto*	   memo	   = world->getComponent<SourceReachMemo>(entity);
		const bool verdict = memo != nullptr && task.type != TaskType::Wander &&
							 (why == RouteFailure::NoStandPoint || (why == RouteFailure::NoKnownWay && task.routeBeliefVersion == memory.beliefVersion));
		if (verdict) {
			const SourceReachMemo::Key now{.connectivityEpoch = m_navSystem->connectivityEpoch(), .beliefVersion = memory.beliefVersion};
			if (task.sourceChoices.empty()) {
				memo->recordVerdict(verdictKeyOf(task), now, m_tick);
			}
			for (const SourceChoice& choice : task.sourceChoices) {
				memo->recordVerdict(verdictKey(choice.sourceKey, choice.target), now, m_tick);
			}
		}
		if (memo != nullptr && why == RouteFailure::NoKnownWay && task.type == TaskType::FulfillNeed) {
			std::uint8_t& widening = memo->roundWidening[needSlotOf(task)];
			widening			   = std::min<std::uint8_t>(static_cast<std::uint8_t>(widening + 1), kMaxRoundWidening);
		}
		task.sourceChoices.clear();
		setNavState(entity, task, NavState::CantFindWayTo);
		LOG_DEBUG(Engine, "[Nav] Entity %llu: no known way to (%.2f, %.2f), stopping", static_cast<unsigned long long>(entity),
				  task.targetPosition.x, task.targetPosition.y);
	}

	void AIDecisionSystem::dropRoute(Task& task) {
		if (task.routeTicket != 0 && m_navSystem != nullptr) {
			m_navSystem->cancelRoute(task.routeTicket);
		}
		task.routeTicket = 0;
	}

	void AIDecisionSystem::setNavState(EntityID entity, Task& task, NavState navState) {
		const bool wasFailed = task.navState == NavState::CantFindWayTo;
		task.navState		 = navState;
		// A failed route holds only the goods it carries (ClaimIndex::addCurrent): re-filing the task as the
		// rebuild would see it lets colonists deciding later this update take what it has not reached while
		// it stays failed, and see it taken again once a route succeeds.
		const bool failed = navState == NavState::CantFindWayTo;
		if (m_claimsBuilt && failed != wasFailed) {
			m_claims.append(entity, task);
		}
	}

	WorkGroup* AIDecisionSystem::workGroupOf(const EvaluatedOption& option) {
		// A Harvest goal has one group, its cut or its clear, so its id names it. A fetch has one per item
		// and kind, piles or pulls; the carry-in has none.
		const bool harvest = option.taskType == TaskType::Harvest;
		if (!harvest && (option.taskType != TaskType::Haul || option.haulFromInventory)) {
			return nullptr;
		}
		const bool pulls = option.haulSourceStorageId != 0;
		for (WorkGroup& group : m_workGroups) {
			const bool cuts = group.kind == WorkKind::Cut || group.kind == WorkKind::Clear;
			if (harvest ? cuts && group.goalId == option.harvestGoalId
						: !cuts && group.goalId == option.haulGoalId && group.item == option.haulItemDefNameId &&
							  (group.kind == WorkKind::Pull) == pulls) {
				return &group;
			}
		}
		return nullptr;
	}

	std::optional<double> AIDecisionSystem::sourceRegrowingUntil(uint32_t defNameId, glm::vec2 position) const {
		return onSourceRegrowth ? onSourceRegrowth(defNameId, position, m_simulationNow) : std::nullopt;
	}

	void AIDecisionSystem::handleChainInterruption(
		EntityID		entity,
		Task&			task,
		Inventory&		inventory,
		const Position& position,
		const EvaluatedOption& selected
	) {
		// The goods a haul or a cut carries for its root.
		const uint32_t	   itemId = chainItem(task);
		const std::string& item = m_registry.getDefName(itemId);

		// A need interrupting a haul or cut whose goods the colonist keeps leaves it as its suspended
		// record, so the goods stay committed to where they were going. Called on every path that keeps
		// them; a drop keeps none, and the pile it leaves is an ordinary source with no claims.
		const auto keepGoods = [&]() {
			if (selected.taskType != TaskType::FulfillNeed) {
				return;
			}
			auto record = suspendableWork(task, ecs::availableQuantity(inventory, item));
			if (!record) {
				return;
			}
			if (task.suspended) {
				m_claims.retire(task.suspended->instanceId);
			}
			task.suspended = std::move(record);
			m_claims.suspend(entity, task);
		};

		// A carry-in delivers what the colonist holds, so it needs no free hands: the goods ride along to it.
		const bool carriesIn = selected.taskType == TaskType::Haul && selected.haulFromInventory;
		if (carriesIn || !taskFirstActionNeedsHands(selected.taskType, selected.needType)) {
			keepGoods();
			return;
		}

		// New task needs hands - must handle carried item
		const auto entityId = static_cast<unsigned long long>(entity);

		// For Haul and Harvest tasks: the goods may be in hands
		if ((task.type == TaskType::Haul || task.type == TaskType::Harvest) && !item.empty()) {
			// Verify item is actually in hands before operating
			if (!inventory.isHolding(item)) {
				keepGoods(); // goods in the pack ride along
				return;
			}

			const auto* assetDef = m_registry.getDefinitionById(itemId);
			uint8_t		handsRequired = (assetDef != nullptr) ? assetDef->handsRequired : 1;

			if (handsRequired == 1) {
				// 1-handed item: try the belt (quick-draw slot) first, then the backpack. The item is
				// in hand, so a belt stow seats a copy on the belt and clears the hand.
				if (inventory.stowToBelt(item)) {
					inventory.putDown(item);
					LOG_INFO(Engine, "[AI] Entity %llu: chain interrupted, stowed %s to belt", entityId, item.c_str());
					keepGoods();
					return;
				}
				if (inventory.stowToBackpack(item)) {
					LOG_INFO(Engine, "[AI] Entity %llu: chain interrupted, stowed %s to backpack", entityId, item.c_str());
					keepGoods();
					return;
				}
				// Belt and backpack full - fall through to drop
				LOG_INFO(Engine, "[AI] Entity %llu: chain interrupted, dropping %s (belt and backpack full)", entityId, item.c_str());
			} else {
				LOG_INFO(Engine, "[AI] Entity %llu: chain interrupted, dropping %s (2-handed)", entityId, item.c_str());
			}

			auto dropped = inventory.putDown(item);
			if (dropped.has_value() && dropped->quantity > 0) {
				ecs::storeOrDrop(*world, dropped->defName, dropped->quantity, position.value, onDropResource, [this](EntityID box) { boxChanged(box); });
			}
		}
	}

	void AIDecisionSystem::setDownUnplacedFurniture(EntityID entity, Inventory& inventory, const Position& position, const EvaluatedOption& selected) {
		if (!inventory.carryingPackagedEntity.has_value()) {
			return;
		}
		const uint64_t carried = *inventory.carryingPackagedEntity;
		if (selected.taskType == TaskType::PlacePackaged && selected.placePackagedEntityId == carried) {
			return;
		}
		// Sleep and other hands-free needs keep it, and the placement resumes after.
		if (selected.taskType == TaskType::FulfillNeed && !selected.gathersFood && !taskFirstActionNeedsHands(selected.taskType, selected.needType)) {
			return;
		}
		// Set down as the packaged item it is, where he stands or on the nearest valid ground off a site:
		// the placement, once offered again, picks it up from there.
		const auto		packaged = static_cast<EntityID>(carried);
		const glm::vec2 setDownAt = onSetDownSpot ? onSetDownSpot(position.value) : position.value;
		if (auto* packagedPos = world->getComponent<Position>(packaged)) {
			packagedPos->value = setDownAt;
		}
		if (auto* state = world->getComponent<Packaged>(packaged)) {
			state->beingCarried = false;
		}
		m_sharedWork.packagesChanged();
		inventory.carryingPackagedEntity.reset();
		inventory.leftHand.reset();
		inventory.rightHand.reset();
		LOG_INFO(
			Engine, "[AI] Entity %llu: set down packaged entity %llu at (%.1f, %.1f)", static_cast<unsigned long long>(entity),
			static_cast<unsigned long long>(carried), setDownAt.x, setDownAt.y
		);
	}

	void AIDecisionSystem::putDownUnwantedArmful(Inventory& inventory, const Position& position, const DecisionTrace& trace) {
		if (inventory.carryingPackagedEntity.has_value() || !inventory.leftHand.has_value() ||
			!ecs::itemIsTwoHand(m_registry, inventory.leftHand->defName)) {
			return;
		}
		// An armful fills the carry, so every haul and cut would read no room: one that no root he knows
		// wants is put down where he stands, a pile any colonist can haul once a root wants it. A carry-in he
		// believes he cannot reach is not offered, so it wants nothing of him.
		const std::string name = inventory.leftHand->defName;
		const uint32_t	  item = m_registry.getDefNameId(name);
		const bool		  wanted = std::any_of(trace.options.begin(), trace.options.end(), [item](const EvaluatedOption& option) {
			   return option.taskType == TaskType::Haul && option.haulFromInventory && option.haulItemDefNameId == item &&
					  option.status == OptionStatus::Available;
		   });
		if (wanted) {
			return;
		}
		const auto dropped = inventory.putDown(name);
		if (dropped.has_value() && dropped->quantity > 0 && onDropResource) {
			onDropResource(dropped->defName, position.value.x, position.value.y, dropped->quantity, position.value);
			LOG_DEBUG(Engine, "[AI] Put down %u x %s nobody wants", dropped->quantity, dropped->defName.c_str());
		}
	}

	void AIDecisionSystem::boxChanged(EntityID box) {
		m_boxRooms.refresh(*world, m_registry, box);
		m_sharedWork.boxChanged();
		if (const GoalTask* root = GoalTaskRegistry::Get().getGoalByDestination(box)) {
			m_needs.refresh(*world, root->id);
		}
		m_foodBoxesBuilt = false;
	}

	void AIDecisionSystem::enterControl(EntityID entity) {
		if (world->getComponent<PlayerControlled>(entity) == nullptr) {
			world->addComponent<PlayerControlled>(entity);
		}
		// Abandon the current task and its suspended record. Their claims go with them, so the work
		// returns to the pool at once; carried items stay with the colonist, uncommitted.
		if (auto* task = world->getComponent<Task>(entity)) {
			m_claims.retire(task->instanceId);
			if (task->suspended) {
				m_claims.retire(task->suspended->instanceId);
			}
			task->discardSuspended();
			dropRoute(*task);
			task->clear();
			task->timeSinceEvaluation = 0.0F;
		}
		// He decides nothing while controlled, so his last trace would keep offering its work to the
		// task panel (ColonyGoalWork) until release.
		if (auto* trace = world->getComponent<DecisionTrace>(entity)) {
			trace->clear();
		}
		// Clear any in-progress action too. Unlike the normal task-switch path (which is gated so a
		// non-interruptable Eat/Drink/Toilet is never abandoned), taking control unconditionally
		// abandons the task, so a stale active Action must be cleared here -- otherwise ActionSystem
		// would later run it against whatever task the colonist picks up after release (it starts a
		// new action only when the current one is inactive), applying a mismatched effect.
		if (auto* action = world->getComponent<Action>(entity)) {
			action->clear();
		}
		if (auto* navPath = world->getComponent<NavPath>(entity)) {
			navPath->valid = false;
		}
		if (auto* mt = world->getComponent<MovementTarget>(entity)) {
			mt->active = false;
		}
	}

	void AIDecisionSystem::releaseControl(EntityID entity) {
		if (world->getComponent<PlayerControlled>(entity) != nullptr) {
			world->removeComponent<PlayerControlled>(entity);
		}
		// Clear the player-issued move task so the next update() re-selects autonomously; a
		// half-finished walk order shouldn't masquerade as an AI task.
		if (auto* task = world->getComponent<Task>(entity)) {
			dropRoute(*task);
			task->clear();
			task->timeSinceEvaluation = 0.0F;
		}
		if (auto* action = world->getComponent<Action>(entity)) {
			action->clear();
		}
		if (auto* navPath = world->getComponent<NavPath>(entity)) {
			navPath->valid = false;
		}
		if (auto* mt = world->getComponent<MovementTarget>(entity)) {
			mt->active = false;
		}
	}

	std::optional<glm::vec2> AIDecisionSystem::issuePlayerMoveOrder(EntityID entity, glm::vec2 worldGoal) {
		auto* position = world->getComponent<Position>(entity);
		auto* task = world->getComponent<Task>(entity);
		auto* movementTarget = world->getComponent<MovementTarget>(entity);
		if (position == nullptr || task == nullptr || movementTarget == nullptr) {
			return std::nullopt;
		}

		// Snap an unwalkable click (water, inside a wall, off the sim area) to the nearest pathable
		// point so a near-miss click still walks the colonist as close as it can get. With no
		// NavigationSystem (headless), take the raw goal -- there's no mesh to judge it against.
		float radius = 0.3F;
		if (const auto* agentRadius = world->getComponent<AgentRadius>(entity)) {
			radius = agentRadius->radiusMeters;
		}
		glm::vec2 goal = worldGoal;
		if (m_navSystem != nullptr && !m_navSystem->isValidPosition(goal)) {
			const auto snapped = m_navSystem->nearestPathablePoint(goal, radius);
			if (!snapped.has_value()) {
				return std::nullopt;  // nothing reachable near the click; leave the colonist be
			}
			goal = *snapped;
		}

		// Express the walk as an ordinary Moving task so the existing repath-on-discovery and
		// arrival hand-off carry it. type=None means ActionSystem does nothing on arrival -- the
		// colonist just stands at the destination. Leave movementTarget.speed alone (spawn-owned).
		dropRoute(*task);
		task->clear();
		task->type = TaskType::None;
		task->state = TaskState::Moving;
		task->navState = NavState::Traveling;
		task->targetPosition = goal;
		task->timeSinceEvaluation = 0.0F;
		movementTarget->target = goal;
		movementTarget->active = true;

		if (const Memory* memory = world->getComponent<Memory>(entity)) {
			// The answer is applied by update() ahead of the control gate, like any AI route. No known
			// way shows "can't get there" and waits for the player to re-click (the control gate skips
			// re-eval); a start in ground on its way waits and asks again once it lands.
			requestRoute(entity, *task, *position, *memory, *movementTarget, false);
		}
		return goal;
	}

} // namespace ecs
