# Colonist dossier: prototype systems + in-game port

## Summary

Brought the colonist dossier (the full-screen "Personnel File" dialog) in line with
the Salvage prototype, in two parts:

1. **Prototype (merged, #200).** The dossier mockup predated three real game systems,
   so it was extended to cover them. Added **Health, Memory, and Tasks** tabs to the
   React prototype, modeled on the actual ECS systems; folded the old "Needs" tab into
   Health; regenerated the reference mocks and synced the screen spec / anatomy / README.
   Final tab set: Bio · Health · Skills · Social · Gear · Memory · Tasks · Log.

2. **In-game C++ port (draft, #204).** Ported `ColonistDetailsDialog` to match: a
   persistent header (avatar + role/origin/age/mood), a "PERSONNEL FILE" kicker, an
   8-tab bar, and a Close / Work Priorities / Draft footer. The shared `Dialog` gained
   optional kicker + footer support. Built Health (two-column needs + body/ailments),
   Memory (metrics + capability categories), Tasks (current-task panel + known work);
   Skills/Log are honest placeholders. Then a fidelity pass: header stat columns fill
   the width (prototype `1fr 1fr`), and empty-state text wraps + the box auto-sizes.

## Details

**Files (prototype, #200):** `docs/ui-prototype/src/screens/InGame/{ColonistDetailsDialog.tsx,
HealthTab.tsx, MemoryTab.tsx, TasksTab.tsx}`, `data/mock.ts`; `docs/ui-prototype/scripts/
capture-mocks.mjs`; regenerated `docs/design/ui/mocks/in-game-dossier*.png`; updated
`docs/design/ui/screens/in-game.md`, `mocks/README.md`, `colonist-dossier-anatomy.svg`.

**Files (C++ port, #204):** `apps/world-sim/scenes/game/ui/dialogs/ColonistDetailsDialog.{h,cpp}`,
`ColonistDetailsModel.cpp`, `tabs/{Bio,Health,Memory,Tasks}TabView.*`, new `tabs/{Skills,Log}TabView.*`
and `tabs/MeterDraw.h`; `libs/ui/components/dialog/Dialog.{h,cpp}`; `apps/world-sim/CMakeLists.txt`.

## Technical decisions

- **Prototype is the design authority; grow it to fit the game, not shrink the game.**
  Rather than delete the working Memory/Tasks features to match an outdated mockup, the
  mockup was extended to model the real systems (per the user).
- **Explicit tab-view positioning, not an auto-stacking layout.** The first port stacked
  the tab views in a vertical `LayoutContainer`, which assigned siblings inconsistent
  offsets (the active tab landed on top of the header band). Switched to direct dialog
  children at explicit coordinates — the reliable pattern for this codebase.
- **Wrapping inflates measured width ~12%.** The C++ text renderer lays glyphs slightly
  wider than `FontRenderer::MeasureText` reports, so wrap/fit decisions that trusted the
  raw measurement let text overflow. `MeterDraw.h` now inflates the measured width before
  breaking, and `drawEmptyState` auto-sizes its box to the wrapped content.
- **UI scale deferred to a setting.** The whole game UI renders denser than the prototype
  (logical viewport ~2.5x the prototype reference), so the dossier reads small. Captured
  as a future global UI-scale setting (`docs/design/ui/ui-scale-setting.md`) instead of
  hand-enlarging the dossier.

## Related documentation

- `docs/design/ui/screens/in-game.md` — "Colonist details dialog" (+ implementation-status note)
- `docs/design/ui/ui-scale-setting.md`
- `docs/status.md` — "In-game dossier fidelity (remaining)" and "UI Scale setting" epics

## Next steps

Tracked under the **In-game dossier fidelity** epic in `status.md`. Biggest item: the
**Gear tab is not implemented to the prototype** (sparse readout vs the full paper-doll).
Skills/Social/Log stay placeholders until their systems exist. Needs a text-overflow pass
with a data-rich colonist. PR #204 is a draft pending these + a rare first-open crash seen
once in testing.
