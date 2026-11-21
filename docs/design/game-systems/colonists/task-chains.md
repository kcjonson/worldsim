# Task Chains

**Status:** Design
**Created:** 2025-01-02
**MVP Status:** Phase 2+ (after basic hauling works)

Every multi-step thing a creature does (eating, hauling, a cut and its carry-in, a placement, a craft) is a chain of steps defined in data, and the mechanism and the chain format that replaces `task-chains.xml` are specified in [Queries and chains](../../../technical/queries-and-chains.md). This document covers the hands rules and how a chain keeps together across its legs. The original chain structure is in the [Historical Addendum](#historical-addendum).

---

## Overview

Some tasks require multiple steps that should stay together. A colonist who picks up an item should carry it to storage, not drop it halfway to eat a berry.

**Task Chains** keep related steps on one task. A haul's pickup becomes its deposit leg, and a landed cut becomes its own delivery, on the same `Task`, so the next step is never a separate choice. Steps that serve an active work order are classified on the `committed_work` rung ([Colonist Task Arbitration](../../../technical/colonist-task-arbitration.md)), which keeps the colonist on them; there is no shared chain id and no chain score bonus.

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

Each action is an entry in `assets/config/actions/`, and its `hands` says whether it needs free hands
([Queries and chains](../../../technical/queries-and-chains.md#actions)):

```xml
<actions>
  <action id="Eat" seconds="2" hands="free" interruptable="false">
    <consume count="1"/>
    <restore need="Hunger" from="nutrition" scale="100"/>
    <restore need="Digestion" from="nutrition" scale="-100"/>
  </action>

  <action id="Sleep" seconds="8" hands="any" interruptable="true" pays="along" curve="2">
    <restore need="Energy" amount="60" scaleBy="sleep_quality"/>
  </action>
</actions>
```

`hands="free"` means the hands must be free before the action starts; `hands="any"`, the default, means
the creature keeps carrying. A delivery (Deposit) keeps its goods in hand, since they are what it
delivers.

---

### Chain Interruption Logic (Config-Driven)

When a colonist is interrupted mid-chain:

```python
def handle_interruption(colonist, interrupting_action, carried_item):
    # Both values come from config
    action_needs_hands = Actions.get(interrupting_action).hands == "free"
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

**Key Point:** The code reads an action's `hands` and an item's `handsRequired` from data. Changing which actions need hands or how heavy items are is pure data.

---

## Keeping a chain together

There is no chain bonus, and `priority-tuning.xml` holds no chain value. Two things keep a colonist on its chain:

- The next step is the same task. When a pickup completes the chain moves on to its deposit leg, and when a cut lands to its carry-in, in place, so arbitration sees the task it is already doing, with its hysteresis margin.
- Provisioning that serves an active work order (a craft, a build site) is at the work-order tier. Ordinary work and an ordinary need never interrupt it; a critical need does, and an urgent need does between actions ([Interruptable crafts](../../../technical/interruptable-crafts.md#what-interrupts-what)).

### Example: Bob hauling wood to a build site

His deposit leg is at the work-order tier. A hungry Bob (Hunger at 40%, an ordinary need) finishes the delivery first, and so does a very hungry one, since only a critical need drops an armful its root still wants. A critically hungry Bob (below 10%) switches: a two-hand armful can't be stowed, so it goes into a box within reach that takes it, else onto the ground as a pile, and he eats.

---

## Resuming Broken Chains

### Item Stowed

1. Item on belt or in backpack
2. If a need interrupted the work, the work, with its inbound and in-flight claims for the item, is
   kept as the task's suspended record ([Task Claims](../../../technical/task-claims.md))
3. After the need, the colonist's carry-in option, or a further cut for the same goal and item,
   adopts the record and keeps its claims
4. While the colonist sleeps the record holds nothing for anyone else, so another colonist may fetch
   or cut what those goods would have covered; it holds again once the colonist wakes
5. Choosing any other work discards the suspended record

### Item Dropped

1. Item goes into a box within reach (1.5 m, no wall between) that takes it, else spawns at the
   colonist's position as a loose pile
2. Original colonist's chain progress and claims for it end
3. The pile is an ordinary source with no claims (anyone can haul it)
4. No ownership: first colonist to select it starts new chain

---

## Config Summary

| What | Where | Example |
|------|-------|---------|
| Item hands required | `assets/world/*/[item].xml` | `<carryable handsRequired="2"/>` |
| Action needs hands | `assets/config/actions/` | `<action id="Eat" hands="free">` |
| Chain definitions | [Queries and chains](../../../technical/queries-and-chains.md) | `<chain id="eat_a_meal">` |

**All behavior is data-driven.** To change whether Sleep needs hands, edit XML. To add a new 3-hand item for giants, add XML. No code changes.

---

## Edge Cases

### Multiple Items (Batch Haul)

If colonist is hauling 5 sticks (1-hand each, batched):
- All items share one chain
- If interrupted, all items handled together
- Items that fit stow to backpack, excess dropped

### Storage Fills Mid-Haul

1. The deposit takes what still fits; the colonist keeps the rest, which holds no claim
2. The colonist re-evaluates; a carry-in to any box that wants the item takes it there

### Several Colonists Bound for One Point

Two carriers delivering to one station, or three thirsty colonists at one shore point, push one
another apart, so no more than one of them could stand on the point itself. Each acts once it is
within 0.4 m of its task's target (`ActionSystem`), so they deposit or drink side by side.

### Colonist Dies (planned; colonist death does not exist yet)

1. All carried items spawn at death location
2. Items become new haul tasks
3. Other colonists can pick up

---

## Related Documents

- [Task Registry System](./task-registry.md) — Where chain tasks are stored
- [Priority Config](./priority-config.md): priority tuning
- [Entity Capabilities](../world/entity-capabilities.md) — Carryable capability
- [AI Behavior](./ai-behavior.md) — Task selection hierarchy
- [Queries and chains](../../../technical/queries-and-chains.md): the generic step chains and target queries

---

## Historical Addendum

The chain structure below, from 2026-01-02, is superseded by [Queries and chains](../../../technical/queries-and-chains.md) (2026-11-17). `assets/config/work/task-chains.xml` held these chains and was loaded and validated, and no simulation code read it; the haul and placement chains that ran were written by hand in C++. On 2026-11-17 the owner retired that hand-written code in favour of chains defined in data, and `task-chains.xml` goes with it. Kept for reference.

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
