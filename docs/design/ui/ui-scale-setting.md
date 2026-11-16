# Interface scaling

**Status:** built

The interface lays out on a canvas whose size comes from the window's resolution, and the player's Interface size setting (60 to 120%) multiplies the scale. A ui px (the unit the design tokens, `fs_*`, `space_*` and component sizes are written in) is a pixel on a 1600 x 1000 screen, the prototype mocks' stage, so at 100% a 16:10 display shows the mock exactly and a 16:9 one shows the mock with more room to the right. The mechanism is salal's ui scale; see "UI scale" in salal's `docs/app-api.md` for the engine side. This doc is the numbers worldsim passes it, and why.

## The formula

`s = max(floor, f x min(W / referenceWidth, H / referenceHeight))`

`W x H` is the framebuffer in device pixels, `f` the player's factor (saved as `ui.scale`), and `s` the device pixels per ui px. The UI lays out on a canvas of `W/s x H/s` ui px. The operating system's display scale doesn't enter into it: a 4K monitor at 100%, 150% or 200% gives the same interface.

The numbers live in `libs/theme/theme/InterfaceScale.h`, and world-sim, ui-sandbox and asset-manager all pass them (`theme::uiScaleConfig()`, `theme::kMinWindowWidth` and `kMinWindowHeight`).

| Number | Value |
|---|---|
| Reference | 1600 x 1000 |
| Floor | 0.8 |
| Interface size | 60 to 120% in steps of 10, default 100% |
| Smallest window | 1280 x 720 device px |
| Frame | none, the play area takes whatever room the window has |

## Common screens at 100%

| Screen | Device px | s | Canvas (ui px) |
|---|---|---|---|
| 1080p | 1920 x 1080 | 1.08 | 1778 x 1000 |
| 1440p | 2560 x 1440 | 1.44 | 1778 x 1000 |
| 4K | 3840 x 2160 | 2.16 | 1778 x 1000 |
| Ultrawide | 3440 x 1440 | 1.44 | 2389 x 1000 |
| 16:10 laptop | 2560 x 1600 | 1.60 | 1600 x 1000 |
| 16" MacBook Pro | 3456 x 2234 | 2.16 | 1600 x 1034 |
| Steam Deck | 1280 x 800 | 0.80 | 1600 x 1000 |
| Smallest window | 1280 x 720 | 0.80 (floor) | 1600 x 900 |

By hand, three rows. 1920 x 1080 gives min(1.2, 1.08) = 1.08, and 1920 / 1.08 = 1778. 3440 x 1440 gives min(2.15, 1.44) = 1.44, and 3440 / 1.44 = 2389. 1280 x 720 gives min(0.8, 0.72) = 0.72, which the floor lifts to 0.8, so the canvas is 1600 x 900.

## Why these numbers

The floor is about type. The smallest type token is 10 px and body is 13 px, so at 0.8 they're 8 and 10.4 device px. dual-deck-builder's floor of 2/3 would put captions at 6.7, which doesn't read. The floor only holds if the window can't get smaller than the size it was worked out for, so the 1280 x 720 minimum window is part of the same decision.

The largest step, 120%, is the least room the HUD and dialogs are laid out for: a 1481 x 833 canvas on 16:9 and 1333 x 833 on 16:10. The design doc's old "75%-150%" line predates the reference. 150% would be a 1185 x 667 canvas on 16:9, 49% of the mock's area, and 1067 x 667 on 16:10, 44%, so it isn't offered.

The smallest step is there so a 4K player can get back near the density the game had before scaling. In a 3072 x 1728 window the old look is about 58%, and 60% on 4K is a 2963 x 1667 canvas. On a 1080p screen the floor takes over under about 75% (0.8 / 1.08 is 0.74), so the lowest steps change nothing there. That's accepted. It's worse in a window smaller than the screen: at the default window on a 1080p monitor (about 1536 x 826), 60 to 90% all give 0.8 and 100% gives 0.826, and at 1280 x 720 only 120% differs. Even on a full 1080p screen 60 and 70% are the same (both 0.8). The Interface size row's description in Settings says so.

