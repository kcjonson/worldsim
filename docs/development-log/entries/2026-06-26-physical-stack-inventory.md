# 2026-06-26 - Physical stack inventory

**Epic:** Specboard `0db82dab`
**Spec:** [/docs/technical/physical-stack-inventory.md](../../technical/physical-stack-inventory.md)

## Summary

A "stack" became a first-class physical unit across the whole game: one material, capped at that material's own `stackSize` (Wood 40, Berry 200, Stick 100), universal across hands, backpacks, shelves, boxes, build sites, and ground piles. There is never more than one stack's worth of a material in a single stack anywhere; more than that is more stacks, each taking a slot. Carry stays weight-or-count, whichever binds first.

This supersedes the reverted "honor item stackSize in `addItem`" chore (PR #219), which clamped a *container's total* at the item stackSize and stalled construction. This does it structurally instead.

## What shipped (5 PRs)

- **#224** — `Inventory.items` goes from `map<defName,total>` to a slot list (`vector<ItemStack>`, multiple stacks per type); container capacity becomes a slot count; the per-container `maxStackSize` field is removed; per-material stack sizes set (Berry 200, Stick 100). The eight accessors reimplemented (spill-on-add, drain-across, aggregate-read, slot accounting). Three-agent reviewed.
- **#228** — over-cap drops split + scatter: a pure `resourcePileDrops(cap, qty, origin)` returns `ceil(qty/cap)` stacks on a deterministic concentric-ring layout (>0.3 m apart, so piles never alias within the 0.25 m pickup epsilon); the GameScene drop callback wires it. Satisfies both the ground-pile split and the resource-placement utility.
- **#230** — construction sites hold required materials directly: build sites drop their `Inventory` entirely, `StructureBlueprint.delivered[]` is the authoritative tracker via `recordDelivery`; `reconcileDelivered` / `slotsForManifest` / `createForBuildSite` deleted (the DrawingSystem-knows-slots smell removed at its source). Demolish returns materials as loose piles — 100% on a not-yet-built cancel, `refundPercent` (50%) salvage on a built teardown — dropped at deconstruct-completion, not at the order tick.
- **#232** — storage hauls sized by the destination's `addableCount(item)` (real item capacity: stack headroom + free-slots × stackSize) instead of the free-slot count; construction/craft hauls (`blueprint.remaining`) untouched.
- **#233** — the two-hand armful clamped to `min(weight-fit, stackSize - held)` — the carry rule made real (no-op for wood, where weight binds at ~14 below the 40 cap, but correct for lighter materials).

## Key decisions

- Stacks are physical and universal (owner directive), so each item's `stackSize` is enforced where stacks physically live — the carry and the ground/storage representation — **not** as a container-total clamp (that was the reverted approach that stalled builds).
- A build site is a work order, not a container: no `Inventory`, materials tracked directly on the manifest.
- Demolish salvage drops at tear-down completion, so the loose piles can't appear before the structure is gone (no salvage-then-cancel concern, and demolish has no cancel path anyway).

## Deferred / follow-ups

- **Phase 6: per-stack container UI** (Specboard `88dc9e90`) — show a container's discrete stacks instead of "N slots." Display-only, outside the core acceptance; left as a tracked follow-up.
- DrawingSystem rename (`7d785fc6`) — parked until its interim render path is removed (a separate construction milestone).
- Nudging dropped piles onto walkable tiles, pile merging, per-container category-acceptance rules, and `SmallStone`/`PlantFiber` stackSize tuning — all spec-level deferrals.
