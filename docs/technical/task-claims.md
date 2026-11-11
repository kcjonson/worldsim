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
   `ColonyStorageKnowledge` (any colonist remembers the box), and any colonist may carry out the
   transfer. Boxes are things the colony built, and transfer, driven by the item's priority in each
   box, should run reliably rather than wait for a colonist who personally saw the source box. The
   exception covers colony-placed storage only. Its cost is handled by the per-update memo under
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
};                              // built only through per-kind factories, which fix targetKind


struct SuspendedWork {          // one interrupted work task, kept across a need
    uint64_t instanceId;        // the interrupted task's id, kept so its claims stay its own
    uint64_t claimSeq;
    uint64_t rootGoalId;
    uint32_t itemDefNameId;
    ClaimList<2> claims;        // that item's inbound and in-flight, for the goods still held
};

// On Task:
uint64_t instanceId;            // assigned at acceptance, never reused
uint64_t claimSeq;              // acceptance order, for deterministic tie-breaks
ClaimList<4> claims;            // fixed-capacity inline array; a chain holds at most three
std::optional<SuspendedWork> suspended;
```

`ClaimList` is a fixed-capacity array with a count. There is no small-vector type in worldsim or
salal today, and one is not needed for a bound this small. Overfilling one is a programming error and
aborts in development builds. Both ids come from one counter: at acceptance a task takes the next
claim sequence number as its instance id and its sequence alike; only an adopted record keeps older
ones.

A `Claim` is built only through one factory per kind, which fixes the key space (Use and
SourceQuantity name theirs, Slot and Inbound are Entity, InFlight is Goal), so no claim can be filed
where its queries never look. A target's `kAnySlot` is its one unnumbered slot (a station) and
conflicts with every slot on that target, numbered or not; two numbered slots (builder positions)
conflict only with themselves.

Inbound and in-flight look redundant for a haul but guard different limits. Inbound protects the
destination's physical capacity (two hauls into one box must fit in it together; today they can
both pass the per-trip `addableCount` check and overfill). In-flight protects the goal's need,
including harvests that have not produced goods yet. A haul carries both; a harvest carries Inbound
only for the goods earlier cuts of its gathering already put in the colonist's hands.

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
fetch known stock of it), with no counters of their own: a Harvest and its sibling Haul see one need
and one in-flight total, and nobody re-cuts what a sibling task is already bringing. A deposit never
touches goals; each goal system retires the children whose item its root no longer needs on its own
poll. The construction umbrella names in `acceptedDefNameIds` the materials that are due (empty
while clearing or while a wall or opening waits on its host), and its remaining need is zero for
anything else, so goods are not carried to a site before it wants them.

The carry-in after a harvest is part of the harvest task, not a separate goal: harvest completion
converts the task in place into its deposit leg (`ActionSystem::carryHarvestIn`), the way a haul's
Pickup becomes its Deposit, so the cut yield never sits in the colonist's hands with no task and no
claim. Every way into a deposit leg (pickup, withdraw, arriving at the source already carrying, a
landed cut) takes one handoff, `ActionSystem::headToDeposit`. Consequences for goal structure:

- `createHaulForCompletedHarvest`, the chainId-tagged stocking carry-in Haul, and construction's
  per-material inventory-source Haul goals are deleted.
- Harvest children no longer have `targetAmount`/`deliveredAmount`; the root's need and in-flight
  decide whether another cut is wanted.
- Craft fetch Haul children stay (they open fetch-from-stock); their counters go too.
- One carry-in-from-possession option (`evaluateCarryInOptions`) replaces the three inventory-source
  haul paths: for any root goal still wanting an item the colonist carries, offer to deliver it. This
  also covers plain storage, and goods left over after a cut, a bump, or a switch. Tools are
  equipment and are never offered, and the colonist's provisions (below) are offered to no root. The
  option is sized min(cargo, root available) and matches the task it is executing.

### Provisions

A colonist's own food is its provisions, never cargo: no carry-in offers it to any root, a cut does
not sweep it along with what it lands, and a fetch does not count it as already picked up. Food is
cargo toward a root only while the colonist's task or suspended record carries it there: berries it
was sent to fetch from a pile or cut for that root, which its Inbound names. Everything else a
colonist carries (anything that is not food) is cargo for any root that wants it. So:

- a carry-in of food is sized to the food its task or record carries toward that root, which is how an
  interrupted delivery of food, to storage or anywhere else, resumes: the record marks it as cargo and
  the carry-in adopts it;
- a cut toward a root counts as already carried only food an earlier cut handed on, and a landed cut
  commits what that cut landed plus that, never the colonist's own;
- a pickup of food commits what the pickup lifted, and a haul of food still on its way to its source
  counts the food in the colonist's pack as provisions, not as goods already in hand;
- a deposit never takes more than the task carries, so the meal stays in the pack.

What the colonist would still fetch or cut for a root nets out its cargo for that root, never its
provisions. Eating is not limited by this: a hungry colonist eats from its pack, cargo included, and a
record whose goods were eaten shrinks to what is left.

---

## Chain claims

What each chain records at acceptance and when each claim drops. Q is the quantity the option was
sized to: the minimum of what the source has unclaimed, what the root still needs, what the
destination can take, and what the colonist can carry.

### Harvest with carry-in

Craft provisioning, construction delivery, and storage stocking.

| Step | Claims held |
|---|---|
| Accept | Use(source), Inbound(destination, yield, C) when C > 0, In-flight(root, yield, C + E) |
| Harvest completes | The task converts to its deposit leg. Use drops. In-flight and Inbound become its cargo, C plus what this cut landed, capped at its own in-flight and at the root's remaining need net of other tasks' in-flight. Nothing carried, or nothing still needed, ends the task and leaves the goods uncommitted. The colonist decides again at once (batching, below) |
| A further cut | The deposit leg hands its instance id and cargo claims to the next cut for the same root and item, which adds Use on the new source and raises In-flight by its E |
| A loose pile | The leg hands its cargo claims to a pickup of a known pile of the item for the same root, which adds SourceQuantity(pile, Q) and raises Inbound and In-flight to C + Q; it lifts Q and delivers C + Q |
| Deposit | all drop with the task |

C is what the colonist already carries toward the root: the cargo an earlier cut of the same
gathering handed on, or an adopted suspended record's (or, with neither, what it carries of the
item that is not provisions, above), capped at the root's remaining need. E is the source's most per
cut (`amountMax`; a pooled source gives at most that per cut too), capped by what still fits in the
colonist's carry and by the root's remaining need less C. Over-estimating only makes another
colonist briefly see less available; under-estimating brings back the over-harvest this spec
removes. Units landed beyond the cap are carried uncommitted. A cut gives its whole yield: what the
colonist can't carry drops beside the source as a loose pile, a one-hand yield like a fell's overflow,
an ordinary new source with no claims. The cap reads the claim index the AI last built,
and the leg never commits more than its own in-flight there: another colonist converting a cut in
the same ActionSystem update read that figure, so the two together never pass the need. The need
itself is read from the world at that moment.

#### Batching

A colonist gathering for a root delivers when one of these holds:

- its trip is full for the item: the room left is too little for another cut (below), or none (the
  cut shows NoRoom);
- what it carries covers the root's remaining need net of others' in-flight;
- it knows no other source of the item that is ready, reachable, and not reserved.

Until then the next leg for the same root and item outranks the carry-in. Within a tier the order
is: a known loose pile of the item toward that root, then a further cut toward it, then every option
that is no leg of a trip, the carry-in included. A pile counts as a source here too: he picks up what
he or anyone dropped before cutting again. A pile is offered for every root that wants the item,
construction sites included; when one is offered, a fresh cut for the same root and item ranks below
everything else in its tier, so even at the start of a trip the pile is taken up first. A pickup that
continues a gathering lifts what still fits and is decided again at once, like a landed cut.

A top-up cut (one toward a root the colonist already carries goods for) is offered only when the room
left takes at least the smaller of the source's largest yield (`amountMax`) and what the root still
wants beyond what is carried; otherwise he delivers. So a trip of ten taking cuts of three is walked at
nine rather than spending a fourth plant on one unit, and no tree is felled for a sliver of its wood.
A cut can still land more than fits (the first cut of a trip into a small pack), and the surplus drops
as a pile. A source that is regrowing, unreachable, or reserved is passed over at selection, so the
carry-in is taken as soon as no source is left. A landed cut is decided again at once rather than after a step
toward the destination. While it gathers, the colonist's in-flight toward the root covers what it
carries plus the cut it has accepted, so another colonist does not cut what those cover, and the
cargo moves from task to task under one instance id with no window in which it is uncommitted. A
need that interrupts the gathering keeps the goods as a suspended record (below), which the next
cut or the carry-in adopts.

### Fetch from stock (Chain_PickupDeposit)

Loose pile to storage, loose pile to a crafting station store (craft fetch), and box to box
(storage-to-storage pull, source is a box Entity).

| Step | Claims held |
|---|---|
| Accept | SourceQuantity(source, item, Q), Inbound(destination, item, Q), In-flight(root, item, Q) |
| Pickup or withdraw | The pickup asks for Q (its SourceQuantity; a pile pickup that took a gathering over delivers its cargo with it). SourceQuantity drops. Inbound and In-flight become the units lifted, capped at Q. Arriving at the source or the destination already carrying the item skips the pickup and rewrites the same way, to what is carried |
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

### Build-site clear

A construction clear Harvest records Use(source) only: it removes what stands in the footprint and
brings nothing toward a need, so it has no in-flight.

### Build and Deconstruct

No claims now; any number of colonists may work an UnderConstruction blueprint. Builder slots
(count, then `slotIndex` positions) come with the builder-cap work.

### Need tasks

Eat records Use on a bush or SourceQuantity of one meal on a pile or box; food already in the pack is
possessed and takes none, and the gather-food option claims its bush like Eat. Drink takes no claim
from unbounded water. Sleep and toilet slots come with beds and toilets.

A hungry colonist's sources are, nearest reachable first in one search: its own pack (no walking),
a ready harvestable whose yield is edible, a loose pile of an edible item it remembers, and a storage
box the colony knows (the `ColonyStorageKnowledge` rule the box-to-box pull uses) that holds an edible
item. A box's own minimum does not keep its food from a hungry colonist; stored food is the buffer
between harvests. One meal is what takes the need back to its seek threshold at the item's
nutrition, at least one unit, capped by what the source has left for this colonist and by what one
pickup can lift (`pickupUnitsThatFit`, never below one). A bush is cut whole, as before, because a
cut gives what it gives; a pile or box gives the meal and no more, because stored food is the
colony's and anything taken past the meal would become one colonist's provisions, out of everyone
else's reach. The pickup or withdraw ends the task and the next decision eats from the pack, the way
a harvest does. A pack with no room for one unit still eats: the pickup first puts away what the
colonist carries that is not food (the pack's last stack, then its hands) by the store-or-drop rule.

What a pile or box has left for a hungry colonist is its live count less the shares of eaters at
least as urgent. Work claims on food (a haul of berries to a box, a pull between boxes, a craft fetch)
never hold it: the hungry colonist takes it, and a hauler that arrives after finds less or nothing,
and its empty pickup ends the haul (or, re-deciding first, it sees the pile taken and lets it go).
Such a held source still comes after a free one, as for bushes.

The regrowing-food wait (`SourceReachMemo::FoodWait`) covers harvestables only: it is set when every
edible harvestable the colonist knows is regrowing, and while it holds those are not searched again.
Piles and boxes are searched at every decision of a hungry colonist, so a box that gains berries, or
a pile that appears, ends the wait at once. The boxes holding food are worked out once per AI update
and shared by every hungry colonist; box contents change only in ActionSystem.

A hungry colonist reads a bush as taken only when the holder will eat from it and is at least as
urgent: a Hunger task whose tier is no higher a number than its own (lower tier wins, equal tiers keep
first-come). A critically hungry colonist (tier 2) ignores a routinely hungry one's claim (tier 5),
however far off that one is and whoever decided first. A work claim (a cut toward a goal) or the idle
gather-food option does not hold food against a need, so the hungry colonist still targets that bush
and both claim it until bumping (step 8) flags the worker; before claims both walked to it anyway, so
nobody is worse off. Such a held source is still the colonist's to take, but one nobody holds is
preferred when it knows one that is reachable and ready: the search, nearest first, passes over a held
source and comes back to the nearest held one only when nothing free is left. The gather-food option
is idle work and gives way to any holder, so it takes the nearest free bush.

The less urgent eater is not flagged (that is the rest of step 8). At its next decision it sees the
bush held by someone at least as urgent and looks elsewhere; if it reaches the bush first it eats, and
if it arrives second the bush pays nothing (gone or regrowing), its task ends, and its next decision
passes the regrowing bush over like any other, so it never loops on it. Colonists can starve only if
they never find food: a Reserved food source is always one another colonist at least as hungry is
about to eat, for one harvest.

---

## Availability

Every count is in items, except a box's slots. Self-exclusion applies to all of it: when a colonist
evaluates, claims whose instance id is its own current task or its suspended record are left out,
so the work it is doing does not read as taken. Without it, every re-evaluation would see its own
target as claimed and its own in-flight eating the remaining need, the option it is executing would
vanish, `isSameTask` would fail, and the switch path would clear the task.

- Root available (item) = remaining need (item) - in-flight (root, item).
- What a colonist would still go and get (fetch or cut) also nets out what it carries of the item:
  its own goods are already on their way. Whether it cuts more before delivering them is the
  batching rule above.
- Source available = live quantity - claimed quantity, for source quantity claims; a Use target is
  available when no other task holds it. Use is exclusive for every cut, clears included: several
  colonists may clear one site, each its own plant.
- Destination available, station store or blueprint: the store is sized to the bill of materials or
  manifest, so remaining need minus inbound is the capacity check.
- Destination available, storage box (item X) = room in X's own stacks (its existing partial stacks
  plus the slots its own inbound already opens, less inbound X), plus (free slots - slots promised to
  all inbound) x stackSize(X). Inbound at a box is converted to slots per destination: each inbound
  item first fills its own partial stacks, the rest needs ceil(rest / stackSize) slots. Different
  items share the box's free slots, so three hauls of three items cannot all count on the same last
  slot. The tail of a slot promised to X counts as room for more X; only other items lose it.
- A harvest toward a storage root checks the same destination room for its yield before it is
  offered, though it holds no inbound until its cut lands.

An option's quantity is the minimum of root, source, and destination availability and the
colonist's carry. Equivalent to ONI's remaining need = target - stored - outstanding in-flight.

### Remaining need

Remaining need is target minus what has landed at the destination, per item:

| Root | Target | Landed |
|---|---|---|
| Storage umbrella | to haul known stock: the item's configured max, or physical room when none is set; to stock by harvesting: the minimum of the rules naming the item (a category wildcard's minimum never drives cutting) | stored quantity of the item in the box |
| Craft | the current recipe's bill of materials | station store contents |
| Construction umbrella | the blueprint manifest | `delivered[]` |

Storage need is bounded per item and by the box's slots through the destination check above, so
per-item needs cannot sum past what the box holds. Either storage target is also capped by the box's
physical room for the item, and a loose haul now stops at the configured max too (it used to fill to
physical room), as does a deposit, which keeps the surplus carried. A packaged box (being moved) needs
nothing and is no pull source. A box keeps its umbrella, and so its root id, while it has rules, full
or not: a full box simply needs nothing, and goods on their way toward it, a suspended record's
included, keep counting toward the same root when room returns. One helper, `GoalTaskRegistry::remainingNeed(world, root, item, basis)`, reads the
target and what has landed from the world on every call (the station's next job and store, the
manifest, the box), so a deposit or a consumed input shows at once with no counter to keep in step;
its cost is the recipe's inputs, the manifest's entries, or the box's rules and slots, never a walk
of goals or colonists. The AI, the goal systems, the deposit leg, and the UI all use it.

The AI asks it per goal, per item, per deciding colonist, so it reads it through a per-update cache
(`RemainingNeedCache`): each (root, item, basis) is worked out the first time any colonist asks in
that update and read from the cache after, in reused storage that allocates nothing once warm. That is
safe because no deposit and no consumed input lands inside an AI update; ActionSystem and the goal
systems run outside it. Only the in-flight exclusion and the colonist's own goods are worked out per
decision. ActionSystem (the deposit leg's commit, a deposit) never reads the cache: other deposits
may have landed earlier in the same ActionSystem update, so it reads `remainingNeed` itself.

### Live source quantity (a deliberate compromise)

A colonist reads the live count of a pile or box it already knows about (a pile's `ResourceStack`,
a box's inventory), the way the haul path already reads destination inventories. Memory gates
whether the colonist knows the source exists; it does not store counts. This is a game compromise,
less realistic than colonists remembering what they last saw in a pile, chosen because remembered
counts cost memory and update traffic and the pickup check corrects any error anyway. It may be
revisited; it is not the intended end state.

A remembered pile the world cannot find (taken since it was seen) has no count to read. It is offered
at the wanted size while no other task claims it, so one colonist walks over and corrects the stale
memory on arrival, and Reserved once someone has.

Live counts need a keyed lookup. Today a pile is found by scanning every `ResourceStack` entity
(`findLoosePile`, `HarvestActions.cpp:36-51`), which would make each option O(piles). A world-side
map from WorldKey to pile entity, maintained where piles spawn and are removed, makes it one lookup.
It indexes world truth, not claims, and is not part of the claim index.

---

## Interruption and bumping

### Rank

A claim's rank is its task's current `priorityTier`, read at each index rebuild, so rank follows
tier changes: a routine Eat that turns critical mid-walk is re-tiered on the next `isSameTask`
evaluation and its claims gain critical rank with it. Lower tier number is higher rank; within a
tier the earlier `claimSeq` ranks higher. A suspended record ranks with the task that holds it
(its goods are possessed, so it is never bumped either way).

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

- Only a haul past its source (no Use or SourceQuantity left) or a cut carrying what earlier cuts
  of its gathering landed (its Inbound; its Use and the uncut part of its in-flight are not kept)
  holds goods of its own; a placement's
  packaged item has no root to carry it toward, so it leaves no record.
- If the colonist keeps the goods (stowed, or the need does not use hands), the interrupted work
  becomes the suspended record: its instance id, claim sequence, root goal, item, and claims
  rewritten to Inbound and In-flight for the goods held (nothing on sources not yet reached).
- If it drops the goods, the dropped pile is an ordinary new source with no claims, and no record is
  kept. Anyone can haul the pile, the original colonist included. No ownership.

Before each AI update's index is built, a record is trued up to the goods: capped at what the
colonist still holds of its item, and dropped when none is left (eaten, used, dropped) or its root
goal is gone, so a colonist seeing to a need never holds a goal's in-flight with goods it no longer
has.

On later selections:

- another need task keeps the record;
- the carry-in-from-possession option for the record's root, item, and destination, or a further
  cut for that root and item, adopts it: the new task takes the record's instance id, sequence, and
  claims;
- any other selection (other work, Wander, a player move) discards it, so a stale record never
  holds a goal's in-flight.

Sleep releases the hold. A record keeps its goods and can still be adopted when the colonist wakes,
but while the colonist sleeps (its task is the Sleep need) the record counts as no in-flight and no
inbound for anyone else, so another colonist is offered the fetch or cut those goods would have
covered. Sleep is long, and a goal should not wait out a night on goods lying in a pack. A record
kept by eating, drinking, or the toilet keeps counting, and the rule follows the task holding the
record, whatever created it: a record kept through a meal stops counting if the colonist then sleeps,
and counts again once it wakes. Awake, the carry-in adopts the record if the root still wants the
goods; if the other colonist's delivery covered the need, no carry-in is offered, the next selection
discards the record, and the goods stay carried until a carry-in to storage takes them.

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

A colonist whose route to its target has failed (`NavState::CantFindWayTo`) holds no claims while it
stays failed. A failure toward a target in another nav region is not remembered as a reach verdict,
so the colonist may re-select the same work every frame and `isSameTask` keeps it; without this rule
its pile, bush, slot, or box room would read as taken for as long as it stands there. The index leaves
such a task's current claims out (`ClaimIndex::addCurrent`, which the rebuild and every append share,
so the validator's fresh rebuild agrees), and `applyNavOutcome` retires them the moment a route fails
inside an update and appends them again when a later route for the same task succeeds. Another
colonist is offered the work meanwhile; if it takes it, the stranded colonist sees it taken at its next
decision and lets it go, and if nobody does, its claims return with its next successful route. Its
suspended record is not affected: those goods are possessed.

---

## The claim index

```
use:        target -> holder (instance id, colonist, tier, seq, task type)
source:     target -> [(instance id, item, quantity, tier, seq)], total per item
slot:       target -> [holder with slotIndex]; bindings included
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
earlier one just took. Every rewrite inside the update goes through the index, so the subset rule
needs no exception for a live instance: the switch path retires the abandoned task's instance (unless
it just became the suspended record), a new record replaces that instance's entries with its smaller
ones, an adopted record is appended under its own instance id after its old entries are retired
(never counted twice), and a discarded record or `enterControl` retires its instance. Tasks
cleared any other way later in the same update (the off-mesh recovery) leave their entries in the
appended index until the next rebuild; ActionSystem's rewrites and clears land between updates, so
the next rebuild reads them. That staleness only over-claims: another colonist may skip a free item
for one update, and nothing is double-booked.

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
the selected option, plus at most one Reserved row per (goal, item) for a better source someone
else holds.

