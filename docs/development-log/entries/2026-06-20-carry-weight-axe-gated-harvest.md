# Carry weight, axe-gated chopping, wood-unit tree pools

Date: 2026-06-20

## Summary

Made the chop → haul → build loop realistic. A blueprint declares wood demand; a
colonist (holding an axe the player gave it) finds a tree from memory, chops up to its
carry weight, hauls the load to the site, repeats until the manifest is met, then builds.
Three mechanics landed on top of the existing goal-driven construction loop:

1. Mass-based carry limits. Items carry a per-unit mass; a colonist has a max carry
   weight (35 kg). A harvest fills to the weight cap, then the colonist hauls and returns.
2. Axe-gated chopping. Trees require the colonist to already hold an axe. Colonists never
   fetch or equip tools on their own (core gameplay rule), so a tool-less colonist simply
   never sees a tree-harvest as an option.
3. Wood-unit tree pools. A tree's resource pool is now wood units, withdrawn by the amount
   actually harvested (conserved), instead of a per-chop counter.

## Mechanics

- Item mass: `ItemProperties.massKg`, parsed from `<item><mass>`. Wood 2.5, Stick 0.5,
  SmallStone 1.5, PlantFiber 0.1, Berry 0.15, AxePrimitive 1.5 kg.
- Carry cap: `Inventory.carryCapacityKg` (colonist 35, pack animal 120, cart 500, storage
  effectively unbounded). Tools (`ItemCategory::Tool`) are equipment, excluded from cargo
  weight, so an axe never eats into how much wood a colonist hauls. 35 kg / 2.5 kg = 14
  wood per trip.
- Mass helpers live in `libs/engine/ecs/InventoryMass.h` (pure `massUnitsThatFit` /
  `massUnitsPerTrip` arithmetic plus registry-backed wrappers; tool-possession check).
- Tool gate: `HarvestableCapability.requiredToolType` (`requiresTool="Axe"` on the trees),
  `AssetDefinition.toolType` (`<toolType>Axe` on the axe). `AIDecisionSystem::evaluateHarvestOptions`
  skips a tree unless the colonist holds a matching tool; `ActionSystem::startHarvestAction`
  guards the same as a safety net.
- Harvest application (`ActionSystem`): the withdrawal is clamped to the colonist's
  remaining weight headroom, the pool is debited by exactly that, and the tree is removed
  on depletion. `Inventory::addItem(_, 0)` is now a no-op (was creating zero-quantity slots).
- Construction per-trip bound (`ConstructionSystem`): the harvest-demand cap is now
  `carryCapacityKg / mass(material)` instead of the backpack stack size, which is what
  drives the repeated trips.
- Resource pool semantics (`PlacementExecutor::decrementResourceCount`): takes a requested
  amount, returns how much it withdrew, erases the entry at zero (= depleted).

## Bugs found and fixed during verification

- Dev/tool-spawned harvestables never got a resource pool (only the chunk-generation path
  seeded it), so a dev-spawned tree gave zero wood. `PlacementSystem::spawnEntity` now
  seeds the pool exactly like `PlacementExecutor::storeChunkResult`.
- Depleted runtime-spawned trees (ECS entities seen by VisionSystem Pass 2) were never
  removed: the remove-entity callback only touched the placement index. `GameScene` now
  also queues the matching ECS entity for the deferred-destroy drain, so a depleted tree
  actually disappears instead of being re-discovered forever.

## Verification (dev tools, live game)

End-to-end in the `Game` scene: spawn oaks, give the colonist an axe, place a 12 m² Wood
foundation (24 wood = 2 trips), run at Fast. Observed: `Chopped 10` then `Chopped 4`
(= exactly 14 wood = 35.0 kg, the second chop weight-clamped), deposit 14, `Chopped 10`
(sized to the 10 the site still needed), deposit 10, then Build → foundation Built. Zero
empty chops, zero failed removals. Axe gate proven separately: with no axe the colonist
never harvests (0 chop actions); the instant the axe is given it starts.

`/api/state?what=colonists` now reports each colonist's `inventory`, `cargoKg`, and
`carryCapacityKg` so the loop is assertable without screenshots.

## Files

Engine: `assets/AssetDefinition.h`, `assets/AssetRegistry.{h,cpp}`,
`assets/placement/PlacementExecutor.{h,cpp}`, `ecs/components/Inventory.h`,
`ecs/InventoryMass.h` (new) + `InventoryMass.test.cpp` (new), `ecs/systems/ActionSystem.{h,cpp}`,
`ecs/systems/AIDecisionSystem.cpp`, `ecs/systems/ConstructionSystem.{h,cpp}` +
`ConstructionSystem.test.cpp`.
App: `scenes/game/GameScene.cpp`, `scenes/game/dev/DevCommandHandler.cpp`,
`scenes/game/world/placement/PlacementSystem.{h,cpp}`.
Assets: new `AxePrimitive`; `<mass>` on Wood/Stick/SmallStone/PlantFiber/Berry;
`requiresTool="Axe"` on Oak/Maple/Palm.

