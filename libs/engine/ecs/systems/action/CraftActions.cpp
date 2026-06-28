#include "../ActionSystem.h"

#include "../../InventoryMass.h"
#include "../../World.h"
#include "../../components/Action.h"
#include "../../components/Inventory.h"
#include "../../components/Task.h"
#include "../../components/WorkQueue.h"

#include "assets/AssetRegistry.h"
#include "assets/RecipeDef.h"
#include "assets/RecipeRegistry.h"

#include <utils/Log.h>

#include <string>
#include <utility>
#include <vector>

namespace ecs {

	void ActionSystem::applyCraftingEffect(const Action& action, Inventory& inventory) {
		const auto& craftEff = action.craftingEffect();

		auto& assetRegistry = engine::assets::AssetRegistry::Get();

		// Consume inputs from the STATION's store, not the colonist's pack: the materials were
		// hauled into the station's Inventory during provisioning (mirroring construction, where
		// the build consumes from the on-site manifest). The crafted output goes to the colonist.
		auto* stationInv = world->getComponent<Inventory>(static_cast<EntityID>(craftEff.stationEntityId));
		if (stationInv == nullptr) {
			LOG_WARNING(
				Engine,
				"[Action] Craft aborted: station %llu has no Inventory store to consume from",
				static_cast<unsigned long long>(craftEff.stationEntityId)
			);
			return;
		}
		for (const auto& [itemName, count] : craftEff.inputs) {
			uint32_t removed = stationInv->removeItem(itemName, count);
			if (removed < count) {
				LOG_WARNING(Engine, "[Action] Craft failed to consume %u x %s from station (only had %u)", count, itemName.c_str(), removed);
			}
		}

		// Add outputs to inventory (or drop on ground if non-backpackable)
		for (const auto& [itemName, count] : craftEff.outputs) {
			bool canBackpack = !ecs::itemIsTwoHand(assetRegistry, itemName);

			if (canBackpack) {
				uint32_t added = inventory.addItem(itemName, count);
				LOG_INFO(Engine, "[Action] Crafted %u x %s (added to inventory)", added, itemName.c_str());
			} else {
				// Non-backpackable item - drop on ground at crafting station
				if (m_onDropItem) {
					for (uint32_t i = 0; i < count; ++i) {
						m_onDropItem(itemName, action.targetPosition.x, action.targetPosition.y);
					}
					LOG_INFO(Engine, "[Action] Crafted %u x %s (dropped on ground)", count, itemName.c_str());
				} else {
					LOG_WARNING(Engine, "[Action] Crafted non-backpackable item %s but no drop callback set", itemName.c_str());
				}
			}
		}

		// Fire notification callback for crafted item
		if (m_onItemCrafted) {
			auto&		recipeRegistry = engine::assets::RecipeRegistry::Get();
			const auto* recipe = recipeRegistry.getRecipe(craftEff.recipeDefName);
			if (recipe != nullptr) {
				m_onItemCrafted(recipe->label);
			} else {
				LOG_WARNING(
					Engine,
					"[Action] Crafted item notification skipped: recipe '%s' not found in registry",
					craftEff.recipeDefName.c_str()
				);
			}
		}

		// Update WorkQueue on the station
		auto* workQueue = world->getComponent<WorkQueue>(static_cast<EntityID>(craftEff.stationEntityId));
		if (workQueue != nullptr) {
			auto* job = workQueue->getNextJob();
			if (job != nullptr && job->recipeDefName == craftEff.recipeDefName) {
				job->completed++;
				LOG_INFO(
					Engine, "[Action] Updated WorkQueue: %s %u/%u complete", job->recipeDefName.c_str(), job->completed, job->quantity
				);
				// Clean up completed jobs
				workQueue->cleanupCompleted();
			}
			// Reset progress for next item (or 0 if queue empty)
			workQueue->progress = 0.0F;
		}
	}

	void ActionSystem::startCraftAction(Task& task, Action& action) {
		auto& recipeRegistry = engine::assets::RecipeRegistry::Get();

		// Get the recipe
		const auto* recipe = recipeRegistry.getRecipe(task.craftRecipeDefName);
		if (recipe == nullptr) {
			LOG_ERROR(Engine, "[Action] Unknown recipe: %s", task.craftRecipeDefName.c_str());
			action.clear();
			return;
		}

		// Verify the station's store holds all required inputs (hauled in during provisioning).
		// This mirrors construction gating on materialsComplete() before the build proceeds.
		const auto* stationInv = world->getComponent<Inventory>(static_cast<EntityID>(task.targetStationId));
		if (stationInv == nullptr) {
			LOG_WARNING(Engine, "[Action] Cannot craft %s - station has no Inventory store", recipe->label.c_str());
			action.clear();
			return;
		}
		for (const auto& input : recipe->inputs) {
			if (stationInv->getQuantity(input.defName) < input.count) {
				LOG_WARNING(
					Engine, "[Action] Cannot craft %s - station missing %u x %s", recipe->label.c_str(), input.count, input.defName.c_str()
				);
				action.clear();
				return;
			}
		}

		// Build inputs and outputs vectors for the action
		std::vector<std::pair<std::string, uint32_t>> inputs;
		for (const auto& input : recipe->inputs) {
			inputs.emplace_back(input.defName, input.count);
		}

		std::vector<std::pair<std::string, uint32_t>> outputs;
		for (const auto& output : recipe->outputs) {
			outputs.emplace_back(output.defName, output.count);
		}

		// Create the craft action
		action = Action::Craft(task.craftRecipeDefName, task.targetStationId, task.targetPosition, recipe->workAmount, inputs, outputs);

		LOG_DEBUG(Engine, "[Action] Starting Craft action for %s (%.1fs duration)", recipe->label.c_str(), action.duration);
	}

} // namespace ecs
