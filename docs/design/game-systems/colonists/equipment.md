# Colonist Equipment & Inventory

Created: 2026-06-15
Status: Design

## Overview

How a colonist wears and carries things. There are three distinct mechanisms,
and the key design decision is that they are *not* the same: worn apparel sits in
fixed body slots, the belt holds a fixed number of quick-draw tool slots, and the
pack is weight-based bulk storage. An item's **hand class** (size) decides where
it is allowed to go.

This supersedes the hands-only model in the current engine (`Inventory` has just
left hand, right hand, and a backpack map). See [Relationship to current code](#relationship-to-current-code).

## Hand class (item size)

Every carryable item has a **hand class**, equal to the engine's `handsRequired`:

| Class | Value | Meaning |
|-------|-------|---------|
| Pocket | 0 | Tiny. Fits a pocket or the pack; never needs a hand. |
| One-hand | 1 | Held in one hand, slotted on a belt, or stowed in the pack. |
| Two-hand | 2 | Needs both hands. Cannot be stowed anywhere — it is "bulky." |

There is no separate "bulky" class: **two-hand already means bulky** (occupies
both hands, can't be stowed). The only thing beyond two-hand is *not carryable on
a person at all* — furniture, a boulder you can only drag/haul — which is a yes/no
property (the Carryable capability), not another size step.

## Worn equipment slots

One item per slot.

| Slot | Holds |
|------|-------|
| Head | Helmets, hats |
| Face | Masks, goggles, respirators |
| Body — Under | Base layer (shirt, thermal weave) |
| Body — Over | Outer layer (jacket, armor, duster) |
| Legs | Trousers, leg armor |
| Feet | Boots, shoes |
| Back | A **container** (backpack) — grants pack capacity |
| Belt | A **container** (tool belt) — grants belt slots |
| Hands ×2 | Held items (see below) |

Body is two layers (under + over) so light clothing and outer armor coexist.

## Carry locations and their rules

### Hands (2)

- Two one-hand items (one per hand), **or** one two-hand item occupying **both** hands.
- A two-hand item leaves no free hand and cannot be stowed.

### Belt — discrete slots (the exception)

The belt is **slot-based, not weight-based.** The equipped belt grants a fixed
number of quick-draw slots; each slot holds **exactly one one-hand item** (a tool
or sidearm).

- Accepts **one-hand items only.** Not pocket items (those go in the pack), not
  two-hand items.
- Slots are for fast access and for parking a tool to free a hand (see [Swapping](#swapping-to-free-hands)).
- No stacking — one item per slot.

### Pack — weight-based bulk (the default)

The pack (equipped in the Back slot) is **weight-based**, capped in kilograms, not
slot count.

- Holds pocket and one-hand items. **Never two-hand** items.
- Same-type items **stack** into one entry; a stack's weight is `qty × unit weight`.
- Capacity is kilograms; the number of distinct items is irrelevant.

### Placement matrix

| Item class | Hands | Belt slot | Pack |
|------------|:-----:|:---------:|:----:|
| Pocket (0) | ✓ | ✗ | ✓ |
| One-hand (1) | ✓ | ✓ | ✓ |
| Two-hand (2) | ✓ (both) | ✗ | ✗ |

"A huge rock can't go on a belt" falls out of this directly: a rock is two-hand,
and neither the belt nor the pack accepts two-hand items, so it lives in both hands
until set down or hauled.

## Weight & encumbrance

Inventory is weight-first. Carrying capacity is strength-derived; the pack has its
own kilogram cap on top of that. Belt tools and held items still count toward total
carried weight even though the belt is slotted rather than weighed. Over-encumbrance
slows movement (degree TBD — future tuning).

## Swapping to free hands

Hands are scarce. When a colonist needs to pick up a two-hand item but is already
holding a one-hand item, they first **stow the held item, then pick up.** Stow
preference order:

1. A free **belt slot** (if the held item is one-hand and a slot is open).
2. The **pack** (by weight, if it fits).
3. Otherwise the action is blocked, or the held item is dropped on the ground
   (player setting / context dependent).

Example: a colonist holding an axe is told to haul a two-hand slab. They holster the
axe on the belt, freeing both hands, then lift the slab. When they set the slab down,
the axe can be drawn back from the belt.

## UI representation

Designed in the React prototype (`docs/ui-prototype`), in-game HUD:

- **Full dossier (Gear tab):** a head-to-toe paper-doll of worn slots, the held
  item shown as a single "Both Hands" box when two-handed, a weight meter + stacked
  item list for the pack, and a row of discrete cells for the belt slots. Each item
  shows its hand class; containers state what they accept.
- **Quick panel (selected colonist):** the armed item with its class ("Breaker Bar ·
  2H · both hands"), a row of belt tools, a carry-weight meter, and the pack contents
  as compact chips.

## Relationship to current code

Implemented today (`libs/engine`):
- `handsRequired` on item assets (1 or 2; the pocket = 0 tier is documented in
  [Entity Capabilities](../world/entity-capabilities.md) but not yet used).
- `Inventory` with left hand, right hand, and a backpack map; two-hand items can't
  be stowed.
- Storage acceptance by `ItemCategory`, see [Storage System](../../features/storage-system.md).

New systems this spec introduces (not yet built):
- Worn equipment slots (head/face/body-under/over/legs/feet) and apparel.
- The belt as a fixed set of one-hand quick-draw slots.
- Weight-based pack capacity and total carry/encumbrance.
- The pocket (0) hand class actually gating placement.
- Hand-freeing swap logic when picking up two-hand items.

**MVP Status:** See [MVP Scope](../../mvp-scope.md) — post-MVP; the survival loop
does not require apparel or a belt yet.

## Related Documentation

- [Entity Capabilities](../world/entity-capabilities.md) — `handsRequired` / carryable
- [Storage System](../../features/storage-system.md) — container storage, hauling
- [Crafting](../world/crafting.md) — where gear comes from
