# 2026-06-18 - Dialogs de-hand-rolled with a ListRow primitive

## Summary

Removed the last hand-rendered content from the in-game dialogs and fixed the dialog/modal terminology. The crafting and storage dialogs were already built on the `Dialog` component, but each hand-drew its left-hand selectable list (recipe list / item+category list) with direct primitives and manual hit-testing, because the design system had no left-aligned selectable list-row widget (`Button` always centers and uppercases its label). Added that missing primitive (`ListRow`) and rebuilt both lists on it.

## Details

- **Terminology (#186).** A dialog is a `Dialog`; "modal" is a *property* (does it block the content beneath), not a synonym. Renamed the ui-sandbox `SalvageModalScene` -> `SalvageDialogScene` (file, class, scene key, `SceneTypes` entry, CMake), renamed the design-system "Modal" primitive to "Dialog" in `components.md`, and fixed the dossier/landing-site specs to say "Dialog title/kicker/footer". The `Dialog` component already modeled this correctly with a `bool modal` field. "modal" now appears only as that property/adjective.

- **`ListRow` (#187).** New header-only widget `libs/ui/components/list/ListRow.h`: a left-aligned, selectable row with hover/selected washes (`bg_hover` / `bg_active`), a left accent bar + bottom hairline when selected, optional right-aligned trailing (mono) text, a `dim` flag for unavailable rows, and an `indent` for tree nesting (the accent bar/hairline/washes stay full-width; only the label shifts). It handles its own hover/click, so it drops into a `ScrollContainer`/`LayoutContainer` with no manual hit-testing.

- **CraftingDialog (#187).** Recipe list rebuilt as composed `ListRow`s in the left `ScrollContainer`. Deleted `renderRecipeList()`, `getRecipeIndexAtPosition()`, `getRecipeItemBounds()`, the manual hit-testing in `handleEvent()`, the hovered-index tracking, and the hardcoded translucent row colors.

- **StorageConfigDialog (#188).** Item/category list rebuilt on `ListRow`: category headers are chevron-prefixed rows that toggle expand/collapse; item rows carry the `current/requested` count as trailing text, a selected highlight, dimming when the item has no rules, and an indent under their category. Deleted the same family of hand-render methods (`renderItemList`, `getItemBounds`, `getItemIndexAtPosition`, `getCategoryHeaderBounds`, `handleItemClick`) and the manual hit-testing.

Net result: the game UI now has no hand-rolled selectable lists and no hardcoded translucent washes; the colonist dossier and its tabs were already composed from widgets.

## Related Documentation

- `libs/ui/components/list/ListRow.h` — the new primitive
- `docs/design/ui/design-system/components.md` — Dialog (renamed from Modal)

## Next Steps

- **Orphaned `TaskListView`.** The per-colonist task-queue panel has no open path in the current UI (`toggleTaskList()` is only called by its own close handler; it was meant to "toggle from info panel" but that wiring was never completed). Decide: re-wire it to the info panel as intended, or delete it as dead code (it is the only consumer of the `StatusTextLine` and `SectionHeader` widgets).
- A full prototype-fidelity pass on the dialogs (the tabbed dossier treatment, footers) remains optional polish; structurally they are now composed and tokenized.