## Follow-up fixes (task-chaining UI review)

A pass over how chained tasks surface in the UI turned up four fixes (all verified live):

- `DecisionTrace::calculatePriority()` returned 0 for an *in-progress* work task: every work
  branch was gated on `status == Available`, but a selected option's status is `Selected`, so
  it fell through to `return 0` while Wander returned 10 unconditionally. That corrupted the
  logged priority, the stored `task.priority`, and the switch-threshold gap. Now Available OR
  Selected keeps full tier priority.
- The selected-colonist info panel showed the raw defName (`Harvesting Flora_TreeOak for
  Wood`). It now builds the task reason from friendly labels: `Cutting Oak Tree for Wood`.
- The info panel's task line overflowed the 140px left column into the needs column. The
  Current/Next lines now render full-width above the Gear|Needs columns (fixed panel height
  bumped 280->320 to fit).
- The "Next" line was a placeholder showing the current action. It now shows the real next
  step of the colonist's chain, derived by classifying the chain's destination entity
  (StructureBlueprint -> build site, WorkQueue -> crafting station, StorageConfiguration ->
  storage): a harvest reads "Next: Haul to build site", the haul reads "Next: Build structure".

Build progress was invisible ("looks frozen when he's building"): the colonist stands in
place while the blueprint's `workDone` advances, but nothing read as progressing. Two fixes:
- The colonist's Current line now appends the build percent ("Building structure (41%)"),
  read from the bound blueprint's `progress()`, so a long build visibly counts up.
- The on-map progress fill was the wood-tan material colour ramping from a 0.15 alpha floor
  — invisible against the sandy ground early on. Raised the foundation `progressAlphaMin` to
  0.4 so an actively-building foundation reads as a filled square from the start and firms up
  to 0.9. Blueprints still awaiting materials stay faint (the fill layer only draws once
  `progress > 0`). The foundation info panel already had a "Work" bar.

The right-side global task list now shows the whole construction chain and who's on it.
Previously it iterated only the Cut/Haul children, all reading "Unassigned" even while a
colonist worked them. Now:
- The umbrella **Build** goal is included, so a site reads Cut for Wood -> Haul Wood ->
  Build structure. Its row pulls the blueprint's real progress: "Needs materials" while
  awaiting, then "Building - Bob (16%)" under construction (the goal's own counters are an
  unused marker).
- "Unassigned" is replaced by **who's working it** ("Working - Bob"), recovered by scanning
  colonist tasks for the goal each is servicing (`World` threaded model -> adapter).
- The status separator was U+2022, which the SDF atlas lacks (it rendered as "???"); swapped
  to " - ".

A "process" note: a stale `world-sim.exe` from a *sibling worktree* held the default debug
port (8081), so relaunches silently lost the port race and drove the wrong binary — the source
of several phantom "stall / foundations vanished" symptoms. Run this worktree's instance on a
dedicated port (`--http-port 8082`). The chained loop itself completes reliably; the apparent
stalls were all artifacts (wrong binary, build-complete-with-leftover-wood, an overlapping
foundation getting rejected, or dev-placed entities lost to chunk unload after a far wander).

## Relationship to the equipment spec

`docs/design/game-systems/colonists/equipment.md` + the React prototype
(`docs/ui-prototype`, GearTab) describe the eventual three-tier carry model: a slot-based
belt (one-hand tools), a weight-capped pack (bulk), hands, and a strength-derived total
carry / encumbrance. That spec lists "weight-based pack capacity and total carry/
encumbrance" as not-yet-built; this is the first engine implementation of it. The single
`Inventory.carryCapacityKg` (35) is the interim stand-in for the prototype's `CARRY_CAP_KG`
(35); the prototype also has a separate `PACK_CAP_KG` (30) and `BELT_SLOT_COUNT` (4) for
when the belt/pack split lands. Excluding tools from cargo maps onto "the axe rides the
belt, wood rides the pack" — consistent for hauling; the nuance not yet modelled is that
belt/held items still count toward *total* carry (encumbrance).

Naming is SI and scientifically accurate on purpose: kilograms are mass, so the engine
fields are `massKg` / `carryCapacityKg` (not "weight", which is a force). The engine always
stores mass in kg; a metric/imperial toggle on the settings page is a future display-layer
conversion, never a stored unit.

## Follow-ups

- Future: belt (slots) + pack (`PACK_CAP_KG`) + total-carry/encumbrance split per the
  equipment spec; movement slowdown over-encumbered. Hauling would then bound on pack
  capacity, total carry on everything.
- Future: metric/imperial display toggle in settings (kg↔lb at render time; SI in the model).
- Multi-colonist carry accounting is coarse (`carriedAmount` sums all colonists vs a single
  colonist's per-trip bound); fine for the single-cutter case.
- A colonist holding a partial load with no reachable trees left will not deliver the
  partial until demand can be topped up; revisit when load-balancing across colonists.
