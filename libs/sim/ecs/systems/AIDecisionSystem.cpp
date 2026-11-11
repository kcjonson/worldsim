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
#include <limits>
#include <numbers>
#include <span>

namespace ecs {

	namespace {

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
				case TaskType::Deconstruct:
					// Config-driven like Build: the Deconstruct action is defined in
					// action-types.xml (needsHands=true), so actionNeedsHands reads it.
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

		/// Does harvesting this definition yield something edible? Not every harvestable is food
		/// (WoodyBush yields Stick, Reed yields PlantFiber).
		[[nodiscard]] bool yieldsEdibleFood(const assets::AssetRegistry& registry, uint32_t defNameId) {
			const auto* def = registry.getDefinitionById(defNameId);
			return def != nullptr && def->capabilities.harvestable.has_value() &&
				   assets::isItemEdible(def->capabilities.harvestable->yieldDefName);
		}

		/// The memo key for a colonist's reach: what every remembered verdict, and a failed route, depends on.
		[[nodiscard]] SourceReachMemo::Key reachKey(const NavigationSystem::Reach& reach, const Memory& memory) {
			return {.regionId = reach.startRegionId,
					.regionGeneration = reach.startRegionGeneration,
					.beliefVersion = memory.beliefVersion,
					.reachRoot = reach.from.root,
					.startTri = reach.from.root < 0 ? reach.from.startTri : -1};
		}

		/// The key a failed route is remembered under in SourceReachMemo: the Memory key of a remembered
		/// source, else the target point itself (a toilet spot, a work site's stand point), quantized
		/// like Memory keys so the same point picked again maps to the same verdict.
		[[nodiscard]] std::uint64_t routeTargetKey(std::uint64_t sourceKey, const glm::vec2& target) {
			return sourceKey != 0 ? sourceKey : hashWorldEntity(target, 0);
		}

		/// Skill names for work types
		constexpr const char* kSkillFarming = "Farming";
		constexpr const char* kSkillCrafting = "Crafting";
		constexpr const char* kSkillConstruction = "Construction";
		constexpr const char* kSkillMedicine = "Medicine";

		/// Skill bonus calculation constants (from priority-config.md)
		constexpr float	  kSkillBonusMultiplier = 10.0F;
		constexpr int16_t kSkillBonusMax = 100;

		/// Calculate skill bonus for priority scoring
		/// @param skills The colonist's skills (may be nullptr if no Skills component)
		/// @param skillName The skill to look up
		/// @return {skillLevel, skillBonus} pair
		[[nodiscard]] std::pair<float, int16_t> calculateSkillBonus(const Skills* skills, const char* skillName) {
			if (skills == nullptr || skillName == nullptr) {
				return {0.0F, 0};
			}
			float	skillLevel = skills->getLevel(skillName);
			int16_t bonus = static_cast<int16_t>(std::min(skillLevel * kSkillBonusMultiplier, static_cast<float>(kSkillBonusMax)));
			return {skillLevel, bonus};
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

		/// Arbitration tier promotion thresholds. Named so the classifier rules mirror the spec
		/// table directly rather than hiding magic numbers.
		constexpr int	kTierCriticalNeed = 2; // a need past its critical threshold
		constexpr int	kTierActiveWorkOrder = 4; // committed/queued work + its provisioning chain
		constexpr int	kTierIdle = 7;			  // gather-food sentinel and Wander
		constexpr float kCriticalNeedThreshold = 10.0F; // needValue below this is critical

		/// Classify an option's arbitration tier: the config BASE tier for its task type, with a
		/// small set of named runtime promotions layered on (mirrors the spec's classification
		/// table exactly). Lower tier = higher priority.
		///
		///   FulfillNeed: needValue < 10%        -> tier 2 (critical need)
		///                gather-food sentinel    -> tier 7 (idle)   (needValue>=100 && threshold==0)
		///                else (actionable)       -> config base (5)
		///   Haul/Harvest: servesActiveWorkOrder  -> tier 4 (work order, incl. mid-chain provisioning)
		///                 else                    -> config base (6, covers stocking + opportunistic)
		///   PlacePackaged: serving a work order OR already carrying (in-transit) -> tier 4
		///                  else                   -> config base (6)
		///   Craft/Build/Deconstruct               -> config base (4)
		///   Wander                                -> config base (7)
		[[nodiscard]] int classifyTier(const EvaluatedOption& option) {
			const int baseTier = assets::PriorityConfig::Get().getTaskTier(taskTypeConfigName(option.taskType));

			switch (option.taskType) {
				case TaskType::FulfillNeed: {
					// Gather-food sentinel: needValue==100 && threshold==0 (a real hunger need always
					// has needValue<100 and a positive threshold). Idle filler, ranks at tier 7.
					const bool gatherFoodSentinel = option.needValue >= 100.0F && option.threshold == 0.0F;
					if (gatherFoodSentinel) {
						return kTierIdle;
					}
					// A satisfied need needs no action; sort it to the idle tier so it can't outrank
					// real work in the trace (it's filtered from selection anyway by its status).
					if (option.status == OptionStatus::Satisfied) {
						return kTierIdle;
					}
					if (option.needValue < kCriticalNeedThreshold) {
						return kTierCriticalNeed;
					}
					return baseTier; // actionable need
				}
				case TaskType::Haul:
				case TaskType::Harvest:
					return option.servesActiveWorkOrder ? kTierActiveWorkOrder : baseTier;
				case TaskType::PlacePackaged:
					// Serving a work order, or already carrying the item (the in-transit case: old
					// needValue>100 sentinel set by evaluatePlacePackagedOptions). Either way it's
					// committed delivery -> tier 4, so a carrying colonist isn't rerouted mid-carry.
					return (option.servesActiveWorkOrder || option.needValue > 100.0F) ? kTierActiveWorkOrder : baseTier;
				case TaskType::Craft:
				case TaskType::Build:
				case TaskType::Deconstruct:
				case TaskType::Wander:
				case TaskType::None:
					return baseTier;
			}
			return baseTier;
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
						   option.haulTargetStorageId == currentTask.haulTargetStorageId;
				case TaskType::PlacePackaged:
					return option.placePackagedEntityId == currentTask.placePackagedEntityId;
				case TaskType::Harvest:
					return option.harvestTargetEntityId == currentTask.harvestTargetEntityId &&
						   option.harvestGoalId == currentTask.harvestGoalId;
				case TaskType::Build:
				case TaskType::Deconstruct:
					return option.buildBlueprintEntityId == currentTask.buildBlueprintEntityId;
				default:
					return false;
			}
		}

		/// The cargo `task` hands to `selected` when it is the next leg of the same gathering: a cut, or a
		/// carry-in, for the root and item the task carries goods toward.
		[[nodiscard]] std::optional<SuspendedWork> nextGatheringLeg(const EvaluatedOption& selected, const Task& task) {
			const auto& goals = GoalTaskRegistry::Get();
			if (selected.taskType == TaskType::Harvest) {
				return handOverCargo(task, goals.rootOf(selected.harvestGoalId), selected.harvestYieldDefNameId);
			}
			if (selected.taskType == TaskType::Haul && selected.haulFromInventory) {
				return handOverCargo(task, goals.rootOf(selected.haulGoalId), selected.haulItemDefNameId);
			}
			return std::nullopt;
		}

		/// Does the task hold goods of its chain: a haul or placement past its pickup, or a cut carrying
		/// what earlier cuts landed?
		[[nodiscard]] bool carriesChainGoods(const Task& task) {
			if (task.chainId.has_value() && task.chainStep > 0) {
				return true;
			}
			return task.type == TaskType::Harvest &&
				   std::any_of(task.claims.begin(), task.claims.end(), [](const Claim& claim) { return claim.kind() == ClaimKind::Inbound; });
		}

