# Landing site

![Landing site selection](../mocks/landing-site.png)

The final step before the session begins. The player clicks anywhere on the planet's solid surface to set a descent vector, reads the local conditions for that spot, and then commits. The copy makes it unambiguous that this choice is permanent.

## Purpose & flow

Comes after [World generation](./world-generation.md) and leads to [In-game](./in-game.md). The header kicker reads `// Expedition · Final Approach`. The planet is the same seed used in WorldGen (2480 in the prototype), now non-rotating and click-interactive.

## Layout & regions

Full-bleed [Starfield](../../../ui-prototype/src/scene/Starfield.tsx) (seed 5, `dim`) behind a two-column CSS grid:

```
grid-template-columns: 1fr 360px
grid-template-rows: auto 1fr auto
grid-template-areas:
  "head  head"
  "stage info"
  "foot  foot"
padding: var(--space-6) var(--space-8)
gap: var(--space-4)
```

**Head** — kicker + h1 left, "Back to Survey" button (ghost, icon `chevronLeft`, navigates to worldgen) right.

**Stage** (left, flexible) — the planet globe centered, with a crosshair hint at top and the viz switcher at bottom.

**Info panel** (right, 360px) — a `Panel` titled "Landing Zone," kicker "Site Analysis," accent `accent`. Shows coordinates, biome name, recommendation badge, stats, difficulty, field report, and hazards.

**Foot** — footer note text left, "Confirm Landing Site" button right. No Back button; the Back is in the header.

## Components used

See [components.md](../design-system/components.md).

- `Badge` — Recommended (ok, icon `check`); hazard badges (warn, icon `alert`).
- `Button` — "Back to Survey" (ghost, icon `chevronLeft`); "Confirm Landing Site" (primary, stencil, icon `rocket`); "Begin Descent" (primary, stencil, icon `rocket`) in the confirmation modal; Cancel (secondary) in the modal.
- `Divider` — labeled sections in the info panel ("Field Report," "Hazards").
- `Icon` — crosshair (14px) in the coordinate display and the crosshair hint label; skull icons for difficulty pips.
- `Panel` — landing zone info panel (accent `accent`); the confirmation modal panel (accent `accent`, glow, corners).
- `SegmentedControl` — four-option viz mode switcher (Biomes/leaf, Terrain/mountain, Temp/temp, Rain/rain; tone `data`, size `sm`), docked at the bottom of the stage.
- `Stat` — Temp Range, Rainfall (two stats in a flex row).

## Stage region

The [Planet](../../../ui-prototype/src/scene/Planet.tsx) component (460px, seed 2480) with `rotate={false}`, a `marker` prop, and an `onPick` handler.

**Crosshair hint** — fixed at top-center of the stage, dimmed mono text: a crosshair icon + "Click the surface to set your descent vector." This is always visible.

**Landing marker** — a translucent crosshair widget on the globe at the picked location: a pulsing outer ring (`--accent`), a small filled dot, and four tick lines (SVG paths). The marker starts at `{ x: 0.43, y: 0.47 }` (the default landing position, which maps to the temperate zone). The player's click anywhere inside the planet disk updates the marker and the coordinate readout.

**Viz switcher** — `SegmentedControl` docked at the bottom of the stage, centered. Switching modes updates the planet overlay (biome coloring, temperature gradient, precipitation gradient).

## Info panel

Scrollable internally. Content in order:

**Coordinates** — mono teal text (`--data-bright`), crosshair icon inline. Derived from the marker's `x, y` (0–1 within the disk box):
- Latitude = `(0.5 - y) × 180`; suffix N/S.
- Longitude = `(x - 0.5) × 360`; suffix E/W.
- Format: `14.7°N  79.2°W` (two spaces between lat and lon, one decimal place).

**Biome name** — Chakra Petch, `--fs-xl`, bold. Mock value: "Temperate Coastal Shelf."

**Recommendation badge** — `Badge` tone `ok`, icon `check`, label "Recommended."

**Stat row** — Temp Range and Rainfall as `Stat` components (sm). Mock: "9°C – 22°C" and "Moderate" (data tone).

**Difficulty row** — inset box with "DIFFICULTY" label and five skull icons. Skulls at or below the difficulty value are `--status-warn`; skulls above are `--text-faint`. Mock difficulty: 2.

**Field Report** — `Divider` labeled "Field Report," then a paragraph. Mock: "Flat alluvial ground beside a river mouth. Fresh water, soft soil, scattered hardwood analogues. A kind place to crash."

**Hazards** — `Divider` labeled "Hazards," then `Badge` components (warn, icon `alert`). Mock hazards: "Seasonal storms," "Tidal flats."

## Confirmation modal

Clicking "Confirm Landing Site" sets `confirming = true`, which mounts a full-screen scrim (backdrop-filter blur 2px, `--scrim` background, `--z-modal` z-index) with a centered Panel.

The panel (440px wide, `Panel` with title "Commit to Descent," kicker "Confirm," accent `accent`, glow, corners) contains:

- Body text: "Your colony will begin at **{coords}** — {biome}. Once you commit, there is no turning back." The coordinate text is amber-bright mono.
- Actions row (right-aligned): Cancel (secondary) and Begin Descent (primary, stencil, icon `rocket`).

Clicking the scrim outside the panel dismisses it (sets `confirming = false`). Cancel does the same. Begin Descent → `go("game")`.

The scrim fades in (opacity 0 → 1, `--dur`). The panel scales and rises into place (`scale(0.96) translateY(8px)` → `scale(1) translateY(0)`, `--dur`, `--ease-out`).

## States & variants

The screen itself has no phases (unlike WorldGen). The only state change is the marker position as the player clicks around. The confirmation modal is additive, not a phase.

## Interactions & transitions

- Click inside planet disk → update marker position and coordinate readout.
- `SegmentedControl` → update planet visualization mode.
- "Back to Survey" → `go("worldgen")`
- "Confirm Landing Site" → show confirmation modal.
- Cancel / click scrim → hide confirmation modal.
- "Begin Descent" → `go("game")`

## Copy

- Kicker: `// Expedition · Final Approach`
- Title: `Select Landing Site`
- Stage hint: "Click the surface to set your descent vector"
- Footer note: "You can land anywhere on solid ground. Choose well — there is no second descent."
- "Confirm Landing Site" button
- Modal title: `Commit to Descent`
- Modal kicker: `Confirm`
- Modal body: "Your colony will begin at **{coords}** — {biome}. Once you commit, there is no turning back."
- "Begin Descent" button
