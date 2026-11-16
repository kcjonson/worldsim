# Task Registry System

**Status:** Design
**Created:** 2025-01-02
**MVP Status:** Phase 2+ (after basic needs/work loop)

---

## Overview

The Task Registry is a **global catalog of all available work** in the colony. It represents the **union of what all colonists know about** — not everything that exists in the infinite world.

**Core Constraint:** An undiscovered entity (berry bush, loose item, etc.) does not generate a task. Tasks only exist for entities that **at least one colonist has discovered**.

**Key Insight:** Task *existence* and task *priority* are separate concerns:
- **Existence:** "A colonist knows about a harvestable berry bush at (10, 15)" — sourced from Memory
- **Priority:** "Bob should harvest it with priority 3,450" — computed per-colonist, includes distance

---

## Architecture: Memory-Sourced Design

### The Fundamental Constraint

```
INFINITE WORLD
┌─────────────────────────────────────────────────────────────┐
│  Millions of entities exist...                              │
│  Berry bushes, trees, rocks, loose items everywhere         │
│  We CANNOT iterate all of them                              │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼ (discovered by exploration)
┌─────────────────────────────────────────────────────────────┐
│  COLONIST MEMORIES (finite, bounded)                        │
│  Each colonist knows ~10,000 entities max (LRU eviction)    │
│  Memory is the SOURCE OF TRUTH for what's "known"           │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼ (union of all colonists)
┌─────────────────────────────────────────────────────────────┐
│  GLOBAL TASK REGISTRY                                        │
│  Tasks for entities that ANY colonist knows about           │
│  Size bounded by: num_colonists × memory_limit              │
│  In practice: ~50,000 known entities max                    │
└─────────────────────────────────────────────────────────────┘
```

### Why This Matters

- **Berry bush 1000 tiles away**: No task generated (no colonist knows it exists)
- **Berry bush colonist walked past**: Task generated (in at least one Memory)
- **Berry bush all colonists forgot** (LRU eviction): Task removed from registry

---

## Three-Layer Architecture

```
┌─────────────────────────────────────────────────────────────┐
│           LAYER 1: Global Task Registry                      │
│  Tasks for entities known to ANY colonist                   │
│  - Sourced from: Union of all Memory components             │
│  - Updated when: goal systems poll colony memory (throttled)│
│  - Bounded by: Total colonist memory capacity               │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼ (filtered by THIS colonist's memory)
┌─────────────────────────────────────────────────────────────┐
│          LAYER 2: Per-Colonist Task View                     │
│  Tasks THIS colonist can do                                 │
│  - Filtered by: This colonist's Memory (must know target)   │
│  - Filtered by: Work preferences (enabled categories)       │
│  - Scored with: tier, distance, skill bonus, task age       │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼ (aggregated for UI)
┌─────────────────────────────────────────────────────────────┐
│          LAYER 3: UI Task List                               │
│  What tasks exist colony-wide (for player visibility)       │
│  - Shows all tasks known to any colonist                    │
│  - Per-task: who is working it, and its status             │
└─────────────────────────────────────────────────────────────┘
```

---

## Layer 1: Global Task Registry

### Data structure

The registry is `GoalTaskRegistry` (`libs/sim/ecs/GoalTaskRegistry.h`), one `GoalTask` per goal:
type, destination, accepted items, owning goal system, parent goal (a Harvest or Haul feeding a
Craft), and status (`Available`, `Blocked`, `NoSource`). How much more a goal wants is its root's
remaining need, read from the world (`GoalTaskRegistry::remainingNeed`), never a counter on the goal.
Goals hold no reservations and no `knownBy` set. Which colonist is working a goal, and how much is on its way, comes from the claims on
colonists' tasks ([Task Claims](../../../technical/task-claims.md)).

### Keeping it current

