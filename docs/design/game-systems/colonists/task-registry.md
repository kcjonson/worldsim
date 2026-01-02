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

## Task Generation from Capabilities

Tasks are generated when colonists **discover** entities with capabilities:

| Capability | When Discovered | Generated Task |
|------------|-----------------|----------------|
| `Harvestable` | Colonist sees berry bush | Harvest task |
| `Carryable` (on ground) | Colonist sees loose item | Haul task |
| `Craftable` + queued recipe | Colonist knows station | Craft task |
| `Constructable` | Colonist sees blueprint | Build task |
| `Packaged` | Colonist sees furniture | PlacePackaged task |

### Discovery Lifecycle

```
Entity spawns in unexplored area
         │
         ▼ (no colonist sees it)
    [NO TASK EXISTS]
         │
         ▼ (colonist walks nearby, enters sight radius)
Colonist discovers entity → Memory updated
         │
         ▼
TaskRegistry creates task (knownBy: [colonist])
         │
         ▼ (second colonist discovers same entity)
TaskRegistry updates task (knownBy: [colonist, colonist2])
         │
         ▼ (both colonists forget due to LRU)
TaskRegistry removes task
```

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

### Bounded by Memory

| Factor | Bound |
|--------|-------|
| Entities per colonist memory | ~10,000 (LRU limit) |
| Colonists | ~50 (typical colony) |
| Max known entities (union) | ~50,000 (with overlap) |
| Tasks per entity | 1-2 average |
| **Max tasks in registry** | **~100,000** |

### Practical Numbers

Most colonies will have:
- 5-20 colonists
- 2,000-10,000 known entities per colonist
- Heavy overlap (colonists share knowledge via social)
- **~5,000-20,000 active tasks**

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
