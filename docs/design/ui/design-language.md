# Salvage — UI design language

The interface look and feel for World-Sim, approved off the React prototype under `docs/ui-prototype/`. This doc is the canonical description; the prototype is the live sandbox. Exact values live in [design-system/tokens.json](./design-system/tokens.json) (machine-readable) and [design-system/tokens.md](./design-system/tokens.md) (readable tables).

This is interface chrome only, the panels, readouts, and controls the player reads through. For terrain and world art direction see [visual-style.md](../visual-style.md); the two surfaces are deliberately separate.

## What it is

A used-future ship terminal. Warm amber readouts and holographic teal data on a near-black hull, sharp bracketed panels, hairline borders, scanlines, stencilled mono labels. Dense and lived-in. Star Wars cockpit, not steampunk frontier, not clean Apple-store minimalism. The player is reading instruments on salvaged equipment.

The aesthetic earns its keep by serving density: a colony throws thousands of tasks and dozens of colonists at the screen, and the design has to stay legible when packed. Sharp edges, tight spacing, monospace numerics, and a strict two-accent palette are what make a busy HUD scannable.

## Principles

- **Legible when dense.** The HUD is the hard case. Tight half-step spacing, single-line rows, monospace values that align. Never style for the empty screen.
- **Two accents, used with meaning.** Amber (`--accent`) is interactive and "your attention here." Teal (`--data`) is read-only data and diagnostics. Everything else is hull, lines, and text. Color carries signal, not decoration.
- **Sharp, not soft.** 2px radii at most on panels, hairline borders, bracketed corners. Roundness reads as consumer-soft; Salvage is hard-edged equipment.
- **Texture is a whisper.** Scanlines at 5%, grain at 4%. Present, never loud. The screen should feel like a CRT under glass, not a glitch effect.
- **Stencilled labels, humanist body.** Uppercase letter-spaced mono for labels and kickers, a humanist sans for reading text. The contrast between the two is a lot of the character.

## Palette

The identity is two warm/cool accents over a cold near-black hull. Full values and the normalized rgba floats are in [tokens.json](./design-system/tokens.json).

| Role | Token | Value | Use |
|------|-------|-------|-----|
| Accent (amber) | `--accent` | `#e8a33e` | interactive, primary actions, "look here", warnings |
| Accent bright | `--accent-bright` | `#ffc56b` | hover/active lift on accent |
| Data (teal) | `--data` | `#45c7c0` | read-only data, stats, diagnostics, info |
| Data bright | `--data-bright` | `#7be6df` | hover/active lift on data |
| Hull (void→raised) | `--bg-void` … `--bg-panel-raised` | `#07080b` → `#181c24` | background to elevated panels |
| Status ok / warn / crit / info | `--status-*` | `#5fb87a` / `#e8a33e` / `#e0533c` / `#45c7c0` | semantic state |

Amber doubles as `--status-warn` and teal as `--status-info` by design: warning is "attention" and info is "data," so they share the accent hues rather than introducing new ones.

### Surfaces and elevation

Five hull tints stack from deepest to most-raised, with three line weights to separate them: `--bg-void` (app background), `--bg-base`, `--bg-panel` (normal panel), `--bg-panel-raised` (prominent panel), `--bg-inset` (wells, inputs, tracks). Separation comes from `--line-hairline` (0.1 alpha), `--line-edge` (0.2), and `--line-strong` (0.36), cool blue-grays rather than pure white, plus the panel shadow. Elevation is shown by tint + line weight + shadow, not by large blurs.

## Typography

Three families, each loaded at fixed weights via @fontsource. The C++ build vendors the matching TTFs; these exact names and weights are the contract.

| Role | Token | Family | Weights | Where |
|------|-------|--------|---------|-------|
| Display | `--font-display` | Chakra Petch | 400, 500, 600, 700 | titles, headings, menu labels, stat values; uppercase + `--title-spacing` for titles |
| UI / body | `--font-ui` | Barlow | 400, 500, 600 | reading text, descriptions, default UI; base size 13px / line-height 1.4 |
| Mono | `--font-mono` | JetBrains Mono | 400, 500, 700 | numeric values, kickers, stencilled labels (uppercase + `--label-spacing`) |

Base body is Barlow 13px, `--text` on `--bg-void`. Headings (h1–h5) are Chakra Petch 600, `--text-bright`, tight line-height. The type scale runs `--fs-2xs` (10px) to `--fs-5xl` (72px); see tokens.

## Texture and effects

Reach for the lightest treatment that reads. Opacities are tokens so the whole system tunes from one place.

- **Scanlines** (`.fx-scanlines`, `--scanline-opacity: 0.05`) — 1px-on / 2px-off horizontal repeating gradient, `mix-blend-mode: overlay`, on panels and screens that want the CRT read.
- **Grain** (`.fx-grain`, `--grain-opacity: 0.04`) — fine fractal-noise sensor grain.
- **Vignette** (`--vignette-opacity: 0.55`) — edge darkening on full-screen scenes.
- **Flicker** (`@keyframes fx-flicker`) — occasional holographic dropout on glowing accents, used sparingly.
- **Glow** — colored bloom via `--accent-glow` / `--data-glow` on active interactive elements. In CSS this is `box-shadow`; the C++ port approximates it (see "What won't port literally").

## Motion

Quick and eased, never bouncy. Durations `--dur-fast` 120ms / `--dur` 200ms / `--dur-slow` 360ms; standard ease `cubic-bezier(0.4, 0, 0.2, 1)` and an emphasized `--ease-out` `cubic-bezier(0.16, 1, 0.3, 1)` for things entering. Meters animate their fill on `--dur-slow`. All motion collapses under `prefers-reduced-motion`.

## Interaction states

- **Hover** — surface lifts with `--bg-hover`; accent/data elements brighten toward their `-bright` and gain glow.
- **Active/pressed** — interactive elements nudge down 1px; segmented/tab selection fills with the accent.
- **Focus (keyboard)** — `:focus-visible` only: 1px `--accent` outline, 2px offset. No focus ring on mouse.
- **Disabled** — `--text-disabled`, no glow, no hover response.
- **Selection** — text selection uses `--accent-glow` background, `--text-bright` text.

## What won't port literally

A handful of tokens are CSS effects with no static value; they're flagged `cssOnly` in tokens.json and the C++ work handles them deliberately rather than copying:

- `color-mix(...)` blends → precompute to a static rgba where inputs are static, else blend in-shader.
- `box-shadow` glow and multi-layer panel shadow → approximate with halo rects / a bloom pass.
- `repeating-linear-gradient` scanlines and the grain noise → procedural overlay or shader; only the opacity scalars port.
- `cubic-bezier` eases → reconstruct from the four control points in the engine's tween system.
- font-family stacks → the role→family names port; real multi-font rendering is a C++ font-system task.

## Related

- [tokens.md](./design-system/tokens.md) / [tokens.json](./design-system/tokens.json) — the values
- [components.md](./design-system/components.md) — the primitive catalog that uses them
- [icons.md](./design-system/icons.md) — the line-icon set
- [screens/INDEX.md](./screens/INDEX.md) — how screens compose it
- [visual-style.md](../visual-style.md) — terrain/world art direction (separate surface)
