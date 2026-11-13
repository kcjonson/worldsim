# Interruptable crafts

Status: Spec, not built. [What is built today](#what-is-built-today) is the starting point.
Created: 2026-11-13
Epic: WOR-822 (Interruptable crafts: saved progress, what interrupts, and the crafter's hold on the
station)

A craft can be long, days of game time for the large ones to come, so it has to survive its crafter
walking away. This spec moves a craft's progress off the action and onto the job at the station,
makes craft and build work interruptable, splits ordinary needs into two levels so that a very
hungry colonist leaves its work and a slightly hungry one does not, and gives the crafter a hold on
the station that lasts while it is away. It also covers what a cancelled part-done job gives back,
how sleep pays out when it is cut short, and what the player sees.

**MVP Status:** See [MVP Scope](../design/mvp-scope.md). Core colonist plumbing; the player-facing
part is the paused-craft status and the bench's work-in-progress look.

---

## What is built today

Read from the code on `main` at 0b12648a.

- **A craft is one action.** `Action::Craft` runs for `RecipeDef::workAmount / 100` seconds (0.9 s
  for a box, 1.2 s for a shelf, 5 s for an axe) and is not interruptable. Its progress is
  `Action::elapsed`. `WorkQueue::progress` is a copy made each tick for the UI; nothing reads it
  back. Skill does not change craft speed.
- **Inputs are consumed at the end.** `startCraftAction` checks that the station's store holds every
  input and takes nothing. `applyCraftingEffect` removes the inputs, produces the outputs, and
  counts the unit (`completed++`) when the action completes.
- **A job of N units is N Craft tasks.** The store is metered to one unit's bill of materials, so
  each unit is provisioned, crafted, and counted, and the Craft task ends after each one.
- **The station is held by the Craft task's Slot claim**, taken at selection
  (`recordChainClaims`). The claim dies with the task. Nothing else records who is crafting:
  `CraftingJob` is `{recipeDefName, quantity, completed}`.
- **The switch gate.** While an action runs, a non-interruptable one blocks every switch, critical
  needs included; an interruptable one yields to a same or higher tier. Craft, Build, Deconstruct,
  Harvest, Pickup, Withdraw, Deposit, PickupPackaged, PlacePackaged, Eat, Drink, and Toilet are
  non-interruptable. Sleep is the only interruptable action.
- **Needs have two levels.** Below its seek threshold (hunger and thirst 50, energy, bladder and
  digestion 30) a need is an option at tier 5. Below 10 it is tier 2. Critical has two definitions:
  the tier comes from a constant in `AIDecisionSystem` (`kCriticalNeedThreshold`), while the
  decide-at-once trigger (`criticalNeedMask`, `shouldReEvaluate`) uses the need's own
  `criticalThreshold`. The thresholds and decay rates are literals in
  `NeedsComponent::createDefault`.
- **Build progress is already on the thing built.** `StructureBlueprint::workDone` and `workTotal`;
  the Build action has no duration and adds work each tick. A builder who stops loses nothing and
  anyone may continue. The Build and Deconstruct actions are still non-interruptable, so a builder
  on a large blueprint cannot leave it even for a critical need.
- **Sleep pays at the end.** 8 s, restoring `60 * quality` energy on completion. Quality is an
  inline choice in `NeedActions.cpp`: 0.5 when the colonist sleeps where it stands, 1.0 at any
  other target. A sleeper woken early gets nothing.
- **A Craft option is never tested for reach.** Hauls and harvests ask whether their source can be
  reached; the Craft option in `buildDecisionTrace` does not ask about its station.
- **Cancelling a job** (`WorkQueue::removeJob`) leaves whatever was delivered sitting in the
  station's store.

So the gap WOR-633 was filed for is narrower than it reads: a crafter never leaves mid-craft, and
loses its station only if a critical need pulls it off the walk there. The owner's ruling is that
the atomic craft is the thing that is wrong.

---

## Decisions

The owner's rulings, all 2026-11-13. Quoted text is his; the rest are options he chose from a
question round, with the question in brackets.

1. "crafts need to be interrupted, large crafts in the future could last days. The question is what
   is capable of interrupting them, is it not?"
2. [Does a long craft stop only for critical needs, or for ordinary hunger and sleep too?]
   "Ordinary needs too".
3. [How does that fit the ladder?] "'ordinary needs' should have a couple tiers. 'slightly hungry'
   could be settled when idle, but 'very hungry' would interrupt work. we just need to set those
   thresholds."
4. [Where does "very" start?] Hunger and thirst below 25, energy, bladder and digestion below 20,
   as config values.
5. [Where does "slightly hungry" sit against hauling and stocking?] Before them, as today. Only
   work orders make it wait.
6. [Where does progress live, and what may a second colonist do?] On the station's job. Only the
   crafter holding the unit may continue it. If the hold is released, the next crafter continues
   from the saved progress; nothing resets.
7. [Builds too?] Yes, the same rule.
8. [Cancelling a part-done job?] All the materials come back; the work is lost.
9. [When does the hold begin?] At the first work on the unit. Before that the crafter has the
   station only while walking to it.
10. [How long does it last on a job of several units?] One unit at a time.
11. [Sleep cut short?] "sleep should have a curve to its restoration, very little at first, then
    grows more quickly at the end. keep in mind that in the future many many things will effect
    sleep quality. bed type, inside/outside, etc."
12. [A very hungry colonist carrying a two-hand armful to a work order?] It delivers first, then
    eats. Only a critical need drops an armful.
13. [What does the player see?] The panels and the in-world look, both in this epic.

Decided earlier and kept: a station with a craft in progress is taken, and a craft interrupted by
its crafter's needs is not handed off
([Multi-colonist Crafting](../design/multi-colonist-crafting.md) section 2); the crafter comes back
and finishes before taking on anything new (2b); direct player control interrupts and releases the
station; a station holds only the current recipe's bill of materials, never leftovers
([Crafting & Material Provisioning](./crafting-provisioning-architecture.md) section 2).

Decision 3 replaces the June lock in [Colonist Task Arbitration](./colonist-task-arbitration.md)
section 3 ("only critical needs preempt active work") for needs past the new threshold. The rest of
that section stands: a slightly hungry colonist still finishes its work order first.

One figure given during the question round was wrong. Option 4 was offered as leaving "roughly 15
game-minutes" between very hungry and critical; at the real decay rates (hunger 0.08 per
game-minute) it is about three game-hours, and about two for thirst. The thresholds are config, and
the choice is recorded as made.

