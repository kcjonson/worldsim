// The chain runner: a Task of type Chain runs its chain's steps in order on the Task, Action, and routing machinery,
// and every `do` step starts its action through one path with no factory. See /docs/technical/queries-and-chains.md
// (Chains, The runner, Requirements on the chain runner).

#include "../ActionSystem.h"

#include "../../GoalTaskRegistry.h"
#include "../../InventoryMass.h"
#include "../../StoreOrDrop.h"
#include "../../World.h"
#include "../../components/Action.h"
#include "../../components/AgentRadius.h"
#include "../../components/Inventory.h"
#include "../../components/Memory.h"
#include "../../components/Movement.h"
#include "../../components/NavPath.h"
#include "../../components/Needs.h"
#include "../../components/Packaged.h"
#include "../../components/Skills.h"
#include "../../components/SourceReachMemo.h"
#include "../../components/StructureBlueprint.h"
#include "../../components/Task.h"
#include "../../components/Thoughts.h"
#include "../../components/Traits.h"
#include "../../components/Transform.h"
#include "../../components/WorkQueue.h"
#include "../AIDecisionSystem.h"
#include "../ChainClaims.h"
#include "../NavigationSystem.h"
#include "../ThoughtSystem.h"
#include "../TimeSystem.h"
#include "../VisionSystem.h"

#include <assets/AssetRegistry.h>
#include <assets/PriorityConfig.h>

#include <utils/Log.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <random>
#include <variant>

namespace ecs {

	namespace {

		/// A target that is not a creature has gone when it is this far from where it was; a creature target has left.
		constexpr float kTargetMovedMeters = 0.5F;

		[[nodiscard]] uint8_t bit(assets::ClaimKind kind) {
			return static_cast<uint8_t>(1U << static_cast<unsigned>(kind));
		}

		[[nodiscard]] bool names(uint8_t bits, assets::EndEvent event) {
			return (bits & (1U << static_cast<unsigned>(event))) != 0;
		}

		[[nodiscard]] const ChainTarget& optionTarget(const Task& task, assets::StepTarget which) {
			switch (which) {
				case assets::StepTarget::Source:
					return task.chain.source;
				case assets::StepTarget::Destination:
					return task.chain.destination;
				case assets::StepTarget::Target:
					break;
			}
			return task.chain.target;
		}

		/// The claim `target` takes as a use, or as `quantity` units of its item from a pile or box, in its own key space.
		[[nodiscard]] Claim claimOn(const ChainTarget& target, bool sourceQuantity, uint32_t quantity) {
			TargetKind space = static_cast<TargetKind>(target.claimSpace);
			uint64_t   key = target.claimKey;
			if (key == 0) {
				space = target.kind == ChainTarget::Kind::Thing ? TargetKind::WorldKey : TargetKind::Entity;
				key = target.kind == ChainTarget::Kind::Thing ? target.key : static_cast<uint64_t>(target.entity);
			}
			return sourceQuantity ? Claim::sourceQuantity(space, key, target.item, quantity) : Claim::use(space, key);
		}

		/// Units of goods with `capability` the creature carries in its hands and pack.
		[[nodiscard]] uint32_t heldWithCapability(const Inventory& inventory, assets::CapabilityType capability) {
			const auto& registry = assets::AssetRegistry::Get();
			uint32_t	held = 0;
			const auto	count = [&](const std::string& defName, bool counted) {
				 // Food is an item's property, not a capability of the thing.
				 const auto* def = registry.getDefinition(defName);
				 const bool	 has = capability == assets::CapabilityType::Edible ? def != nullptr && def->isEdible()
																			: registry.hasCapability(registry.getDefNameId(defName), capability);
				 if (!counted && has) {
					 held += ecs::availableQuantity(inventory, defName);
				 }
			};
			for (std::size_t index = 0; index < inventory.items.size(); ++index) {
				const std::string& name = inventory.items[index].defName;
				const bool		   earlier = std::any_of(inventory.items.begin(), inventory.items.begin() + static_cast<std::ptrdiff_t>(index),
														 [&](const ItemStack& stack) { return stack.defName == name; });
				count(name, earlier);
			}
			const auto inPack = [&](const std::string& name) {
				return std::any_of(inventory.items.begin(), inventory.items.end(), [&](const ItemStack& stack) { return stack.defName == name; });
			};
			if (inventory.leftHand.has_value()) {
				count(inventory.leftHand->defName, inPack(inventory.leftHand->defName));
			}
			if (inventory.rightHand.has_value()) {
				const bool sameAsLeft = inventory.leftHand.has_value() && inventory.leftHand->defName == inventory.rightHand->defName;
				count(inventory.rightHand->defName, sameAsLeft || inPack(inventory.rightHand->defName));
			}
			return held;
		}

	} // namespace

	ChainTarget chainTargetOf(const queries::QueryCandidate& candidate) {
		ChainTarget target;
		switch (candidate.kind) {
			case queries::CandidateKind::Thing:
				target.kind = ChainTarget::Kind::Thing;
				break;
			case queries::CandidateKind::Box:
				target.kind = ChainTarget::Kind::Box;
				break;
			case queries::CandidateKind::Mover:
				target.kind = ChainTarget::Kind::Mover;
				break;
			case queries::CandidateKind::Place:
			case queries::CandidateKind::Spot:
				target.kind = ChainTarget::Kind::Place;
				break;
			case queries::CandidateKind::Destination:
				target.kind = ChainTarget::Kind::Entity;
				break;
		}
		target.key = candidate.key;
		target.entity = candidate.entity;
		target.defNameId = candidate.defNameId;
		target.item = candidate.item;
		target.position = candidate.position;
		target.goal = candidate.goal;
		target.claimKind = candidate.claimKind;
		target.claimSpace = static_cast<uint8_t>(candidate.claimTarget.kind);
		target.claimKey = candidate.claimTarget.key;
		target.claimQuantity = candidate.claimQuantity;
		return target;
	}

