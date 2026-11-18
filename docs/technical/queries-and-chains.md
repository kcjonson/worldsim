# Queries and chains

Epics: WOR-919 (Colonist mood: low-mood behaviors, refusal, and colony failure), WOR-1056 (needs as
data), WOR-951 (behavior definitions as data)

Everything a creature does on purpose is defined in data and run by two generic mechanisms. A
**query** answers "which thing, for this creature, for this purpose": which food, which bed, which
pile to haul from, which builder spot, who to snap at. A **chain** is an ordered list of steps the
creature carries out: go there, do this, give that thought. Needs, actions, colony work, idle
behavior, the player's orders, and low-mood behaviors are XML entries built from them, and the C++
provides the engine they run on: the decision loop, arbitration, claims, supply read from the world,
routing, the hands rules, and the effects an action can have.

---

## Scope

### The end state

A creature's needs are entries, each naming what satisfies it. Each thing it can do is a chain, its
targets picked by queries, and the decision loop offers it as an option: a need's chain at the need's
tier, a piece of colony work at the work's tier, an idle chain, a player's order, a low-mood behavior.
Each `do` step performs an action, and an action is an entry that names engine-provided effects with
the values to use. Adding a need, a kind of work, an action, or a behavior is adding entries; nothing
about what a creature can do is a C++ list.

### Retired

The code that hard-wires what a creature can do and how each thing is done:

- `TaskType` and `ActionType` as closed lists of behaviors, and the names built on them
  (`assets::kTaskTypeNames`, `kActionTypeNames`, `<TaskTiers>` in `priority-tuning.xml`).
- The per-need and per-task-type option builders in `AIDecisionSystem`: the needs loop and its hunger
  special case, `evaluateHaulOptions`, `evaluateCarryInOptions`, `evaluateHarvestOptions`,
  `evaluateBuildOptions`, `evaluateDeconstructOptions`, `evaluatePlacePackagedOptions`, the craft
  station options, the wander and gather-food options, and the per-type rules in `classifyTier`.
- The per-type start routines and effect variants in `ActionSystem` and `action/*.cpp`
  (`startNeedAction`, `startHarvestAction`, `startHaulAction`, `startCraftAction`, `startBuildAction`,
  `startPlacePackagedAction`), the `Action` factories, and the `ActionEffect` variant of per-type
  effect structs.
- The hand-written leg sequencing: `Task::chainStep`, `ActionSystem::headToDeposit`,
  `ActionSystem::carryHarvestIn`, and the PlacePackaged legs.
- The per-type fields on `Task` (`needToFulfill`, `harvestTargetKey`, `craftRecipeDefName`,
  `haulItemDefNameId`, `placePackagedEntityId` and the rest), replaced by the chain's progress and its
  step targets.
- The per-chain-shape claim recording in `ChainClaims.cpp` (`recordChainClaims`), replaced by claims
  named on steps.
- The fixed need list: `NeedType`, `kNeedLabels`, the fixed-size array in `NeedsComponent`,
  `kActionableNeeds`, `needToCapability`, `getFirstActionDefName`, the decay literals in `Needs.cpp`,
  `findToiletLocation`, whose rules become facts of a query, and every `switch` over a need.
- The config that described this without being run: `assets/config/work/task-chains.xml` with
  `TaskChainRegistry`, `assets/config/work/work-types.xml` with `WorkTypeRegistry`, and
  `assets/config/actions/action-types.xml` with `ActionTypeRegistry`, of which only `needsHands` on a
  task's first action is read.

### Kept

The engine mechanisms the data-defined system runs on:

- The decision loop and its cadence (`AIDecisionSystem::shouldReEvaluate`, `kReEvalInterval`), the
  (tier, score) key, the hysteresis margin, and the switch gate
  ([Colonist task arbitration](./colonist-task-arbitration.md)).
- The claim kinds, `Task::claims`, `instanceId`, `claimSeq`, `SuspendedWork`, the claim index
  (`ClaimIndex`), and its development-build validator ([Task claims](./task-claims.md)).
- Supply read from the world: `GoalTaskRegistry::remainingNeed` and its per-update caches
  (`RemainingNeedCache`, `BoxRoomCache`, `ColonyStorageKnowledge`), and `LoosePileIndex`.
- The goal systems that say what colony work exists and how much (`StorageGoalSystem`,
  `CraftingGoalSystem`, `BuildGoalSystem`, `ConstructionSystem`'s goals) and `GoalTaskRegistry`. The
  kinds of goal they create stay a C++ list, `GoalKind`, separate from what a creature does.
- Memory and discovery gating, the colony's shared storage knowledge, routing (route tickets, the
  nearest-of request, `SourceReachMemo` verdicts, connectivity), stand points beside obstacles, and
  the arrival and reach rules in `ActionSystem`.
- Hands and inventory: stow or drop (`storeOrDrop`), `makeRoomToEat`, the two-hand rules, packaged
  items, provisions.
