# UI design

The interface design for World-Sim: the look and feel ("Salvage"), the design system that builds it, and the spec for every screen. Extracted from the interactive prototype under `docs/ui-prototype/`, which stays as the live sandbox; these docs are the canonical spec the C++ UI implements against.

This covers interface chrome (panels, readouts, controls). For terrain and world art direction see [visual-style.md](../visual-style.md).

## Start here

- [design-language.md](./design-language.md) — what Salvage is: principles, palette, typography, texture, motion.

## Design system

- [design-system/tokens.md](./design-system/tokens.md) — every design value (readable). Canonical: [tokens.json](./design-system/tokens.json).
- [design-system/components.md](./design-system/components.md) — the 14 primitives: variants, props, states, anatomy, behavior.
- [design-system/icons.md](./design-system/icons.md) — the 51-glyph line-icon set. Canonical: [icons.json](./design-system/icons.json).

## Screens

The pre-game flow then the in-game HUD. See [screens/INDEX.md](./screens/INDEX.md).

Splash → Main Menu → Scenario Select → Party Selection → World Generation → Landing Site → In-Game.

## Mocks

Rendered screenshots of every screen and in-game state are in [mocks/](./mocks/), captured from the prototype by `docs/ui-prototype/scripts/capture-mocks.mjs` (re-runnable).

## Regenerating the extracted data

The canonical data files are generated from the prototype, not hand-edited:

- `node scripts/extract-tokens.mjs` → `tokens.json` + `palette.svg`
- `node scripts/extract-icons.mjs` → `icons.json` + `icons-preview.svg`
- `node scripts/capture-mocks.mjs` → `mocks/*.png`

Run from `docs/ui-prototype/`.

## Related

- [docs/design/INDEX.md](../INDEX.md) — game design index
- [docs/design/game-systems/colonists/equipment.md](../game-systems/colonists/equipment.md) — the inventory model the gear UI shows
- [docs/technical/ui-framework/INDEX.md](../../technical/ui-framework/INDEX.md) — the C++ UI framework (implementation side)
