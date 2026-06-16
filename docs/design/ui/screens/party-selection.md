# Party selection

![Party selection](../mocks/party-selection.png)

Region anatomy:

![Party selection regions](../mocks/party-selection-anatomy.svg)

Step 02 of 03 in the new-game setup. The player reads through the survivors who walked away from the wreck — their backgrounds, skills, and traits — before committing to the expedition. The screen is read-only; the crew is fixed by the chosen scenario.

## Purpose & flow

Comes after [Scenario select](./scenario-select.md) and leads to [World generation](./world-generation.md). Kicker: `// New Game · Step 02 / 03`. The prototype loads the three colonists from `COLONISTS` in mock.ts, with "Mara Vance" active by default.

## Layout & regions

Full-bleed [Starfield](../../../ui-prototype/src/scene/Starfield.tsx) (seed 57, `dim`) behind a column layout:

```
padding: var(--space-8) var(--space-10)
gap: var(--space-5)

[header]
[main: roster column | detail panel]   ← flex: 1
[footer bar]
```

The main area is a two-column grid: `280px` roster column on the left, `1fr` detail panel on the right, with a `var(--space-5)` gap.

**Header** — kicker, h1, subtitle.

**Roster column** — scrollable list of colonist cards, plus an actions row at the bottom (Randomize button, slot count).

**Detail panel** — a `Panel` component (variant `raised`, accent `accent`, corners, flush) showing the full dossier for the active colonist. Scrollable internally.

**Footer bar** — hairline top border, Back left, "Generate Planet" primary right.

## Components used

See [components.md](../design-system/components.md).

- `Avatar` — 40px on each roster card (seed = colonist name, mood-tinted), 72px in the detail panel hero.
- `Badge` — trait chips in the detail panel, tone driven by trait tone: `ok` for "good," `crit` for "bad," `default` for "neutral."
- `Button` — Randomize (variant `data`, icon `dice`); Back (variant `secondary`, icon `chevronLeft`); Generate Planet (variant `primary`, stencil, iconRight `arrowRight`).
- `Divider` — labeled separators between sections in the detail panel ("Background," "Skills," "Traits").
- `Icon` — skill icons in the skill rows.
- `Meter` — mood meter on each roster card (tone `auto`, size `sm`); skill level meters in the detail panel (tone `accent`, size `sm`, `value = skill.level / 20`, `valueText = level as string`).
- `Panel` — the detail panel (raised, accent, corners, flush).
- `Stat` — three stats in the detail hero row: Origin, Age (unit "yrs"), Mood (tone `crit/warn/ok` based on value).

## Roster cards

Each card is a `<button>` with corner-tick decoration matching the scenario select pattern (6×6px ticks, hidden by default, visible on hover/active). Three-column grid inside: `40px | 1fr | auto`.

- Avatar (40px, mood-tinted, selected ring when active)
- Name (Chakra Petch, `--fs-md`, `--text-bright`) + role (mono, uppercase, `--text-faint`)
- Mood meter (size `sm`, tone `auto`) + mood label (one of: Distressed, Uneasy, Stable, Content)

**Default** — `--bg-panel`, hairline border, ticks hidden. **Hover** — `--bg-hover`, `--line-edge` border. **Active** — `--bg-active`, amber border, amber glow + inner amber tint, amber corner ticks.

The roster list is scrollable (`overflow-y: auto`) to accommodate larger party sizes.

The actions row below the list shows the Randomize button and a "3 / 3 slots" label (users icon + mono text). Randomize shuffles the display order and sets the first card active; the crew composition itself doesn't change.

## Detail panel

The panel's inner content scrolls. Sections in order:

**Hero** — Avatar (72px) beside name (Chakra Petch, `--fs-2xl`, bold) and role (mono, uppercase, amber). Hairline bottom border separates this from the stats.

**Stat row** — Origin, Age, Mood in a flex row using `Stat` components.

**Background** — `Divider` labeled "Background," then an italic paragraph (`--text-dim`, loose line height) with the colonist's backstory.

**Skills** — `Divider` labeled "Skills," then a list of skill rows. Each row: icon (14px, `--text-faint`), name (mono, uppercase, 140px fixed), `Meter` (tone `accent`, value = level/20, `valueText` = level number). Skill level is on a 0–20 scale.

**Traits** — `Divider` labeled "Traits," then trait `Badge` components in a wrapping flex row.

## States & variants

The only state change is which colonist is active; everything else is static. Randomize reorders the roster and immediately activates the new first card.

## Interactions & transitions

- Click a roster card → set that colonist as active, update detail panel instantly.
- Randomize → shuffle roster array, set first card active.
- Back → `go("scenario")`
- Generate Planet → `go("worldgen")`

No entrance animation on the roster itself; the layout appears immediately.

## Copy

- Kicker: `// New Game · Step 02 / 03`
- Title: `Assemble the Crew`
- Subtitle: "Three survivors walked away from the wreck. Learn who they are."
- Randomize button: `Randomize`
- Slot count: `3 / 3 slots`
- Back: `Back`
- Forward: `Generate Planet`

### Colonist roster (mock data)

**Mara Vance** — Flight Engineer, Outpost 28-B, age 34, mood 0.72. Skills: Construction 14, Crafting 11, Mining 9, Medicine 4, Cooking 6, Research 8. Traits: Steady Hands (good), Insomniac (bad), Hauler (neutral).

**Idris Okonkwo** — Field Medic, Kessler Station, age 41, mood 0.58. Skills: Medicine 16, Research 12, Cooking 9, Social 11, Construction 3, Mining 2. Traits: Compassionate (good), Squeamish Mining (bad).

**Rin Calloway** — Botanist, Greenhouse Collective, age 28, mood 0.81. Skills: Growing 15, Cooking 12, Research 10, Animals 8, Medicine 6, Construction 5. Traits: Green Thumb (good), Optimist (good), Frail (bad).

### Mood labels

| Range | Label |
|---|---|
| < 0.30 | Distressed |
| 0.30 – 0.54 | Uneasy |
| 0.55 – 0.74 | Stable |
| ≥ 0.75 | Content |
