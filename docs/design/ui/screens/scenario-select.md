# Scenario select

![Scenario select](../mocks/scenario-select.png)

Step 01 of 03 in the new-game setup. The player picks an expedition scenario, which sets the starting conditions — wreck type, available salvage, party size, and world difficulty — before moving on to crew selection.

## Purpose & flow

This screen comes after [Main Menu](./main-menu.md) ("New Game") and leads to [Party selection](./party-selection.md). The kicker reads `// New Game · Step 01 / 03`. Exactly one scenario is always selected; the prototype defaults to "standard."

## Layout & regions

Full-bleed [Starfield](../../../ui-prototype/src/scene/Starfield.tsx) (seed 42, `dim`) behind a column layout that fills the viewport:

```
padding: var(--space-12) var(--space-16)
gap: var(--space-8)

[header]
[card grid]                 ← flex: 1
[footer bar]
```

**Header** — kicker, h1 title, short subtitle paragraph.

**Card grid** — five cards in a `grid-template-columns: repeat(5, 1fr)` grid, stretching to fill the available height.

**Footer bar** — hairline top border, Back button left, "Confirm Crew Briefing" button right.

The whole layout enters with a `rise` animation (fade up 12px, `--dur-slow`).

## Components used

See [components.md](../design-system/components.md).

- `Badge` (tone `outline`) — scenario tags displayed at the bottom of each card (e.g. "Balanced," "Recommended," "Brutal").
- `Icon` — scenario icon (28px, strokeWidth 1.4) at the top of each card.
- `Button` (variant `secondary`, icon `chevronLeft`) — Back; `Button` (variant `primary`, stencil, iconRight `arrowRight`) — Confirm Crew Briefing.

## Scenario cards

Each card is a `<button>` styled as a selectable panel. Content top to bottom:

1. Scenario icon (28px)
2. Name — Chakra Petch, uppercase, `--fs-md`
3. Blurb — Barlow `--fs-sm`, `--text-dim`, grows to fill available space (`flex: 1`)
4. Meta row — difficulty pips left, party label right
5. Tags — `Badge` components, wrapping

**Difficulty pips** — five horizontal bars (14×4px). Color is determined by scenario difficulty and pip position: pips at or below the difficulty value take a status color (`ok` for ≤2, `warn` for ≤3, `crit` for 4–5); pips above the value are `--text-faint`.

**Party label** — mono, `--fs-2xs`, users icon inline.

**Corner ticks** — four L-shaped hairline marks at the card corners (10×10px, borderless), hidden at 50% opacity by default. On hover they brighten toward `--accent-dim`; on selection they go fully amber and opaque.

## States & variants

**Default** — `--bg-panel`, hairline border, corner ticks dimmed.

**Hover** — `--bg-panel-raised`, `--line-strong` border, `--shadow-panel`. Corner ticks appear at `--accent-dim`.

**Selected** — `--bg-active`, amber border, double amber glow (`0 0 0 1px var(--accent), 0 0 22px var(--accent-glow), var(--shadow-panel)`), corner ticks full amber, icon turns amber. A filled amber circle with a check icon appears top-right.

## Interactions & transitions

Clicking a card sets it as selected; any previous selection clears. Selection state is local (no server round-trip).

- Back → `go("menu")`
- Confirm Crew Briefing → `go("party")`

## Copy

- Kicker: `// New Game · Step 01 / 03`
- Title: `Select Scenario`
- Subtitle: "Each scenario reshapes your wreck site, salvage, and the world you'll fight to survive."
- Footer Back label: `Back`
- Footer forward label: `Confirm Crew Briefing`
- Scenarios:

| Name | Blurb | Difficulty | Party |
|---|---|---|---|
| Standard Colony | "A balanced wreck site with workable salvage and a temperate landing band. The recommended way in." | 2 | 3 survivors |
| Harsh World | "Thin atmosphere, scarce water, a climate that does not negotiate. Salvage is light. For veterans." | 4 | 3 survivors |
| Rich Resources | "Dense ore, lush flora, intact cargo pods scattered nearby. A forgiving economy to learn the ropes." | 1 | 4 survivors |
| Lone Survivor | "One escape pod. One person. Everything else burned on re-entry. The hardest story we tell." | 5 | 1 survivor |
| Large Expedition | "A full survey crew rode the wreck down. More hands, more mouths, more politics. Sandbox-leaning." | 3 | 8 survivors |
