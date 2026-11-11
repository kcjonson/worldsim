# Task claims

Status: Partly built (integration/colonist-survival). [What is built](#what-is-built) draws the line;
everything not built is marked Planned where it appears.
Created: 2026-11-09
Epic: WOR-46 (goal-driven task generation), reservation phase

How colonists avoid working against each other. Goals generate a few hundred goal-level tasks
([Task Registry](../design/game-systems/colonists/task-registry.md),
[Task Generation Architecture](./task-generation-architecture.md)), and each colonist pulls work
by scoring options against its own Memory
([Colonist Task Arbitration](./colonist-task-arbitration.md)). Without claims nothing stops two
colonists from choosing the same tree, both fetching the last unit a recipe needs, or both cutting
toward one goal until it is over-harvested. Claims record what a colonist has committed to, on its
task, visible to every other colonist while it decides.

**MVP Status:** See [MVP Scope](../design/mvp-scope.md). Core plumbing for multi-colonist play;
no new player-facing feature beyond clearer task status.

---

## What is built

Built:

- Claims on tasks (`Task::claims`, `instanceId`, `claimSeq`, `SuspendedWork`), the claim index
  (`ClaimIndex`), and its development-build validator.
- Chain claims for every chain shape that runs: harvest with carry-in, fetch from stock (pile and box),
  carry-in from possession, placement, the Craft slot, build-site clears, and need tasks.
- Remaining need at the root (`GoalTaskRegistry::remainingNeed`), read from the world, with its
  per-update cache (`RemainingNeedCache`).
- Carry-in from possession, and the harvest's in-place deposit leg (`ActionSystem::carryHarvestIn`).
- Batching: several cuts or pickups per trip before the carry-in.
- Provisions: a colonist's own food is never cargo.
- Suspended records across a need, with the sleep release.
- The failed-route rule.
- The evaluators reading claims, with `Reserved` and `NoRoom` rows in the decision trace.
- Hunger from the pack, bushes, loose piles, and storage boxes, with eater urgency and making room
  to eat.

Not built:

- Bumping of any kind. The one thing that exists in its place: a more urgent eater does not count a
  less urgent eater's food claim as an eater's, so it treats that food as held (used last) rather than
  taken.
- The craft binding and job ids. A crafter holds its station only through the Slot claim its Craft
  task takes at selection, which goes with the Craft task when the crafter leaves to eat or sleep.
- Player-facing output beyond naming a row's worker from that colonist's current task (and the trace's
  `Reserved` rows).
- The collapse of options to the best source per goal and item before scoring.
- sim-cli ([Sim CLI](./sim-cli.md)) and its scenarios.
- The performance benchmarks under [Performance requirements](#performance-requirements).

---

## Model

A claim is data on the task that took it. When a colonist accepts work, its task records a claim
list for the whole planned chain: the source and quantity it will take, the station slot it will use,
the quantity it will deliver to a destination, the quantity it is bringing toward a goal. As
steps complete the task rewrites its own list (the source claim drops after pickup, the inbound
claim stays until deposit). When the task is cleared or destroyed its claims are gone, because
they were never anywhere else. There are no release calls and no timeouts.

A claim index (`ClaimIndex`) is derived from task data. It is a cache for lookups, is never saved,
and a development-build validator checks it against the tasks.

Assignment stays agent-pull. Colonists are autonomous and directed indirectly
([Game Philosophy](../design/game-philosophy.md)); there is no dispatcher. A colonist reads claims
while it scores options and never receives work from them.

Why not a stored reservation table: RimWorld stores reservations owned by (pawn, job) with a
stack count and a max-pawns limit, and still ships a periodic verify sweep plus a purge on load
because reservations leak when a job ends by a path that forgot to release. Oxygen Not Included
keeps item reservations as quantities on the item, unserialized and scoped to entering and
leaving a chore state, which is the shape adopted here. None of RimWorld, ONI, Dwarf Fortress, or
Factorio uses timeouts; a timeout releases work that is still legitimately in progress and hides
the leak instead of removing it. In worldsim's code a task ends at many clear sites (navigation
recovery, player control, action failures, arbitration switches); data on the task makes every one of
them correct without touching it.

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
claims; they are what in-flight claims count toward. Claims are owned by the colonist's `Task`
and by its suspended work record (see interruption below), each under its own task instance id.
Planned: a craft binding on a station's work order would be a second owner (see
[Craft binding](#craft-binding-planned)).

Every claim is keyed by the task instance that holds it, never by the colonist. RimWorld's later
"enroute" store for inbound counts is keyed by pawn, so ending one job drops the pawn's inbound
claims for a different job it still has queued; keying by task instance makes a colonist's current
and suspended work independent.

### Query order

A colonist deciding about a target applies, in order:

1. Discovery: the target is in its own Memory ([Memory](../design/game-systems/colonists/memory.md)).
   A colonist never targets something it has not seen. The one exception is storage the colony
   has placed, as a source for box-to-box transfer and as food for a hungry colonist: it is common
   colony knowledge, gated by `ColonyStorageKnowledge` (any colonist remembers the box), and any
   colonist may use it. Boxes are things the colony built, and transfer, driven by the item's
   priority in each box, should run reliably rather than wait for a colonist who personally saw the
   source box. The exception covers colony-placed storage only. Its cost is handled by the per-update
   memo under performance requirements.
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

The Memory key is what the harvest path stores in `Task::harvestTargetEntityId`, so claims on
remembered things use the key every colonist computes for the same object. A pile is both a WorldKey
(how colonists know it) and an `EntityID` (its `ResourceStack`); claims use the WorldKey, and
`LoosePileIndex` (below) resolves it to the pile entity.

---

## Claim kinds

| Kind | Counts | Target | Lifetime |
|---|---|---|---|
| Use | exclusive use, quantity 1 | WorldKey or Entity | until the work on it ends (harvest done, item picked up) |
| Source quantity | units to be taken from a source | pile (WorldKey), box (Entity) | until pickup or withdraw |
| Slot | one slot, optionally a numbered one | station today; planned for bed, toilet, interaction spot, builder slot | while the task that took it lives |
| Inbound | units to be deposited at a destination | box, station store, blueprint manifest | until deposit, or until the goods are dropped |
| In-flight | units on their way toward a root goal | Goal (with item) | until deposit, or until the goods are dropped |

The data on the task (`Task.h`):

```cpp
enum class ClaimKind : uint8_t { Use, SourceQuantity, Slot, Inbound, InFlight };
enum class TargetKind : uint8_t { WorldKey, Entity, Goal };

class Claim {                   // built only through per-kind factories, which fix the target kind
    uint64_t   targetId;
    uint32_t   item;            // 0 for Use and Slot
    uint32_t   count;           // 1 for Use and Slot
    uint16_t   slotNumber;      // numbered slots (builder positions); kAnySlot otherwise
    ClaimKind  claimKind;
    TargetKind space;
};

struct SuspendedWork {          // one interrupted work task, kept across a need
    uint64_t instanceId;        // the interrupted task's id, kept so its claims stay its own
    uint64_t claimSeq;
    uint64_t rootGoalId;
    uint32_t itemDefNameId;
    BasicClaimList<2> claims;   // that item's inbound and in-flight, for the goods still held
};

// On Task:
uint64_t instanceId;            // assigned at acceptance, never reused
uint64_t claimSeq;              // acceptance order, for deterministic tie-breaks
ClaimList claims;               // BasicClaimList<4>; a chain holds at most three
std::optional<SuspendedWork> suspended;
```

`BasicClaimList` is a fixed-capacity array with a count. Overfilling one is a programming error and
aborts in development builds. Both ids come from one counter on `GoalTaskRegistry`
(`allocateClaimSeq`): at acceptance a task takes the next number as its instance id and its sequence
alike; a task that takes over cargo or adopts a record keeps that record's older ones.

The factories fix the key space (Use and SourceQuantity name theirs, Slot and Inbound are Entity,
InFlight is Goal), so no claim can be filed where its queries never look. A target's `kAnySlot` is
its one unnumbered slot (a station) and conflicts with every slot on that target, numbered or not;
two numbered slots conflict only with themselves.

Inbound and in-flight look redundant for a haul but guard different limits. Inbound protects a box's
physical room (two hauls into one box must fit in it together). In-flight protects the goal's need,
including harvests that have not produced goods yet. A haul carries both; a harvest carries Inbound
only for the goods earlier cuts of its gathering already put in the colonist's hands. Inbound at a
station store or a blueprint is recorded but not read; the root's in-flight already bounds them.

### Built now and planned

Built, each by a chain that runs today:

- Use on harvest sources (trees, bushes) and on packaged items being placed.
- Source quantity on loose piles and on storage boxes drained by storage-to-storage pulls.
- Inbound at storage boxes, crafting station stores, and blueprint manifests.
- In-flight toward storage, craft, and construction root goals.
- The craft station slot, from Craft selection, held while the Craft task lives.
- Need tasks claiming the food they walk to (Use on a bush, source quantity of one meal on a pile or
  box), so two hungry colonists do not converge on the last berries.

Planned, none of which needs a new kind: bed, toilet, and medical slots (with ownership),
Chain_Rescue's patient and medical bed, mobile targets (hunting and taming, an Entity key with a
last-known position from Memory's dynamic entities), builder slot counts and positions per blueprint
(the count from `constraints.xml` `builderCapBase` and `builderCapPerSquareMeter`, the position as
`slotIndex` into the construction work slots), slots on deconstruction, and colonist death and
incapacitation, which do not exist in `libs/` yet.

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
landed cut) takes one handoff, `ActionSystem::headToDeposit`. So:

- There is no separate carry-in goal after a harvest, and construction has no per-material
  inventory-source Haul goals.
- Goals carry no `targetAmount`/`deliveredAmount`; the root's need and in-flight decide whether
  another cut is wanted.
- Craft fetch Haul children stay (they open fetch-from-stock), with no counters.
- One carry-in-from-possession option (`evaluateCarryInOptions`) serves every root: for any root goal
  still wanting an item the colonist carries, offer to deliver it. This covers plain storage, and goods
  left over after a cut, an interruption, or a switch. Tools are equipment and are never offered, and
  the colonist's provisions (below) are offered to no root. The option is sized to the smaller of the
  cargo and the root's available need (remaining need net of other tasks' in-flight), and for a
  storage root also to the box's room for the item net of others' inbound.

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

