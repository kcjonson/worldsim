# World-Sim UI Prototype

An interactive design-system and pre-game-flow prototype, built in React so we can
iterate on look and feel quickly before committing anything to the C++ UI layer
(`libs/ui`). This is a sketchpad for the visual language, not production code.

## Run

```bash
cd docs/ui-prototype
npm install        # first time only
npm run dev        # http://localhost:5174
```

## The look: Salvage

A used-future ship terminal. Warm amber readouts and holographic teal data on a
near-black hull, sharp bracketed panels, hairline borders, scanlines, stencilled mono
labels. Dense and lived-in — Star Wars cockpit, not steampunk frontier. (Two other
directions, Atlas and Beacon, were explored and dropped; this is the chosen one.)

Pre-game screens are cinematic and spacious; in-game UI is information-dense (it has to
scale to thousands of colony tasks). The dev rail switches screens; **Esc** hides the
chrome so you can judge a screen full-bleed.

## Screens

- **Pre-game flow** — Splash → Main Menu → Scenario Select → Party Selection → Planet
  Generation → Landing Site. In-screen Back/Next walk the flow end to end.
- **In-Game** — a representative HUD: top bar + speed controls, colonist roster, minimap,
  storage and a dense scrollable task list, selected-colonist info with tabs, command bar
  with drop-up menus, severity-coded toasts, zoom. Double-click a colonist (or the eye
  icon) for the full dossier dialog.
- **Component Gallery** — a living style guide of every primitive.

## Structure

```
src/
  design-system/
    tokens.css            single source of truth — every color, spacing, type, shape,
                          and texture token lives on :root. Tune the look here.
    global.css            reset, base type, scrollbars, scanline/grain utilities
    primitives/           Button, Panel (+ compact), Modal, Meter, Slider, Stat, Badge,
                          Tabs, SegmentedControl, Avatar, Tooltip, Icon, Divider, KeyCap
  scene/                  Starfield backdrop + the holographic Planet
  screens/                one folder per screen (.tsx + .module.css)
  shell/                  the dev rail
  data/mock.ts            all placeholder content (scenarios, crew, world stats, tasks…)
```

## Iterating

- **Colors, spacing, type, shape:** edit `design-system/tokens.css`. Everything keys off
  CSS custom properties, so a change there ripples through every screen. The spacing scale
  is 4px-based with `--space-0-5` (2px) and `--space-1-5` (6px) half-steps for dense rows.
- **Dense panels:** pass `compact` to `<Panel>` for tighter HUD padding.
- **New screen:** add a folder under `screens/`, register it in `App.tsx` and the rail in
  `shell/DevShell.tsx`.

## Mapping back to the game

The tokens translate to `libs/ui/theme/Theme.h`, and the primitives line up with the
existing C++ components (`Button`, `Panel`, `ProgressBar` ≈ `Meter`, `Tabs`, `Icon`, etc.).
The pre-game screens map to the scenes in `docs/design/features/game-start-experience.md`.
