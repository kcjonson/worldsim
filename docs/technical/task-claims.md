# Task Claims

Status: Spec (planning)
Created: 2026-11-09
Epic: WOR-46 (goal-driven task generation), reservation phase

How colonists avoid working against each other. Goals generate a few hundred goal-level tasks
([Task Registry](../design/game-systems/colonists/task-registry.md),
[Task Generation Architecture](./task-generation-architecture.md)), and each colonist pulls work
by scoring options against its own Memory
([Colonist Task Arbitration](./colonist-task-arbitration.md)). Nothing today stops two colonists
from choosing the same tree, both fetching the last unit a recipe needs, or both cutting toward
one goal until it is over-harvested. This spec adds claims: what a colonist has committed to,
recorded on its task, visible to every other colonist while it decides.

**MVP Status:** See [MVP Scope](../design/mvp-scope.md). Core plumbing for multi-colonist play;
no new player-facing feature beyond clearer task status.

---

## Model

A claim is data on the task that took it. When a colonist accepts work, its task records a claim
list for the whole planned chain: the source and quantity it will take, the station it will use,
the quantity it will deliver to a destination, the quantity it is bringing toward a goal. As
steps complete the task rewrites its own list (the source claim drops after pickup, the inbound
claim stays until deposit). When the task is cleared or destroyed its claims are gone, because
they were never anywhere else. There are no release calls and no timeouts.

A claim index (target to claimants and quantities, destination to inbound, goal to in-flight) is
derived from task data. It is a cache for lookups, is never saved, and a debug-build validator
checks it against the tasks.

Assignment stays agent-pull. Colonists are autonomous and directed indirectly
([Game Philosophy](../design/game-philosophy.md)); there is no dispatcher. A colonist reads claims
while it scores options and never receives work from them.

Why not a stored reservation table: RimWorld stores reservations owned by (pawn, job) with a
stack count and a max-pawns limit, and still ships a periodic verify sweep plus a purge on load
because reservations leak when a job ends by a path that forgot to release. Oxygen Not Included
keeps item reservations as quantities on the item, unserialized and scoped to entering and
leaving a chore state, which is the shape adopted here. None of RimWorld, ONI, Dwarf Fortress, or
Factorio uses timeouts; a timeout releases work that is still legitimately in progress and hides
the leak instead of removing it. In worldsim's code a task ends at about fifteen clear sites
(navigation recovery, player control, action failures, arbitration switches); data on the task
makes every one of them correct without touching it.

---

## Vocabulary and scope

A **claim** is a short-lived commitment by one task: "this task will take 14 wood from that pile",
"this task will deposit 14 wood into that box". It lives exactly as long as the task, or the part
of the task that needs it.

**Ownership** is a long-lived stored permission: a bed or room owner, a work order restricted to a
named colonist. It filters what a colonist may target at all. Ownership's design stays in
[Rooms](../design/game-systems/world/rooms.md) and is out of scope here; this spec only fixes the
boundary and the query order (below).

**Possession** means goods carried by a colonist: hands, belt, or pack. Nobody else can take them,
so they need no claim on themselves; what they carry is a claim on where they are going (inbound,
in-flight). Goods stored in a box are not possessed. A box is a source like a pile, and taking
from it is claimed with a source quantity claim.

**Task** means the `Task` component (`libs/sim/ecs/components/Task.h`): one per colonist, the
current activity. A `GoalTask` in `GoalTaskRegistry` is not a task in this sense. Goals never own
claims; they are what in-flight claims count toward. Two things own claims:

- the colonist's `Task`, including its suspended work record (see interruption below), each with
  its own task instance id;
- a craft binding on a station's work order, which owns the station slot and outlives any single
  `Task` (see craft binding below).

Every claim is keyed by the task instance that holds it, never by the colonist. RimWorld's later
"enroute" store for inbound counts is keyed by pawn, so ending one job drops the pawn's inbound
claims for a different job it still has queued; keying by task instance makes a colonist's current
and suspended work independent.

### Query order

A colonist deciding about a target applies, in order:

1. Discovery: the target is in its own Memory ([Memory](../design/game-systems/colonists/memory.md)).
   A colonist never targets something it has not seen. The one exception is storage the colony
   has placed, as a source for box-to-box transfer: it is common colony knowledge, gated by
   `colonyKnowsStorageEntity` (any colonist remembers the box), and any colonist may carry out the
   transfer. Boxes are things the colony built, and transfer, driven by the item's priority in each
   box, should run reliably rather than wait for a colonist who personally saw the source box. The
   exception covers colony-placed storage only. Its cost is handled by the per-poll cache under
   performance requirements.
