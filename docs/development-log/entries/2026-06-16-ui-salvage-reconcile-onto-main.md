# 2026-06-16 — Salvage UI cutover: z-index draw queue + reconcile onto main

## Summary

Two things landed on `feature/ui-salvage-cutover`: a global UI z-index draw queue
in the renderer, and a merge that brought the long-running Salvage UI cutover back
in sync with current `main`. The branch had forked at the prototype commit
(`6bb76c7`) and main moved 53 commits past it, so before any of this could become a
PR the two histories had to be reconciled. They reconciled far more cleanly than
feared: the renderer was the branch's alone (main never carried the Salvage
additions), so only `docs/status.md` conflicted textually.

## z-index: a sorted draw queue

Open popups (dropdown/select menus) painted behind sibling widgets. `Component::render()`
stable-sorts each parent's children by `zIndex`, which orders stacking *within* a
parent, but there was no global order, so a menu nested in its trigger could not
paint above widgets in other subtrees that render later. The per-call `zIndex` was
captured in the draw args, then dropped at the `BatchRenderer` boundary.

Fix: resolve draw order at the batch level. Each `add*` call records one POD group
`{indexStart, indexCount, zIndex}` into a pre-reserved vector (z flows from the
component layer via `args.zIndex`; default 0 = organic submission order). `flush()`
keeps submission order untouched unless some draw carried an explicit non-zero z, in
which case the groups are stable-sorted by z and the emit-order index list is rebuilt
over draw calls, not triangles. The world is unaffected (tiles + entities go through
the tile pass and instancing, not these 2D batch calls). A popup's high `zIndex`
(Menu = 1000) now floats with no portal and no "render last" hack; the dead-end
overlay queue (`submitOverlay`) was reverted.

Perf gate (RelWithDebInfo, `ZSort.bench.cpp`, 10k draw calls — a stress test, real UI
frames are dozens to low hundreds): baseline 94us; fast path (no popup) 105us (+10us
always-on for the group records); sorted (popup open, ~2% explicit z) 282us (only on
frames with an open popup). The always-on cost is 0.06% of a 16.6ms frame; no
meaningful regression on the common path.

## Reconcile onto main

- Forked at `6bb76c7`; main is 53 commits ahead (renderer was *not* among them, so the
  branch is strictly ahead on `BatchRenderer`/`Primitives`).
- Merged `origin/main` into a fresh `feature/ui-salvage-cutover`. Only `docs/status.md`
  conflicted; `CMakeLists.txt`, `WorldCreatorScene.cpp`, and `docs/design/INDEX.md`
  auto-merged.
- main's ~33 new world-sim UI consumers (newer GameUI, GameplayBar, EntityInfoView,
  ParameterPanel, selection adapters/models, landing + splash scenes) compiled against
  the reorganized library unchanged — the unification kept the legacy widget names and
  restyled them in place, so consumer include paths and call sites still resolve.
- The only build breakage was three of the branch's own widget tests, which still
  asserted pre-restyle contracts (`Theme::` layout constants; `ProgressBar`'s
  size-as-`Vec2` `Args` with `labelWidth`/`labelGap` and `getWidth`/`getHeight`).
  Updated them to the current API.

## Verification

- Whole solution builds clean (Debug).
- `ui-tests` 288/288 and `renderer-tests` 79/79 pass.
- ui-sandbox: the Salvage gallery renders the full widget set (buttons, bars, badges,
  tabs, swatches, icons, avatars, sliders, tooltip, panels); the dropdown demo's
  ACTIONS menu paints above the row behind it (z-index).
- world-sim launches to its main menu and renders without crashing.

## Files

- Renderer: `libs/renderer/primitives/BatchRenderer.{h,cpp}` (group queue + z-sort),
  `Primitives.cpp` (plumb `args.zIndex`), `ZSort.bench.cpp` (new).
- Tests: `libs/ui/components/{contextmenu,progress,treeview}/*.test.cpp` (to current API).
- Merge commit `92aa527`; `docs/status.md` reconciled.

## Next

Phase 3 (rebuild HUD views, dialogs, and WorldCreatorScene to the prototype designs)
and Phase 4 (delete the legacy `theme/Theme.h` + `PanelStyle.h`) remain as follow-up
work in separate PRs. This PR is scoped to "reconciled, building, and verified."