What each chain records at acceptance (`recordChainClaims`) and when each claim drops. Q is the
quantity the option was sized to.

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
item, unless the item is food, which is then provisions), capped at what it holds and at the root's
available need (remaining need net of other tasks' in-flight). E is the source's most per cut
(`amountMax`; a pooled source gives at most that per cut too), capped by what still fits in the
colonist's carry and by the root's available need less C. Over-estimating only makes another
colonist briefly see less available; under-estimating would bring back over-harvest. Units landed
beyond the cap are carried uncommitted. A cut gives its whole yield: what the colonist can't carry
drops beside the source as a loose pile, a one-hand yield like a fell's overflow, an ordinary new
source with no claims. The cap reads the claim index the AI last built, and the leg never commits more
than its own in-flight there: another colonist converting a cut in the same ActionSystem update read
that figure, so the two together never pass the need. The need itself is read from the world at that
moment.

#### Batching

A colonist gathering for a root delivers when one of these holds:

- its trip is full for the item: the room left is too little for another cut (below), or none (the
  cut shows NoRoom);
- what it carries covers the root's remaining need net of others' in-flight;
- it knows no other source of the item that is ready, reachable, and not reserved.

Until then the next leg for the same root and item outranks the carry-in. Within a tier the order
(`EvaluatedOption::gatheringRank`, compared after tier and before score) is: the carry-in the colonist
has already chosen, then a known loose pile of the item toward the root it gathers for, then a further
cut toward it, then every other option, the carry-in it has not chosen yet included, then last a fresh
cut for a root and item that a known pile also serves. A pile counts as a source here too: he picks up
what he or anyone dropped before cutting again. A pile is offered for every root that wants the item,
construction sites included; when one is offered, a fresh cut for the same root and item ranks below
everything else in its tier, so even at the start of a trip the pile is taken up first, however far
off it is and however near the plant. A pickup that continues a gathering lifts what still fits and is
decided again at once, like a landed cut.

A top-up cut (one toward a root the colonist already carries goods for) is offered only when the room
left takes at least the smaller of the source's largest yield (`amountMax`) and what the root still
wants beyond what is carried; otherwise he delivers. So a trip of ten taking cuts of three is walked at
nine rather than spending a fourth plant on one unit, and no tree is felled for a sliver of its wood.
A cut can still land more than fits (the first cut of a trip into a small pack), and the surplus drops
as a pile. A source that is regrowing, unreachable, or reserved is passed over at selection, so the
carry-in is taken as soon as no source is left.

Once he has chosen to deliver, he delivers. A delivery leg that a landed cut or pickup has just
become is open: the decision that follows weighs it against the next leg. Once a decision keeps or
selects a Haul past its pickup (`Task::deliveryChosen`), the colonist is no longer gathering for that
root: no cut or pile toward it ranks as a leg of the trip, and the chosen carry-in ranks above every
other option in its tier, so a source that regrows or is released on the walk in, or on the arrival
frame before the deposit starts, does not turn him back. This holds for any delivery past its pickup,
plain pile-to-storage hauls included: once kept, a tier-6 haul to a box outranks every other tier-6
option until it is deposited. A build-site clear brings nothing toward a need and is never a leg of a
gathering. While it gathers, the colonist's in-flight toward the root covers what it carries plus the
cut it has accepted, so another colonist does not cut what those cover, and the cargo moves from task
to task under one instance id with no window in which it is uncommitted. A need that interrupts the
gathering keeps the goods as a suspended record (below), which the next cut or the carry-in adopts.

### Fetch from stock (Chain_PickupDeposit)

Loose pile to storage, loose pile to a crafting station store (craft fetch), loose pile to a build
site, and box to box (storage-to-storage pull, source is a box Entity).

| Step | Claims held |
|---|---|
| Accept | SourceQuantity(source, item, Q), Inbound(destination, item, Q), In-flight(root, item, Q) |
| Pickup or withdraw | The pickup asks for Q (its SourceQuantity; a pile pickup that took a gathering over delivers its cargo with it). SourceQuantity drops. Inbound and In-flight become the units lifted, capped at Q. A pickup that lifts nothing ends the haul at the source. Arriving at the source or the destination already carrying the item skips the pickup and rewrites the same way, to what is carried |
| Deposit | all drop with the task |

Q for a pile is the smallest of the root's available need less the colonist's cargo for it, one
trip's carry of the item, and what the pile has left once others' source claims are taken out; a
pile pickup that continues a gathering is also capped at what still fits. Q for a box-to-box pull is
the smallest of the source box's drainable quantity (stock above its own minimum) less others' source
claims, the destination's want and room, and one trip's carry; the pull is offered only while a pickup
would lift at least one unit now, and the withdraw takes what actually fits, so Q is corrected there.
Two pulls cannot drain a box below its minimum together.

### Carry-in from possession

The colonist already carries the goods. Accept records Inbound(destination, item, n) and
In-flight(root, item, n), n sized as above; deposit ends the task. Metered deposits keep the rules
from [Crafting & Material Provisioning](./crafting-provisioning-architecture.md): the colonist
deposits exactly what is needed and keeps the rest, which holds no claim.

A two-hand armful kept this way, or left when its root is gone, fills the colonist's carry, so every
haul and cut reads no room. A colonist about to wander while holding a two-hand armful that no root it
knows wants (no carry-in of it is offered) puts it down where it stands through the drop path, which
merges it into a pile at the spot (`AIDecisionSystem::putDownUnwantedArmful`); the pile is an ordinary
source with no claims, hauled once a root wants it. One-hand surplus stays in the pack. A carried
packaged item is never put down as a pile (Chain_PlaceItem, below).

### Chain_PlaceItem

Accept records Use(packaged entity), unless the colonist already carries it. Pickup drops it (the item
is now possessed). The placement target is fixed on the packaged item, so no slot is needed.

A carrier whose decision picks anything but placing the item it carries, or a need whose action leaves
the hands free (sleep), sets the item down where it stands, still packaged and no longer carried
(`AIDecisionSystem::setDownUnplacedFurniture`): its placement was cancelled, its target became
unreachable, or other work outranks it. Once a placement is offered again, a colonist picks it up from
there. The pickup is offered only while the placement target is reachable, so a carrier with no way to
the target does not pick the item up again only to set it down.

### Craft (Chain_CraftWithGather)

Provisioning (harvest with carry-in, fetch from stock, carry-in from possession) takes no station
claim; any number of colonists may provision one order in parallel through in-flight claims. The
colonist that selects the Craft step itself records Slot(station) at selection, so two crafters do
not walk to one station: a second colonist sees the Craft option as Reserved, "station in use by
Alice". The slot lives as long as the Craft task: if the crafter switches to a need before the craft
starts, the slot goes with the task and the station is free to anyone. (The Craft action itself is
not interruptable once it starts.) Planned: the craft binding (below) keeps the station for its
crafter across that interruption. The optional output deposit is a carry-in from possession. The
chain's `Gather` step has no runtime path; the claims follow what the code runs.

### Chain_Rescue (planned)

Use(patient), Slot(medical bed). Pickup drops the Use; the bed slot stays until deposit.

### Build-site clear

A construction clear Harvest records Use(source) only: it removes what stands in the footprint and
brings nothing toward a need, so it has no in-flight.

### Build and Deconstruct

No claims; any number of colonists may work an UnderConstruction blueprint. Planned: builder slots
(count, then `slotIndex` positions) come with the builder-cap work.

### Need tasks

Eat records Use on a bush or SourceQuantity of one meal on a pile or box; food already in the pack is
possessed and takes none, and the gather-food option claims its bush like Eat. Drink takes no claim
from unbounded water. Planned: sleep and toilet slots come with beds and toilets.

A hungry colonist with no food in its pack searches, nearest reachable first in one search: a ready
harvestable whose yield is edible, a loose pile of an edible item it remembers, and a storage box the
colony knows (the `ColonyStorageKnowledge` rule the box-to-box pull uses) that holds an edible item.
Food already in the pack is eaten first with no search. A box's own minimum does not keep its food
from a hungry colonist; stored food is the buffer between harvests. One meal is what takes the need
back to its seek threshold at the item's nutrition, at least one unit (with Berry at 0.3 nutrition and
a seek threshold of 50%, one berry above 20% hunger, two below), capped by what a pile or box has left
for this colonist; what the pack holds does not size it. It is sized again whenever the colonist
re-decides and keeps the task, and its claim on a pile or box follows. A hunger task does not re-decide
just because its need turned critical (that trigger is skipped while the colonist already serves a
need); the periodic re-evaluation picks it up. Between two foods in one box at the same distance, the
lower item id is taken first. A bush is cut whole, because a cut gives what it gives; a pile or box
gives the meal and no more, because stored food is the colony's and anything taken past the meal
would become one colonist's provisions, out of everyone else's reach. The pickup or withdraw ends the
task and the next decision eats from the pack, the way a harvest does.

