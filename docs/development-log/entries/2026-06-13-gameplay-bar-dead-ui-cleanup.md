# 2026-06-13 - GameplayBar dead-UI cleanup + Furniture storage placement

## Summary

The GameplayBar's Actions dropdown (Hunt/Harvest/Haul) and Furniture dropdown
(Beds/Tables/Storage) were dead UI: their `onActionSelected` / `onFurnitureSelected`
callbacks were never passed from `GameUI`, so selecting a menu item invoked a null
`std::function` and silently did nothing. The Build dropdown was likewise a
non-functional `(Coming Soon)` placeholder wired to a dead `onBuildClick` callback.

Per direction to move toward a playable game without UI that advertises things that
don't work, the bar was reduced to only functional categories:

- **Removed** the Actions dropdown. Hunt/Harvest/Haul are designation-style overrides
  that require a designation system the codebase does not have. The design spec
  (`main-game-ui-design.md` §9.2) also describes Actions as *exception overrides*
  (Haul-Force, Deconstruct, Deny, Cancel) for colonists that already auto-harvest/hunt
  autonomously — so the stub items did not match the intended feature either.
- **Removed** the Build dropdown and its dead `onBuildClick` → `onBuildToggle` chain.
  Building/construction is a planned-but-unimplemented feature; the B key still opens
  the existing build menu popup (`PlacementSystem::toggleBuildMenu`) for power users.
- **Wired** the Furniture dropdown to real placement. It is now populated dynamically
  from any asset definition that exposes a `storage` capability (currently `BasicBox`,
  `BasicShelf`) and routes selection through `PlacementSystem::selectBuildItem`, the
  same flow the Production dropdown uses for stations.

The result: the bar is now `[Production▾] [Furniture▾]`, and every item places a real
entity. Placing a storage container immediately gives it `Inventory` +
`StorageConfiguration` (via `PlacementSystem::spawnEntity`), which is what
`AIDecisionSystem::evaluateHaulOptions` keys off of to generate Haul tasks — so the
player can now create storage demand directly from the bar rather than only through
the craft-then-place path.

## Details

### Files modified
- `apps/world-sim/scenes/game/ui/views/GameplayBar.h`
  - Removed `onActionSelected` / `onBuildClick` from `Args` and members.
  - Removed `actionsDropdownHandle` / `buildDropdownHandle`.
  - Added `setFurnitureItems()` and a private `setDropdownItems()` helper shared by the
    Production and Furniture dropdowns.
  - Added `kButtonCount` constant so layout math is not hard-coded to 4 buttons.
- `apps/world-sim/scenes/game/ui/views/GameplayBar.cpp`
  - Constructor now creates only the Production and Furniture dropdowns (both populated
    dynamically).
  - `setProductionItems` / `setFurnitureItems` delegate to `setDropdownItems`, whose
    per-item lambdas capture a copy of the callback (safer than capturing `this`).
- `apps/world-sim/scenes/game/ui/GameUI.h` / `GameUI.cpp`
  - Replaced the dead `onBuildToggle` arg with `onFurnitureSelected`.
  - Forward `setFurnitureItems()` to the GameplayBar.
- `apps/world-sim/scenes/game/GameScene.cpp`
  - Removed the dead `onBuildToggle` lambda; added `onFurnitureSelected` →
    `selectBuildItem`.
  - Populate the Furniture dropdown by scanning `AssetRegistry` for definitions with a
    `storage` capability.

### Technical decisions
- **Direct placement of storage furniture** (free, no material cost) was chosen for
  consistency with the existing Production dropdown and the B-key build menu, both of
  which already place innate-recipe outputs directly. Material-cost-gated placement
  (blueprint → craft → haul) is a future construction-system concern, not in scope here.
- **Dynamic population by capability** means new storage containers appear in the
  Furniture menu automatically with no UI code changes. When non-storage furniture
  (beds, tables) gains entities/capabilities, the enumeration can be broadened.

### Verification
- Static verification only: confirmed zero remaining references to the removed symbols
  (`onActionSelected`, `onBuildClick`, `onBuildToggle`, `actionsDropdownHandle`,
  `buildDropdownHandle`) across `apps/` and `libs/`, and that the new wiring is
  consistent across all four files. The new code mirrors the already-compiling
  `setProductionItems` / `onProductionSelected` pattern.
- A full compile was not run: this container has no `vcpkg` toolchain provisioned and
  building the dependency set (GLFW, GLEW, msdfgen, Lua, …) from source is not practical
  here.

## Related Documentation
- `/docs/design/main-game-ui-design.md` §9 (Gameplay Bar)
- `/docs/design/features/storage-system.md`
- `/docs/design/features/player-control.md` (designation/command mode is Phase 3)

## Next Steps
- Build/construction system: when implemented, reintroduce a Build category that places
  walls/floors/doors via the construction flow.
- Actions overrides (Haul-Force, Deconstruct, Deny, Cancel): require a designation
  system; add the Actions category back once that exists.
- Consider unifying the B-key build menu with the Production/Furniture dropdowns to
  satisfy the One Path Rule (currently the build menu redundantly lists all innate
  recipes).