Choices made in writing this spec, not put to the owner: the curve's shape (a power of the fraction
slept, exponent 2); the "very" thresholds for needs with no action yet (hygiene and temperature 25,
recreation 20); dropping job ids (the hold is a field of the job, below); the bench's look being one
on or off overlay per station def; and reading decision 12 as "a need drops an armful only when
critical", which also stops a slightly hungry colonist dropping an armful it is carrying to storage
(it does today).

---

## Model

### Work is saved on the thing worked

A craft's progress belongs to the job, as a build's belongs to the blueprint. `CraftingJob` gains
`workDone`, the work on its unit in progress, in the recipe's own work units (`workAmount`). The
Craft action becomes open-ended like Build: no duration, and each tick it adds
`kCraftWorkPerSecond * dt` to the job. That is 100 per second, today's `kWorkTicksPerSecond`, which
moves out of the `Action::Craft` factory and absorbs the crafting dialog's own copy
(`kWorkUnitsPerSecond`). When `workDone` reaches the recipe's `workAmount` the unit completes:
inputs leave the store, outputs are produced, `completed` goes up by one, and `workDone` returns to
zero. A unit never completes without consuming its inputs: if any is missing at that moment the work
stays as it is and the option reads as needing materials. (Today a craft with missing inputs logs
and still produces its output.)

Inputs stay in the store until the unit completes, as now. While a unit is part-done the store is
that unit's materials, the station's remaining need is zero, and no provisioning is offered.

An action never holds progress of its own. Clearing a Craft or Build action at any tick loses
nothing, which is what makes them safe to interrupt.

### The crafter's hold

The job records who is making its part-done unit: `CraftingJob::crafter`. This is the craft binding
the claims spec planned, reduced to one field. It is set by the first tick of work a colonist adds
to a unit nobody holds, and cleared when the unit completes. In between it survives everything the
crafter does for itself: eating, drinking, sleeping, the toilet, at any need level.

While a job has a live crafter, nobody else may craft at that station. The claim index answers the
station's slot holder with the crafter, ahead of any Slot claim, and another colonist's Craft option
there reads Reserved. The crafter asking about its own station is told it is free.

This hold is not the "hold" the claims spec says sleep releases. That one is a suspended record's
count of carried goods toward a goal; this one is a station, and sleep does not release it.

The hold is released, with `workDone` kept, when:

