#include "../ActionSystem.h"

#include "../../InventoryMass.h"
#include "../../StoreOrDrop.h"
#include "../../World.h"
#include "../../components/Action.h"
#include "../../components/Appearance.h"
#include "../../components/Inventory.h"
#include "../../components/Memory.h"
#include "../../components/Needs.h"
#include "../../components/Packaged.h"
#include "../../components/Task.h"
#include "../../components/Transform.h"

#include "assets/AssetRegistry.h"
#include "assets/ItemProperties.h"

#include <utils/Log.h>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <optional>
#include <random>

namespace ecs {

	namespace {
		/// A food's nutrition (0-1 scale), 0.3 for one whose definition gives none.
		[[nodiscard]] float nutritionOf(const std::string& food) {
			const auto edible = assets::getEdibleItemInfo(food);
			return edible ? edible->nutrition : 0.3F;
		}
	} // namespace

	void ActionSystem::startNeedAction(
		Task&				  task,
		Action&				  action,
		const Position&		  position,
		Memory&				  memory,
		const NeedsComponent& needs,
		Inventory&	  inventory
	) {
		auto& registry = assets::AssetRegistry::Get();

		// Handle FulfillNeed tasks
		switch (task.needToFulfill) {
			case NeedType::Hunger: {
				// Priority 1: food already held, the unit the eat's `<consume>` will use up.
				if (const std::string* food = heldEdibleUnit(inventory)) {
					const float nutrition = nutritionOf(*food);
					action = Action::Eat(actionEntries, registry.getDefNameId(*food), nutrition);
					LOG_DEBUG(
						Engine,
						"[Action] Creating Eat action for %s (nutrition %.2f, qty %u)",
						food->c_str(),
						nutrition,
						inventory.getQuantity(*food)
					);
				}

				// If we already created an eat action from inventory, we're done
				if (action.type == ActionType::Eat) {
					break;
				}

				// Priority 2: one meal from the storage box or loose pile the decision chose. The pickup or
				// withdraw puts it in the pack, the task ends, and the next decision eats it from there, as
				// after a harvest. The effect lifts only what is there and what fits; an emptied box or a
				// pile gone since it was seen lifts nothing, and the colonist decides again.
				if (task.needItemDefNameId != 0) {
					const std::string& food = registry.getDefName(task.needItemDefNameId);
					if (task.needSourceStorageId != 0) {
						const auto box = static_cast<EntityID>(task.needSourceStorageId);
						if (world->getComponent<Inventory>(box) == nullptr || world->hasComponent<Packaged>(box)) {
							LOG_DEBUG(Engine, "[Action] Food box %llu is gone or being moved", static_cast<unsigned long long>(task.needSourceStorageId));
							action.clear();
							break;
						}
						makeRoomToEat(inventory, task, food, task.needQuantity, position.value);
						action = Action::Withdraw(actionEntries, food, task.needQuantity, task.needSourceStorageId, task.workTarget());
					} else {
						makeRoomToEat(inventory, task, food, task.needQuantity, position.value);
						action = Action::Pickup(actionEntries, food, task.needQuantity, task.workTarget(), food);
					}
					LOG_DEBUG(Engine, "[Action] Taking %u x %s to eat", task.needQuantity, food.c_str());
					break;
				}

				// Priority 3: harvest the food source the decision chose (a bush to eat from, or to gather
				// food from), by its Memory key, so two sources at one spot can't swap places by memory's
				// hash order.
				if (const KnownWorldEntity* entity = memory.getWorldEntity(task.needSourceKey);
					entity != nullptr && registry.hasCapability(entity->defNameId, assets::CapabilityType::Harvestable)) {
					const auto& defName = registry.getDefName(entity->defNameId);
					const auto* def = registry.getDefinition(defName);
					if (def != nullptr && def->capabilities.harvestable.has_value() &&
						assets::isItemEdible(def->capabilities.harvestable->yieldDefName)) {
						const auto& harvestCap = def->capabilities.harvestable.value();

						// The decision passes over a regrowing bush, but one another colonist cut while this
						// one walked over still pays nothing at completion (takeFromSource).

						// An eater makes room for its meal as at a pile or box; a pack that could take none of
						// the yield would leave the bush standing ready and bring him back to it. Gathering
						// food sizes no meal and makes no room.
						if (task.needQuantity > 0) {
							makeRoomToEat(inventory, task, harvestCap.yieldDefName, task.needQuantity, position.value);
						}

						// Calculate random yield within range using proper RNG
						uint32_t yield = harvestCap.amountMin;
						if (harvestCap.amountMax > harvestCap.amountMin) {
							std::uniform_int_distribution<uint32_t> yieldDist(harvestCap.amountMin, harvestCap.amountMax);
							yield = yieldDist(m_rng);
						}

						action = Action::Harvest(
							actionEntries,
							harvestCap.yieldDefName,
							yield,
							harvestCap.durability,
							entity->position,
							def->defName,
							harvestCap.destructive,
							harvestCap.regrowthTime
						);
						action.collectionEffect().dropAt = task.targetPosition;
						LOG_DEBUG(
							Engine,
							"[Action] Creating Harvest action for %s → %u x %s",
							def->defName.c_str(),
							yield,
							harvestCap.yieldDefName.c_str()
						);
					}
				}

				// If we created a harvest action, we're done
				if (action.type == ActionType::Harvest) {
					break;
				}

				// No food in inventory and no harvestable at target
				// This indicates a desync between AI decision and world state.
				// Possible causes: entity was harvested by another colonist, or memory is stale.
				LOG_WARNING(
					Engine,
					"[Action] Hunger action failed at (%.1f, %.1f) - no food in inventory and no "
					"harvestable food source at target. Colonist will re-evaluate next tick.",
					task.targetPosition.x,
					task.targetPosition.y
				);
				action.clear();
				break;
			}

			case NeedType::Thirst: {
				// Water tiles are inexhaustible - drinking fully restores thirst
				action = Action::Drink(actionEntries);
				break;
			}

			case NeedType::Hygiene: {
				action = Action::Wash(actionEntries);
				break;
			}

			case NeedType::Energy: {
				// Check if at current position (ground fallback) or at a bed
				bool  isGroundFallback = (task.targetPosition == position.value);
				float quality = isGroundFallback ? actionEntries.groundSleepQuality : 1.0F;
				action = Action::Sleep(actionEntries, quality);
				break;
			}

			case NeedType::Bladder:
			case NeedType::Digestion: {
				// Smart Toilet: check both bladder and digestion needs
				// If both need attention, handle both efficiently
				bool needsPee = needs.bladder().needsAttention();
				bool needsPoop = needs.digestion().needsAttention();

				// Honor the committed task even if need value fluctuated above threshold.
				// The colonist traveled to this location for a reason; complete the action.
				// Also opportunistically handle the other need if it also needs attention.
				if (task.needToFulfill == NeedType::Bladder) {
					needsPee = true; // Always do what we came for
				}
				if (task.needToFulfill == NeedType::Digestion) {
					needsPoop = true; // Always do what we came for
				}

				action = Action::Toilet(actionEntries, position.value, needsPee, needsPoop);
				break;
			}

			case NeedType::Count:
				// Invalid need type - shouldn't happen
				LOG_ERROR(Engine, "[Action] Invalid need type in task: %d", static_cast<int>(task.needToFulfill));
				break;
		}
	}

