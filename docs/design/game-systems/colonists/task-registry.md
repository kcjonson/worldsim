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
│  - Updated when: Memory changes (discover, forget)          │
│  - Bounded by: Total colonist memory capacity               │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼ (filtered by THIS colonist's memory)
┌─────────────────────────────────────────────────────────────┐
│          LAYER 2: Per-Colonist Task View                     │
│  Tasks THIS colonist can do                                 │
│  - Filtered by: This colonist's Memory (must know target)   │
│  - Filtered by: Work preferences (enabled categories)       │
│  - Scored with: distance, skill bonus, chain bonus          │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼ (aggregated for UI)
┌─────────────────────────────────────────────────────────────┐
│          LAYER 3: UI Task List                               │
│  What tasks exist colony-wide (for player visibility)       │
│  - Shows all tasks known to any colonist                    │
│  - Per-task: which colonists can do it (know about it)      │
└─────────────────────────────────────────────────────────────┘
```

---

## Layer 1: Global Task Registry

### Data Structure

```cpp
/// A task that exists because at least one colonist knows about it
struct GlobalTask {
    // Identity
    EntityID targetEntity;        // Entity this task operates on
    TaskType type;                // Harvest, Haul, Craft, Build, etc.
    uint32_t defNameId;           // For filtering/display

    // Location (from Memory, not entity query)
    glm::vec2 position;

    // Which colonists know about this task's target
    std::unordered_set<EntityID> knownBy;

    // For multi-target tasks (e.g., haul: source → destination)
    std::optional<EntityID> secondaryTarget;
    std::optional<glm::vec2> secondaryPosition;

    // Reservation
    std::optional<EntityID> reservedBy;
    float reservedAt;

    // Task chain
    std::optional<uint64_t> chainId;
    uint8_t chainStep;

    // Metadata
    float createdAt;
};
```

### Memory-Driven Updates

The registry updates based on Memory changes, NOT entity spawning:

| Memory Event | Action |
|--------------|--------|
| Colonist discovers entity with `Harvestable` | Add/update task, add colonist to `knownBy` |
| Colonist discovers loose item (`Carryable`) | Add/update haul task |
| Colonist forgets entity (LRU eviction) | Remove colonist from `knownBy` |
| `knownBy` becomes empty | Remove task entirely |
| Entity is destroyed | Remove task, notify all colonists to forget |

### Building the Registry

Two approaches, both valid:

**Option A: Maintain incrementally (recommended)**
```cpp
// Called when any colonist's Memory changes
void TaskRegistry::onMemoryUpdated(EntityID colonist, const Memory& memory) {
    // Scan this colonist's known entities
    for (const auto& [posHash, entities] : memory.worldEntities) {
        for (const auto& entity : entities) {
            if (hasWorkCapability(entity.defNameId)) {
                addOrUpdateTask(entity, colonist);
            }
        }
    }
    // Remove tasks this colonist no longer knows about
    pruneTasksNotIn(colonist, memory);
}
```

**Option B: Rebuild periodically**
```cpp
// Called every N seconds (less efficient but simpler)
void TaskRegistry::rebuildFromMemories(World& world) {
    tasks.clear();
    for (auto [entity, memory] : world.view<Memory>()) {
        for (const auto& knownEntity : memory.getAllKnown()) {
            if (hasWorkCapability(knownEntity.defNameId)) {
                addOrUpdateTask(knownEntity, entity);
            }
        }
    }
}
```

---

## Layer 2: Per-Colonist Task View

### Key Difference from Layer 1

Layer 1 shows tasks **any colonist** knows about.
Layer 2 filters to tasks **this specific colonist** knows about.

```python
def get_tasks_for_colonist(colonist, registry):
    # Only tasks THIS colonist knows about
    tasks = [t for t in registry.all_tasks if colonist.id in t.known_by]

    # Further filtering...
    tasks = [t for t in tasks if colonist.work_prefs.is_enabled(t.type)]
    tasks = [t for t in tasks if colonist.skills.can_do(t.required_skill)]
    tasks = [t for t in tasks if not t.reserved or t.reserved_by == colonist.id]

    return tasks
```

### Why Both Filters?

**Layer 1 filter (knownBy):** Performance — don't score thousands of tasks the colonist can't do anyway.

**Layer 2 filter (this colonist's Memory):** Correctness — final verification that colonist actually knows the target.

---

## Layer 3: UI Task List

### What the Player Sees

The UI shows tasks from Layer 1 (all known tasks), with annotations:

```
┌─────────────────────────────────────────────────────────────┐
│ Colony Tasks                                     [Filter ▼] │
├─────────────────────────────────────────────────────────────┤
│ 🫐 Harvest Berry Bush       (10, 15)   📍 5m    ⏳ Available │
│    Known by: Bob, Alice                                     │
│                                                             │
│ 📦 Haul Stick → Storage     (8, 12)    📍 3m    🔒 Bob      │
│    Known by: Bob                                            │
│                                                             │
│ 🫐 Harvest Berry Bush       (45, 32)   📍 40m   ⚠️ Far      │
│    Known by: Alice only                                     │
└─────────────────────────────────────────────────────────────┘
```

### Status Indicators

- **Available**: No reservation, at least one colonist can do it
- **Reserved/In Progress**: Colonist assigned
- **Far**: All knowing colonists are far away
- **Blocked**: Known but nobody can do it (skill locked, work disabled)

---

## Task Generation: Goal-Driven Model

**Critical:** Tasks are NOT generated from discovery. Tasks are generated from GOALS.

- **Discovery** → Updates **Memory** (what colonists know about)
- **Goals** → Generate **Tasks** (what needs to be done)

### Goal Sources

| Goal Source | Task Type | Validity Condition |
|-------------|-----------|-------------------|
| Storage with capacity | Haul | Storage accepts item type + colonist knows loose item location |
| Crafting recipe queued | Gather | Recipe needs material + colonist knows source |
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

### Two-Level Model: Tasks vs Reservations

| Level | Scope | Count | Purpose |
|-------|-------|-------|---------|
| **Task** | Goal | O(goals) | "Storage X wants rocks" |
| **Reservation** | Item | O(colonists working) | "Bob is hauling rock@(5,3)" |

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
 Reserves    Reserves    Reserves   Reserves
 rock@(5,3)  rock@(8,7)  rock@(12,4) rock@(3,9)
```

**Key insight:**
- The **TASK** is shared (1 per storage, goal-level)
- The **RESERVATION** is per-item (prevents conflicts)
- Memory tracks what's known; reservations track what's claimed

### Data Model

```cpp
struct GoalTask {
    EntityID destination;           // Storage entity
    TaskType type;                  // Haul, Gather, etc.
    std::vector<uint32_t> acceptsDefNameIds;  // Item types accepted

    // Reservations: which items are currently being worked
    // Key = item location hash, Value = colonist working it
    std::unordered_map<uint64_t, EntityID> reservations;

    // Computed: how many items are available (known - reserved)
    size_t availableCount() const;
};
```

### Task Resolution Flow

```
1. Storage X has capacity → GoalTask exists: "Fill Storage"
                │
                ▼
2. Bob considers task → checks his Memory for matching items
                │
                ▼
3. Finds 50 rocks known, 2 already reserved by others → 48 available
                │
                ▼
4. Bob picks closest available rock → adds reservation
                │
                ▼
5. Bob hauls rock, deposits → removes reservation
                │
                ▼
6. Task still exists if storage still has capacity
```

### UI Display

The task list shows GOALS with availability status:

```
┌────────────────────────────────────────────┐
│ Fill Storage Crate              (5, 10)    │
│ ✓ Rocks: 48 available (2 in progress)      │
│ ⚠ Wood: blocked (none known)               │
└────────────────────────────────────────────┘
```

### Task Lifecycle

| Event | Task State |
|-------|------------|
| Storage created with capacity | Task created |
| Storage filled completely | Task removed |
| Colonist reserves item | Reservation added, available count decreases |
| Colonist deposits item | Reservation removed, storage capacity decreases |
| All known items reserved/hauled | Task shows "blocked" until more discovered |
| New item discovered | Available count increases |

---

## Reservation System

### Purpose

Prevent multiple colonists from walking to the same task.

### Rules

1. **Reserve on selection:** When colonist selects a task, mark it reserved
2. **Only knowers can reserve:** Colonist must be in `knownBy` to reserve
3. **Release on completion:** When task finishes, release reservation
4. **Release on abandonment:** If colonist switches to higher-priority task
5. **Timeout:** If no progress for 10 seconds, release

### Edge Case: Reserved but Forgotten

If a colonist reserves a task then forgets the target (LRU eviction while moving):

1. Colonist should re-discover target when they arrive (within sight range)
2. If somehow they don't, task becomes unreserved on timeout
3. Another colonist who knows about it can take over

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
| Reservations | Colonists working | ~50 (one per colonist) |

### Practical Numbers

Most colonies will have:
- 5-20 colonists
- 10-50 storage containers
- 5-20 active crafting recipes
- **~50-100 active tasks** (goal-level)
- **~5-20 active reservations** (item-level)

### Spatial Indexing

Still useful for per-colonist queries:

```cpp
// "Find tasks within 50m of Bob that Bob knows about"
auto nearbyTasks = registry.getTasksInRadius(bobPosition, 50.0f);
auto bobTasks = filterByKnownBy(nearbyTasks, bobEntityId);
```

---

## Integration with Existing Systems

### Memory System (Source of Truth)

Memory drives task existence. Changes to implement:

```cpp
// In Memory component or system
void Memory::onEntityDiscovered(const DiscoveredEntity& entity) {
    // Existing logic...
    addWorldEntity(posHash, entity);

    // NEW: Notify task registry
    if (hasWorkCapability(entity.defNameId)) {
        TaskRegistry::Get().onEntityDiscovered(ownerEntityId, entity);
    }
}

void Memory::onEntityForgotten(const DiscoveredEntity& entity) {
    // Existing logic...
    removeWorldEntity(posHash, entity);

    // NEW: Notify task registry
    TaskRegistry::Get().onEntityForgotten(ownerEntityId, entity);
}
```

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

- [Task Generation Architecture](../../technical/task-generation-architecture.md) — Event-driven vs periodic deep dive
- [Priority Config](./priority-config.md) — Priority formula and tunable weights
- [Task Chains](./task-chains.md) — Multi-step tasks
- [Work Priorities](./work-priorities.md) — Per-colonist work preferences
- [Memory System](./memory.md) — How colonists discover and track entities
- [Entity Capabilities](../world/entity-capabilities.md) — How capabilities define available work
