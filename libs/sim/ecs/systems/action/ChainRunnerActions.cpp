// The one start path of every chain `do` step, and its builders: the action entry (an action group's member for the
// target), with the step's target, item, and amount as the action's effect context and target data. See
// /docs/technical/queries-and-chains.md (Chains, An action entry).

#include "../ActionSystem.h"

#include "../../GoalTaskRegistry.h"
#include "../../InventoryMass.h"
#include "../../World.h"
#include "../../components/Action.h"
#include "../../components/Inventory.h"
#include "../../components/Memory.h"
#include "../../components/Needs.h"
#include "../../components/Packaged.h"
#include "../../components/Skills.h"
#include "../../components/StructureBlueprint.h"
#include "../../components/Task.h"
#include "../../components/Transform.h"
#include "../../components/WorkQueue.h"
#include "../ChainClaims.h"

#include <assets/AssetRegistry.h>

#include <utils/Log.h>

#include <algorithm>
#include <random>
#include <variant>

namespace ecs {

	namespace {
		[[nodiscard]] uint32_t inboundHeld(const Task& task, uint32_t item) {
			uint32_t inbound = 0;
			for (const Claim& claim : task.claims) {
				if (claim.kind() == ClaimKind::Inbound && claim.itemDefNameId() == item) {
					inbound += claim.quantity();
				}
			}
			return inbound;
		}
	} // namespace

	assets::EntryHandle<assets::ActionEntry> ActionSystem::memberFor(const assets::ActionEntry& group, const ChainTarget& target) const {
		const auto& registry = assets::AssetRegistry::Get();
		const auto* packaged = target.entity != kInvalidEntity ? world->getComponent<Packaged>(target.entity) : nullptr;
		const auto* def = registry.getDefinitionById(target.defNameId);
		const bool	pile = target.kind == ChainTarget::Kind::Thing && def != nullptr && def->capabilities.carryable.has_value() &&
						  !def->capabilities.harvestable.has_value();
		for (const auto& member : group.members) {
			const std::optional<assets::ActionTarget>& on = chainWiring.library->actions[member.handle.index].on;
			if (!on.has_value()) {
				continue;
			}
			const bool matches = (on->kind == assets::ActionTarget::Kind::Packaged && packaged != nullptr) ||
								 (on->kind == assets::ActionTarget::Kind::LooseItem && pile) ||
								 (on->kind == assets::ActionTarget::Kind::Capability && packaged == nullptr &&
								  (target.kind == ChainTarget::Kind::Box ? on->capability == assets::CapabilityType::Storage
																		 : registry.hasCapability(target.defNameId, on->capability)));
			if (matches) {
				return member.handle;
			}
		}
		return {};
	}

	uint32_t ActionSystem::carriedForChain(const ChainActor& actor, uint32_t item) const {
		const auto&	   registry = assets::AssetRegistry::Get();
		const auto*	   def = registry.getDefinitionById(item);
		const uint32_t held = ecs::availableQuantity(actor.inventory, registry.getDefName(item));
		const uint64_t root = actor.task.chain.goal.rootGoalId;
		return root != 0 ? cargoToward(actor.task, root, item, held, def != nullptr && def->isEdible()) : held;
	}