A full pack never stops a colonist eating, whatever kind the source is. No room is checked when the
food is chosen; the take itself (the cut of a bush, the pickup from a pile, the withdraw from a box)
starts by making room, at the source, through one rule (`ActionSystem::makeRoomToEat`). Without it a
cut into a pack with no room takes nothing, the bush stays ready, and the colonist picks it again
forever. Making room frees what limits the take and no more than the meal needs, and never costs a
tool (equipment) or food:

- a slot (the pack has no stack room for the meal): the pack's last stack that is neither a tool nor
  food goes whole; if only tools hold the slots, one moves to a free belt slot or a free hand, which
  keeps it;
- weight: just enough units of such goods to carry the meal (or what the slots let in), from the pack's
  last stack first, then the hands; tools weigh nothing as cargo and are never touched;
- goods under no claim go before cargo a suspended record carries, which goes only when nothing else
  can.

What goes is put in a box within reach (1.5 m, with no wall between) that takes it, else dropped as a
pile where the colonist stands (`storeOrDrop`); a record that loses goods this way shrinks to what is
left before the next index rebuild. A bush's cut still lands whole: the room made covers the meal, and
the rest of a cut that doesn't fit drops beside the bush as a pile. If every pack slot holds a tool and
the belt and hands are full too, no room can be made: the take lifts nothing and he picks the source
again. That is a known limit: it takes a tool in every pack slot, belt slot, and hand (fourteen on a
colonist today). Gathering food into the pack is idle work, not a meal: it makes no room, so it is
offered only while the pack holds no food at all and has room for one unit of the yield (with no room
it shows NoRoom).