	double ActionSystem::clockSeconds() const {
		const auto* time = world->tryGetSystem<TimeSystem>();
		return time != nullptr ? time->simulationSeconds() : 0.0;
	}

	bool ActionSystem::startChain(EntityID entity, const ChainStart& start) {
		auto* task = world->getComponent<Task>(entity);
		auto* action = world->getComponent<Action>(entity);
		auto* position = world->getComponent<Position>(entity);
		auto* needs = world->getComponent<NeedsComponent>(entity);
		auto* memory = world->getComponent<Memory>(entity);
		auto* inventory = world->getComponent<Inventory>(entity);
		if (task == nullptr || action == nullptr || position == nullptr || needs == nullptr || memory == nullptr || inventory == nullptr) {
			LOG_ERROR(Engine, "[Chain] Entity %llu lacks what a chain runs on; nothing started", static_cast<unsigned long long>(entity));
			return false;
		}
		if (chainWiring.library == nullptr || chainWiring.chains == nullptr || chainWiring.evaluator == nullptr || !start.chain.valid()) {
			LOG_ERROR(Engine, "[Chain] Entity %llu: the chain runner is not wired, or no chain was named; nothing started",
					  static_cast<unsigned long long>(entity));
			return false;
		}
		const ChainActor actor{.entity = entity, .position = *position, .task = *task, .action = *action, .needs = *needs, .memory = *memory,
							   .inventory = *inventory};
		const assets::ChainEntry& chain = chainWiring.library->chains[start.chain.index];

		// The action in progress goes with the task it served, and a route still out was asked for that task.
		action->clear();
		if (task->routeTicket != 0) {
			if (auto* nav = world->tryGetSystem<NavigationSystem>()) {
				nav->cancelRoute(task->routeTicket);
			}
			task->routeTicket = 0;
		}
		task->clear();
		task->timeSinceEvaluation = 0.0F;
		task->type = TaskType::Chain;
		task->priorityTier = start.tier;
		task->reason = chain.id;
		const uint64_t sequence = GoalTaskRegistry::Get().allocateClaimSeq();
		task->instanceId = sequence;
		task->claimSeq = sequence;
		ChainProgress& progress = task->chain;
		progress.frames[0] = {.chain = start.chain, .step = start.step};
		progress.depth = 1;
		progress.target = start.target;
		progress.source = start.source;
		progress.destination = start.destination;
		progress.goal = start.goal;

		// The claims of every step whose target the option already has are taken now; a step whose target a query
		// finds later takes its claims when it is found.
		ChainTarget walked;
		for (std::size_t index = start.step; index < chain.steps.size(); ++index) {
			const assets::ChainStep& step = chain.steps[index];
			if (const auto* go = std::get_if<assets::GoStep>(&step.step)) {
				walked = go->to ? optionTarget(*task, *go->to) : (index == start.step ? start.target : ChainTarget{});
			} else if (const auto* doStep = std::get_if<assets::DoStep>(&step.step); doStep != nullptr && doStep->claims != 0) {
				const ChainTarget& target = doStep->on ? optionTarget(*task, *doStep->on) : walked;
				if (target.resolved() && !takeStepClaims(*task, *doStep, target)) {
					endChain(actor, "a claim its steps name cannot be sized: inbound and in_flight need the option's goal and destination");
					return false;
				}
			}
		}
		const auto* first = start.step < chain.steps.size() ? std::get_if<assets::GoStep>(&chain.steps[start.step].step) : nullptr;
		return runSteps(actor, first != nullptr && start.target.resolved() ? &start.target : nullptr);
	}

	const assets::ChainStep& ActionSystem::currentStep(const Task& task) const {
		const ChainFrame& frame = task.chain.frame();
		return chainWiring.library->chains[frame.chain.index].steps[frame.step];
	}

	void ActionSystem::nextStep(Task& task) {
		++task.chain.frame().step;
	}

	void ActionSystem::endChain(const ChainActor& actor, const char* why) {
		LOG_DEBUG(Engine, "[Chain] Entity %llu: %s ends (%s)", static_cast<unsigned long long>(actor.entity), actor.task.reason.c_str(), why);
		actor.action.clear();
		actor.task.clear();
		actor.task.timeSinceEvaluation = 0.0F;
	}

