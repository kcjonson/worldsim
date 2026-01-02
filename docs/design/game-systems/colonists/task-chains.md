# Task Chains

**Status:** Design
**Created:** 2025-01-02
**MVP Status:** Phase 2+ (after basic hauling works)

---

## Overview

Some tasks require multiple steps that should stay together. A colonist who picks up an item should carry it to storage, not drop it halfway to eat a berry.

**Task Chains** link related steps with a shared identifier. Colonists receive a large priority bonus (+2000) to complete the next step of their current chain.

---

## Hands System (All Config-Driven)

### Design Principle

**Nothing hardcoded.** Both sides of the equation are defined in XML:
1. **Items:** How many hands needed to carry (in asset definition)
2. **Actions:** Whether they require free hands (in action definition)

This allows designers to balance without code changes, and modders to add new items/actions freely.

---

### Item Hands Required (Asset Definition)

Defined in each item's XML via the `Carryable` capability:

**File:** `assets/world/misc/Stick/Stick.xml`
```xml
<AssetDef>
  <defName>Stick</defName>
  <capabilities>
    <carryable handsRequired="1"/>
  </capabilities>
</AssetDef>
```

**File:** `assets/world/misc/LargeRock/LargeRock.xml`
```xml
<AssetDef>
  <defName>LargeRock</defName>
  <capabilities>
    <carryable handsRequired="2"/>
  </capabilities>
</AssetDef>
```

**File:** `assets/world/storage/BasicBox/BasicBox.xml`
```xml
<AssetDef>
  <defName>BasicBox</defName>
  <capabilities>
    <!-- When packaged (not placed), needs 2 hands -->
    <carryable handsRequired="2" packaged="true"/>
    <!-- When placed, is storage -->
    <storage capacity="20"/>
  </capabilities>
</AssetDef>
```

### Hands Required Values

| Value | Meaning | Can Stow to Backpack? |
|-------|---------|----------------------|
| 0 | Pocket-sized | Always (no hands used) |
| 1 | One-handed | Yes, if backpack has space |
| 2 | Two-handed | Never |

---

### Action Hands Required (Action Definition)

Defined in a separate config file listing all action types:

**File:** `assets/config/actions/action-types.xml`
```xml
<?xml version="1.0" encoding="UTF-8"?>
<ActionTypes>
  <!-- Need Fulfillment Actions -->
  <Action defName="Eat" needsHands="true">
    <description>Consuming food</description>
  </Action>

  <Action defName="Drink" needsHands="true">
    <description>Drinking water</description>
  </Action>

  <Action defName="Sleep" needsHands="false">
    <description>Resting on a sleepable surface</description>
  </Action>

  <Action defName="Toilet" needsHands="false">
    <description>Relieving bladder</description>
  </Action>

  <!-- Work Actions -->
  <Action defName="Harvest" needsHands="true">
    <description>Gathering from harvestable entity</description>
  </Action>

  <Action defName="Craft" needsHands="true">
    <description>Working at a crafting station</description>
  </Action>

  <Action defName="Build" needsHands="true">
    <description>Constructing a structure</description>
  </Action>

  <Action defName="Pickup" needsHands="true">
    <description>Picking up an item</description>
  </Action>

  <Action defName="Deposit" needsHands="true">
    <description>Placing item in storage</description>
  </Action>

  <Action defName="Place" needsHands="true">
    <description>Placing item at designated location</description>
  </Action>

  <!-- Social/Movement Actions -->
  <Action defName="Talk" needsHands="false">
    <description>Conversation with another colonist</description>
  </Action>

  <Action defName="Wander" needsHands="false">
    <description>Idle exploration</description>
  </Action>

  <Action defName="Flee" needsHands="false">
    <description>Running from danger</description>
  </Action>

  <!-- Combat Actions -->
  <Action defName="MeleeAttack" needsHands="true">
    <description>Melee combat</description>
  </Action>

  <Action defName="RangedAttack" needsHands="true">
    <description>Ranged combat</description>
  </Action>
</ActionTypes>
```

---

### Chain Interruption Logic (Config-Driven)

When a colonist is interrupted mid-chain:

```python
def handle_interruption(colonist, interrupting_action, carried_item):
    # Both values come from config
    action_needs_hands = ActionRegistry.get(interrupting_action).needsHands
    item_hands_required = AssetRegistry.get(carried_item).carryable.handsRequired

    if not action_needs_hands:
        # Keep carrying, do action (e.g., talking while holding stick)
        return KeepCarrying

    # Action needs hands - must handle carried item
    if item_hands_required == 0:
        # Pocket item, continue
        return KeepInPocket

    if item_hands_required == 1:
        if colonist.inventory.canStow(carried_item):
            colonist.inventory.stowToBackpack(carried_item)
            return StowedToBackpack
        else:
            drop_item(colonist.position, carried_item)
            return DroppedItem

    if item_hands_required == 2:
        # 2-hand items can never be stowed
        drop_item(colonist.position, carried_item)
        return DroppedItem
```

