# UI design system (Salvage) extracted from the prototype

Date: 2026-06-15

## Summary

The React/Vite/CSS-Modules prototype under `docs/ui-prototype/` got the game's UI look and feel ("Salvage": used-future ship terminal, amber readouts and teal data on near-black hull) to an approved state. That prototype is interactive code, not documentation, and the C++ UI can't follow it directly. So everything in it is now extracted into durable design docs under a new `docs/design/ui/` subtree: the design language, a token reference, a component catalog, the icon set, screen specs, and rendered mocks. The prototype stays as the live sandbox; the docs are the canonical spec the C++ build implements against.

No C++ was written. This is documentation extraction only. The engine implementation (token codegen, primitive library, renderer gaps) is captured as a separate future epic.

## Details

New docs under `docs/design/ui/`:
- `design-language.md` — Salvage overview: principles, palette intent, surfaces/elevation, typography roles (Chakra Petch / Barlow / JetBrains Mono with exact loaded weights), texture (scanlines/grain/vignette), motion, interaction states, and what won't port literally.
- `design-system/tokens.json` — canonical token values, colors carrying normalized rgba floats (the seed for a future C++ `Foundation::Color` theme). Generated, not hand-edited.
- `design-system/tokens.md` — the same values as readable tables, plus `palette.svg`.
- `design-system/components.md` — all 14 primitives (Button, Panel, Modal, Badge, Avatar, Icon, Meter, Slider, Stat, Tabs, SegmentedControl, Tooltip, Divider, KeyCap): variants, props, states, anatomy, and behavior/formulas (Avatar's FNV-1a hash and HSL derivation, Meter's auto-tone thresholds and inline label treatment, Slider's detent, Panel's corner-bracket geometry, etc.).
- `design-system/icons.json` + `icons-preview.svg` + `icons.md` — the 51-glyph line-icon set (8 filled), with the SVG path data that previously existed only as inline JSX.
- `screens/` — INDEX plus one spec per screen: splash, main-menu, scenario-select, party-selection, world-generation, landing-site, in-game. The in-game spec documents every HUD sub-state (roster, info panel, minimap with off-screen marker projection, storage, dense task list, colonist dossier dialog with all six tabs, gear paper-doll).
- `mocks/` — rendered shots of every screen and the in-game dossier/gear states, plus schematic anatomy wireframes for the three dense screens, and a gallery README.
- `INDEX.md` — entry point for the subtree.

New re-runnable extraction scripts in `docs/ui-prototype/scripts/`:
- `extract-tokens.mjs` → `tokens.json` + `palette.svg` (parses `tokens.css`, resolves aliases, converts every color format to rgba floats).
- `extract-icons.mjs` → `icons.json` + `icons-preview.svg` (splits the `PATHS` object on top-level commas; the path data has none).
- `capture-mocks.mjs` → `mocks/*.png` (Playwright via `playwright-core` + the installed Edge channel, so no browser download; clicks the dev-rail nav and toggles chrome between shots).

Wired into `docs/design/INDEX.md` (new User Interface section) and `docs/status.md`.

## Technical decisions

- Single source of truth stays the prototype's `tokens.css`; `tokens.json` is generated from it deterministically rather than hand-transcribed, so colors can't drift and the rgba floats are exact.
- The extraction caught a miscount from earlier exploration: the icon set is 51 glyphs, not 59. The component gallery in the prototype also omits three glyphs (shirt, pants, boot) that the component supports; the extraction took the set from the component, which is authoritative.
- Mocks captured via `playwright-core` + browser channel (Edge) to avoid a ~150MB Chromium download and to be repeatable.
- The eventual C++ theme will be compile-time `constexpr` (matches the existing engine pattern); live look iteration stays in the prototype, which hot-reloads instantly.

## Related documentation

- [/docs/design/ui/](../../design/ui/INDEX.md) — the extracted spec
- [/docs/design/game-systems/colonists/equipment.md](../../design/game-systems/colonists/equipment.md) — the inventory model the gear UI shows
- [/docs/technical/ui-framework/INDEX.md](../../technical/ui-framework/INDEX.md) — the C++ UI framework (implementation side)

## Next steps

A separate C++ implementation epic, planned once these docs are reviewed:
- Generate a `constexpr libs/ui/theme/Theme.h` from `tokens.json` (build tool mirroring `tools/generate_sdf_atlas`), replacing the hand-maintained `Theme.h`.
- Build the primitive library mirroring the catalog; delete superseded components (`components/dialog`, `components/progress`) as replacements land.
- Close the renderer gaps: stroke tessellation for the line icons (the vector pipeline only fills today) and multi-family MSDF font support.
- A `ComponentGalleryScene` in `apps/ui-sandbox` as the C++ acceptance harness.
