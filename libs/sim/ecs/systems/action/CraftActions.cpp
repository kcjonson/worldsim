#include "../ActionSystem.h"

#include "../../InventoryMass.h"
#include "../../StoreOrDrop.h"
#include "../../World.h"
#include "../../components/Action.h"
#include "../../components/Inventory.h"
#include "../../components/Task.h"
#include "../../components/WorkQueue.h"

#include "assets/AssetRegistry.h"
#include "assets/RecipeDef.h"
#include "assets/RecipeRegistry.h"

#include <utils/Log.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace ecs {

	namespace {
		using CraftInputs = std::vector<std::pair<std::string, uint32_t>>;

		/// The recipe's inputs, an item listed on several lines summed: the total is what checking and spending need.
		[[nodiscard]] CraftInputs craftInputs(const assets::RecipeDef& recipe) {
			CraftInputs required;
			for (const auto& input : recipe.inputs) {
				const auto line = std::ranges::find(required, input.defName, &std::pair<std::string, uint32_t>::first);
				if (line != required.end()) {
					line->second += input.count;
				} else {
					required.emplace_back(input.defName, input.count);
				}
			}
			return required;
		}

		/// The first input the station's store holds fewer of than the craft needs; null when it holds them all.
		[[nodiscard]] const std::pair<std::string, uint32_t>* firstMissingInput(const Inventory& store, const CraftInputs& inputs) {
			const auto missing = std::ranges::find_if(inputs, [&](const std::pair<std::string, uint32_t>& input) {
				return store.getQuantity(input.first) < input.second;
			});
			return missing != inputs.end() ? &*missing : nullptr;
		}
	} // namespace

	bool ActionSystem::applyCraft(const Action& action, Inventory& inventory) {
		if (!action.hasCraftingEffect()) {
			return refuseWithoutTarget(action, "craft");
		}
		const auto& craftEff = action.craftingEffect();

		auto& assetRegistry = assets::AssetRegistry::Get();
		auto* workQueue = world->getComponent<WorkQueue>(static_cast<EntityID>(craftEff.stationEntityId));

		// Consume inputs from the STATION's store, not the colonist's pack: the materials were
		// hauled into the station's Inventory during provisioning (mirroring construction, where
		// the build consumes from the on-site manifest). The crafted output goes to the colonist.
		// A store that no longer holds every input makes nothing and spends nothing. The inputs
		// were summed at the start.
		auto* stationInv = world->getComponent<Inventory>(static_cast<EntityID>(craftEff.stationEntityId));
		if (stationInv == nullptr || firstMissingInput(*stationInv, craftEff.inputs) != nullptr) {
			LOG_WARNING(
				Engine,
				"[Action] Craft of %s failed: station %llu no longer holds its inputs",
				craftEff.recipeDefName.c_str(),
				static_cast<unsigned long long>(craftEff.stationEntityId)
			);
			if (workQueue != nullptr) {
				workQueue->progress = 0.0F;
			}
			return false;
		}
		for (const auto& [itemName, count] : craftEff.inputs) {
			stationInv->removeItem(itemName, count);
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
				outputDef != nullptr && outputDef->category == assets::ItemCategory::Furniture;

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
					// Overflow goes into storage within reach, else a loose ground pile at the
					// colonist's position -- which is on the mesh, so the pile is reachable/haulable.
					ecs::storeOrDrop(*world, def, qty, dropPos, m_onDropResource);
				}
			);
			const uint32_t dropped = count - carried;
			if (dropped > 0) {
				LOG_INFO(Engine, "[Action] Crafted %u x %s (%u carried, %u stored nearby or dropped - no room)", count, itemName.c_str(), carried, dropped);
			} else {
				LOG_INFO(Engine, "[Action] Crafted %u x %s (added to inventory)", carried, itemName.c_str());
			}
		}

		// Fire notification callback for crafted item
		if (m_onItemCrafted) {
			auto&		recipeRegistry = assets::RecipeRegistry::Get();
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
		return true;
	}

	void ActionSystem::startCraftAction(Task& task, Action& action) {
		auto& recipeRegistry = assets::RecipeRegistry::Get();

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
		const CraftInputs inputs = craftInputs(*recipe);
		if (const auto* missing = firstMissingInput(*stationInv, inputs)) {
			LOG_WARNING(
				Engine, "[Action] Cannot craft %s - station missing %u x %s", recipe->label.c_str(), missing->second, missing->first.c_str()
			);
			action.clear();
			return;
		}

		// Build the outputs vector for the action

		std::vector<std::pair<std::string, uint32_t>> outputs;
		for (const auto& output : recipe->outputs) {
			outputs.emplace_back(output.defName, output.count);
		}

		// Create the craft action
		action = Action::Craft(
			actionEntries, task.craftRecipeDefName, task.targetStationId, task.targetPosition, recipe->workAmount, inputs, outputs
		);

		LOG_DEBUG(Engine, "[Action] Starting Craft action for %s (%.1fs duration)", recipe->label.c_str(), action.duration);
	}

} // namespace ecs