**Key Point:** The code reads `needsHands` and `handsRequired` from config. Changing which actions need hands or how heavy items are is pure data.

---

## Chain Structure

### Data Model

```cpp
struct ChainStep {
    uint8_t order;
    std::string actionDefName;  // References ActionTypes config
    std::string target;         // "source", "destination", etc.
    bool optional;
    bool requiresPreviousStep;
};
```

### Built-in Chains

**File:** `assets/config/work/task-chains.xml`
```xml
<?xml version="1.0" encoding="UTF-8"?>
<TaskChains>
  <Chain defName="Chain_PickupDeposit">
    <label>Haul Item</label>
    <description>Pick up a loose item and carry it to storage</description>
    <steps>
      <Step order="0" action="Pickup" target="source"/>
      <Step order="1" action="Deposit" target="destination" requiresPreviousStep="true"/>
    </steps>
  </Chain>

  <Chain defName="Chain_PlaceItem">
    <label>Place Item</label>
    <description>Carry an item to its designated placement location</description>
    <steps>
      <Step order="0" action="Pickup" target="item"/>
      <Step order="1" action="Place" target="placementLocation" requiresPreviousStep="true"/>
    </steps>
  </Chain>

  <Chain defName="Chain_Rescue">
    <label>Rescue</label>
    <description>Carry an incapacitated colonist to medical care</description>
    <steps>
      <Step order="0" action="Pickup" target="patient"/>
      <Step order="1" action="Deposit" target="medicalBed" requiresPreviousStep="true"/>
    </steps>
  </Chain>

  <Chain defName="Chain_CraftWithGather">
    <label>Craft with Gathering</label>
    <description>Gather materials, craft item, deposit output</description>
    <steps>
      <Step order="0" action="Gather" target="materialSource" optional="true"/>
      <Step order="1" action="Craft" target="station"/>
      <Step order="2" action="Deposit" target="outputStorage" optional="true"/>
    </steps>
  </Chain>
</TaskChains>
```

---

## Chain Bonus Mechanics

### Priority Bonus (+2000)

When colonist completes chain step N, step N+1 gets +2000 priority bonus. This is tunable in `priority-tuning.xml`:

```xml
<Bonuses>
  <ChainContinuation bonus="2000"/>
</Bonuses>
```

### Example: Bob Hauling Large Rock

| Task | Base | Chain | Distance | Final |
|------|------|-------|----------|-------|
| Deposit rock (step 1) | 1000 | +2000 | +45 | 3045 |
| Harvest bush | 3000 | 0 | +30 | 3030 |

Bob deposits rock first. Chain bonus wins.

### Example: Bob Critically Hungry

| Task | Base | Chain | Distance | Final |
|------|------|-------|----------|-------|
| Deposit rock (step 1) | 1000 | +2000 | +45 | 3045 |
| Eat (CRITICAL < 10%) | 30000 | 0 | +40 | 30040 |

Critical need wins. Bob drops rock (handsRequired=2), eats.

---

## Resuming Broken Chains

### Item Stowed

1. Item in backpack
2. Chain tasks remain in registry
3. Deposit still gets +2000 bonus
4. Colonist resumes naturally after interruption

### Item Dropped

1. Item spawns at colonist position
2. Original colonist's chain progress cleared
3. New pickup task generated (anyone can do it)
4. No ownership — first colonist to select it starts new chain

---

## Config Summary

| What | Where | Example |
|------|-------|---------|
| Item hands required | `assets/world/*/[item].xml` | `<carryable handsRequired="2"/>` |
| Action needs hands | `assets/config/actions/action-types.xml` | `<Action defName="Eat" needsHands="true"/>` |
| Chain definitions | `assets/config/work/task-chains.xml` | `<Chain defName="Chain_PickupDeposit">` |
| Chain bonus value | `assets/config/work/priority-tuning.xml` | `<ChainContinuation bonus="2000"/>` |

**All behavior is data-driven.** To change whether Sleep needs hands, edit XML. To add a new 3-hand item for giants, add XML. No code changes.

---

## Edge Cases

### Multiple Items (Batch Haul)

If colonist is hauling 5 sticks (1-hand each, batched):
- All items share one chain
- If interrupted, all items handled together
- Items that fit stow to backpack, excess dropped

### Storage Fills Mid-Haul

1. Deposit task becomes invalid
2. Chain bonus no longer applies (no valid next step)
3. Colonist keeps item, re-evaluates
4. May find different storage

### Colonist Dies

1. All carried items spawn at death location
2. Items become new haul tasks
3. Other colonists can pick up

---

## Related Documents

- [Task Registry System](./task-registry.md) — Where chain tasks are stored
- [Priority Config](./priority-config.md) — Chain bonus value
- [Entity Capabilities](../world/entity-capabilities.md) — Carryable capability
- [AI Behavior](./ai-behavior.md) — Task selection hierarchy
