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
// Effect types - each contains ONLY relevant data
struct NeedEffect {
    NeedType need;
    float restoreAmount;
    NeedType sideEffectNeed = NeedType::Count;  // optional
    float sideEffectAmount = 0.0F;
};

struct ProductionEffect {
    std::string recipeId;
    uint64_t sourceEntityId;
};

struct ProgressEffect {
    uint64_t targetEntityId;
    float progressAmount;
};

// Variant holds exactly one effect type at a time
using ActionEffect = std::variant<
    std::monostate,      // No effect (ActionType::None)
    NeedEffect,          // Need fulfillment actions
    ProductionEffect,    // Crafting/harvesting actions
    ProgressEffect       // Building/repairing actions
>;

struct Action {
    ActionType type;
    ActionState state;
    float duration;
    float elapsed;
    glm::vec2 targetPosition;

    ActionEffect effect;  // Type-specific data

    // Helper methods for type-safe access
    [[nodiscard]] bool hasNeedEffect() const {
        return std::holds_alternative<NeedEffect>(effect);
    }
    [[nodiscard]] const NeedEffect& needEffect() const {
        return std::get<NeedEffect>(effect);
    }
};
```

### Why std::variant is ECS-Idiomatic

1. **No heap allocation** - variant stores data inline (unlike polymorphic pointers)
2. **Cache-friendly** - contiguous memory layout preserved
3. **Type-safe** - compiler enforces correct access
4. **Compile-time exhaustiveness** - `std::visit` ensures all cases handled

### Usage in Systems

**Pattern 1: Check-and-access (simple cases)**
```cpp
void ActionSystem::completeAction(Action& action, NeedsComponent& needs) {
    if (action.hasNeedEffect()) {
        const auto& effect = action.needEffect();
        needs.get(effect.need).restore(effect.restoreAmount);
    }
}
```

**Pattern 2: std::visit (when handling all cases)**
```cpp
void ActionSystem::applyEffect(Action& action, World& world) {
    std::visit([&](auto&& effect) {
        using T = std::decay_t<decltype(effect)>;

        if constexpr (std::is_same_v<T, std::monostate>) {
            // No effect - do nothing
        } else if constexpr (std::is_same_v<T, NeedEffect>) {
            // Handle need restoration
        } else if constexpr (std::is_same_v<T, ProductionEffect>) {
            // Handle item production
        } else if constexpr (std::is_same_v<T, ProgressEffect>) {
            // Handle construction progress
        }
    }, action.effect);
}
```

### Adding New Effect Types

To add a new action category:

1. **Define the effect struct:**
```cpp
struct SocialEffect {
    uint64_t targetColonistId;
    float moodChange;
    std::string interactionType;
};
```

2. **Add to the variant:**
```cpp
using ActionEffect = std::variant<
    std::monostate,
    NeedEffect,
    ProductionEffect,
    ProgressEffect,
    SocialEffect  // New!
>;
```

3. **Add helper methods to Action:**
```cpp
[[nodiscard]] bool hasSocialEffect() const {
    return std::holds_alternative<SocialEffect>(effect);
}
[[nodiscard]] const SocialEffect& socialEffect() const {
    return std::get<SocialEffect>(effect);
}
```

4. **Update factory methods:**
```cpp
static Action Chat(uint64_t targetId) {
    Action action;
    action.type = ActionType::Chat;
    action.duration = 5.0F;
    action.effect = SocialEffect{targetId, 5.0F, "chat"};
    return action;
}
```

5. **Update systems** that use `std::visit` to handle the new type.

---

## Terminology: Actions vs Effects

### Action
What the colonist is **doing** - the activity itself with timing and state.

- Has a type (Eat, Drink, Sleep, Craft, Build, etc.)
- Has a state machine (Starting → InProgress → Complete)
- Has duration and elapsed time
- May have a target position

### Effect
What **happens** when the action completes - the outcome.

- `NeedEffect` - Restores/drains a need value
- `ProductionEffect` - Produces items (crafting, harvesting)
- `ProgressEffect` - Advances construction/repair
- `SpawnEffect` - Creates entities in the world

### Why the Separation Matters

The original design used "restoreAmount" which only makes sense for needs:
- Eating "restores" hunger ✓
- Crafting "restores" ??? ✗

By separating actions from effects:
- Actions describe **what you're doing**
- Effects describe **what happens when you're done**
- Each effect type uses appropriate terminology

---

## Factory Pattern for Actions

Actions use static factory methods instead of constructors:

```cpp
// Good - factory with meaningful name and clear parameters
auto action = Action::Eat(nutrition);
auto action = Action::Sleep(quality);
auto action = Action::Craft(recipeId, ingredients);

// Avoid - constructor with many parameters
Action action(ActionType::Eat, 2.0F, NeedType::Hunger, nutrition * 100.0F, ...);
```

**Benefits:**
- Self-documenting code
- Type-specific validation
- Correct effect type automatically set
- Impossible to create invalid action/effect combinations

---

## Side Effects in NeedEffect

Some need-fulfillment actions affect multiple needs:

```cpp
struct NeedEffect {
    NeedType need;           // Primary need (e.g., Thirst)
    float restoreAmount;     // Primary restoration (e.g., +40%)

    NeedType sideEffectNeed = NeedType::Count;  // Secondary need (e.g., Bladder)
    float sideEffectAmount = 0.0F;              // Secondary change (e.g., -15%)
};
```

**Sign convention:**
- Positive `sideEffectAmount` = restore (add to need value)
- Negative `sideEffectAmount` = drain (subtract from need value)

Example: Drinking restores thirst (+40%) but drains bladder (-15%):
```cpp
NeedEffect effect{
    .need = NeedType::Thirst,
    .restoreAmount = 40.0F,
    .sideEffectNeed = NeedType::Bladder,
    .sideEffectAmount = -15.0F  // Negative = drains
};
```

---

## Related Documents

- [C++ Coding Standards](./cpp-coding-standards.md) - Basic ECS overview
- [AI Behavior](../design/game-systems/colonists/ai-behavior.md) - How colonists decide to act
- [Entity Capabilities](../design/game-systems/world/entity-capabilities.md) - How entities fulfill needs
- Source: `libs/engine/ecs/components/Action.h`
- Source: `libs/engine/ecs/systems/ActionSystem.cpp`