- the player takes direct control of the crafter. `enterControl` clears it with the task, the
  action, and the suspended record; an away crafter's task does not name the station, so it finds
  the job by scanning the stations' queues. The clear is a write, not only the liveness test, so the
  hold does not come back when control is released;
- the crafter starts work on a unit at another station (one hold per colonist; the write that sets
  the new one clears the old);
- the crafter no longer exists. Death and incapacitation are not in `libs/` yet; when they are,
  they release it the same way.

After a release the unit is part-done with no crafter. The next colonist to select Craft there
continues from the saved work and takes the hold with its first tick.

The hold ends with the job when the job leaves the queue (cancelled, or the station is
deconstructed); see [Cancelling](#cancelling-a-part-done-job).

Before any work lands there is no hold. The Craft task's Slot claim keeps a second crafter from
walking to the same station, as today, and goes with the task if a need pulls the crafter off the
walk. Nothing has been started, so nothing is kept, and anyone may take the craft.

A job of several units is held one unit at a time. Between units the store is empty, anyone may
provision, and anyone may start the next unit. Provisioning never touches the hold: it cannot
release one, because no hold exists while a unit is waiting for materials.

No job ids. The planned design put the binding beside the queue and needed an id to name its job;
as a field of the job it needs none. A part-done unit pins its job as the station's current job:
`WorkQueue::getNextJob` answers the job with work done before any other.

### Needs in three levels

A need has three thresholds, and each puts its option at a different tier:

| Level | Below | Hunger, thirst | Energy, bladder, digestion | Effect |
|---|---|---|---|---|
| Ordinary ("slightly hungry") | `seekThreshold` | 50 | 30 | settled when no work order is waiting |
| Urgent ("very hungry") | `urgentThreshold` (new) | 25 | 20 | interrupts work |
| Critical | `criticalThreshold` | 10 | 10 | interrupts nearly everything, as today |

Classification reads the need's own thresholds; `kCriticalNeedThreshold` is deleted. Hygiene and
recreation get urgent values too (25 and 20) though nothing fulfils them yet.

### The ladder

| Tier | Name | Trigger | Was |
|---|---|---|---|
| 1 | Survival / danger | reserved | 1 |
| 2 | Critical needs | a need below its critical threshold | 2 |
| 3 | Forced player orders | reserved | 3 |
| 4 | Urgent needs | a need below its urgent threshold | new |
| 5 | Own unfinished work | a Craft whose part-done unit this colonist holds | new |
| 6 | Active work orders | a queued craft or build and its provisioning | 4 |
| 7 | Ordinary needs | a need below its seek threshold | 5 |
| 8 | Opportunistic work | haul loose items, stock storage, undirected harvest | 6 |
| 9 | Idle | gather food for the larder, wander | 7 |

Tiers 1 to 3 keep their numbers, so the planned bumping rule ("only tiers 1 to 3 bump") reads as
written. Everything from the old tier 4 down moves by two.

Tier 5 is how "finishing outranks starting new work by a wide margin" is delivered. It is a tier and
not a score bonus for the reason the arbitration spec gives: an ordering that must always hold is a
category. A crafter back from a meal sees its held craft above every other work order, whatever the
distance. Tier 5 sits under urgent needs, so the crafter still leaves when very hungry, and above
ordinary needs, so it does not leave when slightly hungry.

Builds have no tier 5. A blueprint has no single owner, any builder continues it, and a builder
back from a meal picks among work orders as usual.

### What interrupts what

Craft, Build, and Deconstruct work become interruptable. The gate is unchanged: a running
interruptable action yields to a same or higher tier, and a non-interruptable one yields to nothing
until it ends. The short actions (Eat, Drink, Toilet, Harvest, Pickup, Withdraw, Deposit) stay
non-interruptable; they last seconds, and every tier waits for them as critical needs do today.

| Colonist is | Ordinary need (7) | Urgent need (4) | Critical need (2) | Other work order (6) |
|---|---|---|---|---|
| crafting its held unit (5) | waits | interrupts, hold kept | interrupts, hold kept | never |
| building or deconstructing (6) | waits | interrupts | interrupts | past the hysteresis margin (new) |
| hauling or cutting for a work order (6) | waits | interrupts between actions | interrupts between actions | as today |
| carrying a two-hand armful its root still wants | waits | waits for the delivery | interrupts, armful dropped | a higher tier interrupts, as today |
| walking to a craft it has not started (6) | waits | interrupts, Slot claim lost | interrupts, Slot claim lost | as today |

Two cells are new behaviour beyond the needs columns. A builder can be taken by another work order
that beats its build by more than the hysteresis margin, where today the Build action blocks every
switch; the blueprint keeps its work, and a builder standing at its blueprint already has the full
distance score, so the case is rare. And the armful row adds to one rule in
[Task Claims](./task-claims.md#interruption-by-the-colonists-own-needs): "only a strictly higher
tier interrupts a task carrying a two-hand armful its root still wants" stays for work, and gains
"and of the needs, only a critical one". An urgent need waits for an armful bound for a work order;
an ordinary need, which today outranks an armful bound for storage and drops it, waits too. The
rule's own exits stand: once the root wants none of the armful, or the route to it has failed,
nothing is held and the need goes ahead.

A colonist decides at once when a need crosses into urgent, as it does when one crosses into
critical, and not again on every frame the need stays there. `shouldReEvaluate` gets a second mask
on the task, `urgentNeedsAtDecision` beside `criticalNeedsAtDecision`, and the same exemption: a
colonist already serving an urgent or critical need is not re-decided by another need becoming
urgent.

The switch path must clear the abandoned action along with the task. That is the fix for "Waking a
sleeping colonist for other work leaves the Sleep action running under the new task" (WOR-820), in
flight on another branch; this epic's interruption work starts from it. Without it an interrupted
Craft action would run on under the need task.

### Coming back

When the need is met, the crafter's held craft is its tier 5 option. It walks back, the Craft task
takes its Slot claim as usual, and the action adds work to the same job from where it stopped.
`startCraftAction` still checks the store holds every input; if something emptied the store while
the crafter was away, the Craft option reads as needing materials, provisioning reopens for the
shortfall, and the saved work and the hold both stay.

Every Craft option, held or not, gets the reach test work sources already get: a label compare
(`NavigationSystem::connected`) and the failed-route verdicts in `SourceReachMemo`. A station that
cannot be reached is not offered. Without this a held craft behind a new wall would be a tier 5
option picked again on every decision. With it the crafter takes other work, and the hold stays
until one of the release conditions is met; the player can cancel the job.

### Cancelling a part-done job

When the station's current job leaves the queue, everything in the station's store drops beside the
station as loose piles, in full, and the saved work goes with the job. The store only ever holds the
current job's materials, so cancelling a job further down the queue drops nothing. This applies
whether or not work has started: delivered materials for a cancelled job are not the next job's bill
of materials, and today they stay in the store against the exact-bill rule.

The cancel is a function in `libs/sim` (`cancelCraftingJob(world, station, recipe)`), so sim tests
drive it and the game's dialog calls it; `WorkQueue::removeJob` has no world to drop into and stops
being called directly. The piles are ordinary loose sources with no claims, dropped through the
drop-resource callback `ActionSystem` and `ConstructionSystem` already use, outside the station's
footprint.

A Craft action knows its job by the hold. On its first tick it checks the store as
`startCraftAction` does today and takes the hold if nobody has it; on every later tick it carries on
only while the station's current job still names this colonist as crafter, and otherwise ends with
no effect. So a job cancelled and queued again under a running action is a new job with no crafter,
and the old action stops.

Deconstructing a station cancels its current job first, by the same path. There is no way to lower
a job's quantity today (only `addJob` and `removeJob`); one added later goes through the same
function when it would end the current job.

### Sleep pays out along a curve

Sleep restores energy as it goes, slowly at first and faster toward the end. A sleep has a duration
`D` and a total `R = 60 * quality`, both as today. By the fraction `f` of the sleep completed, the
sleeper has gained

```
gained(f) = R * f^p        p = sleepCurveExponent, 2.0
```

and each tick adds the difference since the last. Half a sleep gives a quarter of its energy; a
whole one gives all of it, so completion adds nothing extra. A sleeper woken early keeps what it
has. Going back to sleep starts a new sleep and the curve from the beginning, so broken sleep costs
more than it saves, which is the point of the shape.

Quality is one number worked out when the sleep starts, a product of factors. Today it is the inline
choice between 0.5 and 1.0; that becomes a function in `NeedActions.cpp`
(`sleepQuality(colonist, spot)`) with the spot as its one factor.
Bed type, indoors or out, and whatever else comes later multiply in at that one place; the curve and
the payout do not change when they do.

Eat, Drink, and Toilet stay short, non-interruptable, and paid at the end.

---

## Rules

Numbered so tasks and tests can cite them.

1. Craft progress is `CraftingJob::workDone`, in recipe work units, advanced only by a Craft action
   at `kCraftWorkPerSecond`. No action holds craft progress.
2. At most one job in a queue has `workDone > 0`, and it is the queue's current job.
3. A unit completes when `workDone >= workAmount`: inputs consumed, outputs produced, `completed`
   up by one, `workDone` zero, `crafter` cleared. A recipe with `workAmount` 0 completes on its
   first tick.
4. `crafter` is set by the first tick of work a colonist adds to a unit with no live crafter. A
   job's crafter is live while that colonist exists and is not under direct player control. A Craft
   action past its first tick continues only while its job names its colonist as crafter.
5. A colonist is the crafter of at most one job. Setting a hold clears that colonist's other one.
6. Only a job's live crafter may add work to it. A Craft option at a station whose current job has
   a live crafter is Reserved for everyone else, and never for the crafter.
7. Needs never release a hold. Player control, work started at another station, the crafter ceasing
   to exist, and the job leaving the queue do.
8. A released hold leaves `workDone` as it was.
9. A need below its urgent threshold is tier 4; below critical, tier 2; below seek, tier 7.
10. A Craft option for the unit the colonist holds is tier 5. Every other Craft option is tier 6.
11. Craft, Build, and Deconstruct actions are interruptable. The gate's rules are unchanged.
12. A task carrying a two-hand armful its root still wants yields to a strictly higher tier of work,
    as today, and to a need only when the need is critical.
13. When a station's current job leaves the queue, everything in the store drops beside the station
    as loose piles. A station's store never holds materials for a job that is not its current one.
14. Sleep adds `R * (f1^p - f0^p)` energy per tick and nothing at completion.
15. A unit completes only by consuming its full bill of materials from the store.
16. A Craft option is offered only for a station the colonist can reach.

---

## Data

```cpp
struct CraftingJob {
    std::string recipeDefName;
    uint32_t    quantity = 1;
    uint32_t    completed = 0;
    float       workDone = 0.0F;       // work on the unit in progress, in recipe work units
    EntityID    crafter = kInvalidEntity; // who holds the part-done unit, if anyone
};

struct WorkQueue {
    std::vector<CraftingJob> jobs;     // progress is gone; readers divide workDone by workAmount
};

struct Need {
    float value, decayRate, seekThreshold, criticalThreshold;
    float urgentThreshold;             // appended last: Need{v, d, seek, crit} is written positionally
};
```

**Need tuning as data.** The per-need decay rate and the three thresholds move out of
`NeedsComponent::createDefault` into `assets/config/colonists/needs.xml`, loaded with the other
config ([Config Loading](./config-loading.md)) into a `NeedsConfig` in `libs/assets` beside
`PriorityConfig`, and built the way that one is: the defaults are installed in code
(`installDefaultTaskTiers` is the pattern), the file is read over them, and a need the file leaves
out fails the load. `createDefault` reads `NeedsConfig`, so the tests and benches that never load a
config file (some twenty call sites) get the installed defaults, and its literals are deleted.

```xml
<Needs>
  <Need name="Hunger"     decayPerGameMinute="0.08"  seek="50" urgent="25" critical="10"/>
  <Need name="Thirst"     decayPerGameMinute="0.12"  seek="50" urgent="25" critical="10"/>
  <Need name="Energy"     decayPerGameMinute="0.05"  seek="30" urgent="20" critical="10"/>
  <Need name="Bladder"    decayPerGameMinute="0.03"  seek="30" urgent="20" critical="10"/>
  <Need name="Digestion"  decayPerGameMinute="0.02"  seek="30" urgent="20" critical="10"/>
  <Need name="Hygiene"    decayPerGameMinute="0.015" seek="40" urgent="25" critical="15"/>
  <Need name="Recreation"  decayPerGameMinute="0.01"  seek="30" urgent="20" critical="10"/>
  <Need name="Temperature" decayPerGameMinute="0"     seek="40" urgent="25" critical="15"/>
  <Sleep curveExponent="2.0"/>
</Needs>
```

**Tiers.** `priority-tuning.xml` base tiers are renumbered (FulfillNeed 7, Craft, Build and
Deconstruct 6, Haul, Harvest and PlacePackaged 8, Wander 9). `classifyTier` promotes a need to 4 or
2 by its thresholds, a held Craft to 5, and work serving a work order to 6; the gather-food sentinel
and satisfied needs go to 9. The ladder gets names in one header (an enum or constants: critical,
urgent, own work, work order, ordinary need, opportunistic, idle), and the places that write the
idle tier as a bare 7 use the name: `installDefaultTaskTiers`, `Task::priorityTier`,
`EvaluatedOption::tier`, `ClaimHolder::tier`, and `tierLabel` in `TaskListView.cpp`. Left as
numbers they would all come to mean "ordinary need".

**Claim index.** After the per-task pass, `ClaimIndex::rebuild` walks the stations' queues and
records each live crafter against its station in a table of its own, not as a claim: a hold has no
task instance id, and the validator rejects a claim without one. `slotHolder` consults that table
before the Slot claims. The asking colonist's own hold is left out, which `ClaimExclusion` cannot do
today (it carries the colonist's task instance ids only), so it gains the colonist's entity. There
are few stations; the pass allocates nothing warm. `CraftingGoalSystem`'s existing throttled poll
clears a `crafter` that is no longer live, so a stale one never outlasts a poll; the index never
trusts one in the meantime because it tests liveness itself.

**Validator.** `ClaimIndex::findViolation` gains: every `crafter` set on a job names a job with
`workDone > 0`; no colonist is the crafter of two jobs; no queue has two jobs with `workDone > 0`.
Holds are checked by these and are outside the claim assertions.

**Save and load.** Nothing is saved today. When save lands, `workDone` and `crafter` are saved with
the queue and need nothing else: there is no separate binding store to disagree with.

**In-progress overlay.** A station's `AssetDef` may name the def drawn over it while its current job
has `workDone > 0`:

```xml
<workInProgressOverlay>CraftingSpot_WorkInProgress</workInProgressOverlay>
```

The overlay is an ordinary simple asset (one SVG), drawn at the station's position and sorted just
in front of it, the way the packaging crate's front layer is drawn over a packaged item
(`PackagedLayout`). It is on or off: it does not change with the recipe or the amount of work done,
and it shows whether or not the crafter is there. `CraftingSpot` gets one in this epic. A station
def without the element shows nothing.

---

## Player-facing output

- **Station panel and crafting dialog.** The progress bar reads `workDone / workAmount` and keeps
  its value while the crafter is away. The job line names the crafter: "Crafting Primitive Axe
  (0/1), Alice". While the crafter's current task is not this craft it reads "Alice, away
  (eating)", with the activity taken from the crafter's task. A part-done unit with no crafter reads
  "part-done, no crafter".
- **Tasks panel.** A Craft row follows the Build row's shape (status "Building", detail "Alice
  (40%)"): status "Crafting", detail "Alice (40%)", and status "Paused", detail "Alice away (40%)"
  while the crafter is seeing to a need. The crafter is named from the job, not from whoever's
  current task points at the station, so the row keeps its name while the crafter is gone.
- **Decision trace.** Another colonist's Craft option at a held station: Reserved, "(station held
  by Alice: part-done Primitive Axe)". Tier labels gain "Urgent need" and "Own unfinished work".
- **In the world.** The station shows its work-in-progress overlay from the first tick of work until
  the unit completes or the job is cancelled.
- **Cancel.** The dialog's cancel on a job with materials or work in it drops the materials beside
  the station.
- **Dev state.** `/api/state?what=stations` adds each job's `workDone`, `workAmount`, and
  `crafter`, which the scenario below reads.

An open-ended action has no fraction of its own, so the two places that show `Action::progress()`
read the job's fraction for a Craft: the colonist's activity bar (`ColonistAdapter`) and the task
list's current-action line (`TaskListView`).

---

## Migration

Deleted, each in the change that replaces it:

- `Action::Craft`'s duration and its `interruptable = false`; the `workAmount` parameter and the
  work-ticks conversion in the factory. Build's and Deconstruct's `interruptable = false`.
- `WorkQueue::progress`, the per-tick copy into it in `ActionSystem`, its resets in
  `applyCraftingEffect` and `removeJob`, and its two readers (`CraftingAdapter` and
  `CraftingDialogModel`). `kWorkUnitsPerSecond` in `CraftingDialogModel.cpp`.
- `kCriticalNeedThreshold` in `AIDecisionSystem`, in favour of each need's own thresholds.
- The threshold and decay literals in `NeedsComponent::createDefault`.
- `taskSwitchThreshold`: the element in `priority-tuning.xml`, its parse, its field and getter in
  `PriorityConfig`, and the comments that say the hysteresis margin equals it. Nothing calls the
  getter; the margin is `InProgress/bonus`.
- Direct calls to `WorkQueue::removeJob` from the game, in favour of `cancelCraftingJob`.
- The inline sleep quality choice in `NeedActions.cpp`, in favour of `sleepQuality`.
- Bare tier numbers in C++ defaults (listed under Data), in favour of the named tiers.
- Sleep's restore at completion (`applyNeedEffect` for a Sleep), replaced by the per-tick payout.
- The `GlobalTaskAdapter` lookup of a Craft row's worker through colonists' current tasks, replaced
  by the job's crafter.
- The planned `CraftBinding` struct and job ids, which were never built: the "Craft binding
  (planned)" design in the claims spec is withdrawn in favour of this one.

Docs to bring in line as each piece lands: the tier numbers and section 3 in
[Colonist Task Arbitration](./colonist-task-arbitration.md) and in
[Priority Configuration](../design/game-systems/colonists/priority-config.md); the Craft chain,
interruption, bumping ("those actions are not interruptable"), known gaps, and testing sections of
[Task Claims](./task-claims.md), and section 6 of the arbitration spec ("interrupted only by a
strictly higher tier"); the urgent threshold in [Colonist Needs](../design/game-systems/colonists/needs.md); the status of
[Multi-colonist Crafting](../design/multi-colonist-crafting.md) section 2.

Tests that pin what changes and are rewritten, not kept beside new ones:

- `BuildAction.test.cpp` (Build is non-interruptable, line 59).
- `ActionSystem.test.cpp`: `CraftConsumesWoodFromStationStoreAndProducesOutput`,
  `CraftFurnitureOutputLeavesHandsEmptyAndFiresSpawnPackaged`,
  `CraftToolOutputStillRoutesToColonistInventory` (they complete a craft by running out the action's
  duration).
- `CraftingGoalSystem.test.cpp`: `MultiUnitCraftReprovisionsBetweenUnitsAndCompletesAllN` (bumps
  `completed` by hand).
- `ChainClaims.test.cpp`: `AStationsSlotIsHeldByTheCrafterThatSelectedIt` (gains the hold case).
- `AIDecisionSystem.test.cpp`: `TierDominanceWorkOrderBeatsNearerNeed`,
  `ActionCompletesWithoutReEvaluationInterrupt`.
- Every test that writes a tier number, in `AIDecisionSystem.test.cpp`, `ClaimIndex.test.cpp`, and
  `ChainClaims.test.cpp`.

---

## Testing

Unit and flow tests in `libs/sim`, alongside the suites named above. Check for an existing test of
the same path before adding one. A test recipe with a large `workAmount` stands in for the long
crafts the game does not have yet.

- A craft completes when the job's work reaches the recipe's amount, across any number of actions:
  clear the action half way, start another, and the unit completes with the inputs consumed once.
- An urgent need interrupts a crafter mid-unit; the job keeps its work and its crafter; a second
  colonist's Craft option at that station is Reserved; the crafter returns to it ahead of a nearer
  queued work order and finishes. The same with a critical need.
- A slightly hungry crafter does not leave a held craft, and does not leave a work order.
- A crafter pulled off the walk to a station by an urgent need holds nothing; a second colonist
  crafts the unit.
- Sleeping does not release a hold.
- Player control of the crafter releases the hold and keeps the work; a second colonist then
  continues from it and holds it. Destroying the crafter does the same.
- A crafter that starts a unit at a second station no longer holds the first.
- A job of two units: the hold ends with the first unit, and another colonist may craft the second.
- Cancelling a part-done job puts the full bill of materials beside the station as piles, empties
  the store, and ends the crafter's action without output. Cancelling a job with materials but no
  work does the same.
- An urgent need interrupts a builder; the blueprint keeps its work; another builder continues. A
  critical need interrupts a builder (it does not today).
- A very hungry colonist carrying a two-hand armful delivers it before eating; a critically hungry
  one drops it.
- A need classifies by its own thresholds (tested with a critical threshold that is not 10); a need
  crossing urgent decides at once, and not while the colonist is already serving an urgent need.
- A held craft whose station becomes unreachable is not offered; the crafter takes other work and
  still holds the unit.
- A cancel and re-queue of the same recipe under a running Craft action ends the action; nothing is
  produced from an empty store.
- Cancelling a job that is not the station's current one leaves the store alone.
- A returning crafter's own held station does not read Reserved to it.
- Sleep: half a sleep restores a quarter of its total at exponent 2; a full sleep restores the
  total; a woken sleeper keeps what it gained and its action does not run on under the next task.
- The validator's three new assertions, each with a test that breaks it.

In-game pass: a new scenario under `docs/testing/scenarios/`, "Crafter leaves and returns". Two
colonists, one crafting spot, one axe queued. Once `what=stations` shows `workDone > 0`, pause, set
the crafter's hunger to 20, resume. Expect: the crafter walks off and eats; the station keeps its
`workDone` and `crafter` and shows the overlay; the second colonist does not craft there; the
crafter returns and the axe appears. Then repeat with a cancel while the crafter is away and expect
the materials beside the station and an empty store. Scenario 01 gains one line: the station panel's
bar moves during the craft.

Performance: the index pass over stations and the per-tick work add are constant work per station
and per crafter. `Perf: none` is the expected line for every task here except the overlay, which
adds one sprite per station with work in progress.

---

## Build order

Sized for one agent each; the Specboard items under WOR-822 carry the detail.

1. Craft work saved on the job (rules 1 to 3). No behaviour change for today's recipes.
2. Need tuning as data, the urgent threshold, and the nine-tier ladder (rules 9, 12).
3. Craft, Build, and Deconstruct interruptable (rule 11). Needs 1 and 2, and WOR-820.
4. The crafter's hold (rules 4 to 8, 10): WOR-633. Needs 1 and 2.
5. Cancelling returns the materials (rule 13). Needs 1.
6. Sleep's curve (rule 14). Independent.
7. Panels, trace, and the dev view. Needs 1 and 4.
8. The work-in-progress overlay and the Crafting Spot's art. Needs 1.
9. The in-game scenario, the doc updates listed under Migration, and the development log entry.

---

## Out of scope

- Skill, tools, or station quality changing craft speed. The work rate is one constant; a rate per
  colonist would replace it at the one place work is added.
- Item quality and authorship. Nothing here depends on who made a thing.
- Two colonists working one unit together.
- Long cuts. Harvest is the remaining long action that nothing can interrupt, and its progress is
  on the action. Felling a large tree has the flaw this spec removes from crafts; it wants the same
  treatment and its own item.
- Bumping. Another colonist's needs or orders never take a station from its crafter.
- An ordinary sleeper being woken for a work order. It happens today (tier order), and with the
  curve it now wastes most of the sleep. Whether sleep should resist that is a tuning question for
  the needs work.
- Station policy and the multi-colonist crafting UI beyond what is listed here (WOR-383).
- Unifying the two craft task paths (WOR-295). The Craft option this spec changes is the in-trace
  one; whichever path offers it, the rules are the same.
- Save and load, beyond naming the two fields that will be saved.

---

## How other games do it

Player-report and wiki level; no source code was read, and several claims could not be confirmed.

- **RimWorld** keeps progress on an unfinished item beside the bench, bound to the pawn that started
  it for recipes with an author. Others start a fresh item. The pawn returns only if the bill is
  still its best job, so players force it by hand; a dead author leaves the item behind for good,
  and unfinished items pile up on benches. Construction frames hold their own progress and any
  builder continues. Rest is gained in intervals as the pawn sleeps.
- **Oxygen Not Included** has duplicants see to personal needs when they arise, over any errand
  short of a red alert.
- **Dwarf Fortress** keeps the task on the workshop, open to any dwarf with the labour. Whether
  part-done work carries over was not confirmed.

What this spec takes: progress on the job, where it survives the worker (RimWorld's frames, not its
unfinished items); a hold that is released when the crafter is gone, so nothing is orphaned; and a
tier for the crafter's own unfinished work, since ordinary priority demonstrably does not bring a
worker back.

---

## Related documents

- [Multi-colonist Crafting](../design/multi-colonist-crafting.md): station ownership during a
  craft, the decisions this implements.
- [Task Claims](./task-claims.md): the Slot claim, the claim index, interruption by needs, the
  suspended record.
- [Colonist Task Arbitration](./colonist-task-arbitration.md): the (tier, score) key and the ladder
  this renumbers.
- [Crafting & Material Provisioning](./crafting-provisioning-architecture.md): the station store
  and the exact-bill rule.
- [Building & Construction Architecture](./building-construction-architecture.md): blueprint work,
  the model craft work now follows.
- [Colonist Needs](../design/game-systems/colonists/needs.md): thresholds and decay.
- [Priority Configuration](../design/game-systems/colonists/priority-config.md) and
  `assets/config/work/priority-tuning.xml`: tiers as data.