		/// Populate the (tier, score) arbitration key for an evaluated option.
		/// Sets option.tier (classifyTier) and the within-tier score breakdown (distanceFactor,
		/// taskAgeBonus, hysteresisBonus), then composes option.score. skillBonus is set by the
		/// evaluators. The chain bonus is GONE: a mid-chain provisioning task is tier 4 instead.
		/// @param world ECS world (to read a stocking haul's destination box priority); may be null
		/// @param option The option to populate (skillBonus already set by its evaluator)
		/// @param currentTask Current colonist task (for the hysteresis margin)
		/// @param currentHaulItemId The current task's haul item, interned (0 when not hauling)
		/// @param currentTime Current game time (for task age calculation)
		void populatePriorityBonuses(World* world, EvaluatedOption& option, const Task& currentTask, uint32_t currentHaulItemId, float currentTime) {
			const auto& priorityConfig = assets::PriorityConfig::Get();

			// Categorical tier first: it is compared before any score term.
			option.tier = classifyTier(option);

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
			// by more than this to win (prevents thrashing). Only matters within a tier.
			if (isOptionCurrentTask(option, currentTask, currentHaulItemId)) {
				option.hysteresisBonus = priorityConfig.getInProgressBonus();
			}

			// Storage-priority within-tier bias: a storage-stocking haul (a pull from a lower-priority
			// box, or a stocking carry-in) is ordered within tier 6 by its DESTINATION box's priority
			// for the item. Rank the dest box's StoragePriority (Low=0..Critical=3) and weight it to
			// dominate distance, so a Critical box farther away beats a Low box nearer. Tier is compared
			// first, so this never lets stocking preempt a tier-5 need or tier-4 work order. 0 for every
			// non-stocking option.
			if (option.servesStorageStocking && option.haulTargetStorageId != 0 && world != nullptr) {
				const auto destEntity = static_cast<EntityID>(option.haulTargetStorageId);
				const auto* destConfig = world->getComponent<StorageConfiguration>(destEntity);
				if (destConfig != nullptr) {
					const auto& reg = assets::AssetRegistry::Get();
					const auto* itemDef = reg.getDefinitionById(option.haulItemDefNameId);
					if (itemDef != nullptr) {
						const StoragePriority destPriority = destConfig->getPriorityFor(itemDef->defName, itemDef->category);
						const auto rank = static_cast<int16_t>(static_cast<uint8_t>(destPriority)); // 0..3
						option.storagePriorityBias = static_cast<int16_t>(rank * priorityConfig.getStoragePriorityWeight());
					}
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

			[[nodiscard]] uint32_t available(uint64_t root, uint32_t item, NeedBasis basis) const {
				return rootAvailable(needs.get(world, root, item, basis), claims, root, item, self);
			}
			[[nodiscard]] uint32_t toAcquire(uint64_t root, uint32_t item, NeedBasis basis) const {
				const uint32_t need = available(root, item, basis);
				const uint32_t carried = need > 0 ? ecs::availableQuantity(inventory, registry.getDefName(item)) : 0U;
				return need > carried ? need - carried : 0U;
			}

			/// Units of the item a remembered pile still offers: its live count less what others will take.
			/// A pile the world can't find (taken since it was seen, or a world with no pile index) has no
			/// count to read; it offers the wanted amount while nobody else claims it, so one colonist goes
			/// to look and the stale memory is corrected on arrival.
			[[nodiscard]] uint32_t pileLeft(uint64_t key, uint32_t item, glm::vec2 position, uint32_t wanted) const {
				const uint32_t claimed = claims.sourceClaimed({.kind = TargetKind::WorldKey, .key = key}, item, self);
				const EntityID pile = piles != nullptr ? piles->find(world, item, position) : kInvalidEntity;
				if (pile == kInvalidEntity) {
					return claimed > 0 ? 0U : wanted;
				}
				const uint32_t live = world.getComponent<ResourceStack>(pile)->quantity;
				return live > claimed ? live - claimed : 0U;
			}

			/// Units of the item a storage box can still take, net of every other task's inbound: the room
			/// in its own stacks (existing ones, and the stacks its inbound already opens) plus the free
			/// slots no inbound item has been promised, in stacks of the item. Three hauls of three items
			/// can't all count on one last slot.
			[[nodiscard]] uint32_t storageRoom(EntityID box, uint32_t item) const {
				const auto* store = world.getComponent<Inventory>(box);
				if (store == nullptr) {
					return 0;
				}
				const auto stackRoom = [&](uint32_t id) {
					const std::string& name = registry.getDefName(id);
					return InboundStackRoom{.partialRoom = store->partialStackRoom(name), .stackSize = Inventory::itemStackSize(name)};
				};
				const InboundStackRoom own = stackRoom(item);
				const uint64_t		   stack = own.stackSize == 0 ? UINT32_MAX : own.stackSize;
				const uint64_t		   inbound = claims.inboundItems(box, item, self);
				const uint64_t		   ownSlots = inbound > own.partialRoom ? (inbound - own.partialRoom + stack - 1) / stack : 0;
				const uint64_t		   ownRoom = own.partialRoom + ownSlots * stack - inbound;
				const uint32_t		   promised = claims.inboundSlots(box, self, stackRoom);
				const uint64_t		   free = store->freeSlots() > promised ? store->freeSlots() - promised : 0U;
				return static_cast<uint32_t>(std::min<uint64_t>(ownRoom + free * stack, UINT32_MAX));
			}
		};

		/// The nearest source of one (goal, item) that other colonists' claims take whole, kept while its
		/// sources are visited: the trace shows it as one Reserved row, and only when no source offered
		/// is nearer, so it is bounded by goals and items rather than by remembered sources.
		template <typename Source>
		struct NearestReserved {
			float				  offeredTrip = std::numeric_limits<float>::infinity();
			float				  trip = std::numeric_limits<float>::infinity();
			std::optional<Source> source;
			EntityID			  holder = kInvalidEntity;

			void offered(float sourceTrip) { offeredTrip = std::min(offeredTrip, sourceTrip); }
			void reserved(float sourceTrip, const Source& held, EntityID holdBy = kInvalidEntity) {
				if (sourceTrip < trip) {
					trip = sourceTrip;
					source = held;
					holder = holdBy;
				}
			}
			[[nodiscard]] bool shown() const { return source.has_value() && trip < offeredTrip; }
		};

		/// What one pickup of an item would lift now (InventoryMass's pickupUnitsThatFit, the rule the
		/// Pickup and Withdraw effects size their take with), with what the colonist carries summed once,
		/// on the first item asked about in a decision. Zero means going to get the item gains nothing.
		class PickupRoom {
		  public:
			PickupRoom(const Inventory& inventory, const assets::AssetRegistry& registry)
				: inventory(inventory),
				  registry(registry) {}

			[[nodiscard]] uint32_t operator()(const std::string& itemDefName) {
				if (!carriedKg.has_value()) {
					carriedKg = ecs::carriedCargoMassKg(inventory, registry);
				}
				return ecs::pickupUnitsThatFit(inventory, registry, itemDefName, *carriedKg);
			}

		  private:
			const Inventory&			 inventory;
			const assets::AssetRegistry& registry;
			std::optional<float>		 carriedKg;
		};

		/// Evaluate haul options that go and get goods: loose piles and lower-priority boxes for a
		/// storage umbrella, and remembered loose stock for a craft's fetch child. Each is sized to
		/// what the root still wants (RootSupply::toAcquire) and one trip's carry. Goods already
		/// carried are offered by evaluateCarryInOptions.
		void evaluateHaulOptions(
			World*						 world,
			const assets::AssetRegistry& registry,
			WorkSources&				 sources,
			ColonyStorageKnowledge&		 storageKnowledge,
			const RootSupply&			 supply,
			PickupRoom&					 pickupRoom,
			const Inventory&			 inventory,
			const glm::vec2&			 position,
			DecisionTrace&				 trace
		) {
			auto& goalRegistry = GoalTaskRegistry::Get();

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

			const auto pushLooseHaul = [&](const GoalTask& goal, const CarrySource& source, uint32_t itemDefNameId, uint32_t quantity,
										   bool craftFetch, EntityID reservedBy) {
				const KnownWorldEntity& looseItem = *source.entity;
				EvaluatedOption			option;
				option.taskType = TaskType::Haul;
				option.needType = NeedType::Count;
				option.needValue = 100.0F;
				option.threshold = 0.0F;
				option.targetPosition = looseItem.position;
				option.targetDefNameId = looseItem.defNameId;
				option.distanceToTarget = glm::distance(position, looseItem.position) + glm::distance(looseItem.position, goal.destinationPosition);
				option.haulItemDefNameId = itemDefNameId;
				option.haulQuantity = quantity;
				option.haulSourcePosition = looseItem.position;
				option.haulTargetStorageId = static_cast<uint64_t>(goal.destinationEntity);
				option.haulTargetPosition = goal.destinationPosition;
				option.haulGoalId = goal.id;
				// Fetching loose stock to provision a started craft is committed work (tier 4), so a far
				// pile can't strand a half-provisioned craft.
				option.servesActiveWorkOrder = craftFetch;
				// Tiebreak on (goal, source item) so two equidistant loose items for the same goal resolve
				// deterministically rather than by memory's hash order.
				option.tiebreakId = goal.id ^ (source.key << 1);
				option.status = reservedBy == kInvalidEntity ? OptionStatus::Available : OptionStatus::Reserved;
				option.reservedBy = reservedBy;
				option.reason = craftFetch ? "Fetching {} for crafting" : "Hauling {} to storage";
				trace.options.push_back(std::move(option));
			};

			// Every remembered pile of the item, each sized to what it still has for this colonist, so a
			// fetch never claims more than its source can give.
			const auto pushPileHauls = [&](const GoalTask& goal, uint32_t itemDefNameId, uint32_t quantity, bool craftFetch) {
				NearestReserved<CarrySource> reserved;
				for (const CarrySource& source : sources.carryablesOf(itemDefNameId)) {
					const glm::vec2 at = source.entity->position;
					const float		trip = glm::distance(position, at) + glm::distance(at, goal.destinationPosition);
					const uint32_t	left = supply.pileLeft(source.key, itemDefNameId, at, quantity);
					if (left == 0) {
						reserved.reserved(trip, source);
						continue;
					}
					reserved.offered(trip);
					pushLooseHaul(goal, source, itemDefNameId, std::min(quantity, left), craftFetch, kInvalidEntity);
				}
				if (reserved.shown()) {
					const auto holder =
						supply.claims.sourceHolder({.kind = TargetKind::WorldKey, .key = reserved.source->key}, itemDefNameId, supply.self);
					if (holder.has_value()) {
						pushLooseHaul(goal, *reserved.source, itemDefNameId, quantity, craftFetch, holder->colonist);
					}
				}
			};

			// The pull from strictly lower-priority boxes for one item a storage umbrella names, wanting
			// `destRoom` more of it (storage priority, Phase 2). The strict-< gate is the load-bearing
			// invariant: items flow monotonically up the 4-level ladder (Low<Medium<High<Critical), never
			// sideways or down, which makes A->B->A cycles impossible.
			const auto emitPulls = [&](const GoalTask& goal, const StorageConfiguration& destConfig, uint32_t acceptedId, uint32_t destRoom) {
				const auto& itemDefName = registry.getDefName(acceptedId);
				const auto* itemDef = registry.getDefinitionById(acceptedId);
				if (itemDef == nullptr) {
					return;
				}
				const EntityID			   destEntity = goal.destinationEntity;
				const assets::ItemCategory cat = itemDef->category;
				const StoragePriority	   destPriority = destConfig.getPriorityFor(itemDefName, cat);
				const uint32_t			   perTrip = ecs::cargoUnitsPerTrip(registry, itemDefName, inventory.carryCapacityKg);
				if (perTrip == 0) {
					return; // colonist can't carry a single unit of this item
				}
				std::optional<bool> roomNow; // asked once a source box qualifies

				// The source is the box: walk to it, Withdraw, then the deposit leg stocks the dest.
				const auto pushPull = [&](EntityID srcEntity, glm::vec2 srcPos, uint32_t qty, EntityID reservedBy) {
					EvaluatedOption pullOption;
					pullOption.taskType = TaskType::Haul;
					pullOption.needType = NeedType::Count;
					pullOption.needValue = 100.0F;
					pullOption.threshold = 0.0F;
					pullOption.targetPosition = srcPos;
					pullOption.targetDefNameId = acceptedId;
					pullOption.distanceToTarget = glm::distance(position, srcPos) + glm::distance(srcPos, goal.destinationPosition);
					pullOption.haulItemDefNameId = acceptedId;
					pullOption.haulQuantity = qty;
					pullOption.haulSourcePosition = srcPos;
					pullOption.haulSourceStorageId = static_cast<uint64_t>(srcEntity);
					pullOption.haulTargetStorageId = static_cast<uint64_t>(destEntity);
					pullOption.haulTargetPosition = goal.destinationPosition;
					pullOption.haulGoalId = goal.id;
					// A pull tops up a box toward its config: opportunistic stocking (tier 6), ordered
					// within-tier by the dest box priority (populatePriorityBonuses).
					pullOption.servesStorageStocking = true;
					// Deterministic tiebreak on (umbrella goal, source box), independent of view order.
					pullOption.tiebreakId = goal.id ^ (static_cast<uint64_t>(srcEntity) << 1);
					pullOption.status = reservedBy == kInvalidEntity ? OptionStatus::Available : OptionStatus::Reserved;
					pullOption.reservedBy = reservedBy;
					pullOption.reason = "Pulling {} from lower-priority storage";
					trace.options.push_back(std::move(pullOption));
				};
				struct PullSource {
					EntityID  box;
					glm::vec2 position;
				};
				NearestReserved<PullSource> reserved;

				for (auto [srcEntity, srcConfig, srcInv, srcPos] : world->view<StorageConfiguration, Inventory, Position>()) {
					if (srcEntity == destEntity) {
						continue; // never pull from the destination itself
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
					if (!storageKnowledge.knows(*world, registry, srcEntity)) {
						continue;
					}
					// Don't drain the source below ITS OWN configured minimum for this item, counting what
					// other pulls will take from it, so two pulls can't drain it below that together.
					const uint32_t srcMin = srcConfig.getMinAmountFor(itemDefName, cat);
					const uint32_t drainable = srcHas > srcMin ? srcHas - srcMin : 0U;
					if (drainable == 0) {
						continue;
					}
					const uint32_t claimed =
						supply.claims.sourceClaimed({.kind = TargetKind::Entity, .key = static_cast<uint64_t>(srcEntity)}, acceptedId, supply.self);
					const float trip = glm::distance(position, srcPos.value) + glm::distance(srcPos.value, goal.destinationPosition);
					if (claimed >= drainable) {
						reserved.reserved(trip, PullSource{srcEntity, srcPos.value});
						continue;
					}
					const uint32_t qty = std::min({drainable - claimed, destRoom, perTrip});

					if (!roomNow.has_value()) {
						roomNow = pickupRoom(itemDefName) > 0;
					}
					if (!*roomNow) {
						pushNoRoom(goal, acceptedId, "Pulling from lower-priority storage ({}: no room to carry it)");
						break; // one line says it for every source box
					}

					reserved.offered(trip);
					pushPull(srcEntity, srcPos.value, qty, kInvalidEntity);
				}
				if (reserved.shown() && roomNow.value_or(true)) {
					const auto holder = supply.claims.sourceHolder(
						{.kind = TargetKind::Entity, .key = static_cast<uint64_t>(reserved.source->box)}, acceptedId, supply.self
					);
					if (holder.has_value()) {
						pushPull(reserved.source->box, reserved.source->position, perTrip, holder->colonist);
					}
				}
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
						const auto& itemDefName = registry.getDefName(*acceptedIt);
						// Only fetch what the pickup can actually lift; one that adds 0 would be
						// re-issued every tick. The craft stays pending until the colonist has room.
						if (pickupRoom(itemDefName) == 0) {
							pushNoRoom(goal, *acceptedIt, "Fetching for crafting ({}: no room to carry it)");
							continue;
						}
						const uint32_t quantity = std::min(need, ecs::cargoUnitsPerTrip(registry, itemDefName, inventory.carryCapacityKg));
						pushPileHauls(goal, *acceptedIt, quantity, /*craftFetch=*/true);
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

				// What the box still wants of an item and has room for, net of what is on its way: asked
				// once per item, for its loose piles and its pulls alike.
				const auto wantOf = [&](uint32_t itemDefNameId) {
					const uint32_t want = supply.toAcquire(goal.id, itemDefNameId, NeedBasis::Stock);
					return want > 0 ? std::min(want, supply.storageRoom(goal.destinationEntity, itemDefNameId)) : 0U;
				};
				// Loose piles the colonist remembers of one accepted item. The need and carry gates come
				// first, so a full box or a full pack visits none of them.
				const auto emitLooseHauls = [&](uint32_t itemDefNameId, uint32_t want) {
					const auto& itemDefName = registry.getDefName(itemDefNameId);
					if (pickupRoom(itemDefName) == 0) {
						pushNoRoom(goal, itemDefNameId, "Hauling to storage ({}: no room to carry it)");
						return;
					}
					const uint32_t quantity = std::min(want, ecs::cargoUnitsPerTrip(registry, itemDefName, inventory.carryCapacityKg));
					pushPileHauls(goal, itemDefNameId, quantity, /*craftFetch=*/false);
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
					const uint32_t want = wantOf(*acceptedIt);
					if (want == 0) {
						continue;
					}
					if (sources.knownCarryable(*acceptedIt) != nullptr) {
						emitLooseHauls(*acceptedIt, want);
					}
					emitPulls(goal, *destConfig, *acceptedIt, want);
				}
				// A category goal also takes loose piles of any other carryable of its category.
				if (goal.acceptedCategory != assets::ItemCategory::None) {
					for (uint32_t defNameId : sources.knownCarryableDefs()) {
						if (sources.knownCarryable(defNameId)->category != goal.acceptedCategory ||
							std::find(named.begin(), named.end(), defNameId) != named.end()) {
							continue;
						}
						if (const uint32_t want = wantOf(defNameId); want > 0) {
							emitLooseHauls(defNameId, want);
						}
					}
				}
			});
		}

		/// Carry-in from possession (task-claims.md): for every root goal that still wants an item the
		/// colonist carries, offer to deliver it, sized to what the root still wants net of others'
		/// in-flight. One path for crafts, construction sites, and storage boxes alike, and for goods
		/// left over after a cut, an interruption, or a switch. Tools are equipment, and the colonist's
		/// food stays its own provisions rather than being stocked into a box. Costs nothing unless
		/// the colonist carries cargo; what it carries is read once, into `carried`, reused storage.
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
					if (storage && kind.edible) {
						continue;
					}
					uint32_t quantity = std::min(kind.quantity, supply.available(root.id, kind.item, NeedBasis::Stock));
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
					option.haulTargetStorageId = static_cast<uint64_t>(root.destinationEntity);
					option.haulTargetPosition = root.destinationPosition;
					option.haulGoalId = root.id;
					option.haulFromInventory = true;
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
		/// @param registry Asset registry for capability and definition lookups
		/// @param sources The colonist's remembered harvestables, by yield
		/// @param supply What each root still wants, for this colonist
		/// @param pickupRoom What a pickup of the yield would lift now
		/// @param position Colonist position
		/// @param skills Optional skills component for skill bonus calculation
		/// @param trace Output decision trace
		void evaluateHarvestOptions(
			const assets::AssetRegistry& registry,
			WorkSources&				 sources,
			const RootSupply&			 supply,
			PickupRoom&					 pickupRoom,
			const glm::vec2&			 position,
			const Skills*				 skills,
			DecisionTrace&				 trace
		) {
			auto& goalRegistry = GoalTaskRegistry::Get();

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
				const bool roomToCarry = clearing || pickupRoom(registry.getDefName(goal->yieldDefNameId)) > 0;
				// What it could cut, before asking what the root needs: most goals have no known source.
				// With no room to carry the yield no source is listed (WorkSources gates on the fit), so the
				// need is asked to show why the cut is not taken.
				const auto candidates = clearing		? sources.clearableHarvestables()
										: roomToCarry ? sources.harvestablesYielding(goal->yieldDefNameId)
													  : std::span<const HarvestSource>{};
				if (roomToCarry && candidates.empty()) {
					return;
				}
				if (!clearing && supply.toAcquire(root->id, goal->yieldDefNameId, harvestBasis(*root)) == 0) {
					return;
				}
				if (!clearing && root->owner == GoalOwner::StorageGoalSystem && supply.storageRoom(root->destinationEntity, goal->yieldDefNameId) == 0) {
					return; // every slot it has left is promised to goods already on their way
				}
				if (!roomToCarry) {
					EvaluatedOption noRoom;
					noRoom.taskType = TaskType::Harvest;
					noRoom.needType = NeedType::Count;
					noRoom.needValue = 100.0F;
					noRoom.threshold = 0.0F;
					noRoom.harvestGoalId = goal->id;
					noRoom.harvestYieldDefNameId = goal->yieldDefNameId;
					noRoom.tiebreakId = goal->id;
					noRoom.status = OptionStatus::NoRoom;
					noRoom.reason = "Cutting for {1} (no room to carry it)";
					trace.options.push_back(noRoom);
					return;
				}

				// A Harvest that feeds a Craft or Build goal provisions an active work order: tier 4, so a
				// far harvestable can't strand a started craft or construction. One owned by
				// StorageGoalSystem is stocking: above Wander but strictly below work orders.
				const bool servesWorkOrder = root->type == TaskType::Craft || root->type == TaskType::Build;
				const bool servesStocking = goal->owner == GoalOwner::StorageGoalSystem;
				const bool continuesGathering = !clearing && claimedCargo(supply.task, root->id, goal->yieldDefNameId) > 0;

				// A clear is served by what blocks its footprint, the same test the build gate applies; any
				// other goal by the harvestables that yield its item.
				// Score by the full provisioning round-trip (colonist -> harvestable -> destination), not just
				// colonist -> harvestable, so the source nearest where the yield is needed (the crafting
				// station / storage box) wins. Matches the haul-fetch source scoring above; without it a
				// colonist chops the tree nearest itself even when one sits beside the station it provisions.
				const auto tripOf = [&](const KnownWorldEntity& known) {
					return glm::distance(position, known.position) + glm::distance(known.position, goal->destinationPosition);
				};
				const auto pushHarvest = [&](const HarvestSource& source, EntityID reservedBy) {
					const KnownWorldEntity& knownEntity = *source.entity;
					const uint64_t			key = source.key;
					const uint32_t			yieldDefNameId = source.yieldDefNameId;
					const float				tripDistance = tripOf(knownEntity);

					EvaluatedOption harvestOption;
					harvestOption.taskType = TaskType::Harvest;
					harvestOption.needType = NeedType::Count;
					harvestOption.needValue = 100.0F;
					harvestOption.threshold = 0.0F;
					harvestOption.targetPosition = knownEntity.position;
					harvestOption.targetDefNameId = knownEntity.defNameId;
					harvestOption.distanceToTarget = tripDistance;
					harvestOption.harvestTargetEntityId = key; // the Memory key, not an ECS id
					harvestOption.harvestGoalId = goal->id;
					harvestOption.harvestYieldDefNameId = yieldDefNameId;
					// Tiebreak on (goal, target) so two equidistant harvestables feeding the same
					// goal still resolve deterministically, not by memory's hash order.
					harvestOption.tiebreakId = goal->id ^ (key << 1);
					harvestOption.servesActiveWorkOrder = servesWorkOrder;
					harvestOption.servesStorageStocking = servesStocking;
					harvestOption.continuesGathering = continuesGathering;
					harvestOption.status = reservedBy == kInvalidEntity ? OptionStatus::Available : OptionStatus::Reserved;
					harvestOption.reservedBy = reservedBy;

					// Harvesting uses Farming skill
					auto [farmSkillLevel, farmSkillBonus] = calculateSkillBonus(skills, kSkillFarming);
					harvestOption.skillLevel = farmSkillLevel;
					harvestOption.skillBonus = farmSkillBonus;

					harvestOption.reason = clearing ? "Clearing {} from build site" : "Cutting {} for {}";

					trace.options.push_back(harvestOption);
				};

				// A source another colonist is cutting is its alone: Use is exclusive, so two colonists never
				// cut one plant. Several may still clear one site, each its own plant.
				NearestReserved<HarvestSource> reserved;
				for (const HarvestSource& source : candidates) {
					if (clearing && !goal->clearFootprint->blockedBy(registry, source.entity->defNameId, source.entity->position)) {
						continue;
					}
					const float trip = tripOf(*source.entity);
					if (const auto holder = supply.claims.useHolder({.kind = TargetKind::WorldKey, .key = source.key}, supply.self)) {
						reserved.reserved(trip, source, holder->colonist);
						continue;
					}
					reserved.offered(trip);
					pushHarvest(source, kInvalidEntity);
				}
				if (reserved.shown()) {
					pushHarvest(*reserved.source, reserved.holder);
				}
			});
		}