	bool ActionSystem::runSteps(const ChainActor& actor, const ChainTarget* given) {
		Task&		   task = actor.task;
		ChainProgress& progress = task.chain;
		for (;;) {
			ChainFrame&				  frame = progress.frame();
			const assets::ChainEntry& chain = chainWiring.library->chains[frame.chain.index];
			if (frame.step >= chain.steps.size()) {
				if (--progress.depth == 0) {
					endChain(actor, "finished");
					return false;
				}
				nextStep(task);
				continue;
			}
			const assets::ChainStep& step = chain.steps[frame.step];
			const ChainStepValues&	 values = chainWiring.chains->of(frame.chain, frame.step);
			progress.stepStartedAt = clockSeconds();

			if (const auto* go = std::get_if<assets::GoStep>(&step.step)) {
				const ChainTarget target = given != nullptr ? *given : goTarget(actor, *go, std::nullopt);
				given = nullptr;
				if (!target.resolved()) {
					endChain(actor, "its go step found no target");
					return false;
				}
				progress.stepTarget = target;
				if (const assets::DoStep* next = doStepAfterGo(task); next != nullptr && next->claims != 0 && !takeStepClaims(task, *next, target)) {
					endChain(actor, "a claim its steps name cannot be sized");
					return false;
				}
				if (glm::distance(actor.position.value, target.goal) <= kActionReachMeters) {
					nextStep(task);
					continue;
				}
				headTo(actor.entity, task, target.goal);
				if (target.goal != target.position) {
					task.obstructedTarget = target.position;
				}
				return true;
			}
			if (const auto* doStep = std::get_if<assets::DoStep>(&step.step)) {
				// Where the creature stands: nothing moves it while it acts.
				task.state = TaskState::Arrived;
				task.targetPosition = actor.position.value;
				if (auto* movementTarget = world->getComponent<MovementTarget>(actor.entity)) {
					movementTarget->active = false;
				}
				if (auto* navPath = world->getComponent<NavPath>(actor.entity)) {
					navPath->valid = false;
				}
				progress.heldItem = 0;
				progress.heldAtStart = 0;
				if (names(step.ends, assets::EndEvent::HeldGone)) {
					const assets::ActionEntry& entry = chainWiring.library->actions[doStep->action.handle.index];
					const bool consumes = !entry.group && std::ranges::any_of(actionEntries.of(doStep->action.handle).effects, [](const EffectRow& row) {
						return std::holds_alternative<ConsumeRow>(row);
					});
					progress.heldItem = consumes ? 0U : (progress.takenItem != 0 ? progress.takenItem : progress.goal.item);
					progress.heldAtStart = heldForStep(actor, progress.heldItem);
				}
				return true;
			}
			if (const auto* have = std::get_if<assets::HaveStep>(&step.step)) {
				if (haveMet(actor, *have)) {
					nextStep(task);
				} else {
					progress.frames[progress.depth++] = {.chain = have->fetch.handle, .step = 0};
				}
				continue;
			}
			if (const auto* run = std::get_if<assets::RunStep>(&step.step)) {
				progress.frames[progress.depth++] = {.chain = run->chain.handle, .step = 0};
				continue;
			}
			if (const auto* think = std::get_if<assets::ThinkStep>(&step.step)) {
				giveThought(actor, *think, values.thought);
				nextStep(task);
				continue;
			}
			if (const auto* need = std::get_if<assets::NeedStep>(&step.step)) {
				actor.needs.get(values.need).move(need->change);
				nextStep(task);
				continue;
			}
			if (std::holds_alternative<assets::DropStep>(step.step)) {
				dropTaken(actor);
				nextStep(task);
				continue;
			}
			LOG_ERROR(Engine, "[Chain] %s: <leave_colony/> needs colony membership, which the sim does not have yet", chain.id.c_str());
			endChain(actor, "it asked to leave the colony, which the sim cannot do yet");
			return false;
		}
	}