What a pile or box has left for a hungry colonist is its live count less the shares of eaters at
least as urgent. Work claims on food (a haul of berries to a box, a pull between boxes, a craft fetch)
never hold it: the hungry colonist takes it, and a hauler that arrives after finds less or nothing,
and its empty pickup ends the haul (or, re-deciding first, it sees the pile taken and lets it go).
Such a held source still comes after a free one, as for bushes.

The regrowing-food wait (`SourceReachMemo::FoodWait`) covers harvestables only: it is set when the
search finds nothing and every edible harvestable the colonist knows is regrowing, and while it holds
those are not searched again, until the first is ready or the colonist remembers or forgets a world
entity. Piles and boxes are searched at every decision of a hungry colonist, so a box that gains
berries, or a pile that appears, is found at once. The boxes holding food are worked out once per AI
update and shared by every hungry colonist, and again when a colonist puts goods away in a box inside
the update.

A hungry colonist reads a bush, pile, or box share as an eater's only when the holder is a hunger
task (not the idle gather) at least as urgent: a tier no higher a number than its own (lower tier
wins, equal tiers keep first-come). Such food is taken: the search passes it over, and if nothing else
is found the Hunger option reads Reserved, naming the holder. Any other holder (a less urgent eater, a
work claim such as a cut toward a goal, or the idle gather-food option) only holds the food: it is
still the colonist's to take, but a source nobody holds is preferred when one is reachable and ready,
so the search passes over a held source and comes back to the nearest held one only when nothing free
is left. Both colonists then hold it; whoever cuts first gets the yield. The gather-food option is
idle work and gives way to any holder, so it takes the nearest free bush.

The less urgent eater is not flagged. At its next decision it sees the source held by an eater at
least as urgent and looks elsewhere; if it reaches the bush first it eats, and if it arrives second
the bush pays nothing (gone or regrowing), its task ends, and its next decision passes the regrowing
bush over like any other, so it never loops on it. Colonists can starve only if they never find food:
food reads as taken only when another colonist at least as hungry is about to eat it, for one meal or
one harvest.

---

## Availability

Every count is in items, except a box's slots. Self-exclusion applies to all of it: when a colonist
evaluates, claims whose instance id is its own current task or its suspended record are left out
(`ClaimExclusion`), so the work it is doing does not read as taken. Without it, every re-evaluation
would see its own target as claimed and its own in-flight eating the remaining need, the option it is
executing would vanish, `isSameTask` would fail, and the switch path would clear the task.

- Root available (item) = remaining need (item) - in-flight (root, item) (`rootAvailable`).
- What a colonist would still go and get (fetch or cut) also nets out what it carries of the item
  toward that root (`RootSupply::toAcquire`): its own goods are already on their way. Whether it cuts
  more before delivering them is the batching rule above.
- Source available = live quantity - claimed quantity, for source quantity claims; a Use target is
  available when no other task holds it. Use is exclusive for every cut, clears included: several
  colonists may clear one site, each its own plant.
- Destination available, station store or blueprint: the store is sized to the bill of materials or
  manifest, so root available is the capacity check.
- Destination available, storage box (item X) = room in X's own stacks (its existing partial stacks
  plus the slots its own inbound already opens, less inbound X), plus (free slots - slots promised to
  all inbound) x stackSize(X). Inbound at a box is converted to slots per destination: each inbound
  item first fills its own partial stacks, the rest needs ceil(rest / stackSize) slots. Different
  items share the box's free slots, so three hauls of three items cannot all count on the same last
  slot. The tail of a slot promised to X counts as room for more X; only other items lose it.
- A harvest toward a storage root is offered only while the box has room for at least one unit of the
  yield net of others' inbound, though it holds no inbound until its cut lands.

An option's quantity is the minimum of root, source, and destination availability and the
colonist's carry. Equivalent to ONI's remaining need = target - stored - outstanding in-flight.

### Remaining need

Remaining need is target minus what has landed at the destination, per item:

| Root | Target | Landed |
|---|---|---|
| Storage umbrella | to haul known stock: the item's configured max, or physical room when none is set; to stock by harvesting: the sum of the minimums of every rule naming the item (a category wildcard's minimum never drives cutting) | stored quantity of the item in the box |
| Craft | the station's next job's bill of materials | station store contents |
| Construction umbrella | the blueprint manifest, for materials that are due | `delivered[]` |

Storage need is bounded per item and by the box's slots through the destination check above, so
per-item needs cannot sum past what the box holds. Either storage target is also capped by the box's
physical room for the item, and a loose haul stops at the configured max, as does a deposit, which
keeps the surplus carried. The configured max is summed over every rule that matches the item, and
any matching rule with no max (a category wildcard with max 0, which every placed box starts with)
makes the item unlimited; the storage dialog removes a category's wildcard when a named rule for that
category is added. A packaged box (being moved) needs nothing and is no pull source. A box keeps its
umbrella, and so its root id, while it has rules, full or not: a full box simply needs nothing, and
goods on their way toward it, a suspended record's included, keep counting toward the same root when
room returns. One helper, `GoalTaskRegistry::remainingNeed(world, root, item, basis)`, reads the
target and what has landed from the world on every call (the station's next job and store, the
manifest, the box), so a deposit or a consumed input shows at once with no counter to keep in step;
its cost is the recipe's inputs, the manifest's entries, or the box's rules and slots, never a walk
of goals or colonists. The AI, the goal systems, the deposit leg, and the task panel all use it.