Goal systems (`StorageGoalSystem`, `CraftingGoalSystem`, `ConstructionSystem`, `BuildGoalSystem`)
create, update, and retire goals, and re-resolve sources against colony knowledge (the union of
colonist memories, read through each Memory's capability index) on a throttle. Memory does not
notify the registry when a colonist discovers or forgets something; polling is the model. A goal
whose only known source is forgotten by everyone goes `NoSource` on the next poll and re-resolves
when someone discovers a new one.

---

## Layer 2: Per-Colonist Task View

### Key Difference from Layer 1

Layer 1 holds goals the colony as a whole has.
Layer 2 is what **this specific colonist** can act on.

There is no `knownBy` set on goals and no filter by it. Each colonist's evaluators walk every goal of
each type in the registry (a few hundred at most) and, for each, visit only the entries of its own
Memory for the defs that give the goal's item (Memory's per-def lists). A goal for which it remembers
no source yields no option. What other colonists' tasks have claimed is taken out of what is left
(see [Task Claims](../../../technical/task-claims.md)), so a source others take whole shows as at
most one Reserved row instead of an option. A cut is offered only when the colonist holds the tool it
needs and one unit of the yield fits; skills add to the score but filter nothing. Work-type
preferences ([Work Priorities](./work-priorities.md)) do not filter options today.

---

## Layer 3: UI Task List

### What the Player Sees

