# 2026-11-16 - Interface scaling: the ui scales from the window, with an Interface size setting

## Summary

The interface used to lay out over the window's logical pixels at a fixed scale, which made it about 2.5 times denser than the prototype on a 3072 x 1728 window. It now scales from the window's resolution against the prototype's 1600 x 1000 stage, with a floor of 0.8, and Settings has an Interface size row (60 to 120%, applied at once and saved). world-sim has a settings file for the first time (`settings.json`), which also keeps the window's size. The design doc is [ui-scale-setting.md](../../design/ui/ui-scale-setting.md).

## Details

### Mechanism and numbers

salal's ui scale: `s = max(floor, f x min(W / referenceWidth, H / referenceHeight))` on the framebuffer's device pixels, the UI laying out on `W/s x H/s` ui px. The OS display scale doesn't enter into it. The numbers are in `libs/theme/theme/InterfaceScale.h` (reference 1600 x 1000, floor 0.8, steps 0.6 to 1.2, minimum window 1280 x 720, no frame), and world-sim, ui-sandbox and asset-manager all pass them.

- At 100% a 16:10 screen is the mock exactly and 16:9 is the mock with 178 px more width: a 1778 x 1000 canvas at 1080p, 1440p and 4K alike, 2389 x 1000 on 3440 x 1440, 1600 x 1000 on 2560 x 1600 and a Steam Deck, 1600 x 900 at the smallest window.
- The floor is 0.8 because the smallest type token is 10 px and body is 13, which stay at 8 and 10.4 device px. dual-deck-builder's 2/3 floor would have put captions at 6.7.
- 120% is the largest step: the canvas is 1481 x 833 on 16:9 and 1333 x 833 on 16:10. The design doc's old 75% to 150% predated the reference, and 150% is a 1185 x 667 canvas on 16:9, 49% of the mock's area (1067 x 667 and 44% on 16:10).
- 60% is there so a 4K player can get back near the previous density (about 58% in a 3072 x 1728 window). On a 1080p screen the floor takes over under about 75%, so the lowest steps do nothing there; accepted.

### The model decision (2026-11-16)

Linear, the same proportions on every screen, as Unity's Canvas Scaler (Scale With Screen Size, Expand), Godot's `canvas_items` with `expand` and Unreal's DPI scaling do for a game. A large monitor is what the player's setting is for. Rejected: a resolution-to-scale curve, a default picked per screen at first run, and following the OS display scale (Godot's advice for non-game applications).

### Changes in the repo

- Settings: an Interface size row in `apps/world-sim/scenes/settings/SettingsScene.cpp`, a segmented control over the seven stops plus Reset, through `App::setUiScale`. Settings is reached from the main menu only. `--no-settings` skips the file, and perf captures launch with it.
- Coordinates: world picking, the camera, drawing and placement previews and overlays stay in window pixels. The hover gate in `GameScene.cpp`, the minimap's view rectangle in `GameUI.cpp` and the world creator's globe and marker (`WorldCreatorScene.cpp`, `GlobeView.h`) mixed the two spaces and were fixed.
- The colonist dossier is 760 x 900 held to 92% x 86% of the canvas, its tab area scrolling (`ColonistDetailsDialog.cpp`).
- Tooling: `/api/ui/tree` and `/api/ui/lint` are in ui px, `/api/input` in window logical px, and `/api/state?what=uiscale` gives `{factor, layoutScale, pixelScale}`.
- Tests: `apps/world-sim/scenes/SceneLayout.test.cpp` with `SceneTesting.test.h` has 12 layout tests, each linting its screen at five window sizes and three interface sizes through salal's `app::lintAt`: the five menu and creator screens, the HUD, the HUD with the build menu, colony task list, decision inspector and five toasts open, the HUD with the wall tool on, all eight dossier tabs, and the crafting, storage and pause dialogs. Each asserts a known id is present and its dialogs report visible. Scrollbars are allowed on `tasks_scroll` and the dossier's tab area, and on the settings list only on canvases shorter than the reference height, so the list fits at the default size. World-sim's scenes build into a static library the tests link, and the test executable is at `apps/world-sim/tests/<config>/world-sim-tests.exe`, run from the build root. Scenario 17 covers the setting by hand.
- World-drawn text a player reads as interface (the edit tool's reason text and the room names) follows the layout scale.
- A saved interface size between stops snaps to the nearest at startup, a tie going up.
- Agent and scripted launches pass `--no-settings`.
- Asset manager: a rescale redraws a thumbnail's picture from the cached mesh, rebuilding only a ground tile's.
- `GameUI` fills the dock's catalogue from the registries itself, through one function the game and its tests call.
- `apps/world-sim/UserSettings.h`: its comment said nothing persists across launches; that is now true only of the drawing settings.

### Layout fixes the lint found

- The world creator's Regenerate and Confirm row stayed laid out, empty, until a world existed; the row is hidden now, not just its buttons.
- The crafting queue's list took room while empty; it's hidden until it has entries.
- The storage rules list did the same.
- The Gear tab pinned a filled hand slot shorter than its text, and two empty caption values had zero size (`8923f7d1`). A filled slot now grows to fit its name and an empty value takes no room.
- The settings list's scrollbar drew over its rows' right edge, and at 100% the list ran 4.4 ui px past a 1000 ui px canvas, so it scrolled for nothing. The list reserves 16 ui px for the bar and the page's top padding is 24 rather than 32, so it fits with 3.6 to spare.

### Engine fixes from salal v0.42.1

The pin moved to salal v0.42.1 for three fixes this work needed: the Windows DPI manifest helper for a fetched game, a stale text measure when a window returns to an earlier size, and `setUiScale` reaching debug layers' and scenes' `onResize`. world-sim, ui-sandbox and asset-manager each call `salal_windows_manifest` on their executable, so each process is per-monitor DPI aware from its manifest.

## Related Documentation

- [Interface scaling](../../design/ui/ui-scale-setting.md)
- salal `docs/app-api.md`, "UI scale"
- [Scenario 17: Interface size](../../testing/scenarios/17-interface-size.md)
- [Testing](../../testing/README.md)

## Next Steps

- Scenario 17 hasn't been run by hand as of this writing.
- The splash, loading and the game scene itself aren't linted, nor are the world creator's later states. Reaching them needs a ground-bake source and a loader interface in the App context, and a world creator model that can start in Reviewing.