2. Permission: ownership filters (not this colonist's bed, a work order restricted to someone
   else). Stored, long-lived, out of scope.
3. Claims: what is left after other tasks' claims.
4. Scoring: (tier, score) arbitration.

Claims are colony-wide common knowledge for anything the colonist already knows about. A colonist
considering a tree in its memory sees that another colonist has claimed it, even if that colonist
learned of the tree separately. Claims never reveal targets: a colonist does not learn of a pile
because someone else claimed it.

### Target keys

Each claim carries a target-kind tag, because the three key spaces overlap as raw integers:

| Kind | Key | Example |
|---|---|---|
| WorldKey | the Memory world-entity key, `hashWorldEntity(position, defNameId)` | tree, berry bush, ground pile |
| Entity | `EntityID` | storage box, crafting station, blueprint, packaged item, colonist |
| Goal | `GoalTask::id` | craft, storage umbrella, construction umbrella |

The Memory key is already what the harvest path stores in `Task::harvestTargetEntityId` (the
evaluator assigns the `knownWorldEntities` key, not an ECS id, whatever the comment beside it
says), so claims on remembered things use the key every colonist computes for the same object.
A pile is both a WorldKey (how colonists know it) and an `EntityID` (its `ResourceStack`); claims
use the WorldKey, and a world-side map from WorldKey to pile entity (below) resolves it.

---

## Claim kinds

| Kind | Counts | Target | Lifetime |
|---|---|---|---|
| Use | exclusive use, quantity 1 | WorldKey or Entity | until the work on it ends (harvest done, item picked up) |
| Source quantity | units to be taken from a source | pile (WorldKey), box (Entity) | until pickup or withdraw |
| Slot | one slot, optionally a numbered one | station, bed, toilet, interaction spot, builder slot | until the slot's activity ends; a craft slot then passes to the binding |
| Inbound | units to be deposited at a destination | box, station store, blueprint manifest | until deposit, or until the goods are dropped |
| In-flight | units on their way toward a root goal | Goal (with item) | until deposit, or until the goods are dropped |

A sketch of the data on the task:

```cpp
enum class ClaimKind : uint8_t { Use, SourceQuantity, Slot, Inbound, InFlight };
enum class TargetKind : uint8_t { WorldKey, Entity, Goal };

struct Claim {
    ClaimKind  kind;
    TargetKind targetKind;
    uint64_t   target;
    uint32_t   itemDefNameId;   // 0 for Use and Slot
    uint32_t   quantity;        // 1 for Use and Slot
    uint16_t   slotIndex;       // numbered slots (builder positions); kAnySlot otherwise
};

struct SuspendedWork {          // one interrupted work task, kept across a need
    uint64_t instanceId;        // the interrupted task's id, kept so its claims stay its own
    uint64_t claimSeq;
    uint64_t rootGoalId;
    uint32_t itemDefNameId;
    ClaimList claims;           // inbound and in-flight for the goods still held
};

// On Task:
uint64_t instanceId;            // assigned at acceptance, never reused
uint64_t claimSeq;              // acceptance order, for deterministic tie-breaks
ClaimList claims;               // fixed-capacity inline array; a chain holds at most a handful
std::optional<SuspendedWork> suspended;
```

`ClaimList` is a fixed-capacity array with a count. There is no small-vector type in worldsim or
salal today, and one is not needed for a bound this small.

Inbound and in-flight look redundant for a haul but guard different limits. Inbound protects the
destination's physical capacity (two hauls into one box must fit in it together; today they can
both pass the per-trip `addableCount` check and overfill). In-flight protects the goal's need,
including harvests that have not produced goods yet. A haul carries both; a harvest that has not
cut yet carries only in-flight.

### Needed now and built later

Needed now, each by a chain that runs today:

- Use on harvest sources (trees, bushes) and on packaged items being placed.
- Source quantity on loose piles and on storage boxes drained by storage-to-storage pulls.
- Inbound at storage boxes, crafting station stores, and blueprint manifests.
- In-flight toward storage, craft, and construction root goals.
- The craft station slot, from Craft selection, then held by the craft binding.
- Need tasks claiming the food they walk to (Use on a bush, source quantity on a pile or box), so
  two hungry colonists do not converge on the last berries and the bumping rule has something to
  compare against. This assumes the need-option fixes tracked as WOR-605 (the Hunger option picks
  the nearest edible source, not the nearest harvestable of any kind) and WOR-607 (an unreachable
  nearest source falls back to the next one instead of NoSource).

