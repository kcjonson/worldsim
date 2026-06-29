# 2026-06-29 - Belt stow-to-free-hands (combined craft→chop→carry blocker)

## Summary

Fixed the combined-flow blocker from the bulk-materials spec (§4 "Belt slot +
stow-to-free-hands"): a colonist who CRAFTED an axe got it seated in a hand, and the
subsequent two-hand wood armful at fell completion needed both hands free, so
`addArmful` returned 0 and the entire chopped yield dropped on the ground ("Collected
0 … carry-limited") instead of going into the hands. The axe was never moved out of the
way. Now a held one-hand tool is stowed (belt → pack → drop) just before any two-hand
armful is lifted, the wood goes into the hands, and the colonist keeps chopping with the
axe drawn from the belt.

## What existed vs what was implemented

Already present (from the earlier physical-carry epic), so NOT re-built:
- The **belt** itself: `Inventory::belt` is a `std::array<std::optional<ItemStack>, 2>`
  (2 one-hand quick-draw slots) with `stowToBelt` / `takeFromBelt` / `beltHasFreeSlot`.
- `inventoryHoldsToolType` already scans hands **+ belt +** backpack, so a belted axe
  already satisfies the harvest tool-check.
- `giveItemToColonist`'s full hand → belt → backpack → drop cascade (used by craft
  output), and `clearHandItem`'s belt → backpack → drop for a single hand.

Missing (the actual bug): nothing stowed a held tool before a two-hand `addArmful`. The
fell-completion and loose-pile-haul branches in `HarvestActions.cpp` called `addArmful`
directly; with an axe in a hand, `hasHandsFree(2)` failed → `addArmful` returned 0 → the
whole yield dropped.

Implemented:
- `ecs::stowHeldToolsToFreeHands(inv, registry, onDrop)` in `InventoryMass.h` — moves any
  held **one-hand** item out of the hands (belt if a slot is free, else pack by weight,
  else drop). A two-hand armful already in the hands is left alone (it can't be stowed and
  isn't in its own way).
- `ActionSystem::stowHeldToolsForArmful(inv, dropPos)` in `HaulActions.cpp` — binds the
  drop to `m_onDropItem` (per-unit loose drop, matching `clearHandItem`).
- Called before **both** two-hand `addArmful` sites in `HarvestActions.cpp`: the
  single-shot fell-completion branch and the loose-pile haul branch. These two are the only
  places a two-hand armful is taken (a Pickup and a Harvest both route through
  `applyCollectionEffect`), so this covers fell-completion AND haul pickup of loose wood.
- Colonist state readback (`DevCommandHandler::serializeColonists`) now emits explicit
  `hands` and `belt` so the carry surfaces are visible (the old `inventory` map folded
  hands into the backpack aggregate and couldn't tell a held axe from a stowed one).

The dedicated `addArmful` path was kept (not folded into `giveItemToColonist`) — the
cargo-vs-backpack divergence for two-hand goods is intentional; this just stows first.

## Files modified

- `libs/engine/ecs/InventoryMass.h` — `stowHeldToolsToFreeHands` helper.
- `libs/engine/ecs/systems/ActionSystem.h` — `stowHeldToolsForArmful` declaration.
- `libs/engine/ecs/systems/action/HaulActions.cpp` — `stowHeldToolsForArmful` definition.
- `libs/engine/ecs/systems/action/HarvestActions.cpp` — call it before both two-hand
  `addArmful` sites.
- `apps/world-sim/scenes/game/dev/DevCommandHandler.cpp` — `hands`/`belt` in the readback.
- `libs/engine/ecs/InventoryMass.test.cpp` — 4 `StowHeldTool_*` tests.
- `libs/engine/ecs/systems/ActionSystem.test.cpp` — `HeldAxeStowedToBeltFreeingHandsForWoodArmful`,
  `HeldAxeFallsBackToPackWhenBeltFull`.

## Verification

- **engine-tests (Debug): 872/872 green**, including the 6 new tests. No existing test
  needed changing (the `HandsFullStillDropsWholeYieldAndRemovesTree` case pre-fills hands
  with a two-hand Wood armful, which `stowHeldToolsToFreeHands` deliberately leaves alone,
  so its old behavior holds).
- **In-game (world-sim RelWithDebInfo, port 8111):** a colonist crafted an axe (seated
  in-hand via `giveItemToColonist`), then felled an oak: `Collected 14 of 18 x Wood`
  (into the hands, not 0), 4-unit remainder dropped as a loose pile, axe stowed to the
  belt (gone from the hands/backpack aggregate), then `Deposited 14 x Wood into storage`,
  and the loose remainder hauled in further trips; the box reached 15 Wood.

## Decision noted

Belt size: kept the existing 2 one-hand slots (`Inventory::belt`); 1 is enough for the
axe and the spec calls for a small fixed set. No change.

## Known limitation surfaced (not this change)

Reproducing the crafted-axe path a second time was repeatedly blocked by a pre-existing
AI-arbitration defect: after partial provisioning, a colonist abandons the queued craft to
forage/wander ("All needs satisfied → Wander", priority 10) and the available craft goal
never re-surfaces as a higher-priority option, so the craft never starts. Tracked under
"AI arbitration / reliably do the queued job" in `docs/known-issues-and-followups.md`. The
fix here is proven by the unit tests and the one clean in-game run above.

## Related Documentation

- `docs/technical/bulk-materials-carry-and-felling.md` — §4 (belt + stow-to-free-hands),
  §5 (one-shot felling), §7 (hand-aware haul/craft/deposit).
- `docs/design/game-systems/colonists/equipment.md` — hands/belt/pack carry model, stow order.
- `docs/known-issues-and-followups.md` — the "Belt stow-to-free-hands" entry.