## The model

Linear: the same proportions on every screen, which is what Unity's Canvas Scaler (Scale With Screen Size, Expand), Godot's `canvas_items` stretch with `expand` (Stretch Scale as the player's multiplier) and Unreal's DPI scaling give a game. A large monitor shows more world around a HUD of the same proportions, and the player's setting is what makes the HUD bigger or smaller on it.

Considered and rejected:

- A resolution-to-scale curve, which is a second thing to tune per screen and gives two players on different monitors different proportions.
- A default picked per screen at first run, which makes the saved value mean different things on different machines.
- Following the OS display scale, which is Godot's advice for non-game applications. A game that knows its own reference doesn't need it, and it would give the same monitor a different interface at 150% than at 200%.

## How the original requirements are met

- Single global factor: `App::setUiScale` in salal, one factor for every scene, configured by `uiScaleConfig()` in `libs/theme/theme/InterfaceScale.h`. No per-screen constants.
- Layout, not zoom: the canvas is `W/s x H/s`, so screens reflow to the room they have. Scenes lay out in ui px.
- Input stays correct: salal hit-tests in ui px. World picking, the camera, drawing and placement previews and overlays stay in window pixels, and the places that mixed the two were fixed to convert: the hover gate that skips the world while the pointer is over the ui (`GameScene.cpp`), the minimap's view rectangle (`GameUI.cpp`), and the world creator's globe and marker (`WorldCreatorScene.cpp`, `GlobeView.h`).
- Live: Settings calls `App::setUiScale`, which re-lays-out the open scene at once. With the salal tag worldsim pins, scenes' `onResize` runs on a scale change too.
- Persisted: `settings.json` (`apps/world-sim/Main.cpp` passes it as the App's settings file) holds `ui.scale` and the window's size. `--no-settings` neither reads nor saves it, which perf captures use.
- World text: text drawn in the world that a player reads as interface (the drawing tool's validation message, room names) is sized by the layout scale, so it follows Interface size while the world around it keeps its own.
- Crisp: the scale reaches the renderer, so text and tessellated shapes are drawn at the device size they end up at, not stretched from a smaller one.
- Bounds and reset: `kInterfaceSizes` holds the factor to 0.6 through 1.2, and the Interface size row in `apps/world-sim/scenes/settings/SettingsScene.cpp` is a segmented control over the seven stops with a Reset button back to 100%. A saved value that isn't a stop is snapped to the nearest one (`nearestInterfaceSize`) at startup, and Settings shows that stop. Settings is reached from the main menu only.

The colonist dossier is 760 x 900 ui px held to 92% x 86% of the canvas, its tab area scrolling in what's left (`ColonistDetailsDialog.cpp`), so it fits the smallest canvas.

## What the tests cover

`apps/world-sim/scenes/SceneLayout.test.cpp`, over `SceneTesting.test.h`, has 12 tests. Each lints its screen at five window sizes (1280 x 720, 1600 x 1000, 1920 x 1080, 3440 x 1440, 1600 x 1200) and three interface sizes (120%, 100%, 60%), through salal's `app::lintAt`, and checks that nothing shows a scrollbar except what's meant to scroll. Each first asserts that a known id is present and that its dialogs report visible, so a test can't pass over an empty screen.

- The main menu, settings, scenario select, party select and the world creator (configuring, before a world exists).
- The HUD, plain.
- The HUD with the build menu, the colony task list, the decision inspector and five toasts open.
- The HUD with the wall tool on.
- The colonist dossier, on each of its eight tabs.
- The crafting, storage and pause dialogs over the HUD.

Scrollbars: `tasks_scroll` and the dossier's tab area may scroll at every size. The settings list's bar is allowed only on canvases under 1200 ui px tall. It does scroll at the default size on a 16:9, 16:10 or 21:9 screen, where the canvas is 1000 ui px tall, because the list is long (a row each for the interface size, the grid and ten switches, every one with a description); it fits from 1200.

Scenario `docs/testing/scenarios/17-interface-size.md` is the by-hand pass: the setting applies at once, is saved, and the HUD holds at 60 and 120%.

Not linted: the splash, loading, the game scene itself (the world under the HUD), and the world creator's later states, which need a world to exist.

The test executable builds to `apps/world-sim/tests/<config>/world-sim-tests.exe`.

## Checking a scene at a size

1. Run the app and read `/api/state?what=uiscale` for `{factor, layoutScale, pixelScale}`.
2. `/api/ui/tree` and `/api/ui/lint` are in ui px. `/api/input` is in the window's logical px, so a position from the tree times `layoutScale` is where to point it.
3. Launch with `--no-settings` for the default window at 100%, whatever a player saved. Agent and scripted launches always pass it, so a test run never reads or leaves behind the player's settings; scenario 17 is the one run on the real file.
4. In a test, `app->setUiScale(size)` then `app::lintAt(*app, *platform, width, height)`.

## Historical addendum

The original requirements and approach notes, written before the reference and the numbers were chosen.

**Status then:** planned (requirements only)

**Why:** The game renders the UI at a denser scale than the Salvage prototype. With the window at 3072x1728, the logical viewport the UI lays out against is ~2.5x the prototype's reference width, so elements built from the shared design tokens (`fs_*`, `space_*`, sizes) come out visibly smaller on screen than the prototype mocks (e.g. the colonist dossier renders at ~19% of screen width vs ~47% in the mock). This is global, it affects every screen, not just the dossier. Rather than hand-enlarge individual screens (which desyncs them from each other and from the tokens), expose a single UI-scale factor the player controls.

**Goal:** One user-facing setting that scales the entire UI up or down uniformly, so players on high-DPI / large displays can make the UI larger (toward the prototype's proportions) and players who want more on screen can make it denser. The default should land the UI close to the prototype's intended proportions on a typical display.

**Requirements:**

- **Single global factor.** A scalar (e.g. 0.75x-1.5x, default TBD after a calibration pass) applied once, high in the render/layout path, so all screens (menus, worldgen, in-game HUD, dialogs) scale together. No per-screen scale constants.
- **Applies to layout, not just zoom.** Scaling must change the logical viewport the UI lays out against (so text, panels, gaps, hit-testing all scale coherently and reflow), not post-scale a rendered bitmap (which would blur text and break input mapping).
- **Input stays correct.** Pointer hit-testing must use the same scaled space the UI is drawn in (today input is in framebuffer pixels; verify it still lines up after scaling).
- **Live, no restart.** Changing the setting re-lays-out the UI immediately. Open screens/dialogs reflow; nothing needs a scene reload or planet regen.
- **Persisted** in user settings alongside the planned metric/imperial toggle (see [[units-naming-convention]]), surfaced in the Settings screen as a slider or stepped control with a live preview.
- **Crisp at any scale.** Text is MSDF and vector icons are tessellated, so both should stay sharp when the logical viewport changes; confirm the font atlas pixel-range and icon tessellation hold up at the extremes.
- **Sensible bounds + reset.** Clamp to a tested range; provide a "reset to default".

**Approach notes (not a design):**

- The factor most likely belongs where `getLogicalViewport()` is derived (renderer coordinate system), so `logicalSize = framebufferSize / uiScale` and the whole UI layout follows. Confirm whether DPI is already folded in there before adding a second factor.
- Do a calibration pass first: measure the current logical viewport vs the prototype's 1600x1000 reference and pick a default that matches the mock proportions on a common display, then expose the range around it.
- Auto-detecting a default from DPI/resolution is a possible enhancement; ship the manual setting first.

**Out of scope:** per-element or per-screen overrides; separate font-only scaling (the global factor covers it).
