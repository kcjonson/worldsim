# Decision Trace System

**Status:** Design
**Created:** 2025-12-09
**MVP Status:** See [MVP Scope](../../mvp-scope.md) — Task queue display in Phase 1

---

## Overview

The Decision Trace system captures **why** a colonist chose their current task and what alternatives exist. This powers the task queue UI, helping players understand colonist behavior.

**Core Principle:** The "task queue" is not a pre-planned schedule. It's a **computed explanation** of what the colonist would do based on current state, generated on-demand for display.

---

## Design Goals

1. **Player Understanding:** Answer "Why isn't Bob eating?" with clear explanations
2. **Memory Alignment:** Respect that colonists only know about observed entities
3. **Scalable:** No per-entity task generation; compute from needs + memory
4. **Future-Ready:** Support task cancellation UI (Phase 2+)

---

## Architecture

### Component: DecisionTrace

Added to colonists alongside Task component. Updated each time AIDecisionSystem re-evaluates.

```cpp
struct EvaluatedOption {
    TaskType taskType;
    NeedType needType;              // For FulfillNeed tasks
    float needValue;                // Current need value (0-100)
    float threshold;                // Seek threshold for this need

    // Fulfillment status
    enum class Status {
        Selected,       // This is the current task
        Available,      // Could do this, but lower priority
        NoSource,       // Need exists but no known entity
        Satisfied       // Need above threshold, no action needed
    };
    Status status;

    // If Available or Selected
    std::optional<glm::vec2> targetPosition;
    std::optional<uint32_t> targetDefNameId;  // For display name lookup
    float distanceToTarget;

    // Explanation
    std::string reason;  // "Thirst at 35% (below 50% threshold)"
};

struct DecisionTrace {
    // All evaluated options, sorted by priority (highest first)
    std::vector<EvaluatedOption> options;

    // Timestamp of last evaluation
    float lastEvaluationTime;

    // Summary of selection
    std::string selectionSummary;  // "Selected Drink: Thirst more urgent than Hunger"
};
```

### Priority Calculation

Options are sorted by a priority score that matches the existing tier system:

```cpp
float calculateOptionPriority(const EvaluatedOption& option) {
    float priority = 0.0f;

    // Tier 3: Critical needs get highest priority
    if (option.needValue < 10.0f) {
        priority = 300.0f + (10.0f - option.needValue);  // 300-310
    }
    // Tier 5: Actionable needs
    else if (option.needValue < option.threshold) {
        priority = 100.0f + (option.threshold - option.needValue);  // 100-150ish
    }
    // Tier 7: Wander (only if no needs require attention)
    else if (option.taskType == TaskType::Wander) {
        priority = 10.0f;
    }
    // Satisfied needs
    else {
        priority = 0.0f;
    }

    return priority;
}
```

---

## Integration with AIDecisionSystem

### Modified Update Loop

```cpp
void AIDecisionSystem::update(float deltaTime) {
    for (auto [entity, position, needs, memory, task, trace] :
         world->view<Position, NeedsComponent, Memory, Task, DecisionTrace>()) {

        if (!shouldReEvaluate(task, needs)) {
            task.timeSinceEvaluation += deltaTime;
            continue;
        }

        // Build decision trace (evaluates ALL options)
        buildDecisionTrace(entity, position, needs, memory, trace);

        // Select best option from trace
        selectTaskFromTrace(entity, task, trace);

        task.timeSinceEvaluation = 0.0f;
    }
}

void AIDecisionSystem::buildDecisionTrace(
    EntityID entity,
    const Position& position,
    const NeedsComponent& needs,
    const Memory& memory,
    DecisionTrace& trace
) {
    trace.options.clear();
    trace.lastEvaluationTime = currentGameTime();

    // Evaluate each need type
    for (size_t i = 0; i < static_cast<size_t>(NeedType::Count); ++i) {
        auto needType = static_cast<NeedType>(i);
        const auto& need = needs.get(needType);

        EvaluatedOption option;
        option.taskType = TaskType::FulfillNeed;
        option.needType = needType;
        option.needValue = need.value;
        option.threshold = need.seekThreshold;

        // Check memory for fulfillment source
        auto capability = needToCapability(needType);
        auto nearest = findNearestWithCapability(memory, m_registry, capability, position.value);

        if (nearest.has_value()) {
            option.targetPosition = nearest->position;
            option.targetDefNameId = nearest->defNameId;
            option.distanceToTarget = glm::distance(position.value, nearest->position);

            if (need.needsAttention()) {
                option.status = EvaluatedOption::Status::Available;
            } else {
                option.status = EvaluatedOption::Status::Satisfied;
            }
        } else if (needType == NeedType::Energy || needType == NeedType::Bladder) {
            // Ground fallback
            option.targetPosition = position.value;
            option.distanceToTarget = 0.0f;
            option.status = need.needsAttention()
                ? EvaluatedOption::Status::Available
                : EvaluatedOption::Status::Satisfied;
        } else {
            option.status = EvaluatedOption::Status::NoSource;
        }

        option.reason = formatOptionReason(option);
        trace.options.push_back(option);
    }

    // Add wander option
    EvaluatedOption wanderOption;
    wanderOption.taskType = TaskType::Wander;
    wanderOption.needType = NeedType::Count;  // N/A
    wanderOption.status = EvaluatedOption::Status::Available;
    wanderOption.reason = "All needs satisfied";
    trace.options.push_back(wanderOption);

    // Sort by priority (highest first)
    std::sort(trace.options.begin(), trace.options.end(),
        [](const auto& a, const auto& b) {
            return calculateOptionPriority(a) > calculateOptionPriority(b);
        });

    // Mark the top available option as Selected
    for (auto& option : trace.options) {
        if (option.status == EvaluatedOption::Status::Available) {
            option.status = EvaluatedOption::Status::Selected;
            break;
        }
    }

    // Build selection summary
    trace.selectionSummary = buildSelectionSummary(trace.options);
}
```