	void ActionSystem::runChain(float deltaTime, const ChainActor& actor) {
		Task&	task = actor.task;
		Action& action = actor.action;
		if (chainWiring.library == nullptr || chainWiring.chains == nullptr || chainWiring.evaluator == nullptr || !task.chain.active()) {
			if (!reportedUnwiredChains) {
				LOG_ERROR(Engine, "[Chain] Entity %llu runs a chain, but the chain runner is not wired; its task ends",
						  static_cast<unsigned long long>(actor.entity));
				reportedUnwiredChains = true;
			}
			endChain(actor, "the chain runner is not wired");
			return;
		}

		const assets::ChainStep& step = currentStep(task);
		if (step.ends != 0 && stepEndEvent(actor, step.ends)) {
			action.clear();
			nextStep(task);
			runSteps(actor, nullptr);
			return;
		}

		if (const auto* go = std::get_if<assets::GoStep>(&step.step)) {
			if (task.state == TaskState::Moving) {
				// A remembered target forgotten on the way: a query is asked again, a target the option named is gone.
				const ChainTarget& target = task.chain.stepTarget;
				if (target.kind == ChainTarget::Kind::Thing && actor.memory.getWorldEntity(target.key) == nullptr) {
					const ChainTarget again = go->query ? goTarget(actor, *go, std::nullopt) : ChainTarget{};
					if (!again.resolved()) {
						endChain(actor, "the target it walked to is gone");
						return;
					}
					const uint64_t oldKey = claimOn(target, false, 0).target();
					task.claims.eraseIf([oldKey](const Claim& claim) {
						return (claim.kind() == ClaimKind::Use || claim.kind() == ClaimKind::SourceQuantity) && claim.target() == oldKey;
					});
					task.chain.stepTarget = again;
					if (const assets::DoStep* next = doStepAfterGo(task); next != nullptr && next->claims != 0) {
						takeStepClaims(task, *next, again);
					}
					headTo(actor.entity, task, again.goal);
					if (again.goal != again.position) {
						task.obstructedTarget = again.position;
					}
				}
				if (glm::distance(actor.position.value, task.targetPosition) <= kActionReachMeters) {
					task.state = TaskState::Arrived;
				}
			}
			if (task.state != TaskState::Arrived) {
				return;
			}
			nextStep(task);
			if (!runSteps(actor, nullptr)) {
				return;
			}
		}

		const auto* doStep = std::get_if<assets::DoStep>(&currentStep(task).step);
		if (doStep == nullptr) {
			return;
		}
		if (!action.isActive()) {
			ChainLift alreadyHeld;
			switch (startChainAction(actor, *doStep, alreadyHeld)) {
				case DoStart::Refused:
					endChain(actor, "its do step started nothing");
					return;
				case DoStart::AlreadyHeld:
					doStepDone(actor, *doStep, alreadyHeld);
					nextStep(task);
					runSteps(actor, nullptr);
					return;
				case DoStart::Started:
					actionStarted(actor.entity, task, action, actor.memory);
					break;
			}
		}

		// Every action's first tick: where it started is where its spawn lands.
		if (action.state == ActionState::Starting) {
			action.startPosition = actor.position.value;
		}
		const Actor rows{.entity = actor.entity, .position = actor.position.value, .needs = actor.needs, .inventory = actor.inventory, .memory = actor.memory};
		advanceAction(deltaTime, rows, action);
		if (action.hasCraftingEffect() && action.isActive()) {
			if (auto* workQueue = world->getComponent<WorkQueue>(static_cast<EntityID>(action.craftingEffect().stationEntityId))) {
				workQueue->progress = action.progress();
			}
		}
		if (!action.isComplete()) {
			return;
		}
		// What a take lifts is what its collection adds to the creature.
		const std::string* lifting = action.hasCollectionEffect() ? &action.collectionEffect().itemDefName : nullptr;
		const uint32_t	   heldBefore = lifting != nullptr ? ecs::availableQuantity(actor.inventory, *lifting) : 0U;
		const bool		   took = finishAction(actor.entity, actor.position, action, actor.needs, task, actor.inventory, actor.memory);
		const uint32_t	   heldAfter = lifting != nullptr ? ecs::availableQuantity(actor.inventory, *lifting) : 0U;
		const ChainLift	   lifted{.item = lifting != nullptr ? assets::AssetRegistry::Get().getDefNameId(*lifting) : 0U,
								  .quantity = heldAfter > heldBefore ? heldAfter - heldBefore : 0U};
		action.clear();
		if (!took) {
			endChain(actor, "its action's effect did not take");
			return;
		}
		doStepDone(actor, *doStep, lifted);
		nextStep(task);
		runSteps(actor, nullptr);
	}

	ChainTarget ActionSystem::goTarget(const ChainActor& actor, const assets::GoStep& step, std::optional<uint64_t> keep) {
		if (step.to) {
			return optionTarget(actor.task, *step.to);
		}
		const assets::DecisionRungs& rungs = assets::PriorityConfig::Get().decisionRungs();
		const assets::DoStep*		 acting = doStepAfterGo(actor.task);
		queries::QueryRequest		 request;
		request.query = step.query->handle;
		request.creature = actor.entity;
		request.position = actor.position.value;
		if (const auto* radius = world->getComponent<AgentRadius>(actor.entity)) {
			request.agentRadius = radius->radiusMeters;
		}
		request.memory = &actor.memory;
		request.reach = world->getComponent<SourceReachMemo>(actor.entity);
		if (const auto* traits = world->getComponent<Traits>(actor.entity)) {
			request.traits = traits->ids;
		}
		const uint8_t tier = actor.task.rankedTier(rungs.idle);
		request.claims = {
			.kinds = acting != nullptr ? acting->claims : uint8_t{0},
			.everyHolderTakes = tier == rungs.idle,
			.tier = tier,
			.idleTier = rungs.idle,
			.wanted = std::max(actor.task.chain.goal.quantity, 1U),
			.self = ClaimExclusion::of(actor.task),
		};
		request.keepKey = keep;
		const queries::QueryResult result = chainWiring.evaluator->evaluate(request, chainQueryWorld());
		const queries::QueryCandidate* pick = result.target();
		return pick != nullptr ? chainTargetOf(*pick) : ChainTarget{};
	}

	queries::QueryWorld ActionSystem::chainQueryWorld() {
		queries::QueryWorld query;
		query.navigation = world->tryGetSystem<NavigationSystem>();
		query.chunks = chainWiring.chunks;
		if (const auto* ai = world->tryGetSystem<AIDecisionSystem>()) {
			query.claims = &ai->claimIndex();
		}
		query.world = world;
		query.piles = &m_loosePiles;
		query.regrowth = chainWiring.regrowth ? &chainWiring.regrowth : nullptr;
		query.now = clockSeconds();
		query.recentSightingGameMinutes = chainWiring.recentSightingGameMinutes;
		query.generator = &m_rng;
		return query;
	}

	const assets::DoStep* ActionSystem::doStepAfterGo(const Task& task) const {
		const ChainFrame&		  frame = task.chain.frame();
		const assets::ChainEntry& chain = chainWiring.library->chains[frame.chain.index];
		if (static_cast<std::size_t>(frame.step) + 1 >= chain.steps.size()) {
			return nullptr;
		}
		const auto* next = std::get_if<assets::DoStep>(&chain.steps[frame.step + 1].step);
		return next != nullptr && !next->on ? next : nullptr;
	}

	const ChainTarget& ActionSystem::doTarget(const Task& task, const assets::DoStep& step) {
		return step.on ? optionTarget(task, *step.on) : task.chain.stepTarget;
	}

