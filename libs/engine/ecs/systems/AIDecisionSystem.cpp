#include "AIDecisionSystem.h"

#include "../World.h"
#include "../components/Action.h"
#include "../components/Appearance.h"
#include "../components/DecisionTrace.h"
#include "../components/Inventory.h"
#include "../components/Memory.h"
#include "../components/MemoryQueries.h"
#include "../components/Movement.h"
#include "../components/Needs.h"
#include "../components/Packaged.h"
#include "../components/StorageConfiguration.h"
#include "../components/Task.h"
#include "../components/ToiletLocationFinder.h"
#include "../components/Transform.h"
#include "../components/WorkQueue.h"

#include "assets/AssetDefinition.h"
#include "assets/AssetRegistry.h"
#include "assets/ItemProperties.h"
#include "assets/RecipeDef.h"
#include "assets/RecipeRegistry.h"
#include "world/chunk/ChunkManager.h"

#include <utils/Log.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace ecs {

	namespace {

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

		/// Check if inventory contains any edible food item
		[[nodiscard]] bool hasEdibleFood(const Inventory& inventory) {
			for (const auto& edibleItemName : engine::assets::getEdibleItemNames()) {
				if (inventory.hasItem(edibleItemName)) {
					return true;
				}
			}
			return false;
		}

	} // namespace

	AIDecisionSystem::AIDecisionSystem(
		const engine::assets::AssetRegistry& registry,
		const engine::assets::RecipeRegistry& recipeRegistry,
		std::optional<uint32_t> rngSeed
	)
		: m_registry(registry),
		  m_recipeRegistry(recipeRegistry),
		  m_rng(rngSeed.value_or(std::random_device{}())) {}

	void AIDecisionSystem::update(float deltaTime) {
		// Process all entities with the required components
		for (auto [entity, position, needs, memory, task, movementTarget, inventory] :
			 world->view<Position, NeedsComponent, Memory, Task, MovementTarget, Inventory>()) {

			// Get optional Action component (may be nullptr if entity doesn't have one)
			auto* action = world->getComponent<Action>(entity);

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
				// Build full decision trace (always, for UI updates)
				buildDecisionTrace(entity, position, needs, memory, task, inventory, *trace);

				// Get the best option's priority
				const auto* selected = trace->getSelected();
				float		newPriority = (selected != nullptr) ? selected->calculatePriority() : 0.0F;

				// Check if the new task is actually different from current task
				bool isSameTask = false;
				if (selected != nullptr && task.isActive()) {
					// Compare task type
					bool sameType = (task.type == selected->taskType);

					// For wander tasks, same type is enough - don't interrupt just because target changed
					if (sameType && task.type == TaskType::Wander) {
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
						// For gather tasks, also check if targeting same entity
						bool sameGatherTarget = true;
						if (selected->taskType == TaskType::Gather) {
							// Both IDs must be valid (non-zero) to compare
							if (task.gatherTargetEntityId != 0U && selected->gatherTargetEntityId != 0U) {
								sameGatherTarget = (task.gatherTargetEntityId == selected->gatherTargetEntityId);
							} else {
								// At least one gather target is invalid/unset; treat as different targets
								sameGatherTarget = false;
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

						isSameTask = sameType && sameTarget && sameGatherTarget && samePlaceTarget;
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
					// Clear and assign new task
					task.clear();
					task.timeSinceEvaluation = 0.0F;
					selectTaskFromTrace(task, movementTarget, *trace, position);
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
		EntityID /*entity*/,
		const Position&		  position,
		const NeedsComponent& needs,
		const Memory&		  memory,
		const Task&			  currentTask,
		const Inventory&	  inventory,
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
			float nearestEdibleDist = std::numeric_limits<float>::max();

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

			// Check if colonist has all required inputs and track missing ones
			bool hasAllInputs = true;
			std::vector<std::pair<std::string, uint32_t>> missingInputs; // defName, countNeeded
			for (const auto& input : recipe->inputs) {
				uint32_t have = inventory.getQuantity(input.defName);
				if (have < input.count) {
					hasAllInputs = false;
					missingInputs.emplace_back(input.defName, input.count - have);
				}
			}

			// PHASE 6.2: Input Validation using Memory
			// Before adding gather options, verify ALL missing inputs have known sources in memory.
			// Colonist should only "know" they can craft if they've seen sources for everything.
			struct GatherSource {
				std::string inputDefName;
				KnownWorldEntity source;
				bool isHarvestable; // true = harvest, false = pickup
			};
			std::vector<GatherSource> gatherSources;
			bool allInputsObtainable = true;

			if (!hasAllInputs) {
				for (const auto& [inputDefName, countNeeded] : missingInputs) {
					bool foundSource = false;
					uint32_t inputDefNameId = m_registry.getDefNameId(inputDefName);

					// Look for Carryable sources (e.g., SmallStone on ground)
					// Optimize for total trip: colonist -> resource -> crafting station
					auto matchingCarryable = findOptimalForTrip(
						memory,
						position.value,
						stationPos.value,
						[&](const KnownWorldEntity& entity) {
							return entity.defNameId == inputDefNameId
								&& m_registry.hasCapability(entity.defNameId, engine::assets::CapabilityType::Carryable);
						}
					);

					if (matchingCarryable.has_value()) {
						gatherSources.push_back({inputDefName, *matchingCarryable, false});
						foundSource = true;
					} else {
						// Look for Harvestable sources that yield this item
						// Optimize for total trip: colonist -> resource -> crafting station
						auto harvestableSource = findOptimalForTrip(
							memory,
							position.value,
							stationPos.value,
							[&](const KnownWorldEntity& entity) {
								if (!m_registry.hasCapability(entity.defNameId, engine::assets::CapabilityType::Harvestable)) {
									return false;
								}
								const auto& defName = m_registry.getDefName(entity.defNameId);
								const auto* def = m_registry.getDefinition(defName);
								if (def == nullptr || !def->capabilities.harvestable.has_value()) {
									return false;
								}
								return def->capabilities.harvestable->yieldDefName == inputDefName;
							}
						);

						if (harvestableSource.has_value()) {
							gatherSources.push_back({inputDefName, *harvestableSource, true});
							foundSource = true;
						}
					}

					if (!foundSource) {
						// This input has no known source - colonist can't obtain it
						allInputsObtainable = false;
						break; // No need to check further
					}
				}
			}

			// Add craft option
			EvaluatedOption craftOption;
			craftOption.taskType = TaskType::Craft;
			craftOption.needType = NeedType::Count; // N/A for crafting
			craftOption.needValue = 100.0F;			// Not a need
			craftOption.threshold = 0.0F;
			craftOption.targetPosition = stationPos.value;
			craftOption.distanceToTarget = glm::distance(position.value, stationPos.value);
			craftOption.craftRecipeDefName = nextJob->recipeDefName;
			craftOption.stationEntityId = static_cast<uint64_t>(stationEntity);

			if (hasAllInputs) {
				craftOption.status = OptionStatus::Available;
				craftOption.reason = "Crafting " + recipe->label;
			} else if (allInputsObtainable) {
				// Missing materials but colonist knows where to get them all
				craftOption.status = OptionStatus::NoSource;
				craftOption.reason = "Crafting " + recipe->label + " (gathering materials)";
			} else {
				// Missing materials and colonist doesn't know where to find some
				craftOption.status = OptionStatus::NoSource;
				craftOption.reason = "Crafting " + recipe->label + " (unknown sources)";
			}

			trace.options.push_back(craftOption);

			// Only add gather options if ALL inputs are obtainable
			// This prevents partial gathering when colonist can't complete the recipe
			if (!hasAllInputs && allInputsObtainable) {
				for (const auto& gatherSource : gatherSources) {
					EvaluatedOption gatherOption;
					gatherOption.taskType = TaskType::Gather;
					gatherOption.needType = NeedType::Count;
					gatherOption.needValue = 100.0F;
					gatherOption.threshold = 0.0F;
					gatherOption.targetPosition = gatherSource.source.position;
					gatherOption.targetDefNameId = gatherSource.source.defNameId;
					gatherOption.distanceToTarget = glm::distance(position.value, gatherSource.source.position);
					gatherOption.gatherItemDefName = gatherSource.inputDefName;
					gatherOption.status = OptionStatus::Available;
					gatherOption.reason = "Gathering " + gatherSource.inputDefName + " for crafting";
					trace.options.push_back(gatherOption);
				}
			}
		}

		// Add "Haul" work options (Tier 6.4)
		// Find loose items (Carryable) and match them to storage containers
		for (const auto& [key, looseItem] : memory.knownWorldEntities) {
			// Check if entity is Carryable (loose item on ground)
			if (!m_registry.hasCapability(looseItem.defNameId, engine::assets::CapabilityType::Carryable)) {
				continue;
			}

			const auto& itemDefName = m_registry.getDefName(looseItem.defNameId);
			const auto* itemDef = m_registry.getDefinition(itemDefName);
			if (itemDef == nullptr) {
				continue;
			}

			// Get item category for storage matching
			engine::assets::ItemCategory itemCategory = itemDef->category;
			if (itemCategory == engine::assets::ItemCategory::None) {
				LOG_WARNING(Game, "Carryable item '%s' has no category - can only go to universal storage", itemDefName.c_str());
			}

			// Find a storage container that accepts this item category
			// Use ECS view to get actual entity IDs for storage containers with Inventory
			glm::vec2 nearestStoragePos{0.0F, 0.0F};
			float	  nearestStorageDist = std::numeric_limits<float>::max();
			uint64_t  nearestStorageEntityId = 0;
			bool	  foundStorage = false;

			for (auto [storageEntity, storagePos, storageInv, storageConfig] :
				 world->view<Position, Inventory, StorageConfiguration>()) {
				(void)storageInv; // Required in view query; capacity checking planned for future

				// Skip packaged storage containers - they're being moved and can't receive items
				if (world->hasComponent<Packaged>(storageEntity)) {
					continue;
				}

				// Use StorageConfiguration to check if this container accepts the item
				// This respects user-configured rules, not just static asset definitions
				if (!storageConfig.acceptsItem(itemDefName, itemCategory)) {
					continue;
				}

				// TODO: Check if storage has capacity remaining (query Inventory component)

				// Optimize for total trip: colonist -> item -> storage
				float totalTrip = glm::distance(position.value, looseItem.position)
								+ glm::distance(looseItem.position, storagePos.value);
				if (totalTrip < nearestStorageDist) {
					nearestStorageDist = totalTrip;
					nearestStoragePos = storagePos.value;
					nearestStorageEntityId = static_cast<uint64_t>(storageEntity);
					foundStorage = true;
				}
			}

			if (!foundStorage) {
				continue; // No storage container accepts this item
			}

			// Create haul option
			EvaluatedOption haulOption;
			haulOption.taskType = TaskType::Haul;
			haulOption.needType = NeedType::Count; // N/A for work tasks
			haulOption.needValue = 100.0F;
			haulOption.threshold = 0.0F;
			haulOption.targetPosition = looseItem.position; // Initial target is the loose item
			haulOption.targetDefNameId = looseItem.defNameId;
			haulOption.distanceToTarget = nearestStorageDist; // Total trip for fair priority comparison
			haulOption.haulItemDefName = itemDefName;
			// Get quantity from carryable capability (ensures deposit matches pickup)
			if (itemDef->capabilities.carryable.has_value()) {
				haulOption.haulQuantity = itemDef->capabilities.carryable.value().quantity;
			}
			haulOption.haulSourcePosition = looseItem.position;
			haulOption.haulTargetStorageId = nearestStorageEntityId;
			haulOption.haulTargetPosition = nearestStoragePos;
			haulOption.status = OptionStatus::Available;
			haulOption.reason = "Hauling " + itemDefName + " to storage";
			trace.options.push_back(haulOption);
		}

		// =====================================================================
		// Tier 6.35: Place Packaged Items
		// Find packaged items with targetPosition set (awaiting colonist delivery)
		// =====================================================================
		for (auto [packagedEntity, packagedPos, packaged, packagedAppearance] :
			 world->view<Position, Packaged, Appearance>()) {
			// Only consider items with a target position set
			if (!packaged.targetPosition.has_value()) {
				continue;
			}

			// Skip items being carried by a DIFFERENT colonist
			if (packaged.beingCarried) {
				// Check if THIS colonist is carrying it
				bool thisColonistCarrying =
					inventory.carryingPackagedEntity.has_value() &&
					inventory.carryingPackagedEntity.value() == static_cast<uint64_t>(packagedEntity);
				if (!thisColonistCarrying) {
					continue; // Someone else is carrying it
				}
			}

			const auto& targetPos = packaged.targetPosition.value();

			// After the filter above, if carryingPackagedEntity has a value it must be this entity
			// (otherwise we would have continued). So we can simplify the check.
			bool isCarryingThis = inventory.carryingPackagedEntity.has_value();

			// Create place packaged option
			EvaluatedOption placeOption;
			placeOption.taskType = TaskType::PlacePackaged;
			placeOption.needType = NeedType::Count; // N/A for work tasks
			placeOption.needValue = 100.0F;
			placeOption.threshold = 0.0F;

			if (isCarryingThis) {
				// Phase 2: Already carrying - go to placement target
				// High priority (150) to ensure colonist finishes delivery before other tasks
				placeOption.targetPosition = targetPos;
				placeOption.distanceToTarget = glm::distance(position.value, targetPos);
				placeOption.needValue = 150.0F; // Higher priority than most needs
			} else {
				// Phase 1: Need to pick up - go to source
				placeOption.targetPosition = packagedPos.value;
				placeOption.distanceToTarget = glm::distance(position.value, packagedPos.value);
			}

			placeOption.placePackagedEntityId = static_cast<uint64_t>(packagedEntity);
			placeOption.placeSourcePosition = packagedPos.value;
			placeOption.placeTargetPosition = targetPos;
			placeOption.status = OptionStatus::Available;
			placeOption.reason = isCarryingThis ? "Delivering " + packagedAppearance.defName
												: "Placing " + packagedAppearance.defName;
			trace.options.push_back(placeOption);
		}

		// Add wander option
		EvaluatedOption wanderOption;
		wanderOption.taskType = TaskType::Wander;
		wanderOption.needType = NeedType::Count; // N/A
		wanderOption.status = OptionStatus::Available;
		wanderOption.reason = "All needs satisfied";
		wanderOption.targetPosition = generateWanderTarget(position.value);
		trace.options.push_back(wanderOption);

		// Sort by priority (highest first)
		std::sort(trace.options.begin(), trace.options.end(), [](const auto& a, const auto& b) {
			return a.calculatePriority() > b.calculatePriority();
		});

		// Mark the first actionable option as Selected
		for (auto& option : trace.options) {
			if (option.status == OptionStatus::Available) {
				option.status = OptionStatus::Selected;
				trace.selectionSummary = "Selected: " + option.reason;
				break;
			}
		}
	}

	void AIDecisionSystem::selectTaskFromTrace(
		Task&				 task,
		MovementTarget&		 movementTarget,
		const DecisionTrace& trace,
		const Position&		 position
	) {
		const auto* selected = trace.getSelected();
		if (selected == nullptr) {
			// No actionable option - shouldn't happen, but fallback to wander
			task.type = TaskType::Wander;
			task.reason = "No actionable options";
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

		// Copy gathering-specific fields for Gather tasks
		if (selected->taskType == TaskType::Gather) {
			task.gatherItemDefName = selected->gatherItemDefName;
			task.gatherTargetEntityId = selected->gatherTargetEntityId;
		}

		// Copy hauling-specific fields for Haul tasks
		if (selected->taskType == TaskType::Haul) {
			task.haulItemDefName = selected->haulItemDefName;
			task.haulQuantity = selected->haulQuantity;
			task.haulSourcePosition = selected->haulSourcePosition.value_or(glm::vec2{0.0F, 0.0F});
			task.haulTargetStorageId = selected->haulTargetStorageId;
			task.haulTargetPosition = selected->haulTargetPosition.value_or(glm::vec2{0.0F, 0.0F});
			// For haul tasks, target is initially the source position (pickup first)
			task.targetPosition = task.haulSourcePosition;
		}

		// Copy placement-specific fields for PlacePackaged tasks
		if (selected->taskType == TaskType::PlacePackaged) {
			task.placePackagedEntityId = selected->placePackagedEntityId;
			task.placeSourcePosition = selected->placeSourcePosition.value_or(glm::vec2{0.0F, 0.0F});
			task.placeTargetPosition = selected->placeTargetPosition.value_or(glm::vec2{0.0F, 0.0F});
			// For place tasks, target is initially the source position (pickup first)
			task.targetPosition = task.placeSourcePosition;
		}

		movementTarget.target = task.targetPosition;

		// Check if ground fallback (already at target)
		bool isGroundFallback = (task.targetPosition == position.value);
		if (isGroundFallback) {
			movementTarget.active = false;
			task.state = TaskState::Arrived;
		} else {
			movementTarget.active = true;
			task.state = TaskState::Moving;
		}
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

} // namespace ecs