	ActionSystem::DoStart ActionSystem::startChainAction(const ChainActor& actor, const assets::DoStep& step, ChainLift& alreadyHeld) {
		auto&			   registry = assets::AssetRegistry::Get();
		const ChainTarget& target = doTarget(actor.task, step);

		assets::EntryHandle<assets::ActionEntry> handle = step.action.handle;
		const assets::ActionEntry&				 named = chainWiring.library->actions[handle.index];
		if (named.group) {
			handle = memberFor(named, target);
			if (!handle.valid()) {
				// A remembered thing no member acts on is no longer what it was when the chain set out for it.
				if (const KnownWorldEntity* known =
						target.kind == ChainTarget::Kind::Thing ? actor.memory.getWorldEntity(target.key) : nullptr) {
					actor.memory.forgetWorldEntity(known->position, known->defNameId);
				}
				LOG_DEBUG(Engine, "[Chain] No member of action group %s acts on its target", named.id.c_str());
				return DoStart::Refused;
			}
		}
		const ActionValues& values = actionEntries.of(handle);
		Action				started = Action::fromEntry(ActionType::Chain, values);
		started.targetPosition = target.resolved() ? target.position : actor.position.value;

		bool takes = false;
		bool puts = false;
		bool works = false;
		bool crafts = false;
		bool installs = false;
		bool bySleepQuality = false;
		bool onTarget = false;
		bool consumes = false;
		for (const EffectRow& row : values.effects) {
			takes = takes || std::holds_alternative<TakeRow>(row);
			puts = puts || std::holds_alternative<PutRow>(row);
			works = works || std::holds_alternative<WorkRow>(row);
			crafts = crafts || std::holds_alternative<CraftRow>(row);
			installs = installs || std::holds_alternative<InstallRow>(row);
			consumes = consumes || std::holds_alternative<ConsumeRow>(row);
			if (const auto* restore = std::get_if<RestoreRow>(&row)) {
				bySleepQuality = bySleepQuality || restore->bySleepQuality;
				onTarget = onTarget || restore->onTarget;
			}
		}

		// An action that consumes starts only while the creature holds a unit; the one it sets out to use is what it
		// reports being about until its `<consume>` chooses.
		if (consumes) {
			const std::string* food = heldEdibleUnit(actor.inventory);
			if (food == nullptr) {
				LOG_DEBUG(Engine, "[Chain] %s consumes, and nothing it could consume is held", values.id.c_str());
				return DoStart::Refused;
			}
			started.context.consumedDefNameId = registry.getDefNameId(*food);
			started.context.nutrition = nutritionOf(*food);
		}
		if (bySleepQuality) {
			const bool thing = target.kind == ChainTarget::Kind::Thing || target.kind == ChainTarget::Kind::Entity;
			started.context.sleepQuality = !thing ? actionEntries.groundSleepQuality
										   : registry.hasCapability(target.defNameId, assets::CapabilityType::Sleepable) ? 1.0F
																														 : 0.0F;
		}
		if (onTarget) {
			if (target.entity == kInvalidEntity || world->getComponent<NeedsComponent>(target.entity) == nullptr) {
				LOG_WARNING(Engine, "[Chain] %s restores its target's need, and its target is no creature", values.id.c_str());
				return DoStart::Refused;
			}
			started.context.targetCreature = target.entity;
		}

		bool built = true;
		if (takes) {
			const DoStart take = buildTake(actor, step, target, started, alreadyHeld);
			if (take != DoStart::Started) {
				return take;
			}
		} else if (puts) {
			built = buildPut(actor, step, target, started);
		} else if (works) {
			built = buildWork(actor, target, started);
		} else if (crafts) {
			built = buildCraft(target, started);
		} else if (installs) {
			built = buildInstall(actor, target, started);
		}
		if (!built) {
			return DoStart::Refused;
		}
		if (values.duration == assets::ActionDuration::FromDurability && started.duration <= 0.0F) {
			LOG_WARNING(Engine, "[Chain] %s takes its time from a durability, and its target has none", values.id.c_str());
			return DoStart::Refused;
		}
		actor.action = std::move(started);
		return DoStart::Started;
	}

