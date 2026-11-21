// The chain runner: a Task of type Chain runs its chain's steps in order on the Task, Action, and routing machinery.
// Every `do` step starts its action through one path with no factory (startChainAction, with its builders in
// ChainRunnerActions.cpp). See /docs/technical/queries-and-chains.md (Chains, The runner, Requirements on the chain
// runner).

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
#include "../../components/SourceReachMemo.h"
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
#include <variant>

namespace ecs {

	namespace {

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
			auto	 space = static_cast<TargetKind>(target.claimSpace);
			uint64_t key = target.claimKey;
			if (key == 0) {
				space = target.kind == ChainTarget::Kind::Thing ? TargetKind::WorldKey : TargetKind::Entity;
				key = target.kind == ChainTarget::Kind::Thing ? target.key : static_cast<uint64_t>(target.entity);
			}
			return sourceQuantity ? Claim::sourceQuantity(space, key, target.item, quantity) : Claim::use(space, key);
		}

		[[nodiscard]] bool performsWork(const ActionValues& values) {
			return values.bound == ActionBound::Work;
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
		if (chainWiring.library == nullptr || chainWiring.chains == nullptr || chainWiring.evaluator == nullptr || !start.chain.valid() ||
			start.chain.index >= chainWiring.library->chains.size()) {
			LOG_ERROR(
				Engine,
				"[Chain] Entity %llu: the chain runner is not wired, or the chain named is none of its library's; nothing started",
				static_cast<unsigned long long>(entity)
			);
			return false;
		}
		const ChainActor actor{
			.entity = entity,
			.position = *position,
			.task = *task,
			.action = *action,
			.needs = *needs,
			.memory = *memory,
			.inventory = *inventory
		};
		const assets::ChainEntry& chain = chainWiring.library->chains[start.chain.index];

		// The action in progress goes with the task it served, and a route still out was asked for that task.
		action->clear();
		if (task->routeTicket != 0) {
			if (auto* nav = world->tryGetSystem<NavigationSystem>()) {
				nav->cancelRoute(task->routeTicket);
			}
			task->routeTicket = 0;
		}
		// The task it replaces leaves its claims behind, from the index too.
		task->claims.clear();
		fileClaims(actor);
		task->clear();
		task->timeSinceEvaluation = 0.0F;
		task->type = TaskType::Chain;
		task->decideAtInterval = false;
		task->priorityTier = start.tier;
		task->reason = kRunningChainLabel;
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
				if (target.resolved() && !takeStepClaims(actor, *doStep, target)) {
					endChain(actor, "a claim its steps name cannot be taken");
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
		const ChainProgress& progress = actor.task.chain;
		LOG_DEBUG(
			Engine,
			"[Chain] Entity %llu: %s ends (%s)",
			static_cast<unsigned long long>(actor.entity),
			progress.active() && chainWiring.library != nullptr && progress.frames[0].chain.index < chainWiring.library->chains.size()
				? chainWiring.library->chains[progress.frames[0].chain.index].id.c_str()
				: "a chain",
			why
		);
		actor.action.clear();
		// The claims go with the task, from the index too, so a creature reading it later this update sees them free.
		actor.task.claims.clear();
		fileClaims(actor);
		actor.task.clear();
		actor.task.timeSinceEvaluation = 0.0F;
	}

	void ActionSystem::fileClaims(const ChainActor& actor) {
		if (chainWiring.appendClaims && actor.task.instanceId != 0) {
			chainWiring.appendClaims(actor.entity, actor.task);
		}
	}

	void ActionSystem::refuseChain(const ChainActor& actor, const char* why) {
		endChain(actor, why);
		actor.task.decideAtInterval = true;
	}

	void ActionSystem::headToTarget(EntityID entity, Task& task, const ChainTarget& target) {
		headTo(entity, task, target.goal);
		if (target.goal != target.position) {
			task.obstructedTarget = target.position;
		}
	}

	bool ActionSystem::claimForGo(const ChainActor& actor, const ChainTarget& target) {
		const assets::DoStep* next = doStepAfterGo(actor.task);
		return next == nullptr || next->claims == 0 || takeStepClaims(actor, *next, target);
	}

	bool ActionSystem::runSteps(const ChainActor& actor, const ChainTarget* given) {
		Task&		   task = actor.task;
		ChainProgress& progress = task.chain;
		for (;;) {
			ChainFrame& frame = progress.frame();
			if (frame.chain.index >= chainWiring.library->chains.size()) {
				LOG_ERROR(
					Engine, "[Chain] Entity %llu runs a chain its library does not hold", static_cast<unsigned long long>(actor.entity)
				);
				endChain(actor, "its chain is none of the library's");
				return false;
			}
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
			const ChainStepValues&	 stepValues = chainWiring.chains->of(frame.chain, frame.step);
			progress.stepStartedAt = clockSeconds();

			if (const auto* go = std::get_if<assets::GoStep>(&step.step)) {
				const ChainTarget target = given != nullptr ? *given : goTarget(actor, *go);
				given = nullptr;
				if (!target.resolved()) {
					endChain(actor, "its go step found no target");
					return false;
				}
				progress.stepTarget = target;
				progress.targetAtStepStart = target.position;
				if (!claimForGo(actor, target)) {
					endChain(actor, "a claim its steps name cannot be taken");
					return false;
				}
				if (glm::distance(actor.position.value, target.goal) <= kActionReachMeters) {
					nextStep(task);
					continue;
				}
				headToTarget(actor.entity, task, target);
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
				const ChainTarget& target = doTarget(task, *doStep);
				const auto*		   live = target.entity != kInvalidEntity && world->isAlive(target.entity)
											  ? world->getComponent<Position>(target.entity)
											  : nullptr;
				progress.targetAtStepStart = live != nullptr ? live->value : target.position;
				progress.heldItem = 0;
				progress.heldAtStart = 0;
				if (names(step.ends, assets::EndEvent::HeldGone)) {
					// The goods it holds for the step: the food it would eat for an action that consumes, else the chain's.
					const assets::ActionEntry&					   named = chainWiring.library->actions[doStep->action.handle.index];
					const assets::EntryHandle<assets::ActionEntry> performed =
						named.group ? memberFor(named, target) : doStep->action.handle;
					const bool consumes =
						performed.valid() && std::ranges::any_of(actionEntries.of(performed).effects, [](const EffectRow& row) {
							return std::holds_alternative<ConsumeRow>(row);
						});
					const std::string* food = consumes ? heldEdibleUnit(actor.inventory) : nullptr;
					progress.heldItem = food != nullptr			  ? assets::AssetRegistry::Get().getDefNameId(*food)
										: progress.takenItem != 0 ? progress.takenItem
																  : progress.goal.item;
					progress.heldAtStart = heldUnits(actor, progress.heldItem);
				}
				return true;
			}
			const auto* have = std::get_if<assets::HaveStep>(&step.step);
			const auto* run = std::get_if<assets::RunStep>(&step.step);
			if (have != nullptr && haveMet(actor, *have)) {
				nextStep(task);
				continue;
			}
			if (have != nullptr || run != nullptr) {
				if (progress.depth >= ChainProgress::kMaxDepth) {
					LOG_ERROR(Engine, "[Chain] %s nests deeper than a chain may", chain.id.c_str());
					endChain(actor, "it nests deeper than a chain may");
					return false;
				}
				progress.frames[progress.depth++] = {.chain = have != nullptr ? have->fetch.handle : run->chain.handle, .step = 0};
				continue;
			}
			if (const auto* think = std::get_if<assets::ThinkStep>(&step.step)) {
				giveThought(actor, *think, stepValues.thought);
				nextStep(task);
				continue;
			}
			if (const auto* need = std::get_if<assets::NeedStep>(&step.step)) {
				actor.needs.get(stepValues.need).move(need->change);
				nextStep(task);
				continue;
			}
			if (std::holds_alternative<assets::DropStep>(step.step)) {
				dropTaken(actor);
				nextStep(task);
				continue;
			}
			LOG_ERROR(Engine, "[Chain] %s: <leave_colony/> needs colony membership, which the sim does not have", chain.id.c_str());
			endChain(actor, "it asked to leave the colony, which the sim cannot do");
			return false;
		}
	}

	void ActionSystem::runChain(float deltaTime, const ChainActor& actor) {
		Task&	task = actor.task;
		Action& action = actor.action;
		if (chainWiring.library == nullptr || chainWiring.chains == nullptr || chainWiring.evaluator == nullptr) {
			if (!reportedUnwiredChains) {
				LOG_ERROR(
					Engine,
					"[Chain] Entity %llu runs a chain, but the chain runner is not wired; its task ends",
					static_cast<unsigned long long>(actor.entity)
				);
				reportedUnwiredChains = true;
			}
			endChain(actor, "the chain runner is not wired");
			return;
		}
		if (!task.chain.active() || task.chain.frame().chain.index >= chainWiring.library->chains.size()) {
			LOG_ERROR(
				Engine,
				"[Chain] Entity %llu has a Chain task with no chain of the library to run",
				static_cast<unsigned long long>(actor.entity)
			);
			endChain(actor, "it has no chain of the library to run");
			return;
		}

		const assets::ChainStep& step = currentStep(task);
		const auto*				 go = std::get_if<assets::GoStep>(&step.step);
		if (step.ends != 0 && stepEndEvent(actor, step.ends)) {
			// The step's target gone or left: a `go` cannot reach it, so the chain ends; a `do` ends its step and goes on.
			if (go != nullptr) {
				endChain(actor, "the target it walked to is gone or has left");
				return;
			}
			action.clear();
			releaseStepClaims(task, std::get<assets::DoStep>(step.step));
			fileClaims(actor);
			nextStep(task);
			runSteps(actor, nullptr);
			return;
		}

		if (go != nullptr) {
			if (task.state == TaskState::Moving) {
				ChainTarget& target = task.chain.stepTarget;
				// A remembered target forgotten on the way: a query is asked again; a target the option named is gone.
				if (target.kind == ChainTarget::Kind::Thing && actor.memory.getWorldEntity(target.key) == nullptr) {
					const ChainTarget again = go->query ? goTarget(actor, *go) : ChainTarget{};
					if (!again.resolved()) {
						endChain(actor, "the target it walked to is gone");
						return;
					}
					const uint64_t oldKey = claimOn(target, false, 0).target();
					task.claims.eraseIf([oldKey](const Claim& claim) {
						return (claim.kind() == ClaimKind::Use || claim.kind() == ClaimKind::SourceQuantity) && claim.target() == oldKey;
					});
					fileClaims(actor);
					target = again;
					task.chain.targetAtStepStart = again.position;
					if (!claimForGo(actor, again)) {
						endChain(actor, "a claim its steps name cannot be taken");
						return;
					}
					headToTarget(actor.entity, task, again);
				}
				// A creature target is followed: walking on toward where it now stands.
				if (target.kind == ChainTarget::Kind::Mover && world->isAlive(target.entity)) {
					if (const auto* at = world->getComponent<Position>(target.entity);
						at != nullptr && glm::distance(at->value, target.position) > kSameSpotMeters) {
						target.goal += at->value - target.position;
						target.position = at->value;
						headToTarget(actor.entity, task, target);
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
					refuseChain(actor, "its do step started nothing");
					return;
				case DoStart::AlreadyHeld:
					if (doStepDone(actor, *doStep, alreadyHeld, true)) {
						nextStep(task);
						runSteps(actor, nullptr);
					}
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
		const Actor rows{
			.entity = actor.entity,
			.position = actor.position.value,
			.needs = actor.needs,
			.inventory = actor.inventory,
			.memory = actor.memory
		};
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
		const ChainLift	   lifted{
			   .item = lifting != nullptr ? assets::AssetRegistry::Get().getDefNameId(*lifting) : 0U,
			   .quantity = heldAfter > heldBefore ? heldAfter - heldBefore : 0U
		   };
		const bool takes = lifting != nullptr;
		action.clear();
		// A take that took nothing is a take that lifted nothing, which ends the chain unless it carries cargo on.
		if (!took && !takes) {
			endChain(actor, "its action's effect did not take");
			return;
		}
		if (doStepDone(actor, *doStep, lifted, takes)) {
			nextStep(task);
			runSteps(actor, nullptr);
		}
	}

	ChainTarget ActionSystem::goTarget(const ChainActor& actor, const assets::GoStep& step) {
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
		const queries::QueryResult	   result = chainWiring.evaluator->evaluate(request, chainQueryWorld());
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

	bool ActionSystem::takeStepClaims(const ChainActor& actor, const assets::DoStep& step, const ChainTarget& target) {
		Task&	   task = actor.task;
		bool	   changed = false;
		bool	   fits = true;
		const auto take = [&](const Claim& wanted) {
			for (Claim& claim : task.claims) {
				if (claim.kind() == wanted.kind() && claim.targetKind() == wanted.targetKind() && claim.target() == wanted.target() &&
					claim.itemDefNameId() == wanted.itemDefNameId() && claim.slotIndex() == wanted.slotIndex()) {
					if (wanted.quantity() > claim.quantity()) {
						claim.setQuantity(wanted.quantity());
						changed = true;
					}
					return;
				}
			}
			if (task.claims.size() >= ClaimList::kCapacity) {
				fits = false;
				return;
			}
			[[maybe_unused]] const bool pushed = task.claims.push(wanted);
			changed = true;
		};
		const ChainProgress& progress = task.chain;
		const ChainGoal&	 goal = progress.goal;
		const bool			 use = (step.claims & bit(assets::ClaimKind::Use)) != 0;
		const bool			 sourceQuantity = (step.claims & bit(assets::ClaimKind::SourceQuantity)) != 0;
		if ((use || sourceQuantity) && target.resolved()) {
			// A step naming both holds the one its target fits.
			const bool fromSource =
				sourceQuantity && (!use || target.claimKind == assets::ClaimKind::SourceQuantity || target.kind == ChainTarget::Kind::Box);
			const uint32_t quantity = target.claimQuantity > 0 ? target.claimQuantity : std::max(goal.quantity, 1U);
			take(claimOn(target, fromSource, quantity));
		}
		// A station's one slot for a craft; a foundation's numbered spot for a build, and no slot for a build with none.
		if ((step.claims & bit(assets::ClaimKind::Slot)) != 0) {
			const bool				   spot = progress.source.claimKind == assets::ClaimKind::Slot;
			const EntityID			   site = progress.destination.entity != kInvalidEntity ? progress.destination.entity : target.entity;
			const assets::ActionEntry& named = chainWiring.library->actions[step.action.handle.index];
			const assets::EntryHandle<assets::ActionEntry> performed = named.group ? memberFor(named, target) : step.action.handle;
			if (!performed.valid() || !performsWork(actionEntries.of(performed)) || spot) {
				if (site == kInvalidEntity) {
					LOG_ERROR(Engine, "[Chain] A slot claim has no station or site to be on");
					return false;
				}
				take(Claim::slot(static_cast<uint64_t>(site), spot ? static_cast<uint16_t>(progress.source.key) : kAnySlot));
			}
		}
		if ((step.claims & bit(assets::ClaimKind::Inbound)) != 0) {
			const EntityID destination = progress.destination.entity;
			if (goal.rootGoalId == 0 || goal.item == 0 || destination == kInvalidEntity) {
				LOG_ERROR(Engine, "[Chain] An inbound claim needs the option's goal and destination");
				return false;
			}
			// Before a take of the goods, from the plan: a cut brings in its cargo and expects its yield, a haul brings its
			// cargo and the planned amount. After one, the claims follow what was lifted and are not sized again; a step
			// that names them with none held (a deposit no earlier claim covered) takes them at what the last take's
			// rewrite left, and none when that capped them to nothing.
			bool holdsGoods = false;
			for (const Claim& claim : task.claims) {
				holdsGoods = holdsGoods || ((claim.kind() == ClaimKind::Inbound || claim.kind() == ClaimKind::InFlight) &&
											claim.itemDefNameId() == goal.item);
			}
			const bool	   cut = goal.expectedYield > 0;
			const uint32_t claimed = progress.goodsClaimed;
			const uint32_t inbound = progress.goodsTaken ? claimed : cut ? goal.cargo : goal.cargo + goal.quantity;
			const uint32_t inFlight = progress.goodsTaken ? claimed : cut ? goal.cargo + goal.expectedYield : goal.cargo + goal.quantity;
			if (!progress.goodsTaken || (!holdsGoods && claimed > 0)) {
				if (inbound > 0) {
					take(Claim::inbound(static_cast<uint64_t>(destination), goal.item, inbound));
				}
				take(Claim::inFlight(goal.rootGoalId, goal.item, inFlight));
			}
		}
		if (!fits) {
			LOG_ERROR(
				Engine, "[Chain] Entity %llu: its chain names more claims than a task holds", static_cast<unsigned long long>(actor.entity)
			);
			return false;
		}
		if (changed) {
			fileClaims(actor);
		}
		return true;
	}

	void ActionSystem::releaseStepClaims(Task& task, const assets::DoStep& step) const {
		const ChainTarget& target = doTarget(task, step);
		const uint64_t	   targetKey = target.resolved() ? claimOn(target, false, 0).target() : 0U;
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
	}

	bool ActionSystem::doStepDone(const ChainActor& actor, const assets::DoStep& step, ChainLift lifted, bool took) {
		Task&			 task = actor.task;
		ChainProgress&	 progress = task.chain;
		const ChainGoal& goal = progress.goal;
		const uint32_t	 item = lifted.item != 0 ? lifted.item : goal.item;
		const bool		 goods = goal.item != 0 && item == goal.item;
		const uint32_t	 handed = goods ? goal.cargo : 0U;
		// What it carried of the item before this take: the cargo it was handed and what the chain already took.
		const uint32_t cargo = handed + (progress.takenItem == item ? progress.takenQuantity : 0U);
		releaseStepClaims(task, step);
		if (!took) {
			fileClaims(actor);
			return true;
		}
		const bool rewritten = progress.goodsTaken;
		// A tool or other good taken on the way leaves the goal's goods' claims to be sized from the plan.
		if (goods) {
			progress.goodsTaken = true;
		}
		if (lifted.quantity == 0) {
			// A take that lifted nothing ends the task at the source, unless it carries cargo on to deliver.
			if (cargo == 0) {
				endChain(actor, "its take lifted nothing");
				return false;
			}
			// The goods' claims stay at what an earlier take's rewrite left, which the root's need may have capped.
			const uint32_t claimed = goods && rewritten ? progress.goodsClaimed : cargo;
			if (goods) {
				progress.goodsClaimed = claimed;
			}
			for (Claim& claim : task.claims) {
				if ((claim.kind() == ClaimKind::Inbound || claim.kind() == ClaimKind::InFlight) && claim.itemDefNameId() == item) {
					claim.setQuantity(claimed);
				}
			}
			task.claims.eraseIf([](const Claim& claim) { return claim.quantity() == 0; });
			fileClaims(actor);
			return true;
		}
		if (progress.takenItem != lifted.item) {
			progress.takenQuantity = 0;
		}
		progress.takenItem = lifted.item;
		progress.takenQuantity += lifted.quantity;
		if (goal.rootGoalId == 0) {
			fileClaims(actor);
			return true;
		}
		// What follows the goods is what was lifted, capped at what the root still needs with no other task bringing it.
		const GoalTaskRegistry& goals = GoalTaskRegistry::Get();
		const GoalTask*			root = goals.getGoal(goal.rootGoalId);
		if (root == nullptr) {
			endChain(actor, "the goal it carries goods toward is gone");
			return false;
		}
		static const ClaimIndex kNoClaims;
		const auto*				ai = world->tryGetSystem<AIDecisionSystem>();
		const uint32_t			need = rootAvailable(
			 goals.remainingNeed(*world, goal.rootGoalId, lifted.item, harvestBasis(*root)),
			 ai != nullptr ? ai->claimIndex() : kNoClaims,
			 goal.rootGoalId,
			 lifted.item,
			 ClaimExclusion::of(task)
		 );
		const uint32_t carried = std::min(handed + progress.takenQuantity, need + handed);
		if (goods) {
			progress.goodsClaimed = carried;
		}
		bool inFlight = false;
		bool inbound = false;
		for (Claim& claim : task.claims) {
			if ((claim.kind() == ClaimKind::Inbound || claim.kind() == ClaimKind::InFlight) && claim.itemDefNameId() == lifted.item) {
				claim.setQuantity(carried);
				inFlight = inFlight || claim.kind() == ClaimKind::InFlight;
				inbound = inbound || claim.kind() == ClaimKind::Inbound;
			}
		}
		// A cut that set out with no cargo claimed no inbound; the goods it lifted are now on their way to the destination.
		if (inFlight && !inbound && carried > 0 && progress.destination.entity != kInvalidEntity) {
			if (task.claims.size() >= ClaimList::kCapacity) {
				LOG_ERROR(
					Engine,
					"[Chain] Entity %llu: no room for the inbound of what its cut lifted",
					static_cast<unsigned long long>(actor.entity)
				);
				endChain(actor, "its claims have no room for the goods it lifted");
				return false;
			}
			[[maybe_unused]] const bool pushed =
				task.claims.push(Claim::inbound(static_cast<uint64_t>(progress.destination.entity), lifted.item, carried));
		}
		task.claims.eraseIf([](const Claim& claim) { return claim.quantity() == 0; });
		fileClaims(actor);
		return true;
	}

	bool ActionSystem::stepEndEvent(const ChainActor& actor, uint8_t ends) const {
		const ChainProgress&	 progress = actor.task.chain;
		const assets::ChainStep& step = currentStep(actor.task);
		const auto*				 doStep = std::get_if<assets::DoStep>(&step.step);
		const ChainTarget&		 target = doStep != nullptr ? doTarget(actor.task, *doStep) : progress.stepTarget;
		if (names(ends, assets::EndEvent::HeldGone) && progress.heldAtStart > 0 && heldUnits(actor, progress.heldItem) == 0) {
			return true;
		}
		const auto* live =
			target.entity != kInvalidEntity && world->isAlive(target.entity) ? world->getComponent<Position>(target.entity) : nullptr;
		if (names(ends, assets::EndEvent::TargetGone)) {
			switch (target.kind) {
				case ChainTarget::Kind::Thing:
					if (actor.memory.getWorldEntity(target.key) == nullptr) {
						return true;
					}
					break;
				case ChainTarget::Kind::Box:
				case ChainTarget::Kind::Entity:
					if (live == nullptr || glm::distance(live->value, progress.targetAtStepStart) > kSameSpotMeters) {
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
		// A creature followed by a `go` has not left; one a `do` acts on has when it is no longer where the step began.
		return names(ends, assets::EndEvent::TargetLeft) && doStep != nullptr && target.kind == ChainTarget::Kind::Mover &&
			   live != nullptr && glm::distance(live->value, progress.targetAtStepStart) > kSameSpotMeters;
	}

	uint32_t ActionSystem::heldUnits(const ChainActor& actor, uint32_t item) {
		return item != 0 ? ecs::availableQuantity(actor.inventory, assets::AssetRegistry::Get().getDefName(item)) : 0U;
	}

	bool ActionSystem::haveMet(const ChainActor& actor, const assets::HaveStep& step) const {
		const ChainProgress& progress = actor.task.chain;
		const EntityID		 packagedEntity = progress.target.entity;
		if (packagedEntity != kInvalidEntity) {
			if (const auto* packaged = world->getComponent<Packaged>(packagedEntity)) {
				const auto* at = world->getComponent<Position>(packagedEntity);
				return actor.inventory.carryingPackagedEntity == static_cast<uint64_t>(packagedEntity) ||
					   (packaged->targetPosition.has_value() && at != nullptr &&
						glm::distance(at->value, *packaged->targetPosition) <= kSameSpotMeters);
			}
		}
		const auto& registry = assets::AssetRegistry::Get();
		uint32_t	held = 0;
		if (step.item && step.item->kind != assets::ItemArgument::Kind::Capability) {
			const uint32_t item = step.item->kind == assets::ItemArgument::Kind::Goal ? progress.goal.item : step.item->defNameId;
			held = item != 0 ? ecs::availableQuantity(actor.inventory, registry.getDefName(item)) : 0U;
		} else {
			const assets::CapabilityType capability = step.item ? step.item->capability : *step.capability;
			std::vector<std::string>	 counted;
			const auto					 count = [&](const std::string& defName) {
				  if (std::ranges::find(counted, defName) != counted.end()) {
					  return;
				  }
				  counted.push_back(defName);
				  const auto* def = registry.getDefinition(defName);
				  // Food is an item's property, not a capability of the thing.
				  const bool has = capability == assets::CapabilityType::Edible
													   ? def != nullptr && def->isEdible()
													   : registry.hasCapability(registry.getDefNameId(defName), capability);
				  held += has ? ecs::availableQuantity(actor.inventory, defName) : 0U;
			};
			for (const ItemStack& stack : actor.inventory.items) {
				count(stack.defName);
			}
			for (const std::optional<ItemStack>* hand : {&actor.inventory.leftHand, &actor.inventory.rightHand}) {
				if (hand->has_value()) {
					count((*hand)->defName);
				}
			}
		}
		// The planned amount beyond the cargo the chain was handed.
		return held >= std::max(progress.goal.quantity, 1U) + progress.goal.cargo;
	}

	void ActionSystem::giveThought(const ChainActor& actor, const assets::ThinkStep& step, ThoughtEventTypeId thought) {
		auto* thoughts = world->tryGetSystem<ThoughtSystem>();
		if (thoughts == nullptr) {
			LOG_WARNING(Engine, "[Chain] A <think> step with no ThoughtSystem in the world gives no thought");
			return;
		}
		const ChainTarget& target = actor.task.chain.stepTarget.resolved() ? actor.task.chain.stepTarget : actor.task.chain.target;
		switch (step.to) {
			case assets::ThoughtRecipient::Self: {
				const ThoughtSubject subject = target.entity != kInvalidEntity ? ThoughtSubject::aboutEntity(target.entity)
											   : target.defNameId != 0		   ? ThoughtSubject::aboutKind(target.defNameId)
																			   : ThoughtSubject::aboutNothing();
				thoughts->create({.colonist = actor.entity, .eventType = thought, .subject = subject});
				return;
			}
			case assets::ThoughtRecipient::Target:
				if (target.entity == kInvalidEntity || world->getComponent<Thoughts>(target.entity) == nullptr) {
					LOG_WARNING(Engine, "[Chain] A <think to=\"target\"> step whose target has no thoughts gives none");
					return;
				}
				thoughts->create({.colonist = target.entity, .eventType = thought, .subject = ThoughtSubject::aboutEntity(actor.entity)});
				return;
			case assets::ThoughtRecipient::Witnesses: {
				const auto* vision = world->tryGetSystem<VisionSystem>();
				if (vision == nullptr) {
					LOG_WARNING(Engine, "[Chain] A <think to=\"witnesses\"> step with no VisionSystem has no witnesses");
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
		// The goods set down, a later step's goods' claims are sized from the plan again until the next take.
		if (item == progress.goal.item) {
			progress.goodsTaken = false;
			progress.goodsClaimed = 0;
		}
		progress.takenItem = 0;
		progress.takenQuantity = 0;
		fileClaims(actor);
	}

} // namespace ecs