Built so far: the Reserved row. Each (goal, item) shows at most one, the nearest source others' claims
take whole, and only when no source offered is nearer. The collapse of available options is not built:
the selection walks options in order and passes over unreachable ones, so collapsing to the nearest
before reachability is known would lose the fallback to the next source.

---

## Save and load

Claims are not saved separately. What is saved (when save lands) is the data they derive from: each
colonist's `Task` with its claim list, instance id, sequence number, tier, and suspended record; the
claim sequence counter (kept on `GoalTaskRegistry`, the colony state a save will hold); and each station's craft binding and job ids. On load the index is rebuilt
and the validator runs once in debug builds. There is nothing to purge, because there is no
separate store to disagree with.

---

## Player-facing output

The UI docs own the layout ([In-game screen](../design/ui/screens/in-game.md) tasks panel,
[Decision Trace](../design/game-systems/colonists/decision-trace.md)); this spec only supplies the
data.

- Task list availability per root and item: "N available (M in progress)". N is the unclaimed
  quantity of sources the colony knows about, capped at the root's remaining need; M is in-flight.
- Worker: the colonist whose current task serves the row, read once per panel refresh from each
  colonist's task, replacing the adapter's per-goal colonist scan. A cut or a haul names the goal it
  was picked through (a carry-in and a cut's deposit leg name their root); a build, craft, or
  placement names the entity it works, so the Build umbrella names its builder, a Craft row its
  crafter, and a clear row its clearer. Builds and clears hold no claim until builder slots exist,
  so claims alone could not name them, and a suspended record is not current work.
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
- No per-goal full-memory scans. Memory keeps each def's keys beside its capability index
  (`worldEntitiesOfDef`, one push per remember, one swap-remove per forget), so a goal visits only
  the entries of the defs that give its item: the item itself for a haul, the registry's
  `harvestablesYielding` defs for a harvest. The per-def gates (storage headroom, room to carry,
  the tool and the yield fit) are answered once per def before any entry is visited. Extend
  `MemoryCapacityDoesNotAffectDecisionSpeed` with 10,000 remembered entities that match no goal:
  decision time stays within noise of an empty memory.
- Live pile quantity is one keyed lookup through the WorldKey-to-pile map, not a scan of piles.
- Box-pull gating keeps the colony-wide rule without its old cost (a walk of every colonist's
  memory per box per goal per evaluating colonist, quadratic in colonists). `ColonyStorageKnowledge`
  is a memo: a box's answer is worked out the first time a pull check asks about it in a pass, one
  probe of each colonist's memory, and later checks in that pass read it; a box nobody asks about
  (holding none of the item, or not strictly lower priority) costs nothing. `AIDecisionSystem` keeps
  one for every colonist it decides in an update, and invalidates it at the start of each update
  rather than per goal-system poll, so the answer is never staler than the memories it reads. The
  storage dialog's model keeps another for its known-source affordance, invalidated per refresh and
  shared by every rule it checks. Warm, neither memo allocates.
- A storage box's room for an item is measured only when it is used: after the amount the box still
  wants is known to be more than zero, and, for a pull, once a source box qualifies. What the box
  itself offers (an item's partial-stack headroom and stack size, looked up by id, and the box's free
  slots) does not depend on who asks and is read from the world once per (box, item) per AI update
  (`BoxRoomCache`, stamped with the update, allocation-free when warm); each colonist takes its own
  claim exclusion out of the index on top.
- Trace size per colonist is bounded by roots times accepted items after dedup, not by remembered
  sources.
- The UI's availability and `knownBy` run on the panel throttle, never per frame, and also use the
  capability index.

These evaluator changes land before claim lookups are added to the evaluators, so the added lookups
are measured against an evaluator that already scales with goals, not memory.

---

## Testing

The multi-colonist regression vehicle is the headless sim-cli ([Sim CLI](./sim-cli.md), WOR-603),
built before this work. Its scenarios for this spec are `.sim` files under
`apps/sim-cli/scenarios/`, each registered as a ctest entry (label `scenario`) with its determinism
rerun ([scenario format](./sim-cli.md#scenarios)):

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

The debug validator runs at the end of every AI update that rebuilt the index, in development builds
(`DEVELOPMENT_BUILD`), so every test that runs the AI runs it, and aborts on a violation. It
asserts: a fresh rebuild is a subset of the appended index, and every surplus entry belongs to an
instance id that no live task or suspended record holds (a task cleared this update); entries compare
by holder instance, kind, target, item, and slot with quantities summed, not by tier, which
`isSameTask` may change within the update; no target is over-subscribed except by
claims flagged bumped (checked once bumping lands: until then a hungry colonist's Use legitimately
shares a bush with a work claim); every Inbound has a matching In-flight on the same task; every binding names a
live crafter bound nowhere else and a queued job id; no claim belongs to an inactive task. In-flight
is not checked against remaining need: the need can drop under goods already on their way by means
no claim controls (a player lowers a box's max, a dev give fills a site), and the deposit then keeps
the surplus, so the check would abort on legitimate play. Its working storage lives on the scratch
index, so a warm check allocates nothing.

---

## Migration

Deleted in the same change that replaces them, per the no-legacy rule:

- `GoalStatus::InProgress` and `GoalStatus::Complete` (never assigned). "In progress" is in-flight
  from the index.
- `GoalOwner::None`. Every creator already sets an owner; the default goes, and the registry's
  owner-index guards for None go with it.
- `reservationTimeout`: `TimingConfig::reservationTimeout`, `getReservationTimeout()`, its parse in
  `PriorityConfig.cpp`, the element in `priority-tuning.xml`. It is read by nothing today.
- Done (WOR-46 step 6): the in-flight approximations claims replace, all at once so nothing subtracts
  carried goods twice:
  `ConstructionSystem::carriedAmount` and `constructionHarvestDemand`; `CraftingGoalSystem`'s
  `colonyCarriesStock`; the craft-fetch gate `deliveredAmount + carried` in `evaluateHaulOptions`
  (`AIDecisionSystem.cpp:535-537`); the craft pickup sizing from `deliveredAmount + carried` in
  `HaulActions.cpp` (about lines 413-424).
- Done (step 6): `createHaulForCompletedHarvest`, the chainId-tagged stocking carry-in Haul, and with it
  `GoalTask::chainId` (and later `Task::chainId`, whose value nothing read, with the registry's chain id
  counter), construction's
  per-material inventory-source Haul goals, and the three inventory-source haul paths in
  `evaluateHaulOptions`, replaced by the in-place deposit leg and carry-in from possession.
- Done (step 6): goal counters (`targetAmount`/`deliveredAmount` on every goal, roots included), the storage
  slot-count use of `targetAmount` and its owner special case in `evaluateHaulOptions`, and
  `GoalTask::availableCapacity()`, replaced by remaining need at the root. `recordDelivery`'s
  Haul-only parent credit goes with them.
- The per-call colony memory walk inside `colonyKnowsStorageEntity`, replaced by the per-update
  `ColonyStorageKnowledge` (the colony-wide rule itself stays). Done.
- The adapter's per-goal colonist scan (`colonistWorkingGoal`), in favor of one read of the current
  tasks per refresh. Done.
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
