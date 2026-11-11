# 2026-11-11 - Task claims and colonist survival (integration/colonist-survival)

## Summary

Colonists now record what they have committed to as claims on their own task, and every other
colonist reads those claims while it decides, so two colonists no longer cut one plant, fetch one
pile past its count, or overfill a box. Goal counters are gone; how much a goal still wants is read
from the world at its root. Hunger now draws on piles and boxes as well as bushes and the pack, and
bushes regrow on the game clock. The branch also carries the survival fixes merged into it: need
source selection, the need-action slide, build-site clearing, and the sim-cli groundwork. Bumping,
the craft binding, most of the panel output, and sim-cli itself are not built.

## Details

### Claims, the index, and the validator

- A claim (Use, SourceQuantity, Slot, Inbound, InFlight) lives on `Task::claims`, keyed by the task's
  instance id and ordered by its claim sequence, both from one counter on `GoalTaskRegistry`. Claims
  go with the task; there are no release calls and no timeouts.
- `ClaimIndex` is derived from the tasks: a flat entry array with a keyed table and a per-instance
  chain. It is rebuilt the first time a colonist decides in an AI update, each selection appends to
  it, and every rewrite inside the update retires or re-files the instance it touches.
- In development builds `ClaimIndex::findViolation` runs after every AI update that built the index:
  a fresh rebuild must be a subset of the appended index, with surplus only on dead instances; every
  Inbound pairs with an InFlight; no inactive task holds claims.

### Remaining need replaces the counters

- `targetAmount`, `deliveredAmount`, `availableCapacity()`, `recordDelivery`'s parent credit,
  `createHaulForCompletedHarvest`, the chain ids, construction's per-material inventory Haul goals,
  `carriedAmount`, `constructionHarvestDemand`, `colonyCarriesStock`, and the three inventory-source
  haul paths are deleted.
- `GoalTaskRegistry::remainingNeed(world, root, item, basis)` reads the target and what has landed
  (the station's next job and store, the blueprint manifest, the box's rules and stock) on every call.
  What other colonists are bringing is their in-flight claims at the root.
- Dead goal state went too: `GoalStatus::InProgress` and `Complete`, `GoalOwner::None`, and
  `reservationTimeout`.

### Chains

- A landed cut converts in place into its deposit leg (`ActionSystem::carryHarvestIn`), and every way
  into a deposit leg goes through `ActionSystem::headToDeposit`.
- One carry-in from possession (`evaluateCarryInOptions`) offers anything a colonist carries to any
  root that wants it.
- Batching: a colonist gathering for a root takes a known pile, then another cut, before carrying in,
  until the trip is full, the need is covered, or no source is left. A top-up cut is taken only when
  its yield fits. Once a delivery is kept (`Task::deliveryChosen`) nothing turns it back.
- A need that interrupts a haul or a cut keeps the goods as a suspended record, which the carry-in or
  a further cut adopts afterwards; while the colonist sleeps the record holds nothing for anyone else.
- A colonist whose route has failed holds only the claims on goods it carries.
- Provisions: a colonist's own food is never cargo for any root.

### Hunger

- A hungry colonist with an empty pack searches bushes, remembered piles, and colony boxes, nearest
  reachable first. A pile or box gives one meal (enough to reach the seek threshold); a bush is cut
  whole.
- The take makes room first (`ActionSystem::makeRoomToEat`): it puts away just enough non-food,
  non-tool goods, claimed cargo last, into a box within 1.5 m or onto the ground.
- Eater urgency: an eater at least as urgent takes food; anything else (a less urgent eater, work,
  idle gathering) only holds it, and the colonist uses held food last.
- Bushes regrow on `TimeSystem::simulationSeconds`, keyed by the entity's position and def; on main
  the cooldown never ran out. A colonist whose only known food is regrowing waits
  (`SourceReachMemo::FoodWait`) without re-searching.

### Decision path

- Per-update caches: `RemainingNeedCache`, `BoxRoomCache`, `ColonyStorageKnowledge`, and the list of
  boxes holding food, each refreshed for a box a colonist puts goods into mid-update.
- Work sources come from Memory's per-def key lists, so a goal visits only its own items; definitions
  and harvest yields are looked up by id; a loose pile is found through `LoosePileIndex`.
- The trace keeps a format literal and ids per option and composes its text when drawn
  (`describeOption`). It gained `Reserved`, `Regrowing`, and `NoRoom` rows.
- The task panel names each row's worker from the colonists' current tasks and hides goals with
  nothing to do.

### Vision

Vision's reconcile forgot and re-remembered every pile, box, station, spawned bush, and packaged item
in view on every tick, which churned Memory and dropped the regrowing-food wait near the colony. An
entry missing from the placement index is now only a candidate, struck off when the ECS pass sees it.

### Merged into the branch

- Need source selection (WOR-605, WOR-607): the Hunger option takes the nearest reachable edible
  source and tries farther ones; reach verdicts are remembered per colonist; a failed route is stamped
  with the colonist's own reach key.
- The need-action slide (WOR-606): MovementSystem stops a colonist whose movement target is off.
- Build-site clearing (WOR-646): clear goals serve any harvestable in the footprint, one per site, and
  a regrowing blocker is removed.