The "Tasks (N)" panel lists Layer 1 goals that have something to do
(`apps/world-sim/scenes/game/ui/adapters/GlobalTaskAdapter.cpp`): Harvest, Haul, Build, Craft, and
PlacePackaged goals, each with a description ("Cut for Wood", "Haul Wood", "Haul Materials", "Build
structure", "Craft", "Place Item", with "(for crafting)" on a craft's children), the destination's
position, and its distance from the camera center. A storage goal with nothing to do (a box with no
room for anything it accepts, a stocking cut whose minimum is met) is left out while the goal stays.
The colonist view's task tab shows the same rows measured from the colonist; it is not filtered to
what that colonist knows.

### Status Indicators

- **Working <name>**: a colonist's current task serves the row. One name, even when several colonists
  serve it. A cut or a haul names the goal it was picked through; a carry-in names its root; a Build,
  Craft, or placement row names whoever works its entity.
- **Building <name> (N%)**, **Ready to build (N%)**, **Needs materials**: a Build umbrella, from its
  blueprint's phase and progress; "Needs materials" names whoever is delivering, if anyone.
- **Blocked**: a Craft waiting for its materials, with "landed/required materials".
- **Waiting (no source found)**: the goal is `NoSource`.
- **Available (far)**: nobody works it and it is more than 50 m away.
- **Unassigned**: nobody works it.

Planned, not built: a "Known by" line, per-item "N available (M in progress)" counts, a per-colonist
filter, and distinct blocked reasons such as "all known sources claimed" ([Task
Claims](../../../technical/task-claims.md#player-facing-output)).

---

## Task Generation: Goal-Driven Model

**Critical:** Tasks are NOT generated from discovery. Tasks are generated from GOALS.

- **Discovery** → Updates **Memory** (what colonists know about)
- **Goals** → Generate **Tasks** (what needs to be done)

### Goal Sources

| Goal Source | Task Type | Validity Condition |
|-------------|-----------|-------------------|
| Storage with capacity | Haul | Storage accepts item type + colonist knows loose item location |
| Crafting recipe queued | Harvest / Haul | Recipe needs material + colonist knows source |
| Need below threshold | FulfillNeed | Colonist knows resource that fulfills need |
| Build order placed | Haul/PlacePackaged | Structure needs materials + colonist knows source |

### Discovery vs Goals

```
DISCOVERY (VisionSystem)              GOALS (Various Systems)
─────────────────────────             ─────────────────────────
Colonist sees berry bush              Storage has empty capacity
         │                                      │
         ▼                                      ▼
Memory updated                        Task created: "Fill Storage"
(colonist knows bush location)        (references Memory for fulfillment)
         │                                      │
         ▼                                      ▼
NO TASK CREATED                       Colonist checks Memory:
(just knowledge)                      "Do I know any items Storage accepts?"
```

### Why Goal-Driven?

A Haul task requires BOTH:
1. A **destination** (storage that wants items) — the GOAL
2. A **source** (loose item the colonist knows about) — from MEMORY

Without a destination, a Haul task is meaningless.

---

## Task Granularity

**Problem:** Storage that holds 10,000 rocks should NOT generate 10,000 tasks.

**Solution:** Tasks exist at the GOAL level, not the ITEM level.

### Two-Level Model: Tasks vs Claims

| Level | Scope | Count | Purpose |
|-------|-------|-------|---------|
| **Task** | Goal | O(goals) | "Storage X wants rocks" |
| **Claim** | Item, on a colonist's task | O(colonists working) | "Bob's task will take 5 rocks from the pile at (5,3)" |

### How 5 Colonists Work in Parallel

```
Task: "Fill Storage with rocks" (1 task, goal-level)
                │
     ┌──────────┼──────────┬──────────┐
     ▼          ▼          ▼          ▼
   Bob       Alice       Carol      Dave
     │          │          │          │
     ▼          ▼          ▼          ▼
 Checks      Checks      Checks     Checks
 Memory      Memory      Memory     Memory
 (50 rocks)  (49 avail)  (48 avail) (47 avail)
     │          │          │          │
     ▼          ▼          ▼          ▼
 Claims      Claims      Claims     Claims
 rock@(5,3)  rock@(8,7)  rock@(12,4) rock@(3,9)
```

**Key insight:**
- The **TASK** is shared (1 per storage, goal-level)
- The **CLAIM** is per-item and lives on the colonist's own task (prevents conflicts)
- Memory tracks what's known; claims track what's committed

### Data Model

The goal holds no claims. A colonist's `Task` carries its claim list (source quantity, inbound at
the destination, in-flight toward the goal), and a claim index derived from all tasks answers
"how much of this goal is in flight" and "who holds this pile". Clearing or destroying a task
removes its claims with no release step. Data shapes, chain claims, and availability formulas are
in [Task Claims](../../../technical/task-claims.md).

### Task Resolution Flow

```
1. Storage X has capacity → GoalTask exists: "Fill Storage"
                │
                ▼
2. Bob considers task → checks his Memory for matching items
                │
                ▼
3. Finds 50 rocks known, 2 already claimed by others' tasks → 48 available
                │
                ▼
4. Bob picks closest available rock → his task records its claims
                │
                ▼
5. Bob hauls rock, deposits → task ends, its claims go with it
                │
                ▼
6. Task still exists if storage still has capacity
```

### UI Display

The task list shows GOALS, one row each, with the status above. Per-item availability counts ("48
available (2 in progress)") are planned, not built.

### Task Lifecycle

| Event | Task State |
|-------|------------|
| Storage given rules | Umbrella goal created |
| Storage filled completely | Umbrella goal stays (goods on their way keep counting toward it); the panel hides it until it wants something again |
| Storage rules removed, or storage gone | Umbrella goal and its children removed |
| Colonist accepts work | Its task records claims; other colonists see less available |
| Colonist picks up | Source claim drops, inbound and in-flight stay |
| Colonist deposits item | Task ends with its claims, storage stock rises |
| Colonist abandons work or is taken over | Task cleared, claims gone, the work is available again |
| All known sources claimed | No option for other colonists; their trace shows a Reserved row for the nearest one |
| New item discovered | The discovering colonist's next decision offers it |

---

## Claims

Two colonists must not walk to the same tree or both fetch the last unit a recipe needs. The
rules, in brief (full design in [Task Claims](../../../technical/task-claims.md)):

1. A colonist's task records claims for its whole planned chain when it accepts the work, and
   rewrites them as steps complete.
2. Only a colonist that knows a target can claim it; claims on anything a colonist knows about
   are visible to every colonist.
3. Claims end with the task. There are no release calls and no timeout.
4. Nobody takes over another colonist's claim (bumping is planned, not built). The one exception
   is food: a hungry colonist may take food another colonist's work, idle gathering, or less urgent
   hunger holds, and prefers food nobody holds.
5. Claims are checked against the world when the task runs. Finding the target gone or taken is
   normal re-planning.

A colonist that forgets its target while walking (LRU eviction) keeps its claim, because the
claim is on its task, and re-sees the target on arrival.

---

## Performance Considerations

### Bounded by Goals (Not Entities)

With goal-driven task generation, task count is bounded by GOALS, not discovered entities:

| Factor | Bound |
|--------|-------|
| Storage containers | ~50 (typical colony) |
| Active crafting queues | ~10-20 |
| Colonists with needs | ~50 |
| Build orders | ~10-50 |
| **Max tasks in registry** | **~200** |

Compare to old discovery-driven model:
- Old: ~100,000 tasks (one per discovered entity)
- New: ~200 tasks (one per goal)

### What Scales with Exploration

| System | Scales With | Bound |
|--------|-------------|-------|
| Memory | Discovered entities | ~10,000 per colonist (LRU) |
| Tasks | Active goals | ~200 (storage + crafting + needs) |
| Claims | Colonists working | a handful per colonist |

### Practical Numbers

Most colonies will have:
- 5-20 colonists
- 10-50 storage containers
- 5-20 active crafting recipes
- **~50-100 active tasks** (goal-level)
- **~10-100 active claims** (item-level, on colonists' tasks)

### Spatial Indexing

The registry has no spatial query and no `knownBy` filter. Goals are few, so each colonist walks
them all; what scales with exploration is Memory, which each goal reads through its per-def lists.

---

## Integration with Existing Systems

### Memory System (Source of Truth)

Memory is the source of what is known, and nothing is pushed from it. Goal systems read colony
knowledge (every colonist's Memory, through its capability index) on their throttled poll to
resolve sources and retire stale goals; each colonist's evaluators read its own Memory when it
decides.

### AIDecisionSystem

Query registry instead of scanning world:

```cpp
void AIDecisionSystem::buildDecisionTrace(/* ... */) {
    // Get tasks this colonist knows about
    auto tasks = m_taskRegistry->getTasksFor(entity);

    for (const auto& task : tasks) {
        // Score with distance, skill, chain bonuses
        float priority = scoreTask(task, position, skills, currentTask);

        EvaluatedOption option;
        option.type = mapTaskTypeToOptionType(task.type);
        option.targetPosition = task.position;
        option.priority = priority;
        trace.options.push_back(option);
    }
}
```

---

## Modding & Extensibility

### Adding New Task Types

1. Define new capability in asset XML
2. Task automatically generated when colonists discover entities with that capability
3. No task definition files needed

### Example: New "Tame" Task

```xml
<!-- assets/world/fauna/Wolf/Wolf.xml -->
<AssetDef>
  <defName>Fauna_Wolf</defName>
  <capabilities>
    <tameable skill="AnimalHandling" duration="30.0"/>
  </capabilities>
</AssetDef>
```

When colonist discovers wolf → "Tame Wolf" task appears in registry (if colonist has AnimalHandling skill enabled).

---

## Related Documents

- [Task Generation Architecture](../../../technical/task-generation-architecture.md): goal sources, polling, UI vs AI priority
- [Task Claims](../../../technical/task-claims.md): claims on colonist tasks, the derived claim index (bumping planned)
- [Priority Config](./priority-config.md) — Priority formula and tunable weights
- [Task Chains](./task-chains.md) — Multi-step tasks
- [Work Priorities](./work-priorities.md) — Per-colonist work preferences
- [Memory System](./memory.md) — How colonists discover and track entities
- [Entity Capabilities](../world/entity-capabilities.md) — How capabilities define available work
