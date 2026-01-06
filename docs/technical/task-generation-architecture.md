# Task Generation Architecture

**Status:** Technical Design
**Created:** 2025-01-02
**Related:** [Task Registry System](../design/game-systems/colonists/task-registry.md)

---

## Overview

This document analyzes different approaches to task generation and recommends a hybrid architecture that balances freshness, performance, and UI responsiveness.

**Key Constraint:** The world is infinite. We cannot scan all entities. Tasks must be sourced from colonist Memory (what colonists have discovered).

---

## Two Different Priority Systems

### Critical Distinction

There are **two separate ordering systems**:

| System | Purpose | Includes | Used By |
|--------|---------|----------|---------|
| **Global Display Priority** | UI ordering for player | Base priority + distance from reference | GlobalTaskListView |
| **Colonist Selection Priority** | Which task a colonist picks | Base + distance + skill + chain + in-progress | AIDecisionSystem |

### Global Display Priority (UI)

The global task list shows tasks ordered by:
1. **Work type tier** (Emergency > Medical > Farming > ... > Cleaning)
2. **Distance from reference point** (camera or selected colonist)

**Does NOT include:**
- Skill bonuses (which colonist's skills?)
- Work preference filtering (which colonist's preferences?)
- Chain continuation bonus (which colonist's chain?)
- In-progress bonus (which colonist's current task?)

```cpp
// UI ordering - simple, colonist-agnostic
float getDisplayPriority(const GlobalTask& task, glm::vec2 referencePoint) {
    float basePriority = getWorkTypeTierPriority(task.type);  // 1000-7000
    float distance = glm::distance(referencePoint, task.position);
    float distancePenalty = distance * 10.0f;  // Further = lower display priority
    return basePriority - distancePenalty;
}
```

### Colonist Selection Priority (AI)

When a colonist picks their next task:
1. **Filter:** Only tasks this colonist knows about + has enabled + has skill for
2. **Score:** Base + distance + skill bonus + chain bonus + in-progress bonus
3. **Select:** Highest scoring task

```cpp
// AI ordering - full formula with all bonuses
float getSelectionPriority(const GlobalTask& task, const Colonist& colonist) {
    float base = getWorkTypeTierPriority(task.type);
    float distanceBonus = calculateDistanceBonus(colonist.position, task.position);
    float skillBonus = colonist.skills.getLevel(task.requiredSkill) * 10.0f;
    float chainBonus = (colonist.currentChainId == task.chainId) ? 2000.0f : 0.0f;
    float inProgressBonus = (colonist.currentTaskId == task.id) ? 200.0f : 0.0f;
    return base + distanceBonus + skillBonus + chainBonus + inProgressBonus;
}
```

---

## The Core Problem

We need to answer two questions:

1. **What tasks exist?** (Task Existence)
2. **What priority should each task have?** (Task Scoring — differs by context!)

### Critical: Goal-Driven Task Generation

**Tasks are NOT generated from discovery. Tasks are generated from GOALS.**

- **Discovery** → Updates **Memory** (what colonists know about)
- **Goals** → Generate **Tasks** (what needs to be done)

A task requires BOTH:
1. A **GOAL** that needs achieving (storage wanting items, hungry colonist, etc.)
2. **Knowledge** of how to achieve it (colonist knows item location in Memory)

| Aspect | Task Existence | UI Display Priority | Colonist Selection Priority |
|--------|----------------|---------------------|----------------------------|
| Source | Goal systems (Storage, Needs, Crafting) | Task + reference point | Task + colonist state |
| Changes when | Goal created/fulfilled | Reference point moves | Colonist moves/changes |
| Includes colonist bonuses | N/A | No | Yes |
| Update frequency | On goal events | 4-10 Hz | 0.5s per colonist |

---

## Approach Analysis

### 1. On-Demand Per-Colonist (Current System)

**How it works:**
```cpp
// Every 0.5s per colonist
void buildDecisionTrace(EntityID colonist, ...) {
    for (auto& knownEntity : memory.getAll()) {
        if (hasWorkCapability(knownEntity)) {
            float priority = calculatePriority(knownEntity, colonistPos);
            trace.addOption(knownEntity, priority);
        }
    }
}
```

**Pros:**
- No persistent task list (minimal memory)
- Scores always fresh (calculated at selection time)
- Already implemented

**Cons:**
- **No global view:** Cannot show "all colony tasks" in UI
- **Redundant work:** Multiple colonists scan same entities
- **No reservation:** Two colonists might walk to same target

**Verdict:** Good for per-colonist scoring, but insufficient for global visibility.

---

### 2. Periodic Global Scan

**How it works:**
```cpp
// Every 0.5s globally
void TaskGenerationSystem::update() {
    globalTasks.clear();
    for (auto [colonist, memory] : world.view<Memory>()) {
        for (auto& entity : memory.getAll()) {
            globalTasks.addOrUpdate(entity, colonist);
        }
    }
}
```

**Pros:**
- Global task list available for UI
- Single source of truth
- Easy reservation system

**Cons:**
- **Stale between scans:** 0.5s gaps noticeable
- **Wasted work:** Regenerates unchanged tasks

**Verdict:** Better for UI, but wasteful.

---

### 3. Event-Driven Generation

**How it works:**
```cpp
// Called when Memory changes
void Memory::onEntityDiscovered(entity) {
    TaskRegistry::Get().addTask(entity, ownerColonist);
}
```

**Pros:**
- **Always fresh:** Tasks update immediately
- **No wasted work:** Only updates what changed
- **Global list available:** For UI

**Cons:**
- **Event wiring complexity:** Every Memory change must notify registry

**Verdict:** Best for existence tracking.

---

### 4. Hybrid Architecture (Recommended)

**Core Insight:**
- Event-driven for *existence*
- On-demand for *colonist selection priority*
- Throttled for *UI display priority*

```
Memory Events (discover/forget)
         │
         ▼ [EVENT-DRIVEN]
┌─────────────────────────────────────┐
│  Global Task Registry               │
│  - What tasks EXIST                 │
│  - Which colonists know each task   │
│  - Reservation status               │
│  - Base priority (work type tier)   │
│  - NO colonist-specific bonuses     │
└─────────────────────────────────────┘
         │
         ├──────────────────────────────────┐
         │                                  │
         ▼ [ON-DEMAND at selection]         ▼ [THROTTLED 4-10 Hz]
┌─────────────────────────────┐    ┌─────────────────────────────┐
│  Colonist Selection         │    │  UI Display                 │
│  - Filter by this colonist  │    │  - All tasks (any colonist) │
│  - Full priority formula    │    │  - Simple ordering          │
│  - Skill/chain/in-progress  │    │  - Distance from reference  │
│  - When colonist needs task │    │  - No colonist bonuses      │
└─────────────────────────────┘    └─────────────────────────────┘
```

---

## UI Global Task List

### What It Shows

All tasks known to ANY colonist, ordered by:
1. **Work category tier** (Emergency first, Cleaning last)
2. **Distance from reference point** (selected colonist or camera)

### What It Does NOT Show

- Per-colonist priority scoring
- Skill bonuses (would need to pick a colonist)
- Chain bonuses (specific to one colonist)
- Whether a specific colonist would choose this task

### Example UI

```
┌─────────────────────────────────────────────────────────────┐
│ Colony Tasks                              Sort: [Distance ▼]│
├─────────────────────────────────────────────────────────────┤
│ FARMING (Tier 3)                                            │
│   🫐 Harvest Berry Bush    (10,15)   5m    Available        │
│   🫐 Harvest Berry Bush    (12,18)   8m    🔒 Bob           │
│   🫐 Harvest Berry Bush    (45,32)   40m   Available        │
│                                                             │
│ HAULING (Tier 6)                                            │
│   📦 Haul Stick            (8,12)    3m    🔒 Alice         │
│   📦 Haul Berry            (11,14)   6m    Available        │
│                                                             │
│ CLEANING (Tier 7)                                           │
│   🧹 Clean Area            (5,5)     2m    Available        │
└─────────────────────────────────────────────────────────────┘
```

### Sorting Options

Player can choose:
- **Distance** (default): Closest first within each tier
- **Priority**: Strict tier ordering, then distance
- **Category**: Group by work category

---

## Update Frequency Summary

| Component | Update Trigger | Frequency |
|-----------|----------------|-----------|
| Task Registry (existence) | Memory events | Immediate |
| Colonist Selection Priority | Task selection | 0.5s (kReEvalInterval) |
| UI Display Priority | UI frame | 4-10 Hz (100-250ms) |
| Reservations | Task claim/release | Immediate |

---

## Event Wiring Requirements

### Goal Sources (What Creates Tasks)

| Goal System | Task Type | Creates Task When | Removes Task When |
|-------------|-----------|-------------------|-------------------|
| StorageSystem | Haul | Storage has capacity | Storage filled completely |
| CraftingSystem | Gather | Recipe queued, needs materials | Recipe completed/cancelled |
| NeedsSystem | FulfillNeed | Need below threshold | Need satisfied |
| BuildSystem | Haul/Place | Build order placed, needs materials | Build completed/cancelled |

### Events That Update Task Availability

| Source System | Event | Registry Action |
|---------------|-------|-----------------|
| Memory | Entity discovered | Update task availability (more fulfillment options) |
| Memory | Entity forgotten | Update task availability (fewer options) |
| ActionSystem | Item reserved | Mark item unavailable for other colonists |
| ActionSystem | Item delivered | Release reservation, update goal progress |
| Goal system | Goal fulfilled | Remove task entirely |

### Implementation Pattern

```cpp
// Goal systems CREATE tasks
class StorageSystem {
    void onStorageCapacityAvailable(EntityID storage, const std::vector<uint32_t>& acceptedTypes) {
        // Storage wants items - create goal-level task
        TaskRegistry::Get().createGoalTask(GoalTask{
            .destination = storage,
            .type = TaskType::Haul,
            .acceptsDefNameIds = acceptedTypes
        });
    }

    void onStorageFilled(EntityID storage) {
        // Storage full - remove the task (goal achieved)
        TaskRegistry::Get().removeGoalTask(storage, TaskType::Haul);
    }
};

// Memory updates task AVAILABILITY (not existence)
class Memory {
    void addWorldEntity(uint64_t posHash, const DiscoveredEntity& entity) {
        // Existing logic...
        worldEntities[posHash].push_back(entity);

        // Notify registry: colonist now knows about potential fulfillment option
        // This does NOT create a task - it makes existing tasks more fulfillable
        TaskRegistry::Get().onFulfillmentOptionDiscovered(ownerEntityId, entity);
    }

    void forgetEntity(uint64_t posHash, uint32_t defNameId) {
        // Existing logic...

        // Notify registry: colonist forgot about this fulfillment option
        TaskRegistry::Get().onFulfillmentOptionForgotten(ownerEntityId, posHash, defNameId);
    }
};
```

---

## Performance Analysis

### Bounded by Goals (Not Entities)

With goal-driven task generation, task count is bounded by **active goals**, not discovered entities:

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
| Reservations | Colonists actively working | ~50 (one per colonist max) |

### Per-Frame Costs

| Operation | Frequency | Cost |
|-----------|-----------|------|
| Goal event processing | Per storage/craft/need change | O(1) |
| Memory event processing | Per discovery | O(1) |
| Per-colonist scoring | 0.5s per colonist | O(T_goals) — now ~200, not ~100,000 |
| UI aggregation | 10 Hz | O(T_goals) sort — trivial at ~200 tasks |

---

## Comparison: UI vs AI Ordering

### Scenario: Bob (skilled farmer) and Alice (skilled hauler)

**Global Task List (UI):**
```
1. Harvest Berry Bush (10,15) - 5m - Tier 3
2. Haul Stick (8,12) - 3m - Tier 6
```
Harvest shows first because Tier 3 > Tier 6, even though haul is closer.

**Bob's Selection Priority (AI):**
```
1. Harvest Berry Bush: 3000 (base) + 45 (distance) + 80 (farming skill) = 3125
2. Haul Stick: 1000 (base) + 48 (distance) + 0 (no skill bonus) = 1048
```
Bob picks harvest.

**Alice's Selection Priority (AI):**
```
1. Harvest Berry Bush: 3000 (base) + 45 (distance) + 20 (low farming) = 3065
2. Haul Stick: 1000 (base) + 48 (distance) + 50 (strength bonus) = 1098
```
Alice also picks harvest (tier wins), but with less skill bonus.

**Key Point:** The UI ordering gives a general colony overview. Individual colonist priorities may differ based on their bonuses.

---

## Summary

| Question | Answer |
|----------|--------|
| How do we know what tasks exist? | Goal systems create tasks (Storage, Crafting, Needs, Build) |
| What role does Memory play? | Provides fulfillment options for goals (what items colonists know about) |
| Where do we store task existence? | GlobalTaskRegistry (goal-level tasks) |
| How many tasks can exist? | ~200 (bounded by goals, not discovered entities) |
| Does UI include colonist bonuses? | **No** — just base priority + distance |
| When do we calculate colonist priority? | At selection time (0.5s interval) |
| How does UI stay ordered? | Throttled refresh (4-10 Hz) by tier + distance |
| How do we scale to infinite world? | Tasks bounded by goals; Memory bounded by LRU per colonist |

---

## Config Validation

The task system is config-driven with multiple XML files that reference each other by `defName` strings. Validation ensures these references are valid at load time, not runtime.

### Design Principle: Fail Fast

**All validation errors are fatal.** The game refuses to start if any config is invalid. This ensures:
1. Modders get clear error messages immediately
2. No silent failures or undefined behavior
3. Problems are caught in development, not in player saves

### Load Order (Dependency Chain)

Configs must load in dependency order. Each stage validates references to previously loaded data:

```
┌─────────────────────────────────────────────────────────────────┐
│ 1. action-types.xml (no dependencies)                           │
│    └─► ActionTypeRegistry populated                             │
├─────────────────────────────────────────────────────────────────┤
│ 2. task-chains.xml                                              │
│    └─► Validate: step.actionDefName exists in ActionTypeRegistry│
│    └─► TaskChainRegistry populated                              │
├─────────────────────────────────────────────────────────────────┤
│ 3. work-types.xml                                               │
│    └─► Validate: taskChain exists in TaskChainRegistry          │
│    └─► Validate: triggerCapability is valid CapabilityType      │
│    └─► Validate: skillRequired exists (if specified)            │
│    └─► WorkTypeRegistry populated                               │
├─────────────────────────────────────────────────────────────────┤
│ 4. priority-tuning.xml                                          │
│    └─► Validate: WorkCategoryOrder categories exist             │
│    └─► PriorityConfig populated                                 │
└─────────────────────────────────────────────────────────────────┘
```

### Validation Errors

| Error Type | Severity | Example |
|------------|----------|---------|
| XML syntax error | Fatal | Missing closing tag |
| Missing required field | Fatal | WorkType without defName |
| Invalid reference | Fatal | `taskChain="Chain_Typo"` |
| Duplicate defName | Warning | Two WorkTypes with same name (second ignored) |
| Unknown attribute | Warning | `<WorkType foo="bar">` (ignored) |

### Implementation

```cpp
class ConfigValidator {
public:
    // Call after each registry loads
    static bool validateActionTypes();        // No deps - just syntax check
    static bool validateTaskChains();         // Refs ActionTypeRegistry
    static bool validateWorkTypes();          // Refs TaskChainRegistry, CapabilityType
    static bool validatePriorityConfig();     // Refs WorkTypeRegistry

    // Call at end of config loading
    static bool validateAll();                // Cross-registry checks
};

// In game initialization
bool loadAllConfigs() {
    // 1. Action types (no dependencies)
    if (!ActionTypeRegistry::Get().loadFromFile("assets/config/actions/action-types.xml")) {
        LOG_ERROR(Config, "Failed to load action-types.xml");
        return false;
    }

    // 2. Task chains (depends on action types)
    if (!TaskChainRegistry::Get().loadFromFile("assets/config/work/task-chains.xml")) {
        LOG_ERROR(Config, "Failed to load task-chains.xml");
        return false;
    }
    if (!ConfigValidator::validateTaskChains()) {
        return false;  // Errors already logged
    }

    // 3. Work types (depends on chains, capabilities)
    if (!WorkTypeRegistry::Get().loadFromFolder("assets/config/work/")) {
        LOG_ERROR(Config, "Failed to load work types");
        return false;
    }
    if (!ConfigValidator::validateWorkTypes()) {
        return false;
    }

    // 4. Priority tuning (depends on work types)
    if (!PriorityConfig::Get().loadFromFile("assets/config/work/priority-tuning.xml")) {
        LOG_ERROR(Config, "Failed to load priority-tuning.xml");
        return false;
    }
    if (!ConfigValidator::validatePriorityConfig()) {
        return false;
    }

    // Final cross-validation
    return ConfigValidator::validateAll();
}
```

### Reference Validation Example

```cpp
bool ConfigValidator::validateTaskChains() {
    bool valid = true;

    for (const auto& chain : TaskChainRegistry::Get().getAllChains()) {
        for (const auto& step : chain.steps) {
            // Check action reference
            if (!ActionTypeRegistry::Get().hasAction(step.actionDefName)) {
                LOG_ERROR(Config,
                    "Chain '%s' step %d references unknown action '%s'\n"
                    "  Available actions: %s",
                    chain.defName.c_str(),
                    step.order,
                    step.actionDefName.c_str(),
                    ActionTypeRegistry::Get().getAvailableActionsString().c_str()
                );
                valid = false;
            }
        }
    }

    return valid;
}
```

### Error Message Format

Error messages should be actionable:

```
[ERROR] Config: Chain 'Chain_PickupDeposit' step 1 references unknown action 'Depositt'
  Available actions: Pickup, Deposit, Place, Eat, Drink, Sleep, Harvest, Craft, Build
  Location: assets/config/work/task-chains.xml:15
```

### Mod Loading

Mods load after base game configs, following the same validation:

```
Base: assets/config/work/*.xml
Mods: mods/my-mod/config/work/*.xml (alphabetical order)
```

Each mod config is validated against the accumulated registry. A mod can:
- ✅ Add new work types referencing base game chains
- ✅ Add new chains referencing base game actions
- ✅ Override base game definitions (same defName)
- ❌ Reference another mod's definitions (unless loaded earlier alphabetically)
