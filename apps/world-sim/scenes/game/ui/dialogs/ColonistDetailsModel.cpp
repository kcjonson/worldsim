#include "ColonistDetailsModel.h"

#include "scenes/game/ui/adapters/ColonistAdapter.h"
#include "scenes/game/ui/adapters/GlobalTaskAdapter.h"

#include <ecs/components/Colonist.h>
#include <ecs/components/Inventory.h>
#include <ecs/components/Memory.h>
#include <ecs/components/Mood.h>
#include <ecs/components/Needs.h>
#include <ecs/components/Task.h>
#include <ecs/components/Transform.h>

#include <ecs/InventoryMass.h>

#include <theme/Tokens.h>

#include <cmath>

namespace world_sim {

ColonistDetailsModel::UpdateType ColonistDetailsModel::refresh(ecs::World& world, ecs::EntityID colonistId) {
	// Check if colonist changed
	bool colonistChanged = (colonistId != currentColonistId);
	currentColonistId = colonistId;

	// Validate colonist exists
	const auto* colonist = world.getComponent<ecs::Colonist>(colonistId);
	if (colonist == nullptr) {
		valid = false;
		return colonistChanged ? UpdateType::Structure : UpdateType::None;
	}

	valid = true;

	// Extract all data
	extractBioData(world, colonistId);
	extractNeedsData(world, colonistId);
	extractSocialData();
	extractGearData(world, colonistId);
	extractMemoryData(world, colonistId);
	extractTasksData(world, colonistId);

	// Gear signature: hands, belt, and pack contents. Catches quantity-only
	// changes (a growing armful) that a stack-count diff would miss.
	std::string gearSig;
	auto appendSlot = [&gearSig](const std::optional<GearItem>& item) {
		gearSig += item.has_value() ? item->name + ":" + std::to_string(item->quantity) : "-";
		gearSig += "|";
	};
	appendSlot(gearData.leftHand);
	appendSlot(gearData.rightHand);
	appendSlot(gearData.belt[0]);
	appendSlot(gearData.belt[1]);
	for (const auto& item : gearData.items) {
		gearSig += item.name + ":" + std::to_string(item.quantity) + "|";
	}

	// Detect what changed
	if (colonistChanged) {
		// Save current values for next comparison
		prevNeedValues = needsData.needValues;
		prevMood = needsData.mood;
		prevGearSig = std::move(gearSig);
		prevMemoryCount = memoryData.totalKnown;
		prevTaskCount = tasksData.totalCount;
		prevBioTask = bioData.currentTask;
		prevCurrentProgress = tasksData.currentProgress;
		return UpdateType::Structure;
	}

	// Check for value changes
	bool valuesChanged = false;

	// Check needs
	for (size_t i = 0; i < 8; ++i) {
		if (std::abs(needsData.needValues[i] - prevNeedValues[i]) > 0.1F) {
			valuesChanged = true;
			break;
		}
	}

	// Check mood
	if (std::abs(needsData.mood - prevMood) > 0.5F) {
		valuesChanged = true;
	}

	// Check carried gear
	if (gearSig != prevGearSig) {
		valuesChanged = true;
	}

	// Check memory count
	if (memoryData.totalKnown != prevMemoryCount) {
		valuesChanged = true;
	}

	// Check task count
	if (tasksData.totalCount != prevTaskCount) {
		valuesChanged = true;
	}

	// Check the rendered bio task line (driven by task type/state/navState + labels).
	// extractBioData ran above, so bioData.currentTask reflects this frame's task.
	if (bioData.currentTask != prevBioTask) {
		valuesChanged = true;
	}

	// Check the running action's progress so the "Currently" meter animates as it fills.
	if (std::abs(tasksData.currentProgress - prevCurrentProgress) > 0.01F) {
		valuesChanged = true;
	}

	// Update previous values
	prevNeedValues = needsData.needValues;
	prevMood = needsData.mood;
	prevGearSig = std::move(gearSig);
	prevMemoryCount = memoryData.totalKnown;
	prevTaskCount = tasksData.totalCount;
	prevBioTask = bioData.currentTask;
	prevCurrentProgress = tasksData.currentProgress;

	return valuesChanged ? UpdateType::Values : UpdateType::None;
}

void ColonistDetailsModel::extractBioData(const ecs::World& world, ecs::EntityID colonistId) {
	const auto* colonist = world.getComponent<ecs::Colonist>(colonistId);
	if (colonist != nullptr) {
		bioData.name = colonist->name;
	} else {
		bioData.name = "Unknown";
	}

	// Placeholder data
	bioData.age = "--";
	bioData.traits.clear();
	bioData.background = "No background";

	// Get mood from needs
	const auto* needs = world.getComponent<ecs::NeedsComponent>(colonistId);
	if (needs != nullptr) {
		bioData.mood = ecs::computeMood(*needs);
		bioData.moodLabel = getMoodLabel(bioData.mood);
	} else {
		bioData.mood = 100.0F;
		bioData.moodLabel = "Unknown";
	}

	// Get current task
	const auto* task = world.getComponent<ecs::Task>(colonistId);
	if (task != nullptr && task->isActive()) {
		// For moving tasks, prefer the nav-state vocabulary so the player understands
		// belief-driven navigation ("Re-routing", "Can't find a way") as intent, not bug.
		if (task->state == ecs::TaskState::Moving) {
			switch (task->navState) {
				case ecs::NavState::Rerouting:
					bioData.currentTask = "Re-routing";
					bioData.currentTaskColor = UI::text;
					break;
				case ecs::NavState::CantFindWayTo:
					bioData.currentTask = "Can't find a way to target";
					bioData.currentTaskColor = UI::status_crit;
					break;
				case ecs::NavState::SearchingLKP:
					bioData.currentTask = "Searching for target";
					bioData.currentTaskColor = UI::status_warn;
					break;
				case ecs::NavState::LookingForWayIn:
					bioData.currentTask = "Looking for a way in";
					bioData.currentTaskColor = UI::status_warn;
					break;
				case ecs::NavState::Traveling:
				default: {
					// Build a destination label from the task type
					std::string dest;
					switch (task->type) {
						case ecs::TaskType::FulfillNeed:
							dest = (task->needToFulfill != ecs::NeedType::Count)
								? ecs::needLabel(task->needToFulfill)
								: "need";
							break;
						case ecs::TaskType::Craft:
							dest = "crafting station";
							break;
						case ecs::TaskType::Haul:
							dest = task->haulItemDefName.empty() ? "storage" : task->haulItemDefName;
							break;
						case ecs::TaskType::Build:
							dest = "build site";
							break;
						case ecs::TaskType::Deconstruct:
							dest = "structure";
							break;
						case ecs::TaskType::PlacePackaged:
							dest = "placement";
							break;
						case ecs::TaskType::Harvest:
							dest = "resource";
							break;
						default:
							dest = "target";
							break;
					}
					bioData.currentTask = "Going to " + dest;
					bioData.currentTaskColor = UI::status_ok;
					break;
				}
			}
		} else {
			// Not moving (Pending/Arrived or non-nav tasks)
			switch (task->type) {
				case ecs::TaskType::FulfillNeed:
					bioData.currentTask = (task->needToFulfill != ecs::NeedType::Count)
						? std::string("Fulfilling ") + ecs::needLabel(task->needToFulfill)
						: "Fulfilling need";
					bioData.currentTaskColor = UI::status_ok;
					break;
				case ecs::TaskType::Craft:
					bioData.currentTask = "Crafting " + task->craftRecipeDefName;
					bioData.currentTaskColor = UI::status_ok;
					break;
				case ecs::TaskType::Haul:
					bioData.currentTask = "Hauling " + (task->haulItemDefName.empty() ? "item" : task->haulItemDefName);
					bioData.currentTaskColor = UI::status_ok;
					break;
				case ecs::TaskType::Build:
					bioData.currentTask = "Building";
					bioData.currentTaskColor = UI::status_ok;
					break;
				case ecs::TaskType::Deconstruct:
					bioData.currentTask = "Deconstructing";
					bioData.currentTaskColor = UI::status_ok;
					break;
				case ecs::TaskType::PlacePackaged:
					bioData.currentTask = "Placing item";
					bioData.currentTaskColor = UI::status_ok;
					break;
				case ecs::TaskType::Harvest:
					bioData.currentTask = "Harvesting";
					bioData.currentTaskColor = UI::status_ok;
					break;
				case ecs::TaskType::Wander:
					bioData.currentTask = "Wandering";
					bioData.currentTaskColor = UI::text_dim;
					break;
				default:
					bioData.currentTask = "Idle";
					bioData.currentTaskColor = UI::text_dim;
					break;
			}
		}
	} else {
		bioData.currentTask = "Idle";
		bioData.currentTaskColor = UI::text_dim;
	}
}

void ColonistDetailsModel::extractNeedsData(const ecs::World& world, ecs::EntityID colonistId) {
	const auto* needs = world.getComponent<ecs::NeedsComponent>(colonistId);
	if (needs != nullptr) {
		for (size_t i = 0; i < 8; ++i) {
			const auto& need = needs->needs[i];
			needsData.needValues[i] = need.value;
			needsData.needsAttention[i] = need.needsAttention();
			needsData.isCritical[i] = need.isCritical();
		}
		needsData.mood = ecs::computeMood(*needs);
		needsData.moodLabel = getMoodLabel(needsData.mood);
	} else {
		needsData.needValues.fill(100.0F);
		needsData.needsAttention.fill(false);
		needsData.isCritical.fill(false);
		needsData.mood = 100.0F;
		needsData.moodLabel = "Unknown";
	}
}

void ColonistDetailsModel::extractSocialData() {
	// Placeholder - no social system yet
	socialData.placeholder = "Relationships not yet tracked";
}

void ColonistDetailsModel::extractGearData(const ecs::World& world, ecs::EntityID colonistId) {
	gearData = GearData{};

	const auto* inventory = world.getComponent<ecs::Inventory>(colonistId);
	if (inventory == nullptr) {
		return;
	}

	const auto& registry = engine::assets::AssetRegistry::Get();
	auto toItem = [&](const ecs::ItemStack& stack) {
		const auto* def = registry.getDefinition(stack.defName);
		std::string name = (def != nullptr && !def->label.empty()) ? def->label : stack.defName;
		const float kg = registry.getItemMassKg(stack.defName) * static_cast<float>(stack.quantity);
		return GearItem{std::move(name), stack.quantity, kg};
	};

	// Hands: a two-hand item mirrors identically across both hands; collapse the
	// mirror so the UI shows one spanning slot and the mass counts once. The
	// registry check matters: two separate one-hand items of the same type (an
	// axe in each hand) also match by name but are NOT a mirrored armful.
	gearData.bothHands = inventory->leftHand.has_value() && inventory->rightHand.has_value() &&
						 inventory->leftHand->defName == inventory->rightHand->defName &&
						 ecs::itemIsTwoHand(registry, inventory->leftHand->defName);
	if (inventory->leftHand.has_value()) {
		gearData.leftHand = toItem(*inventory->leftHand);
	}
	if (inventory->rightHand.has_value() && !gearData.bothHands) {
		gearData.rightHand = toItem(*inventory->rightHand);
	}

	for (size_t i = 0; i < inventory->belt.size(); ++i) {
		if (inventory->belt[i].has_value()) {
			gearData.belt[i] = toItem(*inventory->belt[i]);
			gearData.beltKg += gearData.belt[i]->totalKg;
		}
	}

	for (const auto& stack : inventory->getAllItems()) {
		gearData.items.push_back(toItem(stack));
	}
	gearData.slotCount = inventory->getSlotCount();
	gearData.maxSlots = inventory->maxCapacity;

	// Cargo weight (tools excluded) vs the colonist's strength-derived capacity
	gearData.cargoKg = ecs::carriedCargoMassKg(*inventory, registry);
	gearData.capacityKg = inventory->carryCapacityKg;

	// Everything carried, tools included: hands (mirror counted once) + belt + pack
	gearData.totalKg = gearData.beltKg;
	if (gearData.leftHand.has_value()) {
		gearData.totalKg += gearData.leftHand->totalKg;
	}
	if (gearData.rightHand.has_value()) {
		gearData.totalKg += gearData.rightHand->totalKg;
	}
	for (const auto& item : gearData.items) {
		gearData.totalKg += item.totalKg;
	}
}

void ColonistDetailsModel::extractMemoryData(const ecs::World& world, ecs::EntityID colonistId) {
	memoryData.categories.clear();
	memoryData.totalKnown = 0;

	const auto* memory = world.getComponent<ecs::Memory>(colonistId);
	if (memory == nullptr) {
		memoryData.sightRadius = ecs::kDefaultSightRadius;
		return;
	}

	memoryData.sightRadius = memory->sightRadius;

	auto& assetRegistry = engine::assets::AssetRegistry::Get();

	// Helper to build category from capability
	auto buildCategory = [&](const std::string& categoryName, engine::assets::CapabilityType capability) {
		MemoryCategory category;
		category.name = categoryName;

		const auto& entityKeys = memory->getEntitiesWithCapability(capability);
		category.count = entityKeys.size();

		// Limit displayed entities to avoid performance issues
		constexpr size_t kMaxDisplayedEntities = 100;
		size_t			 displayed = 0;

		for (uint64_t key : entityKeys) {
			if (displayed >= kMaxDisplayedEntities) {
				break;
			}

			const auto* entity = memory->getWorldEntity(key);
			if (entity != nullptr) {
				MemoryEntity memEntity;
				memEntity.name = assetRegistry.getDefName(entity->defNameId);
				memEntity.x = entity->position.x;
				memEntity.y = entity->position.y;
				category.entities.push_back(memEntity);
				++displayed;
			}
		}

		return category;
	};

	// Build categories
	memoryData.categories.push_back(buildCategory("Food Sources", engine::assets::CapabilityType::Edible));
	memoryData.categories.push_back(buildCategory("Water Sources", engine::assets::CapabilityType::Drinkable));
	memoryData.categories.push_back(buildCategory("Resources", engine::assets::CapabilityType::Harvestable));

	// Threats category - placeholder (no threat system yet)
	MemoryCategory threats;
	threats.name = "Threats";
	threats.count = 0;
	memoryData.categories.push_back(threats);

	// Known colonists/dynamic entities
	MemoryCategory colonists;
	colonists.name = "Other Colonists";
	colonists.count = memory->knownDynamicEntities.size();
	for (const auto& [entityId, knownEntity] : memory->knownDynamicEntities) {
		// Try to get colonist name
		const auto*	 colonist = world.getComponent<ecs::Colonist>(entityId);
		MemoryEntity memEntity;
		if (colonist != nullptr) {
			memEntity.name = colonist->name;
		} else {
			memEntity.name = "Unknown Entity";
		}
		memEntity.x = knownEntity.lastKnownPosition.x;
		memEntity.y = knownEntity.lastKnownPosition.y;
		colonists.entities.push_back(memEntity);
	}
	memoryData.categories.push_back(colonists);

	// Calculate total from category counts (not totalKnown() which includes entities with no capabilities)
	memoryData.totalKnown = 0;
	for (const auto& category : memoryData.categories) {
		memoryData.totalKnown += category.count;
	}
}

std::string ColonistDetailsModel::getMoodLabel(float mood) {
	if (mood >= 80.0F) {
		return "Happy";
	}
	if (mood >= 60.0F) {
		return "Content";
	}
	if (mood >= 40.0F) {
		return "Neutral";
	}
	if (mood >= 20.0F) {
		return "Stressed";
	}
	return "Miserable";
}

void ColonistDetailsModel::extractTasksData(ecs::World& world, ecs::EntityID colonistId) {
	// Get colonist position for distance calculations
	glm::vec2	colonistPosition{0.0F, 0.0F};
	const auto* position = world.getComponent<ecs::Position>(colonistId);
	if (position != nullptr) {
		colonistPosition = position->value;
	}

	// Get tasks known by this colonist via the adapter.
	tasksData.tasks = adapters::getTasksForColonist(colonistPosition);
	adapters::sortTasksForDisplay(tasksData.tasks);
	tasksData.totalCount = tasksData.tasks.size();

	// The "Currently" panel mirrors the bio task line (extractBioData ran first) and
	// adds the running action's progress for the meter.
	tasksData.currentTask = bioData.currentTask;
	tasksData.currentProgress = adapters::getColonistActivity(world, colonistId).progress;
}

} // namespace world_sim