---

## Memory System Integration

The DecisionTrace respects the memory constraint:

| Scenario | Trace Shows |
|----------|-------------|
| Berry bush in memory | "Eat at Berry Bush (12m away)" |
| No food in memory | Status: `NoSource`, "No known food source" |
| Food exists but not seen | Status: `NoSource` (colonist doesn't know) |

**Key Point:** The trace only shows entities the colonist has observed. If a berry bush exists but the colonist hasn't seen it, it won't appear in the trace.

### Query Pattern

```cpp
// Uses existing MemoryQueries functions
auto nearest = findNearestWithCapability(
    memory,           // Colonist's personal memory
    m_registry,       // For capability lookup
    capability,       // Edible, Drinkable, etc.
    position.value    // Colonist position
);
```

---

## UI Display

### Configuration

```cpp
// In DecisionTrace.h or a config header
constexpr size_t kMaxDisplayedOptions = 10;  // Configurable for future expansion
```

### Task Queue Panel

The EntityInfoPanel displays up to `kMaxDisplayedOptions` items from the trace:

```
┌─────────────────────────────────────┐
│ Bob's Tasks                         │
├─────────────────────────────────────┤
│ 🟢 Drinking at Pond                 │
│    Thirst at 35% (critical)         │
├─────────────────────────────────────┤
│ ⏳ Eat at Berry Bush                │
│    Hunger at 48% (12m away)         │
├─────────────────────────────────────┤
│ ✓ Sleep                             │
│    Energy at 72% (satisfied)        │
├─────────────────────────────────────┤
│ ✓ Toilet                            │
│    Bladder at 85% (satisfied)       │
├─────────────────────────────────────┤
│ 🚶 Wander                           │
│    Fallback when needs satisfied    │
└─────────────────────────────────────┘
```

### Status Icons

| Icon | Status | Meaning |
|------|--------|---------|
| 🟢 | Selected | Currently executing this task |
| ⏳ | Available | Would do next if current completes |
| 🔒 | NoSource | Need exists but no known entity |
| ✓ | Satisfied | Need above threshold |
| 🚶 | Wander | Idle behavior |

### Reason Formatting

```cpp
std::string formatOptionReason(const EvaluatedOption& option) {
    if (option.taskType == TaskType::Wander) {
        return "All needs satisfied";
    }

    std::string reason = needTypeName(option.needType);
    reason += " at " + std::to_string(static_cast<int>(option.needValue)) + "%";

    if (option.status == EvaluatedOption::Status::NoSource) {
        reason += " (no known source)";
    } else if (option.needValue < 10.0f) {
        reason += " (critical)";
    } else if (option.status == EvaluatedOption::Status::Available && option.targetPosition) {
        reason += " (" + std::to_string(static_cast<int>(option.distanceToTarget)) + "m away)";
    } else if (option.status == EvaluatedOption::Status::Satisfied) {
        reason += " (satisfied)";
    }

    return reason;
}
```

---

## Future: Task Cancellation (Phase 2+)

The DecisionTrace enables future task cancellation:

1. Player clicks "Cancel" on current task
2. System marks current option as `Cancelled` (temporary status)
3. Re-run selection, skipping cancelled option
4. Next `Available` option becomes `Selected`
5. UI immediately shows what colonist will do instead

**Not implementing now**, but architecture supports it.

---

## Alignment with Existing Systems

### Task Component

The existing `Task.reason` field is populated from the trace:

```cpp
void selectTaskFromTrace(EntityID entity, Task& task, const DecisionTrace& trace) {
    for (const auto& option : trace.options) {
        if (option.status == EvaluatedOption::Status::Selected) {
            task.type = option.taskType;
            task.needToFulfill = option.needType;
            task.targetPosition = option.targetPosition.value_or(glm::vec2{0, 0});
            task.reason = option.reason;  // Populated from trace
            return;
        }
    }
}
```

### Reservation System (Future)

When reservation is implemented:

```cpp
enum class Status {
    Selected,
    Available,
    NoSource,
    Satisfied,
    Reserved    // Added: Another colonist claimed this
};
```

The trace will show "Berry Bush (reserved by Alice)" to explain why Bob didn't pick it.

---

## Performance Considerations

| Operation | Complexity | Notes |
|-----------|------------|-------|
| Evaluate all needs | O(4) | Fixed 4 need types |
| Memory query per need | O(n) | n = entities with capability in memory |
| Sort options | O(1) | Fixed ~5 options (4 needs + wander) |
| Total per colonist | O(4n) | Same as current AIDecisionSystem |

**No additional performance cost** - we're capturing information already computed.

---

## Implementation Checklist

- [ ] Add `EvaluatedOption` struct to new header
- [ ] Add `DecisionTrace` component
- [ ] Modify `AIDecisionSystem::update()` to build trace
- [ ] Add `buildDecisionTrace()` method
- [ ] Update `EntityInfoPanel` to display trace
- [ ] Add status icons and formatting
- [ ] Unit tests for trace generation
- [ ] Unit tests for priority sorting

---

## Related Documents

- [AI Behavior](./ai-behavior.md) — Decision hierarchy this trace explains
- [Memory System](./memory.md) — Constrains what colonists know about
- [Needs System](./needs.md) — Need thresholds and decay
- [MVP Scope](../../mvp-scope.md) — Task queue display requirements