	ActionSystem::DoStart ActionSystem::buildTake(
		const ChainActor&	  actor,
		const assets::DoStep& step,
		const ChainTarget&	  target,
		Action&				  started,
		ChainLift&			  alreadyHeld
	) {
		auto&				 registry = assets::AssetRegistry::Get();
		Task&				 task = actor.task;
		const ChainProgress& progress = task.chain;
		const auto*			 packaged = target.entity != kInvalidEntity ? world->getComponent<Packaged>(target.entity) : nullptr;
		if (packaged != nullptr) {
			PlacePackagedEffect lift;
			lift.packagedEntityId = static_cast<uint64_t>(target.entity);
			lift.targetPosition = packaged->targetPosition.value_or(target.position);
			started.effect = lift;
			return DoStart::Started;
		}

		// The goods already carried beyond what the chain was handed and has taken: the take is done as having lifted them,
		// before anything about the source is asked.
		if (progress.goal.rootGoalId != 0 && target.item != 0) {
			const uint32_t taken = progress.takenItem == target.item ? progress.takenQuantity : 0U;
			const uint32_t carried = carriedForChain(actor, target.item);
			if (carried > progress.goal.cargo + taken) {
				alreadyHeld = {.item = target.item, .quantity = carried - progress.goal.cargo - taken};
				return DoStart::AlreadyHeld;
			}
		}

		const auto amountOf = [&](uint32_t item) -> uint32_t {
			if (step.amount == assets::StepAmount::Carried) {
				return carriedForChain(actor, item);
			}
			if (target.claimKind == assets::ClaimKind::SourceQuantity && target.claimQuantity > 0) {
				return target.claimQuantity;
			}
			return std::max(progress.goal.quantity, 1U);
		};

		if (target.kind == ChainTarget::Kind::Box) {
			if (world->getComponent<Inventory>(target.entity) == nullptr || world->hasComponent<Packaged>(target.entity)) {
				LOG_DEBUG(Engine, "[Chain] Box %llu is gone or being moved", static_cast<unsigned long long>(target.entity));
				return DoStart::Refused;
			}
			const std::string& item = registry.getDefName(target.item);
			const uint32_t	   quantity = amountOf(target.item);
			if (step.amount == assets::StepAmount::Meal) {
				makeRoomToEat(actor.inventory, task, item, quantity, actor.position.value);
			}
			CollectionEffect withdraw;
			withdraw.itemDefName = item;
			withdraw.quantity = quantity;
			withdraw.sourcePosition = target.position;
			withdraw.sourceDefName = item;
			withdraw.destroySource = false;
			withdraw.sourceStorageId = static_cast<uint64_t>(target.entity);
			started.effect = withdraw;
			return DoStart::Started;
		}

		const KnownWorldEntity* known = target.kind == ChainTarget::Kind::Thing ? actor.memory.getWorldEntity(target.key) : nullptr;
		if (known == nullptr) {
			LOG_DEBUG(Engine, "[Chain] Its take's target is no thing it remembers");
			return DoStart::Refused;
		}
		const auto* def = registry.getDefinitionById(known->defNameId);
		if (known->defNameId != target.defNameId || def == nullptr ||
			(!def->capabilities.harvestable.has_value() && !def->capabilities.carryable.has_value())) {
			actor.memory.forgetWorldEntity(known->position, known->defNameId);
			LOG_DEBUG(Engine, "[Chain] Its take's target is no longer what it was - forgot it");
			return DoStart::Refused;
		}
		CollectionEffect collection;
		collection.sourcePosition = known->position;
		collection.sourceDefName = def->defName;
		if (def->capabilities.harvestable.has_value()) {
			const auto& harvestable = *def->capabilities.harvestable;
			if (!ecs::inventoryHoldsToolType(actor.inventory, registry, harvestable.requiredToolType)) {
				LOG_DEBUG(Engine, "[Chain] Cannot cut %s without a %s", def->defName.c_str(), harvestable.requiredToolType.c_str());
				return DoStart::Refused;
			}
			uint32_t yield = harvestable.amountMin;
			if (harvestable.amountMax > harvestable.amountMin) {
				std::uniform_int_distribution<uint32_t> roll(harvestable.amountMin, harvestable.amountMax);
				yield = roll(m_rng);
			}
			if (step.amount == assets::StepAmount::Meal) {
				makeRoomToEat(actor.inventory, task, harvestable.yieldDefName, std::max(target.claimQuantity, 1U), actor.position.value);
			}
			collection.itemDefName = harvestable.yieldDefName;
			collection.quantity = yield;
			collection.destroySource = harvestable.destructive;
			collection.regrowthTime = harvestable.regrowthTime;
			// What he can't carry drops where he stands to work the source, or for a site clear just outside its footprint.
			collection.dropAt = actor.position.value;
			const bool		clears = std::ranges::any_of(started.values->effects, [](const EffectRow& row) {
				 const auto* take = std::get_if<TakeRow>(&row);
				 return take != nullptr && take->clears;
			 });
			const GoalTask* goal = clears ? GoalTaskRegistry::Get().getGoal(progress.goal.rootGoalId) : nullptr;
			if (goal != nullptr && goal->clearFootprint.has_value()) {
				collection.dropAt = goal->clearFootprint->nearestOutside(known->position, kClearDropMargin);
			}
			if (started.values->duration == assets::ActionDuration::FromDurability) {
				started.duration = harvestable.durability;
			}
		} else {
			const uint32_t quantity = amountOf(known->defNameId);
			if (step.amount == assets::StepAmount::Meal) {
				makeRoomToEat(actor.inventory, task, def->defName, quantity, actor.position.value);
			}
			collection.itemDefName = def->defName;
			collection.quantity = quantity;
			collection.destroySource = true;
		}
		started.effect = collection;
		return DoStart::Started;
	}

