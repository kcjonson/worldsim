# Main menu

![Main menu](../mocks/main-menu.png)

The title screen proper, reachable from the splash and from in-game via the "Menu" button in the HUD. It sits behind a living background, leans heavily on the left side of the screen, and lets a procedural planet fill the right.

## Purpose & flow

The main menu is the navigation hub between sessions. From here the player starts a new game (which opens [Scenario select](./scenario-select.md)), continues the last colony (which goes directly to [In-game](./in-game.md)), or accesses settings, credits, and exit. "Load Game," "Settings," and "Credits" are present but disabled in the prototype.

## Layout & regions

The screen has no fixed grid — it's a full-bleed canvas with three visual layers:

- **Background** — [Starfield](../../../ui-prototype/src/scene/Starfield.tsx) at seed 11, full-bleed.
- **Planet** — a [Planet](../../../ui-prototype/src/scene/Planet.tsx) component at seed 108, size 640, biome mode, positioned absolutely at right −160px (partially off-screen) and vertically centered. A linear gradient scrim (`--bg-void` at 26% → transparent at 68%, angled 100deg) bleeds the left panel content over it cleanly.
- **Content column** — left-aligned, max-width 640px, centered vertically with padding `var(--space-16) var(--space-20)`. Contains the identity header and the menu nav block.

**Header** stacks horizontally: the `◈` glyph (40px, amber, glow) beside the title + tagline pair.

**Menu nav** has a mono kicker (`// Main Menu`), then six stacked menu items.

**Footer** is absolute, bottom of screen, spanning from the left padding to near the right edge. Two text spans: version left, "Star system: Maed · Sector 28-B" right.

## Components used

See [components.md](../design-system/components.md).

- `Icon` — appears inside each menu item (18px). The icon animates to amber on hover alongside the rest of the item.
- No Button or Panel primitives; the menu items are plain `<button>` elements with custom styling.

## States & variants

Each menu item has three states:

**Default** — `--text-dim` text, no left border, 2px transparent left border reserved for layout stability, hint text hidden, bracket `›` hidden.

**Hover** — background lifts to `--bg-hover`, text goes `--text-bright`, the 2px amber left border appears, the item nudges right by increasing padding-left (`var(--space-4)` → `var(--space-6)`), the `›` bracket fades in, the hint text fades in right-aligned, icon turns amber. All transitions at `--dur-fast`.

**Primary** ("New Game") — always-visible amber glow on the label (`--accent-bright`, `text-shadow` with `--accent-glow`), `›` bracket always visible. The intent: the recommended action is pre-highlighted.

**Disabled** — 40% opacity, `cursor: not-allowed`, no hover response. Used for "Load Game," "Settings," "Credits" in the prototype.

## Interactions & transitions

- "New Game" → `go("scenario")`
- "Continue" → `go("game")`
- All other items are currently disabled stubs.

The menu has no entrance animation of its own; the content column appears immediately. Individual items respond to hover at `--dur-fast` (120ms) with the transition bundle described above.

## Copy

- Title: `WORLD-SIM`
- Tagline: `Prospecting Expedition 28-B`
- Nav kicker: `// Main Menu`
- Menu items (label · icon · hint):
  - New Game · play · "Begin a new expedition"
  - Continue · refresh · "Resume your last colony"
  - Load Game · save · "Restore a saved expedition" *(disabled)*
  - Settings · gear · "Graphics, audio, controls" *(disabled)*
  - Credits · users · "The crew behind the crew" *(disabled)*
  - Exit · close · "Quit to desktop"
- Footer right: `Star system: Maed · Sector 28-B`
