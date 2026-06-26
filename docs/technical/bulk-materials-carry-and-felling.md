# Bulk Material Carry & Tree Felling

Status: Spec (planning)
Created: 2026-06-23

Implementation spec for an epic. The *gameplay model* it implements already exists in
[Colonist Equipment & Inventory](../design/game-systems/colonists/equipment.md) and
[Colonist Attributes](../design/game-systems/colonists/attributes.md); this doc scopes the
slice we build now and the concrete engine changes. It revises the prior carry loop (dev log
`2026-06-20-carry-weight-axe-gated-harvest.md`) and the harvest-durability change (PR #216).

**MVP Status:** See [MVP Scope](../design/mvp-scope.md). The full equipment system
(equipment.md: worn apparel slots, encumbrance) stays post-MVP; this epic implements only the
bulk-carry + felling slice the wood loop needs.

## Summary

Bulk materials (wood first) become **two-hand "bulky" items** carried as a **weight-limited
armful in the hands**, never in the backpack. How much a colonist carries is derived from a new
minimal **Strength** attribute. Felling a tree is now **one action that destroys it**: the
colonist takes an armful and the remainder drops as a **loose ground stack** that gets hauled in
further armfuls. The axe (a one-hand tool) is **stowed on fell completion** (belt → pack → drop)
to free both hands for the wood. The harvest → haul → craft chain, which moves wood through the
backpack today, becomes **hand-aware** for two-hand materials.

This replaces the current model where wood is a backpack stack chipped from a renewable pool and
the tree only vanishes once fully drained.

## Why

- A felled tree should *disappear*; today `destructive="false"` is a dead flag for pooled
  sources and a partly-harvested tree lingers (the pool drains over trips).
- Wood is a bulk material you carry in your arms, not pocket in a backpack. Carry should be
  governed by slots + weight + strength, not an arbitrary `carryable quantity` (currently `1`,
  which is meaningless under the slots+weight model).
- The model already exists on paper (equipment.md / attributes.md); the engine just hasn't
  caught up.

## Scope

**In this epic**

- A minimal **Strength** attribute on colonists, rolled at generation, driving hand-carry kg.
- **Two-hand bulk material** as a general item trait (`handsRequired=2`), applied to **Wood**
  first (stack 40, 2.5 kg/log). Generalizes to stone/ore later with no new mechanic.
- **Hand-carried weighted stacks** (an "armful": a hand stack of quantity > 1, capped by
  weight), since hands today only ever hold quantity 1.
- A minimal **belt** slot for one-hand tools + **stow-to-free-hands** (belt → pack → drop) so the
  axe gets out of the way when picking up wood.
- **One-shot tree felling**: trees become single-shot destructive; the action fells the tree,
  the colonist takes an armful, the remainder drops as a **loose ground stack** (per-entity
  quantity), tree removed.
- **Hand-aware harvest / haul / craft**: harvest deposits wood into hands; haul recognizes
  hand-held wood and loose piles and moves them in weight-limited armfuls; craft input reads and
  consumes wood from hands; deposit moves from hands to storage.
- HUD: surface hands (armful), the belt tool, and the pack per equipment.md's quick panel /
  Gear-tab intent (reuse, don't redesign).

**Deferred (not this epic)**

- Worn apparel slots (head/face/body/legs/feet) and the rest of equipment.md.
- Encumbrance / over-weight movement penalty (note the hook, don't tune it).
- Tool **acquisition** (colonists still don't fetch/equip tools on their own; they start with an
  axe — see Open Questions). Stowing/dropping an owned axe is in scope; finding one is not.
- Loose-pile **merging** of adjacent stacks.
- Strength affecting anything other than carry (melee, labor speed) — attributes.md lists those;
  out of scope here.

## Design decisions (locked)

- **Strength lives in this epic**, minimal: a rolled-at-generation attribute that drives carry
  only, extensible later (per attributes.md it will also feed melee/labor).
- **Axe handling on fell:** when the chop completes, stow the axe to a **belt slot if free, else
  the pack** (by weight), **else drop it** — the wood is the goal, so a homeless axe is dropped
  rather than blocking the haul.
- **General bulk-material trait**, not wood-special-casing. Any `handsRequired >= 2` carryable
  material follows the same rules.
- **Stack size is item-driven** (Wood = 40), independent of a container's own stack cap.
- **Loose remainder is one ground entity carrying a variable quantity** (a per-entity stack
  count), not N unit entities — cleaner and avoids entity bloat.

## Model (what the player gets)

Carrying surfaces, per [equipment.md](../design/game-systems/colonists/equipment.md):

- **Hands (×2):** two one-hand items, or one two-hand item in both hands. A two-hand armful of
  wood fills both hands and cannot be stowed to belt or pack.
- **Belt:** a small fixed set of quick-draw slots, **one-hand items only** (the axe). For
  parking a tool to free the hands.
- **Pack:** weight-based (kg), holds pocket/one-hand items, **never two-hand**. Wood never goes
  here.

Strength (attributes.md) sets how heavy an armful can be. A colonist can carry well under a full
40-stack of wood (40 × 2.5 = 100 kg), so a felled tree of 20-30 wood always leaves a remainder.

Felling: chop (axe in hand, work = the tree's `durability` / skill-scaled rate from PR #216) →
on completion the tree is destroyed, the axe is stowed (belt → pack → drop), the colonist lifts
an armful (weight-limited), and the rest drops as a loose stack at the stump. Colonists then haul
the loose stack to storage in armful-sized trips. Crafting consumes wood straight from the hands.

## Implementation plan

Grouped by area; each group has acceptance notes. Sequence is roughly top-down (foundations
first). Not code — engine touch-points are named so the work is concrete.

### 1. Item config: two-hand bulk materials

- Wood (`assets/world/misc/Wood/Wood.xml`): `handsRequired=2`, item `stackSize=40`, keep
  `mass=2.5`. Remove the `<carryable quantity="1"/>` flat quantity; loose piles carry their own
  count (see §6).
- Done: `addItem`/stack logic caps each stack at the **item's** `stackSize` (40); the per-container
  `maxStackSize` is gone. Stacks of a bulk material cap at 40 in storage and on the ground.
- Acceptance: Wood reports as two-hand; cannot be added to a backpack; a storage stack of wood
  caps at 40.

### 2. Strength attribute → carry capacity

- New component (e.g. `Attributes`/`Physique`) holding `strength` (scale TBD, see Open
  Questions). Rolled at colonist generation (`GameScene::spawnColonist`, where skills are rolled
  today).
- `Inventory::carryCapacityKg` becomes **derived from strength** instead of the hardcoded 35.
  Keep a sensible default for non-colonist carriers (pack animal, cart, storage) unchanged.
- Acceptance: two colonists with different strength carry different armful sizes; an
  average-strength colonist lands near today's 35 kg so existing balance holds.

### 3. Hand-carried weighted stacks (the armful)

- Support a hand `ItemStack` with **quantity > 1** for two-hand bulk items (the data already has
  the field; nothing fills it past 1). The armful quantity is weight-limited:
  `floor(carryCapacityKg_remaining / unitMassKg)`, also bounded by the source amount.
- `carriedCargoMassKg` already sums hand items (and de-dups the two-hand mirror) — confirm it
  weighs a quantity-N armful correctly.
- Acceptance: a colonist lifts an armful whose size tracks strength/weight; total carried mass is
  correct; the armful blocks a second two-hand pickup but coexists with pre-existing pack items.

### 4. Belt slot + stow-to-free-hands

- Minimal **belt** container (one or a few one-hand tool slots). Not the full worn-slot system.
- **Stow order** when hands are needed for a two-hand pickup and a one-hand tool is held: belt
  slot (if free) → pack (by weight) → drop on ground. Drives the axe-on-fell behavior.
- Tools remain `ItemCategory::Tool` (excluded from cargo weight) and remain valid for the harvest
  tool check whether held, belted, or packed (`inventoryHoldsToolType` already scans hands +
  pack; extend to the belt).
- Acceptance: felling with an axe held → axe ends up belted (or packed, or dropped) and both
  hands carry wood; the colonist can still chop the next tree (tool found on belt/pack); a
  dropped axe is left at the stump.

### 5. One-shot tree felling

- Tree XMLs (Oak/Maple/Pine/Palm): `destructive="true"`, **remove** the `totalResource*` pool,
  set the yield (`amountMin/Max`) to the whole-tree wood (keep current pool totals, e.g. oak
  20-30), and set `durability` to the whole-fell work (≈ the old multi-chop total; ~3× the
  current per-chop value so a fell stays ~15 s at base rate — see PR #216).
- Harvest completion (`ActionSystem::completeAction`, collection branch): trees route through the
  single-shot path; take the weight-limited armful into hands, **drop the remainder** as a loose
  stack (§6), then destroy via `m_onRemoveEntity`. The pool branch (`m_onDecrementResource`) is
  no longer used by trees; keep it only if another pooled source remains, else remove it (no dead
  paths).
- Acceptance: one harvest action destroys the tree; the colonist carries an armful; leftover wood
  sits at the stump as a haulable stack; no wood is lost.

### 6. Loose ground stacks (per-entity quantity)

- A dropped pile is **one ground entity** with a per-entity **stack count** (new small
  component), spawned via a new ActionSystem callback (e.g. `m_onDropResource(defName, x, y,
  quantity)`) wired in `GameScene` to `m_placementSystem->spawnEntity` **without** `Packaged`
  (loose, not player-placed — unlike the crafting/deconstruct drops).
- Pickup/haul read the **per-entity count**, not a flat `carryable.quantity`, and lift a
  weight-limited armful, decrementing the pile (pile entity removed at 0).
- Vision/memory already discover carryable world entities; confirm a loose stack is discovered
  and becomes a haul candidate.
- Acceptance: a 16-wood remainder is one pile; a colonist hauls it in armful-sized trips until
  it's gone; the pile is removed when empty.

### 7. Hand-aware haul / craft / deposit

- **Haul** (`AIDecisionSystem` evaluate + `ActionSystem::startHaulAction`): recognize hand-held
  wood and loose piles as sources; size pickups by weight (armful), not `carryable.quantity`.
- **Craft input** (`startCraftAction` check + `completeAction` consume): read/remove two-hand
  materials from the **hands** as well as the pack (`hasQuantity`/`removeItem` are backpack-only
  today).
- **Deposit** (`completeAction` deposit branch): move a hand-carried armful into storage, capping
  the destination stack at the item's stack size (40).
- Acceptance: the full wood loop runs end-to-end with wood in hands: harvest → haul loose stack →
  deposit to storage → craft consumes it; nothing assumes wood is in the backpack.

### 8. HUD

- Reflect the carry surfaces per equipment.md's quick panel / Gear tab: the armful (both-hands
  box), the belt tool, and the pack. Reuse existing components; this is display only.
- Acceptance: a wood-carrying colonist reads as holding an armful in both hands with the axe on
  the belt.

## Acceptance criteria (epic-level)

- Harvesting a tree **destroys it in one action**; the colonist walks away with an armful and the
  remainder is a haulable ground stack — no wood lost, no lingering stump.
- Wood is two-hand and weight-stacked: it is never in the backpack; armful size tracks strength
  and per-unit weight; a full 40-stack is more than any colonist carries at once.
- The axe is stowed (belt → pack → drop) on fell and the colonist can keep chopping.
- The harvest → haul → craft chain runs entirely with hand-carried wood.
- Engine tests green; no dead pool-harvest path left if trees were its only user.

## Open questions / tuning

- **Strength scale + formula.** Scale (0-20 like skills? 0-100?) and `strength → carryCapacityKg`
  curve, centered so an average colonist ≈ today's 35 kg. Range (weakest → strongest kg)?
- **Belt size.** How many tool slots (1 is enough for the axe; equipment.md implies a small
  fixed set)?
- **Tool acquisition.** Colonists don't fetch tools yet; do they **start** with an axe at
  generation, or is acquisition a separate epic? (Stowing/dropping an owned axe is in scope.)
- **Encumbrance.** equipment.md notes over-weight slows movement; do we add the hook now (no
  penalty) or defer entirely?
- **Felling time.** Confirm a one-shot fell at ~15 s base is the target (vs. the prior multi-chop
  feel).

## Dependencies & related docs

- [Colonist Equipment & Inventory](../design/game-systems/colonists/equipment.md) — the carry
  model (hands/belt/pack, hand class, stow order). Source of truth.
- [Colonist Attributes](../design/game-systems/colonists/attributes.md) — Strength and the other
  physical attributes.
- [Entity Capabilities](../design/game-systems/world/entity-capabilities.md) — `handsRequired`,
  carryable, harvestable.
- [Crafting](../design/game-systems/world/crafting.md) and
  [Storage System](../design/features/storage-system.md) — the consume/deposit ends of the chain.
- Dev log `2026-06-20-carry-weight-axe-gated-harvest.md` — the carry loop this revises.
- PR #216 — harvest durability + Harvesting-skill work rate (the felling-time basis).
