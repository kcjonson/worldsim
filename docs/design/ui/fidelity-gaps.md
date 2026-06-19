# UI fidelity gap analysis (prototype vs live game)

Systematic comparison of every implemented screen against the Salvage prototype
(`docs/ui-prototype/`, mockups in `docs/design/ui/mocks/`). Goal: match the
prototype near pixel-perfect, with allowances for screen-size variants.

**Out of scope:** the in-game gameplay tile/terrain rendering (the tan/green world
layer) is a placeholder and is not part of this pass. Everything else (UI chrome
and scene backdrops like the starfield and planet) is in scope.

PartySelect and ScenarioSelect exist in the prototype but have no C++ scene yet —
not analyzed (build, don't pixel-match).

---

## Cross-screen shared work (do first — highest leverage)

These appear on multiple screens; build once, reuse.

1. **Starfield backdrop** — missing on Splash, Main Menu, Worldgen, Landing. The
   prototype (`src/scene/Starfield.tsx`) draws ~220 far + ~55 near seeded dots
   over a faint nebula radial gradient, plus a vignette. Build a reusable
   `Starfield` render helper (seeded mulberry32, two `drawCircle` passes, gradient
   nebula, vignette) and call it as the first pass on each space screen. Per-screen
   seeds: splash 3, menu 11, worldgen/landing 5.
2. **Planet backdrop** — Main Menu shows a large planet docked off the right edge
   (640px, 160px clipped) on the starfield; Worldgen/Landing show the planet in the
   stage. Reuse the existing `GlobeView` (`apps/world-sim/scenes/shared/GlobeView`,
   already used by WorldCreatorScene) with a fixed seed + low subdivision for the
   menu backdrop, plus a `bg_void → transparent` scrim.
3. **`UI::Avatar` usage** — the EntityInfoView and the dossier use a plain gray
   rect placeholder; swap to the existing `UI::Avatar` primitive (mood-tinted,
   seeded).

---

## Splash  (`scenes/splash/SplashScene.cpp` vs `Splash.tsx`)

High:
- Title `fs_4xl` (52) → `fs_5xl` (72); letter-spacing `ls_wide` → `ls_widest`.
- Add the starfield + nebula background (currently bare `bg_void` clear).
- Add the boot log: up to 6 `[ OK ]` lines (status_ok prefix + text_faint body,
  mono fs_xs) building above the progress bar as progress crosses each 1/6.

Med:
- Center block anchor `0.45` → `0.5`; recompute the diamond/title/tagline/flavor
  vertical rhythm (diamond gap currently 84px, too large).
- Add the flavor quote (fontUi, fs_md, text_dim, italic, centered, max 460px).
- Add the done-state "Enter Expedition" button instead of auto-advancing at 100%.
- Phase label color text_dim → text; bar anchor `screenH-96` → `screenH-space_12`.

Low: progress % color accent → accent_bright; flavor font mono → ui; strip the
"– N defs" suffix from the phase string; pill radius on the progress bar.

## Main Menu  (`scenes/menu/MainMenuScene.cpp` vs `MainMenu.tsx`)

High:
- Add starfield (seed 11) + the docked planet backdrop on the right with a scrim.
- Title `fs_3xl` (38) → `fs_4xl` (52); letter-spacing `ls_wide` → `ls_wider`.

Med:
- Menu label font `fs_lg` → `fs_xl`; tagline color text_dim → accent.
- Reconcile items to the spec: New Game, Continue (disabled), Load Game (disabled),
  Settings, Credits (disabled), Exit; add a disabled state (40% alpha). Move/remove
  the dev-only "Quick Start".
- Per-item icons (18px, text_faint → accent on hover) in an icon column.

Low: hover bracket "›" glyph; kicker→items gap 30 → space_3; tagline
letter-spacing + uppercase.

## Worldgen  (`scenes/world-creator/WorldCreatorScene.cpp`, `ui/ParameterPanel.cpp` vs `WorldGen.tsx`)

High:
- Add starfield (seed 5, dim).
- Three-column grid: reserve 320px on the right for a **World Survey** panel
  (Stat grid + habitability + climate Meters + hazard Badges); stage shrinks
  accordingly.
- Replace the preset `Select` dropdown with the 2-column preset button grid
  (active = data-tinted).
- Title: "World Creator" (centered, 20px) → "Generate Planet" (left, 38px) + a
  kicker "// New Game · Step 03 / 03".

Med:
- Footer bar (Back left, phase status center, Generate/Regenerate/Accept right);
  move Generate/Cancel out of the parameter panel.
- Progress bar: center it, 16px → 3px, full-width → min(420, 80%), teal stage
  label (data_bright, mono 11px) + right-aligned %.
- Replace the mode-hint text with a `SegmentedControl` (Terrain/Biomes/Temp/Rain)
  docked stage-bottom-center.
- Show a decorative planet during the Configuring phase (not the bare placeholder).

Low: panel bg/border to bg_panel/line_edge; section labels to mono 10px ls_wider
uppercase text_dim; move the Resolution selector + advanced star/orbit sliders to a
dev/advanced area (not in the player spec).

## Landing site  (`scenes/landing/`, `WorldCreatorScene` reviewing overlay vs `LandingSite.tsx`)

Status: partially implemented — the details panel + tile selection exist, but the
whole screen shell (header, footer, two-column grid, confirmation modal) is absent;
landing currently renders as an overlay on the worldgen scene.

High:
- Restructure to the two-column grid (`1fr 360px`), no parameter panel in this
  phase; padding 24/32, gap 16.
- Header: kicker "// Expedition · Final Approach" + title "Select Landing Site"
  (38px) left; ghost "Back to Survey" button right. Remove the centered "World
  Creator" title + ESC/mode hints.
- Info panel: widen 270 → 360, wrap in the `Panel` component (title "Landing Zone",
  kicker "Site Analysis", accent) instead of a raw rect; add the biome-name header
  (Chakra Petch 22px).
- Footer: note left + "Confirm Landing Site" primary button right (hairline top).
- Confirmation modal ("Commit to Descent") on confirm, before `land()`.

Med: starfield; crosshair landing marker (pulsing accent ring) replacing the yellow
dot; crosshair hint top-center; SegmentedControl viz switcher; Recommended badge;
Stat row (temp range / rainfall); difficulty inset box with skull icons; coord
color data_bright + format "14.7°N  79.2°W".

## In-game HUD  (`scenes/game/ui/` vs `InGame.tsx`)  — priority screen

High:
- **Region minimap panel** — does not exist. Build `RegionMinimapPanel` (terrain
  blobs, river, grid, crash-site, colonist dots, viewport rect, coord label) as the
  first item in the right stack.
- **Storage panel has no data** — `ResourcesPanel` shows only an empty-state
  message; wire ECS stockpile/inventory via a new `ResourcesAdapter` and render
  resource rows (icon + name + right-aligned count).
- **EntityInfoView** — raw slot list, no tabs. Rebuild to the prototype: Avatar
  (52px) header + Needs/Bio/Gear/Log tabs + a Draft/Go-to/Priorities action row;
  swap the gray portrait rect for `UI::Avatar`.

Med:
- Right stack width: ResourcesPanel 160 → 232, GlobalTaskListView 300 → 232, right
  margin 20 → 12, top 120 → 60 (below the minimap).
- Roster card: add the task-progress Meter second row (label + accent progress).
- Command bar: add a **Zones** dropdown (Stockpile/Growing/Dumping); reconcile
  Furniture/Rooms; button height 28 → 36, bar 40 → 48.
- Zoom control: horizontal 4-button strip → vertical 3-button column, bottom-right.
- Top bar: add the alert button (amber bell + crit count badge) before Menu; flat
  bg_panel → bg_base gradient + the `◈` colony mark on the left.

Low: info panel left 0 → 12, width 340 → 320, name 14 → 15; roster amber strip
3 → 2; season text → Badge pill; command-bar keycap hints.

## Colonist dossier  (`scenes/game/ui/dialogs/ColonistDetailsDialog` + tabs vs `ColonistDetailsDialog.tsx`)

High:
- Dialog width 600 → 760 (size lg); enable `modal=true` (scrim); confirm the amber
  accent border.
- Kicker "Personnel File · {role}" in the header.
- Bio tab: header block = Avatar (72px) + a 2×2 Stat grid (Role/Origin/Age/Mood);
  real backstory paragraph (15px); trait Badge chips; current-task note in accent.
- Tabs: spec order Bio / Needs / Skills / Social / Gear / Log. Rename Health →
  Needs (Mood Meter + need rows); add Skills + Log; decide the fate of the non-spec
  Memory/Tasks debug tabs.
- Gear tab: rebuild as the 3-column paperdoll (worn slots / silhouette / back-belt)
  + Field Pack (weight readout + Meter + stack rows) + Tool Belt grid + carry Meter.

Med/Low: footer button row (Close/Work Priorities/Draft); title 14 → 18; content
padding 16 → 20; text_bright pure-white → #f4eee2; Chakra Petch/JetBrains Mono fonts
on labels.

---

## Execution roadmap

- **Wave 1 — quick wins** (one-line size/spacing/color/typography + panel widths):
  scattered across all screens, high visual impact, near-zero risk. Per-screen.
- **Wave 2 — shared elements**: the `Starfield` helper (4 screens) + `UI::Avatar`
  swaps + the menu/worldgen planet backdrop via `GlobeView`.
- **Wave 3 — structural**: in-game right panel (minimap + storage data), worldgen
  3-column + World Survey panel, landing two-column shell + confirmation modal,
  EntityInfoView tabs, dossier rebuild.

Each item is verified by screenshotting the live screen against its mockup.