	void ActionSystem::makeRoomToEat(Inventory& inventory, const Task& task, const std::string& food, uint32_t meal, glm::vec2 position) {
		auto&		   registry = assets::AssetRegistry::Get();
		const uint32_t claimedItem = task.suspended ? task.suspended->itemDefNameId : 0U;
		const bool	   twoHandFood = ecs::itemIsTwoHand(registry, food);
		const float	   foodMassKg = registry.getItemMassKg(food);
		const auto*	   foodDef = registry.getDefinition(food);
		const uint32_t foodStackCap = foodDef != nullptr && foodDef->itemProperties.has_value() ? foodDef->itemProperties->stackSize : UINT32_MAX;

		const auto isTool = [&](const std::string& defName) {
			const auto* def = registry.getDefinition(defName);
			return def != nullptr && def->category == assets::ItemCategory::Tool;
		};
		// Tools are equipment and never go; food is not in the way of food.
		const auto mayGo = [&](const std::string& defName) {
			const auto* def = registry.getDefinition(defName);
			return def == nullptr || (!isTool(defName) && !def->isEdible());
		};
		// Goods a suspended record carries are under claims, and go only once nothing else is left.
		const auto underClaim = [&](const std::string& defName) { return claimedItem != 0 && registry.getDefNameId(defName) == claimedItem; };
		const auto putAway = [&](const std::string& defName, uint32_t quantity) {
			ecs::storeOrDrop(*world, defName, quantity, position, m_onDropResource);
			LOG_DEBUG(Engine, "[Action] Put away %u x %s to make room to eat", quantity, defName.c_str());
		};
		// The last pack stack that may go, under no claim first.
		const auto packStackToGo = [&]() -> std::optional<size_t> {
			for (const bool claimed : {false, true}) {
				for (size_t index = inventory.items.size(); index-- > 0;) {
					const ItemStack& stack = inventory.items[index];
					if (mayGo(stack.defName) && underClaim(stack.defName) == claimed) {
						return index;
					}
				}
			}
			return std::nullopt;
		};

		for (;;) {
			const float carriedKg = ecs::carriedCargoMassKg(inventory, registry);
			if (ecs::pickupUnitsThatFit(inventory, registry, food, carriedKg) >= meal) {
				return;
			}

			// A slot: a pack stack for one-hand food, or the hands another armful holds for a two-hand one.
			uint32_t slotRoom = 0;
			if (twoHandFood) {
				const std::optional<ItemStack>& held = inventory.leftHand.has_value() ? inventory.leftHand : inventory.rightHand;
				if (held.has_value() && held->defName != food && ecs::itemIsTwoHand(registry, held->defName)) {
					if (!mayGo(held->defName)) {
						return;
					}
					const ItemStack armful = *held;
					ecs::removeFromHands(inventory, armful.defName, armful.quantity);
					putAway(armful.defName, armful.quantity);
					continue;
				}
				const uint32_t heldFood = ecs::handHeldQuantity(inventory, food);
				slotRoom = heldFood < foodStackCap ? foodStackCap - heldFood : 0U;
			} else {
				slotRoom = inventory.addableCount(food);
				if (slotRoom < meal) {
					if (const auto index = packStackToGo()) {
						const ItemStack stack = inventory.items[*index];
						inventory.items.erase(inventory.items.begin() + static_cast<std::ptrdiff_t>(*index));
						putAway(stack.defName, stack.quantity);
						continue;
					}
					// Only tools hold the pack's slots: one moved to the belt or a free hand frees a slot
					// and keeps the tool.
					const auto tool = std::find_if(inventory.items.rbegin(), inventory.items.rend(), [&](const ItemStack& stack) {
						return stack.quantity == 1 && isTool(stack.defName) && !ecs::itemIsTwoHand(registry, stack.defName);
					});
					if (tool != inventory.items.rend() && (inventory.beltHasFreeSlot() || inventory.freeHandCount() > 0)) {
						const std::string name = tool->defName;
						inventory.items.erase(std::next(tool).base());
						if (!inventory.stowToBelt(name)) {
							(inventory.rightHand.has_value() ? inventory.leftHand : inventory.rightHand) = ItemStack{name, 1};
						}
						continue;
					}
				}
			}

			// Weight: free what the meal (or what the slots let in) needs, and no more.
			const uint32_t target = std::min(meal, slotRoom);
			const uint32_t byWeight = ecs::massUnitsThatFit(inventory.carryCapacityKg - carriedKg, foodMassKg, UINT32_MAX);
			if (target == 0 || byWeight >= target) {
				return;
			}
			const float needKg = static_cast<float>(target) * foodMassKg - (inventory.carryCapacityKg - carriedKg);
			const auto	unitsFor = [&](const ItemStack& stack) {
				 const float mass = registry.getItemMassKg(stack.defName);
				 return std::min(stack.quantity, std::max(1U, static_cast<uint32_t>(std::ceil(needKg / mass))));
			};
			bool freed = false;
			for (const bool claimed : {false, true}) {
				for (size_t index = inventory.items.size(); !freed && index-- > 0;) {
					ItemStack& stack = inventory.items[index];
					if (!mayGo(stack.defName) || underClaim(stack.defName) != claimed || registry.getItemMassKg(stack.defName) <= 0.0F) {
						continue;
					}
					const std::string name = stack.defName;
					const uint32_t	  units = unitsFor(stack);
					stack.quantity -= units;
					if (stack.quantity == 0) {
						inventory.items.erase(inventory.items.begin() + static_cast<std::ptrdiff_t>(index));
					}
					putAway(name, units);
					freed = true;
				}
				for (std::optional<ItemStack>* hand : {&inventory.leftHand, &inventory.rightHand}) {
					if (freed || !hand->has_value() || !mayGo((*hand)->defName) || underClaim((*hand)->defName) != claimed ||
						registry.getItemMassKg((*hand)->defName) <= 0.0F) {
						continue;
					}
					const std::string name = (*hand)->defName;
					const uint32_t	  units = unitsFor(**hand);
					if (ecs::itemIsTwoHand(registry, name)) {
						ecs::removeFromHands(inventory, name, units); // keeps the two hands' mirror
					} else if (((*hand)->quantity -= units) == 0) {
						hand->reset();
					}
					putAway(name, units);
					freed = true;
				}
				if (freed) {
					break;
				}
			}
			if (!freed) {
				return;
			}
		}
	}