Designed for, built later: bed, toilet, and medical slots (with ownership), Chain_Rescue's patient
and medical bed, mobile targets (hunting and taming, an Entity key with a last-known position from
Memory's dynamic entities), builder slot counts and positions per blueprint (the count from
`constraints.xml` `builderCapBase` and `builderCapPerSquareMeter`, the position as `slotIndex`
into the construction work slots), slots on deconstruction, and colonist death and incapacitation,
which do not exist in `libs/` yet. None of these needs a new kind.

---

## Goal structure

Availability is computed per item at the root goal: the craft goal, the storage umbrella Haul
goal, or the construction umbrella Build goal. In-flight claims name the root. Child goals stay as
markers of which source kinds are open for an item (Harvest: cut a source that yields it; Haul:
fetch known stock of it), with no counters of their own. Today the counters disagree by child type:
`recordDelivery` credits the parent only for Haul children (`GoalTaskRegistry.cpp:103-112`), and
each Harvest child keeps its own `deliveredAmount`; with availability at the root, a Harvest and
its sibling Haul see one need and one in-flight total, and nobody re-cuts what a sibling task is
already bringing.

The carry-in after a harvest is part of the harvest task, not a separate goal. Today harvest
completion clears the task (`ActionSystem.cpp:349-351`), the carry-in Haul goal is spawned only when
the whole Harvest goal completes (`HarvestActions.cpp:337-346`), and construction pre-creates an
inventory-source Haul instead (`ConstructionSystem.cpp:485-505`). In every case the cut yield sits
in the colonist's hands with no task and no claim until it re-selects. Instead, harvest completion
converts the task in place into its deposit leg, the way a haul's Pickup becomes its Deposit
(`ActionSystem.cpp:243-262`). Consequences for goal structure:

- `createHaulForCompletedHarvest`, the chainId-tagged stocking carry-in Haul, and construction's
  per-material inventory-source Haul goals are deleted.
- Harvest children no longer have `targetAmount`/`deliveredAmount`; the root's need and in-flight
  decide whether another cut is wanted.
- Craft fetch Haul children stay (they open fetch-from-stock); their counters go too.
- One carry-in-from-possession option replaces the three inventory-source haul paths in
  `evaluateHaulOptions`: for any root goal accepting an item the colonist carries uncommitted, offer
  to deliver it. This also covers plain storage hauls, which have no carry-in path today
  (`AIDecisionSystem.cpp:457-461`), and goods left over after a bump or a switch.

---

## Chain claims

What each chain records at acceptance and when each claim drops. Q is the quantity the option was
sized to: the minimum of what the source has unclaimed, what the root still needs, what the
destination can take, and what the colonist can carry.

### Harvest with carry-in

Craft provisioning, construction delivery, and storage stocking.

| Step | Claims held |
|---|---|
| Accept | Use(source), In-flight(root, yield, E) |
| Harvest completes | The task converts to its deposit leg. Use drops. In-flight becomes the units landed, capped at the root's remaining need net of other tasks' in-flight; Inbound(destination, yield, same) is added |
| Deposit | all drop with the task |

E is the source's maximum yield (`amountMax`, or the remaining pool for a pooled source), capped
by what fits in the colonist's carry and by the root's remaining need. Over-estimating only makes
another colonist briefly see less available; under-estimating brings back the over-harvest this
spec removes. Units landed beyond the capped need are carried uncommitted; overflow that drops at
the stump is a loose pile, an ordinary new source with no claims.

### Fetch from stock (Chain_PickupDeposit)

Loose pile to storage, loose pile to a crafting station store (craft fetch), and box to box
(storage-to-storage pull, source is a box Entity).

| Step | Claims held |
|---|---|
| Accept | SourceQuantity(source, item, Q), Inbound(destination, item, Q), In-flight(root, item, Q) |
| Pickup or withdraw | SourceQuantity drops. Inbound and In-flight become the units lifted, capped at Q |
| Deposit | all drop with the task |

A box-to-box pull's source claim counts against the box's drainable quantity (stock above its own
minimum), so two pulls cannot drain a box below its minimum together.

### Carry-in from possession

The colonist already carries the goods. Accept records Inbound(destination, item, n) and
In-flight(root, item, n), n capped at the root's remaining need; deposit ends the task. Metered
deposits keep the rules from
[Crafting & Material Provisioning](./crafting-provisioning-architecture.md): the colonist deposits
exactly what is needed and keeps the rest, which holds no claim.

### Chain_PlaceItem

Accept records Use(packaged entity). Pickup drops it (the item is now possessed). The placement
target is fixed on the packaged item, so no slot is needed.

### Craft (Chain_CraftWithGather)