		/// Evaluate build options by querying Build goals from GoalTaskRegistry.
		/// Goal-driven: ConstructionSystem emits a Build goal once a blueprint's materials are
		/// staged. We surface it only while the blueprint is actually UnderConstruction (the
		/// phase ActionSystem's Build action requires), targeting the goal's work slot.
		void evaluateBuildOptions(
			World*			 world,
			const glm::vec2& position,
			const Skills*	 skills,
			DecisionTrace&	 trace
		) {
			if (world == nullptr) {
				return;
			}
			auto& goalRegistry = GoalTaskRegistry::Get();

			goalRegistry.forEachGoalOfType(TaskType::Build, [&](const GoalTask& buildGoal) {
				const GoalTask* goal = &buildGoal;
				if (goal->status != GoalStatus::Available) {
					return;
				}
				const auto blueprintEntity = static_cast<EntityID>(goal->destinationEntity);
				const auto* blueprint = world->getComponent<StructureBlueprint>(blueprintEntity);
				if (blueprint == nullptr ||
					blueprint->phase != StructureBlueprint::BuildPhase::UnderConstruction) {
					return; // not ready to build (cleared/awaiting materials/complete)
				}

				EvaluatedOption buildOption;
				buildOption.taskType = TaskType::Build;
				buildOption.needType = NeedType::Count;
				buildOption.needValue = 100.0F;
				buildOption.threshold = 0.0F;
				buildOption.targetPosition = goal->destinationPosition;
				buildOption.distanceToTarget = glm::distance(position, goal->destinationPosition);
				buildOption.buildBlueprintEntityId = goal->destinationEntity;
				buildOption.tiebreakId = goal->id;
				// A blueprint reaches this option only when fully provisioned and UnderConstruction:
				// the build is committed work. Floor its priority above idle Wander so a far site's
				// distance penalty can't drop it below Wander and strand the ready foundation
				// (the abandonment bug: provision 18/18 wood, then wander off and never build).
				buildOption.servesActiveWorkOrder = true;
				buildOption.status = OptionStatus::Available;

				auto [buildSkillLevel, buildSkillBonus] = calculateSkillBonus(skills, kSkillConstruction);
				buildOption.skillLevel = buildSkillLevel;
				buildOption.skillBonus = buildSkillBonus;
				buildOption.reason = "Building structure";

				trace.options.push_back(buildOption);
			});
		}

