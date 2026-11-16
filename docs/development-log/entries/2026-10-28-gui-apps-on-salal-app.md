# 2026-10-28 - The GUI apps run on salal's App

## Summary

world-sim, ui-sandbox and asset-manager start and run on salal's `app` library (pinned at v0.15.0): its window, frame, input, scene stack and dev server. engine's own `application`, `scene`, `input`, `clipboard` and `debug` directories are gone, and what worldsim still owns between its ui and salal's app is the bridge in `libs/engine/app`, which goes when salal brings a ui of its own. Pixels, ui trees and lint match the build before it scene for scene; the behaviour changes are the ones salal's `docs/app-api.md` lists ("What worldsim keeps, and what changes there"), plus a few more listed in the PR.

## Details

- Each `Main.cpp` does what AppLauncher did, in the open: `app::parseCommandLine`, `createGlfwPlatform`, `App::create` with its `AppConfig` (a 120 Hz cap with vsync, its port, the clear colour, the font atlases and engine's base shader), its resources, its scenes added by name, the first scene, `run`, `shutdown`, its own teardown. The dev server is on wherever it's compiled in (Debug, RelWithDebInfo), so a Release build has none and logs nothing about it.
- `engine::UiScene` (over `app::Scene`) keeps `handleInput(UI::InputEvent&)` and `getUiRoots()`. Every scene moved onto it; scene switches are `app.scenes().switchTo(world_sim::scenes::MainMenu.name)`, and keys come from the frame's snapshot (`engine::key(frame.input, Key::Escape).pressed()`).
- `engine::UiBridge` is the App's dispatch. It walks each frame's input events in order: keys and characters to the focus manager, paused or not; unless paused, each Left or Right press and release to the overlays and the top scene at its own position, each wheel event as a scroll that's sent again every frame until something takes it, and a move at the frame's pointer for hover. It owns the `FocusManager`, adapts `app::Clipboard` to `UI::IClipboard`, and answers `/api/ui/tree` and `/api/ui/lint` from the top scene. GameScene zooms with the scroll nothing took, through world-sim's App context (`world_sim::AppContext`).
- GameScene's dev surface is registrations on `app::DevServer`: its verbs and state views from `DevCommandHandler::registerWith`, the `camera` control (`scenes/game/dev/CameraCommand`) applied in its next update as before. `what=scene` is the server's, from every stacked scene's `exportState()`.
- ui-sandbox's copies of the six scenes salal's sandbox took (sdf_minimal, polygon, arc, polyline, blend, input_test) are gone, and so is the never-registered SliderScene.
- Tests: `libs/engine/app/UiBridge.test.cpp` runs the bridge on salal's headless platform (event order, drags, clicks and scrolls batched into one frame, pause, Tab, modifiers, overlays); `world-sim-tests` covers the camera control's parsing.

Found on the way: DropdownScene installs its own `FocusManager` on enter and clears the singleton on exit, so in a ui-sandbox run every scene entered after it throws in `onEnter` and the exit aborts. It predates the conversion.

## Related Documentation

- salal's `docs/app-api.md` (the design and the accepted differences)
- `CLAUDE.md` "Sandbox Control", `/docs/testing/README.md`, `/docs/workflows.md`
- `/docs/technical/ui-framework/event-system.md`, `/docs/technical/ui-framework/focus-management.md`
- `/docs/technical/include-graph.md`

## Next Steps

- Move the scene handoffs (`GameStartConfig`, `GameWorldState`, `NewGameSetup`) and `UserSettings` into `world_sim::AppContext` and scene `Args`.
- When salal's ui lands, the bridge and `UiScene` go.
