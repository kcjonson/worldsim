#include "AIDecisionSystem.h"

#include "../GoalTaskRegistry.h"
#include "../InventoryMass.h"
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
#include "../components/Skills.h"
#include "../components/StorageConfiguration.h"
#include "../components/StructureBlueprint.h"
#include "../components/Task.h"
#include "../components/ToiletLocationFinder.h"
#include "../components/Transform.h"
#include "../components/WorkQueue.h"
#include "NavigationSystem.h"

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
#include <atomic>
#include <cmath>
#include <numbers>

namespace ecs {

	namespace {

		/// Global chain ID counter for generating unique chain identifiers.
		/// Starts at 1 so that 0 can represent "no chain" in optional contexts.
		/// NOTE: This variable intentionally has internal linkage by living in an anonymous
		/// namespace inside this .cpp file. Do not move this definition into a header, or
		/// each translation unit would get its own counter and chain IDs could collide.
		std::atomic<uint64_t> g_nextChainId{1};

		/// Generate a unique chain ID for multi-step tasks
		[[nodiscard]] uint64_t generateChainId() {
			return g_nextChainId.fetch_add(1, std::memory_order_relaxed);
		}

		/// Map NeedType to the CapabilityType that fulfills it
		[[nodiscard]] engine::assets::CapabilityType needToCapability(NeedType need) {
			switch (need) {
				case NeedType::Hunger:
					return engine::assets::CapabilityType::Edible;
				case NeedType::Thirst:
					return engine::assets::CapabilityType::Drinkable;
				case NeedType::Energy:
					return engine::assets::CapabilityType::Sleepable;
				case NeedType::Bladder:
				case NeedType::Digestion:
					// Both bladder and digestion use Toilet capability
					return engine::assets::CapabilityType::Toilet;
				case NeedType::Hygiene:
				case NeedType::Recreation:
				case NeedType::Temperature:
					break; // Non-actionable for now
				case NeedType::Count:
					break;
			}
			// All valid NeedTypes must be handled above - hitting this is a bug
			LOG_ERROR(Engine, "needToCapability: unhandled NeedType %d", static_cast<int>(need));
			return engine::assets::CapabilityType::Edible;
		}

		/// Get a human-readable name for a need type (for debug logging)
		[[nodiscard]] const char* needTypeName(NeedType need) {
			switch (need) {
				case NeedType::Hunger:
					return "Hunger";
				case NeedType::Thirst:
					return "Thirst";
				case NeedType::Energy:
					return "Energy";
				case NeedType::Bladder:
					return "Bladder";
				case NeedType::Digestion:
					return "Digestion";
				case NeedType::Count:
					break;
			}
			// All valid NeedTypes must be handled above - hitting this is a bug
			LOG_ERROR(Engine, "needTypeName: unhandled NeedType %d", static_cast<int>(need));
			return "Unknown";
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
			return engine::assets::ActionTypeRegistry::Get().actionNeedsHands(std::string(actionDefName));
		}

