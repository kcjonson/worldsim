# Queries and chains

Epics: WOR-919 (Colonist mood: low-mood behaviors, refusal, and colony failure), WOR-1056 (needs as
data), WOR-951 (behavior definitions as data)

Everything a creature does on purpose is defined in data and run by two generic mechanisms. A
**query** answers "which thing, for this creature, for this purpose": which food, which bed, which
pile to haul from, which builder spot, who to snap at. A **chain** is an ordered list of steps the
creature carries out: go there, do this, give that thought. Needs, actions, colony work, idle
activity, the player's orders, and low-mood behaviors are XML entries built from them, grouped into
**behaviors** that creatures include by name. The C++ provides the engine they run on: the decision
loop, arbitration, claims, supply read from the world, routing, the hands rules, and the effects an
action can have.

---

## Scope

### The end state

A creature is the behaviors it includes. A behavior bundles what belongs together: the needs it
brings (each with its drain and levels), the chains that meet them, the queries those chains pick
targets with, and the actions they perform. Each thing a creature can do is a chain, and the decision
loop offers it as an option: a need's chain at the need's rung, a piece of colony work at the work's
rung, an idle chain, a player's order, a low-mood behavior. Each `do` step performs an action, and an
action is an entry that names engine-provided effects with the values to use. Adding a need, a kind of
work, an action, or a behavior is adding entries; nothing about what a creature can do is a C++ list.

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
- The config that described this without being run: `task-chains.xml` with `TaskChainRegistry`,
  `assets/config/legacy/work-types.xml` with `WorkTypeRegistry`, and `assets/config/legacy/action-types.xml`
  with `ActionTypeRegistry`.

### Kept

The engine mechanisms the data-defined system runs on:

- The decision loop and its cadence (`AIDecisionSystem::shouldReEvaluate`, `kReEvalInterval`), the
  (rung, score) key, the hysteresis margin, and the switch gate
  ([Colonist task arbitration](./colonist-task-arbitration.md)).
- The claim kinds, `Task::claims`, `instanceId`, `claimSeq`, `SuspendedWork`, the claim index
  (`ClaimIndex`), and its development-build validator ([Task claims](./task-claims.md)).
- Supply read from the world: `GoalTaskRegistry::remainingNeed` and its per-update caches
  (`RemainingNeedCache`, `BoxRoomCache`, `ColonyStorageKnowledge`), and `LoosePileIndex`.
- The goal systems that say what colony work exists and how much (`StorageGoalSystem`,
  `CraftingGoalSystem`, `BuildGoalSystem`, `ConstructionSystem`'s goals) and `GoalTaskRegistry`. The
  types of goal they create stay a C++ list, `GoalType`, separate from what a creature does.
- Memory and discovery gating, the colony's shared storage knowledge, routing (route tickets, the
  nearest-of request, `SourceReachMemo` verdicts, connectivity), stand points beside obstacles, and
  the arrival and reach rules in `ActionSystem`.
- Hands and inventory: stow or drop (`storeOrDrop`), `makeRoomToEat`, the two-hand rules, packaged
  items, provisions.
