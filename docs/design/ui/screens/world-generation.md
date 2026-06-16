# World generation

![World / planet generation](../mocks/world-generation.png)

Step 03 of 03 in the new-game setup. The player configures planetary parameters, runs a simulated geological and atmospheric model, and inspects the resulting world before accepting it. The globe renders live in the center as generation progresses.

## Purpose & flow

Comes after [Party selection](./party-selection.md) and leads to [Landing site](./landing-site.md). Kicker: `// New Game · Step 03 / 03`. The screen has three internal phases — setup, generating, done — that change the available controls and the center stage.

## Layout & regions

Full-bleed [Starfield](../../../ui-prototype/src/scene/Starfield.tsx) (seed 5, `dim`) behind a three-column CSS grid:

```
grid-template-columns: 320px 1fr 320px
grid-template-rows: auto 1fr auto
grid-template-areas:
  "head  head  head"
  "params stage info"
  "foot  foot  foot"
padding: var(--space-6) var(--space-8)
gap: var(--space-4)
```

**Head** — kicker + h1 left, ghost close button (icon `close`, navigates back to party) right.

**Params panel** (left, 320px) — a `Panel` titled "Parameters," kicker "Survey Config," accent `data`. Contains presets grid, sliders, and seed row.

**Stage** (center) — the `Planet` component centered, with overlaid hints and controls.

**Info panel** (right, 320px) — a `Panel` titled "World Survey," kicker changes with phase. Empty placeholder before generation; fills with stats after.

**Foot** — three-cell flex row: Back button left, phase status text center, action button(s) right.

## Components used

See [components.md](../design-system/components.md).

- `Badge` — hazard badges in the info panel (tones `ok`, `warn`).
- `Button` — Back (secondary, icon `chevronLeft`); Generate/Generating (primary, stencil, icon `globe`, disabled during generation); ghost close (icon-only, icon `close`); Regenerate (ghost, icon `refresh`); Accept World (primary, stencil, iconRight `arrowRight`); seed randomize (secondary, icon-only, icon `dice`).
- `Icon` — star rating icons (5×, `name="star"`, `filled`, amber when < habitability, faint otherwise), spinner element in the progress bar.
- `Meter` — climate distribution rows (tone `data`, size `sm`).
- `Panel` — params panel (accent `data`), info panel.
- `SegmentedControl` — visualization mode switcher (4 segments: Terrain/mountain, Biomes/leaf, Temp/temp, Rain/rain; tone `data`, size `sm`). Appears only after generation starts.
- `Slider` — 6 parameter sliders. See parameter table below.
- `Stat` — world stats in a 2-column grid (labels: Land, Water, Continents, Avg Temp, Largest Sea, Volcanism).

## Parameters panel

**Presets** — 2-column grid of small preset buttons. Active preset highlighted in teal (`--data-bright` text, teal border, teal-tinted background). Selecting a preset overwrites the six sliders with preset values (other sliders not in the preset's partial object retain their current values).

| Preset | Water | Temp | Atmosphere |
|---|---|---|---|
| Earth-Like | 62% | 14°C | 1.0 atm |
| Desert World | 18% | 31°C | 0.7 atm |
| Ocean World | 91% | 19°C | 1.3 atm |
| Frozen World | 55% | −14°C | 0.6 atm |
| Volcanic World | 34% | 27°C | 1.8 atm |
| Ancient Garden | 58% | 21°C | 1.1 atm |

**Sliders** (all use the `Slider` component with a `detent` mark):

| Label | Min | Max | Unit | Default | Detent (fraction) |
|---|---|---|---|---|---|
| Water Coverage | 0 | 100 | % | 62 | 0.62 |
| Tectonic Plates | 2 | 30 | — | 9 | 0.28 |
| Atmosphere | 0.1 | 3 | atm | 1.0 | 0.31 |
| Day Length | 8 | 48 | h | 26 | 0.45 |
| Planet Age | 0.5 | 10 | Gy | 4.5 | 0.45 |
| Mean Temp | −20 | 40 | °C | 14 | 0.57 |

**Seed row** — an inset read-only box shows the current seed (4-digit number, `--data-bright`, mono). A dice icon button randomizes it (picks a new number in 1000–9999).

## Stage region

The [Planet](../../../ui-prototype/src/scene/Planet.tsx) SVG component (440px, seed = current seed) is centered in the stage area.

**Setup phase** — planet renders at full opacity, slowly rotating. A dimmed hint text reads "Configure the survey, then run generation." No viz switcher.

**Generating phase** — `scan` prop enabled; a horizontal sweep line animates top-to-bottom across the planet disk. Land continents reveal progressively as `progress` rises from 0 to 1 (the `landReveal` value clamps the opacity of the land layer). The viz switcher remains hidden. A progress bar appears below the globe: phase label (teal, mono, with a 10px spinning border circle) left, percentage right, 3px teal bar fills left-to-right with teal glow.

**Done phase** — scan stops, planet at full opacity. The viz switcher (`SegmentedControl`) appears docked at the bottom of the stage, letting the player toggle between four map modes. Land reveal is 1.0.

## Info panel

**Setup / Generating** — empty state: globe icon, paragraph "Survey data resolves as the world is generated." Kicker: "Pending."

**Done** — Kicker: "Complete." Content in order:
- 2×3 `Stat` grid: Land 38%, Water 62% (data tone), Continents 4, Avg Temp 14°C, Largest Sea "Maed Belt," Volcanism "Low" (ok tone).
- Habitability row: inset box, "HABITABILITY" label left, 5 amber star icons right (4 of 5 filled in the mock).
- Climate distribution: "CLIMATE DISTRIBUTION" label above five labeled `Meter` bars (Temperate 35%, Tropical 20%, Desert 20%, Boreal 15%, Polar 10%, all `data` tone).
- Hazard badges: Water: Good (ok), Arable: 25% (ok), Storms: Moderate (warn).

## Generation phases (9 steps, in order)

Displayed sequentially in the progress bar label as percentage advances:

1. Generating tectonic plates
2. Simulating plate movement
3. Raising terrain from collisions
4. Modeling atmospheric circulation
5. Calculating precipitation & rivers
6. Forming oceans and seas
7. Assigning biomes
8. Calculating snow and glaciers
9. Finalizing world data

## Footer

**Setup** — Back left, "Awaiting generation" center (dim mono), Generate button right (primary, stencil, icon `globe`).

**Generating** — Back left, "Generating world…" center, Generate button right (disabled, label "Generating…").

**Done** — Back left, "World ready · suitable for colonization" center (ok-status color, check icon), two buttons right: Regenerate (ghost, icon `refresh`) and Accept World (primary, stencil, iconRight `arrowRight`).

## Interactions & transitions

- Preset click → writes preset values to slider state, updates planet seed's implied shape (the planet re-renders on seed change, not parameter change; the visual is purely illustrative during setup).
- Slider drag → updates the relevant parameter.
- Dice button → picks a new random seed, planet rerenders with the new seed's continent layout.
- Generate → transitions to "generating" phase, starts the interval, disables the button.
- Accept World → `go("landing")`
- Regenerate → restarts the generation interval from 0.
- Back (any phase) → `go("party")`
- Ghost close → `go("party")` (same as Back)

## Copy

- Kicker: `// New Game · Step 03 / 03`
- Title: `Generate Planet`
- Stage hint (setup): "Configure the survey, then run generation."
- Footer center (setup): "Awaiting generation"
- Footer center (generating): "Generating world…"
- Footer center (done): "World ready · suitable for colonization"
- Info panel empty: "Survey data resolves as the world is generated."