Provisioning (harvest with carry-in, fetch from stock, carry-in from possession) takes no station
claim; any number of colonists may provision one order in parallel through in-flight claims. The
colonist that selects the Craft step itself records Slot(station) at selection, so two crafters do
not walk to one station. At its first Craft action the station is bound to it (craft binding
below), and the binding holds the slot from then on. The optional output deposit is a carry-in from
possession. (The chain's `Gather` action has no runtime path since the `TaskType::Gather`
provisioning path was deleted; the claims follow what the code runs.)

### Chain_Rescue (built later)

Use(patient), Slot(medical bed). Pickup drops the Use; the bed slot stays until deposit.

### Build and Deconstruct

No claims now; any number of colonists may work an UnderConstruction blueprint. Builder slots
(count, then `slotIndex` positions) come with the builder-cap work.

### Need tasks

Eat records Use on a bush or SourceQuantity of one meal on a pile or box. Drink takes no claim
from unbounded water. Sleep and toilet slots come with beds and toilets.

---

## Availability

Every count is in items, except a box's slots. Self-exclusion applies to all of it: when a colonist
evaluates, claims whose instance id is its own current task or its suspended record are left out,
so the work it is doing does not read as taken. Without it, every re-evaluation would see its own
target as claimed and its own in-flight eating the remaining need, the option it is executing would
vanish, `isSameTask` would fail, and the switch path would clear the task.

- Root available (item) = remaining need (item) - in-flight (root, item).
- Source available = live quantity - claimed quantity, for source quantity claims; a Use target is
  available when no other task holds it.
- Destination available, station store or blueprint: the store is sized to the bill of materials or
  manifest, so remaining need minus inbound is the capacity check.
- Destination available, storage box (item X) = stack room in existing X stacks not already promised
  to inbound X, plus (free slots - slots promised to all inbound) x stackSize(X). Inbound at a box
  is converted to slots per destination: each inbound item first fills its own partial stacks, the
  rest needs ceil(rest / stackSize) slots. Different items share the box's free slots, so three
  hauls of three items cannot all count on the same last slot.

An option's quantity is the minimum of root, source, and destination availability and the
colonist's carry. Equivalent to ONI's remaining need = target - stored - outstanding in-flight.

### Remaining need

Remaining need is target minus what has landed at the destination, per item:

| Root | Target | Landed |
|---|---|---|
| Storage umbrella | to haul known stock: the item's configured max, or physical room when none is set; to stock by harvesting: the configured minimum | stored quantity of the item in the box |
| Craft | the current recipe's bill of materials | station store contents |
| Construction umbrella | the blueprint manifest | `delivered[]` |

Storage need is bounded per item and by the box's slots through the destination check above, so
per-item needs cannot sum past what the box holds. Today storage goals put a free-slot count in
`targetAmount`, so `availableCapacity()` goes to zero after one stack and the AI bypasses it with an
owner special case and `Inventory::addableCount`. That ends with item-counted roots and one helper,
remaining need for (root, item), used by the AI, the goal systems, and the UI.

### Live source quantity (a deliberate compromise)

A colonist reads the live count of a pile or box it already knows about (a pile's `ResourceStack`,
a box's inventory), the way the haul path already reads destination inventories. Memory gates
whether the colonist knows the source exists; it does not store counts. This is a game compromise,
less realistic than colonists remembering what they last saw in a pile, chosen because remembered
counts cost memory and update traffic and the pickup check corrects any error anyway. It may be
revisited; it is not the intended end state.

Live counts need a keyed lookup. Today a pile is found by scanning every `ResourceStack` entity
(`findLoosePile`, `HarvestActions.cpp:36-51`), which would make each option O(piles). A world-side
map from WorldKey to pile entity, maintained where piles spawn and are removed, makes it one lookup.
It indexes world truth, not claims, and is not part of the claim index.

---

## Interruption and bumping

### Rank

A claim's rank is its task's current `priorityTier`, read at each index rebuild, so rank follows
tier changes: a routine Eat that turns critical mid-walk is re-tiered on the next `isSameTask`
evaluation and its claims gain critical rank with it. Lower tier number is higher rank.

### Rules

- Needs beat work claims. A colonist acting on a critical need may take from any work claim: the
  last berries claimed for a storage haul go to the starving colonist.
- Only a selection at tier 1 to 3 (survival, critical needs, player orders) can bump, and only a
  holder whose tier number is strictly higher. Equal tiers never bump: two starving colonists do not
  trade the last berries back and forth; the second one looks for other food. Ordinary work (tiers
  4 to 7) never bumps anything, so there is no better-placed-worker preemption.
- A critical need (tier 2) may bump a player order (tier 3), following the arbitration ladder: a
  starving colonist eats before a forced chore. Decided by the owner.
- A claim stops being bumpable once its holder's action on that target has started (the chop has
  begun, the pickup is under way). Those actions are non-interruptable today, and a bumper treats
  the target as unavailable.