	bool ActionSystem::takeStepClaims(Task& task, const assets::DoStep& step, const ChainTarget& target) const {
		const auto holds = [&task](const Claim& wanted) {
			return std::any_of(task.claims.begin(), task.claims.end(), [&](const Claim& claim) {
				return claim.kind() == wanted.kind() && claim.targetKind() == wanted.targetKind() && claim.target() == wanted.target() &&
					   claim.itemDefNameId() == wanted.itemDefNameId() && claim.slotIndex() == wanted.slotIndex();
			});
		};
		const auto take = [&](const Claim& claim) {
			if (!holds(claim)) {
				[[maybe_unused]] const bool pushed = task.claims.push(claim);
			}
		};
		const ChainProgress& progress = task.chain;
		const bool			 use = (step.claims & bit(assets::ClaimKind::Use)) != 0;
		const bool			 sourceQuantity = (step.claims & bit(assets::ClaimKind::SourceQuantity)) != 0;
		if ((use || sourceQuantity) && target.resolved()) {
			// A step naming both holds the one its target fits.
			const bool fromSource = sourceQuantity && (!use || target.claimKind == assets::ClaimKind::SourceQuantity ||
													   target.kind == ChainTarget::Kind::Box);
			const uint32_t quantity = target.claimQuantity > 0 ? target.claimQuantity : std::max(progress.goal.quantity, 1U);
			take(claimOn(target, fromSource, quantity));
		}
		if ((step.claims & bit(assets::ClaimKind::Slot)) != 0) {
			const EntityID station = progress.destination.entity != kInvalidEntity ? progress.destination.entity : target.entity;
			if (station == kInvalidEntity) {
				return false;
			}
			const uint16_t spot = progress.source.claimKind == assets::ClaimKind::Slot ? static_cast<uint16_t>(progress.source.key) : kAnySlot;
			take(Claim::slot(static_cast<uint64_t>(station), spot));
		}
		if ((step.claims & bit(assets::ClaimKind::Inbound)) != 0) {
			const EntityID destination = progress.destination.entity;
			if (progress.goal.rootGoalId == 0 || progress.goal.item == 0 || destination == kInvalidEntity) {
				return false;
			}
			take(Claim::inbound(static_cast<uint64_t>(destination), progress.goal.item, progress.goal.quantity));
			take(Claim::inFlight(progress.goal.rootGoalId, progress.goal.item, progress.goal.quantity));
		}
		return true;
	}

	void ActionSystem::doStepDone(const ChainActor& actor, const assets::DoStep& step, ChainLift lifted) {
		Task&			   task = actor.task;
		ChainProgress&	   progress = task.chain;
		const ChainTarget& target = doTarget(task, step);
		if (lifted.quantity > 0) {
			if (progress.takenItem != lifted.item) {
				progress.takenQuantity = 0;
			}
			progress.takenItem = lifted.item;
			progress.takenQuantity += lifted.quantity;
		}
		// The step's own claims are done with: its use or source share, its slot, and the goods it delivered.
		const uint64_t targetKey = target.resolved() ? claimOn(target, false, 0).target() : 0U;
		task.claims.eraseIf([&](const Claim& claim) {
			switch (claim.kind()) {
				case ClaimKind::Use:
					return (step.claims & bit(assets::ClaimKind::Use)) != 0 && claim.target() == targetKey;
				case ClaimKind::SourceQuantity:
					return (step.claims & bit(assets::ClaimKind::SourceQuantity)) != 0 && claim.target() == targetKey;
				case ClaimKind::Slot:
					return (step.claims & bit(assets::ClaimKind::Slot)) != 0;
				case ClaimKind::Inbound:
				case ClaimKind::InFlight:
					return (step.claims & bit(assets::ClaimKind::Inbound)) != 0;
			}
			return false;
		});
		// A take: the claims that follow the goods are rewritten to what the chain carries.
		if (lifted.quantity > 0) {
			for (Claim& claim : task.claims) {
				if ((claim.kind() == ClaimKind::Inbound || claim.kind() == ClaimKind::InFlight) && claim.itemDefNameId() == progress.takenItem) {
					claim.setQuantity(std::min(claim.quantity(), progress.takenQuantity));
				}
			}
			task.claims.eraseIf([](const Claim& claim) { return claim.quantity() == 0; });
		}
	}

	bool ActionSystem::stepEndEvent(const ChainActor& actor, uint8_t ends) const {
		const ChainProgress&	 progress = actor.task.chain;
		const assets::ChainStep& step = currentStep(actor.task);
		const auto*				 doStep = std::get_if<assets::DoStep>(&step.step);
		const ChainTarget&		 target = doStep != nullptr ? doTarget(actor.task, *doStep) : progress.stepTarget;
		if (names(ends, assets::EndEvent::HeldGone) && progress.heldAtStart > 0 && heldForStep(actor, progress.heldItem) == 0) {
			return true;
		}
		const auto* live = target.entity != kInvalidEntity && world->isAlive(target.entity) ? world->getComponent<Position>(target.entity) : nullptr;
		if (names(ends, assets::EndEvent::TargetGone)) {
			switch (target.kind) {
				case ChainTarget::Kind::Thing:
					if (actor.memory.getWorldEntity(target.key) == nullptr) {
						return true;
					}
					break;
				case ChainTarget::Kind::Box:
				case ChainTarget::Kind::Entity:
					if (live == nullptr || glm::distance(live->value, target.position) > kTargetMovedMeters) {
						return true;
					}
					break;
				case ChainTarget::Kind::Mover:
					if (live == nullptr) {
						return true;
					}
					break;
				case ChainTarget::Kind::Place:
				case ChainTarget::Kind::None:
					break;
			}
		}
		return names(ends, assets::EndEvent::TargetLeft) && target.kind == ChainTarget::Kind::Mover && live != nullptr &&
			   glm::distance(live->value, target.position) > kTargetMovedMeters;
	}

