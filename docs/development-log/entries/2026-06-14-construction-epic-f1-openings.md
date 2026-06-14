# 2026-06-14 - Construction Epic F1: openings (doors & windows)

## Summary

Doors and windows placed on walls, end to end with interim visuals. Place a Door/Window along a built wall, it reserves a gap in the wall and builds as its own blueprint (own materials + work), renders as a procedural fill in the gap, and is selectable with an info panel and per-opening demolish. The heavy parts (full parameterized procedural Lua door/window assets, retrofit-cut-as-work, portal publication to nav) are deferred to F2, the same way walls deferred their baked element-emitter rendering.

## Details

### What was built (F1)

The wall topology already modeled openings from Epic D (`Opening{id, segment, t, type, material, state, entity}`, `addOpening`, T-split re-attach by parameter range). F1 adds everything around that.

- **Config + validation + snap (F1a, engine).** Opening types in `materials.xml` (Door 0.9 m pathable, Window 0.6 m not; Wood-only v1) loaded into `ConstructionRegistry::OpeningTypeDef`, with `ConfigValidator` cross-checks. `ConstructionValidator::validateOpening` (segment/type/material valid, `t` in range, end + inter-opening margins, wall length). `SnapEngine::snapOpening` (cursor → nearest BUILT segment → parameter `t`, clamped to honor end margins). Plus `ConstructionWorld` opening mutators (`setOpeningState`/`setOpeningEntity`/`removeOpening`).
- **Lifecycle (F1b, engine ECS).** `ConstructionSystem` includes `Opening` blueprints in the deliver-then-build loop (own materials, constant work, no clearing phase), gated on the host wall segment being Built via `isOpeningHostSegmentBuilt` (mirrors `isWallHostBuilt`). Built in an isolated worktree in parallel with the app slice and merged cleanly (disjoint files).
- **Tool + dev API (F1c, app).** `OpeningTool` in `DrawingSystem` (slide along a wall, validity-colorized ghost, commit → `addOpening` + spawn the opening blueprint entity), config strip opening mode, GameplayBar Door/Window entries, `/api/dev/opening`, and the GameScene completion/deconstruct callbacks for openings.
- **Rendering (F1d, app).** Interim: the wall band renders with a GAP at each opening interval, and the opening renders as a simple procedural fill (door slab / window frame+pane) sized to opening width × wall thickness, styled by build progress. Plus the in-progress ghost. Mirrors the interim wall band render; walls without openings render byte-identically.
- **Selection (F1e, app).** `OpeningSelection` (priority above walls so it picks over the host wall), point-in-footprint hit test, `adaptOpening` info panel (type, material, pathable, state, materials, work, Demolish), and per-opening demolish. A shared `OpeningGeometry` footprint keeps render, hit-test, and indicator on one definition.

### Adversarial review

A 5-dimension review (validation, lifecycle, rendering, selection/dev-api, determinism/standards), each finding independently verified, surfaced 5 confirmed issues; two were the same bug found by two dimensions and two were the same comment. Fixed:

- **High: wall-demolish leaked its openings' entities.** `removeSegment` erased the opening topology records but the demolish handler only despawned the segment's own entity, leaking the door/window blueprint entities (stranded Blocked, inflating the active-blueprint count). `removeSegment` now surfaces the removed openings' ECS handles (out-param) and the handler despawns them through the deferred-removal queue. Pinned by a `ConstructionWorld` test. (Durable: a future cascade-demolish gets this right too.)
- **Medium:** `openingMarginMeters` is now sign-checked in `ConfigValidator` (a negative value would have silently disabled the margin/overlap checks).
- **Low:** corrected misleading fit-check comments; included `Opening` in the dev-helper kind filters (`creditMaterialToSites`/`forceCompleteBlueprint`) so they match the main loop.

Two findings were correctly refuted (a snap/validate float-boundary "disagreement" — the preview routes through `validateOpening`, so preview and commit always agree; and the dev-helper kind filter — no shipped path relied on it).

### Verification

558 engine tests green. Sandbox: a Door placed on a built wall shows a real gap in the wall band with the door filling it; clicking selects it and the panel reads Type: Door / Material: Wood / Pathable: Yes / State: Built / Materials: 3/3 / Work [full] / Demolish.

### Files

- New: `apps/world-sim/scenes/game/world/construction/OpeningGeometry.{h,cpp}`
- Engine: `ConstructionRegistry.{h,cpp}`, `ConfigValidator.cpp`, `materials.xml`, `ConstructionValidator.{h,cpp}`, `SnapEngine.{h,cpp}`, `ConstructionWorld.{h,cpp}`, `ConstructionSystem.{h,cpp}` (+ their tests)
- App: `DrawingSystem.{h,cpp}`, `GameScene.cpp`, `SelectionTypes.h`, `SelectionSystem.{h,cpp}`, `SelectionAdapter.{h,cpp}`, the info-panel view/model, `GameplayBar`, `ConstructionConfigStrip`, `apps/world-sim/CMakeLists.txt`

## Related Documentation

- Design: [`docs/design/game-systems/world/building-construction.md`](../../design/game-systems/world/building-construction.md) (Doors & Windows)
- Architecture: [`docs/technical/building-construction-architecture.md`](../../technical/building-construction-architecture.md) (D9)

## Next Steps

Epic G (editing & polish: foundation add/subtract, vertex editing, cascade demolish, multi-select), or F2 (the deferred parameterized procedural door/window assets + retrofit-cut-as-work + portal publication).