The AI asks it per goal, per item, per deciding colonist, so it reads it through a per-update cache
(`RemainingNeedCache`): each (root, item, basis) is worked out the first time any colonist asks in
that update and read from the cache after, in reused storage that allocates nothing once warm. Deposits
and consumed inputs land in ActionSystem and the goal systems, outside it. The one change inside an AI
update is a colonist dropping a chain for a need, which puts its goods away in a box within reach
(`storeOrDrop`); the AI then works out again what it cached for that box (`AIDecisionSystem::boxChanged`:
its root's needs, its room, and the boxes holding food), so later deciders and the colonist's own
claims read the box as it is. Only the in-flight exclusion and the colonist's own goods are worked out
per decision. ActionSystem (the deposit leg's commit, a deposit) never reads the cache: other deposits
may have landed earlier in the same ActionSystem update, so it reads `remainingNeed` itself.

### Live source quantity (a deliberate compromise)

A colonist reads the live count of a pile or box it already knows about (a pile's `ResourceStack`,
a box's inventory), the way the haul path reads destination inventories. Memory gates whether the
colonist knows the source exists; it does not store counts. This is a game compromise, less realistic
than colonists remembering what they last saw in a pile, chosen because remembered counts cost memory
and update traffic and the pickup check corrects any error anyway. It may be revisited; it is not the
intended end state.

A remembered pile the world cannot find (taken since it was seen) has no count to read. It is offered
at the wanted size while no other task claims it, so one colonist walks over and corrects the stale
memory on arrival, and Reserved once someone has.

Piles are found by key: `LoosePileIndex` maps a WorldKey to the pile entity, maintained where piles
spawn and are removed (`makeLoosePile`, `destroyWorldEntity`), so a live count is one lookup. It
indexes world truth, not claims, and is not part of the claim index. ActionSystem owns the one
instance and the AI reads it.

---

## Interruption

### Interruption by the colonist's own needs

The colonist stows or drops carried goods (`AIDecisionSystem::handleChainInterruption`, driven by
`needsHands` and `handsRequired`; see [Task Chains](../design/game-systems/colonists/task-chains.md)).
A one-hand item goes to the belt, else the pack; a two-hand armful, or one-hand goods with no belt or
pack room, go through `storeOrDrop`: into a box within reach that takes them, else onto the ground as a
pile. A suspended record is created only when the interrupting selection is a need task:

- Only a haul past its source (no Use or SourceQuantity left) or a cut carrying what earlier cuts
  of its gathering landed (its Inbound; its Use and the uncut part of its in-flight are not kept)
  holds goods of its own; a placement's packaged item has no root to carry it toward, so it leaves no
  record.
- If the colonist keeps the goods (stowed, or the need does not use hands), the interrupted work
  becomes the suspended record: its instance id, claim sequence, root goal, item, and claims
  rewritten to Inbound and In-flight for the goods held (nothing on sources not yet reached).
- If it puts the goods away or drops them, no record is kept. A dropped pile is an ordinary new source
  with no claims; anyone can haul it, the original colonist included. No ownership.

Before each AI update's index is built, a record is trued up to the goods (`refreshSuspended`): capped
at what the colonist still holds of its item, and dropped when none is left (eaten, used, dropped) or
its root goal is gone, so a colonist seeing to a need never holds a goal's in-flight with goods it no
longer has.

On later selections:

- another need task keeps the record;
- the carry-in-from-possession option for the record's root, item, and destination, or a further
  cut for that root and item, adopts it: the new task takes the record's instance id, sequence, and
  claims (a build-site clear is no cut toward a need and never adopts one, even of the same site);
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

`Task::clear()` leaves the suspended record alone: the switch path in `AIDecisionSystem::update` calls
`handleChainInterruption` and then `task.clear()`, and the record must survive that. Discarding it is a
separate, explicit operation (`Task::discardSuspended`), called by the selection rules above (in
`AIDecisionSystem::acceptClaims`) and by `enterControl`; a destroyed colonist takes its Task with it.

Two existing behaviors to keep in mind. Craft, harvest, pickup, withdraw, and deposit actions are not
interruptable (`Action.h`), and the switch path's non-interruptable gate holds regardless of tier, so
a critical need waits for the current action to finish. And the switch path leaves an interruptable
action (sleep) active when it clears the task, while `ActionSystem::update` starts a new action only
when none is active; see Known gaps.

### Death and direct player control

Direct player control returns half-done work to the pool at once: `enterControl` clears the task and
Action, retires both instance ids from the index, and discards the suspended record. Carried goods stay
with the colonist. Death will do the same through entity destruction (no claims survive the task);
spawning the dead colonist's carried goods as piles is planned. Player orders that target a claimed
thing (tier 3, a separate epic) take nothing from anyone today; see bumping, planned.

### Bumping (planned)

Nothing in this section is built. A task's claims are never taken by another colonist; the only
related behaviour is the food rule under [Need tasks](#need-tasks), where a more urgent eater treats a
less urgent eater's claim as held rather than taken.

**Rank.** A claim's rank is its task's current `priorityTier`, read at each index rebuild, so rank
follows tier changes. Lower tier number is higher rank; within a tier the earlier `claimSeq` ranks
higher (`outranks`, which the index already uses to name the top holder). A suspended record ranks
with the task that holds it (its goods are possessed, so it would never be bumped either way).

**Rules.**

- Needs would beat work claims. A colonist acting on a critical need could take from any work claim:
  the last berries claimed for a storage haul would go to the starving colonist. (For food this already
  holds without bumping: work claims never hold food against a hungry colonist.)
- Only a selection at tier 1 to 3 (survival, critical needs, player orders) could bump, and only a
  holder whose tier number is strictly higher. Equal tiers would never bump. Ordinary work (tiers 4
  to 7) would never bump anything, so there would be no better-placed-worker preemption.
- A critical need (tier 2) could bump a player order (tier 3), following the arbitration ladder.
  Decided by the owner.
- A claim would stop being bumpable once its holder's action on that target has started. Those
  actions are not interruptable, and a bumper would treat the target as unavailable.
- Possessed goods would never be bumped.

**Mechanism.** A bumping colonist would need a target's claimant list, which the index does not keep
(it answers only totals and the top holder). When its need exceeds what is available (or the target
is under a Use claim), it would select anyway, record its own claim, and mark the lowest-rank, latest
claims that cover the shortfall as bumped on those colonists' tasks (a flag naming the bumper): the
RimWorld player-forced-job pattern. Until the victim re-plans, the target would be over-subscribed by
exactly the flagged claims. The bumped colonist would check the flag at its next AI update and at
every action start, abandon the task past the switch path's tier and non-interruptable gates, clear
its Action, NavPath, and MovementTarget as `enterControl` does, keep what it carries, and re-plan.

### Craft binding (planned)

Not built; no `CraftBinding`, and `CraftingJob` has no id. What runs today is the Craft task's Slot
claim (above), which dies with the task.

The decision it would implement is already made in
[Multi-colonist Crafting](../design/multi-colonist-crafting.md) section 2b: a crafter interrupted by
its own needs keeps the station and comes back to finish. A `Task` does not survive that interruption,
so the station slot would be owned by a craft binding stored on the station's work order:

```cpp
struct CraftBinding {      // on WorkQueue
    EntityID crafter;
    uint64_t jobId;
};
```

- Jobs would get ids. `WorkQueue::addJob` merges a repeat recipe into the existing job; a merge would
  keep the existing job's id, a new job would get a fresh one.
- Bound at the crafter's first Craft action on the job, not when provisioning starts. Between Craft
  selection and that action, the crafter's Slot(station) claim holds the station, as it does today.
- Ties: a binding would beat any other task's Slot claim on its station. Between two Slot claims with
  no binding, the earlier `claimSeq` would win. The loser would find out at selection (the index shows
  the slot held, as today) or at action start and re-plan.
- Held while the crafter does anything that is not other work: eating, sleeping, a critical need.
  Tier-4 stickiness brings the crafter back.
- Released when the job completes, when the job is cancelled or removed, when the player takes
  direct control of the crafter, when it starts a craft on a different station (one binding per
  colonist), and, once they exist, when it dies or is incapacitated.
- Release would be derived the same way as task claims: the index would treat a binding whose crafter
  is gone, player-controlled, or bound elsewhere, or whose job id is no longer queued, as free, and
  `CraftingGoalSystem` would clear such bindings on its existing throttled poll. No event wiring.

Everything else about multi-colonist stations (UI, visuals, policy) stays in WOR-383.

---

## Stale memory

Memory can be wrong: the pile was hauled, the tree was felled, a box filled up. Claims are
validated against world truth when the task executes, and finding the thing gone or taken is
normal re-planning, not an error.

| Point | Checks | On mismatch |
|---|---|---|
| Selection | target in this colonist's memory (or colony storage), permission, claims in the index (self excluded) | option not offered, or offered as Reserved in the trace |
| Action start (arrival) | a cut's target is still in memory, still harvestable, still yields the goal's item, and the colonist holds the tool; a pile pickup's pile is in memory and carryable; a source or destination box, or a food box, exists and is not packaged | forget the memory entry where the thing is gone or no longer what it was, end the task, re-plan |
| Pickup or withdraw | live source quantity, and what fits | take what is there up to Q, rewrite inbound and in-flight to what was lifted, never take more than claimed; a pickup that lifts nothing ends the haul |
| Cut completion | the source still stands and is not regrowing | a gone or regrowing source pays nothing and a gone one is forgotten |
| Deposit | a station's next-job need, a blueprint's manifest, a box's accept rule, max, and room | deposit what fits and is needed, keep the rest, end the task; a destination that no longer exists keeps the goods |

A deposit does not check that the root goal still exists: for a box the configuration's accept rule
and max gate it, and for a station or site the need read from the world does. A colonist whose target
is evicted from memory while it travels keeps its claim (the claim is on the task, not in memory) and
re-sees the target on arrival, within sight range. All of these log at debug level; they are expected.

A colonist whose route to its target has failed (`NavState::CantFindWayTo`) holds, while it stays
failed, only the claims on goods it already carries. A failure toward a target in another nav region is
not remembered as a reach verdict, so the colonist may re-select the same work every frame and
`isSameTask` keeps it; without this rule its pile, bush, slot, or box room would read as taken for as
long as it stands there. What it has not reached is dropped: source quantity, Use, and slot claims, and
the part of an in-flight not yet landed (a cut's expected yield). What it carries keeps its Inbound and
In-flight, up to its cargo by the one cargo rule (`cargoOnClaims`): a haul past its pickup, or a cut
carrying what earlier cuts of its gathering landed. Those goods are possessed and on their way, so
dropping their claims would make the root read as wanting them, others would fetch them again, and
once the route recovered the root would be over-supplied. The index files a failed task this way
(`ClaimIndex::addCurrent`, which the rebuild and every append share, so the validator's fresh rebuild
agrees), and `AIDecisionSystem::applyNavOutcome` re-files the task the moment a route fails inside an
update and again when a later route for the same task succeeds. Another colonist is offered the work
meanwhile; if it takes it, the stranded colonist sees it taken at its next decision and lets it go, and
if nobody does, its claims return with its next successful route. Its suspended record is not affected:
those goods are possessed.

---

## The claim index

`ClaimIndex` (`libs/sim/ecs/ClaimIndex.h`) is one flat entry array (each entry a claim and its holder:
colonist, instance id, claim sequence, tier, task type, and whether it is a suspended record) and an
open-addressed table from a claim key to the head of a chain of the entries under that key. Keys are
(Use, target), (SourceQuantity, target, item), (Slot, target), (InFlight, root, item), and for inbound
(destination, item), grouped per destination so the slot query walks one group per item. A second
chain links each instance's entries, so retiring an instance touches exactly its entries.

Queries, each excluding a colonist's own instance ids: units claimed from a source, the top-ranked
holder of a source, units claimed from a source by holders a filter admits with the top of them
(eaters at least as urgent), inbound units at a destination, slots inbound goods will take at a box,
in-flight toward a root, the top holder of a Use target (all holders, or those a filter admits), the
top holder of a slot, and a walk of every claim. There is no per-colonist query and no claimant list;
the index names only the highest-ranked holder of a target.

### Rebuild per AI update

The index is rebuilt from every colonist's task and suspended record the first time a colonist
decides in an `AIDecisionSystem::update` (`ensureClaimIndex`); an update in which nobody decides
rebuilds nothing. Within the update each selection appends its new claims, so a colonist deciding
later in the same update sees what an earlier one just took. Every rewrite inside the update goes
through the index, so the subset rule needs no exception for a live instance: the switch path retires
the abandoned task's instance (unless it just became the suspended record or handed its cargo on), a
new record replaces that instance's entries with its smaller ones, an adopted record is appended under
its own instance id after its old entries are retired (never counted twice), a kept meal re-sized at a
decision is appended again, a discarded record or `enterControl` retires its instance, and the off-mesh
recovery at the top of the update retires the task it clears. ActionSystem's rewrites and clears land
between updates, so the next rebuild reads them. Any staleness only over-claims: another colonist may
skip a free item for one update, and nothing is double-booked.

Cost: dozens to low hundreds of colonists, each holding at most a handful of claims, is a few
hundred to a couple of thousand entries into flat, reused storage, against scoring that already
walks a few hundred goals per evaluating colonist. Incremental maintenance would need every clear
site, ActionSystem rewrite, and entity destruction to update the index, which is the release-call
surface this design removes.

### Readers and staleness

The AI is the only reader that sees a current index, during its update. The one other reader is the
deposit leg's commit (`ActionSystem::carryHarvestIn`), which reads the index the AI last built and
commits no more than its own in-flight there (above). The task panel does not read the index: it
names each row's worker from the colonists' current tasks. The goal systems do not read it either:
they read only remaining need.

### Determinism

Which of two colonists wins a contested target depends on ECS view order and each colonist's
re-evaluation timer. Both are deterministic for a given world, spawn order, and command sequence,
so a headless run repeats exactly. Tests assert that exactly one colonist wins, never which one.

### Reserved rows, and option dedup (planned)

The haul and harvest evaluators emit one option per known matching source per goal, visiting only
the remembered entries of the defs that give the goal's item (Memory's per-def lists). A source others'
claims take whole is not offered; instead each (goal, item) shows at most one Reserved row, for the
nearest such source, and only when no source offered is nearer. The row names the holder.

Planned: before scoring, collapse equivalent available options to the best one per (root, item), the
nearest available source by trip distance. ONI collapses equivalent undriven fetch chores to the best
per type for the same reason; it would bound the trace by goals and items rather than by remembered
sources. It is not built because the selection walks options in order and passes over unreachable
ones, so collapsing to the nearest before reachability is known would lose the fallback to the next
source.

---

## Save and load

Claims are not saved separately. What is saved (when save lands) is the data they derive from: each
colonist's `Task` with its claim list, instance id, sequence number, tier, and suspended record; and
the claim sequence counter (kept on `GoalTaskRegistry`, the colony state a save will hold). Planned:
each station's craft binding and job ids, once they exist. On load the index is rebuilt and the
validator runs once in development builds. There is nothing to purge, because there is no separate
store to disagree with.

---

## Player-facing output

The UI docs own the layout ([In-game screen](../design/ui/screens/in-game.md) tasks panel,
[Decision Trace](../design/game-systems/colonists/decision-trace.md)); this spec only supplies the
data.

Built:

- Worker: the task panel's "Working" status names the colonist whose current task serves the row,
  read once per panel refresh from each colonist's task (`GlobalTaskAdapter`, `workersFromTasks`). A
  cut or a haul names the goal it was picked through (a carry-in and a cut's deposit leg name their
  root); a build, craft, or placement names the entity it works, so the Build umbrella names its
  builder, a Craft row its crafter, and a clear row its clearer. A row shows one name even when several
  colonists serve it: the first colonist found in view order. A suspended record is not current work.
- The panel lists only goals with something to do (`goalHasWork`): a full box, and a stocking cut whose
  minimum is met, drop out of the list while their goals stay.
- Decision trace: a Reserved option status naming the holder, "(reserved by Alice)", or for a Craft
  option "(station in use by Alice)", from the index's top-holder lookups.

Planned:

- Task list availability per root and item: "N available (M in progress)". N would be the unclaimed
  quantity of sources the colony knows about, capped at the root's remaining need; M in-flight.
- `knownBy`: colonists whose memory holds at least one unclaimed source for the root, computed on
  the panel's throttle through Memory's capability index.
- Per-colonist task list: roots for which that colonist knows an unclaimed source. Today the colonist
  view shows the same rows as the colony view, measured from the colonist.
- Distinct blocked reasons: "no source known", "all known sources claimed", "destination full (M
  inbound)", "needs materials", "station in use by Alice". Today the panel shows "Blocked" (with
  landed/required materials for a craft), "Waiting (no source found)", "Needs materials" for a site,
  "Working", "Available (far)", and "Unassigned".
- A bumped colonist's trace showing "bumped by Alice (critical hunger)" on its re-plan.

---

## Performance requirements

The figures below are targets. No benchmark measures them yet; the tests named here count work, not
time.

- AI update cost (`AIDecisionSystem::update`, mean and p99) shows no regression against `main`
  beyond run-to-run noise in the desert perf view, and in a two-colonist run of scenario 02. Measured
  by the perf capture against `main` on the same machine, on an idle machine.
- Target: index rebuild under 5% of `AIDecisionSystem::update` with 200 colonists and 300 goals, with
  no heap allocation in steady state. No benchmark exists. `ClaimIndex` keeps its entries, groups, and
  table across rebuilds, so a warm rebuild allocates only when it grows.
- No per-goal full-memory scans. Memory keeps each def's keys beside its capability index
  (`worldEntitiesOfDef`, one push per remember, one swap-remove per forget), so a goal visits only
  the entries of the defs that give its item: the item itself for a haul, the registry's
  `harvestablesYielding` defs for a harvest. The per-def gates (storage headroom, room to carry,
  the tool and the yield fit) are answered once per def before any entry is visited. Counted by
  `AIDecisionSystemTest.FindsANeedSourceInALargeMemory`, `WorkGatesComeBeforeTheSources`,
  `HarvestGoalSeesEveryDefThatYieldsItsItem`, and `HaulGoalsOfferEachPileOnce`, which count the work
  sources visited. Target, not measured: with 10,000 remembered entities that match no goal, decision
  time stays within noise of an empty memory.
- Remaining need is worked out once per (root, item, basis) per update, counted by
  `AIDecisionSystemTest.EachRemainingNeedIsWorkedOutOncePerUpdate`. A need source's reachability is
  tested once per source per reach key, counted by `MemoryQueries.SourceReachMemoTestsEachSourceOncePerKey`.
- Live pile quantity is one keyed lookup through `LoosePileIndex`, not a scan of piles.
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
- Trace size per colonist is bounded today by roots times accepted items times remembered sources of
  those items; the planned dedup would bound it by roots times accepted items.

---

## Known gaps

Behaviour the code has that no doc should promise otherwise, kept here until fixed:

- A crafter that leaves for a need before its Craft action starts loses the station (the binding is
  planned, not built).
- The panel names one worker per row, so a row several colonists serve reads as one colonist's.
- An unwanted two-hand armful is put down only when the colonist selects Wander; a colonist that idles
  by gathering food (tier 7, ranked above Wander) carries it until it wanders.
- A colonist whose every pack slot, belt slot, and hand holds a tool can't make room to eat and picks
  the same source again.
- A known loose pile of an item demotes every fresh cut for the same root and item, however far the
  pile and however near the plant.
- Interrupting a sleeping colonist for a higher tier leaves the Sleep action active (the switch path
  clears the task but not the Action, and ActionSystem starts a new action only when none is active).
  Not covered by a test; it may run the rest of the sleep under the next task when it arrives.
- A named storage rule's max does not cap the item while a matching category wildcard has no max, and
  every placed box starts with such wildcards; the storage dialog removes them when it adds a named
  rule, the dev `storage` verb does not.

---

## Testing

The multi-colonist regression pass is the unit and flow tests in `libs/sim`; the headless sim-cli
([Sim CLI](./sim-cli.md), WOR-603) that would run `.sim` scenarios is planned, not built (its
groundwork steps 1 to 5 are done; `apps/sim-cli` does not exist). Planned sim-cli scenarios for this
spec: two colonists and one tree; the last ingredient unit; three haulers, one free slot, three items;
a starving colonist taking the last berries from a hauler; a crafter who sleeps mid-craft and returns
to its bound station. The last needs the binding. Scenarios 01 to 04 and 11 under
`docs/testing/scenarios/` are the in-game pass.

Unit and flow tests by behavior live in `AIDecisionSystem.test.cpp`, `ActionSystem.test.cpp`,
`ChainClaims.test.cpp`, `ClaimIndex.test.cpp`, `GoalTaskRegistry.test.cpp`, `StoreOrDrop.test.cpp`, and
the goal-system suites. Check for an existing test of the same path before adding one. What they cover:

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
  discards it; a drop leaves no record and the pile is claimable by anyone; sleep releases it.
- Batching, the top-up rule, piles before cuts, the chosen delivery, and the failed-route rule.
- Hunger from the pack, bushes, piles, and boxes; eater urgency; making room; stale food.
- Storage remaining need is item-counted and matches the box's per-item room.

Planned with bumping and the binding: critical hunger bumping a storage hauler (its Action cleared, it
re-plans and keeps its cargo), two critically hungry colonists never bumping each other, ordinary work
never bumping ordinary work, and two colonists selecting Craft at one station with the binding
surviving the crafter eating and releasing on job cancel, completion, control, destruction, and a craft
elsewhere.

The validator (`ClaimIndex::findViolation`) runs at the end of every AI update that built the index, in
development builds (`DEVELOPMENT_BUILD`), so every test that runs the AI runs it, and aborts on a
violation. It asserts: a fresh rebuild is a subset of the appended index, and every surplus entry
belongs to an instance id that no live task or suspended record holds (a task cleared this update);
entries compare by holder instance, kind, target, item, and slot with quantities summed, not by tier,
which `isSameTask` may change within the update; every Inbound has a matching In-flight of the same item
on the same task or record; no inactive task holds claims, and no claim lacks an instance id. It does
not check over-subscription (a hungry colonist's Use legitimately shares a bush with a work claim).
Planned with the binding: every binding names a live crafter bound nowhere else and a queued job id.
In-flight is not checked against remaining need: the need can drop under goods already on their way by
means no claim controls (a player lowers a box's max, a dev give fills a site), and the deposit then
keeps the surplus, so the check would abort on legitimate play. Its working storage lives on the
scratch index, so a warm check allocates nothing.

---

## Migration

Done, each deleted in the same change that replaced it, per the no-legacy rule:

- `GoalStatus::InProgress` and `GoalStatus::Complete` (never assigned). `GoalStatus` is `Available`,
  `Blocked`, and `NoSource`.
- `GoalOwner::None`, with the registry's owner-index guards for it.
- `reservationTimeout`: `TimingConfig::reservationTimeout`, `getReservationTimeout()`, its parse in
  `PriorityConfig.cpp`, the element in `priority-tuning.xml`.
- The in-flight approximations claims replace, all at once so nothing subtracts carried goods twice:
  `ConstructionSystem::carriedAmount` and `constructionHarvestDemand`; `CraftingGoalSystem`'s
  `colonyCarriesStock`; the craft-fetch gate on delivered plus carried in `evaluateHaulOptions`; the
  craft pickup sizing from delivered plus carried in `ActionSystem::startHaulAction`.
- `createHaulForCompletedHarvest`, the chain-id-tagged stocking carry-in Haul, `GoalTask::chainId`,
  `Task::chainId`, and the registry's chain id counter (the counter's slot became the claim sequence);
  construction's per-material inventory-source Haul goals; and the three inventory-source haul paths in
  `evaluateHaulOptions`, replaced by the in-place deposit leg and carry-in from possession.
- Goal counters (`targetAmount`/`deliveredAmount` on every goal, roots included), the storage
  slot-count use of `targetAmount` and its owner special case in `evaluateHaulOptions`, and
  `GoalTask::availableCapacity()`, replaced by remaining need at the root. `recordDelivery`'s
  Haul-only parent credit went with them.
- The per-call colony memory walk in the colony-storage check, replaced by the per-update
  `ColonyStorageKnowledge` (the colony-wide rule itself stays).
- The adapter's per-goal colonist scan, in favor of one read of the current tasks per refresh.
- The comments that said there was no reservation system (`enterControl`, the storage-pull gate), and
  the "world entity ID" comment on `harvestTargetEntityId`.

---

## Out of scope (seams only)

- Unifying the two crafting task paths (WOR-295). Claims attach to whatever task shape runs; the
  chain table changes rows, not kinds.
- Multi-colonist crafting UI and station policy beyond the claim and binding (WOR-383).
- Door slot queues. Pathfinding owns them.
- Ownership's own design. Only the query order above is fixed here.

The need-option source selection bugs this spec once assumed fixed (WOR-605: the Hunger option picked
the nearest harvestable of any kind; WOR-607: an unreachable nearest source gave NoSource instead of
the next one) are fixed on this branch.

---

## Related documents

- [Task Registry](../design/game-systems/colonists/task-registry.md): goal-level tasks and the
  per-colonist view.
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
- [Sim CLI](./sim-cli.md): the planned headless scenarios.