	uint32_t ActionSystem::heldForStep(const ChainActor& actor, uint32_t item) const {
		if (item == 0) {
			return heldWithCapability(actor.inventory, assets::CapabilityType::Edible);
		}
		return ecs::availableQuantity(actor.inventory, assets::AssetRegistry::Get().getDefName(item));
	}

	bool ActionSystem::haveMet(const ChainActor& actor, const assets::HaveStep& step) const {
		const ChainProgress& progress = actor.task.chain;
		const EntityID		 packagedEntity = progress.target.entity;
		if (packagedEntity != kInvalidEntity) {
			if (const auto* packaged = world->getComponent<Packaged>(packagedEntity)) {
				const auto* at = world->getComponent<Position>(packagedEntity);
				return actor.inventory.carryingPackagedEntity == static_cast<uint64_t>(packagedEntity) ||
					   (packaged->targetPosition.has_value() && at != nullptr &&
						glm::distance(at->value, *packaged->targetPosition) <= kTargetMovedMeters);
			}
		}
		const auto&	   registry = assets::AssetRegistry::Get();
		uint32_t	   item = 0;
		uint32_t	   held = 0;
		if (step.item && step.item->kind != assets::ItemArgument::Kind::Capability) {
			item = step.item->kind == assets::ItemArgument::Kind::Goal ? progress.goal.item : step.item->defNameId;
			held = item != 0 ? ecs::availableQuantity(actor.inventory, registry.getDefName(item)) : 0U;
		} else {
			held = heldWithCapability(actor.inventory, step.item ? step.item->capability : *step.capability);
		}
		const uint32_t handed = item != 0 && progress.goal.rootGoalId != 0 ? cargoOnClaims(actor.task.claims, progress.goal.rootGoalId, item) : 0U;
		return held >= std::max(progress.goal.quantity, 1U) + handed;
	}

	void ActionSystem::giveThought(const ChainActor& actor, const assets::ThinkStep& step, ThoughtEventTypeId thought) {
		auto* thoughts = world->tryGetSystem<ThoughtSystem>();
		if (thoughts == nullptr) {
			LOG_WARNING(Engine, "[Chain] %s: a <think> step with no ThoughtSystem in the world gives no thought", actor.task.reason.c_str());
			return;
		}
		const ChainTarget& target = actor.task.chain.stepTarget.resolved() ? actor.task.chain.stepTarget : actor.task.chain.target;
		switch (step.to) {
			case assets::ThoughtRecipient::Self: {
				const ThoughtSubject subject = target.entity != kInvalidEntity ? ThoughtSubject::aboutEntity(target.entity)
											   : target.defNameId != 0		 ? ThoughtSubject::aboutKind(target.defNameId)
																			 : ThoughtSubject::aboutNothing();
				thoughts->create({.colonist = actor.entity, .eventType = thought, .subject = subject});
				return;
			}
			case assets::ThoughtRecipient::Target:
				if (target.entity == kInvalidEntity || world->getComponent<Thoughts>(target.entity) == nullptr) {
					LOG_WARNING(Engine, "[Chain] %s: a <think to=\"target\"> step whose target has no thoughts gives none",
								actor.task.reason.c_str());
					return;
				}
				thoughts->create({.colonist = target.entity, .eventType = thought, .subject = ThoughtSubject::aboutEntity(actor.entity)});
				return;
			case assets::ThoughtRecipient::Witnesses: {
				const auto* vision = world->tryGetSystem<VisionSystem>();
				if (vision == nullptr) {
					LOG_WARNING(Engine, "[Chain] %s: a <think to=\"witnesses\"> step with no VisionSystem has no witnesses",
								actor.task.reason.c_str());
					return;
				}
				for (auto [witness, standing, held] : world->view<Position, Thoughts>()) {
					(void)held;
					const auto sight = witness != actor.entity ? vision->sightShape(witness) : std::nullopt;
					if (sight.has_value() && sight->holds(standing.value, actor.position.value)) {
						thoughts->create({.colonist = witness, .eventType = thought, .subject = ThoughtSubject::aboutEntity(actor.entity)});
					}
				}
				return;
			}
		}
	}

	void ActionSystem::dropTaken(const ChainActor& actor) {
		ChainProgress& progress = actor.task.chain;
		if (progress.takenItem == 0 || progress.takenQuantity == 0) {
			return;
		}
		auto&			   registry = assets::AssetRegistry::Get();
		const std::string& name = registry.getDefName(progress.takenItem);
		const uint32_t	   quantity = std::min(progress.takenQuantity, ecs::availableQuantity(actor.inventory, name));
		uint32_t		   removed = 0;
		if (ecs::itemIsTwoHand(registry, name)) {
			removed = ecs::removeFromHands(actor.inventory, name, quantity);
		} else {
			removed = actor.inventory.removeItem(name, quantity);
			if (removed < quantity) {
				removed += ecs::removeFromHands(actor.inventory, name, quantity - removed);
			}
		}
		if (removed > 0) {
			ecs::storeOrDrop(*world, name, removed, actor.position.value, m_onDropResource);
		}
		const uint32_t item = progress.takenItem;
		actor.task.claims.eraseIf([item](const Claim& claim) {
			return (claim.kind() == ClaimKind::Inbound || claim.kind() == ClaimKind::InFlight) && claim.itemDefNameId() == item;
		});
		progress.takenItem = 0;
		progress.takenQuantity = 0;
	}