- sim-cli groundwork (WOR-603 steps 1 to 5): required RNG seeds, work config loading in libs/assets,
  the chain id counters moved into `GoalTaskRegistry` (now the claim sequence),
  `NavigationSystem::finishPendingBuilds`, and `SyntheticWorldSampler`.
- The nav region split and edge walk (WOR-644) were merged and then removed (2c0f3b19) for a redesign.

### What a player will see

Lines marked "decided during implementation" were judgement calls made without the owner and are for
the owner to confirm.

- A cut berry bush regrows after 60 game seconds; colonists skip it while it regrows.
- A hungry colonist eats from a known box or loose pile, one meal (one berry above 20% hunger, two
  below) (decided during implementation). A box's minimum does not protect its food (decided during
  implementation). Of two foods in one box, the lower item id goes first (decided during
  implementation).
- A full pack does not stop a colonist eating; he puts away non-food, non-tool goods first (decided
  during implementation). A colonist holding a tool in every pack slot, belt slot, and hand can't.
- A critically hungry colonist takes a bush a routinely hungry one was walking to; equal urgency is
  first-come. A hungry colonist prefers food nobody is heading for but will take one a worker holds
  (decided during implementation).
- Idle food gathering is offered only while the pack has no food and has room (decided during
  implementation). A colonist's own food is never hauled anywhere unless he fetched or cut it for
  there (decided during implementation).
- A box's configured max caps hauls and deposits, as long as no wildcard rule for the category has
  no max (decided during implementation). A category wildcard's minimum no longer drives cutting
  (decided during implementation).
- Two colonists never cut one plant, never fetch one pile past its count, and never overfill a box's
  last slot. A haul whose pickup finds nothing ends at the source.
- Colonists gather a full trip before delivering; a top-up cut is taken only if its yield fits; a
  known loose pile comes before a fresh cut whatever the distance; a cut's surplus drops beside the
  source as a pile; once a delivery starts nothing turns it back (all decided during implementation).
- A colonist with no route releases what he hasn't reached (decided during implementation).
- An interrupted haul's goods go into a box within 1.5 m before the ground. A colonist interrupted by
  a need resumes the delivery afterwards; while he sleeps another colonist may fetch the same goods
  instead (decided during implementation). A two-hand armful nobody wants is put down before he
  wanders (decided during implementation).
- Several colonists can provision one craft without over-delivering. A station another crafter is
  walking to reads Reserved; it is free again once that crafter leaves for a need.
- Construction sites take known loose piles of a due material (a fell's overflow) before anyone cuts
  more (decided during implementation), and nothing is carried to a site before it wants it.
- The task panel hides rows with nothing to do and names one worker per row; the trace shows
  Reserved, Regrowing, and no-room rows.

### Not built

Bumping (beyond the eater-urgency rule for food), the craft binding and job ids, the panel's Known by,
availability counts, per-colonist filter and distinct blocked reasons, the collapse of options to the
best source per goal and item, sim-cli and its scenarios, and the performance benchmarks the spec
names as targets.

### Files and directories touched

- Claims and chains: `libs/sim/ecs/ClaimIndex.*`, `libs/sim/ecs/systems/ChainClaims.*`,
  `libs/sim/ecs/components/Task.h`.
- Goals and need: `libs/sim/ecs/GoalTaskRegistry.*`, the goal systems in `libs/sim/ecs/systems/`
  (storage, crafting, construction).
- Decisions and actions: `libs/sim/ecs/systems/AIDecisionSystem.*`, `ActionSystem.*`, `action/`,
  `WorkSources.*`, `libs/sim/ecs/StoreOrDrop.*`, `LoosePileIndex.h`, `WorldSource.*`,
  `components/DecisionTrace.*`, `components/Memory.*`, `components/MemoryQueries.h`,
  `systems/VisionSystem.*`.
- Game: `apps/world-sim/scenes/game/ui/adapters/GlobalTaskAdapter.cpp`, the game scene's regrowth
  wiring.
- Tests: the `libs/sim` suites, `ChainClaims.test.cpp` and `ClaimIndex.test.cpp` new.
- Docs: `docs/technical/task-claims.md` and the colonist, crafting, arbitration, task generation,
  sim-cli, and testing docs.

## Related Documentation

- [Task Claims](../../technical/task-claims.md)
- [Task Registry](../../design/game-systems/colonists/task-registry.md)
- [Task Chains](../../design/game-systems/colonists/task-chains.md)
- [Decision Trace](../../design/game-systems/colonists/decision-trace.md)
- [Crafting & Material Provisioning](../../technical/crafting-provisioning-architecture.md)
- [Sim CLI](../../technical/sim-cli.md)
- [Scenario 11: Colonists eat](../../testing/scenarios/11-colonists-eat.md)

## Next Steps

- Run the scenario pass (01 to 04, 11) in the game and confirm the decided-during-implementation
  behaviour with the owner.
- Build the craft binding, so a crafter keeps its station across a need.
- Decide whether bumping is still wanted beyond the food rule.
- Work through the known gaps in the task claims spec.