- Possessed goods are never bumped.

### Mechanism

A bumping colonist sees a target's claimant list through the index. When its need exceeds what is
available (or the target is under a Use claim), it selects anyway, records its own claim, and marks
the lowest-rank, latest claims that cover the shortfall as bumped on those colonists' tasks (a flag
naming the bumper). This is the one place a colonist writes to another's task, and it is the
RimWorld player-forced-job pattern: find the holder through a target-to-claimant lookup and
interrupt it. Until the victim re-plans, the target is over-subscribed by exactly the flagged
claims; the validator allows that and nothing else.

The bumped colonist checks the flag at its next AI update and at every action start. Abandoning a
bumped task bypasses the switch path's tier gate and non-interruptable gate
(`AIDecisionSystem.cpp:1361-1377`) and clears the task's Action, NavPath, and MovementTarget, as
`enterControl` does; the switch path today clears only the task, not the Action. The colonist keeps
whatever it carries and re-plans; any carried goods are offered back through carry-in from
possession. Being bumped is normal play, logged at debug level only.

### Interruption by the colonist's own needs

The colonist stows or drops carried goods as today (`handleChainInterruption`, driven by
`needsHands` and `handsRequired`; see [Task Chains](../design/game-systems/colonists/task-chains.md)).
A suspended record is created only when the interrupting selection is a need task:

- If the colonist keeps the goods (stowed, or the need does not use hands), the interrupted work
  becomes the suspended record: its instance id, claim sequence, root goal, item, and claims
  rewritten to Inbound and In-flight for the goods held (nothing on sources not yet reached).
- If it drops the goods, the dropped pile is an ordinary new source with no claims, and no record is
  kept. Anyone can haul the pile, the original colonist included. No ownership.

On later selections:

- another need task keeps the record;
- the carry-in-from-possession option for the record's root and item adopts it: the new task takes
  the record's instance id, sequence, and claims;
- any other selection (other work, Wander, a player move) discards it, so a stale record never
  holds a goal's in-flight.

Work preempted by other work (tier 4 over tier 6, or a same-tier switch past the hysteresis margin)
keeps no record; its claims end with the task, and carried goods are uncommitted until the colonist
picks carry-in from possession.

`Task::clear()` leaves the suspended record alone; the switch path calls `handleChainInterruption`
and then `task.clear()` (`AIDecisionSystem.cpp:1383-1387`), and the record must survive that.
Discarding it is a separate, explicit operation, called by the selection rules above, by
`enterControl`, and on colonist destruction.

Two existing behaviors to keep in mind. Craft, harvest, and pickup actions are non-interruptable
(`Action.h`), and the switch path's non-interruptable gate holds regardless of tier, so a critical
need waits for the current action to finish. And the switch path leaves an interruptable action
(sleep) active when it clears the task, while ActionSystem starts a new action only when none is
active (`ActionSystem.cpp:61-62`); a stale action may run under the next task. The bump path clears
the Action explicitly; the switch-path case is to be verified separately.

### Death and direct player control

Direct player control returns half-done work to the pool at once: `enterControl` already clears the
task and Action; it now also discards the suspended record. Carried goods stay with the colonist.
Death will do the same through entity destruction (no claims survive the task), and spawning the
dead colonist's carried goods as piles is designed for, not existing behavior. Player orders that
target a claimed thing (tier 3, a separate epic) bump through the mechanism above.

### Craft binding

The one exception to "claims die with the task" is already decided in
[Multi-colonist Crafting](../design/multi-colonist-crafting.md) section 2b: a crafter interrupted by
its own needs keeps the station and comes back to finish. A `Task` does not survive that
interruption, so the station slot is owned by a craft binding stored on the station's work order:

```cpp
struct CraftBinding {      // on WorkQueue
    EntityID crafter;
    uint64_t jobId;
};
```

- Jobs get ids. `CraftingJob` has none today and `addJob` merges a repeat recipe into the existing
  job (`WorkQueue.h:35-45`); a merge keeps the existing job's id, a new job gets a fresh one.
- Bound at the crafter's first Craft action on the job, not when provisioning starts. Between Craft
  selection and that action, the crafter's Slot(station) claim holds the station.
- Ties: a binding beats any other task's Slot claim on its station. Between two Slot claims with no
  binding, the earlier `claimSeq` wins. The loser finds out at selection (the index shows the slot
  held) or at action start (the station is bound or claimed by someone else) and re-plans.
