# Crafting & Material Provisioning — Technical Architecture

**Status:** Design (single-colonist provisioning implemented 2026-06-28, PR #240)
**Related:** [Task Generation Architecture](./task-generation-architecture.md),
[Building & Construction Architecture](./building-construction-architecture.md),
[Physical Stack Inventory](./physical-stack-inventory.md),
[Multi-colonist crafting (deferred spec)](../design/multi-colonist-crafting.md)

How a queued craft gets its materials and finishes. The organizing idea: **crafting works like
construction.** A crafting station provisions a bill of materials exactly the way a build site
provisions its structure manifest, so the deposit, metering, and no-leftover rules are one shared
mechanism, not a craft-specific fork.

---

## 1. Craft-as-construction: the station store

A crafting station holds its own materials. A `CraftingSpot` spawns with an `Inventory` store
alongside its `WorkQueue`, the way a build site holds its delivered manifest; `PlacementSystem`
attaches the store. Colonists haul each recipe input INTO the station, and the Craft action
consumes the inputs FROM the station store. This replaced the older model where staged materials
stayed in the hauler's pack and the craft consumed them from the colonist's inventory.

- The deposit effect's craft-station branch moves carried material from the colonist into the
  station store (the pack empties), mirroring the build-site branch, and credits the parent Craft
  goal by what physically landed.
- `applyCraftingEffect` / `startCraftAction` consume and validate against the station store, not
  the colonist. The Craft option in the AI gates on the station holding every input.
- `CraftingGoalSystem` is store-aware: materials already in the store count as delivered (only the
  shortfall is provisioned), and an input a colonist already carries resolves to a Haul (deliver
  it) rather than a fresh Harvest.

There is exactly one provisioning path. The keep-in-pack deposit path and its idempotent-credit
bookkeeping were deleted (One Path Rule), as was the legacy `TaskType::Gather` craft-provisioning
path and the dead harvest-to-haul dependency mechanism (`WaitingForItems` / `dependsOnGoalId` /
`notifyGoalCompleted`). Food gathering (`FulfillNeed`) is a separate path and untouched.

## 2. The metered-deposit / no-leftover invariant

**A crafting station NEVER holds more than the current recipe needs.** The colonist deposits only
what the recipe requires, exactly, never more; surplus stays on him. Deposits are **metered to the
recipe's remaining need** — the crafting analogue of construction's
`StructureBlueprint::remaining()`. A colonist carrying a pile of 7 with a recipe need of 2 deposits
2 into the station and keeps 5. The station store accumulates up to exactly the recipe's bill of
materials, then the craft consumes it.

This is ONE shared mechanism with construction. Crafting mirrors construction's
`StructureBlueprint.delivered[]` / `recordDelivery` / `materialsComplete()` model, so the
metered-deposit and no-leftover logic is not duplicated.

**Do NOT build handling for "extra/leftover in a station"** (return-excess, station overflow,
storing or draining surplus). That case does not exist by design. (Deconstruct or cancel mid-craft
still has the in-progress BOM to drop, but that is the current recipe's materials, not leftover.)

## 3. Pickup quantity differs by source (intended)

The amount a colonist takes depends on where the material comes from:

- **Harvest (cut/gather):** take the whole yield into inventory up to carry capacity; overflow
  drops as a pile. Take it all even if it exceeds the craft's need, so freshly cut resources aren't
  stranded at a far harvest spot; the surplus rides along and is kept.
- **Pickup from a ground pile or storage:** take EXACTLY the remaining need, leaving the rest in
  the pile (it's already at a known reachable spot). Pile of 7, need 2, take 2, leave 5.
- **Deposit into a station or build site:** metered to the recipe (section 2).

So: harvest = take all you can carry; pile/storage pickup = take only what's needed.

A carry gate guards both harvest and craft-fetch: skip emitting the option when no unit of the
yield fits (no carry weight and no stack/slot headroom). Without it an over-weight or full colonist
picks the option, collects 0 (the take clamps to carry weight and slots), re-evaluates, picks the
same option, and loops — the "stuck harvesting beside a tree" and "infinite hauling" symptoms.

## 4. The `giveItemToColonist` cascade

Crafted output (and, by intent, every item handed to a colonist) routes through a canonical
`giveItemToColonist` in `libs/engine/ecs/InventoryMass.h`: empty hand, then belt slot, then
backpack, then ground drop, weight-respecting at every step. Overflow drops a loose pile at the
colonist. This replaced a path that force-added output to the backpack, skipping hands and belt and
ignoring carry weight. The ground-drop leg is the natural consumer of `findValidPositionNear` (see
the nav doc) once that lands. Harvest-yield and pile-pickup add sites should be consolidated onto
this one cascade (open follow-up).

## 5. Provisioning model (goal-driven, availability-resolved)

Provisioning is the goal path's job; the global task list shows what's actually obtainable, not a
speculative chain. Each recipe input resolves against colony knowledge (any colonist's memory):

- **known stock** to a **Haul**,
- **known harvestable source** to a **Harvest**,
- **neither** to a `NoSource` "waiting (none found)" need that re-resolves to Cut/Haul once a
  colonist discovers a source (`GoalStatus::NoSource`).

**Lazy haul:** a harvestable input creates only a Harvest goal; the haul that carries cut material
to the station is created when the harvest completes (`createHaulForCompletedHarvest`), so no haul
row appears before there is anything to haul. **Fetch from stock:** a craft haul can bring a
remembered loose pile to the station (two-phase pickup then deliver-to-station) instead of
re-harvesting.

### Reliability rules (AI arbitration)

Single-colonist crafting was made reliable by fixing the blockers that let a queued axe stall:

- **`colonyCarriesStock` is scoped to colonist inventories.** It viewed every `Inventory`, so
  leftover material in a *different* station's store counted as "carried" and the input resolved to
  a fetch Haul that could never source it, stranding a second craft. Scope it to entities with a
  `Colonist` tag (the only deliverable carriers).
- **Provisioning that serves an active work order floors above idle Wander.** A craft-provisioning
  Haul/Harvest priority collapsed below Wander once its source was far (distance penalty), so a
  colonist abandoned a half-provisioned craft and wandered. A `servesActiveWorkOrder` flag plus a
  priority floor keeps provisioning above Wander.
- **Available craft Hauls re-resolve, not just `NoSource` ones.** A fetch Haul born `Available`
  from known loose stock that was then consumed/forgotten produced no AI option and stranded the
  craft. Re-resolve `Available` craft Hauls too: when no stock is fetchable but a harvestable
  source is known, swap to a Harvest.
- **Haul phase is carry-state-first, not position-first.** `startHaulAction` chose Pickup vs
  Deposit by position (`atSource` / `atTarget`). When a loose pile sits within tolerance of the
  station, the colonist is `atSource` AND `atTarget` at once, skips Pickup, runs an empty Deposit,
  and the AI re-emits the fetch forever. Pick up until actually carrying; deposit only once
  carrying. Position still decides *where*.

## 6. Open follow-ups

- **Prefer-nearest-source provisioning** (efficiency, not correctness): with natural ground-scatter
  everywhere, a craft prefers fetching far scattered stock over cutting an adjacent source.
  Completion is guaranteed by the priority floor plus re-resolve; the round-trips are just long.
- **Consolidate all item-add sites onto `giveItemToColonist`** (harvest-yield and pickup still add
  directly).
- **Multi-colonist crafting** is a deferred spec ([multi-colonist-crafting.md](../design/multi-colonist-crafting.md));
  single-colonist is the current scope. The station-store model with metered deposits is the
  substrate it builds on, and provisioning across colonists raises claim/reservation questions that
  spec resolves.

## 7. Related Documents

- [Task Generation Architecture](./task-generation-architecture.md) — goal-driven task existence
  vs UI vs colonist-selection priority; provisioning lives on the goal path described there.
- [Building & Construction Architecture](./building-construction-architecture.md) — the
  `StructureBlueprint.delivered[]` / `recordDelivery` / `remaining()` model crafting mirrors.
- [Physical Stack Inventory](./physical-stack-inventory.md) — slot-based inventory, stack sizes,
  ground piles, and the build-site delivery model that the station store reuses.
- [Pathfinding & Navigation](./pathfinding-architecture.md) — `findValidPositionNear`, the ground
  drop primitive the cascade's drop leg uses.
- [Multi-colonist crafting](../design/multi-colonist-crafting.md) — the deferred coordination spec.