	ActionSystem::DoStart ActionSystem::startChainAction(const ChainActor& actor, const assets::DoStep& step, ChainLift& alreadyHeld) {
		auto&				 registry = assets::AssetRegistry::Get();
		Task&				 task = actor.task;
		const ChainProgress& progress = task.chain;
		const ChainTarget&	 target = doTarget(task, step);
		const char*			 chainId = task.reason.c_str();

		// The action, or the member of the group whose `on` matches the target.
		assets::EntryHandle<assets::ActionEntry> handle = step.action.handle;
		const assets::ActionEntry&				 named = chainWiring.library->actions[handle.index];
		if (named.group) {
			handle = {};
			const auto* packaged = target.entity != kInvalidEntity ? world->getComponent<Packaged>(target.entity) : nullptr;
			const auto* def = registry.getDefinitionById(target.defNameId);
			const bool	pile = target.kind == ChainTarget::Kind::Thing && def != nullptr && def->capabilities.carryable.has_value() &&
							  !def->capabilities.harvestable.has_value();
			for (const auto& member : named.members) {
				const std::optional<assets::ActionTarget>& on = chainWiring.library->actions[member.handle.index].on;
				const bool matches = on.has_value() && ((on->kind == assets::ActionTarget::Kind::Packaged && packaged != nullptr) ||
														(on->kind == assets::ActionTarget::Kind::LooseItem && pile) ||
														(on->kind == assets::ActionTarget::Kind::Capability && packaged == nullptr &&
														 (target.kind == ChainTarget::Kind::Box ? on->capability == assets::CapabilityType::Storage
																								: registry.hasCapability(target.defNameId, on->capability))));
				if (matches) {
					handle = member.handle;
					break;
				}
			}
			if (!handle.valid()) {
				// A remembered thing no member acts on is no longer what it was when the chain set out for it.
				if (const KnownWorldEntity* known = target.kind == ChainTarget::Kind::Thing ? actor.memory.getWorldEntity(target.key) : nullptr) {
					actor.memory.forgetWorldEntity(known->position, known->defNameId);
				}
				LOG_DEBUG(Engine, "[Chain] %s: no member of action group %s acts on its target", chainId, named.id.c_str());
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
				LOG_DEBUG(Engine, "[Chain] %s: %s consumes, and nothing it could consume is held", chainId, values.id.c_str());
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
				LOG_WARNING(Engine, "[Chain] %s: %s restores its target's need, and its target is no creature", chainId, values.id.c_str());
				return DoStart::Refused;
			}
			started.context.targetCreature = target.entity;
		}

		// What a take asks for, and a put delivers: the step's amount.
		const auto carriedOf = [&](uint32_t item) {
			const auto*	   def = registry.getDefinitionById(item);
			const uint32_t held = ecs::availableQuantity(actor.inventory, registry.getDefName(item));
			return progress.goal.rootGoalId != 0 ? cargoToward(task, progress.goal.rootGoalId, item, held, def != nullptr && def->isEdible()) : held;
		};
		const auto amountOf = [&](uint32_t item) -> uint32_t {
			if (step.amount == assets::StepAmount::Carried) {
				return carriedOf(item);
			}
			if (target.claimKind == assets::ClaimKind::SourceQuantity && target.claimQuantity > 0) {
				return target.claimQuantity;
			}
			return std::max(progress.goal.quantity, 1U);
		};

		const auto* packaged = target.entity != kInvalidEntity ? world->getComponent<Packaged>(target.entity) : nullptr;
		if (takes && packaged != nullptr) {
			PlacePackagedEffect lift;
			lift.packagedEntityId = static_cast<uint64_t>(target.entity);
			lift.targetPosition = packaged->targetPosition.value_or(target.position);
			started.effect = lift;
		} else if (takes && target.kind == ChainTarget::Kind::Box) {
			if (world->getComponent<Inventory>(target.entity) == nullptr || world->hasComponent<Packaged>(target.entity)) {
				LOG_DEBUG(Engine, "[Chain] %s: box %llu is gone or being moved", chainId, static_cast<unsigned long long>(target.entity));
				return DoStart::Refused;
			}
			const std::string& item = registry.getDefName(target.item);
			if (progress.goal.rootGoalId != 0 && carriedOf(target.item) > cargoOnClaims(task.claims, progress.goal.rootGoalId, target.item)) {
				alreadyHeld = {.item = target.item, .quantity = carriedOf(target.item)};
				return DoStart::AlreadyHeld;
			}
			const uint32_t quantity = amountOf(target.item);
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
		} else if (takes) {
			const KnownWorldEntity* known = target.kind == ChainTarget::Kind::Thing ? actor.memory.getWorldEntity(target.key) : nullptr;
			if (known == nullptr) {
				LOG_DEBUG(Engine, "[Chain] %s: its take's target is no thing it remembers", chainId);
				return DoStart::Refused;
			}
			const auto* def = registry.getDefinitionById(known->defNameId);
			if (known->defNameId != target.defNameId || def == nullptr ||
				(!def->capabilities.harvestable.has_value() && !def->capabilities.carryable.has_value())) {
				actor.memory.forgetWorldEntity(known->position, known->defNameId);
				LOG_DEBUG(Engine, "[Chain] %s: its take's target is no longer what it was - forgot it", chainId);
				return DoStart::Refused;
			}
			CollectionEffect collection;
			collection.sourcePosition = known->position;
			collection.sourceDefName = def->defName;
			if (def->capabilities.harvestable.has_value()) {
				const auto& harvestable = *def->capabilities.harvestable;
				if (!ecs::inventoryHoldsToolType(actor.inventory, registry, harvestable.requiredToolType)) {
					LOG_DEBUG(Engine, "[Chain] %s: cannot cut %s without a %s", chainId, def->defName.c_str(), harvestable.requiredToolType.c_str());
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
				// What he can't carry drops where he stands to work the source, not at its centre.
				collection.dropAt = actor.position.value;
				if (values.duration == assets::ActionDuration::FromDurability) {
					started.duration = harvestable.durability;
				}
			} else {
				if (progress.goal.rootGoalId != 0 && carriedOf(known->defNameId) > cargoOnClaims(task.claims, progress.goal.rootGoalId, known->defNameId)) {
					alreadyHeld = {.item = known->defNameId, .quantity = carriedOf(known->defNameId)};
					return DoStart::AlreadyHeld;
				}
				const uint32_t quantity = amountOf(known->defNameId);
				if (step.amount == assets::StepAmount::Meal) {
					makeRoomToEat(actor.inventory, task, def->defName, quantity, actor.position.value);
				}
				collection.itemDefName = def->defName;
				collection.quantity = quantity;
				collection.destroySource = true;
			}
			started.effect = collection;
		} else if (puts) {
			const uint32_t item = progress.takenItem != 0 ? progress.takenItem : progress.goal.item != 0 ? progress.goal.item : target.item;
			if (item == 0 || target.entity == kInvalidEntity || world->hasComponent<Packaged>(target.entity)) {
				LOG_DEBUG(Engine, "[Chain] %s: its put has nothing to put, or nowhere to put it", chainId);
				return DoStart::Refused;
			}
			DepositEffect deposit;
			deposit.itemDefName = registry.getDefName(item);
			deposit.quantity = step.amount == assets::StepAmount::ForGoal ? std::max(progress.goal.quantity, 1U) : carriedOf(item);
			deposit.storageEntityId = static_cast<uint64_t>(target.entity);
			deposit.deliverToCraftStation = world->hasComponent<WorkQueue>(target.entity);
			started.effect = deposit;
		} else if (works) {
			const auto* blueprint = target.entity != kInvalidEntity ? world->getComponent<StructureBlueprint>(target.entity) : nullptr;
			const auto& work = std::get<WorkRow>(values.effects[values.boundRow]);
			if (blueprint == nullptr || !workMayStart(work, *blueprint)) {
				LOG_DEBUG(Engine, "[Chain] %s: its work's target is no blueprint it may work", chainId);
				return DoStart::Refused;
			}
			ProgressEffect progressEffect;
			progressEffect.targetEntityId = static_cast<uint64_t>(target.entity);
			if (const auto* skills = world->getComponent<Skills>(actor.entity)) {
				progressEffect.skillLevel = skills->getLevel(work.skill);
			}
			started.effect = progressEffect;
		} else if (crafts) {
			const auto*		   queue = target.entity != kInvalidEntity ? world->getComponent<WorkQueue>(target.entity) : nullptr;
			const CraftingJob* job = queue != nullptr ? queue->getNextJob() : nullptr;
			std::optional<CraftReady> ready = job != nullptr ? craftReady(job->recipeDefName, static_cast<uint64_t>(target.entity)) : std::nullopt;
			if (!ready) {
				LOG_DEBUG(Engine, "[Chain] %s: its craft's station has no job it can make now", chainId);
				return DoStart::Refused;
			}
			started.duration = ready->workAmount / assets::kJobWorkTicksPerSecond;
			started.effect = std::move(ready->effect);
		} else if (installs) {
			const EntityID placing = packaged != nullptr				 ? target.entity
									 : actor.inventory.carryingPackagedEntity ? static_cast<EntityID>(*actor.inventory.carryingPackagedEntity)
																			  : kInvalidEntity;
			const auto* placingPackage = placing != kInvalidEntity ? world->getComponent<Packaged>(placing) : nullptr;
			if (placingPackage == nullptr || !placingPackage->targetPosition.has_value()) {
				LOG_DEBUG(Engine, "[Chain] %s: its install has no packaged thing with a placement", chainId);
				return DoStart::Refused;
			}
			PlacePackagedEffect place;
			place.packagedEntityId = static_cast<uint64_t>(placing);
			place.targetPosition = *placingPackage->targetPosition;
			started.targetPosition = place.targetPosition;
			started.effect = place;
		}
		if (values.duration == assets::ActionDuration::FromDurability && !started.hasCollectionEffect()) {
			LOG_WARNING(Engine, "[Chain] %s: %s takes its time from a durability, and its target has none", chainId, values.id.c_str());
			return DoStart::Refused;
		}
		actor.action = std::move(started);
		return DoStart::Started;
	}

} // namespace ecs