- The effects an action can have, as the primitives listed under [Actions](#actions), reshaped from
  the `apply*Effect` functions to take their values from an action entry.
- Direct control (`issuePlayerMoveOrder`: a click snapped to walkable ground, with the AI suspended).
- Thoughts, mood, the condition language, and the decision trace.

### Reshaped

Kept structures keyed on `TaskType` or `NeedType` take a new key:

| Structure | Keyed on | Becomes |
|---|---|---|
| `GoalTask::type`, the registry's index of goals | `TaskType` | `GoalType`: `haul`, `harvest`, `clear`, `craft`, `build`, `deconstruct`, `place_packaged`, `care`; a clear is its own type, not a harvest goal with `clearFootprint` |
| `ClaimHolder::taskType`, which the food rule reads | `TaskType` | the holder's entry id, its need id when it serves one, and its rung |
| a suspended record filed as a haul | `TaskType::Haul` | the record's root, item, and destination |
| `ClaimIndex::addSuspended`'s sleeper rule | `needToFulfill == Energy` | the running action's `asleep` property |
| `chainItem`, `claimedCargo`, `adoptsSuspended`, `suspendableWork` | `TaskType` | the option's work entry (`goal`, `gathers`) and the claims its chain names |
| `ColonyGoalWork`'s task panel | trace rows by `TaskType` | trace rows by work entry and goal |
| `SourceReachMemo::roundWidening`, `FoodWait`, `lastToiletSpot` | a need slot, hunger, the toilet | the query entry |
| `actsOnArrival` | `TaskType` | the chain: a `go` followed by a `do` acts on arrival; a chain that ends on a `go` does not |
| `EvaluatedOption::taskType`, `needType` | both | the entry id and the need id |
| `current_task(...)` and `<TaskGroup>` | task type names | chain ids |

### Where the line is drawn

What a creature does, in what order, and with what values is data. What every doing relies on is
engine: whether a target is known, free, reachable, and still there; how much of it is wanted; what
the hands may hold; when to decide again. The start routines mix two jobs, a per-type choice of
effect (which goes to data) and the checks every start needs (arrival, a target still in memory, a
stale entry forgotten, a start that starts nothing ending the task), which become one start path for
every `do` step. The option builders mix a per-type choice of target (which goes to queries and work
entries) with claims, supply, reach, and gathering (which become services every query and step asks
for).

Goal types stay in C++ because the goal systems that create them stay: a craft goal exists because a
station has a queued job, and that is world state the engine owns. What is done about a goal is data.

---

## Data rule

Every need, action, query, chain, behavior, work entry, order, and creature is an XML entry that works
on its own for the simple case, as an asset definition does. XML holds no control flow: no branching
and no loops. A chain is a straight list of steps, each performed once; a query is a list of weighted
facts.

One yes-or-no test that picks whether a row of data applies is allowed on a row, written as `when=` in
the [condition language](../design/game-systems/colonists/mood.md#conditions), the way thought feelings
and low-mood behavior weights use it. A row whose `when` is false or Unknown does not apply. A `when`
is never on an entry as a whole, never on a step, and never chooses between steps.

Rows that each add an option (`<satisfiedBy>`, `<idle>`) are independent options, scored against each
other by arbitration, and never alternatives: no row is written as the else of another. Two rows of
one entry whose conditions are exact negations of each other load with a warning naming both.

Repetition is never a row. An action that goes on until something holds carries that as a numeric
bound on its effect (`upTo`), and a step ends only on events the engine reports.

`have` is the one step with a precondition. It is a skip, not a branch: when the creature already has
what the step names, its fetch chain is skipped, and either way the chain continues with the same next
step ([Chains](#chains)).

Every entry may name a Lua script for rich expression, the way an asset definition names a generator,
with the entry's own values passed to it. The format reserves an optional `script` attribute on every
entry type for this: a path relative to the entry's file, whose script takes the entry's values as a
table. An entry that gives `script` is a load error naming the file, the entry, and the attribute as
reserved, so no mod ships a script that is silently ignored, and a later hook adds meaning to the
attribute without changing any entry that works.

### Files

What is the same for every creature is shared under `assets/config/`. What a creature is lives in its
asset folder.

| Folder | Root | Entries |
|---|---|---|
| `assets/config/behaviors/<folder>/` | `<behavior>` | one behavior a file, holding its `<need>`, `<chain>`, `<query>`, `<action>`, `<idle>`, and `<lowMood>` entries |
| `assets/config/actions/` | `<actions>` | `<action>`, `<actionGroup>`: the actions colony work and many behaviors share (take, put, work, install) |
| `assets/config/work/` | `<colonyWork>` | `<category>`, `<work>`, and the `<chain>`, `<query>`, and `<action>` entries colony work uses |
| `assets/config/orders/` | `<orders>` | `<order>` and the `<chain>` entries orders use |
| `assets/config/facts/` | `<facts>` | `<fact>` tuning values the engine's facts read |
| `assets/config/arbitration/` | `<arbitration>` | `<ladder>` and the within-rung bonuses (moved from `work/priority-tuning.xml`) |
| `assets/world/creatures/<Name>/` | see [Creatures](#creatures) | the creature's definition |

A loader reads a file whose root it owns; a file in one of these folders with any other root is a load
error, so no tuning file is ever silently skipped. Each folder loads its own files first, then its
subfolders' in path order, as `assets/config/conditions/` does ([Config loading](./config-loading.md)).
The same full id twice within one entry type, in one file or in two, is a load error naming both; no
entry silently replaces another.

The library loads first, then the creatures. Need ids are collected from every behavior and every
creature folder before the condition vocabulary loads, because they fill its owned `need` set. The
references between entries (a need naming a chain, a chain naming a query or an action, a creature
naming a behavior, a work entry naming a category) are resolved once all are read, so no folder order
matters for them.

Any problem in a file (an unknown element or attribute, a name that does not exist, a bad number)
loads nothing from it. Every load error names the file, the entry, the element, and the attribute,
is prefixed with the folder's name (`[behaviors]`, `[actions]`, `[work]`, `[orders]`, `[facts]`,
`[arbitration]`, `[creatures]`), and reaches the loading scene's failure view like the colonist rules'
errors.

### Ids

Every entry has an `id` and, where the player sees it, a `label`. An entry is defined with `id` and
referred to by the same id; there is no second name. Ids are case-sensitive paths. The shipped library
names its files and entries in snake_case; a creature's folder part is its definition name as written.
Every declared id, and every file and folder name an id is made from, is letters, digits, and underscores,
case as written; anything else is a load error showing the text. The entries of `actions/`, `work/`,
`orders/`, and `facts/` take top-level ids, so a subfolder in one of those folders is a load error.

Ids are unique per entry type: behavior, chain, query, action (actions and action groups together), work,
order, and category. A need is keyed by its behavior and its id, so two behaviors may each bring a need of
one id, and the creature that includes both settles which one it takes. The attribute that refers to an
entry names its type (`chain="human/sleep"`, `action="human/sleep"`, `query=`, `work=`), so a chain and an
action may share an id, and a work entry and its chain often do.

A behavior's id is its path under `assets/config/behaviors/` without the extension:
`assets/config/behaviors/human/eating.xml` is `human/eating`, and
`assets/config/behaviors/herbivore/eating.xml` is `herbivore/eating`. The file declares only the last
part (`<behavior id="eating">`), and one that disagrees with its file name is a load error. A
creature's own behaviors take the creature's definition name as their folder: the cow's
`being_milked.xml` is `Cow/being_milked`. A mod's ids start with the mod's folder.

The chains, queries, and actions defined in a behavior's file are in its folder too: `human/fetch_food`,
`herbivore/graze`. Inside a file, a short id names an entry of the same folder, else a top-level entry
of the shared folders (`deposit`, `harvest`); from outside the folder an entry is written in full. A
folder entry whose short id is also a top-level id of the same type loads with a warning naming both,
since inside the folder it hides the top-level one.

A need's id is a plain name, the same for every creature that has the need: in a thought's `need`, in
a `<restore>` effect, and in the condition term `need(hunger)`. Two creatures' needs with the same id
share that name and nothing else: a human's `hunger` and a deer's `hunger` drain, rise, cost, and are
met in their own ways. `need(hunger)` reads the creature's own hunger and is Unknown for a creature
without one.

---

## Behaviors

A behavior is a named bundle of what a creature can do and what starts it. Its entries are what a
creature gets by including it.

```xml
<!-- assets/config/behaviors/common/drinking_at_water.xml: common/drinking_at_water -->
<behavior id="drinking_at_water" label="Drinking at water">
  <need id="thirst" label="Thirst" seek="50" urgent="25" critical="10">
    <drains perGameMinute="0.12"/>
    <satisfiedBy chain="drink_at_water"/>
  </need>

  <chain id="drink_at_water">
    <go query="water_nearby"/>
    <do action="drink"/>
  </chain>

  <query id="water_nearby">
    <from capability="drinkable"/>
    <weigh fact="distance" by="-1"/>
  </query>

  <action id="drink" seconds="1.5" hands="free" interruptable="false">
    <restore need="thirst" amount="100"/>
    <restore need="bladder" amount="-15"/>
  </action>
</behavior>
```

- A behavior may bring any number of needs, chains, queries, actions, idle rows, and low-mood
  episodes. Something started by nothing (a chain no need, idle row, work entry, order, or episode
  names) is a load warning.
- An effect on a need the creature does not have does nothing, so a deer that includes
  `common/drinking_at_water` drinks without a bladder.
- A behavior may not include another behavior. Sharing is by a creature listing several, and by
  referring to another folder's chain or query by its full id.

The library's folders group behaviors by who has them: `common/` for many kinds of creature,
`human/` for humans, `herbivore/` for grass eaters. A behavior one creature alone uses may live in that
creature's folder instead.

A low-mood behavior is a behavior whose start is a mood episode rather than a need: it holds a
`<lowMood>` entry with the roll's values ([Low-mood behaviors](#low-mood-behaviors)). The human
includes the low-mood behaviors it can have, the same way it includes the ones that meet its needs.

---

## Creatures

A creature is a definition in its own asset folder, `assets/world/creatures/<Name>/`. The folder's
primary file, `<Name>/<Name>.xml`, holds its asset definitions (sprites, motion, size), loaded by the
asset registry like every asset. The base definition, the one whose `defName` is the folder's name,
carries a `<creature>` block; a `<creature>` block on any other definition is a load error.

```xml
<AssetDefinitions>
  <AssetDef>
    <defName>Human</defName>
    <assetType>simple</assetType>
    <svgPath>human.svg</svgPath>
    <motion>biped.motion.xml</motion>
    <worldHeight>1.0</worldHeight>

    <creature mood="true">
      <behavior id="human/eating"/>
      <behavior id="common/drinking_at_water"/>
      <behavior id="human/sleeping"/>
      <behavior id="human/relieving"/>
      <behavior id="human/washing"/>
      <behavior id="human/enjoying_nature"/>
      <behavior id="human/temperature"/>
      <behavior id="common/wandering"/>
      <moodCost need="hunger" cost="25"/>
      <moodCost need="thirst" cost="25"/>
      <moodCost need="energy" cost="20"/>
      <moodCost need="bladder" cost="10"/>
      <moodCost need="digestion" cost="10"/>
      <moodCost need="hygiene" cost="15"/>
      <moodCost need="recreation" cost="15"/>
      <moodCost need="temperature" cost="60"/>
    </creature>
  </AssetDef>
</AssetDefinitions>
```

- `<behavior id>` includes a behavior from the library or the creature's folder, by its full id. A
  creature is the behaviors it includes; there is no parent creature and no chain of inheritance.
- A `<need>` row in the `<creature>` block with the id of an included need changes only the values it
  gives: `<need id="hunger"><drains perGameMinute="0.05"/></need>` gives a cow a slower hunger and
  keeps the rest. A `<need>` with an id no included behavior has is the creature's own need.
- Two included behaviors that both bring a need of the same id are a load error naming both, unless
  the `<creature>` block settles it with `<need id="hunger" from="herbivore/eating"/>`, naming the
  behavior whose entry it takes.
- `mood` says whether the creature has a mood. A creature with mood gives each of its needs a mood
  cost with a `<moodCost>` row, the most mood the need takes off at its worst
  ([Needs](../design/game-systems/colonists/needs.md#what-a-need-costs-mood)); a need of such a creature
  with no cost is a load error naming it. The penalty curve, easing, and bands are the creature's
  `mood.xml`.
- The creature's order (below) breaks a tie between two needs equally low, and ranks idle rows.

### Loading a creature

The asset registry parses only the primary file and only the elements of an asset definition; it reads
nothing of the `<creature>` block. The creature loader, run on the loading scene after the asset
registry and beside the colonist rules, reads the rest:

- It finds each creature folder under `assets/world/creatures/`, and in each mod's own creature
  folders, and re-reads the base definition's `<creature>` block from the primary file.
- It reads every other XML file at the top level of the folder, in file name order, except a file the
  definition names as a helper (as it names its motion file). Each is read by its root: a `<creature>`
  file adds rows to the block; a `<behavior>` file is one of the creature's own behaviors; `<Mood>`,
  `<Memory>`, `<Traits>`, and `<Attributes>` go to their own loaders. Any other root is a load error.
  These files are never named after the folder, which the asset scan keeps for the primary file, and
  the folder has no subfolders.
- The creature's order is the primary block's `<behavior>` includes in the order written, then each
  `<creature>` file's includes in file name order, then the creature's own behavior files in file name
  order, then mods' additions in mod load order.
- The same behavior included twice is a load error naming both places. A creature's own behavior file
  is included by being in its folder, and a `<behavior id>` row naming it places it in the order without
  including it twice. Two rows that set the same
  value of the same need (an override, a settling row, a `<moodCost>`) are a load error naming both
  files.
- Traits and attributes keep their own loaders' rule: a later file replaces a trait or an attribute of
  the same name and the replacement is logged. They are the human's config, not entries of this
  design.

A mod only adds its own behaviors; it cannot replace a shipped one, and its entry with a shipped
entry's full id is the duplicate load error above. A mod never writes into a shipped folder. Its files sit in the mod's own folder: its behaviors under
its own behavior folder, its ids starting with that folder, and a `<creature for="Human">` file naming
the creature it adds to, which includes the mod's behaviors and gives their `<moodCost>` rows. A mod
adds a creature with a creature folder of its own, and traits with a `<Traits for="Human">` file.
Nothing is replaced whole to add to it.

### The human and the colony

A colonist is a human with colony membership, a role a human gains and loses; there is no colonist
definition. Membership is engine state the sim grants and removes: a human spawned by the colony is a
member, and one who leaves the colony stops being one
([Mood](../design/game-systems/colonists/mood.md), leaving the colony). Colony work is offered to
members; a member counts for the colony's sight and work, and appears in the colonist UI. Every human
has a mood, thoughts, and traits, member or not; what the player sees of them, and what the player can
set, follows membership. The condition kind `colonist` is a human who is a member.

### Shipped human files

| File | Holds |
|---|---|
| `assets/world/creatures/Human/Human.xml` | the human's asset definitions (`Human`, `Human_up`, `Human_down`, `Human_left`, `Human_right`) and the `<creature>` block |
| `assets/world/creatures/Human/biped.motion.xml` | the walk motion |
| `assets/world/creatures/Human/mood.xml` | the penalty curve, easing, and bands |
| `assets/world/creatures/Human/memory.xml` | memory tuning |
| `assets/world/creatures/Human/traits.xml` | the traits |
| `assets/world/creatures/Human/attributes.xml` | the attribute ranges |

### A cow

```xml
<!-- assets/world/creatures/Cow/Cow.xml, base definition -->
<creature mood="false">
  <behavior id="herbivore/eating"/>
  <behavior id="common/drinking_at_water"/>
  <behavior id="common/sleeping_on_the_ground"/>
  <behavior id="common/wandering"/>
  <behavior id="Cow/being_milked"/>
  <need id="hunger"><drains perGameMinute="0.05"/></need>
</creature>

<!-- assets/world/creatures/Cow/being_milked.xml: Cow/being_milked -->
<behavior id="being_milked">
  <need id="milking" label="Milking" seek="40" urgent="20" critical="5">
    <drains perGameMinute="0.02"/>
    <satisfiedBy work="milk"/>
  </need>
</behavior>
```

```xml
<!-- assets/config/behaviors/herbivore/eating.xml: herbivore/eating -->
<behavior id="eating" label="Grazing">
  <need id="hunger" label="Hunger" seek="50" urgent="25" critical="10">
    <drains perGameMinute="0.08"/>
    <satisfiedBy chain="graze"/>
  </need>
  <chain id="graze">
    <go query="grass_nearby"/>
    <do action="graze"/>
  </chain>
  <query id="grass_nearby">
    <from capability="grazable"/>
    <weigh fact="distance" by="-1"/>
  </query>
  <action id="graze" seconds="10" hands="any" interruptable="true" pays="along">
    <restore need="hunger" amount="30"/>
  </action>
</behavior>

<!-- assets/config/work/milking.xml -->
<colonyWork>
  <work id="milk" goal="care" need="milking" category="farming" rung="opportunistic_work">
    <chain id="milk_a_cow"/>
  </work>
  <chain id="milk_a_cow">
    <go to="destination"/>
    <do action="milk" claims="use"/>
  </chain>
  <action id="milk" seconds="5" hands="free" interruptable="false">
    <restore who="target" need="milking" amount="100"/>
    <spawn def="Milk" into="hands"/>
  </action>
</colonyWork>
```

A deer would include `herbivore/eating` too, with its own drain, and a human's `hunger` comes from
`human/eating`: the same need id, met in different ways. The care goal's destination is the cow. `milk` restores the target's Milking and puts the milk in the
milker's hands; the milk is then carried to storage by carry-in from possession when a box takes milk,
or put down by the idle rule when none does. A cow is a mover, so the claim is on a mobile target, and
the go step ends with `target_left` when the cow walks off
([Task claims](./task-claims.md#built-now-and-planned), mobile targets).

---

## Needs

A need is an entry in a behavior, from the library or from a creature's own folder.

```xml
<need id="hunger" label="Hunger" seek="50" urgent="25" critical="10">
  <drains perGameMinute="0.08"/>
  <satisfiedBy chain="eat_from_pack"/>
  <satisfiedBy chain="fetch_food" when="carried_food &lt; 1"/>
</need>
```

- `seek`, `urgent`, and `critical` are the three levels, need percentages, each below the one before
  ([Interruptable crafts](./interruptable-crafts.md#needs-in-three-levels)).
- `<satisfiedBy>` names what meets it (below). A need with none is tracked, costs mood where the
  creature has one, and is never acted on.

### How a level moves

A level is how well the need is met: 100 is fully met and 0 is empty. A need that builds up in the
creature (a bladder, a cow's udder) is written the same way, its level falling as the pressure grows,
so every need reads alike to the AI, the mood curve, and the player.

A need moves in one of two ways, and an entry gives exactly one:

- `<drains perGameMinute="r"/>`: the level falls by `r` each game minute, clamped at 0.
- `<setBy provider="..."/>`: the level is set each tick by a provider the sim implements in C++, an
  enumerated set like the facts, and nothing drains it. Temperature is set this way by the creature's
  surroundings. A provider the sim declares and nothing answers holds the need at full, and the load
  logs once that it is unanswered.

A drain may carry rate rows, each a multiplier with one `when`, all that apply multiplying together:
`<rate when="current_task(work)" multiply="1.5"/>`. Anything else that moves a level is an action's
effect (drinking fills the bladder) or a chain's `<need>` step.

### Levels and rungs

Below `seek` the need's option is on the `ordinary_need` rung, and below `critical` on the
`critical_need` rung, which interrupts work. A creature decides at once when a need crosses into
critical, unless it already serves a critical need (requirement 5). An ordinary need outranks
opportunistic work; only a critical need interrupts committed work, so committed work is left only in
extreme cases. The urgent level is read by thoughts and the UI; [An urgent-need rung](#an-urgent-need-rung)
gives it a rung of its own.

### What satisfies a need

`<satisfiedBy chain="..."/>` names a chain the creature runs itself. A need may list several, each with
an optional `when`; each that applies is its own option at the need's rung, and arbitration picks
between them. The chain's first target is found by a query, so the search for a need is a query and
nothing else. How a need is met belongs to the creature's need, so a cow's hunger grazes while a
human's runs the meal chains.

`<satisfiedBy work="..."/>` names a work entry another creature does: the need's holder cannot meet it
itself. While the holder's level is below `seek`, a care goal for that need exists on it (`GoalType`
care, made by the care goal system, one engine system for every need met this way), and the work entry
offers it to colony members. The care work's rung is its work entry's.

### What replaces the fixed list

| Retired | Replaced by |
|---|---|
| `NeedType`, `kNeedLabels`, `needLabel` | a need id interned at load, with the entry's `label` |
| `NeedsComponent`'s array of every need | the creature's own list: its need ids and their values, inline with a fixed capacity of 16, so no creature's needs are on the heap; levels and rates are read from the entries |
| `kActionableNeeds` | a need with a `<satisfiedBy>` |
| `needToCapability`, the Hunger special case | the satisfying chains' queries |
| `startNeedAction`, `getFirstActionDefName` | the chain's `do` steps and the action entries' `hands` |
| `Task::needToFulfill`, `criticalNeedsAtDecision` as a bit per `NeedType` | the option's need id, and a bit per slot in the creature's need list |
| the sim filling the condition vocabulary's `need` set from `kNeedLabels` | every loaded need id |
| thought triggers `needFalls` and `needRises` checking `kNeedLabels` | the same check against the creature's needs |
| `<NeedWeights>` in `mood.xml` | the creature's `<moodCost>` rows |
| the decay literals in `Needs.cpp`, `assets/world/creatures/Human/needs.xml` | each need's `<drains>` and levels |

---

## The human behaviors

The behaviors in this section reproduce the hand-written behavior value for value, with the one
difference [Relief decided before setting off](#relief-decided-before-setting-off) states.
[Changes to behavior](#changes-to-behavior) gives what some of them become, and the human behaviors
with no hand-written predecessor, enjoying nature among them. Each block below is one file under
`assets/config/behaviors/`.

```xml
<!-- human/eating.xml: human/eating -->
<behavior id="eating" label="Eating">
  <need id="hunger" label="Hunger" seek="50" urgent="25" critical="10">
    <drains perGameMinute="0.08"/>
    <satisfiedBy chain="eat_from_pack"/>
    <satisfiedBy chain="fetch_food" when="carried_food &lt; 1"/>
  </need>
  <idle chain="gather_food" when="carried_food &lt; 1"/>

  <chain id="eat_from_pack">
    <do action="eat"/>
  </chain>
  <chain id="fetch_food">
    <go query="food_to_eat"/>
    <do action="take" claims="use source_quantity" amount="meal"/>
  </chain>
  <chain id="gather_food">
    <go query="ripe_food_bush"/>
    <do action="harvest" claims="use"/>
  </chain>

  <query id="food_to_eat">
    <from yields="edible"/>
    <from pile="edible"/>
    <from box="edible"/>
    <prefer fact="held"/>
    <weigh fact="distance" by="-1"/>
  </query>
  <query id="ripe_food_bush">
    <from yields="edible"/>
    <weigh fact="distance" by="-1"/>
  </query>

  <action id="eat" seconds="2" hands="free" interruptable="false">
    <consume capability="edible" count="1"/>
    <restore need="hunger" from="nutrition" scale="100"/>
    <restore need="digestion" from="nutrition" scale="-100"/>
  </action>
</behavior>

<!-- human/washing.xml: human/washing -->
<behavior id="washing" label="Washing">
  <need id="hygiene" label="Hygiene" seek="40" urgent="25" critical="15">
    <drains perGameMinute="0.015"/>
    <satisfiedBy chain="wash_at_water"/>
  </need>
  <chain id="wash_at_water">
    <go query="water_to_wash_in"/>
    <do action="wash"/>
  </chain>
  <query id="water_to_wash_in">
    <from capability="washable"/>
    <weigh fact="distance" by="-1"/>
  </query>
  <action id="wash" seconds="3" hands="free" interruptable="false">
    <restore need="hygiene" amount="100"/>
  </action>
</behavior>

<!-- human/sleeping.xml: human/sleeping -->
<behavior id="sleeping" label="Sleeping">
  <need id="energy" label="Energy" seek="30" urgent="20" critical="10">
    <drains perGameMinute="0.05"/>
    <satisfiedBy chain="sleep"/>
  </need>
  <chain id="sleep">
    <go query="place_to_sleep"/>
    <do action="sleep"/>
  </chain>
  <query id="place_to_sleep">
    <from capability="sleepable"/>
    <from place="here"/>
    <prefer fact="sleep_quality" below="1"/>
    <weigh fact="distance" by="-1"/>
  </query>
  <action id="sleep" seconds="8" hands="any" interruptable="true" asleep="true">
    <restore need="energy" amount="60" scaleBy="sleep_quality"/>
  </action>
</behavior>

<!-- human/relieving.xml: human/relieving -->
<behavior id="relieving" label="Relieving self">
  <need id="bladder" label="Bladder" seek="30" urgent="20" critical="10">
    <drains perGameMinute="0.03"/>
    <satisfiedBy chain="relieve_bladder"/>
    <satisfiedBy chain="relieve_both" when="seeking(digestion)"/>
  </need>
  <need id="digestion" label="Digestion" seek="30" urgent="20" critical="10">
    <drains perGameMinute="0.02"/>
    <satisfiedBy chain="relieve_bowels"/>
    <satisfiedBy chain="relieve_both" when="seeking(bladder)"/>
  </need>
  <chain id="relieve_bladder">
    <go query="place_to_relieve"/>
    <do action="urinate"/>
  </chain>
  <chain id="relieve_bowels">
    <go query="place_to_relieve"/>
    <do action="defecate"/>
  </chain>
  <chain id="relieve_both">
    <go query="place_to_relieve"/>
    <do action="relieve_both"/>
  </chain>
  <query id="place_to_relieve">
    <from capability="toilet"/>
    <from place="grid" spacing="3" radius="25"/>
    <from place="here" fallback="true"/>
    <exclude fact="on_shore"/>
    <prefer fact="is_place"/>
    <weigh fact="distance" by="-1"/>
    <weigh fact="waste_nearby" by="10"/>
    <weigh fact="food_nearby" by="-20"/>
  </query>
  <action id="urinate" seconds="2" hands="any" interruptable="false">
    <restore need="bladder" amount="100"/>
  </action>
  <action id="defecate" seconds="4" hands="any" interruptable="false">
    <restore need="digestion" amount="100"/>
    <spawn def="Misc_BioPile"/>
  </action>
  <action id="relieve_both" seconds="5" hands="any" interruptable="false">
    <restore need="bladder" amount="100"/>
    <restore need="digestion" amount="100"/>
    <spawn def="Misc_BioPile"/>
  </action>
</behavior>

<!-- human/temperature.xml: human/temperature -->
<behavior id="temperature" label="Temperature">
  <need id="temperature" label="Temperature" seek="40" urgent="25" critical="15">
    <setBy provider="thermal_comfort"/>
  </need>
</behavior>

<!-- common/wandering.xml: common/wandering -->
<behavior id="wandering" label="Wandering">
  <idle chain="wander"/>
  <chain id="wander">
    <go query="wander_point"/>
  </chain>
  <query id="wander_point" pick="weighted">
    <from place="around" minRadius="2.4" radius="8" count="1"/>
    <exclude fact="distance" above="8"/>
  </query>
</behavior>
```

Food already carried is eaten first. Eating ends at the seek level without a loop. `eat_from_pack` is offered while the creature holds
edible food, since its one step consumes held food; each run eats one unit, and the next decision
offers it again while hunger is still below seek. `fetch_food` is offered only while the creature
carries no food, and its take puts one meal in the pack and ends the task. `carried_food` is a
condition term: the number of edible units the creature carries in hands, belt, and pack, provisions
included. The `eat` action's Digestion effect is why `human/relieving` brings Digestion.

`seeking(need)` is true while that need of the creature is below its seek level. A chain that several
needs' `<satisfiedBy>` rows name is one option, serving every one of those needs that is below seek,
and among options on one rung the one serving more needs ranks first, ahead of score. So when Bladder
and Digestion are both below seek, `relieve_both` is offered once and wins over `relieve_bladder` and
`relieve_bowels` every time; when only one is below seek, `relieve_both` is not offered.

Any known latrine is taken before a spot on the ground, at any distance. With none, the grid's spots
off the shore are scored as the toilet finder scores them; with no such spot, or one with a route
verdict, the colonist relieves itself where it stands.

Sleep takes any known bed before the ground, at any distance, and the ground where the colonist stands
when it knows none.

Drinking and washing are separate chains, so a colonist low on both takes the critical one first, else
the one whose behavior is included first
([AI behavior](../design/game-systems/colonists/ai-behavior.md#ordinary_need)).

`common/sleeping_on_the_ground`, which a cow or a deer includes, has Energy with a chain whose only
query candidate is `<from place="here"/>`.

---

## Actions

An action is what a `do` step performs where the creature stands. The engine provides a fixed set of
effects in C++; an action entry is a named use of them, with the values to use, how long it takes,
whether it needs free hands, whether it may be interrupted, and when it pays.

### Primitive effects

| Effect | Does |
|---|---|
| `<restore need amount>` | moves a need by a signed amount, the creature's own or, with `who="target"`, the target creature's. `amount` is a number, or `from` names a property of what the step consumes (`nutrition`) with `scale`, or `scaleBy` names a fact of the step's target (`sleep_quality`). With `seconds` and `upTo`, it restores `amount` per that many seconds and the action is open-ended, ending when the level reaches `upTo` |
| `<consume capability count>` | uses up held units with the capability. An action that consumes is offered only while the creature holds such a unit |
| `<take>` | takes from the target into hands or pack: cuts a harvestable (its yield, its regrowth), picks up from a pile, withdraws from a box, lifts a packaged item. How much is the step's amount ([Amounts](#amounts)); `clears="true"` removes the source whatever it is, for a site clear |
| `<put>` | puts what the step carries into the target: a box, a station's store, a blueprint's manifest, metered to what the target still needs and has room for |
| `<work rate direction>` | advances the work saved on the target, a blueprint's `workDone`, up or down, at the rate the named skill gives. The action is open-ended and ends when the work reaches its bound |
| `<craft>` | makes one unit of the station's current job at the end of a fixed-duration action ([Craft completion](#craft-completion)) |
| `<spawn def>` | creates a thing where the creature stands (a bio pile), or with `into="hands"` in its hands |
| `<install>` | puts the packaged item the creature carries down at its placement target and unpackages it |
| `<damage amount>` | lowers the target's condition by the amount; a target with no condition is a load error for the step's query |

Giving a thought is not an action effect: a chain gives one with a `<think>` step, and a thought type
reacts to an action with its own `actionStarts` and `actionFinishes` sources, which name action entry
ids ([Thoughts](../design/game-systems/colonists/thoughts.md)). Effects apply in the order written.
Effect rows carry no `when`: an action whose effects differ is two actions.

### An action entry

- Duration is one of `seconds`, a fixed time; `workFrom="durability" skill="harvesting"`, the target's
  durability over the work rate that skill gives; or nothing, with an open-ended effect (`<work>`, or
  a `<restore>` with `upTo`), so the action runs until its bound.
- `hands="free"` means the action needs free hands, and the hands rules run when its chain is selected
  ([Hands](#hands)); `hands="any"`, the default, means the creature keeps carrying.
- `interruptable` is whether a running action yields to an option of the same or a higher rung; one
  that is not yields to nothing until it ends (requirement 4).
- `pays="end"`, the default, applies the effects when the action completes, so an interrupted action
  gives nothing. `pays="along"` applies them as it goes, so an interrupted action keeps what it earned.
- `asleep="true"` marks the creature asleep while the action runs: its suspended record counts for no
  one else ([What survives an interruption](#what-survives-an-interruption)).
- `on` names the target an action works on (a capability, `storage`, `loose_item`, or `packaged`),
  for action groups.

### Paying out as it goes

With `pays="along"` each tick applies the share of the action's effects earned since the last tick. A
`curve` shapes it: by the fraction `f` of the action done the creature has earned `total x f^curve`,
so `curve="1"` (the default) pays evenly and a larger curve pays little at first and more toward the
end. A whole action earns the whole total and completion adds nothing. An open-ended action pays at its
rate as it goes.

A long, low-value activity relies on this: enjoying nature restores a little Recreation over a long
time, and a colonist pulled away by hunger keeps what it gained.

### Action groups

```xml
<actionGroup id="take">harvest pickup withdraw</actionGroup>
```

A `do` step that names a group performs the member whose `on` matches its target. One food query over
bushes, piles, and boxes is followed by one take step: the target the query picked is a bush, a pile,
or a box, and the group carries the action that goes with each. A group whose members do not cover
every target its step's query can return is a load error naming the ones left out. `clear` shares
`on` with `harvest` and is in no group with it; a site clear is its own chain.

### The shared actions

These are in `assets/config/actions/`, with the values the hand-written factories use.

| Action | Effects | Duration | Hands | Interruptable | Pays |
|---|---|---|---|---|---|
| harvest (`on` harvestable) | take | from durability, harvesting | free | no | end |
| clear (`on` harvestable) | take, clears | from durability, harvesting | free | no | end |
| pickup (`on` loose_item) | take | 0.5 s | free | no | end |
| withdraw (`on` storage) | take | 0.5 s | free | no | end |
| deposit | put | 1 s | any | no | end |
| craft | craft, crafting | the job's work amount over the work rate | free | no | end |
| build | work, construction, up | open-ended | free | no | along |
| deconstruct | work, construction, down | open-ended | free | no | along |
| pickup_packaged (`on` packaged) | take | 1.5 s | free | no | end |
| place_packaged | install | 2 s | free | no | end |

Deposit keeps the hands as they are because a delivery carries its goods in them.

### Craft completion

The `<craft>` effect is one engine service with the rules the craft has:

- Repeated input lines in a recipe are summed, and the sum is what the station must hold.
- Every input is checked at the start and again at completion; nothing is spent unless all are there.
- Inputs are spent from the station's store.
- Furniture is spawned packaged, through the deferred spawn queue, and placed by `place_packaged`.
- Any other output goes to the crafter's hands, then belt, then pack, then is stored or dropped.
- The item-crafted event fires, the job's completed count goes up, a finished job is cleaned up, and
  the job's progress resets.

### Construction work

The `<work>` effect on a blueprint keeps the construction guards:

- A build needs the blueprint under construction and no demolish order; a deconstruct needs work done
  above 0.
- A builder arriving after completion is cleared.
- Completion fires exactly once, when the bound is crossed, whatever number of builders work the
  site, and not when a demolish order lands in the completing tick.
- A teardown begins once, and its refund is paid at completion.

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
| `<from capability="drinkable"/>` | remembered things with that capability, as asset definitions spell it |
| `<from kind="bed"/>` | what a declared kind of the condition vocabulary maps to (`<Kind>` in `assets/config/conditions`): things under its capability, or movers of its definition seen within the recent-sighting window |
| `<from item="Berry"/>` | remembered things of one asset definition |
| `<from yields="Wood"/>` or `yields="edible"` | remembered harvestables whose yield is the item, or whose yield has the capability (the registry's `harvestablesYielding`) |
| `<from pile="Wood"/>` or `pile="edible"` | remembered loose piles of the item, or of any item with the capability |
| `<from box="Wood"/>` or `box="edible"` | storage boxes the colony knows that hold the item, or any item with the capability; each (box, item) pair is a candidate, so a box with two foods is two |
| `<from place="here"/>` | where the creature stands, as a place with no thing in it |
| any row with `fallback="true"` | its candidates are taken only when no candidate of the other rows survives, and no exclusion or prefer row removes them |
| `<from place="around" radius="r" count="n" minRadius="m"/>` | up to `n` points, each at a seeded random angle and a distance between `m` (default 0) and `r`, kept only where the creature's disc fits on walkable ground joined to it |
| `<from place="grid" spacing="s" radius="r"/>` | points on a grid of spacing `s` around the creature out to `r`, walkable and joined to it |
| `<from goal="destination"/>` | the destination of the goal the work serves: a box, a station, a blueprint, a creature |
| `<from goal="spots"/>` | the builder spots of the goal's foundation |

In a work entry's queries, an item argument may be `goal`, the item the option is for
([A work entry](#a-work-entry)).

A definition-scoped fact of a harvestable reads the definition of its yield, so a berry bush's
`quality` is the berry's.

Every query also passes over, without rows of its own, what the decision already passes over: a target
with a route verdict ([AI behavior](../design/game-systems/colonists/ai-behavior.md#what-a-colonist-can-get-to)),
a source that is regrowing or exhausted, a target not joined to the creature on the nav mesh, a
packaged box as a source or destination, and a target claims take against the creature
([Claims and the query](#claims-and-the-query)).

### Facts

A fact is one number about this candidate, for this creature, at this moment. Facts are an enumerated
set provided by the sim in C++, like the condition terms, and each declares its range and its scope:
definition when it depends only on the candidate's asset definition and the creature, item when it
depends on the one thing. A fact may take one argument, as a condition term does (`contains(meat)`). A
fact the sim declares and nothing answers reads 0, and the load logs once that it is unanswered.

The values a fact reads from tuning are in `assets/config/facts/`, never in C++:

```xml
<facts>
  <fact id="on_shore" within="1.5"/>
  <fact id="waste_nearby" within="8"/>
  <fact id="food_nearby" within="15"/>
  <fact id="sleep_quality" ground="0.5"/>
</facts>
```

Every fact is also a condition term of the same name taking a target (`built_by_self(target)`), so a
low-mood behavior's roll multiplier can read what its query weighed.

| Fact | Scope | Range | Meaning |
|---|---|---|---|
| `distance` | item | 0 m and up | straight-line metres from the creature |
| `held` | item | 0 or 1 | another task holds a claim on it that does not take it from this creature (food held by a less urgent eater or by work) |
| `on_shore` | item | 0 or 1 | a place within the tuned distance of water, that distance included |
| `waste_nearby` | item | 0 and up | the sum, over remembered waste things (bio piles) within the tuned distance `r`, of `1 - d/r` for each at distance `d` |
| `food_nearby` | item | 0 and up | the same sum over remembered edible things within its tuned distance |
| `is_place` | item | 0 or 1 | the candidate is a place (`here`, `around`, `grid`) rather than a thing |
| `sleep_quality` | item | 0 to 1 | how well the creature sleeps there: the tuned ground value on the ground, 1 at a sleepable thing |
| `below_destination` | item | 0 or 1 | for a box candidate, its priority for the goal's item is strictly below the destination's |
| `quality` | definition | 0 to 1 | the edible capability's `quality`, Terrible 0 to Excellent 1 in steps of 0.25 |
| `liking` | definition | -1 to 1 | the creature's like or dislike of this kind of thing ([Thoughts](../design/game-systems/colonists/thoughts.md#recalling-old-thoughts)) |
| `against_diet` | definition | 0 or 1 | the thing holds something against the creature's diet (a vegetarian and meat), by ingredient tags on definitions; it is weighed, never excluded on |
| `contains(tag)` | definition | 0 or 1 | the definition carries the ingredient tag |
| `eaten_recently` | definition | 0 to 1 | the share of the creature's recent meals that were this kind of thing |
| `freshness` | item | 0 to 1 | 1 fresh, 0 spoiled |
| `maker_opinion` | item | -1 to 1 | the creature's opinion of who made it, 0 when nobody did or the maker is unknown |
| `fits_meal_time` | definition | 0 or 1 | the definition's `meal` tag names the meal of the hour, by the meal hours in the condition vocabulary |
| `comfort` | definition | 0 to 1 | how comfortable a place to sit, sleep, or eat is, the `comfort` of the definition's furniture capability |
| `opinion` | item | -1 to 1 | for a human candidate, the creature's opinion of them |
| `mood_source` | item | -1 to 1 | for a human candidate, how much of how the creature feels right now is about them, and which way: the held mood effects whose thought has that human as its subject, as a share of all it holds, below 0 when they have lowered its mood and above 0 when they have lifted it |
| `owner_opinion` | item | -1 to 1 | the creature's opinion of the thing's owner, 0 when unowned |
| `has_owner` | item | 0 or 1 | the thing has an owner |
| `owned_by_self` | item | 0 or 1 | the creature owns it |
| `owner_present` | item | 0 or 1 | the owner was seen beside it within the recent-sighting window |
| `built_by_self` | item | 0 or 1 | the creature built it |
| `flammable` | definition | 0 or 1 | the thing burns |
| `colony_stock` | definition | 0 and up | how many of this definition the colony knows it holds |

Context goes into facts, not branching. Eggs in the morning is a `meal` tag on the egg's definition and
one fact, `fits_meal_time`; no query asks what hour it is.

### Rows

- `<weigh fact by>` adds the fact times `by` to the score. `by` is a signed number. It may carry one
  `when` for a one-off (`<weigh fact="comfort" by="3" when="need(energy) &lt; 30"/>`).
- `<exclude fact>` removes a candidate whose fact is above 0; `above="v"` or `below="v"` moves the line
  (`<exclude fact="distance" above="15"/>`). An exclusion on a definition-scoped fact removes every item
  of that definition at once. It may carry one `when`.
- `<prefer fact>` splits the candidates in two classes by the same line an exclusion uses: a candidate
  that the row would exclude ranks below every candidate it would keep, whatever their scores, and is
  picked only when no kept candidate survives. A query's prefer rows apply together.
- `<scale trait fact by>` multiplies that fact's weight by `by` for a creature with the trait. Several
  scale rows that match multiply together. A trait the trait registry lacks is a load error, as it is in
  a condition.

A query weighs `distance` with a negative weight or excludes it above a bound; one that does neither is
a load error, since nothing would end its search (see [Evaluation](#evaluation)). A query only counted
(`count_of`), which no `go` step, work `<source>`, or low-mood `<target>` names, ranks nothing and is exempt.

`<prefer fact="held"/>` is the food rule of [Task claims](./task-claims.md#need-tasks): free
food first at any distance, held food only when no free food is known, and food that is taken never.

### Picking: the best, or a weighted draw

A query picks in one of two ways, stated on the entry.

- `pick="best"`, the default: the highest score wins.
- `pick="weighted"`: every surviving candidate has a chance, and a higher score makes it likelier, not
  certain. Each candidate's chance is in proportion to its score less the lowest surviving score, plus
  1, so the worst candidate still has a small chance and the best has the largest; with no weigh rows
  every candidate is as likely. The draw uses the sim's seeded generator, so the same game gives the
  same pick.

A weighted query draws from a set, so it cannot stop at the first winner. It must exclude `distance`
above a bound, which keeps the set small, and it draws from at most the 16 best candidates. One that
does not bound distance is a load error.

### Creatures differ through facts, traits, and their behaviors

Per-creature differences come from the facts reading the creature (its likes, its opinions, its meals),
from traits scaling weights, and from the behaviors a creature includes: a vulture's feeding behavior
has its own food query, weighing `freshness` by 0 or below it, where a human's weighs it up.

### Evaluation

The case to design for is a creature that knows 10,000 edible items (`Memory::kMaxWorldEntities` is
10,000). A script called on each of them per decision is what this design avoids.

1. Score definitions, not items. The terms whose facts are definition-scoped are computed once per
   definition and cached per creature and query, stamped with what they read (the creature's likes, its
   meal record, the meal of the hour, its traits), and recomputed only when a stamp moves. An exclusion
   on a definition-scoped fact removes every item of that definition before any is visited, and the
   work gates that depend on a definition (room to carry its yield, the tool to cut it, a box's room
   for it) are answered there, before any of its items is visited.
2. Walk nearest first and stop early. Candidates are visited nearest first through a spatial index per
   definition over what the creature remembers, kept at the same remember and forget points as
   `Memory::worldEntitiesByDef`. Because the distance weight is declared and every other fact has a
   declared range, the engine knows for each definition the best score an item of it could reach apart
   from distance: its cached definition terms plus, for each item-scoped fact, the larger of the weight
   times the range's ends. That bound less the best score found so far, divided by the distance weight's
   size, is the farthest an item of that definition could still win from. The walk passes over an item
   beyond its definition's horizon and ends when the next candidate is beyond every horizon. A prefer
   row's lower class is walked only when the upper class has no survivor.
3. Per-item facts only for survivors. An item-scoped fact other than distance is read only for a
   candidate inside its definition's horizon that no exclusion removed.
4. Keep a chosen target. A creature heading for a target a query chose keeps it while it is valid; a
   query is not run again at every decision. It is run again when the target fails (gone, a route
   verdict, claimed against it) or when the step that uses it starts again. A `meal` amount is sized
   again at every decision that keeps the task, and its source claim follows.
5. Straight-line distance for scoring. Reach is the component-label compare; a route is asked only for
   the winner, or, for a need, for the nearest few survivors at once through the nearest-of route
   request. The request names only candidates in the winner's prefer class. The claims move to the
   candidate the route reached, and a reached candidate taken in the meantime ends the task. A round
   whose nearest few are all sealed asks the next few at the next decision.
6. Wait when everything is passed over. A query whose every candidate was passed over (regrowing,
   verdicts, taken) records a wait, and is not walked again until something it read changes: a
   remembered thing added, a claim released, a source ready again, a box's contents changed.
7. Answer once per decision. A source's reach answer is shared by every query and work entry that asks
   in the same decision; each pile is offered to one goal only per decision; the colony's food boxes
   are listed once per update.
8. No allocation in the steady state. The walk, the definition cache, and the results use scratch
   buffers the query system owns and reuses across creatures. Results are in a total order: score,
   then distance, then the memory key. Nothing depends on hash or view order.

### Claims and the query

A query run for a step that names a claim passes over every candidate whose claims take it from this
creature, the way it passes over a route verdict, with no row of its own: a use target another task
holds, a pile or box with nothing left once others' source claims are taken out, a station slot or
builder spot another holds, a box with no room for the item once others' inbound is counted. The
creature's own task and suspended record are left out of what it reads (`ClaimExclusion`), so its own
claim never reads as taken (colony work requirement 3).

For food the line between taken and held follows the holder, as [Task claims](./task-claims.md#need-tasks)
says. An eater is a holder whose option serves the same need on a rung no lower than this creature's
option: an eater takes the food, and anything else only holds it, which `held` reads. An idle chain
reads any holder as taking it.

A candidate passed over this way is not lost from the decision trace: the first one in score order that
ranks ahead of every candidate offered becomes the option's Reserved row, naming its holder
([Task claims](./task-claims.md#reserved-rows-and-option-dedup)).

---

## Chains

A chain is a straight list of steps, each performed once. A step is one of an enumerated set of step
kinds provided in C++. A leg is a `go` step and the `do` step after it.

| Step | Does |
|---|---|
| `<go query="..."/>` or `<go to="..."/>` | walks to the target a query picks, or to a target the option already has: `target` (an order's or a low-mood behavior's), `source` (the work option's source), `destination` (the goal's destination) |
| `<do action="..."/>` | performs an action, or an action group's member for the target, where the creature stands; `on` names the thing it works on when that is not the step it walked to (`on="destination"`) |
| `<have item="..." fetch="..."/>` or `<have capability="...">` | a goal: met when the creature carries the option's planned amount of the item beyond the cargo it was handed, or for a packaged target, carries that one thing, or the packaged thing is already at its placement target; otherwise it runs the named chain, which gets it |
| `<think thought="..." to="self"/>` | gives a thought to `self`, `target`, or `witnesses` (every human who saw the step) |
| `<need id="..." change="..."/>` | moves one of the creature's needs by a signed amount |
| `<drop/>` | sets down what the chain's steps took, by the drop path (a box within reach that takes it, else a pile) |
| `<run chain="..."/>` | runs another chain as this step; chains nest |
| `<leave_colony/>` | ends the creature's colony membership; the engine walks it to the edge of the simulated area and unloads it there |

A `do` names an action entry or an action group; one that does not exist is a load error. Taking a
thing and damaging one are actions, so they are `do` steps with the action's duration, hands, and
arrival rules.

A take step that starts with the goods already held completes as having lifted what is carried, and
the chain goes on to its deposit leg.

### Ending a step

A `go` ends on arrival. A `do` ends when its action completes, or, for an open-ended action, when the
action's bound is reached. A step also ends on the events the engine reports for it, written as
`<ends on="..."/>`: `held_gone` (what the creature holds for the step is used up), `target_gone` (the
target no longer exists or is no longer where it was), or `target_left` (a creature target walked
away). A step never ends on a condition.

### When a target is resolved

A step that needs a target runs its query when the step starts, not when the chain is chosen, because
the world may have changed in between. Two kinds of target are resolved at decision time instead: the
first step's, because the AI ranks options partly by distance and must know the chain can be started
at all, and every target a step names a claim on, because claims are taken at selection
([Claims on steps](#claims-on-steps)). For a work option that is its source and its destination. A
chain whose first step finds no target is not offered, except as below.

A chain whose query finds nothing but a `<from place="here"/>` candidate runs where the creature
stands, so a relief or a sleep with no better place falls back to the ground.

### Claims on steps

A `do` step names the claims it holds with `claims`, from the claim kinds of
[Task claims](./task-claims.md#claim-kinds): `use`, `source_quantity`, `slot`, `inbound`, and
`in_flight`. Each kind keys on what that document says it keys on: use and source quantity on the
step's target, a slot on the goal's destination (numbered with the builder spot when the option's
source is one), inbound on the destination, in-flight on the root goal and the item. Use is for a
target taken whole (a harvestable, a packaged item, a creature) and source quantity for a pile or a
box, so a step whose query can return either names both, and its target holds the one that fits it.

- Every claim a chain names, in its own steps and in the chains its unmet `have` steps would run, is
  taken when the option is selected, in the same update, through the claim index, so a creature
  deciding later in that update sees it (colony work requirement 2). A `have` met at selection takes
  none of its fetch chain's claims.
- A claim on a later step is sized at selection from the chain's planned amount. For a haul, source
  quantity is the take's amount Q, and inbound and in-flight are Q plus the cargo handed over. For a
  cut, inbound is the cargo C already carried and in-flight is C plus the cut's expected yield E.
- When a take completes, the claims that follow the goods (inbound, in-flight) are rewritten to what
  it lifted, and its own source claim drops. Any other claim is held until the step naming it
  completes. The task's last step completing ends the task and every claim with it.
- A task whose route failed holds only the cargo it carries: inbound less the source quantity still to
  take.
- A chain naming more claims than a task holds (four on the task, two on a suspended record) is a load
  error.
- Claims live on the task and nowhere else. A task cleared by any path, a creature taken under direct
  control, or a creature destroyed leaves no claim behind (colony work requirement 1).

### Progress is engine state

The chain's progress (which step, the targets resolved so far, when the step began, whether its
delivery was chosen) is state the engine holds on the task, so the UI can show it and it can be saved.
No script holds it.

### What survives an interruption

When another option takes over, the task's chain progress, its targets, and its claims end with it.
State survives in two places only, and both belong to the world or the goods rather than to the chain:

- On the thing worked. A blueprint's `workDone` keeps what was built.
- On the goods carried. A work task carrying cargo toward a root (a haul past its pickup, a cut
  carrying what earlier cuts of its gathering landed) that is interrupted by a need, with the goods
  kept, becomes the task's suspended record: its instance id, claim sequence, root, item, and inbound
  and in-flight for the goods held ([Task claims](./task-claims.md#interruption-by-the-colonists-own-needs)).
  Another need task keeps the record; the carry-in to the record's root, item, and destination, or a
  further cut for that root and item, adopts it, taking its instance id and claims; any other selection
  discards it; and while an `asleep` action runs, the record counts for no one else. Resuming is that
  adoption, a new task under the old identity, not a replay of the old chain from its step.

Need chains, idle chains, and player orders keep nothing: the next decision chooses afresh. A low-mood
behavior's chain never resumes: an episode that a critical need interrupts is over, and the behavior's
relief and affinity follow from how it ended (Mood).

### The runner

A chain is not a second engine. It runs on the existing `Task`, `Action`, and routing machinery,
which keeps every hard-won fix that lives there:

- A chain-run is the task. `Task` keeps its routing, claim, rung, and timing fields and gains the
  chain id, the step index, the resolved targets, and the step's start time, in place of the per-type
  fields.
- A go step sets the task's target through `ActionSystem::headTo`, which cancels the old leg's route
  ticket, invalidates the path, and leaves the AI to ask a new route.
- The running task matches an offered option by identity: the entry, the goal, the item, the source or
  the destination, and the builder slot, with a target that has no entity matched within 0.5 m. The
  step is not compared. A chain past its take is the `carry_in` option for its root, item, and
  destination, so a hauler on its deposit leg keeps its hysteresis and its claims.
- A do step is an ordinary `Action`, started by `ActionSystem` on arrival within `kActionReachMeters`
  through one start path that checks the target is still there and still what it was, and completed
  through `completeAction`, which reports whether its effect took.
- Selection, the switch gate, claims, and route answers are `AIDecisionSystem`'s.
- The option a chain becomes in the decision trace names its entry (the need, the work, the order, the
  low-mood behavior) and its target, with the entry's label as its reason text; the trace lists every
  option offered, and a query's early stop applies inside the query only. `current_task(...)` reads
  chain ids and the declared task groups, which list chain ids.

---

## Work

Colony work is chains offered for goals. The goal systems say what work exists and how much; a work
entry says which chain does it and which queries pick its source; the decision loop offers one option
per goal, item, and work entry to every colony member.

### A work entry

```xml
<colonyWork>
  <category id="hauling" label="Hauling" order="6"/>

  <work id="haul_pile" goal="haul" category="hauling" rung="opportunistic_work" gathers="true">
    <source query="pile_for_goal"/>
    <chain id="haul_from_pile"/>
  </work>
</colonyWork>
```

- `goal` names the goal type the entry serves (`haul`, `harvest`, `clear`, `craft`, `build`,
  `deconstruct`, `place_packaged`, `care`, with `need` naming the need for a care goal), or `root`
  for any root goal (the carry-in). The engine offers the entry once per goal of that type and, for
  goals that take items, once per item the goal still wants; `goal` in the entry's queries is that
  item.
- `<source>` names the query that picks the option's source; an entry with none takes its target from
  the goal (a craft's station, a teardown's structure) or, with `source="carried"`, from what the
  creature carries toward the root.
- `<chain>` names the chain.
- `rung` is the entry's base rung. The engine promotes it by rules that hold for every work entry: work
  whose root goal is a craft or a build, and work carrying a packaged thing to its placement, is on the
  `committed_work` rung.
- `gathers="true"` puts the entry's legs in a gathering ([Legs](#legs)).
- `<category>` declares a category, its label, its default order, and `canDisable`. A colony member's
  work settings name categories and work entries.

`work-types.xml`'s categories are kept because [Work Priorities](../design/game-systems/colonists/work-priorities.md)
needs them: the player turns a category on or off for a colonist and, in detailed mode, sets a 1 to 9
priority per work type. A work entry names its category, and a category is the unit those settings
attach to. The per-colonist priority is a within-rung score factor for the work it covers, as
[Colonist task arbitration](./colonist-task-arbitration.md#work-priorities) records.

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
  ([Need tasks](./task-claims.md#need-tasks)). A take toward a meal makes room in a full pack first.
- `carried`: what the creature carries toward the root.

A take asks for its amount and takes what is there up to it; the claims that follow the goods are
rewritten to what it lifted, and a take that lifts nothing ends the task at the source. A put is
metered to what the destination still needs and has room for: a station takes exactly the recipe's
remaining need and the rest stays carried with no claim. What a box or a hand cannot take bounces back
to the carrier, and a deposit into a full box keeps the goods and does not finish. A cut gives its whole
yield, and what the creature cannot carry drops beside the source as a pile, or just outside the
footprint for a site clear ([Chain claims](./task-claims.md#chain-claims)). A cut checks its tool at
the start. A creature's own food is provisions, never cargo, in every amount above
([Provisions](./task-claims.md#provisions)). An option whose amount is zero is not offered, and shows
as NoRoom when the creature's own load is what stands in the way.

#### Hands

An action entry's `hands="free"` is what `needsHands` was. The hands rules run when an option is
selected, where the creature stands, keyed on the first `do` the selected chain will perform, counting the fetch chain of a `have` that is not met: a one-hand
item to the belt, else the pack; a two-hand armful, or one-hand goods with no room, into a box within
reach that takes them, else onto the ground; a packaged item set down still packaged. A carry-in
delivers what the creature holds and needs no free hands. Any option on a strictly higher rung
interrupts a task carrying a two-hand armful its root still wants, and nothing else does, nor a cut of
the same item. A creature about to idle with a two-hand armful no root wants puts it down first. A
take toward a meal makes room in a full pack, never at the cost of a tool or food
([Interruption](./task-claims.md#interruption-by-the-colonists-own-needs),
[Carry-in from possession](./task-claims.md#carry-in-from-possession),
[Chain_PlaceItem](./task-claims.md#chain_placeitem), [Need tasks](./task-claims.md#need-tasks),
[Task chains](../design/game-systems/colonists/task-chains.md)).

#### Legs

- A chain's next leg is the same task: a step completing advances the chain in place, so a pickup
  becomes its deposit leg and a landed cut its carry-in.
- A gathering moves its cargo from task to task: a landed cut or a gathering pickup ends its task and
  asks for a decision at once, and the next leg's option adopts the cargo under the same instance id,
  its claims sized from that cargo: a cut adds use and raises in-flight by its expected yield; a pile
  adds source quantity Q and raises inbound and in-flight to the cargo plus Q.
- Which leg a chain is on is decided by what the creature carries, not by where it stands, through the
  `have` step and the take that finds its goods already held
  ([Chain claims](./task-claims.md#fetch-from-stock-chain_pickupdeposit)).
- The destination of a work option is reach-tested at selection with its source: a label compare and
  the creature's verdicts, so a haul whose deposit has no known way is not picked
  ([Reserved rows](./task-claims.md#reserved-rows-and-option-dedup)).
- A delivery the decision keeps or selects is chosen, and nothing on its rung turns it back until the
  deposit ([Batching](./task-claims.md#batching)).
- Entries with `gathers="true"` (the cut, the pile pickup, and the carry-in toward one root and item)
  form a gathering: the creature fills its trip before walking, a known pile comes before a fresh cut,
  a top-up cut is taken only when its yield fits, and the cargo moves from task to task under one
  instance id ([Batching](./task-claims.md#batching)).
- A need between legs leaves the suspended record ([What survives an interruption](#what-survives-an-interruption)).

#### Builder spots and station slots

A station has one unnumbered slot, held by the creature whose craft step names `slot`. A foundation's
builder spots are candidates of `<from goal="spots"/>`: the query passes over a spot another holds and
a spot whose stand point does not serve, and a builder deciding again keeps its own spot by the chosen
target rule ([Evaluation](#evaluation), rule 4). The spots, their stand points, and their spacing are
[Task claims, Build and Deconstruct](./task-claims.md#build-and-deconstruct).

### The haul

```xml
<colonyWork>
  <work id="haul_pile" goal="haul" category="hauling" rung="opportunistic_work" gathers="true">
    <source query="pile_for_goal"/>
    <chain id="haul_from_pile"/>
  </work>
  <work id="pull_from_box" goal="haul" category="hauling" rung="opportunistic_work">
    <source query="box_to_pull_from"/>
    <chain id="haul_from_box"/>
  </work>
  <work id="carry_in" goal="root" source="carried" category="hauling" rung="opportunistic_work" gathers="true">
    <chain id="carry_in"/>
  </work>

  <chain id="haul_from_pile">
    <have item="goal" fetch="pick_up_for_goal"/>
    <go to="destination"/>
    <do action="deposit" claims="inbound in_flight" amount="carried"/>
  </chain>
  <chain id="pick_up_for_goal">
    <go to="source"/>
    <do action="pickup" claims="source_quantity"/>
  </chain>
  <chain id="haul_from_box">
    <have item="goal" fetch="withdraw_for_goal"/>
    <go to="destination"/>
    <do action="deposit" claims="inbound in_flight" amount="carried"/>
  </chain>
  <chain id="withdraw_for_goal">
    <go to="source"/>
    <do action="withdraw" claims="source_quantity"/>
  </chain>
  <chain id="carry_in">
    <go to="destination"/>
    <do action="deposit" claims="inbound in_flight" amount="carried"/>
  </chain>

  <query id="pile_for_goal">
    <from pile="goal"/>
    <weigh fact="distance" by="-1"/>
  </query>
  <query id="box_to_pull_from">
    <from box="goal"/>
    <exclude fact="below_destination" below="1"/>
    <weigh fact="distance" by="-1"/>
  </query>
</colonyWork>
```

The haul goal's destination is a storage box for a storage root, a station's store for a craft
fetch, and a blueprint for a site delivery. A pull only ever moves goods to a box of strictly higher
priority, never past the destination's maximum, never below the source's minimum, and only from a box
the colony knows, so pulls never form a cycle; when two destinations want one source, the higher one
wins.

### The harvest for a goal

```xml
<colonyWork>
  <work id="cut_for_goal" goal="harvest" category="farming" rung="opportunistic_work" gathers="true">
    <source query="cut_for_goal"/>
    <chain id="cut_and_carry_in"/>
  </work>
  <chain id="cut_and_carry_in">
    <go to="source"/>
    <do action="harvest" claims="use"/>
    <go to="destination"/>
    <do action="deposit" claims="inbound in_flight" amount="carried"/>
  </chain>
  <query id="cut_for_goal">
    <from yields="goal"/>
    <weigh fact="distance" by="-1"/>
  </query>
</colonyWork>
```

The inbound and in-flight on the deposit are sized at selection from the cut's expected yield, and
rewritten when the cut lands to what it landed, capped at the root's need: the in-flight is what keeps
a second colonist from cutting what this one's cut covers. The site clear is the same shape with the
`clear` action and no deposit: it brings nothing toward a need and is never a leg of a gathering.

### The craft and its carry-ins

```xml
<colonyWork>
  <work id="craft" goal="craft" category="crafting" rung="committed_work">
    <chain id="craft_at_station"/>
  </work>
  <chain id="craft_at_station">
    <go to="destination"/>
    <do action="craft" claims="slot"/>
  </chain>
</colonyWork>
```

The craft's inputs come through the craft goal's children, a haul goal and a harvest goal per input,
which `haul_pile`, `cut_for_goal`, and `carry_in` serve with the station's store as their destination
and the craft as their root, so any number of colonists provision one order in parallel through their
in-flight claims and none over-delivers. The craft step holds the station's slot from selection, and
the craft option is offered only for a station the creature can reach.

### The build

```xml
<colonyWork>
  <work id="build" goal="build" category="construction" rung="committed_work">
    <source query="builder_spot"/>
    <chain id="build_at_spot"/>
  </work>
  <chain id="build_at_spot">
    <go to="source"/>
    <do action="build" on="destination" claims="slot"/>
  </chain>
  <query id="builder_spot">
    <from goal="spots"/>
    <weigh fact="distance" by="-1"/>
  </query>
</colonyWork>
```

`on="destination"` makes the do step work on the blueprint while standing at the spot. A build goal
with no spots (an opening) has the blueprint's nearest walkable point as its one candidate and
takes no claim; a wall's builders stand on a side its goal allows ([task claims](task-claims.md#build-and-deconstruct)). A site ordered demolished is not offered as build work, and a build step on it ends
before it adds work, the build action's open-ended work checking its site each tick
([Construction work](#construction-work)). Materials reach the site through the build goal's due
materials and the same haul, cut, and carry-in entries.

### The other work

| Work | Goal | Rung | Chain |
|---|---|---|---|
| `deconstruct` | deconstruct | `committed_work` | go to the structure's nearest walkable point; `deconstruct`; no claim |
| `place_packaged` | place_packaged | `opportunistic_work` | `have item` the packaged thing, fetched by go to it and `pickup_packaged` with `claims="use"`; go to its placement target; `place_packaged`. A carrier who selects anything else sets it down still packaged, off any site |
| `clear_site` | clear | `committed_work` | go to the blocker; `clear` with `claims="use"` |

---

## Rungs

The ladder is a list of named rungs, in order, declared once in `assets/config/arbitration/`:

```xml
<arbitration>
  <ladder>
    <rung name="survival"/>
    <rung name="critical_need"/>
    <rung name="low_mood"/>
    <rung name="player_order"/>
    <rung name="urgent_need"/>
    <rung name="own_unfinished_work"/>
    <rung name="committed_work"/>
    <rung name="ordinary_need"/>
    <rung name="opportunistic_work"/>
    <rung name="idle"/>
  </ladder>
</arbitration>
```

No entry writes a tier number; it names a rung, and the rung's position is its tier. The rung for
assigned colony work is `committed_work`, since "order" means only a player's order, and the mood rung
is `low_mood`. This document is
the authority for the ladder's rungs and their order; [Colonist task arbitration](./colonist-task-arbitration.md)
is the authority for the (rung, score) key and the switch gate.

| Chain | Rung |
|---|---|
| a need's chain | `critical_need` or `ordinary_need`, by the need's level |
| a low-mood behavior | `low_mood` |
| a player's order | `player_order` |
| work whose root is a craft or a build, a craft, a build, a teardown, a site clear, a placement being carried | `committed_work` |
| other colony work | `opportunistic_work` |
| an idle chain | `idle` |

`urgent_need` and `own_unfinished_work` are given options by [An urgent-need rung](#an-urgent-need-rung)
and [Interruptable crafts](#interruptable-crafts) under Changes to behavior. `survival` is reserved for
the threats of [Interruptable crafts](./interruptable-crafts.md#the-ladder), and no entry names it.
Within a rung an option serving more of the creature's needs ranks first; then the score is
arbitration's, in this order of terms: distance, the gathering rank, skill, task age, the hysteresis
margin, a box's storage priority for stocking, and the colony member's priority for the work's
category.

---

## Idle and player orders

An idle chain is a behavior's `<idle>` row, offered on the idle rung. A creature's idle rows rank in
its order ([Creatures](#creatures)), ahead of any score, so a human gathers food before it wanders.

- With no option at all, the creature holds where it stands.
- A creature with no nav mesh under it is offered no wander.
- A walking wander decides every frame, so anything better is taken the moment it appears.
- `gather_food`'s row carries `when="carried_food &lt; 1"`, so it is offered only while the pack holds
  no food, and its take shows NoRoom when the pack has no room for a unit of the yield.

A player's order is a chain run on the player-order rung, with the target the player clicked as the
chain's target:

```xml
<orders>
  <order id="harvest" label="Harvest" on="harvestable" chain="harvest_target"/>
  <order id="relieve_here" label="Relieve self" on="ground" chain="relieve_at_target"/>
  <chain id="harvest_target">
    <go to="target"/>
    <do action="harvest" claims="use"/>
  </chain>
  <chain id="relieve_at_target">
    <go to="target"/>
    <do action="human/urinate"/>
  </chain>
</orders>
```

`on` says what the context menu offers the order on. An order takes the claims its chain names by the
same rules as anything else, and as the `player_order` rung it bumps a claim on its target held on a
lower rung ([Task claims, Bumping](./task-claims.md#bumping-planned)). There is no "move here"
order. Moving a
colonist is direct control: direct control suspends the AI, snaps the click to walkable ground, and returns the creature's
work to the pool ([Player control](../design/features/player-control.md)). Direct control and player
orders are unavailable to a colonist in a low-mood episode.

---

## Requirements on the chain runner

Each comes from what the existing multi-step task code had to solve, and names the test that encodes
the lesson.

1. When a task or a step changes or is cancelled, the `Action` in progress is cleared with it
   (`WokenSleeperCarriesOutTheNewTask`, `EnterControlClearsInProgressAction`).
2. Same-task detection compares identity as [The runner](#the-runner) states, not the step, and a task
   whose route failed is always a fresh pick (`HysteresisHoldsThenYieldsThroughUpdate`,
   `StoppedWanderRepicksFreshTarget`).
3. The hysteresis margin is applied at scoring, as the in-progress option's bonus, and not again at the
   switch gate (`HysteresisResistsThrashButYieldsToClearlyCloser`).
4. The rung gate holds: a non-interruptable action blocks every switch, and an interruptable one yields
   to the same or a higher rung (`ActionCompletesWithoutReEvaluationInterrupt`,
   `WokenSleeperCarriesOutTheNewTask`).
5. A creature decides at once on no task, on arrival, on a need newly critical, on the first route
   failure, on a landed cut or gathering pickup, or on a requested decision (`requestDecision`),
   otherwise on the 0.5 s interval, and never every frame while a failure persists
   (`CrossingIntoCriticalDecidesAtOnce`, `AColonistWhoseEveryRouteFailsDecidesOnTheInterval`).
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
12. When a selected chain's first action needs free hands, held goods are stowed or set down by the
    existing rules, a two-hand armful is never stranded, and a packaged item is set down still packaged
    (`ASwitchToBuildFreesTheHands`, `InterruptedTwoHandArmfulDropsOnceWithWholeQuantity`,
    `FurnitureWhosePlacementIsCancelledIsSetDownPackaged`).
13. Only what the creature remembers is targeted, and a regrowing or exhausted source is passed over at
    selection (`CannotFindUnknownEntities`, `RegrowingSourceIsPassedOverForAReadyOne`).
14. Every order the runner makes is total with a final tie-break, nothing depends on unordered
    iteration, and the only randomness is the seeded generators (`EqualKeyBreaksTiesOnTiebreakId`,
    `WanderSequenceFollowsSeed`).
15. A decision scans no creature's whole memory and reuses its scratch buffers; name lookups, cache
    sizes, and route requests stay bounded as memory and time grow
    (`NameLookupsPerDecisionDoNotGrowWithMemory`, `BoxRoomCacheStaysSizedToOneUpdate`,
    `RouteRequestsStayFewOverTenSecondsOfChurn`).
16. Every field the runner adds to `Task` says whether it survives `Task::clear()`; the chain, the step,
    its targets, and its start time are all reset by it, so an interrupted chain never resumes from its
    step. The suspended record survives `clear()` and is discarded only by the selection rules
    ([What survives an interruption](#what-survives-an-interruption)).
17. A creature off the nav mesh is snapped to the nearest floor, else the colony origin, else not moved,
    and while the snap or the first mesh is pending its target holds and it asks again once, when the
    ground lands (`PrefersNearestFloorThenColonyOriginThenNoMove`,
    `AColonistTheSnapHoldsAsksAgainOnlyOnceReleased`,
    `AColonistTheSnapNeverFreesLeavesTheWaitAfterAFewDecisions`,
    `ADecisionWhileTheSnapHoldsHimKeepsHisTask`, `TargetHoldsWhileGroundLandsElsewhere`,
    `IdleHoldTakesLiveWanderOnceMeshLands`, `HeldGatherFoodRouteRequestedOnceMeshLands`).
18. A nearest-of route request widens round by round and reaches a source while the route epoch moves
    (`NinthWaterIsReachedWhileTheEpochKeepsMoving`, `WhenTheNearestFewAreSealedTheNextDecisionAsksTheNext`).
19. A query whose every candidate was passed over is not walked again until something changes
    (`RegrowingFoodIsNotProbedAgainUntilSomethingChanges`, `ABoxThatGainsFoodEndsTheWaitOnRegrowingBushes`).
20. An idle creature gathers food before it wanders, and a wander point fits the creature's disc
    (`GatherFoodRanksAboveWanderInIdleTier`, `WanderPointsFitHisDisc`).
21. A relief or a sleep with no better place happens on the ground where the creature stands
    (`EnergyNeedUsesGroundFallback`, `BladderNeedUsesGroundFallback`,
    `UnreachableBedAndToiletFallBackToGround`).
22. Two needs met at one place are met one after the other, the critical one first
    (`ThirstyAndGrubbyColonistDrinksThenWashes`,
    `CriticalHygieneOnTheWayToDrinkWashesFirstAtTheSameShore`,
    `EquallyLowThirstAndHygieneAtOneShoreTieBreakToThirst`).

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
4. Food claims follow the eater: a more urgent eater takes food a routine one claimed first, equal
   urgency is first-come, work never holds food from a hungry creature, and free food is taken before
   held food (`AStarvingColonistBeatsARoutineEaterThatClaimedFirst`,
   `AStarvingColonistTakesBerriesAHaulerClaimed`, `FoodNobodyHoldsIsPreferredToFoodAWorkerHolds`,
   `OnlyAnotherEaterHoldsAHungryColonistsFood`).
5. A task whose route failed holds only claims on goods it carries
   (`AFailedRouteHoldsNothingItHasNotReached`, `AFailedRoutePastThePickupKeepsItsCargoClaimed`,
   `AFailedRouteThatSucceedsTakesItsClaimsBack`).
6. Supply is read from the world with no counters, so every unit of a multi-unit craft is provisioned
   and made (`MultiUnitCraftWithFetchedAndHarvestedInputsCompletes`), and each remaining need is worked
   out once per update (`EachRemainingNeedIsWorkedOutOncePerUpdate`).
7. In-flight claims keep creatures serving one root from over-gathering
   (`TwoColonistsServingOneGoalNeverOverDeliver`, `TwoColonistsDoNotBothCutWhatOneCutCovers`,
   `ASecondColonistDoesNotCutWhatTheFirstCarriesAndHasAccepted`, `TwoGatherersMeetANeedExactly`).
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
    `AnySelectionButANeedOrTheCarryInDiscardsTheRecord`, `AdoptionMatchesTheRecordsDestination`,
    `ARecordKeptByEatingStillHolds`, `CriticalHungerLeavesWorkForABoxAndReturns`).
13. A sleeping creature's record counts for no one else, another is offered what it carries, and on
    waking it delivers what is still wanted or drops a record another delivered
    (`AWokenSleeperWhoseGoodsWereFetchedByAnotherDropsHisRecord`,
    `AnotherColonistIsOfferedWhatASleeperCarries`, `AWokenColonistDeliversWhatIsStillWanted`).
14. A creature's own food is never cargo (`AColonistsOwnFoodIsNeverDelivered`,
    `FoodFetchedForACraftIsDeliveredAndTheMealKept`).
15. Nothing is offered when nothing would fit: no cut for a creature that cannot carry its yield, and no
    leg for less room than it brings (`OverweightColonistOffersNoHarvest`,
    `NoCutForABoxWithRoomForLessThanItLands`, `APileIsNoLegForTheLastUnitOfRoom`,
    `AColonistWithNoRoomDeliversInsteadOfCutting`).
16. An armful bound for a box goes to a site that wants it without being dropped
    (`AnArmfulBoundForABoxGoesToASiteWithoutBeingDropped`).
17. An interrupted two-hand armful is dropped once, whole (`InterruptedTwoHandArmfulDropsOnceWithWholeQuantity`).
18. A two-hand delivery holds against fresh work of its own rung and yields to a higher one
    (`TwoHandPullHoldsAgainstFreshSamePriorityPullMidCarry`, `TwoHandPullYieldsToCriticalNeedMidCarry`).
19. An armful nobody wants is put down before idling, and an armful whose root is lost is put down,
    while another site's pick does not drop it (`AnArmfulNobodyWantsIsPutDown`,
    `AnArmfulNobodyWantsIsPutDownToGatherFood`, `ALostRootPutsTheArmfulDown`,
    `AnotherSitesPickDoesNotDropTheArmful`).
20. Furniture whose placement is cancelled is set down packaged, off any site
    (`FurnitureWhosePlacementIsCancelledIsSetDownPackaged`, `FurnitureSetDownOnASiteGoesToGroundOffIt`).
21. A full pack still eats, giving up goods and never a tool (`AFullPackStillEatsFromABush`,
    `AFullPackGivesUpGoodsNotItsTool`, `ClaimedCargoGoesLast`).
22. The leg is decided by what is carried, not by where the creature stands: a take that finds its
    goods already held goes on to the deposit
    (`LooseFetchPicksUpWhenSourceOverlapsStation`, `CarryingAtSourceRedirectsToDepositInsteadOfDeadlock`).
23. A chosen delivery is not turned back by a source that becomes ready
    (`AChosenCarryInIsNotTurnedBackByAReadySource`).
24. The deposit leg is reach-tested at selection, and a hauler walled off from its deposit lets the
    delivery go, puts its armful down, and takes it up once the way opens
    (`HaulWhoseDepositIsNotConnectedIsNotPicked`,
    `AWalledOffHaulerLetsTheDeliveryGoAndTakesItUpOnceTheWayOpens`, `AWalledOffHaulerPutsHisArmfulDown`).
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
29. Pulls go only to a box of strictly higher priority, from a box the colony knows, never past the
    destination's maximum and never below the source's minimum, the higher destination winning a
    conflict (`NoPullBetweenSamePriorityBoxes`, `NoPullFromHigherPriorityBox`,
    `PullDoesNotDrainSourceBelowItsMin`, `PullDoesNotOverfillDestinationMax`,
    `PullConflictHigherDestinationWins`, `PullRequiresColonyToKnowSourceBox`,
    `StockingFillsToTheMinimumAndHaulingNeverPassesTheMaximum`, `LooseStockGoesToTheHigherPriorityBoxFirst`).
30. A goal visits only the remembered entries of the definitions that give its item, answering the
    per-definition gates first (`WorkGatesComeBeforeTheSources`, `HaulGoalsOfferEachPileOnce`,
    `HarvestGoalSeesEveryDefThatYieldsItsItem`, `FindsANeedSourceInALargeMemory`).
31. The development-build claim validator (`ClaimIndex::findViolation`) runs after every AI update that
    built the index and aborts on a violation, for claims named on steps
    ([Task claims, Testing](./task-claims.md#testing)).
32. A meal is sized again at each decision that keeps it (`AMealGrowsWhenHungerTurnsCriticalOnTheWay`).
33. A gathering that ends early delivers what it has (`ARouteFailureMidGatheringDeliversWhatHeHas`,
    `ARootRemovedBetweenCutsEndsTheGathering`, `ATreeGoneOnTheWayToATopUpCutSendsTheArmfulIn`).
34. A cut gives its yield once, whole, and drops what cannot be carried
    (`WhatACutCannotCarryDropsAsAPile`, `HandsFullStillDropsWholeYieldAndRemovesTree`,
    `TwoFinishersOnOneBushShareASingleYield`, `RegrowingBushPaysNoYield`).
35. A deposit takes what fits and the rest stays with the carrier
    (`DepositFromHandsBouncesStorageFullRemainderToBothHands`,
    `DepositIntoAFullStorageKeepsTheGoodsAndDoesNotFinish`,
    `DepositToBuildSiteRecordsOnManifestAndBouncesOverDelivery`).
36. A pile is found by the pile index and drained in order (`ArmfulDrainsPileAcrossTwoPickupsThenRemoves`,
    `TwoPilesInOneCellDrainOneAfterTheOther`, `TwoPilesInOneCellResolveToTheLowestEntity`,
    `PileIndexMissTakesNothingFromANeighbour`).
37. A craft keeps its completion rules ([Craft completion](#craft-completion))
    (`CraftWhoseRecipeListsAnItemTwiceNeedsTheTotal`, `CraftWhoseInputsLeftTheStationMakesNothing`,
    `CraftFurnitureOutputLeavesHandsEmptyAndFiresSpawnPackaged`, `CraftToolOutputStillRoutesToColonistInventory`).
38. A placement fetches only what it lacks and keeps a held tool
    (`PlacePackagedWithoutPickupKeepsHeldTool`, `PickupPackagedOfABoxAlreadyCarriedTakesNothing`,
    `ReselectingAPlacementWhileCarryingGoesStraightToTheSpot`,
    `CarryingOnePackagedItemDoesNotMakeAnotherOneDelivering`).
39. Construction completes once and pays its refund ([Construction work](#construction-work))
    (`CompletionCallbackFiresOnceWithTwoConcurrentBuilders`, `DemolishOrderInCompletingTickPreventsCompletion`,
    `FreshDeconstructOfHalfBuiltSiteStartsOnce`, `DeconstructSalvagesRefundPercentAtCompletion`).
40. A box of food is a source item by item, and stops being one when it is packaged or has no
    appearance (`ABoxWithTwoFoodsHandsOutTheLowerItemId`, `ABoxPackagedOnTheWaySendsHimToOtherFood`,
    `ABoxWithNoAppearanceIsNoFoodSource`).
41. The task panel shows work for a goal only while a colonist is offered some
    (`AStorageGoalHasWorkOnlyWhileAColonistIsOfferedSome`).

---

## Low-mood behaviors

A low-mood behavior is a behavior whose `<lowMood>` entry joins the roll of a mood episode
([Mood](../design/game-systems/colonists/mood.md#data)). It keeps its flat facts and weights in XML
(ramp, base, `<requires>` and `<weight>` rows, relief). Its target is a query and its effect is a
chain, in the same behavior:

```xml
<!-- assets/config/behaviors/human/soil_bed.xml: human/soil_bed -->
<behavior id="soil_bed" label="Soiling a bed">
  <lowMood rampStart="25" rampFull="15" base="4">
    <target query="bed_to_soil"/>
    <weight when="dislikes(owner(target))" multiply="4"/>
    <weight when="not observed" multiply="2"/>
    <effect chain="soil_bed"/>
    <ends after="task"/>
    <relief amount="medium"/>
  </lowMood>

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
    <do action="urinate"/>
    <think thought="soiled_a_bed" to="self"/>
  </chain>
</behavior>
```

A low-mood behavior has one target, picked by its query's weighted draw before the roll; the behavior
takes part in the roll once, with its weights evaluated against that target. `<requires>` rows are
rows of the roll, each one test like a `<weight>` row: one that fails sets the behavior's weight to 0.
A low-mood behavior's own `<ends>` bound the whole run, `after="task"` being the end of its chain; a
run stops at the first of them.

The `low_mood` rung sits directly below critical needs and above the player's orders, because the
player cannot draft, direct, or order a colonist during an episode. A critical need still interrupts a
low-mood behavior. For that, a low-mood behavior's do steps use interruptable actions, or short ones
that complete before the gate is asked again, since a non-interruptable action blocks every switch.
A colonist in an episode bumps another colonist's claim on its target when a lower rung holds it ([Task claims, Bumping](./task-claims.md#bumping-planned)).

A low-mood behavior may have a chain whose only step gives a thought, the log-only rule in Mood: the
episode is announced and logged, and nothing in the world changes. Leaving the colony is a chain ending in `<leave_colony/>`.

### The target need not be the source

Who a colonist takes a mood out on and who put them in it are separate. One person angers them and
another gets the sharp word. The target query weighs toward the source of the mood without requiring
it:

```xml
<query id="who_to_snipe_at" pick="weighted">
  <from kind="colonist"/>
  <exclude fact="distance" above="5"/>
  <weigh fact="distance" by="-0.2"/>
  <weigh fact="opinion" by="-5"/>
  <weigh fact="mood_source" by="-6"/>
</query>
```

The colonist who is the source of the mood is the likeliest target when they are near, and not the
certain one: the pick is a weighted draw. When they are out of reach, or are someone the colonist likes
too well to turn on, the weights pick whoever else is nearby and least liked.

The fact is the source of a feeling, not blame for it, so it reads both ways. A colonist lifted by
seeing someone they love is inclined to be kind to someone, and generally to that person: a kind act's
target query weighs `mood_source` upward where a snipe's weighs it down.

### Entries plain conditions could not express

| Table entry | Expressed as |
|---|---|
| x0.8 per consecutive hold | a weight row that applies once per count of a number term: `<weight per="holds_in_a_row" multiply="0.8"/>` |
| a food type with 1 or 2 left | the low-mood behavior's target query: `<from capability="edible"/>` with `<exclude fact="colony_stock" above="2"/>` |
| no liked colonists, liked colonist near | a query `<from kind="colonist"/>` excluding `opinion` below the like line (and `distance` above the radius for "near"), read by the condition term `count_of(query)`: `<weight when="count_of(liked_colonist_near) > 0" multiply="1.5"/>` |
| built it themselves | the fact `built_by_self`, read as `built_by_self(target)` in the weight row |
| flammable structure near | `count_of` over a query `<from kind="structure"/>` excluding `flammable` below 1 and `distance` above 5 |
| confined indoors | the self term `hours_indoors`, game hours since the colonist was last outside: `<weight when="hours_indoors > 6" multiply="2"/>` |
| just failed a task, craft interrupted | recent events about self, the declared `recent_event(self, event, within)`, with the events `task_failed` and `craft_interrupted` |
| has a sleep spot | `count_of(sleep_spot) > 0`, a query over `<from kind="bed"/>` and `<from place="here"/>` |
| no bed | `count_of(own_bed) < 1`, a query over `<from kind="bed"/>` excluding `owned_by_self` below 1 |

`per` on a weight row names a number term and raises the multiplier to its value. `count_of(query)`
counts the candidates that survive a query's exclusions, scoring none of them.

---

## Changes to behavior

What some human behaviors become, and the human behaviors with no hand-written predecessor. Each
section is the end state of that behavior; the entries under [The human behaviors](#the-human-behaviors)
are what it is before it.

### Relief decided before setting off

Which relief a colonist takes (bladder, bowels, or both) is decided when it sets off, not on arrival.
The hand-written code decides on arrival whether a relief covers both needs, from the levels it finds
then, and always relieves the need it set out for. The data entries decide it when the option is
selected: `relieve_both` is chosen while both needs are below seek, and a colonist on its way to
relieve one need switches to `relieve_both` at a later decision if the other crosses seek on the way,
since that option then ranks first. A need that crosses seek in the last moments before arrival waits
for the next relief.

### Enjoying nature

Recreation is acted on (WOR-1054, enjoy nature). A bored colonist walks to a pleasant spot near it and
restores a little Recreation over a long time, keeping what it gained when something pulls it away:

```xml
<!-- human/enjoying_nature.xml: human/enjoying_nature -->
<behavior id="enjoying_nature" label="Enjoying nature">
  <need id="recreation" label="Recreation" seek="30" urgent="20" critical="10">
    <drains perGameMinute="0.01"/>
    <satisfiedBy chain="enjoy_nature"/>
  </need>
  <chain id="enjoy_nature">
    <go query="pleasant_spot"/>
    <do action="enjoy_nature"/>
  </chain>
  <query id="pleasant_spot" pick="weighted">
    <from place="around" radius="30" count="8"/>
    <exclude fact="distance" above="30"/>
    <weigh fact="distance" by="-0.1"/>
  </query>
  <action id="enjoy_nature" hands="any" interruptable="true" pays="along">
    <restore need="recreation" amount="2" seconds="60" upTo="90"/>
  </action>
</behavior>
```

Recreation drains at a constant rate and only `enjoy_nature` restores it.

### An urgent-need rung

Below `urgent`, a need's option is on the `urgent_need` rung, which ranks above work, and a creature
decides at once when a need crosses into urgent. Every need with a `<satisfiedBy>` acts at all three
levels, the ones that cannot kill included, so a colonist leaves a long job to wash or to rest before a
low-mood behavior over it starts ([Needs](../design/game-systems/colonists/needs.md#what-a-need-costs-mood)).
Tuning a need's `urgent` level against the mood its cost takes off is how meeting the need is kept
ahead of the behavior.

### Sleep along a curve

The action `human/sleep` pays along with `curve="2"`, so a woken sleeper keeps what it slept
([Interruptable crafts](./interruptable-crafts.md#sleep-pays-out-along-a-curve)), and the factors of
that section multiply into `sleep_quality`.

### Interruptable crafts

The craft's progress is saved on the job, and `craft` is an open-ended `<work>` action on the
station's current job, interruptable and paying along, with the crafter's hold bringing it back on the
`own_unfinished_work` rung ([Interruptable crafts](./interruptable-crafts.md#the-crafters-hold)); the
`<craft>` effect's completion rules run when the job's work reaches its bound. `build` and
`deconstruct` are interruptable. A held part-done unit puts its craft on `own_unfinished_work` by an
engine rule like the other promotions.

### Eating to full, at a place to eat

The action `human/eat` is open-ended, consuming held food until hunger reaches 95
(`<consume capability="edible" count="1" seconds="2" upTo="95" need="hunger"/>`), and the meal chain
walks to a place to eat first:

```xml
<chain id="eat_a_meal">
  <have capability="edible" fetch="fetch_food"/>
  <go query="place_to_eat"/>
  <do action="eat"/>
</chain>
<query id="place_to_eat">
  <from kind="dining_table"/>
  <from kind="chair"/>
  <from place="here"/>
  <weigh fact="distance" by="-0.2"/>
  <weigh fact="comfort" by="5"/>
</query>
```

Hunger's `<satisfiedBy>` rows are the one `eat_a_meal`. The kinds `dining_table` and `chair` are
`<Kind>` declarations the furniture adds; with none known the query's only candidate is where the
colonist stands, and it eats there.

### Food preference

The query `human/food_to_eat` weighs what the colonist likes as well as distance:

```xml
<query id="food_to_eat">
  <from yields="edible"/>
  <from pile="edible"/>
  <from box="edible"/>
  <prefer fact="held"/>
  <weigh fact="against_diet" by="-20"/>
  <weigh fact="distance" by="-0.1"/>
  <weigh fact="liking" by="8"/>
  <weigh fact="eaten_recently" by="-3"/>
  <weigh fact="quality" by="4"/>
  <weigh fact="freshness" by="3"/>
  <weigh fact="maker_opinion" by="6"/>
  <weigh fact="fits_meal_time" by="2"/>
  <scale trait="gourmand" fact="quality" by="2"/>
</query>
```

A colonist knows berries 5 m away and a stew their best friend made, their favourite dish, 100 m away.

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

Diet weighs against food and never excludes it. Meat costs a vegetarian 20 points, so they walk a long
way for food without it, but a starving vegetarian who knows only meat eats the meat, unhappily.

### Relief spots sampled

The query `human/place_to_relieve` draws 16 points within 20 m by `place="around"` in place of the 3 m
grid, and its waste and food facts read how close the nearest one is (0 to 1) in place of the
proximity sum.

---

## Related documents

- [Mood](../design/game-systems/colonists/mood.md): the low-mood behaviors, their data, and the conditions
- [Thoughts](../design/game-systems/colonists/thoughts.md): what a think step gives, and the action and need triggers
- [Needs](../design/game-systems/colonists/needs.md): what each need is to the player, and the mood cost rule
- [AI behavior](../design/game-systems/colonists/ai-behavior.md): when a colonist decides, and what it can get to
- [Memory](../design/game-systems/colonists/memory.md): where candidates come from
- [Task chains](../design/game-systems/colonists/task-chains.md): the hands rules
- [Work priorities](../design/game-systems/colonists/work-priorities.md): categories and the player's work settings
- [Player control](../design/features/player-control.md): orders and direct control
- [Colonist task arbitration](./colonist-task-arbitration.md): the (rung, score) key and the switch gate
- [Task claims](./task-claims.md): claims, amounts, interruption, and the suspended record, the authority for every service under Work
- [Interruptable crafts](./interruptable-crafts.md): the urgent level, work saved on the thing worked, the sleep curve
- [Config loading](./config-loading.md)
- [Behavior data migration](./behavior-data-migration.md): the order the hand-written code moves in

---

## Addendum: decisions

A supplement, not part of the spec. Kevin, 2026-11-17:

- 2026-11-17: XML always, with optional Lua: "its not XML OR LUA, its xml always, with optional lua".
- 2026-11-17: No logic in XML: "I don't want logic in the XML. linear non branching chains fine, but no
  logic." One `when=` test on a row is allowed; a step performs once, repetition is a bound on an
  action, work rungs are one base rung with engine promotions, an action's effects never depend on a
  condition, and no `when` sits on a whole entry.
- 2026-11-17: A query is weighted facts, a weighting system and not a logic tree, and "a very generic
  item query", not specific to food.
- 2026-11-17: No Lua in the first build; the `script` attribute is reserved.
- 2026-11-17: How to migrate: "we've done a lot of bug fixing in task chains. understand the complexity
  there. we just finally got that stable". The rule for moving that code is one path at a time with its
  tests as the contract, so every shipped entry reproduces the hand-written behavior and values, and
  each intended change lands afterwards as its own change with his sign-off.
- 2026-11-17: The hand-written behavior code is retired: "no more C++ that system is going to be
  retired. we need to plan out the migration to the system we just designed." So the AI and action
  parts of interruptable crafts are built on this engine, not on the old code.
- 2026-11-17: The player cannot step in during an episode, so direct control and orders are unavailable
  to a colonist in one.
- 2026-11-17: One target per low-mood behavior, by a weighted pick: "it should be weighted, not a
  guarantee".
- 2026-11-17: The target of a low-mood behavior and the source of the mood need not be the same
  colonist: "one person pisses you off, but you take it out on another." The target is weighted toward
  the source.
- 2026-11-17: The source of a feeling is not blame, and works for good moods too: "you saw someone you
  love, so you're going to be nice to someone, and it generally should be that person."
- 2026-11-17: Needs are data: "the needs become data. different entities will have different needs.
  cows need to be milked for instance." Needs can interrupt work in extreme cases.
- 2026-11-17: A very basic "enjoy nature" restores Recreation: "a very basic 'enjoy nature' for the
  really baseline. takes a long amount of time for little gain".
- 2026-11-17: Creatures are understood through humans: "colonists and other humans need to be
  understood here. colonists are kinda their own thing, but still should be structured in a modable way
  if possible." and "all the needs in the spec Wash, Drink, Sleep, etc. are all human behaviors (and
  therefore colonist behaviors) and other creatures will have needs by the same name, but operate very
  differently."
- 2026-11-17: Shared behaviors, composed by creatures: "well, there can be shared needs. we don't want a
  bunch of copy/paste for animals who eat grass. we'll need a structure that has a general behavior
  library and some sort of mixin or inheritance I'm afraid. isn't this what an entity behavior system is
  exactly for?"
- 2026-11-17: How the norm does it. RimWorld gives each race a ThinkTreeDef built from reusable subtrees
  and shares def values through XML `ParentName` and `Abstract` inheritance; Unreal shares behavior tree
  subtrees across pawns; Godot composes a character from child nodes. This design takes the composition
  of reusable parts that all three share, without RimWorld's parent chain.
- 2026-11-17: Ids are folder paths: a behavior's id is its path under the library, `human/eating`,
  `herbivore/eating`. He suggested a namespace ("perhaps they can have a namespace? human:: eating
  herbivore::eating"), then settled it as the path: "human/eating and human::eating are semantically
  identical". This follows Minecraft's resource locations and Godot's `res://` paths.

- 2026-11-17: `have` is his: he described eating as a chain of "1) get food if you dont have it 2) find
  a place that you want to eat and go there ... 3) do the eating". `have` is the one step with a
  precondition, a skip and not a branch.

The following he settled with "Yes to all" when they were put to him:

- 2026-11-17: A creature lists its behaviors flat; there are no parent creatures and no inheritance.
- 2026-11-17: A colonist is a human with colony membership, a role a human gains and loses.
- 2026-11-17: Non-colony humans have mood, thoughts, and traits; only what the player sees depends on
  membership.
- 2026-11-17: The colonist's asset folder is renamed to `assets/world/creatures/Human/`, its
  definitions to `Human`, `Human_up`, and the rest, and its mood, traits, attributes, and memory config
  move into it (`mood.xml`, `memory.xml`, `traits.xml`, `attributes.xml`); `needs/needs.xml` is
  replaced by the need entries, and `assets/config/thoughts/` and `assets/config/conditions/` stay
  shared.
- 2026-11-17: Diet weighs against food and does not exclude it: a starving vegetarian eats meat,
  unhappily.
- 2026-11-17: Food already carried is eaten first: `eat_from_pack` while food is carried, `fetch_food`
  only while none is.
- 2026-11-17: A mod only adds its own behaviors; it cannot replace a shipped one, so a duplicate full
  id within an entry type is a load error everywhere.
- 2026-11-17: The "move here" order is dropped; moving a colonist is direct control.
- 2026-11-17: The rung for assigned colony work is `committed_work`; "order" means only a player's
  order. The mood rung is `low_mood`.
- 2026-11-17: Ids are unique per entry type, the referring attribute naming the type, so a chain and an
  action may share an id.
- 2026-11-17: Relief (bladder, bowels, or both) is decided when the colonist sets off, not on arrival.
- 2026-11-17: Temperature's mood cost is 60, up from 40.
- 2026-11-20: A mood episode bumps another colonist's claim on its target held on a lower rung (the `low_mood` rung is a bumping rung).
- 2026-11-20: A player's order bumps another colonist's claim on its target held on a lower rung (the `player_order` rung is a bumping rung).

Corrections from the first build of the loaders (WOR-1067, Entry loaders for actions, queries, chains, and
behaviors):

- 2026-11-21: The relieving examples spawn `Misc_BioPile`, the bio pile's definition name.
- 2026-11-21: A need is not among the entry types whose ids are unique; it is keyed by its behavior and its
  id and settled per creature, as the human's and the deer's `hunger` require.
- 2026-11-21: A creature naming its own behavior file in a `<behavior id>` row places it in the order and
  is not the included-twice error, as the cow example requires.

Rules from the first build of the loaders:

- 2026-11-21: Every declared id, and every file and folder name an id is made from, is letters, digits, and
  underscores, case as written.
- 2026-11-21: A subfolder under `actions/`, `work/`, `orders/`, or `facts/` is a load error, since their
  entries take top-level ids.

Rules from the first build of queries (WOR-1070, Facts and queries):

- 2026-11-21: A query used only to count (`count_of`) is exempt from the rule that a query bounds distance: a
  count ranks nothing and walks nothing nearest first. The loader tells a counted query by its references: a
  query no `go` step, work entry `<source>`, or low-mood `<target>` names can only be counted, and is not held
  to the rule; any query one of them names is. A weighted query still excludes distance above a bound.
- 2026-11-21: `on_shore` counts a place exactly the tuned distance from water (1.5 m) as on the shore, as the
  toilet finder does: a relief spot is kept only when its distance to water is strictly greater.

Choices made in writing this document, not put to the owner:

- Creature loading: the asset registry reads only the primary file's asset definitions; a creature
  loader on the loading scene re-reads the base definition's `<creature>` block and the folder's other
  top-level files by root, in file name order; the base definition is the one named after the folder;
  an unknown root, a behavior included twice, and two rows setting one value are load errors; files are
  never named after the folder and the folder has no subfolders.
- The creature's order: the primary block's includes, then `<creature>` files' includes by file name,
  then its own behavior files by file name, then mods' additions in load order.
- Mods: a mod never writes into a shipped folder; it adds its own folder with its behaviors and a
  `<creature for="Human">` file naming the creature it adds to, and `<Traits for="Human">` for traits.
  Traits and attributes keep their replace-by-name rule.
- Ids as case-sensitive paths, with the shipped library in snake_case and a creature's folder part its
  definition name as written (`Cow/being_milked`).
- Short ids inside a folder: a short id names an entry of the same folder, else a top-level entry of
  the shared folders; from outside, the full id; a folder entry hiding a top-level id of its type loads
  with a warning. The file declares only the last part of its id.
- One behavior a file, with chains, queries, and actions in their behavior's folder; need ids plain
  names, shared by every creature; shared actions, work, and orders as top-level ids.
- Mood cost as `<moodCost>` rows the creature gives, not a value of the shared need.
- A low-mood behavior as a behavior with a `<lowMood>` entry, in the same library.
- The shared folders (`behaviors`, `actions`, `work`, `orders`, `facts`, `arbitration`), with
  `priority-tuning.xml` moving out of `work/` into `arbitration/`, and a file with an unowned root being
  a load error.
- Load order: the library, then creatures, with need ids collected before the condition vocabulary
  and cross-references resolved after all are read.
- Thought sources naming action entry ids and need ids.
- `GoalType` as the name of the goal list, with `clear` its own type.
- The ladder in `<ladder>` with `<rung>` entries; `survival` reserved.
- `<prefer fact>` as the row for food's free-before-held rule, sleep's bed-before-ground rule, and
  relief's latrine-before-ground rule; `fallback="true"` on a `<from>` row for the stand-where-you-are
  fallback; the fact `is_place`.
- `<from place="grid">` and `place="around"` with `minRadius`, to reproduce the toilet finder's grid and
  wander's ring; `waste_nearby` and `food_nearby` as the toilet finder's proximity sums.
- Idle rows ranking in the creature's order ahead of score.
- The term `carried_food`.
- Relief as three actions (`urinate` 2 s, `defecate` 4 s, `relieve_both` 5 s) and three chains; a chain
  named by several needs being one option, and the option serving more needs ranking first on its rung;
  the term `seeking(need)`.
- The hands rules keyed on the first `do` the chain will perform, counting an unmet `have`'s fetch.
- `enjoy_nature` as an open-ended action restoring 2 Recreation a minute of action up to 90.
- Fact tuning values in `assets/config/facts/`; the meal hours in the condition vocabulary.
- The craft completion rules as a `<craft>` effect, with craft fixed-duration before interruptable
  crafts.
- `asleep` as an action property.
- The `<leave_colony/>` step.
- `<drains>` and `<setBy provider>` as the two ways a level moves, and rate rows on a drain.
- `pays="end"` or `pays="along"` with a `curve` exponent.
- Action groups, matched to the target by the member's `on`.
- Claims named on `do` steps, sized at selection from the chain's planned amount.
- The step end events `held_gone`, `target_gone`, and `target_left`; an unanswered fact reading 0.
- Nothing of a chain surviving `Task::clear()`; resuming work being the suspended record's adoption.
- The retired, kept, and reshaped lines as drawn under Scope.
- `work-types.xml` deleted, with its categories kept as `<category>` entries in the work folder.
- Facts doubling as condition terms on a target; `per` on weight rows and `count_of(query)`.
- The weighted pick's formula: score less the lowest, plus 1, over the best 16, with a distance bound
  required.
- The values under Changes to behavior: the food weights, `against_diet` weighed by -20, eating to 95,
  the sleep curve of 2, and the relief sample of 16 points within 20 m.

## Historical addendum

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
  hand-written task code. The same day the owner retired the hand-written code, and the document was
  rewritten to describe the whole end state.