- The effects an action can have, as the primitives listed under [Actions](#actions): what is in
  `applyNeedEffect`, `applyConsumptionEffect`, `applyCollectionEffect`, `applyDepositEffect`,
  `applyProgressEffect`, `applyCraftingEffect`, `applySpawnEffect`, and `applyPlacePackagedEffect`,
  reshaped to take their values from an action entry.
- Thoughts, mood, the condition language, and the decision trace.

### Where the line is drawn

What a creature does, in what order, and with what values is data. What every doing relies on is
engine: whether a target is known, free, reachable, and still there; how much of it is wanted; what
the hands may hold; when to decide again. The code already splits this way below the option builders.
The `apply*Effect` functions are close to primitives already, taking their values from the effect
struct; the start routines mix two jobs, a per-type choice of effect (which goes to data) and the
checks every start needs (arrival, a target still in memory, a stale entry forgotten, a start that
starts nothing ending the task), which become one start path for every `do` step. Likewise the option
builders mix a per-type choice of target (which goes to queries and work entries) with claims, supply,
reach, and gathering (which become services every query and step asks for).

Goal kinds stay in C++ because the goal systems that create them stay: a craft goal exists because a
station has a queued job, and that is world state the engine owns. What is done about a goal is data.

### Config files

| File | End state |
|---|---|
| `assets/config/work/task-chains.xml`, `TaskChainRegistry` | deleted; chains are in `assets/config/chains/` |
| `assets/config/work/work-types.xml`, `WorkTypeRegistry` | deleted; the work categories and their order move into `assets/config/work/` as `<category>` entries beside the `<work>` entries, and `triggerCapability`, `taskChain`, and the filters have no successor because goal systems say what work exists |
| `assets/config/actions/action-types.xml`, `ActionTypeRegistry` | replaced by `<action>` entries in `assets/config/actions/`; `needsHands` becomes the entry's `hands` |
| `assets/config/colonists/needs/needs.xml` | replaced by `<need>` entries in `assets/config/needs/`, which also carry the decay and the mood cost |
| `assets/config/colonists/mood.xml` | keeps the penalty curve, the easing, and the bands; `<NeedWeights>` goes, since each need carries its own cost |
| `assets/config/work/priority-tuning.xml` | `<TaskTiers>` becomes `<Tiers>`, the named ladder in order; `<WorkCategoryOrder>` goes to the categories; the within-tier bonuses stay |

`work-types.xml`'s categories are kept because [Work Priorities](../design/game-systems/colonists/work-priorities.md)
needs them: the player turns a category on or off for a colonist and, in detailed mode, sets a 1 to 9
priority per work type. A work entry names its category, and a category is the unit those settings
attach to. The per-colonist priority is a within-tier score factor for the work it covers, as
[Colonist task arbitration](./colonist-task-arbitration.md#work-priorities) records.

---

## Data rule

Every need, action, query, chain, work entry, order, and creature kind is an XML entry that works on
its own for the simple case, as an asset definition does. XML holds no control flow: no branching and
no loops. A chain is a straight list of steps; a query is a list of weighted facts.

One yes-or-no test that picks whether a row of data applies is allowed on a row, written as `when=` in
the [condition language](../design/game-systems/colonists/mood.md#conditions), the way thought feelings
and behavior weights already use it. A row whose `when` is false or Unknown does not apply. A `when` is
never on the entry as a whole and never chooses between steps.

An entry may name a Lua script for rich expression, the way an asset definition names a generator,
with the entry's own values passed to it. The format reserves an optional `script` attribute on
`<query>` and on `<chain>` for this: a path relative to the entry's file, whose script takes the entry's
values as a table. No loader reads scripts. An entry that gives `script` is a load error naming the
file, the entry, and the attribute as reserved, so no mod ships a script that is silently ignored, and
a later hook adds meaning to the attribute without changing any entry that works.

### Files

| Folder | Root | Entries |
|---|---|---|
| `assets/config/needs/` | `<needs>` | `<need>` |
| `assets/config/actions/` | `<actions>` | `<action>`, `<actionGroup>` |
| `assets/config/queries/` | `<queries>` | `<query>` |
| `assets/config/chains/` | `<chains>` | `<chain>` |
| `assets/config/work/` | `<work>` | `<category>`, `<work>` |
| `assets/config/orders/` | `<orders>` | `<order>` |
| `assets/config/creatures/` | `<creatures>` | `<creature>` |

Each folder loads its own files first, then its subfolders' in path order, as
`assets/config/conditions/` does ([Config loading](./config-loading.md)). A later file's entry with the
same `id` (or `name`, for a need) replaces the earlier one and is logged with both files; the same id
twice in one file is an error. The needs load first, because their names fill the condition
vocabulary's owned `need` set; then the condition vocabulary; then every other folder is parsed, and
the references between them (a need naming a chain, a chain naming a query or an action, a work entry
naming a category) are resolved once all are read, so no folder order matters for them. Any problem
in a file (an unknown element or attribute, a name that does not exist, a bad number) loads nothing
from it. Errors are prefixed with the folder's name (`[needs]`, `[actions]`, `[queries]`, `[chains]`,
`[work]`, `[orders]`, `[creatures]`) and reach the loading scene's failure view like the colonist
rules' errors.

---

## Needs

A need is an entry. Which needs a creature has is its creature kind's list, so a colonist and a cow
have different needs and a mod adds one with a file.

### A need entry

```xml
<needs>
  <need name="Hunger" seek="50" urgent="25" critical="10" moodCost="25">
    <drains perGameMinute="0.08"/>
    <satisfiedBy chain="eat_a_meal"/>
  </need>
</needs>
```

- `name` is what the player sees and what every reference uses: a thought's `need`, a creature
  kind's list, a `<restore>` effect, and the condition term `need(...)`, which takes the name in its
  condition form (`need(hunger)`), as it does for every owned name.
- `seek`, `urgent`, and `critical` are the three levels, need percentages, each below the one before
  ([Interruptable crafts](./interruptable-crafts.md#needs-in-three-levels)).
- `moodCost` is the most mood the need takes off at its worst
  ([Needs](../design/game-systems/colonists/needs.md#need-to-mood-weights)). A creature with no mood
  ignores it.
- `<satisfiedBy>` names what meets it (below). A need with none is tracked, costs mood, and is never
  acted on.

### How a level moves

A level is how well the need is met: 100 is fully met and 0 is empty. A need that builds up in the
creature (a bladder, a cow's udder) is written the same way, its level falling as the pressure grows,
so every need reads alike to the AI, the mood curve, and the player.

A need moves in one of two ways, and an entry gives exactly one:

- `<drains perGameMinute="r"/>`: the level falls by `r` each game minute, clamped at 0.
- `<setBy provider="..."/>`: the level is set each tick by a provider the sim implements in C++, an
  enumerated set like the facts, and nothing drains it. Temperature is set this way by the creature's
  surroundings. A provider the sim declares and nothing answers holds the need at full.

A drain may carry rate rows, each a multiplier with one `when`, all that apply multiplying together:
`<rate when="current_task(work)" multiply="1.5"/>`. Anything else that moves a level is an action's
effect (drinking fills the bladder) or a chain's `<need>` step.

### Levels and tiers

Below `seek` the need's option is an ordinary need; below `urgent`, an urgent need, which interrupts
work; below `critical`, a critical need, which interrupts nearly everything
([Tiers](#tiers)). Every need with a `<satisfiedBy>` acts at all three, the ones that cannot kill
included, so a colonist leaves a long job to wash or to rest before a low-mood behavior over it starts
([Needs](../design/game-systems/colonists/needs.md#need-to-mood-weights)). Tuning a need's `urgent`
level against the mood its cost takes off is how meeting the need is kept ahead of the behavior.

A creature decides at once when a need crosses into urgent or into critical, unless it already serves
an urgent or critical need (requirement 5).

### What satisfies a need, and who

`<satisfiedBy chain="..."/>` names a chain the creature runs itself. A need may list several, each with
an optional `when`; each that applies is its own option at the need's tier, and arbitration picks
between them. The chain's first step's query finds the target, so the search for a need is a query and
nothing else.

`<satisfiedBy work="..."/>` names a work entry another creature does: the need's holder cannot meet it
itself. While the holder's level is below `seek`, a care goal for that need exists on it (`GoalKind`
care, made by the care goal system, one engine system for every need met this way), and the work entry
offers it to creatures that take colony work. The tier of that work follows the holder's level: an
urgent or critical cow's milking is a work order, and a cow below seek is opportunistic work.

### Creature kinds

```xml
<creatures>
  <creature id="colonist" def="Colonist" mood="true" colonyWork="true">
    <need name="Hunger"/>
    <need name="Thirst"/>
    <need name="Energy"/>
    <need name="Bladder"/>
    <need name="Digestion"/>
    <need name="Hygiene"/>
    <need name="Recreation"/>
    <need name="Temperature"/>
    <idle chain="gather_food" when="carried_food &lt; 1"/>
    <idle chain="wander"/>
  </creature>
</creatures>
```

`def` is the mover's asset definition. A creature has the needs its kind lists, in that order, each
starting full. `mood` says whether it has a mood (and so whether need costs, thoughts' mood effects,
and low-mood behaviors apply), and `colonyWork` whether work entries are offered to it. `<idle>` rows
are its idle chains ([Idle and player orders](#idle-and-player-orders)).

### The shipped needs

```xml
<needs>
  <need name="Hunger" seek="50" urgent="25" critical="10" moodCost="25">
    <drains perGameMinute="0.08"/>
    <satisfiedBy chain="eat_a_meal"/>
  </need>
  <need name="Thirst" seek="50" urgent="25" critical="10" moodCost="25">
    <drains perGameMinute="0.12"/>
    <satisfiedBy chain="drink_at_water"/>
  </need>
  <need name="Energy" seek="30" urgent="20" critical="10" moodCost="20">
    <drains perGameMinute="0.05"/>
    <satisfiedBy chain="sleep"/>
  </need>
  <need name="Bladder" seek="30" urgent="20" critical="10" moodCost="10">
    <drains perGameMinute="0.03"/>
    <satisfiedBy chain="relieve_self"/>
  </need>
  <need name="Digestion" seek="30" urgent="20" critical="10" moodCost="10">
    <drains perGameMinute="0.02"/>
    <satisfiedBy chain="relieve_self"/>
  </need>
  <need name="Hygiene" seek="40" urgent="25" critical="15" moodCost="15">
    <drains perGameMinute="0.015"/>
    <satisfiedBy chain="wash_at_water"/>
  </need>
  <need name="Recreation" seek="30" urgent="20" critical="10" moodCost="15">
    <drains perGameMinute="0.01"/>
    <satisfiedBy chain="enjoy_nature"/>
  </need>
  <need name="Temperature" seek="40" urgent="25" critical="15" moodCost="40">
    <setBy provider="thermal_comfort"/>
  </need>
</needs>
```

Temperature has no `<satisfiedBy>` until there is a chain that seeks warmth or shade, and
`thermal_comfort` is unanswered until the temperature of a place exists, so the need holds at full.

```xml
<chains>
  <chain id="drink_at_water">
    <go query="water_nearby"/>
    <do action="Drink"/>
  </chain>
  <chain id="wash_at_water">
    <go query="water_nearby"/>
    <do action="Wash"/>
  </chain>
  <chain id="sleep">
    <go query="place_to_sleep"/>
    <do action="Sleep"/>
  </chain>
  <chain id="relieve_self">
    <go query="place_to_relieve"/>
    <do action="Toilet"/>
  </chain>
  <chain id="enjoy_nature">
    <go query="pleasant_spot"/>
    <do action="EnjoyNature">
      <ends when="need(recreation) > 90"/>
    </do>
  </chain>
</chains>

<queries>
  <query id="water_nearby">
    <from capability="drinkable"/>
    <weigh fact="distance" by="-1"/>
  </query>
  <query id="place_to_sleep">
    <from kind="bed"/>
    <from place="here"/>
    <weigh fact="distance" by="-0.2"/>
    <weigh fact="comfort" by="5"/>
  </query>
  <query id="place_to_relieve">
    <from kind="latrine"/>
    <from place="around" radius="20" count="16"/>
    <exclude fact="on_shore"/>
    <weigh fact="distance" by="-0.3"/>
    <weigh fact="near_waste" by="2"/>
    <weigh fact="near_food" by="-4"/>
  </query>
  <query id="pleasant_spot" pick="weighted">
    <from place="around" radius="30" count="8"/>
    <exclude fact="distance" above="30"/>
    <weigh fact="distance" by="-0.1"/>
  </query>
</queries>
```

Drinking and washing share the shore query and are separate chains, so a colonist low on both takes
the critical one first, else the one written first in its creature kind's need list
([AI behavior](../design/game-systems/colonists/ai-behavior.md#tier-5-actionable-needs)).

### The cow

```xml
<needs>
  <need name="Milking" seek="40" urgent="20" critical="5" moodCost="0">
    <drains perGameMinute="0.02"/>
    <satisfiedBy work="milk"/>
  </need>
</needs>

<creatures>
  <creature id="cow" def="Cow" mood="false" colonyWork="false">
    <need name="Hunger"/>
    <need name="Thirst"/>
    <need name="Milking"/>
    <idle chain="graze"/>
  </creature>
</creatures>

<work>
  <work id="milk" goal="care" need="Milking" category="Farming">
    <chain id="milk_a_cow"/>
  </work>
</work>

<chains>
  <chain id="milk_a_cow">
    <go to="destination"/>
    <do action="Milk" claims="use"/>
  </chain>
</chains>
```

The care goal's destination is the cow. `Milk` restores the target's Milking and puts the milk in the
milker's hands; the milk is then carried to storage by the carry-in work like any goods. A cow is a
mover, so the claim is on a mobile target, and the go step ends with `target_left` when the cow walks
off ([Task claims](./task-claims.md#built-now-and-planned), mobile targets).

### What replaces the fixed list

| Retired | Replaced by |
|---|---|
| `NeedType`, `kNeedLabels`, `needLabel` | a need id interned at load, an index into the loaded need list, with the entry's `name` |
| `NeedsComponent`'s array of every need | the creature's own list: its kind's need ids and their values, inline with a fixed capacity of 16, so no creature's needs are on the heap; thresholds and rates are read from the entries |
| `kActionableNeeds` | a need with a `<satisfiedBy>` |
| `needToCapability`, the Hunger special case | the satisfying chain's queries |
| `startNeedAction`, `getFirstActionDefName` | the chain's `do` steps and the action entries' `hands` |
| `Task::needToFulfill`, `criticalNeedsAtDecision` as a bit per `NeedType` | the option's need id, and a bit per slot in the creature's need list, for urgent and for critical |
| the sim filling the condition vocabulary's `need` set from `kNeedLabels` | the loaded needs' names |
| thought triggers `needFalls` and `needRises` checking `kNeedLabels` | the same check against the loaded needs |
| `<NeedWeights>` in `mood.xml` | each need's `moodCost` |
| the decay literals in `Needs.cpp`, `assets/config/colonists/needs/needs.xml` | each need's `<drains>` and levels |

---

## Actions

An action is what a `do` step performs where the creature stands. The engine provides a fixed set of
effects in C++; an action entry is a named use of them, with the values to use, how long it takes,
whether it needs free hands, whether it may be interrupted, and when it pays.

### Primitive effects

| Effect | Does | In the code it comes from |
|---|---|---|
| `<restore need amount>` | moves a need by a signed amount, the creature's own or, with `who="target"`, the target creature's. `amount` is a number, or `from` names a property of what the step consumes (`nutrition`) with `scale`, or `scaleBy` names a fact of the step's target (`sleep_quality`) | `applyNeedEffect`, the need halves of `applyConsumptionEffect` |
| `<consume count>` | uses up held units of what the step holds | `applyConsumptionEffect` |
| `<take>` | takes from the target into hands or pack: cuts a harvestable (its yield, its regrowth), picks up from a pile, withdraws from a box, lifts a packaged item. How much is the step's amount ([Amounts](#amounts)); `clears="true"` removes the source whatever it is, for a site clear | `applyCollectionEffect` |
| `<put>` | puts what the step carries into the target: a box, a station's store, a blueprint's manifest, metered to what the target still needs and room for | `applyDepositEffect` |
| `<work rate>` | advances the work saved on the target (a blueprint's `workDone`, up or down with `direction`, or a crafting job's `workDone`) at the rate the named skill gives; the thing's own completion (a structure finished, a craft unit made) belongs to the thing | `applyProgressEffect`, `advanceConstructionWork`, `applyCraftingEffect` |
| `<spawn def>` | creates a thing where the creature stands (a bio pile), or with `into="hands"` in its hands | `applySpawnEffect`, the craft output to inventory |
| `<install>` | puts the packaged item the creature carries down at its placement target and unpackages it | `applyPlacePackagedEffect` |
| `<damage amount>` | damages the target | none; added with the first behavior that damages |

Two primitives go beyond the list the effects were designed from: `install`, because placing a
packaged item is neither a put nor a spawn, and `spawn into="hands"`, because a craft's tool output and
the cow's milk land in hands rather than on the ground. Giving a thought is not an action effect: a
chain gives one with a `<think>` step, and a thought type reacts to an action with its own
`actionStarts` and `actionFinishes` sources ([Thoughts](../design/game-systems/colonists/thoughts.md)).

Every effect row may carry one `when`. Effects apply in the order written.

### An action entry

```xml
<actions>
  <action id="Eat" seconds="2" hands="free" interruptable="false">
    <consume count="1"/>
    <restore need="Hunger" from="nutrition" scale="100"/>
    <restore need="Digestion" from="nutrition" scale="-100"/>
  </action>
</actions>
```

- Duration is one of `seconds`, a fixed time (or `seconds` on each effect row, added over the rows
  that apply); `workFrom="durability" skill="Harvesting"`, the
  target's durability over the work rate that skill gives; or nothing, with a `<work>` effect, so the
  action runs until the thing's work reaches its bound.
- `hands="free"` means the action needs free hands, and the hands rules run before it starts
  ([Hands](#hands)); `hands="any"`, the default, means the creature keeps carrying.
- `interruptable` is whether a running action yields to an option of the same or a higher tier; one
  that is not yields to nothing until it ends (requirement 4).
- `pays="end"`, the default, applies the effects when the action completes. `pays="along"` applies
  them as it goes, so an interrupted action keeps what it earned.

### Paying out as it goes

With `pays="along"` each tick applies the share of the action's effects earned since the last tick. A
`curve` shapes it: by the fraction `f` of the action done the creature has earned `total x f^curve`,
so `curve="1"` (the default) pays evenly and `curve="2"` pays little at first and more toward the end,
the sleep curve in [Interruptable crafts](./interruptable-crafts.md#sleep-pays-out-along-a-curve). A
whole action earns the whole total and completion adds nothing. An open-ended action (one with a
`<work>` effect) pays along by its nature, its work saved on the thing worked.

A long, low-value activity relies on this: enjoying nature restores a little Recreation over a long
time, and a colonist pulled away by hunger keeps what it gained.

### Action groups

```xml
<actionGroup id="Take">Harvest Pickup Withdraw</actionGroup>
```

Each action entry may declare the kind of target it works on with `on` (a capability, `storage`, or
`loose_item`). A `do` step that names a group performs the member whose `on` matches its target. This
is how one food query over bushes, piles, and boxes is followed by one take step: the target the query
picked is a bush, a pile, or a box, and the group carries the action that goes with each. A group whose
members do not cover every kind its step's query can return is a load error naming the kinds left out.

### The shipped actions

| Action | Effects | Duration | Hands | Interruptable | Pays |
|---|---|---|---|---|---|
| Eat | consume 1; restore Hunger by nutrition x 100; restore Digestion by nutrition x -100 | 2 s | free | no | end |
| Drink | restore Thirst 100; restore Bladder -15 | 1.5 s | free | no | end |
| Wash | restore Hygiene 100 | 3 s | any | no | end |
| Sleep | restore Energy 60, scaled by the target's `sleep_quality` | 8 s | any | yes | along, curve 2 |
| Toilet | restore Bladder 100 when bladder is below seek or the chain serves Bladder; restore Digestion 100 and spawn BioPile when digestion is below seek or the chain serves Digestion | 2 s for the Bladder row and 3 s for the Digestion row, added | any | no | end |
| Harvest (`on` harvestable) | take | from durability, Harvesting | free | no | end |
| Clear (`on` harvestable) | take, clears | from durability, Harvesting | free | no | end |
| Pickup (`on` loose_item) | take | 0.5 s | free | no | end |
| Withdraw (`on` storage) | take | 0.5 s | free | no | end |
| Deposit | put | 1 s | any | no | end |
| Craft | work, Crafting, on the station's current job | open-ended | free | yes | along |
| Build | work, Construction, up | open-ended | free | yes | along |
| Deconstruct | work, Construction, down | open-ended | free | yes | along |
| PickupPackaged (`on` packaged) | take | 1.5 s | free | no | end |
| PlacePackaged | install | 2 s | free | no | end |
| EnjoyNature | restore Recreation 2 | 60 s | any | yes | along |

Craft, Build, and Deconstruct are interruptable and Sleep pays along its curve, as
[Interruptable crafts](./interruptable-crafts.md) specifies. Deposit keeps the hands as they are
because a delivery carries its goods in them. Toilet is the one action whose effects are chosen at its
start by the creature's levels: two `restore` rows, each with a `when` reading `need(...)` and the
term `serves(need)` (the chain was chosen for that need), so a colonist that walked to relieve one need
also relieves the other when it is low. Each row carries its own `seconds`, and the action lasts the sum
of the rows that apply.

---

## Queries

A query is three things: where candidates come from, what excludes a candidate, and a list of weighted
facts.

`score = sum over rows of (fact value x weight)`

The candidate with the highest score wins. A query is a weighting system, not a logic tree, and it is a
generic item query: food is one use of it, not its shape.

### Candidates

Candidates come only from what the creature knows: its memory, and the colony's shared knowledge where
that applies (the storage boxes the colony knows, `ColonyStorageKnowledge`). A query never learns of a
candidate from the world. It may read the live state of a candidate the creature already knows
(whether a bush is regrowing, a pile's count, who holds a claim on it), as the decision does through its
regrowth callback, `LoosePileIndex`, and the claim index.

A `<from>` row names one source of candidates, and a query may have several:

| Row | Candidates |
|---|---|
| `<from capability="edible"/>` | remembered things with that capability, as asset definitions spell it |
| `<from kind="bed"/>` | what a declared kind of the condition vocabulary maps to (`<Kind>` in `assets/config/conditions`): things under its capability, or movers of its definition seen within the recent-sighting window |
| `<from item="Berry"/>` | remembered things of one asset definition |
| `<from place="here"/>` | where the creature stands, as a place with no thing in it |
| `<from place="around" radius="r" count="n"/>` | `n` walkable points within `r` metres, drawn with the sim's seeded generator, on ground joined to the creature |
| `<from yields="Wood"/>` | remembered harvestables whose yield is the item (the registry's `harvestablesYielding`) |
| `<from pile="Wood"/>` | remembered loose piles of the item |
| `<from box="Wood"/>` | storage boxes the colony knows that hold the item |
| `<from goal="destination"/>` | the destination of the goal the work serves: a box, a station, a blueprint, a creature |
| `<from goal="spots"/>` | the builder spots of the goal's foundation |

In a work entry's queries, an item argument may be `goal`, the item the option is for
([A work entry](#a-work-entry)).

Every query also passes over, without rows of its own, what the decision already passes over: a target
with a route verdict ([AI behavior](../design/game-systems/colonists/ai-behavior.md#what-a-colonist-can-get-to)),
a source that is regrowing or exhausted, a target not joined to the creature on the nav mesh, and a
target claims take against the creature ([Claims and the query](#claims-and-the-query)).

### Facts

A fact is one number about this candidate, for this creature, at this moment. Facts are an enumerated
set provided by the sim in C++, like the condition terms, and each declares its range and its scope:
**definition** when it depends only on the candidate's asset definition and the creature, **item** when
it depends on the one thing. A fact may take one argument, as a condition term does (`contains(meat)`).
A fact the sim declares but nothing yet answers reads 0, and the load logs once that it is unanswered.

Every fact is also a condition term of the same name taking a target (`built_by_self(target)`), so a
behavior's roll multiplier can read what its query weighed.

The starting set:

| Fact | Scope | Range | Meaning | In the sim |
|---|---|---|---|---|
| `distance` | item | 0 m and up | straight-line metres from the creature | yes, from memory's positions |
| `liking` | definition | -1 to 1 | the creature's like or dislike of this kind of thing | waits on likes and dislikes ([Thoughts](../design/game-systems/colonists/thoughts.md#recalling-old-thoughts)) |
| `against_diet` | definition | 0 or 1 | the thing holds something the creature's diet rules out (a vegetarian and meat) | waits on diets and on ingredient tags on definitions |
| `contains(tag)` | definition | 0 or 1 | the definition carries the ingredient tag | waits on ingredient tags |
| `eaten_recently` | definition | 0 to 1 | the share of the creature's recent meals that were this kind of thing | waits on a record of meals |
| `quality` | definition | 0 to 1 | the edible capability's `quality`, Terrible 0 to Excellent 1 in steps of 0.25 | yes, `CapabilityQuality` on the definition |
| `freshness` | item | 0 to 1 | 1 fresh, 0 spoiled | waits on spoilage; definitions carry only `spoilable` |
| `maker_opinion` | item | -1 to 1 | the creature's opinion of who made it, 0 when nobody did or the maker is unknown | waits on items recording a maker, and on relationships |
| `fits_meal_time` | definition | 0 or 1 | the definition's `meal` tag names the meal of the hour (breakfast, lunch, supper) | waits on the `meal` tag; the hour exists (`TimeSystem`) |
| `comfort` | definition | 0 to 1 | how comfortable a place to sit, sleep, or eat is | waits on furniture comfort |
| `sleep_quality` | item | 0 to 1 | how well the creature sleeps there: 0.5 on the ground, 1 at any other target, the factors of [Interruptable crafts](./interruptable-crafts.md#sleep-pays-out-along-a-curve) multiplied in | yes, the inline choice in `NeedActions.cpp` |
| `opinion` | item | -1 to 1 | for a colonist candidate, the creature's opinion of them | waits on relationships |
| `mood_source` | item | -1 to 1 | for a colonist candidate, how much of how the creature feels right now is about them, and which way: the held mood effects whose thought has that colonist as its subject, as a share of all it holds, below 0 when they have lowered its mood and above 0 when they have lifted it | yes, from the subjects of held thoughts; few thought types name a colonist until relationships exist |
| `owner_opinion` | item | -1 to 1 | the creature's opinion of the thing's owner, 0 when unowned | waits on ownership and relationships |
| `has_owner` | item | 0 or 1 | the thing has an owner | waits on ownership |
| `owned_by_self` | item | 0 or 1 | the creature owns it | waits on ownership |
| `owner_present` | item | 0 or 1 | the owner was seen beside it within the recent-sighting window | waits on ownership |
| `built_by_self` | item | 0 or 1 | the creature built it | waits on structures recording who built them |
| `flammable` | definition | 0 or 1 | the thing burns | waits on fire |
| `colony_stock` | definition | 0 and up | how many of this definition the colony knows it holds | waits on scarcity |
| `on_shore` | item | 0 or 1 | a place within 1.5 m of water | yes, `findToiletLocation`'s shore rule |
| `near_waste` | item | 0 to 1 | how close remembered waste (a bio pile) is, 1 beside it | yes, `findToiletLocation`'s clustering |
| `near_food` | item | 0 to 1 | how close remembered food is, 1 beside it | yes, `findToiletLocation`'s food penalty |
| `held` | item | 0 or 1 | another task holds a claim on it that does not take it from this creature (food held by a less urgent eater or by work) | yes, the claim index's filtered holder lookups |
| `below_destination` | item | 0 or 1 | for a box candidate, its priority for the goal's item is strictly below the destination's | yes, the storage pull gate |

Context goes into facts, not branching. Eggs in the morning is a `meal` tag on the egg's definition and
one fact, `fits_meal_time`; no query asks what hour it is.

### Rows

```xml
<queries>
  <query id="food_to_eat">
    <from capability="edible"/>
    <from pile="edible"/>
    <from box="edible"/>
    <exclude fact="against_diet"/>
    <weigh fact="distance" by="-0.1"/>
    <weigh fact="held" by="-1000"/>
    <weigh fact="liking" by="8"/>
    <weigh fact="eaten_recently" by="-3"/>
    <weigh fact="quality" by="4"/>
    <weigh fact="freshness" by="3"/>
    <weigh fact="maker_opinion" by="6"/>
    <weigh fact="fits_meal_time" by="2"/>
    <scale trait="gourmand" fact="quality" by="2"/>
  </query>
</queries>
```

- `<weigh fact by>` adds the fact times `by` to the score. `by` is a signed number. It may carry one
  `when` for a one-off (`<weigh fact="comfort" by="3" when="need(energy) &lt; 30"/>`).
- `<exclude fact>` removes a candidate whose fact is above 0; `above="v"` or `below="v"` moves the line
  (`<exclude fact="distance" above="15"/>`). An exclusion on a definition-scoped fact removes every item
  of that definition at once. It may carry one `when`.
- `<scale trait fact by>` multiplies that fact's weight by `by` for a creature with the trait. Several
  scale rows that match multiply together. A trait the trait registry lacks is a load error, as it is in
  a condition.

A query weighs `distance` with a negative weight or excludes it above a bound; one that does neither is
a load error, since nothing would end its search (see [Evaluation](#evaluation)).

`held` weighed by -1000 is the food rule of [Task claims](./task-claims.md#need-tasks) in weights: food
nobody holds is taken over held food within the 10 km a distance weight of -0.1 spans, and held food is
still taken when nothing free is known.

### Picking: the best, or a weighted draw

A query picks in one of two ways, stated on the entry.

- `pick="best"`, the default: the highest score wins. A food search takes the best.
- `pick="weighted"`: every surviving candidate has a chance, and a higher score makes it likelier, not certain. Each candidate's chance is in proportion to its score less the lowest surviving score, plus 1, so the worst candidate still has a small chance and the best has the largest. The draw uses the sim's seeded generator, so the same game gives the same pick.

A behavior's target is a weighted pick: who a colonist lashes out at is a tendency, not a rule.

A weighted query draws from a set, so it cannot stop at the first winner. It must exclude `distance` above a bound, which keeps the set small, and it draws from at most the 16 best candidates. One that does not bound distance is a load error.

### Creatures differ through facts and traits

Per-creature differences come from the facts reading the creature (its likes, its opinions, its meals)
and from traits scaling weights: a gourmand doubles the quality weight. A different kind of creature has
its own queries: a vulture's food query weighs `freshness` by 0, or below it, where a colonist's weighs
it up.

### Worked example: a long walk for a friend's stew

A colonist knows berries 5 m away and a stew their best friend made, their favourite dish, 100 m away,
with the weights above.

| Term | Berries, 5 m | Stew, 100 m | Stew, 300 m |
|---|---|---|---|
| distance x -0.1 | -0.5 | -10 | -30 |
| liking x 8 | 0 (no feeling) | 8 (favourite) | 8 |
| quality x 4 | 2 (Normal) | 3 (Good) | 3 |
| freshness x 3 | 3 | 3 | 3 |
| maker_opinion x 6 | 0 (nobody made them) | 6 (best friend) | 6 |
| Score | 4.5 | 10 | -10 |

At 100 m the stew wins; at 300 m the berries do. Its other terms come to 20 against the berries' 5, so
the stew wins out to 155 m, where `20 - 0.1 x 155 = 4.5`. A gourmand weighs quality by 8: the stew's
other terms come to 23 against the berries' 7 less 0.5, and the walk stretches to 165 m.

### Evaluation

The case to design for is a creature that knows 10,000 edible items (`Memory::kMaxWorldEntities` is
10,000). A script called on each of them per decision is what this design avoids.

1. **Score definitions, not items.** The terms whose facts are definition-scoped are computed once per
   definition and cached per creature and query, stamped with what they read (the creature's likes, its
   meal record, the meal of the hour, its traits), and recomputed only when a stamp moves. An exclusion
   on a definition-scoped fact removes every item of that definition before any is visited, and the
   work gates that depend on a definition (room to carry its yield, the tool to cut it, a box's room
   for it) are answered there, before any of its items is visited.
2. **Walk nearest first and stop early.** Candidates are visited nearest first through a spatial index
   per definition over what the creature remembers, kept at the same remember and forget points as
   `Memory::worldEntitiesByDef`. Because the distance weight is declared and every other fact has a
   declared range, the engine knows for each definition the best score an item of it could reach apart
   from distance: its cached definition terms plus, for each item-scoped fact, the larger of the weight
   times the range's ends. That bound less the best score found so far, divided by the distance weight's
   size, is the farthest an item of that definition could still win from. The walk passes over an item
   beyond its definition's horizon and ends when the next candidate is beyond every horizon. A favourite
   definition is searched farther than a plain one.
3. **Per-item facts only for survivors.** An item-scoped fact other than distance is read only for a
   candidate inside its definition's horizon that no exclusion removed.
4. **Keep a chosen target.** A creature heading for a target a query chose keeps it while it is valid;
   a query is not run again at every decision. It is run again when the target fails (gone, a route
   verdict, claimed against it) or when the step that uses it starts again.
5. **Straight-line distance for scoring.** Reach is the component-label compare; a route is asked only
   for the winner, or, for a need, for the nearest few survivors at once through the nearest-of route
   request, whose answer picks among them (`Task::sourceChoices`).
6. **No allocation in the steady state.** The walk, the definition cache, and the results use scratch
   buffers the query system owns and reuses across creatures, as `AIDecisionSystem` reuses its source
   scratch. Results are in a total order: score, then distance, then the memory key. Nothing depends on
   hash or view order.

### Claims and the query

A query run for a step that names a claim passes over every candidate whose claims take it from this
creature, the way it passes over a route verdict, with no row of its own: a Use target another task
holds, a pile or box with nothing left once others' source claims are taken out, a station slot or
builder spot another holds, a box with no room for the item once others' inbound is counted. The
creature's own task and suspended record are left out of what it reads (`ClaimExclusion`), so its own
claim never reads as taken (colony work requirement 3). For food the line between taken and held follows the
holder's tier, as [Task claims](./task-claims.md#need-tasks) says: an eater at least as urgent takes
it, and anything else only holds it, which the query weighs through `held`.

A candidate passed over this way is not lost from the decision trace: the first one in score order that
ranks ahead of every candidate offered becomes the option's Reserved row, naming its holder
([Task claims](./task-claims.md#reserved-rows-and-option-dedup)).

---

## Chains

A chain is a straight list of steps. A step is one of an enumerated set of step kinds provided in C++.

| Step | Does |
|---|---|
| `<go query="..."/>` or `<go to="..."/>` | walks to the target a query picks, or to a target the option already has: `target` (a behavior's or an order's), `source` (the work option's source), `destination` (the goal's destination) |
| `<do action="..."/>` | performs an action, or an action group's member for the target, where the creature stands, once, or again and again until one of its end conditions holds; `on` names the thing it works on when that is not the step it walked to (`on="destination"`) |
| `<have capability="..." fetch="..."/>` | a goal: done at once if the creature already holds a thing with the capability (or `item`), otherwise it runs the named chain, which gets one |
| `<think thought="..." to="self"/>` | gives a thought to `self`, `target`, or `witnesses` (every colonist who saw the step) |
| `<need name="..." change="..."/>` | moves one of the creature's needs by a signed amount |
| `<drop/>` | sets down what the chain's steps took, by the drop path (a box within reach that takes it, else a pile) |
| `<run chain="..."/>` | runs another chain as this step; chains nest |

A `do` names an action entry or an action group; one that does not exist is a load error. Taking a
thing and damaging one are actions (`Pickup`, `Harvest`, a damaging action of a behavior), so they
are `do` steps with the action's duration, hands, and arrival rules.

The owner's eating example: have food, go to an eating place a query finds (a dining table, a chair, the
floor), eat.

```xml
<chains>
  <chain id="eat_a_meal">
    <have capability="edible" fetch="fetch_food"/>
    <go query="place_to_eat"/>
    <do action="Eat">
      <ends when="need(hunger) > 95"/>
      <ends on="held_gone"/>
    </do>
  </chain>

  <chain id="fetch_food">
    <go query="food_to_eat"/>
    <do action="Take" claims="use source_quantity" amount="meal"/>
  </chain>
</chains>

<queries>
  <query id="place_to_eat">
    <from kind="dining_table"/>
    <from kind="chair"/>
    <from place="here"/>
    <weigh fact="distance" by="-0.2"/>
    <weigh fact="comfort" by="5"/>
  </query>
</queries>
```

The kinds `dining_table` and `chair` are `<Kind>` declarations the furniture adds; with none known the
query's only candidate is where the colonist stands, and it eats there. "Get food if you don't have it"
is the `have` step, with no condition in the XML. The take makes room in a full pack first, as every
take toward a meal does ([Hands](#hands)).

### Ending a step

A step ends on its own end conditions, the `ends` of the behaviors in mood.md. That is how "eat until
full or the food runs out" is one step and not a loop: the `do` performs Eat, and performs it again,
until the first `<ends>` holds. An `<ends>` is one of `after` (game time, `after="3h"`), `when` (a
condition), or `on`, an event the step kind reports: `held_gone` (what the creature holds for the step
is used up), `target_gone` (the target no longer exists or is no longer where it was), or `target_left`
(a creature target walked away). A `do` with no `<ends>` performs its action once, and a `do` whose
action is open-ended ends when the thing's work reaches its bound. A `go` ends on arrival.

### When a target is resolved

A step that needs a target runs its query when the step starts, not when the chain is chosen, because
the world may have changed in between. Two kinds of target are resolved at decision time instead: the
first step's, because the AI ranks options partly by distance and must know the chain can be started
at all, and every target a step names a claim on, because claims are taken at selection
([Claims on steps](#claims-on-steps)). For a work option that is its source and its destination. A
chain whose first step finds no target is not offered.

### Claims on steps

A `do` step names the claims it holds with `claims`, from the claim kinds of
[Task claims](./task-claims.md#claim-kinds): `use`, `source_quantity`, `slot`, `inbound`, and
`in_flight`. Each kind keys on what that document says it keys on: Use and source quantity on the step's
target, a slot on the goal's destination (numbered with the builder spot when the option's source is
one), inbound on the destination, in-flight on the root goal and the item. Use is for a target taken
whole (a harvestable, a packaged item, a creature) and source quantity for a pile or a box, so a step
whose query can return either names both, and its target holds the one that fits it.

- Every claim a chain names, in its own steps and in the chains its `have` steps would run, is taken
  when the option is selected, in the same update, through the claim index, so a creature deciding
  later in that update sees it (colony work requirement 2).
- A claim is held until the step that names it completes; then it drops, or, for the inbound and
  in-flight that follow cargo, is rewritten to what the step lifted ([Amounts](#amounts)). The task's
  last step completing ends the task and every claim with it.
- Claims live on the task and nowhere else. A task cleared by any path, a creature taken under direct
  control, or a creature destroyed leaves no claim behind (colony work requirement 1).

### Progress is engine state

The chain's progress (which step, the targets resolved so far, how many times a `do` has performed its
action, when the step began, whether its delivery was chosen) is state the engine holds on the task, so
the UI can show it and it can be saved. No script holds it.

### What survives an interruption

When another option takes over, the task's chain progress, its targets, its counts, and its claims end
with it. State survives in two places only, and both belong to the world or the goods rather than to
the chain:

- On the thing worked. A crafting job's `workDone` and its crafter, and a blueprint's `workDone`, keep
  what was done, and the crafter's hold brings it back to the station at the own-unfinished-work tier
  ([Interruptable crafts](./interruptable-crafts.md#the-crafters-hold)).
- On the goods carried. A work task carrying cargo toward a root (a haul past its pickup, a cut
  carrying what earlier cuts of its gathering landed) that is interrupted by a need, with the goods
  kept, becomes the task's suspended record: its instance id, claim sequence, root, item, and inbound
  and in-flight for the goods held ([Task claims](./task-claims.md#interruption-by-the-colonists-own-needs)).
  Another need task keeps the record; the carry-in to the record's root, item, and destination, or a
  further cut for that root and item, adopts it, taking its instance id and claims; any other selection
  discards it; and while the creature sleeps it counts for no one else. Resuming is that adoption, a new
  task under the old identity, not a replay of the old chain from its step.

Need chains, idle chains, and player orders keep nothing: the next decision chooses afresh. A low-mood
behavior's chain never resumes: an episode that a critical need interrupts is over, and the behavior's
relief and affinity follow from how it ended (Mood).

### The runner

A chain is not a second engine. It runs on the existing `Task`, `Action`, and routing machinery,
which keeps every hard-won fix that lives there:

- A chain-run is the task. `Task` keeps its routing, claim, tier, and timing fields and gains the
  chain id, the step index, the resolved targets, the step's count, and its start time, in place of
  the per-type fields.
- A go step sets the task's target the way a deposit leg does, through `ActionSystem::headTo`, which
  cancels the old leg's route ticket, invalidates the path, and leaves the AI to ask a new route. The
  next step being on the same task is what keeps a creature on its chain: the decision sees the task it
  is already doing, with its hysteresis margin.
- A do step is an ordinary `Action`, started by `ActionSystem` on arrival within `kActionReachMeters`
  through one start path that checks the target is still there and still what it was, and completed
  through `completeAction`, which reports whether its effect took.
- Selection, the switch gate, claims, and route answers are `AIDecisionSystem`'s.
- The option a chain becomes in the decision trace names its entry (the need, the work, the order, the
  behavior) and its target, with the entry's label as its reason text; `current_task(...)` reads the
  chain ids and the declared task groups, which list chain ids.

---

## Work

Colony work is chains offered for goals. The goal systems say what work exists and how much; a work
entry says which chain does it and which queries pick its source; the decision loop offers one option
per goal, item, and work entry to every creature that takes colony work.

### A work entry

```xml
<work>
  <category id="Hauling" label="Hauling" order="6"/>

  <work id="haul_pile" goal="haul" category="Hauling">
    <source query="pile_for_goal"/>
    <chain id="haul_from_pile"/>
    <tier rung="opportunistic_work"/>
    <tier rung="work_order" when="serves_work_order"/>
  </work>
</work>
```

- `goal` names the goal kind the entry serves (`haul`, `harvest`, `clear`, `craft`, `build`,
  `deconstruct`, `place_packaged`, `care`, with `need` naming the need for a care goal), or `root`
  for any root goal (the carry-in). The engine
  offers the entry once per goal of that kind and, for goals that take items, once per item the goal
  still wants; `goal` in the entry's queries is that item.
- `<source>` names the query that picks the option's source; an entry with none takes its target from
  the goal (a craft's station, a teardown's structure) or, with `source="carried"`, from what the
  creature carries toward the root.
- `<chain>` names the chain.
- `<tier>` rows give the rung; the highest rung whose row applies is the option's. The terms a work
  row reads describe the goal and the creature: `serves_work_order` (the goal's root is a craft or a
  build), `carrying` (the creature carries the option's goods), `holds_craft` (the creature holds the
  station's part-done unit).
- `gathers="true"` puts the entry's legs in a gathering ([Legs](#legs)).
- `<category>` declares a category, its label, its default order, and `canDisable`. A creature's work
  settings name categories and work entries.

### Engine services

The work the old option builders did by hand is a set of services any step or query asks for by
naming it, so no chain does it again. [Task claims](./task-claims.md) is the authority for what each
service does; this section says only how chains and queries use them.

#### Claims

A step names its claim kinds ([Claims on steps](#claims-on-steps)), and a query run for it passes over
what claims take ([Claims and the query](#claims-and-the-query)). Taken at selection, visible to later
deciders in the same update, self-excluded, tier-aware for food, dying with the task, and only carried
cargo held by a task whose route failed: [Task claims, Model](./task-claims.md#model),
[Availability](./task-claims.md#availability), [Need tasks](./task-claims.md#need-tasks),
[Stale memory](./task-claims.md#stale-memory), and [The claim index](./task-claims.md#the-claim-index).

#### Amounts

A `do` step that takes or puts has an amount, given by `amount`:

- `for_goal` (the default for work): the smallest of the root's available need (remaining need, read
  from the world, less others' in-flight), the source's available quantity, the destination's room
  net of others' inbound, and what the creature can carry, less its own cargo toward the root
  ([Availability](./task-claims.md#availability), [Remaining need](./task-claims.md#remaining-need)).
  No goal keeps a counter.
- `meal`: what takes the need back to its seek level at the item's nutrition, at least one unit,
  capped by what a pile or box has left for the creature; a bush is cut whole
  ([Need tasks](./task-claims.md#need-tasks)).
- `carried`: what the creature carries toward the root.

A take asks for its amount and takes what is there up to it; the claims that follow the goods are
rewritten to what it lifted, and a take that lifts nothing ends the task at the source. A put is
metered to what the destination still needs and has room for: a station takes exactly the recipe's
remaining need and the rest stays carried with no claim. A cut gives its whole yield, and what the
creature cannot carry drops beside the source as a pile ([Chain claims](./task-claims.md#chain-claims)).
A creature's own food is provisions, never cargo, in every amount above
([Provisions](./task-claims.md#provisions)). An option whose amount is zero is not offered, and shows
as NoRoom when the creature's own load is what stands in the way.

#### Hands

An action entry's `hands="free"` is what `needsHands` was. Before a step whose action needs free hands,
and when an option is selected over a task that carries goods, the hands rules run: a one-hand item to
the belt, else the pack; a two-hand armful, or one-hand goods with no room, into a box within reach
that takes them, else onto the ground; a packaged item set down still packaged. A carry-in delivers what
the creature holds and needs no free hands. Only a strictly higher tier of work, or a critical need,
interrupts a task carrying a two-hand armful its root still wants, and never a cut of the same item. A
creature about to idle with a two-hand armful no root wants puts it down first. A take toward a meal
makes room in a full pack, never at the cost of a tool or food
([Interruption](./task-claims.md#interruption-by-the-colonists-own-needs),
[Carry-in from possession](./task-claims.md#carry-in-from-possession),
[Chain_PlaceItem](./task-claims.md#chain_placeitem), [Need tasks](./task-claims.md#need-tasks),
[Task chains](../design/game-systems/colonists/task-chains.md)).

#### Legs

- The next leg is the same task: a step completing advances the chain in place, so a pickup becomes
  its deposit leg and a landed cut its carry-in with no new choice in between.
- Which leg a chain is on is decided by what the creature carries, not by where it stands: a `have`
  step is done at once when the creature already carries the goods toward the root, so a creature
  arriving at the source already carrying skips the pickup, and one standing on a station that is also
  the source still picks up ([Chain claims](./task-claims.md#fetch-from-stock-chain_pickupdeposit)).
- The destination of a work option is reach-tested at selection with its source: a label compare and
  the creature's verdicts, so a haul whose deposit has no known way is not picked
  ([Reserved rows](./task-claims.md#reserved-rows-and-option-dedup)).
- A delivery the decision keeps or selects is chosen, and nothing in its tier turns it back until the
  deposit ([Batching](./task-claims.md#batching)).
- Entries with `gathers="true"` (the cut, the pile pickup, and the carry-in toward one root and item)
  form a gathering: the creature fills its trip before walking, a known pile comes before a fresh cut,
  a top-up cut is taken only when its yield fits, and the cargo moves from task to task under one
  instance id ([Batching](./task-claims.md#batching)).
- A need between legs leaves the suspended record ([What survives an interruption](#what-survives-an-interruption)).

#### Builder spots and station slots

A station has one unnumbered slot, held by the creature whose Craft step names `slot`. A foundation's
builder spots are candidates of `<from goal="spots"/>`: the query passes over a spot another holds and
a spot whose stand point does not serve, and a builder deciding again keeps its own spot by the chosen
target rule ([Evaluation](#evaluation), rule 4). The spots, their stand points, and their spacing are
[Task claims, Build and Deconstruct](./task-claims.md#build-and-deconstruct).

### The haul

```xml
<work>
  <work id="haul_pile" goal="haul" category="Hauling" gathers="true">
    <source query="pile_for_goal"/>
    <chain id="haul_from_pile"/>
    <tier rung="opportunistic_work"/>
    <tier rung="work_order" when="serves_work_order"/>
  </work>
  <work id="pull_from_box" goal="haul" category="Hauling">
    <source query="box_to_pull_from"/>
    <chain id="haul_from_box"/>
    <tier rung="opportunistic_work"/>
  </work>
  <work id="carry_in" goal="root" source="carried" category="Hauling" gathers="true">
    <chain id="carry_in"/>
    <tier rung="opportunistic_work"/>
    <tier rung="work_order" when="serves_work_order"/>
  </work>
</work>

<chains>
  <chain id="haul_from_pile">
    <have item="goal" fetch="pick_up_for_goal"/>
    <go to="destination"/>
    <do action="Deposit" claims="inbound in_flight" amount="carried"/>
  </chain>
  <chain id="pick_up_for_goal">
    <go to="source"/>
    <do action="Pickup" claims="source_quantity"/>
  </chain>
  <chain id="haul_from_box">
    <have item="goal" fetch="withdraw_for_goal"/>
    <go to="destination"/>
    <do action="Deposit" claims="inbound in_flight" amount="carried"/>
  </chain>
  <chain id="withdraw_for_goal">
    <go to="source"/>
    <do action="Withdraw" claims="source_quantity"/>
  </chain>
  <chain id="carry_in">
    <go to="destination"/>
    <do action="Deposit" claims="inbound in_flight" amount="carried"/>
  </chain>
</chains>

<queries>
  <query id="pile_for_goal">
    <from pile="goal"/>
    <weigh fact="distance" by="-1"/>
  </query>
  <query id="box_to_pull_from">
    <from box="goal"/>
    <exclude fact="below_destination" below="1"/>
    <weigh fact="distance" by="-1"/>
  </query>
</queries>
```

The haul goal's destination is a storage box for a storage root, a station's store for a craft
fetch, and a blueprint for a site delivery. A pull only ever moves goods to a box of strictly higher
priority, so pulls never form a cycle.

### The harvest for a goal

```xml
<work>
  <work id="cut_for_goal" goal="harvest" category="Farming" gathers="true">
    <source query="cut_for_goal"/>
    <chain id="cut_and_carry_in"/>
    <tier rung="opportunistic_work"/>
    <tier rung="work_order" when="serves_work_order"/>
  </work>
</work>

<chains>
  <chain id="cut_and_carry_in">
    <go to="source"/>
    <do action="Harvest" claims="use"/>
    <go to="destination"/>
    <do action="Deposit" claims="inbound in_flight" amount="carried"/>
  </chain>
</chains>

<queries>
  <query id="cut_for_goal">
    <from yields="goal"/>
    <weigh fact="distance" by="-1"/>
  </query>
</queries>
```

The inbound and in-flight on the deposit are taken at selection for what the cut is expected to give,
and rewritten when the cut lands to what it landed, capped at the root's need: the in-flight is what
keeps a second colonist from cutting what this one's cut covers. The site clear is the same shape with
the `Clear` action and no deposit: it brings nothing toward a need and is never a leg of a gathering.

### The craft and its carry-ins

```xml
<work>
  <work id="craft" goal="craft" category="Crafting">
    <chain id="craft_at_station"/>
    <tier rung="work_order"/>
    <tier rung="own_unfinished_work" when="holds_craft"/>
  </work>
</work>

<chains>
  <chain id="craft_at_station">
    <go to="destination"/>
    <do action="Craft" claims="slot"/>
  </chain>
</chains>
```

The craft's inputs come through the craft goal's children, a haul goal and a harvest goal per input,
which `haul_pile`, `cut_for_goal`, and `carry_in` serve with the station's store as their destination
and the craft as their root, so any number of colonists provision one order in parallel through their
in-flight claims and none over-delivers. The Craft step holds the station's slot from selection, and
the Craft option is offered only for a station the creature can reach. A tool the craft makes lands in
the crafter's hands; furniture is spawned packaged and placed by `place_packaged`.

### The build

```xml
<work>
  <work id="build" goal="build" category="Construction">
    <source query="builder_spot"/>
    <chain id="build_at_spot"/>
    <tier rung="work_order"/>
  </work>
</work>

<chains>
  <chain id="build_at_spot">
    <go to="source"/>
    <do action="Build" on="destination" claims="slot"/>
  </chain>
</chains>

<queries>
  <query id="builder_spot">
    <from goal="spots"/>
    <weigh fact="distance" by="-1"/>
  </query>
</queries>
```

`on="destination"` makes the do step work on the blueprint while standing at the spot. A build goal
with no spots (a wall, an opening) has the blueprint's nearest walkable point as its one candidate and
takes no claim. A site ordered demolished is not offered as build work, and a build step on it ends
before it adds work, the build action's open-ended work checking its site each tick. Materials reach
the site through the build goal's due materials and the same haul, cut, and carry-in entries.

### The other work

| Work | Goal | Chain |
|---|---|---|
| `deconstruct` | deconstruct | go to the structure's nearest walkable point; `Deconstruct`; no claim |
| `place_packaged` | place_packaged | `have item` the packaged thing, fetched by go to it and `PickupPackaged` with `claims="use"`; go to its placement target; `PlacePackaged`. A carrier who selects anything else sets it down still packaged |
| `clear_site` | clear | go to the blocker; `Clear` with `claims="use"` |

---

## Tiers

The ladder is a list of named rungs, in order, declared once in `priority-tuning.xml`:

```xml
<Tiers>
  <Tier name="survival"/>
  <Tier name="critical_need"/>
  <Tier name="behavior"/>
  <Tier name="player_order"/>
  <Tier name="urgent_need"/>
  <Tier name="own_unfinished_work"/>
  <Tier name="work_order"/>
  <Tier name="ordinary_need"/>
  <Tier name="opportunistic_work"/>
  <Tier name="idle"/>
</Tiers>
```

No entry writes a tier number; it names a rung, and the rung's position is its tier.

| Chain | Rung |
|---|---|
| a need's chain | `critical_need`, `urgent_need`, or `ordinary_need`, by the need's level |
| a low-mood behavior | `behavior` |
| a player's order | `player_order` |
| a held craft | `own_unfinished_work` |
| work serving a craft or build, a craft, a build, a teardown, a placement being carried | `work_order` |
| other colony work | `opportunistic_work` |
| an idle chain | `idle` |

[Colonist task arbitration](./colonist-task-arbitration.md) is the authority for the ladder's order and
the switch gate. [Interruptable crafts](./interruptable-crafts.md#the-ladder) adds the urgent-need and
own-unfinished-work rungs, and [Mood](../design/game-systems/colonists/mood.md#the-episode-cycle) the
behavior rung directly below critical needs. Within a rung the score is arbitration's: distance, skill,
task age, the hysteresis margin, a box's storage priority for stocking, and the colonist's priority
for the work's category.

---

## Idle and player orders

An idle chain is a creature kind's `<idle>` row, offered at the idle rung:

```xml
<chains>
  <chain id="wander">
    <go query="wander_point"/>
  </chain>
  <chain id="gather_food">
    <go query="ripe_bush"/>
    <do action="Harvest" claims="use"/>
  </chain>
</chains>

<queries>
  <query id="wander_point" pick="weighted">
    <from place="around" radius="15" count="8"/>
    <exclude fact="distance" above="15"/>
    <weigh fact="distance" by="-0.1"/>
  </query>
</queries>
```

`gather_food`'s row carries `when="carried_food &lt; 1"`, so it is offered only while the pack holds
no food, and its take shows NoRoom when the pack has no room for a unit of the yield. A walking wander
decides every frame, so anything better is taken the moment it appears.

A player's order is a chain run at the player-order rung, with the target the player clicked as the
chain's target:

```xml
<orders>
  <order id="move_here" label="Move here" on="ground" chain="go_to_target"/>
  <order id="harvest" label="Harvest" on="harvestable" chain="harvest_target"/>
  <order id="relieve_here" label="Relieve self" on="ground" chain="relieve_at_target" when="need(bladder) &lt; 50"/>
</orders>
```

`on` says what the context menu offers the order on, and the order's `when` whether it is shown. An
order takes the claims its chain names by the same rules as anything else and takes nothing another
task holds ([Task claims, Bumping](./task-claims.md#bumping-planned)). Direct control is not an order:
it suspends the AI and returns the creature's work to the pool
([Player control](../design/features/player-control.md)).

---

## Requirements on the chain runner

The runner meets these from its first use. Each comes from what the existing multi-step task code had
to solve, and names the test that encodes the lesson.

1. When a task or a step changes or is cancelled, the `Action` in progress is cleared with it
   (`WokenSleeperCarriesOutTheNewTask`, `EnterControlClearsInProgressAction`).
2. Same-task detection compares identity (the chain, the step, the target's entity or memory key), not
   position alone; the world origin is a valid target; and a task whose route failed is always a fresh
   pick (`AClaimantKeepsItsTargetAcrossReevaluations`, `StoppedWanderRepicksFreshTarget`).
3. The hysteresis margin is applied at scoring, as the in-progress option's bonus, and not again at the
   switch gate (`HysteresisResistsThrashButYieldsToClearlyCloser`).
4. The tier gate holds: a non-interruptable action blocks every switch, and an interruptable one yields
   to the same or a higher tier (`ActionCompletesWithoutReEvaluationInterrupt`).
5. A creature decides at once on no task, on arrival, on a need newly urgent or critical, on the first
   route failure, or on a requested decision (`requestDecision`), otherwise on the 0.5 s interval, and
   never every frame while a failure persists (`AColonistWhoseEveryRouteFailsDecidesOnTheInterval`).
6. A step start that starts nothing ends the task, the target is checked on arrival, and stale memory of
   it is forgotten (`ACutWhoseTreeIsGoneEndsAndTheArmfulIsCarriedIn`,
   `AGonePileIsForgottenAndOtherFoodFound`).
7. A step is reported finished, and the next one begun, only when its effect took
   (`EatWhoseFoodVanishedFinishesNothing`).
8. Reach is tested at selection by connectivity without a path search, an unreachable option is
   skipped rather than frozen on, and a failed route leaves a verdict so its target is not picked again
   (`FoodSkipsEveryUnreachableNearerSource`, `WanderingColonistDoesNotRepickAFailedRoute`).
9. A target inside an obstacle gets a stand point beside it, and the step's work finds the target by its
   memory key, not by nearness to the stand point (`WideTreeIsHarvestedFromItsStandPoint`,
   `VanishedTargetIdClearsActionWithoutGrabbingDecoy`).
10. A new leg cancels the old route ticket, invalidates the path, and lets the AI ask a new route, and a
    route answer is applied only to the goal it was asked for
    (`AnAnswerForATargetLeftBehindIsNotApplied`, `OrderReplacedWhileAwaitingLeavesNoTicketBehind`).
11. A do step's action starts within 0.4 m of its target, because creatures bound for one point push
    each other apart (`ThreeThirstyColonistsAllDrinkAtOneShorePoint`).
12. When a step's action needs free hands (`hands="free"` on its action entry), held goods are stowed
    or set down by the existing rules, a two-hand armful is never stranded, and a packaged item is set
    down still packaged (`ASwitchToBuildFreesTheHands`,
    `InterruptedTwoHandArmfulDropsOnceWithWholeQuantity`,
    `FurnitureWhosePlacementIsCancelledIsSetDownPackaged`).
13. Only what the creature remembers is targeted, and a regrowing or exhausted source is passed over at
    selection (`CannotFindUnknownEntities`, `RegrowingSourceIsPassedOverForAReadyOne`).
14. Every order the runner makes is total with a final tie-break, nothing depends on unordered
    iteration, and the only randomness is the seeded generators (`EqualKeyBreaksTiesOnTiebreakId`).
15. A decision scans no creature's whole memory, reuses its scratch buffers, and probes options in
    priority order, stopping at the first selectable one.
16. Every field the runner adds to `Task` says whether it survives `Task::clear()`; the chain, the step,
    its targets, its count, and its start time are all reset by it, so an interrupted chain never
    resumes from its step. The suspended record survives `clear()` and is discarded only by the
    selection rules ([What survives an interruption](#what-survives-an-interruption)).

## Requirements for colony work

The services meet these, and each names the test that encodes it. These tests keep their assertions
whatever code they drive.

1. Claims live on the task and die with it: clearing a task, taking direct control, and destroying a
   creature each leave none (`PlayerControlAndDestructionLeaveNoClaims`,
   `ASourceIsFreedWhenItsHolderTakesControlOrDies`).
2. Claims are taken at selection and seen by creatures deciding later in the same update
   (`TwoColonistsOneTreeOneCutsTheOtherWorksElsewhere`, `EachBuilderOfAFoundationHoldsItsOwnSpot`,
   `TheLastUnitOfAnIngredientIsFetchedOnce`).
3. A claimant never reads its own claim as taken (`AClaimantKeepsItsTargetAcrossReevaluations`).
4. Food claims follow the eater's tier: a more urgent eater takes food a routine one claimed first,
   equal urgency is first-come, and work never holds food from a hungry creature
   (`AStarvingColonistBeatsARoutineEaterThatClaimedFirst`, `AStarvingColonistTakesBerriesAHaulerClaimed`,
   `FoodNobodyHoldsIsPreferredToFoodAWorkerHolds`).
5. A task whose route failed holds only claims on goods it carries
   (`AFailedRouteHoldsNothingItHasNotReached`, `AFailedRoutePastThePickupKeepsItsCargoClaimed`,
   `AFailedRouteThatSucceedsTakesItsClaimsBack`).
6. Supply is read from the world with no counters, so every unit of a multi-unit craft is provisioned
   and made (`MultiUnitCraftWithFetchedAndHarvestedInputsCompletes`), and each remaining need is worked
   out once per update (`EachRemainingNeedIsWorkedOutOncePerUpdate`).
7. In-flight claims keep creatures serving one root from over-gathering
   (`TwoColonistsServingOneGoalNeverOverDeliver`, `TwoColonistsDoNotBothCutWhatOneCutCovers`,
   `ASecondColonistDoesNotCutWhatTheFirstCarriesAndHasAccepted`).
8. Inbound claims protect a box's room, item by item and slot by slot
   (`ThreeHaulsOfThreeItemsShareOneFreeSlot`).
9. Source claims split a pile up to its quantity and never past it
   (`TwoHaulersSplitAPileUpToItsQuantity`, `AHaulClaimsWhatHeCanLiftNow`).
10. A station takes exactly the recipe's remaining need and the carrier keeps the surplus
    (`CraftDepositMetersToRecipeNeedAndKeepsSurplus`, `CraftFetchFromPileTakesOnlyRemainingNeedLeavesRest`).
11. A pickup that lifts nothing ends the haul at the source (`PickupFromAGoneSourceEndsTheHaul`,
    `AHaulerArrivingAfterAHungryColonistAteEndsItsHaul`).
12. A carrier interrupted by a need keeps a suspended record that only the matching carry-in or a
    further cut adopts, and any other selection discards it
    (`ColonistInterruptedByANeedMidHaulResumesWithTheGoods`,
    `AnySelectionButANeedOrTheCarryInDiscardsTheRecord`, `AdoptionMatchesTheRecordsDestination`).
13. A sleeping creature's record counts for no one else, and is dropped if another delivered the goods
    meanwhile (`AWokenSleeperWhoseGoodsWereFetchedByAnotherDropsHisRecord`).
14. A creature's own food is never cargo (`AColonistsOwnFoodIsNeverDelivered`,
    `FoodFetchedForACraftIsDeliveredAndTheMealKept`).
15. Nothing is offered when nothing would fit: no cut for a creature that cannot carry its yield, and no
    leg for less room than it brings (`OverweightColonistOffersNoHarvest`,
    `NoCutForABoxWithRoomForLessThanItLands`, `APileIsNoLegForTheLastUnitOfRoom`).
16. An armful bound for a box goes to a site that wants it without being dropped
    (`AnArmfulBoundForABoxGoesToASiteWithoutBeingDropped`).
17. An interrupted two-hand armful is dropped once, whole (`InterruptedTwoHandArmfulDropsOnceWithWholeQuantity`).
18. A two-hand delivery holds against fresh work of its own tier and yields to a critical need
    (`TwoHandPullHoldsAgainstFreshSamePriorityPullMidCarry`, `TwoHandPullYieldsToCriticalNeedMidCarry`).
19. An armful nobody wants is put down before idling (`AnArmfulNobodyWantsIsPutDown`,
    `AnArmfulNobodyWantsIsPutDownToGatherFood`).
20. Furniture whose placement is cancelled is set down packaged, off any site
    (`FurnitureWhosePlacementIsCancelledIsSetDownPackaged`, `FurnitureSetDownOnASiteGoesToGroundOffIt`).
21. A full pack still eats, giving up goods and never a tool (`AFullPackStillEatsFromABush`,
    `AFullPackGivesUpGoodsNotItsTool`, `ClaimedCargoGoesLast`).
22. The leg is decided by what is carried, not by where the creature stands
    (`LooseFetchPicksUpWhenSourceOverlapsStation`, `CarryingAtSourceRedirectsToDepositInsteadOfDeadlock`).
23. A chosen delivery is not turned back by a source that becomes ready
    (`AChosenCarryInIsNotTurnedBackByAReadySource`).
24. The deposit leg is reach-tested at selection (`HaulWhoseDepositIsNotConnectedIsNotPicked`).
25. A gatherer fills its trip before walking, takes a known pile before a fresh cut, and delivers when
    no other source is free (`AColonistFillsItsTripBeforeWalking`,
    `AFellsOverflowIsFetchedBeforeAnotherTreeFalls`, `APartlyFullTripIsDeliveredWhenNoOtherSourceIsFree`,
    `ABuildSiteClearIsNeverTheNextLegOfAGathering`).
26. A builder keeps its spot across decisions, and a need that replaces a build frees the spot
    (`ABuilderDecidingAgainKeepsItsSpot`, `ANeedReplacingABuildFreesItsSpotForAnother`,
    `AStoredSpotTheGoalNoLongerHasIsAFreshPick`).
27. The crafter that selected a station holds its slot (`AStationsSlotIsHeldByTheCrafterThatSelectedIt`).
28. A site ordered demolished offers no build, and a builder on it stops
    (`ABuilderIsNotOfferedASiteOrderedDemolished`, `BuilderMidActionStopsWhenSiteOrderedDemolished`,
    `WalkingBuilderDoesNotStartOnDemolishedSite`).
29. Pulls go only to a box of strictly higher priority, so they never form a cycle, and never drain a
    box below its minimum (`NoPullBetweenSamePriorityBoxes`, `NoPullFromHigherPriorityBox`,
    `PullDoesNotDrainSourceBelowItsMin`).
30. A goal visits only the remembered entries of the definitions that give its item, answering the
    per-definition gates first (`WorkGatesComeBeforeTheSources`, `HaulGoalsOfferEachPileOnce`,
    `HarvestGoalSeesEveryDefThatYieldsItsItem`, `FindsANeedSourceInALargeMemory`).
31. The development-build claim validator (`ClaimIndex::findViolation`) runs after every AI update that
    built the index and aborts on a violation, against claims named on steps exactly as against the
    claims it checks now ([Task claims, Testing](./task-claims.md#testing)).

---

## Low-mood behaviors on top

A behavior in [Mood](../design/game-systems/colonists/mood.md#data) keeps its flat facts and weights in
XML (ramp, base, `<requires>`, `<weight>` rows, relief). Its target is a query and its effect is a
chain:

```xml
<behavior id="soil_bed" rampStart="25" rampFull="15" base="4">
  <target query="bed_to_soil"/>
  <weight when="dislikes(owner(target))" multiply="4"/>
  <weight when="not observed" multiply="2"/>
  <effect chain="soil_bed"/>
  <ends after="task"/>
  <relief amount="medium"/>
</behavior>

<query id="bed_to_soil" pick="weighted">
  <from kind="bed"/>
  <exclude fact="distance" above="15"/>
  <exclude fact="has_owner" below="1"/>
  <exclude fact="owned_by_self"/>
  <exclude fact="owner_present"/>
  <weigh fact="distance" by="-0.2"/>
  <weigh fact="owner_opinion" by="-5"/>
</query>

<chain id="soil_bed">
  <go to="target"/>
  <do action="Toilet"/>
  <think thought="soiled_a_bed" to="self"/>
</chain>
```

The query picks the target, and the behavior takes part in the roll once, with its weights evaluated
against that target. A behavior's own `<ends>` bound the whole run, `after="task"` being the end of its
chain; a run stops at the first of them.

The behavior rung sits directly below critical needs and above the player's orders, because the player
cannot draft or direct a colonist during an episode (decided 2026-11-17, in Mood). A critical need still
interrupts a behavior. For that, a behavior's do steps use interruptable actions, or short ones that
complete before the gate is asked again, since a non-interruptable action blocks every switch.

### The target need not be the cause

Who a colonist takes a mood out on and who put them in it are separate. One person angers them and another gets the sharp word. The target query weighs toward the cause without requiring it:

```xml
<query id="who_to_snipe_at" pick="weighted">
  <from kind="colonist"/>
  <exclude fact="distance" above="5"/>
  <weigh fact="distance" by="-0.2"/>
  <weigh fact="opinion" by="-5"/>
  <weigh fact="mood_source" by="-6"/>
</query>
```

The colonist who caused the mood is the likeliest target when they are near, and not the certain one: the pick is a weighted draw. When they are out of reach, or are someone the colonist likes too well to turn on, the weights pick whoever else is nearby and least liked.

The fact is the source of a feeling, not blame for it, so it reads both ways. A colonist lifted by seeing someone they love is inclined to be kind to someone, and generally to that person: a kind act's target query weighs `mood_source` upward where a snipe's weighs it down.

A behavior whose effect needs a system that does not exist is a chain whose only step gives a thought,
the log-only rule in Mood: the episode is announced and logged, and nothing in the world changes.

### Entries plain conditions could not express

| Table entry | Expressed as | Waits on |
|---|---|---|
| x0.8 per consecutive hold | a weight row that applies once per count of a number term: `<weight per="holds_in_a_row" multiply="0.8"/>` | the episode record that counts holds |
| a food type with 1 or 2 left | the behavior's target query: `<from capability="edible"/>` with `<exclude fact="colony_stock" above="2"/>` | scarcity (`colony_stock`) |
| no liked colonists, liked colonist near | a quantity over colonists filtered by opinion: a query `<from kind="colonist"/>` excluding `opinion` below the like line (and `distance` above the radius for "near"), read by the condition term `count_of(query)`: `<weight when="count_of(liked_colonist_near) > 0" multiply="1.5"/>` | relationships (`opinion`) |
| built it themselves | the fact `built_by_self`, read as `built_by_self(target)` in the weight row | structures recording who built them |
| flammable structure near | `count_of` over a query `<from kind="structure"/>` excluding `flammable` below 1 and `distance` above 5 | fire |
| confined indoors | the self term `hours_indoors`, game hours since the colonist was last outside: `<weight when="hours_indoors > 6" multiply="2"/>` | rooms (`in_room`) |
| just failed a task, craft interrupted | recent events about self, the declared `recent_event(self, event, within)`, with the events `task_failed` and `craft_interrupted` | an event record about self; the craft half also on [Interruptable crafts](./interruptable-crafts.md) |
| has a sleep spot | `count_of(sleep_spot) > 0`, a query over `<from kind="bed"/>` and `<from place="here"/>` | nothing for the ground; beds wait on ownership |
| no bed | `count_of(own_bed) < 1`, a query over `<from kind="bed"/>` excluding `owned_by_self` below 1 | ownership |

`per` on a weight row and the term `count_of(query)` are added with the behaviors: `per` names a number
term and raises the multiplier to its value, and `count_of` counts the candidates that survive a
query's exclusions, scoring none of them.

---

## Related documents

- [Mood](../design/game-systems/colonists/mood.md): the behaviors, their data, and the conditions
- [Thoughts](../design/game-systems/colonists/thoughts.md): what a think step gives, and the action and need triggers
- [Needs](../design/game-systems/colonists/needs.md): what each need is to the player, and the mood cost rule
- [AI behavior](../design/game-systems/colonists/ai-behavior.md): when a colonist decides, and what it can get to
- [Memory](../design/game-systems/colonists/memory.md): where candidates come from
- [Task chains](../design/game-systems/colonists/task-chains.md): the hands rules
- [Work priorities](../design/game-systems/colonists/work-priorities.md): categories and the player's work settings
- [Player control](../design/features/player-control.md): orders and direct control
- [Colonist task arbitration](./colonist-task-arbitration.md): the (tier, score) key, the ladder, and the switch gate
- [Task claims](./task-claims.md): claims, amounts, interruption, and the suspended record, the authority for every service under Work
- [Interruptable crafts](./interruptable-crafts.md): the urgent level, work saved on the thing worked, the sleep curve
- [Config loading](./config-loading.md)

---

## Addendum: decisions

A supplement, not part of the spec. Kevin, 2026-11-17:

- XML always, with optional Lua: "its not XML OR LUA, its xml always, with optional lua".
- No logic in XML: "I don't want logic in the XML. linear non branching chains fine, but no logic." One
  `when=` test on a row is allowed.
- A query is weighted facts, a weighting system and not a logic tree.
- A query is "a very generic item query", not specific to food.
- No Lua in the first build, because no case needed it; the `script` attribute is reserved.
- How to migrate: "we've done a lot of bug fixing in task chains. understand the complexity there. we
  just finally got that stable". Said when the plan was to build beside the existing task code; it
  stands as the rule for moving that code, one path at a time with its tests as the contract.
- The player cannot step in during an episode.
- The target of a behavior and its cause need not be the same colonist: "one person pisses you off, but you take it out on another." The target is weighted toward the colonist who caused it.
- The pick of a behavior's target is weighted, "not a guarantee".
- That cause is the source of the feeling, not blame, and works for good moods too: "you saw someone you love, so you're going to be nice to someone, and it generally should be that person."
- Needs are data: "the needs become data. different entities will have different needs. cows need to
  be milked for instance."
- The hand-written behavior code is retired and replaced by this design: "no more C++ that system is
  going to be retired. we need to plan out the migration to the system we just designed. make sure all
  the decisions are captured, the spec is finished and checked in, the spec is reviewed, then scoped
  and broken down into tickets."
- A very basic "enjoy nature" restores Recreation: "a very basic 'enjoy nature' for the really
  baseline. takes a long amount of time for little gain".

Choices made in writing this document, not put to the owner:

- The folders and roots (`assets/config/needs`, `actions`, `queries`, `chains`, `work`, `orders`,
  `creatures`), with needs moving out of `assets/config/colonists/needs/` because needs belong to any
  creature kind.
- Load order: needs, then the condition vocabulary, then everything else, with cross-folder
  references resolved after all are read.
- `<scale trait fact by>` for trait scaling on the query; `above` and `below` on exclusions; a query
  that neither weighs distance down nor bounds it being a load error.
- Facts doubling as condition terms on a target; `per` on weight rows and `count_of(query)`.
- The step end events `held_gone`, `target_gone`, and `target_left`; an unanswered fact reading 0.
- Nothing of a chain surviving `Task::clear()`; resuming work being the suspended record's adoption,
  never a replay from the old step.
- The retired and kept line as drawn under Scope, including `GoalKind` staying a C++ list.
- `work-types.xml` deleted, with its categories kept as `<category>` entries in the work folder.
- A need's mood cost living on the need entry, with the penalty curve staying in `mood.xml`.
- A level as how well a need is met, so a need that builds up is written as one that falls.
- `<drains>` and `<setBy provider>` as the two ways a level moves, and rate rows on a drain.
- Several `<satisfiedBy>` rows per need, each its own option.
- `<satisfiedBy work>` with one care goal system for every need another creature meets, and the care
  work's tier following the holder's level.
- Creature kinds as entries with `def`, `mood`, `colonyWork`, a need list, and idle rows; a need list
  capacity of 16 per creature.
- Effects as the eight primitives listed, with `install` and `spawn into="hands"` beyond the brief;
  giving a thought kept out of actions, as a step and as thought sources only.
- `pays="end"` or `pays="along"` with a `curve` exponent.
- Action groups, matched to the target by the member's `on`, so one food query is followed by one take
  step.
- Toilet's effects chosen at its start by two `when` rows and a `serves(need)` term, and its durations
  becoming 2 s and 3 s that add (pee alone 2 s, poop alone 3 s where it was 4 s, both 5 s).
- Wash keeping free hands, as the code has it with no Wash entry in `action-types.xml`.
- EnjoyNature's values: 60 s, 2 Recreation, paying along, interruptable.
- `sleep_quality` as a fact the Sleep action scales by.
- `take` and `damage` moving from step kinds to actions; `drop` staying a step.
- Claims named on `do` steps, taken at selection for the whole chain, held until their step completes.
- Targets that carry claims being resolved at decision time with the first step's.
- The new candidate rows (`place="around"`, `yields`, `pile`, `box`, `goal`); the facts `sleep_quality`,
  `on_shore`, `near_waste`, `near_food`, `held`, and `below_destination`; the condition terms
  `serves(need)` and `carried_food`; and the shipped query values for relief, the pleasant spot, and
  wandering, with food's taken and held line expressed as an automatic exclusion plus `held`
  weighed by -1000.
- Work entries per goal kind, item, and source kind, with `<tier>` rows taking the highest rung that
  applies, and the terms `serves_work_order`, `carrying`, and `holds_craft`.
- The ladder as named rungs in order in `priority-tuning.xml`, with the behavior rung between critical
  needs and player orders.
- Player orders as `<order>` entries with `on` and an optional `when`.

## Addendum: history

- 2 January 2026: the work config files (`action-types.xml`, `task-chains.xml`, `work-types.xml`,
  `priority-tuning.xml`) and their registries were added, and the same day the chain design doc,
  [Task chains](../design/game-systems/colonists/task-chains.md).
- 5 January 2026: the "task chaining system" was built as chain ids, a +2000 continuation bonus while
  `chainStep` was past 0, and a step counter for Haul and PlacePackaged; it did not read the chain
  definitions, and refactoring Haul onto `Chain_PickupDeposit` was deferred. The same day the
  goal-driven task generation architecture was specified.
- 29 June 2026: the +2000 bonus became a tier classification, a mid-chain provisioning task at tier 4
  ([Colonist task arbitration](./colonist-task-arbitration.md)).
- 11 November 2026: the chain ids were deleted with the task claims work, since nothing read them;
  whether a task is past its pickup is `chainStep` alone.
- 17 November 2026: this document was first written for low-mood behaviors only, built beside the
  hand-written task code, with need fulfilment, crafting inputs, hauling, player commands, and animals
  named as later users and colony work's claims and amounts kept outside the query. The same day the
  owner retired the hand-written code, and the document was rewritten to describe the whole end state;
  the order of the move is a migration plan, not part of this document.
