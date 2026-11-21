# ECS Design Patterns

**Status:** Active
**Created:** 2025-12-07
**Last Updated:** 2025-12-07

---

## Overview

This document describes design patterns used in our ECS implementation beyond the basics covered in [C++ Coding Standards](./cpp-coding-standards.md#entity-component-system-ecs).

---

## Polymorphic Data with std::variant

### The Problem

ECS components should be pure data (POD structs) for cache-friendly storage. But some components need to hold different data types based on a category:

**Bad approach - Union of all fields:**
```cpp
// Don't do this - confusing which fields apply to which type
struct Action {
    ActionType type;

    // Need fulfillment (only used when type == Eat/Drink/Sleep/Toilet)
    NeedType needToFulfill;
    float restoreAmount;
    float sideEffectAmount;

    // Production (only used when type == Harvest/Craft)
    std::string recipeId;
    uint64_t sourceEntityId;

    // Progress (only used when type == Build/Repair)
    uint64_t targetEntityId;
    float progressAmount;
};
```

Problems:
- Most fields are unused for any given action type
- Unclear which fields apply when
- Easy to accidentally use wrong fields
- Wastes memory

### The Solution: std::variant

Use `std::variant` to hold type-specific data in a tagged union:

```cpp
// The target data an effect row acts on - each holds ONLY what its kind needs
struct CollectionEffect {   // a <take> from a harvestable, a pile, or a box
    std::string itemDefName;
    uint32_t quantity = 1;
    glm::vec2 sourcePosition{0.0F, 0.0F};
    // ...
};

struct ProgressEffect {     // the blueprint a <work> row advances
    uint64_t targetEntityId = 0;
    float skillLevel = 0.0F;
};

// Variant holds exactly one target kind at a time
using ActionEffect =
    std::variant<std::monostate, CollectionEffect, ProgressEffect, CraftingEffect, DepositEffect, PlacePackagedEffect>;

struct Action {
    ActionType type;
    ActionState state;
    const ActionValues* values;  // the entry it runs: its effect rows, in the order written
    EffectContext context;       // what the rows read and do not carry (nutrition, sleep quality, ...)
    float duration;
    float elapsed;

    ActionEffect effect;  // the target the rows act on

    [[nodiscard]] bool hasProgressEffect() const {
        return std::holds_alternative<ProgressEffect>(effect);
    }
    [[nodiscard]] const ProgressEffect& progressEffect() const {
        return std::get<ProgressEffect>(effect);
    }
};
```

What an action does is not in the variant: it is the entry's effect rows (`<restore>`, `<consume>`, `<take>`, and
the rest), resolved once at load into `ActionValues::effects`, which `ActionSystem` applies through one primitive per
row kind ([Queries and chains](./queries-and-chains.md#primitive-effects)).

### Why std::variant is ECS-Idiomatic

1. **No heap allocation** - variant stores data inline (unlike polymorphic pointers)
2. **Cache-friendly** - contiguous memory layout preserved
3. **Type-safe** - compiler enforces correct access
4. **Compile-time exhaustiveness** - `std::visit` ensures all cases handled

### Usage in Systems

**Pattern 1: Check-and-access (simple cases)**
```cpp
bool ActionSystem::applyPut(const Action& action, Inventory& inventory) {
    if (!action.hasDepositEffect()) {
        return refuseWithoutTarget(action, "put");
    }
    const auto& effect = action.depositEffect();
    // ... move effect.quantity of effect.itemDefName into effect.storageEntityId
}
```

**Pattern 2: dispatch on a variant of rows**
```cpp
for (const EffectRow& row : action.values->effects) {
    if (const auto* restore = std::get_if<RestoreRow>(&row)) {
        payRestore(index, *restore, restoredAmount(*restore, action.context), action, needs);
    } else if (const auto* consume = std::get_if<ConsumeRow>(&row)) {
        took = applyConsume(*consume, action, inventory);
    } // ... one branch per row kind
}
```

### Adding a target kind

A new effect row kind whose target needs data of its own adds a struct to `ActionEffect`, a `has...`/`...()` pair on
`Action`, and a primitive that refuses (takes nothing, with a warning) when the action carries no such target.

---

## Terminology: Actions vs Effects

### Action
What the colonist is **doing** - the activity itself with timing and state.

- Has a type (Eat, Drink, Sleep, Craft, Build, etc.)
- Has a state machine (Starting → InProgress → Complete)
- Has duration and elapsed time
- May have a target position

### Effect
What **happens** - the action entry's effect rows, applied in the order written: `<restore>` moves a need,
`<consume>` uses up a held unit, `<take>`, `<put>`, `<craft>`, and `<install>` act on the target in the action's
`effect`, `<work>` advances a blueprint, `<spawn>` creates a thing. A row paid at the end applies when the action
completes; one paid along applies as it goes.

---

## Factory Pattern for Actions

Actions use static factory methods instead of constructors. Each takes the action entries resolved at load
(`ecs::ActionEntries`), from which it takes its entry (duration, interruptability, effect rows), and sets only the
context and target the rows act on:

```cpp
// Good - factory with meaningful name and clear parameters
auto action = Action::Eat(actionEntries, foodDefNameId, nutrition);
auto action = Action::Sleep(actionEntries, quality);
auto action = Action::Craft(actionEntries, recipeDefName, stationId, stationPos, workAmount, inputs, outputs);

// Avoid - constructor with many parameters (no longer valid)
// Action action(ActionType::Eat, ...);
```

**Benefits:**
- Self-documenting code
- Type-specific validation
- Correct effect type automatically set
- Impossible to create invalid action/effect combinations

---

## Several needs from one action

An action that moves several needs has a `<restore>` row for each, with a signed amount: a rise stops at 100, a fall
at 0. Drinking restores thirst and drains the bladder:
```xml
<restore need="thirst" amount="100"/>
<restore need="bladder" amount="-15"/>
```

---

## Related Documents

- [C++ Coding Standards](./cpp-coding-standards.md) - Basic ECS overview
- [AI Behavior](../design/game-systems/colonists/ai-behavior.md) - How colonists decide to act
- [Entity Capabilities](../design/game-systems/world/entity-capabilities.md) - How entities fulfill needs
- Source: `libs/sim/ecs/components/Action.h`
- Source: `libs/sim/ecs/systems/ActionSystem.cpp`
