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

		// Route each output by category. FURNITURE (shelves, boxes, stations) is never seated in
		// the colonist's hands: it comes out PACKAGED and is installed a short distance off the
		// station via the existing PlacePackaged flow, so it can't overlap the station. Everything
		// else (tools, raw materials) goes through the canonical "give to colonist" cascade: an
		// empty hand, then a belt slot, then the backpack, then a loose ground drop -- weight-
		// respecting at every step. Keying on category (not handsRequired) is deliberate: Wood is
		// also two-hand but must stay a carryable resource, not become a packaged install.
		const glm::vec2 dropPos = action.targetPosition; // colonist crafts standing at the station
		for (const auto& [itemName, count] : craftEff.outputs) {
			const auto* outputDef = assetRegistry.getDefinition(itemName);
			const bool	isFurniture =
				outputDef != nullptr && outputDef->category == engine::assets::ItemCategory::Furniture;

			if (isFurniture) {
				// Spawn one packaged entity per unit at the station; the app resolves a valid spot
				// nearby and wires its targetPosition so the place pipeline installs it. Deferred
				// (ENQUEUE) -- the callback never spawns synchronously inside this view loop.
				if (m_onSpawnPackagedAt) {
					for (uint32_t i = 0; i < count; ++i) {
						m_onSpawnPackagedAt(itemName, dropPos);
					}
					LOG_INFO(Engine, "[Action] Crafted %u x %s (packaged for placement near station)", count, itemName.c_str());
				} else {
					LOG_WARNING(Engine, "[Action] Crafted furniture %s but no spawn-packaged callback set (lost)", itemName.c_str());
				}
				continue;
			}

			const uint32_t carried = ecs::giveItemToColonist(
				inventory, assetRegistry, itemName, count,
				[&](const std::string& def, uint32_t qty) {
					// Overflow drops as a loose ground pile (the harvest overflow mechanism), at the
					// colonist's position -- which is on the mesh, so the pile is reachable/haulable.
					if (m_onDropResource) {
						m_onDropResource(def, dropPos.x, dropPos.y, qty);
					} else {
						LOG_WARNING(Engine, "[Action] Crafted %s overflow but no drop callback set (lost)", def.c_str());
					}
				}
			);
			const uint32_t dropped = count - carried;
			if (dropped > 0) {
				LOG_INFO(Engine, "[Action] Crafted %u x %s (%u carried, %u dropped - no room)", count, itemName.c_str(), carried, dropped);
			} else {
				LOG_INFO(Engine, "[Action] Crafted %u x %s (added to inventory)", carried, itemName.c_str());
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