	const std::string* ActionSystem::heldEdibleUnit(const Inventory& inventory) {
		for (const std::string& food : assets::AssetRegistry::Get().edibleItemNames()) {
			if (inventory.hasItem(food)) {
				return &food;
			}
		}
		return nullptr;
	}

	bool ActionSystem::applyConsume(const ConsumeRow& row, Action& action, Inventory& inventory) {
		const std::string* unit = heldEdibleUnit(inventory);
		if (unit == nullptr) {
			LOG_WARNING(Engine, "[Action] Failed to consume: no %s unit held", assets::capabilityName(row.capability));
			return false;
		}
		const uint32_t removed = inventory.removeItem(*unit, row.count);
		action.context.consumedDefNameId = assets::AssetRegistry::Get().getDefNameId(*unit);
		action.context.nutrition = nutritionOf(*unit);
		LOG_INFO(Engine, "[Action] Consumed %u x %s from inventory", removed, unit->c_str());
		return true;
	}

	bool ActionSystem::applySpawn(const SpawnRow& row, glm::vec2 standing, Inventory& inventory) {
		auto&		registry = assets::AssetRegistry::Get();
		const auto* def = registry.getDefinition(row.def);
		const char* name = def != nullptr ? def->label.c_str() : row.def.c_str();
		if (row.intoHands) {
			giveItemToColonist(inventory, registry, row.def, 1, [&](const std::string& defName, uint32_t quantity) {
				storeOrDrop(*world, defName, quantity, standing, m_onDropResource);
			});
			LOG_INFO(Engine, "[Action] Spawned %s into the hands", name);
			return true;
		}
		auto spawned = world->createEntity();
		world->addComponent<Position>(spawned, Position{standing});
		world->addComponent<Rotation>(spawned, Rotation{0.0F});
		world->addComponent<Appearance>(spawned, Appearance{row.def, 1.0F, {1.0F, 1.0F, 1.0F, 1.0F}});
		LOG_INFO(Engine, "[Action] Spawned %s at (%.1f, %.1f)", name, standing.x, standing.y);
		return true;
	}

} // namespace ecs