		/// Check if inventory contains any edible food item
		[[nodiscard]] bool hasEdibleFood(const Inventory& inventory) {
			for (const auto& edibleItemName : engine::assets::getEdibleItemNames()) {
				if (inventory.hasItem(edibleItemName)) {
					return true;
				}
			}
			return false;
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

		/// Check if an option matches the current task (for in-progress bonus)
		[[nodiscard]] bool isOptionCurrentTask(const EvaluatedOption& option, const Task& currentTask) {
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
					return option.haulItemDefName == currentTask.haulItemDefName &&
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

		/// Populate priority bonuses for an evaluated option
		/// Uses PriorityConfig for calculations and goal registry for age bonus
		/// @param option The option to populate bonuses for
		/// @param currentTask Current colonist task (for in-progress bonus)
		/// @param currentTime Current game time (for task age calculation)
		void populatePriorityBonuses(EvaluatedOption& option, const Task& currentTask, float currentTime) {
			const auto& priorityConfig = engine::assets::PriorityConfig::Get();

			// Distance bonus: closer targets get higher priority (0 distance = max bonus)
			option.distanceBonus = priorityConfig.calculateDistanceBonus(option.distanceToTarget);

			// In-progress bonus: current task gets priority to resist switching
			bool isCurrentTask = isOptionCurrentTask(option, currentTask);
			if (isCurrentTask) {
				option.inProgressBonus = priorityConfig.getInProgressBonus();
			}

			// Chain continuation bonus: large bonus for continuing a multi-step task
			// Applied when colonist is mid-chain (has completed step 0) and option is the same task
			// This makes colonists strongly prefer finishing chains (e.g., depositing after pickup)
			if (isCurrentTask && currentTask.chainId.has_value() && currentTask.chainStep > 0) {
				option.chainBonus = priorityConfig.getChainBonus();
			}

			// Task age bonus: old unclaimed goals rise in priority
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
		}

		/// Evaluate haul options by querying Haul goals from GoalTaskRegistry
		/// Goal-driven: Goals define WHAT storage needs items, Memory + inventory provide
		/// fulfillment options.
		///
		/// Two source kinds:
		/// - Craft-material hauls (the Haul is a child of a Craft goal): the colonist harvested
		///   the materials into its own inventory, so the source is inventory. It carries them
		///   to the crafting station - no ground pickup. This is what makes the
		///   harvest -> haul -> craft chain actually connect.
		/// - Storage hauls: the source is a loose ground item the colonist knows about.
		///
		/// @param world ECS world (to read a destination storage's live inventory for haul sizing)
		/// @param registry Asset registry for capability lookups
		/// @param memory Colonist memory (known entities)
		/// @param inventory Colonist inventory (for craft-material hauls)
		/// @param position Colonist position
		/// @param trace Output decision trace
		void evaluateHaulOptions(
			World*								 world,
			const engine::assets::AssetRegistry& registry,
			const Memory&						 memory,
			const Inventory&					 inventory,
			const glm::vec2&					 position,
			DecisionTrace&						 trace
		) {
			auto& goalRegistry = GoalTaskRegistry::Get();

			// Query all Haul goals (storage containers wanting items)
			auto haulGoals = goalRegistry.getGoalsOfType(TaskType::Haul);

			for (const auto* goal : haulGoals) {
				if (goal == nullptr) {
					continue;
				}
				// A storage goal's slot-count targetAmount makes availableCapacity() a poor "full"
				// signal (it goes 0 after one stack); its real headroom is the destination's
				// per-item addableCount, checked below in the loose-pile path. Only gate the
				// counter-driven goals (craft/construction) here.
				if (goal->owner != GoalOwner::StorageGoalSystem && goal->availableCapacity() == 0) {
					continue; // Goal is full
				}

				// Inventory-source haul: the colonist already carries the harvested material, so
				// there is no loose ground pickup. Two cases use this path:
				//  - Craft-material hauls (Haul is a child of a Craft goal): deliver to the
				//    crafting station (items stay in inventory for the Craft action).
				//  - Construction-material hauls (owner ConstructionGoalSystem): deliver to the
				//    blueprint, where the deposit records the material onto the blueprint's delivered[] manifest.
				// Both skip the memory scan and emit only once the dependency completed (status
				// Available) and the colonist actually carries the material.
				const bool isConstructionHaul = goal->owner == GoalOwner::ConstructionGoalSystem;
				bool isCraftHaul = false;
				if (!isConstructionHaul && goal->parentGoalId.has_value()) {
					const auto* parent = goalRegistry.getGoal(goal->parentGoalId.value());
					isCraftHaul = parent != nullptr && parent->type == TaskType::Craft;
				}
				const bool inventorySourceHaul = isConstructionHaul || isCraftHaul;
				if (inventorySourceHaul) {
					if (goal->status == GoalStatus::Available) {
						const bool toBlueprint = goal->owner == GoalOwner::ConstructionGoalSystem;
						for (uint32_t acceptedId : goal->acceptedDefNameIds) {
							const auto& itemDefName = registry.getDefName(acceptedId);
							// Hand-carried two-hand goods (a wood armful) count too, not just the pack.
							uint32_t	carried = ecs::availableQuantity(inventory, itemDefName);
							if (carried == 0) {
								continue;
							}

							// Craft-station deliveries don't remove the item from inventory -- they only
							// credit the goal -- so a re-deposit of already-credited units is a no-op. Suppress
							// the option once everything carried is staged (carried <= delivered); otherwise the
							// colonist parks at the bench re-depositing instead of fetching the units it still
							// lacks. Blueprint deliveries consume the item, so they keep the carried>0 gate.
							if (!toBlueprint && carried <= goal->deliveredAmount) {
								continue;
							}

							EvaluatedOption haulOption;
							haulOption.taskType = TaskType::Haul;
							haulOption.needType = NeedType::Count;
							haulOption.needValue = 100.0F;
							haulOption.threshold = 0.0F;
							haulOption.targetPosition = goal->destinationPosition;
							haulOption.targetDefNameId = acceptedId;
							haulOption.distanceToTarget = glm::distance(position, goal->destinationPosition);
							haulOption.haulItemDefName = itemDefName;
							haulOption.haulQuantity = std::min(carried, goal->availableCapacity());
							haulOption.haulSourcePosition = position; // already carrying
							haulOption.haulTargetStorageId = static_cast<uint64_t>(goal->destinationEntity);
							haulOption.haulTargetPosition = goal->destinationPosition;
							haulOption.haulGoalId = goal->id;
							haulOption.haulFromInventory = true;
							haulOption.tiebreakId = goal->id ^ (static_cast<uint64_t>(acceptedId) << 1);
							haulOption.status = OptionStatus::Available;
							haulOption.reason = toBlueprint ? "Delivering " + itemDefName + " to build site"
															: "Delivering " + itemDefName + " to crafting station";
							trace.options.push_back(haulOption);
						}
					}

					// Craft-only fetch: the colonist isn't carrying the material, so bring a remembered
					// loose stock to the station (two-phase pickup -> deposit; the deposit keeps it in
					// inventory for the Craft action). Construction never fetches; its goods come from a cut.
					if (isCraftHaul && goal->status == GoalStatus::Available) {
						for (const auto& [key, looseItem] : memory.knownWorldEntities) {
							const bool accepted = std::find(goal->acceptedDefNameIds.begin(), goal->acceptedDefNameIds.end(), looseItem.defNameId) != goal->acceptedDefNameIds.end();
							if (!accepted || !registry.hasCapability(looseItem.defNameId, engine::assets::CapabilityType::Carryable)) {
								continue;
							}
							const auto& itemDefName = registry.getDefName(looseItem.defNameId);
							// A craft-station delivery only CREDITS the goal; it never removes the material
							// from inventory (the Craft action consumes it later from there). So the colonist
							// must physically gather the recipe's full count (targetAmount) into its own pack.
							// Fetch while it still holds fewer than that, regardless of how many are already
							// credited -- a recipe needing 2 when the colonist holds 1 still needs a 2nd loose
							// unit fetched. Once it holds the full count, stop fetching and let the deposit finish.
							if (ecs::availableQuantity(inventory, itemDefName) >= goal->targetAmount) {
								continue;
							}
							const auto* itemDef = registry.getDefinition(itemDefName);
							if (itemDef == nullptr || !itemDef->capabilities.carryable.has_value()) {
								continue;
							}
							// Only fetch what the pickup can actually lift. The Pickup action clamps to
							// carry weight (cargoUnitsThatFit); at or over the cap it adds 0, the staged
							// count never rises, and the AI re-issues this same fetch every tick -- an
							// infinite "fetch -> collect 0 -> fetch" loop that never completes the craft.
							// Craft-station deliveries KEEP the staged materials in the pack, so a colonist
							// gathering a multi-material recipe (or already loaded with other cargo) fills up
							// and the last material can't be lifted. Skip the fetch when no unit fits; the
							// craft stays pending until the colonist has room, instead of spinning.
							if (ecs::cargoUnitsThatFit(inventory, registry, itemDefName) == 0) {
								continue;
							}

							float tripDistance = glm::distance(position, looseItem.position) + glm::distance(looseItem.position, goal->destinationPosition);

							EvaluatedOption fetchOption;
							fetchOption.taskType = TaskType::Haul;
							fetchOption.needType = NeedType::Count;
							fetchOption.needValue = 100.0F;
							fetchOption.threshold = 0.0F;
							fetchOption.targetPosition = looseItem.position;
							fetchOption.targetDefNameId = looseItem.defNameId;
							fetchOption.distanceToTarget = tripDistance;
							fetchOption.haulItemDefName = itemDefName;
							fetchOption.haulQuantity = std::min(itemDef->capabilities.carryable.value().quantity, goal->availableCapacity());
							fetchOption.haulSourcePosition = looseItem.position;
							fetchOption.haulTargetStorageId = static_cast<uint64_t>(goal->destinationEntity);
							fetchOption.haulTargetPosition = goal->destinationPosition;
							fetchOption.haulGoalId = goal->id;
							fetchOption.haulFromInventory = false;
							fetchOption.tiebreakId = goal->id ^ (key << 1);
							fetchOption.status = OptionStatus::Available;
							fetchOption.reason = "Fetching " + itemDefName + " for crafting";
							trace.options.push_back(fetchOption);
						}
					}
					continue;
				}

				// Check colonist's memory for items that can fulfill this goal
				for (const auto& [key, looseItem] : memory.knownWorldEntities) {
					// Check if this item type is accepted by the goal
					bool accepted = false;

					// Check by category
					if (goal->acceptedCategory != engine::assets::ItemCategory::None) {
						const auto& defName = registry.getDefName(looseItem.defNameId);
						const auto* def = registry.getDefinition(defName);
						if (def != nullptr && def->category == goal->acceptedCategory) {
							accepted = true;
						}
					}

					// Check by specific defNameId
					if (!accepted) {
						for (uint32_t acceptedId : goal->acceptedDefNameIds) {
							if (looseItem.defNameId == acceptedId) {
								accepted = true;
								break;
							}
						}
					}

					if (!accepted) {
						continue;
					}

					// Check if entity is actually Carryable
					if (!registry.hasCapability(looseItem.defNameId, engine::assets::CapabilityType::Carryable)) {
						continue;
					}

					const auto& itemDefName = registry.getDefName(looseItem.defNameId);
					const auto* itemDef = registry.getDefinition(itemDefName);
					if (itemDef == nullptr) {
						continue;
					}

					// Calculate trip distance: colonist -> item -> goal destination
					float tripDistance = glm::distance(position, looseItem.position) +
										 glm::distance(looseItem.position, goal->destinationPosition);

					// Create haul option
					EvaluatedOption haulOption;
					haulOption.taskType = TaskType::Haul;
					haulOption.needType = NeedType::Count;
					haulOption.needValue = 100.0F;
					haulOption.threshold = 0.0F;
					haulOption.targetPosition = looseItem.position;
					haulOption.targetDefNameId = looseItem.defNameId;
					haulOption.distanceToTarget = tripDistance;
					haulOption.haulItemDefName = itemDefName;
					// Size the trip by what the DESTINATION storage can actually accept of this
					// specific item -- its stack headroom plus free-slot * stackSize
					// (addableCount), not the goal's slot count. A storage with 3 free slots takes
					// up to 3 full stacks of wood (~120), not 3 wood. Then over-propose only as far
					// as the colonist can carry (weight); the pickup clamps to the live
					// ResourceStack and remaining carry capacity, and the deposit clamps to the
					// storage again, so nothing overfills.
					uint32_t storageHeadroom = UINT32_MAX;
					if (const auto* destInv = world != nullptr ? world->getComponent<Inventory>(goal->destinationEntity) : nullptr) {
						storageHeadroom = destInv->addableCount(itemDefName);
					}
					if (storageHeadroom == 0) {
						continue; // storage can't take any more of this item, skip it
					}
					haulOption.haulQuantity =
						std::min(storageHeadroom, ecs::cargoUnitsPerTrip(registry, itemDefName, inventory.carryCapacityKg));
					haulOption.haulSourcePosition = looseItem.position;
					haulOption.haulTargetStorageId = static_cast<uint64_t>(goal->destinationEntity);
					haulOption.haulTargetPosition = goal->destinationPosition;
					haulOption.haulGoalId = goal->id;
					// Tiebreak on (goal, source item) so two equidistant loose items for the same
					// goal resolve deterministically rather than by memory's hash order.
					haulOption.tiebreakId = goal->id ^ (key << 1);
					haulOption.status = OptionStatus::Available;
					haulOption.reason = "Hauling " + itemDefName + " to storage";
					trace.options.push_back(haulOption);
				}
			}
		}

		/// Evaluate harvest options by querying Harvest goals from GoalTaskRegistry
		/// Goal-driven: Goals define WHAT items are needed (via yieldDefNameId), Memory provides harvestable sources
		/// @param registry Asset registry for capability and definition lookups
		/// @param memory Colonist memory (known entities)
		/// @param position Colonist position
		/// @param skills Optional skills component for skill bonus calculation
		/// @param trace Output decision trace
		void evaluateHarvestOptions(
			const engine::assets::AssetRegistry& registry,
			const Memory&						 memory,
			const Inventory&					 inventory,
			const glm::vec2&					 position,
			const Skills*						 skills,
			DecisionTrace&						 trace
		) {
			auto& goalRegistry = GoalTaskRegistry::Get();

			// Query all Harvest goals (requests for items that come from harvestables)
			auto harvestGoals = goalRegistry.getGoalsOfType(TaskType::Harvest);

			for (const auto* goal : harvestGoals) {
				if (goal == nullptr || goal->availableCapacity() == 0) {
					continue; // Goal is full or null
				}

				// Skip goals that aren't workable yet.
				if (goal->status == GoalStatus::Blocked) {
					continue;
				}

				// For Harvest goals, yieldDefNameId tells us what item is needed
				// We need to find harvestable entities in memory that yield this item
				if (goal->yieldDefNameId == 0) {
					continue; // No yield specified
				}

				// Search memory for harvestables that yield the target item
				for (const auto& [key, knownEntity] : memory.knownWorldEntities) {
					// Check if this entity is harvestable
					if (!registry.hasCapability(knownEntity.defNameId, engine::assets::CapabilityType::Harvestable)) {
						continue;
					}

					// Check what this harvestable yields
					const auto& defName = registry.getDefName(knownEntity.defNameId);
					const auto* def = registry.getDefinition(defName);
					if (def == nullptr || !def->capabilities.harvestable.has_value()) {
						continue;
					}

					// Get the yield defNameId for this harvestable
					uint32_t yieldDefNameId = registry.getDefNameId(def->capabilities.harvestable->yieldDefName);
					if (yieldDefNameId != goal->yieldDefNameId) {
						continue; // Yields different item than what goal needs
					}

					// Tool gate: chopping a tree needs the colonist to already hold the right
					// tool (e.g. an axe). Colonists never fetch or equip tools on their own, so
					// a tool-less colonist simply never sees this harvest as an option.
					if (!ecs::inventoryHoldsToolType(inventory, registry, def->capabilities.harvestable->requiredToolType)) {
						continue;
					}

					// Calculate distance to harvestable
					float distanceToHarvestable = glm::distance(position, knownEntity.position);

					// Create harvest option
					EvaluatedOption harvestOption;
					harvestOption.taskType = TaskType::Harvest;
					harvestOption.needType = NeedType::Count;
					harvestOption.needValue = 100.0F;
					harvestOption.threshold = 0.0F;
					harvestOption.targetPosition = knownEntity.position;
					harvestOption.targetDefNameId = knownEntity.defNameId;
					harvestOption.distanceToTarget = distanceToHarvestable;
					harvestOption.harvestTargetEntityId = key; // The world entity ID
					harvestOption.harvestGoalId = goal->id;
					harvestOption.harvestYieldDefNameId = goal->yieldDefNameId;
					// Tiebreak on (goal, target) so two equidistant harvestables feeding the same
					// goal still resolve deterministically, not by memory's hash order.
					harvestOption.tiebreakId = goal->id ^ (key << 1);
					harvestOption.status = OptionStatus::Available;

					// Harvesting uses Farming skill
					auto [farmSkillLevel, farmSkillBonus] = calculateSkillBonus(skills, kSkillFarming);
					harvestOption.skillLevel = farmSkillLevel;
					harvestOption.skillBonus = farmSkillBonus;

					// Build reason string from friendly labels, not raw defNames ("Oak Tree", not
					// "Flora_TreeOak"). Falls back to the defName if a label is missing.
					const auto&		  yieldDefName = registry.getDefName(goal->yieldDefNameId);
					const auto*		  yieldDef = registry.getDefinition(yieldDefName);
					const std::string srcLabel = !def->label.empty() ? def->label : defName;
					const std::string yieldLabel =
						(yieldDef != nullptr && !yieldDef->label.empty()) ? yieldDef->label : yieldDefName;
					harvestOption.reason = "Cutting " + srcLabel + " for " + yieldLabel;

					trace.options.push_back(harvestOption);
				}
			}
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

			for (const auto* goal : goalRegistry.getGoalsOfType(TaskType::Build)) {
				if (goal == nullptr || goal->status != GoalStatus::Available) {
					continue;
				}
				const auto blueprintEntity = static_cast<EntityID>(goal->destinationEntity);
				const auto* blueprint = world->getComponent<StructureBlueprint>(blueprintEntity);
				if (blueprint == nullptr ||
					blueprint->phase != StructureBlueprint::BuildPhase::UnderConstruction) {
					continue; // not ready to build (cleared/awaiting materials/complete)
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
				buildOption.status = OptionStatus::Available;

				auto [buildSkillLevel, buildSkillBonus] = calculateSkillBonus(skills, kSkillConstruction);
				buildOption.skillLevel = buildSkillLevel;
				buildOption.skillBonus = buildSkillBonus;
				buildOption.reason = "Building structure";

				trace.options.push_back(buildOption);
			}
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

			for (const auto* goal : goalRegistry.getGoalsOfType(TaskType::Deconstruct)) {
				if (goal == nullptr || goal->status != GoalStatus::Available) {
					continue;
				}
				const auto	blueprintEntity = static_cast<EntityID>(goal->destinationEntity);
				const auto* blueprint = world->getComponent<StructureBlueprint>(blueprintEntity);
				if (blueprint == nullptr || blueprint->workDone <= 0.0F) {
					continue; // nothing to tear down (already removed, or no work invested)
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
				deconstructOption.status = OptionStatus::Available;

				auto [deconstructSkillLevel, deconstructSkillBonus] = calculateSkillBonus(skills, kSkillConstruction);
				deconstructOption.skillLevel = deconstructSkillLevel;
				deconstructOption.skillBonus = deconstructSkillBonus;
				deconstructOption.reason = "Deconstructing structure";

				trace.options.push_back(deconstructOption);
			}
		}

		/// Evaluate place packaged options for furniture delivery
		/// @param world ECS world for entity queries
		/// @param position Colonist position
		/// @param inventory Colonist inventory (to check if carrying)
		/// @param trace Output decision trace
		void evaluatePlacePackagedOptions(World* world, const glm::vec2& position, const Inventory& inventory, DecisionTrace& trace) {
			// Find packaged items with targetPosition set (awaiting colonist delivery)
			for (auto [packagedEntity, packagedPos, packaged, packagedAppearance] : world->view<Position, Packaged, Appearance>()) {
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
				placeOption.reason = isCarryingThis ? "Delivering " + packagedAppearance.defName : "Placing " + packagedAppearance.defName;
				trace.options.push_back(placeOption);
			}
		}

	} // namespace

	AIDecisionSystem::AIDecisionSystem(
		const engine::assets::AssetRegistry&  registry,
		const engine::assets::RecipeRegistry& recipeRegistry,
		std::optional<uint32_t>				  rngSeed
	)
		: m_registry(registry),
		  m_recipeRegistry(recipeRegistry),
		  m_rng(rngSeed.value_or(std::random_device{}())) {}

	void AIDecisionSystem::update(float deltaTime) {
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
					const auto snapped = m_navSystem->nearestPathablePoint(position.value);
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
					if (outcome == NavRequestOutcome::Routed) {
						// Show "Re-routing" for ~30 ticks so the player sees the colonist react
						// to a newly-discovered wall before the panel reverts to "Going to".
						// navStateHold counts down each update tick; the panel reads Rerouting
						// while it's >0, then Traveling once it hits zero.
						task.navState = NavState::Rerouting;
						task.navStateHold = 30;
					} else if (outcome == NavRequestOutcome::Blocked) {
						task.navState = NavState::CantFindWayTo;
						task.navStateHold = 0;
					} else {
						task.navState = NavState::Traveling; // beelining (no mesh) -- not stuck
						task.navStateHold = 0;
					}
				}
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
				task.navState = (outcome == NavRequestOutcome::Blocked) ? NavState::CantFindWayTo : NavState::Traveling;
				task.navStateHold = 0;
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

			// Check if we should re-evaluate (uses current timer value)
			if (!shouldReEvaluate(task, needs, action)) {
				// Only increment timer when NOT re-evaluating (timer tracks time since last eval)
				task.timeSinceEvaluation += deltaTime;
				continue;
			}

			// Store current task state for priority comparison
			float	  currentPriority = task.priority;
			bool	  hasActiveAction = (action != nullptr && action->isActive());
			TaskState previousState = task.state;

			// Check if entity has DecisionTrace component for trace-based selection
			auto* trace = world->getComponent<DecisionTrace>(entity);
			if (trace != nullptr) {
				// Get optional Skills component for skill bonus calculation
				auto* skills = world->getComponent<Skills>(entity);

				// Build full decision trace (always, for UI updates)
				buildDecisionTrace(entity, position, needs, memory, task, inventory, skills, *trace);

				// Get the best option's priority
				const auto* selected = trace->getSelected();
				float		newPriority = (selected != nullptr) ? selected->calculatePriority() : 0.0F;

				// Check if the new task is actually different from current task
				bool isSameTask = false;
				if (selected != nullptr && task.isActive()) {
					// Compare task type
					bool sameType = (task.type == selected->taskType);

					// For wander tasks, same type is enough - don't interrupt just because target
					// changed. EXCEPT when nav has stopped this wander (no believed route to its
					// point): it MUST then be free to pick a fresh, reachable target, or it stays
					// pinned to the unreachable one forever -- a permanent freeze.
					if (sameType && task.type == TaskType::Wander && task.navState != NavState::CantFindWayTo) {
						isSameTask = true;
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

						isSameTask = sameType && sameTarget && samePlaceTarget;
					}
				}

				// Decision: Should we switch tasks?
				// Don't switch if it's the same task we're already doing
				bool shouldSwitch = !isSameTask;
				if (isSameTask) {
					task.timeSinceEvaluation = 0.0F; // Reset timer, we did evaluate
					// Update priority even when staying on same task
					// (priority can change, e.g., PlacePackaged goes from 38 to 150 when carrying)
					task.priority = newPriority;
				}

				// If action in progress, check if we can/should interrupt
				if (shouldSwitch && hasActiveAction && previousState == TaskState::Arrived) {
					// Check if action is interruptable at all
					if (!action->interruptable) {
						// Biological necessities (Eat, Drink, Toilet) cannot be interrupted
						shouldSwitch = false;
						task.timeSinceEvaluation = 0.0F; // Reset timer, we did evaluate
					} else {
						// Action is interruptable - use priority gap threshold
						float priorityGap = newPriority - currentPriority;
						if (priorityGap < kPrioritySwitchThreshold) {
							// Priority gap too small - don't interrupt current action
							shouldSwitch = false;
							task.timeSinceEvaluation = 0.0F; // Reset timer, we did evaluate
						}
					}
				}

				if (shouldSwitch) {
					// Handle chain interruption if mid-chain and new task needs hands
					if (task.chainId.has_value() && task.chainStep > 0 && selected != nullptr) {
						handleChainInterruption(entity, task, inventory, position, selected->taskType, selected->needType);
					}

					// Clear and assign new task
					task.clear();
					task.timeSinceEvaluation = 0.0F;
					selectTaskFromTrace(entity, task, movementTarget, *trace, position);
					task.priority = newPriority; // Store priority for future comparisons

					LOG_INFO(
						Engine,
						"[AI] Entity %llu: %s (priority %.0f) → (%.1f, %.1f)",
						static_cast<unsigned long long>(entity),
						task.reason.c_str(),
						task.priority,
						task.targetPosition.x,
						task.targetPosition.y
					);
				}
			}
		}
	}

	bool AIDecisionSystem::shouldReEvaluate(
		const Task&			  task,
		const NeedsComponent& needs,
		const Action* /*action (reserved for future interruptability checks)*/
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

		// Check if any critical need requires immediate attention (Tier 3 interrupts all lower tiers)
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

		// No critical needs - don't interrupt wander while moving
		// Wandering gives the colonist a chance to discover new sources (water, food)
		if (task.type == TaskType::Wander && task.state == TaskState::Moving) {
			return false;
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
		DecisionTrace&		  trace
	) {
		trace.clear();

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
				if (hasEdibleFood(inventory)) {
					// Food in inventory - eat at current position (no movement needed)
					option.targetPosition = position.value;
					option.distanceToTarget = 0.0F;
					option.status = need.needsAttention() ? OptionStatus::Available : OptionStatus::Satisfied;
					option.reason = formatOptionReason(option, needTypeName(needType));
					// Add special marker to indicate this is from inventory
					if (option.status == OptionStatus::Available) {
						option.reason += " (from inventory)";
					}
					trace.options.push_back(option);
					continue;
				}

				// Second priority: find harvestable food source
				auto harvestable =
					findNearestWithCapability(memory, m_registry, engine::assets::CapabilityType::Harvestable, position.value);
				if (harvestable.has_value()) {
					// Check if this harvestable yields edible food
					const auto& defName = m_registry.getDefName(harvestable->defNameId);
					const auto* def = m_registry.getDefinition(defName);
					if (def != nullptr && def->capabilities.harvestable.has_value()) {
						const auto& harvestCap = def->capabilities.harvestable.value();
						// Check if yield is any edible item (data-driven check)
						if (engine::assets::isItemEdible(harvestCap.yieldDefName)) {
							option.targetPosition = harvestable->position;
							option.targetDefNameId = harvestable->defNameId;
							option.distanceToTarget = glm::distance(position.value, harvestable->position);
							option.status = need.needsAttention() ? OptionStatus::Available : OptionStatus::Satisfied;
							option.reason = formatOptionReason(option, needTypeName(needType));
							if (option.status == OptionStatus::Available) {
								option.reason += " (harvest)";
							}
							trace.options.push_back(option);
							continue;
						}
					}
				}

				// No food in inventory and no harvestable food source found
				option.status = need.needsAttention() ? OptionStatus::NoSource : OptionStatus::Satisfied;
				option.reason = formatOptionReason(option, needTypeName(needType));
				trace.options.push_back(option);
				continue;
			}

			// Standard handling for other needs
			auto capability = needToCapability(needType);
			auto nearest = findNearestWithCapability(memory, m_registry, capability, position.value);

			if (nearest.has_value()) {
				option.targetPosition = nearest->position;
				option.targetDefNameId = nearest->defNameId;
				option.distanceToTarget = glm::distance(position.value, nearest->position);

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
				} else if (alreadyPursuingThisNeed) {
					// Preserve existing target to avoid "chasing" a moving target
					option.targetPosition = currentTask.targetPosition;
					option.distanceToTarget = glm::distance(position.value, currentTask.targetPosition);
					option.status = OptionStatus::Available;
				} else if ((needType == NeedType::Bladder || needType == NeedType::Digestion) && m_chunkManager != nullptr) {
					// For toilet needs, try smart location finder
					auto location = findToiletLocation(position.value, *m_chunkManager, *world, memory, m_registry);
					if (location.has_value()) {
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

			option.reason = formatOptionReason(option, needTypeName(needType));
			trace.options.push_back(option);
		}

		// Add "Gather Food" work option (Tier 6)
		// Only show if colonist has no food in inventory
		if (!hasEdibleFood(inventory)) {
			// Look for harvestable food sources that yield EDIBLE items
			// (not all harvestables yield food - e.g., WoodyBush → Stick, Reed → PlantFiber)
			std::optional<KnownWorldEntity> edibleHarvestable;
			float							nearestEdibleDist = std::numeric_limits<float>::max();

			for (const auto& [key, entity] : memory.knownWorldEntities) {
				if (!m_registry.hasCapability(entity.defNameId, engine::assets::CapabilityType::Harvestable)) {
					continue;
				}
				// Check if this harvestable yields edible food
				const auto& defName = m_registry.getDefName(entity.defNameId);
				const auto* def = m_registry.getDefinition(defName);
				if (def != nullptr && def->capabilities.harvestable.has_value()) {
					const auto& harvestCap = def->capabilities.harvestable.value();
					if (engine::assets::isItemEdible(harvestCap.yieldDefName)) {
						float dist = glm::distance(position.value, entity.position);
						if (dist < nearestEdibleDist) {
							nearestEdibleDist = dist;
							edibleHarvestable = KnownWorldEntity{entity.defNameId, entity.position};
						}
					}
				}
			}

			EvaluatedOption gatherOption;
			gatherOption.taskType = TaskType::FulfillNeed; // Reuse FulfillNeed for now
			gatherOption.needType = NeedType::Hunger;	   // Will trigger Harvest action
			gatherOption.needValue = 100.0F;			   // Not a real need, just work
			gatherOption.threshold = 0.0F;				   // Always available when no food

			// Harvesting uses Farming skill
			auto [farmSkillLevel, farmSkillBonus] = calculateSkillBonus(skills, kSkillFarming);
			gatherOption.skillLevel = farmSkillLevel;
			gatherOption.skillBonus = farmSkillBonus;

			if (edibleHarvestable.has_value()) {
				gatherOption.targetPosition = edibleHarvestable->position;
				gatherOption.distanceToTarget = nearestEdibleDist;
				gatherOption.status = OptionStatus::Available;
				gatherOption.reason = "Gathering food (inventory empty)";
			} else {
				gatherOption.status = OptionStatus::NoSource;
				gatherOption.reason = "No food source known";
			}

			trace.options.push_back(gatherOption);
		}

		// Add "Crafting Work" options (Tier 6.5) and "Gather" options (Tier 6.6)
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

			// Does the colonist already hold every input? (A wood armful in hand counts, not just the pack.)
			bool hasAllInputs = true;
			for (const auto& input : recipe->inputs) {
				if (ecs::availableQuantity(inventory, input.defName) < input.count) {
					hasAllInputs = false;
					break;
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
			// option is the crafting WORK, workable only once the colonist holds every input.
			if (hasAllInputs) {
				craftOption.status = OptionStatus::Available;
				craftOption.reason = "Crafting " + recipe->label;
			} else {
				craftOption.status = OptionStatus::NoSource;
				craftOption.reason = "Crafting " + recipe->label + " (awaiting materials)";
			}

			trace.options.push_back(craftOption);

		}

		// Tier 6.4: Haul loose items to storage containers (goal-driven), and deliver
		// harvested craft materials from inventory to crafting stations
		evaluateHaulOptions(world, m_registry, memory, inventory, position.value, trace);

		// Tier 6.7: Harvest resources for crafting (goal-driven)
		evaluateHarvestOptions(m_registry, memory, inventory, position.value, skills, trace);

		// Tier 6.45: Build staged construction blueprints (goal-driven, priority 41)
		evaluateBuildOptions(world, position.value, skills, trace);

		// Tier 6.45: Deconstruct demolishing structures (goal-driven, priority 41 - same as Build)
		evaluateDeconstructOptions(world, position.value, skills, trace);

		// Tier 6.35: Place packaged items at target locations
		evaluatePlacePackagedOptions(world, position.value, inventory, trace);

		// Agent footprint + belief for the reachability checks below (reachable-wander generation
		// and the option filter). Defined once here so both uses share them.
		float agentRadius = 0.3F;
		if (const auto* ar = world->getComponent<AgentRadius>(entity)) {
			agentRadius = ar->radiusMeters;
		}
		const geometry::nav::BeliefFilter belief{&memory.knownSegments, &memory.knownOpenings};
		const bool						  haveMesh = (m_navSystem != nullptr) && m_navSystem->hasMesh();

		// Firm "can I actually navigate right now?" gate. An idle wander picks a random point;
		// with no navmesh yet (the costly startup window) the colonist has no idea what's walkable,
		// and isReachable can't help -- it returns true ("can't prove unreachable") when there's no
		// mesh, so the old check was leaky and the colonist committed to a wander it couldn't follow,
		// then stalled. So only OFFER the wander when navigation is genuinely possible: a mesh
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
			bool	  foundReachable = !haveMesh || m_navSystem->isReachable(position.value, wanderTarget, agentRadius, belief);
			for (int attempt = 0; !foundReachable && attempt < 5; ++attempt) {
				const glm::vec2 candidate = generateWanderTarget(position.value);
				if (m_navSystem->isReachable(position.value, candidate, agentRadius, belief)) {
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
			} else if (haveMesh) {
				// DIAGNOSTIC (temporary): nowhere reachable to wander. Log what the mesh thinks here
				// so we can tell a genuinely hemmed-in spot from a mesh that's wrong/incomplete.
				std::size_t meshTris = 0;
				for (const auto& rv : m_navSystem->builtRegions()) {
					meshTris += rv.mesh->triangles.size();
				}
				LOG_DEBUG(Engine,
					"[NavDiag] Entity %llu at (%.2f, %.2f): 0/6 wander probes reachable; onMesh=%d regions=%zu meshTris=%zu",
					static_cast<unsigned long long>(entity), position.value.x, position.value.y,
					m_navSystem->isOnMesh(position.value) ? 1 : 0, m_navSystem->regionCount(), meshTris);
			}
		}

		// Populate priority bonuses for all options using PriorityConfig
		// This includes: distance bonus, in-progress bonus, task age bonus (from GoalTaskRegistry)
		// Note: Using 0.0F for currentTime as task age tracking is refined in later phases
		for (auto& option : trace.options) {
			populatePriorityBonuses(option, currentTask, 0.0F);
		}

		// Sort by priority (highest first), breaking ties on the stable tiebreak id so the order
		// is a deterministic total order independent of container iteration. std::sort is not
		// stable and selection takes the first element, so without the tiebreak two equal-priority
		// options (e.g. two equidistant, equal-skill build sites) would resolve by hash-bucket
		// order and route colonists nondeterministically (a multiplayer desync). Lower tiebreak
		// id wins ties; goals get smaller ids the earlier they were created.
		std::sort(trace.options.begin(), trace.options.end(), [](const auto& a, const auto& b) {
			const float pa = a.calculatePriority();
			const float pb = b.calculatePriority();
			if (pa != pb) {
				return pa > pb;
			}
			return a.tiebreakId < b.tiebreakId;
		});

		// Select the first actionable option the colonist can actually REACH, scanning in priority
		// order. isReachable is a sound, cheap pre-filter: false means DEFINITELY unreachable
		// (off-mesh, or a disconnected component -- e.g. a reed or water across a river with no land
		// bridge). Probing in priority order means we only test down to the best reachable option,
		// not the whole list. An unreachable higher-priority option drops to NoSource so the colonist
		// won't commit to it and freeze at the bank; Wander is never filtered, so there is always a
		// fallback that keeps it moving and exploring for new sources. No mesh yet -> isReachable
		// returns true, so startup / outdoor beelining is unaffected.
		for (auto& option : trace.options) {
			if (option.status != OptionStatus::Available) {
				continue;
			}
			if (haveMesh && option.taskType != TaskType::Wander && option.targetPosition.has_value() &&
				!m_navSystem->isReachable(position.value, option.targetPosition.value(), agentRadius, belief)) {
				option.status = OptionStatus::NoSource; // unreachable: skip it rather than freeze on it
				continue;
			}
			option.status = OptionStatus::Selected;
			trace.selectionSummary = "Selected: " + option.reason;
			break;
		}
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
			// place, movement off, velocity zeroed. state=Moving (not Arrived) keeps shouldReEvaluate
			// from re-deciding every frame; the periodic re-eval still fires, so the colonist picks
			// up a real wander within a tick of the mesh landing.
			task.type			  = TaskType::Wander;
			task.state			  = TaskState::Moving;
			task.navState		  = NavState::Traveling;
			task.targetPosition	  = position.value;
			task.reason			  = "Waiting for the area to settle";
			movementTarget.active = false;
			if (auto* velocity = world->getComponent<Velocity>(entity)) {
				velocity->value = {0.0F, 0.0F};
			}
			return;
		}

		task.type = selected->taskType;
		task.needToFulfill = selected->needType;
		task.targetPosition = selected->targetPosition.value_or(position.value);
		task.reason = selected->reason;

		// Copy crafting-specific fields for Craft tasks
		if (selected->taskType == TaskType::Craft) {
			task.craftRecipeDefName = selected->craftRecipeDefName;
			task.targetStationId = selected->stationEntityId;
		}


		// Copy harvest-specific fields for Harvest tasks
		// Harvest is part of a Harvest→Haul chain: colonist harvests, then linked haul becomes available
		if (selected->taskType == TaskType::Harvest) {
			task.harvestTargetEntityId = selected->harvestTargetEntityId;
			task.harvestGoalId = selected->harvestGoalId;
			task.harvestYieldDefNameId = selected->harvestYieldDefNameId;
			// Get chain ID from goal for task continuity bonus
			const auto* goal = GoalTaskRegistry::Get().getGoal(selected->harvestGoalId);
			if (goal != nullptr && goal->chainId.has_value()) {
				task.chainId = goal->chainId;
				task.chainStep = 0; // Harvest is step 0 of the Harvest→Haul chain
			}
		}

		// Copy build-specific fields for Build/Deconstruct tasks. ActionSystem reads the blueprint
		// entity from the task and advances (Build) or unwinds (Deconstruct) its workDone;
		// targetPosition is the work slot.
		if (selected->taskType == TaskType::Build || selected->taskType == TaskType::Deconstruct) {
			task.buildBlueprintEntityId = selected->buildBlueprintEntityId;
		}

		// Copy hauling-specific fields for Haul tasks
		// Haul is a two-step chain: Pickup (step 0) → Deposit (step 1)
		// If Haul goal has a chainId (linked to Harvest), use that for continuity bonus
		if (selected->taskType == TaskType::Haul) {
			task.haulItemDefName = selected->haulItemDefName;
			task.haulQuantity = selected->haulQuantity;
			task.haulSourcePosition = selected->haulSourcePosition.value_or(glm::vec2{0.0F, 0.0F});
			task.haulTargetStorageId = selected->haulTargetStorageId;
			task.haulGoalId = selected->haulGoalId;
			task.haulTargetPosition = selected->haulTargetPosition.value_or(glm::vec2{0.0F, 0.0F});
			task.haulFromInventory = selected->haulFromInventory;
			// Craft-material hauls already carry the items, so head straight to the station.
			// Standard hauls go to the source first (pickup), then the destination.
			task.targetPosition = task.haulFromInventory ? task.haulTargetPosition : task.haulSourcePosition;
			// Check if goal has a chainId (linked to a prior Harvest)
			const auto* goal = GoalTaskRegistry::Get().getGoal(selected->haulGoalId);
			if (goal != nullptr && goal->chainId.has_value()) {
				task.chainId = goal->chainId;
				task.chainStep = 1; // Haul is step 1 of the Harvest→Haul chain
			} else {
				// Standalone haul - generate new chain ID for Pickup→Deposit
				task.chainId = generateChainId();
				task.chainStep = 0;
			}
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
			task.chainId = generateChainId();
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
				// Only a belief denial (mesh present, no route) is "can't find a way". Waiting
				// (mesh not built / off-area) and Unmeshed (headless) are ordinary travel states.
				task.navState = (outcome == NavRequestOutcome::Blocked) ? NavState::CantFindWayTo : NavState::Traveling;
				task.navStateHold = 0;
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
		// here -- deactivate movement and zero velocity -- instead of beelining blind. The task
		// stays; the periodic re-evaluation picks it back up the moment the mesh lands.
		if (!m_navSystem->hasMesh()) {
			if (auto* navPath = world->getComponent<NavPath>(entity)) {
				navPath->valid = false;
			}
			movementTarget.active = false;
			if (auto* velocity = world->getComponent<Velocity>(entity)) {
				velocity->value = {0.0F, 0.0F};
			}
			LOG_DEBUG(Engine, "[Nav] Entity %llu: no mesh yet, holding (nothing moves without a navmesh)",
				static_cast<unsigned long long>(entity));
			return NavRequestOutcome::Waiting;
		}

		// LOD seam: if either endpoint is outside the BUILT simulation area the exact mesh can't
		// answer the query (the off-area leg is beyond LOD0, and there is no coarse far-field graph
		// yet). A colonist may NOT slide blind toward an off-area goal -- that is the beeline this
		// architecture forbids. HOLD instead, exactly like the no-mesh case: deactivate movement,
		// zero velocity, invalidate any stale route. The periodic re-eval re-offers the task once
		// the camera-tracked sim area covers both endpoints (or the goal is rejected as unreachable).
		//
		// FUTURE LOD1: path to the area edge via LOD0, hand off to a coarse regional graph for the
		// long-haul leg, re-enter LOD0 near the goal. Until that lands, off-area goals are held.
		if (!m_navSystem->inSimArea(position.value) || !m_navSystem->inSimArea(goal)) {
			if (auto* navPath = world->getComponent<NavPath>(entity)) {
				navPath->valid = false;
			}
			movementTarget.active = false;
			if (auto* velocity = world->getComponent<Velocity>(entity)) {
				velocity->value = {0.0F, 0.0F};
			}
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
			// MovementSystem skips entities with an inactive target, so it won't zero the
			// velocity for us -- do it here, or the colonist coasts on its last velocity
			// straight into the wall it just refused to path through.
			if (auto* velocity = world->getComponent<Velocity>(entity)) {
				velocity->value = {0.0F, 0.0F};
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

	std::string AIDecisionSystem::formatOptionReason(const EvaluatedOption& option, const char* needName) {
		if (option.taskType == TaskType::Wander) {
			return "All needs satisfied";
		}

		std::string reason = needName;
		reason += " at " + std::to_string(static_cast<int>(option.needValue)) + "%";

		if (option.status == OptionStatus::NoSource) {
			reason += " (no known source)";
		} else if (option.needValue < 10.0F) {
			reason += " (critical)";
		} else if (option.status == OptionStatus::Available || option.status == OptionStatus::Selected) {
			if (option.distanceToTarget > 0.0F) {
				reason += " (" + std::to_string(static_cast<int>(option.distanceToTarget)) + "m away)";
			} else if (option.targetPosition.has_value()) {
				reason += " (using ground)";
			}
		} else if (option.status == OptionStatus::Satisfied) {
			reason += " (satisfied)";
		}

		return reason;
	}

	void AIDecisionSystem::handleChainInterruption(
		EntityID		entity,
		const Task&		task,
		Inventory&		inventory,
		const Position& position,
		TaskType		newTaskType,
		NeedType		newNeedType
	) {
		// Check if new task's first action needs hands
		if (!taskFirstActionNeedsHands(newTaskType, newNeedType)) {
			// New task doesn't need hands - colonist can keep carrying
			return;
		}

		// New task needs hands - must handle carried item
		const auto entityId = static_cast<unsigned long long>(entity);

		// For Haul tasks: item is in hands
		if (task.type == TaskType::Haul && !task.haulItemDefName.empty()) {
			// Verify item is actually in hands before operating
			if (!inventory.isHolding(task.haulItemDefName)) {
				LOG_WARNING(Engine, "[AI] Entity %llu: chain interrupted but not holding %s", entityId, task.haulItemDefName.c_str());
				return;
			}

			const auto* assetDef = m_registry.getDefinition(task.haulItemDefName);
			uint8_t		handsRequired = (assetDef != nullptr) ? assetDef->handsRequired : 1;

			if (handsRequired == 1) {
				// 1-handed item: try the belt (quick-draw slot) first, then the backpack. The item is
				// in hand, so a belt stow seats a copy on the belt and clears the hand.
				if (inventory.stowToBelt(task.haulItemDefName)) {
					inventory.putDown(task.haulItemDefName);
					LOG_INFO(Engine, "[AI] Entity %llu: chain interrupted, stowed %s to belt", entityId, task.haulItemDefName.c_str());
					return;
				}
				if (inventory.stowToBackpack(task.haulItemDefName)) {
					LOG_INFO(Engine, "[AI] Entity %llu: chain interrupted, stowed %s to backpack", entityId, task.haulItemDefName.c_str());
					return;
				}
				// Belt and backpack full - fall through to drop
				LOG_INFO(
					Engine, "[AI] Entity %llu: chain interrupted, dropping %s (belt and backpack full)", entityId, task.haulItemDefName.c_str()
				);
			} else {
				LOG_INFO(Engine, "[AI] Entity %llu: chain interrupted, dropping %s (2-handed)", entityId, task.haulItemDefName.c_str());
			}

			// Drop the item
			inventory.putDown(task.haulItemDefName);
			if (m_onDropItem) {
				m_onDropItem(task.haulItemDefName, position.value.x, position.value.y);
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

} // namespace ecs
