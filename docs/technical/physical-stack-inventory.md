# Physical Stack Inventory

**Status:** spec / ready to implement
**Supersedes:** the reverted "honor item stackSize in Inventory.addItem" chore (PR #219), which capped a *container's* total at the item stackSize and stalled construction. This does it as a structural model change instead.

## Goal

A **stack** is a first-class physical unit: one material, quantity in `[1, material.stackSize]`. The constraint is universal — hands, backpacks, shelves, boxes, build sites, ground piles. There is never more than one stack's worth of a material in a single stack *anywhere*; more than that is more stacks, each taking a slot. What a colonist can carry of one material is the smaller of carry-weight and what fits in their hands/slots — weight or count, whichever comes first.

## Why it's structural

Today `Inventory.items` is `unordered_map<defName, quantity>` — exactly one total per material — and a single per-container `maxStackSize` clamps that total. The item's own stackSize is parsed but unenforced. That model literally cannot represent "5 stacks of 40 wood in a box," and the naive enforcement (clamp the total at 40) breaks build sites, which hold a whole manifest in one oversized slot (`maxStackSize = 100000`). The map has to become slot-based.

## Stack sizes (per material, from the item def)

| Material | stackSize | mass (kg) | two-hand |
|---|---|---|---|
| Wood | 40 | 2.5 | yes |
| Berry | 200 | 0.15 | no |
| Stick | 100 | 0.5 | no |
| SmallStone | 10 | 1.5 | no |
| PlantFiber | 20 | 0.1 | no |
| AxePrimitive | 1 (tool) | 1.5 | no |

Parser/struct default is 1 (unstackable). Every haulable item must declare a stackSize; SmallStone/PlantFiber are current values, open to tuning.

## Model

### Container = slots
A container holds N slots; each slot is one stack; multiple slots may hold the same material. Capacity is a **slot count**, not a type count. The per-container `maxStackSize` field is removed — the item's stackSize is the only per-stack cap.

| Container | slots | notes |
|---|---|---|
| Colonist backpack | 10 | weight (`carryCapacityKg`) usually binds first |
| BasicShelf | 20 | accepts Tool category |
| BasicBox | 50 | accepts RawMaterial; up to 50 stacks (e.g. 2000 wood) |
| Build-site delivery | sized from manifest | `sum_i ceil(required_i / stackSize_i)` + headroom; retires the `100000` hack |

### Carry (hands)
A two-hand armful is one stack of `min(weight-fit, stackSize)`. For wood, weight binds (~14 at 35 kg / 2.5 kg). A one-hand material in the backpack is one stack per slot (≤ stackSize); total carried is the sum across its slots, weight-bounded.

### Ground piles
A loose pile (`ResourceStack` entity) is one stack ≤ stackSize. A drop whose quantity exceeds stackSize spawns `ceil(qty / stackSize)` pile entities, each ≤ stackSize, on nearby free tiles. (Does not trigger for wood today — fell remainders are ≤ 16 — but the model requires it.)

## Data model changes

### `Inventory.items`: map → slot list
Becomes an ordered slot collection (`std::vector<ItemStack>`), each entry one stack. Reimplement, keeping every public contract:
- `addItem(defName, qty)` — top up existing non-full stacks of `defName` to stackSize, then open new slots until capacity; return amount added.
- `getQuantity(defName)` / `hasQuantity` — sum across all stacks of the type.
- `removeItem(defName, qty)` — drain across stacks, erase emptied slots.
- `canAdd` / `addableCount` — sum stack headroom + free-slot capacity.
- `hasSpace` / `getSlotCount` — slot-based (used slots vs `maxCapacity`-as-slots).
- `maxCapacity` is the slot count; remove `maxStackSize`.
- `getAllItems()` for display aggregates per type (show "Wood x46", not two rows).

### `StructureBlueprint.delivered/required`
Stay as totals. **Superseded:** a later refactor made `delivered[]` the authoritative tracker (`StructureBlueprint::recordDelivery`); build sites carry no Inventory and `reconcileDelivered` is gone. `materialsComplete()` unchanged.

### Build-site delivery inventory
**Superseded:** build sites no longer carry a delivery Inventory. `applyDepositEffect` records straight onto `delivered[]` via `recordDelivery` (capped at the requirement; the surplus bounces back to the colonist's hands); `slotsForManifest`/`createForBuildSite` were deleted.

### Ground-pile drop
`GameScene` drop-resource callback splits a `> stackSize` quantity into multiple `ResourceStack` entities on nearby tiles.

## Blast radius (from code research)

**Must change:**
- `Inventory.h` — the eight methods that touch `items`.
- `StorageGoalSystem.cpp:51` — `items.size()` as used-slots → the new used-slots accessor (this one silently mis-counts otherwise).
- `CraftingAdapter.cpp:37` — iterates raw `.items` → iterate the new structure.
- `InventoryMass.h` `cargoUnitsThatFit` — the `maxStackSize` clamp → item stackSize (weight is the real bound anyway).
- `DrawingSystem.cpp` ×3 + `DevCommandHandler` — build-site delivery sizing.
- `GameScene` drop-resource callback — pile split.
- Tests: `Inventory.test.cpp`, `ActionSystem.test.cpp` (armful/storage-overflow), `ConstructionSystem.test.cpp`, `AIDecisionSystem.test.cpp`, `InventoryMass.test.cpp` (raw `.items["Stone"]` write).

**Safe** (route through `getQuantity` / `availableQuantity` / `addItem` / `removeItem` / `addableCount`, so they hold once those aggregate across stacks): HarvestActions, HaulActions, CraftActions, NeedActions, AIDecisionSystem, ConstructionSystem `carriedAmount`, UI display via `getAllItems`. The two-hand hand-mirror machinery in `InventoryMass.h` is independent of `items` and stays untouched.

## Implementation phases

0. Inventory slot model + accessor reimplementation, behavior-preserving for single-stack cases; full unit coverage (spill-on-add, drain-across, aggregate-read, slot accounting).
1. Container capacity as slots (colonist/shelf/box); remove `maxStackSize`; update `StorageGoalSystem`, `CraftingAdapter`, `cargoUnitsThatFit`.
2. Build-site delivery sized from manifest; verify a large foundation completes (the regression the chore caused).
3. Ground-pile cap + multi-pile drop split.
4. Hand armful stackSize clamp (no-op for wood; correctness for light/future materials).
5. Per-material stackSize values into the asset XML (Berry 200, Stick 100, the rest).
6. UI display aggregation; migrate tests.

## Acceptance

- No stack of any material exceeds its stackSize anywhere — hands, pack, shelf, box, ground.
- A box holds multiple stacks (50-slot box → up to 50 wood stacks); a colonist's wood armful is `min(weight, 40)` (~14 today).
- A 200-wood foundation accumulates wood across delivery slots and **completes** — no AwaitingMaterials stall.
- Felling a yield > one stack drops multiple piles, each ≤ stackSize.
- Deposit spills wood across stacks; haul / craft / build read totals correctly through `getQuantity`.
- engine-tests green; the full carry → fell → pile → haul → store → craft loop runs end to end.

## Deferred / open

- Pile placement rule for overflow drops (nearby free tiles; pile *merging* stays deferred).
- Per-container category acceptance + slot reservation (out of scope).
- Per-stack display inside containers (today the panel shows "N slots"); display-only, later.
- Final stackSize tuning for SmallStone / PlantFiber and any new materials.