- Held while the crafter does anything that is not other work: eating, sleeping, a critical need.
  Tier-4 stickiness brings the crafter back.
- Released when the job completes, when the job is cancelled or removed, when the player takes
  direct control of the crafter, when it starts a craft on a different station (one binding per
  colonist), and, once they exist, when it dies or is incapacitated.
- Release is derived the same way as task claims: the index treats a binding whose crafter is gone,
  player-controlled, or bound elsewhere, or whose job id is no longer queued, as free, and
  `CraftingGoalSystem` clears such bindings on its existing throttled poll. No event wiring.

Everything else about multi-colonist stations (UI, visuals, policy) stays in WOR-383.

---

## Stale memory

Memory can be wrong: the pile was hauled, the tree was felled, a box filled up. Claims are
validated against world truth when the task executes, and finding the thing gone or taken is
normal re-planning, not an error.

| Point | Checks | On mismatch |
|---|---|---|
| Selection | target in this colonist's memory, permission, claims in the index (self excluded) | option not offered (or offered as Reserved in the trace) |
| Action start (arrival) | target exists in the world, is still what the goal wants, source has at least one unit, destination has room, the task is not bumped, the station is not bound or claimed by someone else | forget the specific memory entry if the thing is gone (the harvest path already does this), clear the task, re-plan |
| Pickup | live source quantity | take what is there up to Q, rewrite inbound and in-flight to what was lifted, never take more than claimed |
| Deposit | root still exists, destination room, metered need | deposit what fits and is needed, keep the rest, end the task |

A colonist whose target is evicted from memory while it travels keeps its claim (the claim is on
the task, not in memory) and re-sees the target on arrival, within sight range. All of these log at
debug level; they are expected.

---

## The claim index

```
use:        target -> holder (instance id, colonist, tier, seq)
source:     target -> [(instance id, item, quantity, tier, seq)], total per item
slot:       (target, slotIndex) -> [holder]; bindings included
inbound:    destination -> per item quantity, and slots promised (boxes)
inFlight:   (root goal, item) -> quantity, with per-instance entries for self-exclusion
byColonist: colonist -> its claims, current and suspended
```

Queries: claimed quantity of a source, holder of a Use target or slot, inbound at a destination,
in-flight toward a root, each excluding a given set of instance ids; claimants of a target (for
bumping, the trace, and player orders); a colonist's claims (for the UI).

### Rebuild per AI update (recommended)

At the start of each `AIDecisionSystem::update` in which any colonist will evaluate, the index is
rebuilt from every colonist's task and suspended record plus every craft binding. Within the update
each selection appends its new claims, so a colonist deciding later in the same update sees what an
earlier one just took. Tasks cleared later in the same update (off-mesh recovery at
`AIDecisionSystem.cpp:1126` and `:1141`, the switch path at `:1387`, action failures) leave their
entries in the appended index until the next rebuild. That staleness only over-claims: another
colonist may skip a free item for one update, and nothing is double-booked.

Cost: dozens to low hundreds of colonists, each holding at most a handful of claims, is a few
hundred to a couple of thousand entries into flat, reused storage, against scoring that already
walks a few hundred goals per evaluating colonist. Incremental maintenance would need every clear
site, ActionSystem rewrite, and entity destruction to update the index, which is the release-call
surface this design removes.

### Readers and staleness

The AI is the only reader that sees a current index, during its update. The task panel adapter and
the goal systems read the last built index, which can be stale until the next update in which a
colonist evaluates. That is fine for both: the panel is display on a throttle, and goal systems
poll on a throttle and only lower harvest demand from in-flight, so staleness errs toward a short
delay, never a duplicate.

### Determinism

Which of two colonists wins a contested target depends on ECS view order and each colonist's
re-evaluation timer. Both are deterministic for a given world, spawn order, and command sequence,
so a headless run repeats exactly. Tests assert that exactly one colonist wins, never which one.

### Option dedup before scoring

The haul and harvest evaluators emit one option per known matching source per goal, scanning the
whole memory map for each goal. Before scoring, collapse equivalent options to the best one per
(root, item): the nearest available source by trip distance. ONI collapses equivalent undriven fetch
chores to the best per type for the same reason; it matters more for cost than lookup speed,
because it bounds the trace by goals and items rather than by remembered sources. The trace keeps
the selected option, plus at most one Reserved row per (root, item) for a better source someone
else holds.

---

## Save and load

Claims are not saved separately. What is saved (when save lands) is the data they derive from: each
colonist's `Task` with its claim list, instance id, sequence number, tier, and suspended record; the
claim sequence counter; and each station's craft binding and job ids. On load the index is rebuilt
and the validator runs once in debug builds. There is nothing to purge, because there is no
separate store to disagree with.