		/// Evaluate deconstruct options by querying Deconstruct goals from GoalTaskRegistry.
		/// Mirror of evaluateBuildOptions: ConstructionSystem emits a Deconstruct goal for a
		/// demolishing structure once its dependents are cleared (the cascade gate). We surface it
		/// only while the blueprint still has work to undo (workDone > 0, the bound ActionSystem's
		/// Deconstruct action counts down). Deconstruct is Construction work, so it uses the same
		/// skill and priority tier as Build.
		void evaluateDeconstructOptions(World* world, const glm::vec2& position, const Skills* skills, DecisionTrace& trace) {
			if (world == nullptr) {
				return;
			}
			auto& goalRegistry = GoalTaskRegistry::Get();

			goalRegistry.forEachGoalOfType(TaskType::Deconstruct, [&](const GoalTask& deconstructGoal) {
				const GoalTask* goal = &deconstructGoal;
				if (goal->status != GoalStatus::Available) {
					return;
				}
				const auto	blueprintEntity = static_cast<EntityID>(goal->destinationEntity);
				const auto* blueprint = world->getComponent<StructureBlueprint>(blueprintEntity);
				if (blueprint == nullptr || blueprint->workDone <= 0.0F) {
					return; // nothing to tear down (already removed, or no work invested)
				}

				EvaluatedOption deconstructOption;
				deconstructOption.taskType = TaskType::Deconstruct;
				deconstructOption.needType = NeedType::Count;
				deconstructOption.needValue = 100.0F;
				deconstructOption.threshold = 0.0F;
				deconstructOption.targetPosition = goal->destinationPosition;
				deconstructOption.distanceToTarget = glm::distance(position, goal->destinationPosition);
				deconstructOption.buildBlueprintEntityId = goal->destinationEntity;
				deconstructOption.tiebreakId = goal->id;
				// Committed construction work (mirrors Build): floor above Wander so a far site's
				// distance penalty can't strand an in-progress demolish.
				deconstructOption.servesActiveWorkOrder = true;
				deconstructOption.status = OptionStatus::Available;

				auto [deconstructSkillLevel, deconstructSkillBonus] = calculateSkillBonus(skills, kSkillConstruction);
				deconstructOption.skillLevel = deconstructSkillLevel;
				deconstructOption.skillBonus = deconstructSkillBonus;
				deconstructOption.reason = "Deconstructing structure";

				trace.options.push_back(deconstructOption);
			});
		}

