# Queries and chains

Epic: WOR-919 (Colonist mood: low-mood behaviors, refusal, and colony failure)

Two generic mechanisms for anything a creature does on purpose. A **query** answers "which thing, for
this creature, for this purpose": which food, which bed, where to eat, who to vent to, which stick. A
**chain** is an ordered list of steps the creature carries out: go there, do this, give that thought.

Low-mood behaviors ([Mood](../design/game-systems/colonists/mood.md)) are the first thing built on
them. They are designed generically because the same two questions are answered by hand in many
places: need fulfilment, crafting inputs, hauling sources and destinations, behavior targets, and later
player direct-control commands, animals, and needs defined as data (WOR-1056, needs as data: a need is
defined in config, and each kind of creature has its own).

---

## Data rule

Every query and every chain is an XML entry that works on its own for the simple case, as an asset
definition does. XML holds no control flow: no branching and no loops. A chain is a straight list of
steps; a query is a list of weighted facts.

One yes-or-no test that picks whether a row of data applies is allowed on a row, written as `when=` in
the [condition language](../design/game-systems/colonists/mood.md#conditions), the way thought feelings
and behavior weights already use it. A row whose `when` is false or Unknown does not apply. A `when` is
never on the entry as a whole and never chooses between steps.

An entry may name a Lua script for rich expression, the way an asset definition names a generator,
with the entry's own values passed to it. The format reserves an optional `script` attribute on
`<query>` and on `<chain>` for this: a path relative to the entry's file, whose script takes the entry's
values as a table. No loader reads scripts. An entry that gives `script` is a load error naming the
file, the entry, and the attribute as reserved, so no mod ships a script that is silently ignored, and
a later hook adds meaning to the attribute without changing any entry that works today.

### Files

Queries load from `assets/config/queries/` and chains from `assets/config/chains/`, each folder's own
files first, then its subfolders' in path order, as `assets/config/conditions/` loads
([Config loading](./config-loading.md)). The roots are `<queries>` and `<chains>`. A later file's entry
with the same `id` replaces the earlier one and is logged with both files; the same id twice in one file
is an error. They load after the condition vocabulary, since rows carry conditions, and chains after
queries, since steps name queries. Any problem in a file (an unknown element or attribute, a fact,
step, action, or query that does not exist, a bad number) loads nothing from it. Errors are prefixed
`[queries]` or `[chains]` and reach the loading scene's failure view like the colonist rules' errors.

---

## Queries

A query is three things: where candidates come from, what excludes a candidate, and a list of weighted
facts.

`score = sum over rows of (fact value x weight)`

The candidate with the highest score wins. A query is a weighting system, not a logic tree, and it is a
generic item query: food is one use of it, not its shape.

### Candidates

Candidates come only from what the creature knows: its memory, and the colony's shared knowledge where
that already applies (the storage boxes the colony knows, `ColonyStorageKnowledge`). A query never
learns of a candidate from the world. Like the need search it replaces, it may read the live state of a
candidate the creature already knows (whether a bush is regrowing, a pile's count, who holds a claim on
it), as `AIDecisionSystem` does through its regrowth callback and the claim index.

A `<from>` row names one source of candidates, and a query may have several:

| Row | Candidates |
|---|---|
| `<from capability="edible"/>` | remembered things with that capability, as asset definitions spell it |
| `<from kind="bed"/>` | what a declared kind of the condition vocabulary maps to (`<Kind>` in `assets/config/conditions`): things under its capability, or movers of its definition seen within the recent-sighting window |
| `<from item="Berry"/>` | remembered things of one asset definition |
| `<from place="here"/>` | where the creature stands, as a place with no thing in it |

Every query also passes over, without rows of its own, what the decision already passes over: a target
with a route verdict ([AI behavior](../design/game-systems/colonists/ai-behavior.md#what-a-colonist-can-get-to)),
a source that is regrowing or exhausted, and a target not joined to the creature on the nav mesh.

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
| `opinion` | item | -1 to 1 | for a colonist candidate, the creature's opinion of them | waits on relationships |
| `owner_opinion` | item | -1 to 1 | the creature's opinion of the thing's owner, 0 when unowned | waits on ownership and relationships |
| `has_owner` | item | 0 or 1 | the thing has an owner | waits on ownership |
| `owned_by_self` | item | 0 or 1 | the creature owns it | waits on ownership |
| `owner_present` | item | 0 or 1 | the owner was seen beside it within the recent-sighting window | waits on ownership |
| `built_by_self` | item | 0 or 1 | the creature built it | waits on structures recording who built them |
| `flammable` | definition | 0 or 1 | the thing burns | waits on fire |
| `colony_stock` | definition | 0 and up | how many of this definition the colony knows it holds | waits on scarcity |

Context goes into facts, not branching. Eggs in the morning is a `meal` tag on the egg's definition and
one fact, `fits_meal_time`; no query asks what hour it is.

### Rows

```xml
<queries>
  <query id="food_to_eat">
    <from capability="edible"/>
    <exclude fact="against_diet"/>
    <weigh fact="distance" by="-0.1"/>
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
   on a definition-scoped fact removes every item of that definition before any is visited.
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
   for the winner.
6. **No allocation in the steady state.** The walk, the definition cache, and the results use scratch
   buffers the query system owns and reuses across creatures, as `AIDecisionSystem` reuses its source
   scratch. Results are in a total order: score, then distance, then the memory key. Nothing depends on
   hash or view order.

### Colony work: claims and amounts stay outside

Queries used for colony work also face claims (skip what another colonist has claimed) and amounts (one
pile may not hold everything). Those stay outside the query. A query picks the best single source for
one trip, and the goal system that owns the work keeps asking, reading claims and remaining need as
[Task claims](./task-claims.md) specifies.

### Where queries are used

Queries are used for behavior targets, replacing the `<target kind within where>` form, and for the go
steps of the chains behind them. Later users, not scheduled by this document: need fulfilment (and
needs defined as data, WOR-1056), crafting inputs, hauling sources and destinations, player
direct-control commands, and animals.

---

## Chains

A chain is a straight list of steps. A step is one of an enumerated set of step kinds provided in C++.

| Step | Does |
|---|---|
| `<go query="..."/>` or `<go to="target"/>` | walks to the target a query picks, or to the chain's own target (a behavior's) |
| `<do action="..."/>` | performs an action where the creature stands, once, or again and again until one of its end conditions holds |
| `<have capability="..." fetch="..."/>` | a goal: done at once if the creature already holds a thing with the capability (or `item`), otherwise it runs the named chain, which gets one |
| `<think thought="..." to="self"/>` | gives a thought to `self`, `target`, or `witnesses` (every colonist who saw the step) |
| `<need name="..." change="..."/>` | moves one of the creature's needs by a signed amount |
| `<take/>`, `<damage amount="..."/>`, `<drop/>` | takes the target into hands or pack, damages the target, sets down what the chain's steps took |
| `<run chain="..."/>` | runs another chain as this step; chains nest |

A `do` names an action type of the sim (`ActionType`); one the sim lacks is a load error. Actions a
behavior needs and the sim lacks (talking, standing idle at a place) are added as action types when the
behavior is built.

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
    <take/>
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

The kinds `dining_table` and `chair` are `<Kind>` declarations the furniture adds. "Get food if you
don't have it" is the `have` step, with no condition in the XML.

### Ending a step

A step ends on its own end conditions, the `ends` of the behaviors in mood.md. That is how "eat until
full or the food runs out" is one step and not a loop: the `do` performs Eat, and performs it again,
until the first `<ends>` holds. An `<ends>` is one of `after` (game time, `after="3h"`), `when` (a
condition), or `on`, an event the step kind reports: `held_gone` (what the creature holds for the step
is used up), `target_gone` (the target no longer exists or is no longer where it was), or `target_left`
(a colonist target walked away). A `do` with no `<ends>` performs its action once. A `go` ends on
arrival.

### When a target is resolved

A step that needs a target runs its query when the step starts, not when the chain is chosen, because
the world may have changed in between. The first step's target is the exception: it is resolved at
decision time, because the AI ranks options partly by distance and must know the chain can be started
at all. A chain whose first step finds no target is not offered.

### Progress is engine state

The chain's progress (which step, its target, how many times a `do` has performed its action, when the
step began) is state the engine holds on the task, so the UI can show it and it can be saved. No script
holds it.

### How it sits in the existing code

A chain is not a second engine. It is driven through the existing `Task`, `Action`, and routing
machinery:

- A chain-run is a task type, `TaskType::Chain`, at its own tier. It is named in
  `assets::kTaskTypeNames`, so `current_task(chain)` reads it, and its base tier is in `<TaskTiers>`
  in `priority-tuning.xml` like every other type's.
- A go step sets the task's target the way a deposit leg does, through `ActionSystem::headTo`, which
  cancels the old leg's route ticket, invalidates the path, and leaves the AI to ask a new route.
- A do step is an ordinary `Action`, started by `ActionSystem` on arrival within `kActionReachMeters`
  and completed through `completeAction`, which reports whether its effect took.
- Selection, the switch gate, claims, and route answers are `AIDecisionSystem`'s, unchanged.

Most of the hard-won fixes in multi-step work live in that machinery (the arrival check, the
start-that-starts-nothing rule, route tickets, the switch gate, the hands rules), and a chain gets them
by running through it. A second runner would have to learn each again.

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
5. A creature decides at once on no task, on arrival, on a need newly critical, on the first route
   failure, or on a requested decision (`requestDecision`), otherwise on the 0.5 s interval, and never
   every frame while a failure persists (`AColonistWhoseEveryRouteFailsDecidesOnTheInterval`).
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
12. When a step's first action needs free hands (`needsHands` in `action-types.xml`), held goods are
    stowed or set down by the existing rules, a two-hand armful is never stranded, and a packaged item
    is set down still packaged (`ASwitchToBuildFreesTheHands`,
    `InterruptedTwoHandArmfulDropsOnceWithWholeQuantity`,
    `FurnitureWhosePlacementIsCancelledIsSetDownPackaged`).
13. Only what the creature remembers is targeted, and a regrowing or exhausted source is passed over at
    selection (`CannotFindUnknownEntities`, `RegrowingSourceIsPassedOverForAReadyOne`).
14. Every order the runner makes is total with a final tie-break, nothing depends on unordered
    iteration, and the only randomness is the seeded generators (`EqualKeyBreaksTiesOnTiebreakId`).
15. A decision scans no creature's whole memory, reuses its scratch buffers, and probes options in
    priority order, stopping at the first selectable one.
16. Every field the runner adds to `Task` says whether it survives `Task::clear()`; the chain, the step,
    its target, its count, and its start time are all reset by it, so an interrupted chain never
    resumes.

What applies only if chains ever carry colony work, and is out of scope here:

- claims on shared sources, destinations, slots, and amounts;
- supply accounting read from the world, with no counters;
- suspended records across a need, and their adoption;
- batching several pickups or cuts into one trip;
- the two-hand delivery guard;
- reachability of the deposit leg at selection.

---

## Scope and what is left alone

The runner and queries are built beside the existing hand-written work and used for low-mood
behaviors. Hauling, crafting, building, eating, and the claims system are not moved onto them as part of
this. Any later move is its own piece of work, one path at a time, starting from the requirements above
with the existing claims and chain tests kept passing.

`assets/config/work/task-chains.xml` and `work-types.xml` are loaded and validated
(`TaskChainRegistry`, `WorkTypeRegistry`, `ConfigValidator`) and read by no simulation code. The
multi-step work that runs is hand-written: a haul's pickup turns the same task into its deposit leg
(`headToDeposit`), a landed cut into its carry-in (`ActionSystem::carryHarvestIn`), and `Task::chainStep`
counts the legs. `action-types.xml` is read: `needsHands` decides what a switch does with held goods.

The chain format here replaces `task-chains.xml` when the runner is built. There is one path and no
second chain file: the file, `TaskChainRegistry`, and the work types' `<taskChain>` references to it go
when `assets/config/chains/` arrives. What happens to `work-types.xml` is not decided by this document.

---

## Low-mood behaviors on top

A behavior in [Mood](../design/game-systems/colonists/mood.md#data) keeps its flat facts and weights in
XML as before (ramp, base, `<requires>`, `<weight>` rows, relief). Its target is a query and its effect
is a chain:

```xml
<behavior id="soil_bed" rampStart="25" rampFull="15" base="4">
  <target query="bed_to_soil"/>
  <weight when="dislikes(owner(target))" multiply="4"/>
  <weight when="not observed" multiply="2"/>
  <effect chain="soil_bed"/>
  <ends after="task"/>
  <relief amount="medium"/>
</behavior>

<query id="bed_to_soil">
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

The behavior tier sits directly below critical needs and above the player's orders, because the player
cannot draft or direct a colonist during an episode (decided 2026-11-17, in Mood). A critical need still
interrupts a behavior. For that, a behavior's do steps use interruptable actions, or short ones that
complete before the gate is asked again, since a non-interruptable action blocks every switch.

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

- [Mood](../design/game-systems/colonists/mood.md): the behaviors, their Data, and the conditions
- [Thoughts](../design/game-systems/colonists/thoughts.md): what a think step gives
- [Needs](../design/game-systems/colonists/needs.md)
- [AI behavior](../design/game-systems/colonists/ai-behavior.md): when a colonist decides, and what it can get to
- [Memory](../design/game-systems/colonists/memory.md): where candidates come from
- [Task chains](../design/game-systems/colonists/task-chains.md): the hands rules and the hand-written chains
- [Colonist task arbitration](./colonist-task-arbitration.md): the (tier, score) key and the switch gate
- [Task claims](./task-claims.md): claims and amounts for colony work
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
- Built beside the existing task code: "we've done a lot of bug fixing in task chains. understand the
  complexity there. we just finally got that stable".
- The player cannot step in during an episode.

Choices made in writing this document, not put to the owner: the folders and roots
(`assets/config/queries`, `assets/config/chains`); `<scale trait fact by>` for trait scaling on the
query; `above` and `below` on exclusions; a query that neither weighs distance down nor bounds it being
a load error; facts doubling as condition terms on a target; `per` on weight rows and `count_of(query)`;
the step end events `held_gone`, `target_gone`, and `target_left`; an unanswered fact reading 0; and
nothing of a chain surviving `Task::clear()`.

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
