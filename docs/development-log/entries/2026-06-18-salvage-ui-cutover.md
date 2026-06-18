# 2026-06-18 - Salvage UI cutover complete

## Summary

Finished the Salvage UI cutover: every in-game view, every screen, and the shared `libs/ui` widgets now style off `theme/Tokens.h`, and the legacy `theme/Theme.h` + `theme/PanelStyle.h` are deleted. `Theme::Colors` / `PanelStyles` no longer appear anywhere in the tree (outside historical comments). Tokens are the single source of truth.

## Details

The cutover landed across a chain of small, individually-verified PRs rather than one large one. The earlier reconciliation (merge the diverged cutover branch onto main) and the z-index draw queue shipped in #164; the pre-game screens (splash, main menu, world creator) in #171; the in-game core (top bar, colonist roster, command bar, right stack) in #167. This entry covers the tail that finished it:

- **#176** — `EntityInfoView` (the selected-entity panel) off `Theme::Colors` / `PanelStyles` onto tokens. Verified live: colonist selected, panel renders bottom-left with the Salvage palette, no overlap / overflow / misalignment.
- **#177** — construction build-preview colors (`DrawingSystem`): `statusActive` -> `status_ok` (green valid), `statusBlocked` -> `status_crit` (red invalid) for the foundation/wall/opening rubber-band, vertices, and invalid highlight. Green valid path verified live with the Foundation tool. Also dropped three dead `Theme.h` includes.
- **#178** — `CraftingDialog` and `StorageConfigDialog` text colors tokenized. This cleared the last `Theme::Colors` usage in `apps/`.
- **#180** — shared widgets `StatusTextLine`, `SectionHeader`, `ListItemStyle` tokenized. Tokens qualified `UI::` to avoid colliding with the inherited `Text::text` member.
- **#181** — the finalization. `Dialog` / `Tooltip` / `Icon` already rendered with tokens; only their layout constants still came from `Theme::`, so those moved into the component headers as `constexpr` (`kDialog*`, `kTooltip*`, `kIcon*`) with identical values. Dead `Theme.h` includes dropped from seven ui-sandbox scenes. Then both legacy headers deleted. `world-sim`, `ui-sandbox`, and `ui-tests` build; 99 ui unit tests pass; the Dialog and Icon sandbox scenes render correctly.

A few decisions worth recording. Symbols that were drawn from the font atlas (middot, degree, chevrons) are now vector-drawn via `drawCircle` / primitives where they appear, per the "draw them as SVGs, not glyphs" direction. Centered `drawText` needs an explicit `boxWidth` or it drifts right; the splash and other centered labels pass `boxWidth = screenW`. The per-colonist `TaskListView` (which consumes `StatusTextLine` / `SectionHeader`) is currently wired only to its own close handler, so it isn't reachable in-game; its widgets were tokenized but couldn't be captured live.

## Related Documentation

- `/docs/design/ui/` — Salvage design language, tokens, component catalog, screen specs
- `libs/ui/theme/Tokens.h`, `libs/ui/theme/Variants.h` — the surviving token surface

## Next Steps

- **Dialog redesign (deferred).** `CraftingDialog` and `StorageConfigDialog` were color-tokenized, not rebuilt. The prototype's tabbed colonist dossier (a `Dialog`, size `lg`) and a fuller dialog treatment remain; the two crafting/storage dialogs are still hand-rolled rather than built on the `Dialog` component.
- **Dead `TaskListView`.** It has no open path in the current UI (the info-panel details button opens the colonist dossier instead). Either re-wire it or delete it.