---

## Player-facing output

The UI docs own the layout ([In-game screen](../design/ui/screens/in-game.md) tasks panel,
[Decision Trace](../design/game-systems/colonists/decision-trace.md)); this spec only supplies the
data.

- Task list availability per root and item: "N available (M in progress)". N is the unclaimed
  quantity of sources the colony knows about, capped at the root's remaining need; M is in-flight.
- Worker: the colonist(s) holding claims toward the root, from the index, replacing the adapter's
  scan of every colonist's task.
- `knownBy`: colonists whose memory holds at least one unclaimed source for the root, computed on
  the panel's throttle through Memory's capability index.
- Per-colonist task list: roots for which that colonist knows an unclaimed source.
- Blocked reasons, distinct: "no source known" (NoSource), "all known sources claimed",
  "destination full (M inbound)", "needs materials" (construction), "station in use by Alice".
- Decision trace: a Reserved option status, "Berry Bush (reserved by Alice)", from the index's
  claimant lookup. A bumped colonist's trace shows "bumped by Alice (critical hunger)" on its
  re-plan.

---

## Performance requirements

Measured on the perf capture against `main` on the same machine, plus unit benchmarks:

- AI update cost (`AIDecisionSystem::update`, mean and p99) shows no regression against `main`
  beyond run-to-run noise in the desert perf view, and in a two-colonist run of scenario 02.
- Index rebuild is under 5% of `AIDecisionSystem::update` in a benchmark of 200 colonists and 300
  goals (extend `HandlesMultipleColonistsEfficiently`), with no heap allocation in steady state.
- No per-goal full-memory scans. Evaluators iterate Memory's capability index
  (`getEntitiesWithCapability`, Carryable for piles and Harvestable for sources), and the per-def
  yield lookup is resolved once per def, not per entity per goal. Extend
  `MemoryCapacityDoesNotAffectDecisionSpeed` with 10,000 remembered entities that match no goal:
  decision time stays within noise of an empty memory.
- Live pile quantity is one keyed lookup through the WorldKey-to-pile map, not a scan of piles.
- Box-pull gating keeps today's colony-wide rule, but not its cost. Today `colonyKnowsStorageEntity`
  walks every colonist's memory per box per goal per evaluating colonist, which is quadratic in
  colonists. Instead, colony knowledge of each storage entity is computed once per goal-system poll
  (one pass over colonist memories per box) and cached; evaluators read the cache. Same behavior,
  linear cost, and staleness of one poll interval, the same as every other goal-system input.
- Trace size per colonist is bounded by roots times accepted items after dedup, not by remembered
  sources.
- The UI's availability and `knownBy` run on the panel throttle, never per frame, and also use the
  capability index.

These evaluator changes land before claim lookups are added to the evaluators, so the added lookups
are measured against an evaluator that already scales with goals, not memory.

---

## Testing

The multi-colonist regression vehicle is the headless sim-cli ([Sim CLI](./sim-cli.md), WOR-603),
built before this work. Its scenarios for this spec:

- two colonists, one tree;
- last ingredient unit (one loose unit, two colonists, a recipe short by one);
- three haulers, one free slot in a box, three different items;
- a starving colonist bumps a hauler from the last berries;
- a crafter sleeps mid-craft and returns to its bound station while a second colonist does not take
  it.

Each asserts outcomes (exactly one winner, no overfill, exact bill of materials, nobody left holding
stranded goods), not which colonist won. Scenarios 01 to 04 under `docs/testing/scenarios/` stay the
single-colonist pass in the game.

Unit tests by behavior, in the existing suites (`AIDecisionSystem.test.cpp`, `ActionSystem.test.cpp`,
`CraftingGoalSystem.test.cpp`) plus new `ClaimIndex.test.cpp`, `GoalTaskRegistry.test.cpp`, and
`StorageGoalSystem.test.cpp`. Check for an existing test of the same path before adding one.

- A claimant keeps its target across re-evaluations (self-exclusion).
- Two colonists, one tree, one root: they never pick the same tree, and in-flight stops the second
  from cutting when the first's expected yield covers the need.
- Harvest completion converts the task to its deposit leg with in-flight capped at remaining need
  and inbound added; a second colonist does not re-cut during the carry.
- Two colonists, a recipe needing one more unit, one loose unit known to both: exactly one fetches.
- Three hauls of three items into a box with one free slot: only one is accepted.
- A large pile split across two colonists up to its quantity, never beyond it.
- Clearing a task, taking player control, and destroying a colonist each leave no claims for that
  colonist in the rebuilt index; nothing calls a release function.
