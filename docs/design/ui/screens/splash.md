# Splash

![Splash / loading screen](../mocks/splash.png)

The first thing the player sees: a full-screen boot sequence that simulates system initialization, then yields to a call-to-action button. It exists to establish tone before the player does anything, and to mask any real loading that needs to happen.

## Purpose & flow

The splash screen is the entry point of the application. It runs automatically, plays through the boot log, then reveals a "Enter Expedition" button. Clicking that button navigates to [Main Menu](./main-menu.md).

## Layout & regions

The screen is a full-bleed layer with three zones stacked vertically:

- **Background** — an animated [Starfield](../../../ui-prototype/src/scene/Starfield.tsx) fills the entire screen (seed 3). Two layers of procedurally placed stars (far field: 220 stars, near field: 55 stars) over a nebula div and a vignette + grain overlay.
- **Center** — the identity block, vertically and horizontally centered. The diamond glyph `◈` sits above the title, tagline, and a random flavor quote.
- **Bottom strip** — a fixed-width column (min(520px, 70vw)) pinned 48px from the bottom. While loading it shows the scrolling boot log and a progress bar; once complete it shows the primary CTA button.
- **Version** — bottom-right corner, always visible.

## Components used

See [components.md](../design-system/components.md) for full specs.

- `Button` (variant `primary`, size `lg`, stencil, icon `play`) — the "Enter Expedition" CTA, replaces the loader when `progress >= 100`.
- No other design-system components; the loader and boot log are bespoke to this screen.

## States & variants

Two phases, driven by an interval that increments progress from 0 to 100:

**Loading** — the boot log grows line by line. Each line appears with a `rise` animation (fade up from 10px). The current phase label updates as each stage completes. A 3px progress bar fills left-to-right in amber; the fill has an amber glow. The percentage counter sits right-aligned above the bar.

**Done** (`progress >= 100`) — the loader disappears, replaced by the "Enter Expedition" button with its own `rise` entrance animation.

The six boot lines reveal in order, driven by `Math.floor((progress / 100) * 6) + 1`:
1. NAV-CORE online
2. Mounting salvage manifest
3. Calibrating sensor grain
4. Loading vector atlas
5. Spinning up world cache
6. Crew vitals nominal

Each completed line is prefixed with `[ OK ]` in `--status-ok` green.

## Interactions & transitions

The boot sequence runs without player input. The only interactive element is the button that appears at completion. Clicking "Enter Expedition" calls `go("menu")`, transitioning to the main menu.

No back navigation; the splash screen has no way out except forward.

## Copy

- Title: `WORLD-SIM`
- Tagline: `Prospecting Expedition 28-B` (mono, uppercase, amber)
- Flavor quotes (one chosen at random):
  - "The planet is earth-like. That's the good news."
  - "Rescue is eighteen years out. Best not to count."
  - "Everything you'll need is already here. It's just not yours yet."
  - "Other ships came down before you. Some were not human."
- CTA button label: `Enter Expedition`
- Version string: bottom-right corner (e.g. `v0.1.0-proto`)

## Notes

The `◈` glyph pulses with a `fx-flicker` animation (6s infinite), giving it the holographic dropout feel described in the [design language](../design-language.md#texture-and-effects). The title has an amber text-shadow glow via `color-mix`. Both are CSS-only effects that need approximation in the C++ port.

The progress increment is randomized per tick (`1.4 + random * 2.2` every 60ms), so the bar feels organic rather than mechanical.