	bool ActionSystem::buildPut(const ChainActor& actor, const assets::DoStep& step, const ChainTarget& target, Action& started) {
		const ChainProgress& progress = actor.task.chain;
		const uint32_t item = progress.takenItem != 0 ? progress.takenItem : progress.goal.item != 0 ? progress.goal.item : target.item;
		if (item == 0 || target.entity == kInvalidEntity || world->hasComponent<Packaged>(target.entity)) {
			LOG_DEBUG(Engine, "[Chain] Its put has nothing to put, or nowhere to put it");
			return false;
		}
		const uint32_t held = carriedForChain(actor, item);
		uint32_t	   quantity = held;
		if (step.amount == assets::StepAmount::Carried) {
			// The cargo the task's claims cover.
			quantity = progress.goal.rootGoalId != 0 ? std::min(held, inboundHeld(actor.task, item)) : held;
		} else if (progress.goal.quantity > 0) {
			// For the goal, the default: no more than the goal plans, of what is carried.
			quantity = std::min(held, progress.goal.quantity + progress.goal.cargo);
		}
		DepositEffect deposit;
		deposit.itemDefName = assets::AssetRegistry::Get().getDefName(item);
		deposit.quantity = quantity;
		deposit.storageEntityId = static_cast<uint64_t>(target.entity);
		deposit.deliverToCraftStation = world->hasComponent<WorkQueue>(target.entity);
		started.effect = deposit;
		return true;
	}

	bool ActionSystem::buildWork(const ChainActor& actor, const ChainTarget& target, Action& started) {
		const auto* blueprint = target.entity != kInvalidEntity ? world->getComponent<StructureBlueprint>(target.entity) : nullptr;
		const auto& work = std::get<WorkRow>(started.values->effects[started.values->boundRow]);
		if (blueprint == nullptr || !workMayStart(work, *blueprint)) {
			LOG_DEBUG(Engine, "[Chain] Its work's target is no blueprint it may work");
			return false;
		}
		ProgressEffect progressEffect;
		progressEffect.targetEntityId = static_cast<uint64_t>(target.entity);
		if (const auto* skills = world->getComponent<Skills>(actor.entity)) {
			progressEffect.skillLevel = skills->getLevel(work.skill);
		}
		started.effect = progressEffect;
		return true;
	}

	bool ActionSystem::buildCraft(const ChainTarget& target, Action& started) {
		const auto*				  queue = target.entity != kInvalidEntity ? world->getComponent<WorkQueue>(target.entity) : nullptr;
		const CraftingJob*		  job = queue != nullptr ? queue->getNextJob() : nullptr;
		std::optional<CraftReady> ready =
			job != nullptr ? craftReady(job->recipeDefName, static_cast<uint64_t>(target.entity)) : std::nullopt;
		if (!ready) {
			LOG_DEBUG(Engine, "[Chain] Its craft's station has no job it can make now");
			return false;
		}
		started.duration = ready->workAmount / assets::kJobWorkTicksPerSecond;
		started.effect = std::move(ready->effect);
		return true;
	}

	bool ActionSystem::buildInstall(const ChainActor& actor, const ChainTarget& target, Action& started) {
		// The packaged thing it carries, else the option's target when that is a packaged thing, else the step's own.
		const auto packagedAt = [this](EntityID entity) {
			return entity != kInvalidEntity && world->hasComponent<Packaged>(entity);
		};
		const EntityID optionTarget = actor.task.chain.target.entity;
		EntityID	   placing = kInvalidEntity;
		if (actor.inventory.carryingPackagedEntity) {
			placing = static_cast<EntityID>(*actor.inventory.carryingPackagedEntity);
		} else if (packagedAt(optionTarget)) {
			placing = optionTarget;
		} else if (packagedAt(target.entity)) {
			placing = target.entity;
		}
		const auto* package = placing != kInvalidEntity ? world->getComponent<Packaged>(placing) : nullptr;
		if (package == nullptr || !package->targetPosition.has_value()) {
			LOG_DEBUG(Engine, "[Chain] Its install has no packaged thing with a placement");
			return false;
		}
		PlacePackagedEffect place;
		place.packagedEntityId = static_cast<uint64_t>(placing);
		place.targetPosition = *package->targetPosition;
		started.targetPosition = place.targetPosition;
		started.effect = place;
		return true;
	}

} // namespace ecs