- Stow on a need keeps the suspended record through `task.clear()`; carry-in adopts it; Wander
  discards it; a drop leaves no record and the pile is claimable by anyone.
- Critical hunger bumps a storage hauler; the hauler's Action is cleared, it re-plans, and keeps its
  cargo. Two critically hungry colonists never bump each other. Ordinary work never bumps ordinary
  work. A routine Eat that turns critical mid-walk gains critical rank.
- Two colonists selecting Craft at one station: one Slot claim wins by sequence; the binding then
  survives the crafter eating, and releases on job cancel, job completion, crafter control, crafter
  destruction, and a craft at another station.
- Storage remaining need is item-counted and matches the box's per-item room.

The debug validator runs at the end of every AI update in debug builds and asserts: a fresh rebuild
is a subset of the appended index, and every surplus entry belongs to an instance id that no live
task or suspended record holds (a task cleared this update); no target is over-subscribed except by
claims flagged bumped; every Inbound has a matching In-flight on the same task; in-flight per root
and item never exceeds remaining need plus flagged bumps; every binding names a live crafter bound
nowhere else and a queued job id; no claim belongs to an inactive task.

---

## Migration

Deleted in the same change that replaces them, per the no-legacy rule:

- `GoalStatus::InProgress` and `GoalStatus::Complete` (never assigned). "In progress" is in-flight
  from the index.
- `GoalOwner::None`. Every creator already sets an owner; the default goes, and the registry's
  owner-index guards for None go with it.
- `reservationTimeout`: `TimingConfig::reservationTimeout`, `getReservationTimeout()`, its parse in
  `PriorityConfig.cpp`, the element in `priority-tuning.xml`. It is read by nothing today.
- The in-flight approximations claims replace, all at once so nothing subtracts carried goods twice:
  `ConstructionSystem::carriedAmount` and `constructionHarvestDemand`; `CraftingGoalSystem`'s
  `colonyCarriesStock`; the craft-fetch gate `deliveredAmount + carried` in `evaluateHaulOptions`
  (`AIDecisionSystem.cpp:535-537`); the craft pickup sizing from `deliveredAmount + carried` in
  `HaulActions.cpp` (about lines 413-424).
- `createHaulForCompletedHarvest`, the chainId-tagged stocking carry-in Haul, construction's
  per-material inventory-source Haul goals, and the three inventory-source haul paths in
  `evaluateHaulOptions`, replaced by the in-place deposit leg and carry-in from possession.
- Child goal counters (`targetAmount`/`deliveredAmount` on Harvest and Haul children), the storage
  slot-count use of `targetAmount` and its owner special case in `evaluateHaulOptions`, and
  `GoalTask::availableCapacity()`, replaced by remaining need at the root. `recordDelivery`'s
  Haul-only parent credit goes with them.
- The per-call colony memory walk inside `colonyKnowsStorageEntity`, replaced by the per-poll cache
  (the colony-wide rule itself stays).
- The adapter's per-goal colonist scan (`colonistWorkingGoal`) in favor of the index.
- Comments that state there is no reservation system (`enterControl`, the storage-pull gate), and
  the "world entity ID" comment on `harvestTargetEntityId`.

---

## Out of scope (seams only)

- Unifying the two crafting task paths (WOR-295). Claims attach to whatever task shape runs; the
  chain table changes rows, not kinds.
- Multi-colonist crafting UI and station policy beyond the claim and binding (WOR-383).
- Door slot queues. Pathfinding owns them.
- Ownership's own design. Only the query order above is fixed here.
- The need-option source selection bugs (WOR-605, WOR-607); this spec assumes them fixed.

---

## Related documents

- [Task Registry](../design/game-systems/colonists/task-registry.md): goal-level tasks and the
  per-colonist view claims filter.
- [Task Generation Architecture](./task-generation-architecture.md): goal sources and polling.
- [Colonist Task Arbitration](./colonist-task-arbitration.md): tiers, which set claim rank.
- [Crafting & Material Provisioning](./crafting-provisioning-architecture.md): station store and
  metered deposits.
- [Multi-colonist Crafting](../design/multi-colonist-crafting.md): station ownership during a craft.
- [Building & Construction Architecture](./building-construction-architecture.md): blueprint
  manifests and builder slots.
- [Task Chains](../design/game-systems/colonists/task-chains.md): stow-or-drop on interruption.
- [Memory](../design/game-systems/colonists/memory.md): discovery gating and the capability index.
- [Decision Trace](../design/game-systems/colonists/decision-trace.md): the Reserved status.
- [Sim CLI](./sim-cli.md): headless scenarios used as the multi-colonist regression pass.