		/// Evaluate place packaged options for furniture delivery
		/// @param world ECS world for entity queries
		/// @param position Colonist position
		/// @param inventory Colonist inventory (to check if carrying)
		/// @param trace Output decision trace
		void evaluatePlacePackagedOptions(
			World* world, const RootSupply& supply, const glm::vec2& position, const Inventory& inventory, DecisionTrace& trace
		) {
			// Find packaged items with targetPosition set (awaiting colonist delivery)
			for ([[maybe_unused]] auto [packagedEntity, packagedPos, packaged, appearance] : world->view<Position, Packaged, Appearance>()) {
				// Only consider items with a target position set
				if (!packaged.targetPosition.has_value()) {
					continue;
				}

				// Skip items being carried by a DIFFERENT colonist
				if (packaged.beingCarried) {
					bool thisColonistCarrying = inventory.carryingPackagedEntity.has_value() &&
												inventory.carryingPackagedEntity.value() == static_cast<uint64_t>(packagedEntity);
					if (!thisColonistCarrying) {
						continue;
					}
				}

				const auto& targetPos = packaged.targetPosition.value();
				bool		isCarryingThis = inventory.carryingPackagedEntity.has_value();

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
					placeOption.needValue = 150.0F; // Higher priority than most needs
				} else {
					// Phase 1: Need to pick up - go to source
					placeOption.targetPosition = packagedPos.value;
					placeOption.distanceToTarget = glm::distance(position, packagedPos.value);
				}

				placeOption.placePackagedEntityId = static_cast<uint64_t>(packagedEntity);
				placeOption.placeSourcePosition = packagedPos.value;
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
				trace.options.push_back(placeOption);
			}
		}

	} // namespace

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
		// Only the regrowth callback reports a regrowing source, so without it no food wait is set and
		// the time goes unread; a world that wires it has a TimeSystem.
		m_simulationNow = onSourceRegrowth ? simulationSeconds(*world) : 0.0;
		m_colonyStorage.invalidate();
		m_needs.invalidate();
		m_claimsBuilt = false;
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
			// GATE: only recover a colonist that is genuinely STRANDED -- inside a sim region but
			// off a walkable face. A colonist that NO region currently covers (off-camera, an
			// unsimulated corner) is left exactly where it stands: there is no mesh to judge it
			// against, and relocating it merely because the sim area moved away is the bug this
			// rework fixes (it dumped a wandering colonist into the river when a camera pan rebuilt
			// the area off him). inSimArea() is true only when a built region contains the point,
			// so inSimArea && !isOnMesh is exactly "in a region, off its mesh".
			//
			// A "fresh route" (a valid NavPath stamped to the current mesh generation) does NOT
			// prove on-mesh: a teleport or a recenter can leave a current-version path under a
			// colonist that is itself off-mesh, and the route's first waypoint is the standing
			// point the locate already rejects. So recovery keys ONLY on isOnMesh; the snap nudge
			// (nearestPathablePoint biases a hair toward the triangle centroid) lifts the colonist
			// off the boundary edge that locate excludes.
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
						task.clear();
					}
				}
			}

			// Get optional Action component (may be nullptr if entity doesn't have one)
			auto* action = world->getComponent<Action>(entity);

			// Replan-on-discovery: this is the free-space-assumption loop. A colonist
			// plans over what it remembers, walks, and when vision reveals a wall that
			// cuts its corridor (Memory::beliefVersion moves) or the navmesh rebuilds
			// under it (NavigationSystem::generation() moves), re-request the SAME goal
			// with current belief. Not a re-task -- the destination is unchanged. Runs
			// before the shouldReEvaluate gate so it isn't skipped between re-evals, and
			// only when a version actually changed, so it can't thrash. Vision bumps
			// beliefVersion at most ~12 Hz while this runs per frame, so the version
			// compare naturally coalesces many idle frames into at most one repath per
			// discovery. Gate on a still-pursued goal: a valid route on a Moving task.
			if (auto* navPath = world->getComponent<NavPath>(entity);
				navPath != nullptr && navPath->valid && task.state == TaskState::Moving) {
				const bool beliefMoved = memory.beliefVersion != navPath->builtBeliefVersion;
				const bool navMoved =
					(m_navSystem != nullptr) && (m_navSystem->generation() != navPath->builtNavVersion);
				if (beliefMoved || navMoved) {
					// requestNavPath re-stamps the path on success, or (on a believed-route
					// denial) invalidates it and clears movementTarget.active to stop the
					// colonist instead of beelining through the believed wall.
					const NavRequestOutcome outcome = requestNavPath(entity, task.targetPosition, position, memory, movementTarget);
					applyNavOutcome(task, outcome);
					if (outcome == NavRequestOutcome::Routed) {
						// Show "Re-routing" for ~30 ticks so the player sees the colonist react
						// to a newly-discovered wall before the panel reverts to "Going to".
						// navStateHold counts down each update tick; the panel reads Rerouting
						// while it's >0, then Traveling once it hits zero.
						task.navState = NavState::Rerouting;
						task.navStateHold = 30;
					}
				}
			}

			// Deferred-route retry. requestNavPath holds a route it can't plan yet (no mesh built,
			// or an endpoint outside every built region) and applyNavOutcome parks the task in
			// AwaitingMesh with the nav generation it was deferred at. Nothing else would request
			// it again: replan-on-discovery needs a live route, the chain-leg repath below needs an
			// active target, and a re-eval that re-selects the same option keeps the task
			// (isSameTask) without routing it. So once a mesh build lands, re-request the SAME
			// goal. Keyed on the generation, so a goal still out of reach costs one request per
			// build, not one per frame.
			if (task.state == TaskState::Moving && task.navState == NavState::AwaitingMesh && m_navSystem != nullptr &&
				m_navSystem->generation() != task.deferredNavGeneration) {
				// The hold switched movement off; re-arm it for the request, which switches it off
				// again if the route still can't be planned.
				movementTarget.active = true;
				const NavRequestOutcome outcome = requestNavPath(entity, task.targetPosition, position, memory, movementTarget);
				applyNavOutcome(task, outcome);
			}

			// Chain-leg repath: a multi-phase task (Haul Pickup->Deposit, PlacePackaged
			// Pickup->Place) hands off to its next leg inside ActionSystem by re-pointing the
			// task at the new goal (state Moving, MovementTarget active) and dropping the now-stale
			// pickup-leg route. Because it's the SAME task, the re-eval below sees isSameTask and
			// never runs selectTaskFromTrace -- the only OTHER place a route is requested. So
			// without this, the colonist would carry an active MovementTarget and no NavPath, which
			// MovementSystem would steer straight at the goal (a beeline). Request the navmesh path
			// for the new leg HERE so every move follows an A* route. Gate on a Moving task with an
			// active target but no live route, which the replan-on-discovery block above (valid
			// route only) does not cover. The off-mesh recovery at the top of update() has already
			// snapped any stranded colonist onto valid ground before we get here.
			if (auto* navPath = world->getComponent<NavPath>(entity); task.state == TaskState::Moving &&
				movementTarget.active && (navPath == nullptr || !navPath->valid)) {
				const NavRequestOutcome outcome = requestNavPath(entity, task.targetPosition, position, memory, movementTarget);
				applyNavOutcome(task, outcome);
			}

			// Drain the Rerouting hold counter so "Re-routing" is a brief visible beat.
			// Once it reaches zero the info panel maps navState to the display string,
			// so the colonist naturally shows "Going to" again without any extra set.
			if (task.navState == NavState::Rerouting && task.navStateHold > 0) {
				--task.navStateHold;
				if (task.navStateHold == 0) {
					task.navState = NavState::Traveling;
				}
			}

			// Direct player control: skip autonomous task selection entirely. The player drives this
			// colonist via issuePlayerMoveOrder, which sets a Moving task that the repath-on-discovery,
			// deferred-route retry, and chain-leg repath blocks above carry exactly like an AI route;
			// the colonist otherwise stands and waits. Only buildDecisionTrace/selectTaskFromTrace are
			// suppressed -- off-mesh recovery and repath still run, and VisionSystem is independent, so
			// discovery keeps working.
			if (world->getComponent<PlayerControlled>(entity) != nullptr) {
				task.timeSinceEvaluation = 0.0F;
				continue;
			}

			// Check if we should re-evaluate (uses current timer value)
			if (!shouldReEvaluate(task, needs, action, movementTarget.active)) {
				// Only increment timer when NOT re-evaluating (timer tracks time since last eval)
				task.timeSinceEvaluation += deltaTime;
				continue;
			}

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
				buildDecisionTrace(entity, position, needs, memory, task, inventory, skills, *sourceReach, *trace);

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
					// it MUST be free to take the fresh option, or it stays parked forever.
					if (sameType && task.type == TaskType::Wander) {
						isSameTask = movementTarget.active;
					} else {
						bool sameTarget = false;
						if (selected->targetPosition.has_value()) {
							const auto& selectedPos = selected->targetPosition.value();
							// Check that at least one position is non-zero to avoid default (0,0) matches
							const float selectedLen2 = glm::dot(selectedPos, selectedPos);
							const float currentLen2 = glm::dot(task.targetPosition, task.targetPosition);
							if (selectedLen2 > 0.0001F || currentLen2 > 0.0001F) {
								// Use distance threshold for "same" position (within 0.5 meters)
								float dist = glm::distance(task.targetPosition, selectedPos);
								sameTarget = (dist < 0.5F);
							}
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

						// A task whose route failed on a mesh or belief that has since changed is a fresh
						// pick: keeping it would leave the colonist stopped with a stale failure and no new
						// route request.
						const bool failedOnStaleNav =
							task.navState == NavState::CantFindWayTo && task.blockedReachStamp != reachStamp;
						isSameTask = sameType && sameTarget && samePlaceTarget && !failedOnStaleNav;
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
				// re-evaluates with empty hands once the load is deposited.
				//
				// Two escapes/limits keep this from ever pinning a colonist on a stuck haul:
				//   - It never fires when the delivery itself has no believed route to its destination
				//     (navState CantFindWayTo) -- holding an undeliverable load would strand it.
				//   - A strictly-higher-tier challenger (critical need, active work order) still preempts;
				//     dropping for an emergency is acceptable. Only same-or-lower-priority challengers
				//     (the opportunistic pull, idle Wander) are blocked.
				const bool currentHaulUnreachable = (task.navState == NavState::CantFindWayTo);
				const bool carryingTwoHandLoad = task.type == TaskType::Haul && !task.haulItemDefName.empty() &&
					ecs::itemIsTwoHand(m_registry, task.haulItemDefName) && inventory.isHolding(task.haulItemDefName);
				if (shouldSwitch && !handedCargo && !currentHaulUnreachable && carryingTwoHandLoad && selected != nullptr) {
					// Case 1: a Harvest of the same yield (e.g. a construction material Harvest for Wood). Held regardless of tier -- the chop->drop->chop loop it forms is the
					// original two-hand-drop bug this guard was built for.
					const bool sameYieldHarvest = selected->taskType == TaskType::Harvest &&
						selected->harvestYieldDefNameId == m_registry.getDefNameId(task.haulItemDefName);
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
					// Update the (tier, score) key even when staying on the same task (the score can
					// change, e.g. PlacePackaged promotes to tier 4 when the colonist starts carrying).
					task.priorityTier = newTier;
					task.priority = newScore;
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
						handleChainInterruption(entity, task, inventory, position, selected->taskType, selected->needType);
					}

					// The abandoned task's claims leave the index now, unless it just became the
					// suspended record or hands its cargo on, so a colonist deciding later this update
					// sees them free.
					if (!handedCargo && (!task.suspended || task.suspended->instanceId != task.instanceId)) {
						m_claims.retire(task.instanceId);
					}

					// Clear and assign new task
					task.clear();
					task.timeSinceEvaluation = 0.0F;
					selectTaskFromTrace(entity, task, movementTarget, *trace, position);
					task.priorityTier = newTier; // Store the (tier, score) key for future comparisons
					task.priority = newScore;
					acceptClaims(entity, task, memory, inventory, handedCargo);

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
			if (!handedCargo && adoptsSuspended(m_registry, task)) {
				takeOverCargo(task, *task.suspended);
			} else {
				m_claims.retire(recordId);
			}
			task.discardSuspended();
		}
		recordChainClaims(*world, m_needs, m_claims, m_registry, memory, inventory, task);
		m_claims.append(entity, task);
	}

	bool AIDecisionSystem::shouldReEvaluate(
		const Task&			  task,
		const NeedsComponent& needs,
		const Action* /*action (reserved for future interruptability checks)*/,
		bool				  movementTargetActive
	) {
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
		// site across water, or a route the freshly-built mesh invalidated). Re-evaluate so it
		// picks a reachable target instead of standing frozen -- a Wander would otherwise never
		// re-eval while "Moving". selectTaskFromTrace re-stamps navState from the new route, so this
		// clears as soon as a reachable target is chosen (no thrash).
		if (task.navState == NavState::CantFindWayTo) {
			return true;
		}

		// Re-evaluate periodically
		if (task.timeSinceEvaluation >= kReEvalInterval) {
			return true;
		}

		// Check if any critical need requires immediate attention (tier 2 interrupts all lower tiers)
		bool hasCriticalNeed = false;
		for (auto needType : NeedsComponent::kActionableNeeds) {
			if (needs.get(needType).isCritical()) {
				hasCriticalNeed = true;
				break;
			}
		}

		if (hasCriticalNeed) {
			// If already handling a critical need, don't interrupt for other critical needs
			if (task.type == TaskType::FulfillNeed) {
				const auto& currentNeed = needs.get(task.needToFulfill);
				if (currentNeed.isCritical()) {
					return false;
				}
			}
			// Critical need interrupts non-critical tasks and wander
			return true;
		}

		// Idle Wander with a LIVE target is freely preemptible: re-evaluate every tick so any higher-tier
		// task is taken the instant it appears, with no commitment lag. isSameTask keeps the wander target
		// from thrashing (nothing better -> stays on the same leg), so discovery is preserved; this trades
		// a per-tick decision-trace rebuild for that responsiveness, acceptable at current colony scale.
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
		DecisionTrace&		  trace
	) {
		trace.clear();

		// Agent footprint + belief for every reachability check below (need sources, reachable-wander
		// generation and the option filter).
		float agentRadius = 0.3F;
		if (const auto* ar = world->getComponent<AgentRadius>(entity)) {
			agentRadius = ar->radiusMeters;
		}
		const geometry::nav::BeliefFilter belief{&memory.knownSegments, &memory.knownOpenings};
		const bool						  haveMesh = (m_navSystem != nullptr) && m_navSystem->hasMesh();

		// Everything this colonist can reach, from one start-side climb of the nav forest; each
		// target test is then a locate and a subtree check. No mesh: nothing is provably unreachable.
		std::optional<NavigationSystem::Reach> reach;
		if (haveMesh) {
			reach = m_navSystem->reachFrom(position.value, agentRadius, belief);
		}
		const auto canReach = [&](const glm::vec2& target) {
			return !reach.has_value() || m_navSystem->reaches(*reach, target);
		};

		// Verdicts for sources in the colonist's own region are remembered while it stays in the same
		// reach subtree of the same mesh, so one surrounded by unreachable sources doesn't test them all
		// again every decision. A source anywhere else is judged live (by which regions cover it, no
		// mesh involved) and never remembered, since which region a point falls in moves on its own.
		reachStamp = 0;
		if (reach.has_value()) {
			const SourceReachMemo::Key key = reachKey(*reach, memory);
			reachStamp = key.stamp();
			sourceReach.keep(key, 2 * Memory::kMaxWorldEntities);
			// The route to the target this colonist was heading for (a need source, a toilet spot, a work
			// site) just came back with no believed route, though the forest let it through (a known
			// wall's room, a passage the funnel can't take). Remember it unreachable like any other
			// verdict so the next decision picks something else instead of re-deciding the same target
			// every frame, but only under the key it failed under, or the verdict would outlive what it
			// was true for. A wander point is random, so there's nothing to remember.
			if (currentTask.type != TaskType::Wander && currentTask.navState == NavState::CantFindWayTo &&
				currentTask.blockedReachStamp == reachStamp && m_navSystem->sharesStartRegion(*reach, currentTask.targetPosition)) {
				sourceReach.recordUnreachable(routeTargetKey(currentTask.needSourceKey, currentTask.targetPosition));
			}
		}
		// A target whose route already failed under the current key; one in another region is judged live.
		const auto routeFailed = [&](std::uint64_t sourceKey, const glm::vec2& target) {
			return reach.has_value() && sourceReach.knownUnreachable(routeTargetKey(sourceKey, target)) &&
				   m_navSystem->sharesStartRegion(*reach, target);
		};
		const auto canReachSource = [&](uint64_t sourceKey, const glm::vec2& sourcePosition) {
			if (!reach.has_value()) {
				return true;
			}
			return m_navSystem->sharesStartRegion(*reach, sourcePosition) ? sourceReach.reachable(sourceKey, sourcePosition, canReach)
																		   : canReach(sourcePosition);
		};
		// Drop a source already found unreachable before anything else looks at it.
		const auto notKnownUnreachable = [&](uint64_t sourceKey, const KnownWorldEntity& known) {
			return !reach.has_value() || !sourceReach.knownUnreachable(sourceKey) ||
				   !m_navSystem->sharesStartRegion(*reach, known.position);
		};
		const auto anywhere = [](uint64_t, const glm::vec2&) { return true; };
		const auto anySource = [](uint64_t, const KnownWorldEntity&) { return true; };

		// A need targets the nearest remembered source the colonist can reach, so known sources across
		// a river don't hide a reachable one farther off.
		const auto findNeedSource = [&](assets::CapabilityType capability, const auto& accept, const auto& reachable) {
			return findNearestReachableWithCapability(memory, capability, position.value, accept, reachable, sourceScratch);
		};

		// With an empty pack, the one food source both the Hunger option and the gather-food option
		// target: the nearest reachable remembered harvestable whose yield is edible and that is ready
		// to give it. The gather option is on offer whenever the pack is empty, so this is probed even
		// when Hunger is met. Readiness is asked of candidates nearest first, only until one is chosen;
		// the nearest reachable one passed over for regrowing is kept to show why the colonist waits.
		// When every edible source it knows is regrowing, the search isn't run again until the first of
		// them is ready or its memory of world entities changes (SourceReachMemo::FoodWait).
		//
		// A source another colonist is going to eat from (its Hunger task holds the Use) is passed over
		// like a regrowing one, so two hungry colonists don't converge on one bush; the nearest is kept
		// to show who holds it. Work claims (a cut for a goal, the idle gather) don't hold food against
		// a hungry colonist: a need's claim is one harvest it will eat, and nothing else may starve it.
		// The gather option is idle work and does give way to any holder of the source it picks.
		const ClaimExclusion self = ClaimExclusion::of(currentTask);
		const auto			 eats = [](const ClaimHolder& holder) {
			return holder.taskType == TaskType::FulfillNeed && holder.tier < kTierIdle;
		};
		const bool						packHasFood = hasEdibleFood(m_registry, inventory);
		std::optional<KnownWorldEntity> foodSource;
		std::optional<KnownWorldEntity> regrowingFood;
		std::optional<KnownWorldEntity> reservedFood;
		EntityID						reservedFoodHolder = kInvalidEntity;
		if (!packHasFood) {
			const double now = m_simulationNow;
			if (const auto* wait = sourceReach.waitingForFood(memory.worldEntityVersion, now)) {
				regrowingFood = wait->nearest;
			} else {
				bool   everyCandidateRegrowing = true;
				double firstReadyAt = std::numeric_limits<double>::infinity();
				foodSource = findNeedSource(
					assets::CapabilityType::Harvestable,
					[&](uint64_t sourceKey, const KnownWorldEntity& known) {
						if (!yieldsEdibleFood(m_registry, known.defNameId)) {
							return false;
						}
						if (!notKnownUnreachable(sourceKey, known)) {
							everyCandidateRegrowing = false; // never asked, so it may be ready
							return false;
						}
						return true;
					},
					[&](uint64_t sourceKey, const glm::vec2& sourcePosition) {
						const KnownWorldEntity* known = memory.getWorldEntity(sourceKey);
						if (const auto readyAt = sourceRegrowingUntil(known->defNameId, sourcePosition)) {
							firstReadyAt = std::min(firstReadyAt, *readyAt);
							if (!regrowingFood.has_value() && canReachSource(sourceKey, sourcePosition)) {
								regrowingFood = *known;
							}
							return false;
						}
						everyCandidateRegrowing = false;
						if (!canReachSource(sourceKey, sourcePosition)) {
							return false;
						}
						if (const auto eater = m_claims.useHolderWhere({.kind = TargetKind::WorldKey, .key = sourceKey}, self, eats)) {
							if (!reservedFood.has_value()) {
								reservedFood = *known;
								reservedFoodHolder = eater->colonist;
							}
							return false;
						}
						return true;
					}
				);
				sourceReach.foodWait.reset();
				if (!foodSource.has_value() && everyCandidateRegrowing && firstReadyAt > now &&
					firstReadyAt < std::numeric_limits<double>::infinity()) {
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

				// Second priority: harvest the known food source
				if (foodSource.has_value()) {
					option.targetPosition = foodSource->position;
					option.targetDefNameId = foodSource->defNameId;
					option.distanceToTarget = glm::distance(position.value, foodSource->position);
					option.targetReachable = true;
					option.sourceKey = hashWorldEntity(foodSource->position, foodSource->defNameId);
					option.status = need.needsAttention() ? OptionStatus::Available : OptionStatus::Satisfied;
					option.reason = "harvest";
					trace.options.push_back(option);
					continue;
				}

				// The food it knows is someone else's next meal: wait for it, taking other options meanwhile
				if (reservedFood.has_value() && need.needsAttention()) {
					option.targetPosition = reservedFood->position;
					option.targetDefNameId = reservedFood->defNameId;
					option.distanceToTarget = glm::distance(position.value, reservedFood->position);
					option.status = OptionStatus::Reserved;
					option.reservedBy = reservedFoodHolder;
					trace.options.push_back(option);
					continue;
				}

				// The only reachable food known is regrowing: wait on it, taking other options meanwhile
				if (regrowingFood.has_value() && need.needsAttention()) {
					option.targetPosition = regrowingFood->position;
					option.targetDefNameId = regrowingFood->defNameId;
					option.distanceToTarget = glm::distance(position.value, regrowingFood->position);
					option.status = OptionStatus::Regrowing;
					trace.options.push_back(option);
					continue;
				}

				// No food in inventory and no reachable food source known
				option.status = need.needsAttention() ? OptionStatus::NoSource : OptionStatus::Satisfied;
				trace.options.push_back(option);
				continue;
			}

			// Standard handling for other needs
			auto capability = needToCapability(needType);
			// A satisfied need only shows its nearest source, so it spends no reachability tests.
			const auto nearest = need.needsAttention() ? findNeedSource(capability, notKnownUnreachable, canReachSource)
													   : findNeedSource(capability, anySource, anywhere);

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
				} else if (alreadyPursuingThisNeed && !routeFailed(0, currentTask.targetPosition)) {
					// Preserve existing target to avoid "chasing" a moving target
					option.targetPosition = currentTask.targetPosition;
					option.distanceToTarget = glm::distance(position.value, currentTask.targetPosition);
					option.status = OptionStatus::Available;
				} else if ((needType == NeedType::Bladder || needType == NeedType::Digestion) && m_chunkManager != nullptr &&
						   m_navSystem != nullptr) {
					// For toilet needs, try smart location finder
					// A spot whose route already failed is no spot: go where the colonist stands instead.
					auto location = findToiletLocation(position.value, *m_chunkManager, *m_navSystem, *world, memory, m_registry);
					if (location.has_value() && !routeFailed(0, *location)) {
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
				// No source and no fallback
				option.status = need.needsAttention() ? OptionStatus::NoSource : OptionStatus::Satisfied;
			}

			trace.options.push_back(option);
		}

		// Add "Gather Food" work option (tier 7)
		// Only show if colonist has no food in inventory
		if (!packHasFood) {
			EvaluatedOption gatherOption;
			gatherOption.taskType = TaskType::FulfillNeed; // Reuse FulfillNeed for now
			gatherOption.needType = NeedType::Hunger;	   // Will trigger Harvest action
			gatherOption.needValue = 100.0F;			   // Not a real need, just work
			gatherOption.threshold = 0.0F;				   // Always available when no food

			// Harvesting uses Farming skill
			auto [farmSkillLevel, farmSkillBonus] = calculateSkillBonus(skills, kSkillFarming);
			gatherOption.skillLevel = farmSkillLevel;
			gatherOption.skillBonus = farmSkillBonus;

			const auto foodSourceHolder =
				foodSource.has_value()
					? m_claims.useHolder({.kind = TargetKind::WorldKey, .key = hashWorldEntity(foodSource->position, foodSource->defNameId)}, self)
					: std::nullopt;
			if (foodSourceHolder.has_value()) {
				gatherOption.targetPosition = foodSource->position;
				gatherOption.distanceToTarget = glm::distance(position.value, foodSource->position);
				gatherOption.status = OptionStatus::Reserved;
				gatherOption.reservedBy = foodSourceHolder->colonist;
				gatherOption.reason = "Food source";
			} else if (foodSource.has_value()) {
				gatherOption.targetPosition = foodSource->position;
				gatherOption.distanceToTarget = glm::distance(position.value, foodSource->position);
				gatherOption.targetReachable = true;
				gatherOption.sourceKey = hashWorldEntity(foodSource->position, foodSource->defNameId);
				gatherOption.status = OptionStatus::Available;
				gatherOption.reason = "Gathering food (inventory empty)";
			} else if (reservedFood.has_value()) {
				gatherOption.targetPosition = reservedFood->position;
				gatherOption.distanceToTarget = glm::distance(position.value, reservedFood->position);
				gatherOption.status = OptionStatus::Reserved;
				gatherOption.reservedBy = reservedFoodHolder;
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
		for (auto [stationEntity, stationPos, workQueue] : world->view<Position, WorkQueue>()) {
			if (!workQueue.hasPendingWork()) {
				continue;
			}

			const CraftingJob* nextJob = workQueue.getNextJob();
			if (nextJob == nullptr) {
				continue;
			}

			// Get the recipe
			const auto* recipe = m_recipeRegistry.getRecipe(nextJob->recipeDefName);
			if (recipe == nullptr) {
				continue;
			}

			// Does the STATION's store already hold every input? Materials are hauled into the
			// station during provisioning (mirroring construction); the craft work is only
			// available once they're all physically present in the station, not in the colonist.
			const auto* stationStore = world->getComponent<Inventory>(stationEntity);
			bool		hasAllInputs = stationStore != nullptr;
			if (hasAllInputs) {
				for (const auto& input : recipe->inputs) {
					if (stationStore->getQuantity(input.defName) < input.count) {
						hasAllInputs = false;
						break;
					}
				}
			}


			// Add craft option with skill bonus
			EvaluatedOption craftOption;
			craftOption.taskType = TaskType::Craft;
			craftOption.needType = NeedType::Count; // N/A for crafting
			craftOption.needValue = 100.0F;			// Not a need
			craftOption.threshold = 0.0F;
			craftOption.targetPosition = stationPos.value;
			craftOption.distanceToTarget = glm::distance(position.value, stationPos.value);
			craftOption.craftRecipeDefName = nextJob->recipeDefName;
			craftOption.stationEntityId = static_cast<uint64_t>(stationEntity);
			craftOption.tiebreakId = static_cast<uint64_t>(stationEntity);

			// Calculate skill bonus for crafting
			auto [craftSkillLevel, craftSkillBonus] = calculateSkillBonus(skills, kSkillCrafting);
			craftOption.skillLevel = craftSkillLevel;
			craftOption.skillBonus = craftSkillBonus;

			// Provisioning the materials is the goal-driven Harvest/Haul options' job; the Craft
			// option is the crafting WORK, workable only once the station holds every input.
			if (hasAllInputs) {
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

			trace.options.push_back(craftOption);

		}
		// Goal-driven work: fetch goods for storage and crafts, carry in what the colonist holds, cut
		// for what a root still needs (tier 4 when serving an active work order, else tier 6).
		const RootSupply supply{*world, m_needs, m_claims, self, currentTask, inventory, m_registry, m_loosePiles};
		PickupRoom		 pickupRoom(inventory, m_registry);
		m_workSources.begin(memory, m_registry, inventory);
		evaluateHaulOptions(world, m_registry, m_workSources, m_colonyStorage, supply, pickupRoom, inventory, position.value, trace);
		evaluateCarryInOptions(supply, m_carried, position.value, trace);
		evaluateHarvestOptions(m_registry, m_workSources, supply, pickupRoom, position.value, skills, trace);

		// Build staged construction blueprints (goal-driven)
		evaluateBuildOptions(world, position.value, skills, trace);

		// Deconstruct demolishing structures (goal-driven, same tier as Build)
		evaluateDeconstructOptions(world, position.value, skills, trace);

		// Place packaged items at target locations
		evaluatePlacePackagedOptions(world, supply, position.value, inventory, trace);

		// Firm "can I actually navigate right now?" gate. An idle wander picks a random point;
		// with no navmesh yet (the costly startup window) the colonist has no idea what's walkable,
		// and canReach can't help: with no reach it passes every point, so a wander offered then is
		// one the colonist can't follow. So only OFFER the wander when navigation is possible: a mesh
		// exists, or there's no nav system at all (headless/tests, direct movement). With a nav
		// system but no mesh, emit no idle option; the colonist holds (see the no-option hold in
		// selectTaskFromTrace) until the mesh lands. Needs and work keep their own options, but they
		// too only MOVE once requestNavPath returns a route -- an off-area or pre-mesh leg HOLDS, it
		// does not slide blind. (Off-mesh stranding is handled separately by the snap-to-mesh
		// recovery at the top of update().)
		const bool canNavigate = (m_navSystem == nullptr) || haveMesh;
		if (canNavigate) {
			// Only offer a wander we can ACTUALLY reach. Probe a few random points; if none are
			// reachable -- a colonist hemmed in by water/obstacles where every point lands somewhere
			// it can't path to -- emit no idle option at all. The no-option hold then makes it stand
			// and look around instead of committing to an unreachable point and thrashing on it.
			glm::vec2 wanderTarget	= generateWanderTarget(position.value);
			bool	  foundReachable = canReach(wanderTarget);
			for (int attempt = 0; !foundReachable && attempt < 5; ++attempt) {
				const glm::vec2 candidate = generateWanderTarget(position.value);
				if (canReach(candidate)) {
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
		// score (distance factor, skill, age, hysteresis). Goal createdAt is always 0, so a goal's age is
		// m_elapsedTime, monotonic, which is all the within-tier age term needs.
		const uint32_t currentHaulItemId = currentTask.type == TaskType::Haul ? m_registry.getDefNameId(currentTask.haulItemDefName) : 0U;
		for (auto& option : trace.options) {
			populatePriorityBonuses(world, option, currentTask, currentHaulItemId, m_elapsedTime);
		}

		// Sort by the lexicographic (tier asc, score desc, tiebreakId asc) key. Tier is categorical
		// and compared first: a far tier-4 option always beats any tier-5 option, with no floor
		// constants. The tiebreak id gives a deterministic total order independent of container
		// iteration (the goal registry is hash-bucket ordered), so two equal-(tier,score) options
		// can't route colonists nondeterministically (a multiplayer desync). higherPriority()
		// encodes the full ordering.
		std::sort(trace.options.begin(), trace.options.end(), EvaluatedOption::higherPriority);

		// Select the first actionable option the colonist can actually REACH, scanning in priority
		// order. The reach test is a sound, cheap pre-filter: false means DEFINITELY unreachable
		// (off-mesh, or a disconnected component -- e.g. a reed or water across a river with no land
		// bridge). Probing in priority order means we only test down to the best reachable option,
		// not the whole list. An unreachable higher-priority option drops to NoSource so the colonist
		// won't commit to it and freeze at the bank; Wander is never filtered, so there is always a
		// fallback that keeps it moving and exploring for new sources. No mesh yet -> the reach test
		// passes, so startup / outdoor beelining is unaffected. A need source the builder already
		// chose as reachable (targetReachable) isn't tested again.
		for (auto& option : trace.options) {
			if (option.status != OptionStatus::Available) {
				continue;
			}
			const bool needsTarget = option.taskType != TaskType::Wander && option.targetPosition.has_value();
			// A harvest of a regrowing source would pay nothing at completion: pass it over for the next
			// option, and it is chosen again once ready. A clear removes its source whatever its state.
			// Asked before the snap moves the target off the source's own position.
			if (option.taskType == TaskType::Harvest && needsTarget && option.targetDefNameId.has_value() &&
				sourceRegrowingUntil(*option.targetDefNameId, *option.targetPosition).has_value()) {
				const auto* goal = GoalTaskRegistry::Get().getGoal(option.harvestGoalId);
				if (goal == nullptr || !goal->clearFootprint.has_value()) {
					option.status = OptionStatus::Regrowing;
					continue;
				}
			}
			// Snap only the task types whose action looks its target up by entity/footprint and so
			// is satisfied by arriving ADJACENT: Harvest (incl. footprint clearing), Build, and
			// Deconstruct. Their stored target is a CENTER that can sit in a navmesh hole -- a tree
			// trunk's collision carves the tile out, so the pathfinder (which needs the goal point
			// itself on a walkable triangle and never snaps) can never reach it. Snapping to the
			// nearest walkable point makes the chop/clear/build reachable; a center already walkable
			// snaps essentially in place. Haul/Craft are EXCLUDED: their actions validate arrival
			// against the EXACT source/target position (haulSourcePosition / haulTargetPosition /
			// station) within a tight tolerance, and their targets (loose piles, storage, stations)
			// already sit on walkable ground -- snapping would move the nav goal off the tolerance-
			// checked point and the action would report "not at source or target" forever.
			const bool snapEligible = option.taskType == TaskType::Harvest ||
									  option.taskType == TaskType::Build || option.taskType == TaskType::Deconstruct;
			if (haveMesh && needsTarget && snapEligible) {
				// nullopt means the target is outside every active sim region (off-area): drop the
				// option to NoSource so the colonist prefers a reachable nearby source and never
				// commits to an off-area target it would hold on forever.
				// The snap nudge stays within HarvestActions' kPositionTolerance (0.5 m) of the target,
				// so the colonist still registers "at source" on arrival. (The arbitration epic's
				// locate-by-entity supersedes this position-based snap.)
				const auto snapped = m_navSystem->nearestPathablePoint(option.targetPosition.value());
				if (!snapped.has_value()) {
					option.status = OptionStatus::NoSource; // off every active region: don't commit
					continue;
				}
				option.targetPosition  = snapped;
				option.targetReachable = false; // a moved target is tested where it now is
			}
			if (needsTarget && !option.targetReachable && !canReach(option.targetPosition.value())) {
				option.status = OptionStatus::NoSource; // unreachable: skip it rather than freeze on it
				continue;
			}
			if (needsTarget && routeFailed(option.sourceKey, option.targetPosition.value())) {
				option.status = OptionStatus::NoSource; // its route already failed under this key
				continue;
			}
			option.status = OptionStatus::Selected;
			break;
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
		const Position&		 position
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

		// Copy crafting-specific fields for Craft tasks
		if (selected->taskType == TaskType::Craft) {
			task.craftRecipeDefName = selected->craftRecipeDefName;
			task.targetStationId = selected->stationEntityId;
		}

		// Copy harvest-specific fields for Harvest tasks. A cut toward a root becomes its own deposit leg
		// when it lands (ActionSystem::carryHarvestIn).
		if (selected->taskType == TaskType::Harvest) {
			task.harvestTargetEntityId = selected->harvestTargetEntityId;
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
			task.haulItemDefName = m_registry.getDefName(selected->haulItemDefNameId);
			task.haulQuantity = selected->haulQuantity;
			task.haulSourcePosition = selected->haulSourcePosition.value_or(glm::vec2{0.0F, 0.0F});
			task.haulSourceStorageId = selected->haulSourceStorageId;
			task.haulTargetStorageId = selected->haulTargetStorageId;
			task.haulGoalId = selected->haulGoalId;
			task.haulTargetPosition = selected->haulTargetPosition.value_or(glm::vec2{0.0F, 0.0F});
			task.haulFromInventory = selected->haulFromInventory;
			// Craft-material hauls already carry the items, so head straight to the station.
			// Standard hauls go to the source first (pickup), then the destination.
			task.targetPosition = task.haulFromInventory ? task.haulTargetPosition : task.haulSourcePosition;
			// Pickup (step 0) then Deposit (step 1); goods already carried start at the deposit leg.
			task.chainId = GoalTaskRegistry::Get().allocateChainId();
			task.chainStep = task.haulFromInventory ? 1 : 0;
		}

		// Copy placement-specific fields for PlacePackaged tasks
		// PlacePackaged is a two-step chain: PickupPackaged (step 0) → Place (step 1)
		if (selected->taskType == TaskType::PlacePackaged) {
			task.placePackagedEntityId = selected->placePackagedEntityId;
			task.placeSourcePosition = selected->placeSourcePosition.value_or(glm::vec2{0.0F, 0.0F});
			task.placeTargetPosition = selected->placeTargetPosition.value_or(glm::vec2{0.0F, 0.0F});
			// For place tasks, target is initially the source position (pickup first)
			task.targetPosition = task.placeSourcePosition;
			// Assign chain ID and start at step 0 (Pickup phase)
			task.chainId = GoalTaskRegistry::Get().allocateChainId();
			task.chainStep = 0;
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
			// Memory is a required component on every AI entity (it's in update()'s view),
			// so it's always present here; plan the route over the colonist's belief.
			const Memory* memory = world->getComponent<Memory>(entity);
			if (memory != nullptr) {
				const NavRequestOutcome outcome = requestNavPath(entity, task.targetPosition, position, *memory, movementTarget);
				applyNavOutcome(task, outcome);
			}
		}
	}

	AIDecisionSystem::NavRequestOutcome AIDecisionSystem::requestNavPath(
		EntityID entity, const glm::vec2& goal, const Position& position, const Memory& memory,
		MovementTarget& movementTarget) {
		// No NavigationSystem wired at all (headless / tests only -- the running game always wires
		// one). With no system there is no mesh to plan over, so MovementSystem follows the active
		// MovementTarget directly. This branch is unreachable in the game; the movement invariant
		// (path-or-snap) holds there because m_navSystem is non-null. Invalidate any route left over
		// from a previous task so a stale path isn't followed.
		if (m_navSystem == nullptr) {
			if (auto* navPath = world->getComponent<NavPath>(entity)) {
				navPath->valid = false;
			}
			return NavRequestOutcome::Unmeshed;
		}

		// A navmesh exists as a system but hasn't finished building yet -- the costly startup
		// window while chunks stream in. NOTHING moves without a navmesh: dead-reckoning toward a
		// goal with no idea what's walkable is exactly what stranded and stalled colonists. HOLD
		// here -- deactivate movement -- instead of beelining blind. The task
		// stays, deferred (applyNavOutcome): update()'s deferred-route retry requests the same
		// goal again once the first mesh lands. The periodic re-eval does NOT pick it back up; it
		// re-selects the same option and keeps the task as it stands.
		if (!m_navSystem->hasMesh()) {
			if (auto* navPath = world->getComponent<NavPath>(entity)) {
				navPath->valid = false;
			}
			movementTarget.active = false;
			LOG_DEBUG(Engine, "[Nav] Entity %llu: no mesh yet, holding (nothing moves without a navmesh)",
				static_cast<unsigned long long>(entity));
			return NavRequestOutcome::Waiting;
		}

		// LOD seam: if either endpoint is outside the BUILT simulation area the exact mesh can't
		// answer the query (the off-area leg is beyond LOD0, and there is no coarse far-field graph
		// yet). A colonist may NOT slide blind toward an off-area goal -- that is the beeline this
		// architecture forbids. HOLD instead, exactly like the no-mesh case: deactivate movement,
		// invalidate any stale route. The deferred-route retry requests it again
		// after each region build lands, so it routes once a build covers both endpoints.
		//
		// FUTURE LOD1: path to the area edge via LOD0, hand off to a coarse regional graph for the
		// long-haul leg, re-enter LOD0 near the goal. Until that lands, off-area goals are held.
		if (!m_navSystem->inSimArea(position.value) || !m_navSystem->inSimArea(goal)) {
			if (auto* navPath = world->getComponent<NavPath>(entity)) {
				navPath->valid = false;
			}
			movementTarget.active = false;
			LOG_DEBUG(Engine, "[Nav] Entity %llu: off-area endpoint, holding (no LOD0 route to (%.2f, %.2f))",
				static_cast<unsigned long long>(entity), goal.x, goal.y);
			return NavRequestOutcome::Waiting;
		}

		// Agent footprint feeds the disc-clearance query; default if the entity has none.
		float radius = 0.3F;
		if (const auto* agentRadius = world->getComponent<AgentRadius>(entity)) {
			radius = agentRadius->radiusMeters;
		}

		// Plan over what THIS colonist remembers, not ground truth: unseen walls are
		// absent (the optimistic free-space assumption), seen walls block, known doors
		// pass. The filter holds pointers into the colonist's Memory sets and is consumed
		// synchronously inside requestPath, so there's no lifetime hazard.
		const geometry::nav::BeliefFilter belief{&memory.knownSegments, &memory.knownOpenings};

		auto path = m_navSystem->requestPath(position.value, goal, radius, belief);
		if (!path.has_value()) {
			// A mesh exists but the colonist's belief admits no route: a believed wall cuts the
			// corridor. STOP rather than beeline dishonestly through that wall -- clear
			// movementTarget.active so MovementSystem doesn't carry the colonist straight at the
			// geometry it believes is solid. (An OFF-mesh start can't happen here: the per-colonist
			// loop snaps stranded colonists back onto the mesh before any path request.)
			if (auto* navPath = world->getComponent<NavPath>(entity)) {
				navPath->valid = false;
			}
			movementTarget.active = false;
			if (auto* task = world->getComponent<Task>(entity)) {
				task->blockedReachStamp = reachKey(m_navSystem->reachFrom(position.value, radius, belief), memory).stamp();
			}
			LOG_DEBUG(Engine, "[Nav] Entity %llu: no believed route to (%.2f, %.2f), stopping",
				static_cast<unsigned long long>(entity), goal.x, goal.y);
			return NavRequestOutcome::Blocked;
		}

		// Attach or overwrite the route. waypoints[0] is ~the start, so steer toward
		// index 1 when there's more than one point (skip the point we're standing on).
		NavPath navPath;
		navPath.waypoints = std::move(*path);
		navPath.current = navPath.waypoints.size() >= 2 ? 1 : 0;
		navPath.valid = true;
		// Stamp the belief/nav versions this route was planned against, so the replan
		// loop can detect staleness with a cheap compare (no re-query).
		navPath.builtBeliefVersion = memory.beliefVersion;
		navPath.builtNavVersion = m_navSystem->generation();

		const std::size_t count = navPath.waypoints.size();
		if (auto* existing = world->getComponent<NavPath>(entity)) {
			*existing = std::move(navPath);
		} else {
			world->addComponent<NavPath>(entity, std::move(navPath));
		}

		LOG_DEBUG(Engine, "[Nav] Entity %llu: path to (%.2f, %.2f), %zu waypoints",
			static_cast<unsigned long long>(entity), goal.x, goal.y, count);
		return NavRequestOutcome::Routed;
	}

	void AIDecisionSystem::applyNavOutcome(Task& task, NavRequestOutcome outcome) const {
		task.navStateHold = 0;
		switch (outcome) {
			case NavRequestOutcome::Routed:
			case NavRequestOutcome::Unmeshed:
				task.navState = NavState::Traveling;
				return;
			case NavRequestOutcome::Waiting:
				// Only requestNavPath's hold branches return Waiting, and both run with a
				// NavigationSystem wired.
				task.navState = NavState::AwaitingMesh;
				task.deferredNavGeneration = m_navSystem->generation();
				return;
			case NavRequestOutcome::Blocked:
				task.navState = NavState::CantFindWayTo;
				return;
		}
	}

	std::optional<double> AIDecisionSystem::sourceRegrowingUntil(uint32_t defNameId, glm::vec2 position) const {
		return onSourceRegrowth ? onSourceRegrowth(defNameId, position, m_simulationNow) : std::nullopt;
	}

	void AIDecisionSystem::handleChainInterruption(
		EntityID		entity,
		Task&			task,
		Inventory&		inventory,
		const Position& position,
		TaskType		newTaskType,
		NeedType		newNeedType
	) {
		// The goods a haul or a cut carries for its root.
		const std::string& item = m_registry.getDefName(chainItem(m_registry, task));

		// A need interrupting a haul or cut whose goods the colonist keeps leaves it as its suspended
		// record, so the goods stay committed to where they were going. Called on every path that keeps
		// them; a drop keeps none, and the pile it leaves is an ordinary source with no claims.
		const auto keepGoods = [&]() {
			if (newTaskType != TaskType::FulfillNeed) {
				return;
			}
			auto record = suspendableWork(m_registry, task, ecs::availableQuantity(inventory, item));
			if (!record) {
				return;
			}
			if (task.suspended) {
				m_claims.retire(task.suspended->instanceId);
			}
			task.suspended = std::move(record);
			m_claims.suspend(entity, task);
		};

		// Check if new task's first action needs hands
		if (!taskFirstActionNeedsHands(newTaskType, newNeedType)) {
			// New task doesn't need hands - colonist can keep carrying
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

			const auto* assetDef = m_registry.getDefinition(item);
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
			if (dropped.has_value() && dropped->quantity > 0 && world != nullptr) {
				ecs::storeOrDrop(*world, dropped->defName, dropped->quantity, position.value, onDropResource);
			}
			return;
		}

		// For PlacePackaged tasks: packaged entity is being carried
		if (task.type == TaskType::PlacePackaged && inventory.carryingPackagedEntity.has_value()) {
			uint64_t packagedEntityId = inventory.carryingPackagedEntity.value();

			// Update packaged entity's position to colonist's position (drop it here)
			auto* packagedPos = world->getComponent<Position>(packagedEntityId);
			if (packagedPos != nullptr) {
				packagedPos->value = position.value;
			}

			// Clear carrying state
			inventory.carryingPackagedEntity.reset();
			inventory.leftHand.reset();
			inventory.rightHand.reset();

			LOG_INFO(
				Engine,
				"[AI] Entity %llu: chain interrupted, dropped packaged entity %llu at (%.1f, %.1f)",
				entityId,
				static_cast<unsigned long long>(packagedEntityId),
				position.value.x,
				position.value.y
			);
		}
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
			task->clear();
			task->timeSinceEvaluation = 0.0F;
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
		if (m_navSystem != nullptr && !m_navSystem->isOnMesh(goal)) {
			const auto snapped = m_navSystem->nearestPathablePoint(goal, radius);
			if (!snapped.has_value()) {
				return std::nullopt;  // nothing reachable near the click; leave the colonist be
			}
			goal = *snapped;
		}

		// Express the walk as an ordinary Moving task so the existing repath-on-discovery and
		// arrival hand-off carry it. type=None means ActionSystem does nothing on arrival -- the
		// colonist just stands at the destination. Leave movementTarget.speed alone (spawn-owned).
		task->clear();
		task->type = TaskType::None;
		task->state = TaskState::Moving;
		task->navState = NavState::Traveling;
		task->targetPosition = goal;
		task->timeSinceEvaluation = 0.0F;
		movementTarget->target = goal;
		movementTarget->active = true;

		if (const Memory* memory = world->getComponent<Memory>(entity)) {
			// A Blocked order shows "can't get there" and waits for the player to re-click (the
			// control gate skips re-eval). A Waiting one is deferred like any AI route: the
			// deferred-route retry runs ahead of the control gate, so the order resumes on its own
			// once a mesh build covers it.
			const NavRequestOutcome outcome = requestNavPath(entity, goal, *position, *memory, *movementTarget);
			applyNavOutcome(*task, outcome);
		}
		return goal;
	}

} // namespace ecs
