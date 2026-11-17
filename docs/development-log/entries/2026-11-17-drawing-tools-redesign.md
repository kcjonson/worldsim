# 2026-11-17 - Drawing tools redesign: Rectangle, Freeform, helpers, and the key guide (WOR-741)

## Summary

The foundation and wall drawing tools were rebuilt in three levels, from a spec (PR 394) written around an approved interactive prototype: the Rectangle tool by default, then Freeform, then Freeform with helpers switched on in Drawing Settings. This is the record of the build, "Spec + build: angle-aware snap/align for rotated rectangular buildings" (WOR-741), which landed over 2026-11-16 and 2026-11-17 as eleven slices (the tickets WOR-888 to WOR-898) in sixteen pull requests, plus the spec. The design doc, [Building and construction](../../design/game-systems/world/building-construction.md), is the place to read first for what the player sees.

The Build menu's Foundation entry opens Rectangle the first time and then whichever foundation tool was last picked; the construction strip has a two-card picker. Freeform with every helper off still lands on existing corners and edges, closes on its first point, and rounds lengths to 0.1 m. The eight helpers (Angle steps, Guides from the shape, Align to existing buildings, Guides from the world axes, Crossings, Common lengths, Guide distances, Angle readout) are switches in Drawing Settings, reached from the main menu's Settings only, and all default to off; Round lengths to 0.1 m and Show key guide while drawing default to on, and Grid spacing to 1 meter. Six of the helpers turn on a snap and what it draws; Guide distances and Angle readout only draw. A key guide panel above the strip lists the active tool's keys and a legend line per helper that is on (`?` or H folds it). A drawing tool pauses while the pointer is outside the window or the game is in the background, keeping placed points. A double-click ends a wall chain, as Enter does, without a "wall too short" toast.

For testers: `/api/state?what=drawing` and `/api/state?what=camera`, corners and openings in `what=construction`, and scenario 20 for what scenarios 03 and 04 leave out.

### The PRs

| PR | Merged | What |
|----|--------|------|
| 394 | 2026-11-16 | Spec: drawing tools in three levels (design doc and technical doc) |
| 430 | 2026-11-16 | Config: load and validate the new snapping and look keys (WOR-888) |
| 431 | 2026-11-16 | Snap engine input and result reshaped, today's behaviour pinned (WOR-889) |
| 432 | 2026-11-16 | Drawing Settings section in Settings (WOR-893) |
| 433 | 2026-11-16 | Rectangle corner snap and angle readout geometry (WOR-892) |
| 436 | 2026-11-16 | Snap guides from the shape, nearby buildings, and the world axes (WOR-890) |
| 447 | 2026-11-17 | Length rounding and common lengths in the engine (WOR-891) |
| 437 | 2026-11-17 | Guide look: dotted guides, marker, halo, plated labels (WOR-894) |
| 460 | 2026-11-17 | Architecture doc: engine, rectangle geometry, settings as built |
| 468 | 2026-11-17 | Rectangle tool, tool picker, and the player defaults (WOR-895) |
| 472 | 2026-11-17 | Pause off the window, wall double-click (WOR-895) |
| 474 | 2026-11-17 | Dev drawing state view (WOR-898) |
| 473 | 2026-11-17 | Angle readout and common-length marks (WOR-896) |
| 478 | 2026-11-17 | Key guide panel (WOR-897) |
| 485 | 2026-11-17 | Drawing sections of the two docs as built (WOR-898) |
| 486 | 2026-11-17 | Dev readbacks: settings, key guide, camera (WOR-898) |
| 491 | 2026-11-17 | Scenarios read the state views; scenario 20 (WOR-898) |

## Details

### Where the code lives

- Config: `assets/config/construction/snapping.xml` holds the tuning keys and the `<DrawingSettingsDefaults>` block; `rendering.xml` `<Preview>` holds the look keys. `libs/assets/assets/ConstructionRegistry.{h,cpp}` loads them (`SnappingConfig`, `PreviewStyle`), `ConfigValidator.cpp` bounds every number.
- Engine (pure, in `libs/sim/construction`): `SnapEngine` (`snap`, `snapWall`), `SnapGuidesDetail` (lines, capture, merge, crossings, ranking, edge squaring), `SnapLengthsDetail` (common lengths, length step), `RectangleSnap` (`snapRectangleCorner`, `rectangleFromCorners`), `CornerAngles`, `DrawingAids.h`. `ConstructionWorld` records a placement order on foundations and wall segments.
- Settings: `apps/world-sim/UserSettings.h` (`DrawingSettings`, `drawingAids()`), the pane in `scenes/settings/SettingsScene.cpp`.
- Tools and feedback (in `apps/world-sim/scenes/game/world`): `construction/DrawingSystem` (the tools, the click verdict, the pause), `construction/DrawingFeedback` (the plan and the drawing), `WorldPointer` (pointer routing, `WindowPresence`, `WallDoubleClick`), `construction/DrawingStatusHint`, `construction/KeyGuideVisibility`.
- UI: `scenes/game/ui/views/DrawingKeyGuide` (rows, toggle, panel), `ConstructionConfigStrip` (tool picker, hint).
- Dev views: `scenes/game/dev/DrawingStateJson`, `CameraStateJson`, `ConstructionStateJson`, `DevJson`.

### Decisions and why

**Pure plans, so most of it is unit tested without a renderer.** The snap result, the preview, the key guide, the strip's hint and the dev view are each a pure function of plain data: `planSnapFeedback` (strokes, rings, dots, plates, labels in screen space), `keyGuideContent(tool, settings)`, `statusHint(status)`, and `drawingStateJson`. The thin drawing layer on top only replays a plan. Tests assert on the plan, not on pixels, which fits the project's rule against screenshot diffs.

**One feedback routine for the drawing previews.** `drawSnapFeedback`, reached only from `drawPointerFeedback`, replaced two copies of guide drawing in `DrawingSystem` (PR 431). `drawPointerFeedback` is called from `renderPreview` (the Freeform tool, and Add and Subtract, which fall through to it after drawing the resulting outline, whenever a drawing is under way), `renderRectanglePreview` (Rectangle) and `renderWallChainPreview` (Wall). The Opening tool draws only its ghost. Labels are placed by an obstacle scheme (the marker, halo, wall ring, a rejection reason's keep-clear box, the corner's own edges, the segment being drawn, each arc), and the key guide's legend glyphs are planned by the same stroke helpers (`planLegendGlyph`, drawn through `drawFeedbackPlan`), so a legend mark cannot drift from what the world draws.

**Refactors proved by bitwise comparison.** Where a change had to leave behaviour alone, the previous code was built as a second translation unit and compared over random inputs. The engine reshape: a verbatim copy of the old `snap` and `snapWall` against the new, 48,000 random cases, zero mismatches (swapping the guide order on purpose gave 1,538, so the comparison has teeth). The angle readout: with both settings off the plan was identical over 250,000 frames, then 260,000 and 320,000 with other generators. The key guide's shared stroke helpers: previous planner against new over 420,000 frames across all five tools with every setting, look value and interface scale randomised, no difference.

**The prototype is the source of truth for behaviour, with one stated difference.** The engine ports were compared to the prototype over tens of thousands of random inputs; remaining differences are exact ties decided by order, float against double at a boundary, and float resolution kilometres from the origin. One deliberate difference: at an exact tie between two corners or two edges the prototype's rectangle code keeps the first and the shared `nearestFoundationCorner` and `nearestFoundationEdge` keep the last. The game keeps one rule for both tools (two tests pin it).

**`cornerAngles` decides a corner's inside at the vertex.** The inside of a polygon corner is taken by a ray along the bisector at the vertex, counting crossings of the path's other edges with the even-odd rule. The first version probed a fixed fraction of an edge away, which read a nearly straight corner of a thin shape as reflex: a 1 degree bend read 181.0 where it now reads 179.0. Checked against an independent interior-angle computation over 663,613 corners (random simple polygons, thin triangles, squares, rectangles and a hexagon in both windings), no mismatch.

**The pause is decided in one place.** `pointerWithTool` says a tool follows the pointer when the pointer is in the window and the window is focused. `WindowPresence` tracks enter, leave, focus gained and focus lost event by event, and a key or button press proves focus. Event tracking alone was not enough: the scene's event handler is not called while the app is paused (by the dev server's pause control or otherwise), so an enter, leave or `FocusGained` can be lost. `GameScene::update` therefore calls `syncPointer` each frame with the frame's input snapshot, which is the window system's word at frame end. A `--background` launch ignores focus (it opens unfocused and is driven by injected input), the same way `Main.cpp` turns off `freezeTicksOnFocusLoss` for it; the flag reaches the scene through `GameContext`. `DrawingSystem::pause()` clears what the pointer proposed and keeps what is placed.

**The rest rule.** In the Freeform tool, a pointer resting on the point it just placed proposes nothing (`restsOnLastPoint`), so the strip does not flash "points too close" for a red the player did not cause by moving. It silences only that one refusal (`VerticesTooClose`); any other refusal shows as it would anywhere. The wall, edit and opening tools were not touched and still flash it (ticket WOR-993, the status strip flashing a red refusal while the pointer rests on what was just placed).

**`WallDoubleClick`.** It holds the last wall release and answers click or end-the-chain. Two limits in `snapping.xml`: `doubleClickSeconds` (0.4) and `doubleClickDistancePx` (4 logical px, which is also the click-versus-drag threshold for the Rectangle tool), both inclusive, the second a straight-line distance. A double-click consumes the pair, so a third quick click starts a new chain. The scene forgets the record wherever a chain ends, the tool changes or the tool pauses.

**Drawing Settings last one launch.** `DrawingSettings` is a struct in `UserSettings` seeded from `<DrawingSettingsDefaults>` in `snapping.xml`, with `drawingAids()` building the engine's input from it. They reset each launch (tickets WOR-899, persisting them and reaching Settings from the pause menu, and WOR-907, persisting them in `settings.json`). The dev view's settings JSON destructures every member of `DrawingSettings` by name, so adding a setting without its key does not compile.

**The degrees label.** It sits just beyond its arc, inside the corner. When the corner's own edges or the segment being drawn are in the way it moves to the outside; when neither side has room, neither the arc nor the number is drawn. A corner whose shorter edge is under 18 px on screen draws no readout. The readout reads at most two corners (`kMaxReadCorners`). Nothing shows under Alt, in the Rectangle tool, or on the first segment.

**Units.** Snap and guide distances are world metres, the same at every zoom, except where a pixel value comes in: the origin-close radius is the larger of its config value in metres and an 8 screen pixel floor (`effectiveOriginCloseRadiusMeters`), and `doubleClickDistancePx` is in pixels. What the preview draws, and what scales with the Interface size, is set out in D12 and D16 of the architecture doc.

**Other decisions worth keeping.**
- One `closeVerdict()` (later `clickVerdict()` for every tool) feeds the click, the preview and the strip, so the halo promises a close only when the click closes. While a click would close the shape, the preview's colour, halo and marker read `previewVerdict` and the strip reads `clickVerdict`, which `previewVerdict` is built from, so all of them follow the one close verdict. Before this the band could read red while the halo promised a close.
- The snap and point validation sit behind a memo keyed on tool, pointer world position, aids, construction world version, freeform, zoom, wall preset and host, edit target and placed points.
- Common lengths decide "newest first" by a placement order, the world version when placement began, stored on each foundation and wall segment; every segment of one drawn chain shares one value. A future save format must carry it.
- Ties between guides fall to generation order; `ConstructionWorld::getVertex` and `getSegment` became binary searches.
- Validator: a mistyped enum value in the config is now a startup error naming the key and token, where it used to warn and run in the default mode.
- Pointer routing (`pointerTarget`, `drawingPointerAction`) is pure and tested as a table.

### Dev views and docs

`what=drawing` returns the tool, pointer, placed points, the snap (point, kind, each guide, the length step, whether the click closes), the verdict and reason, the strip's readouts and `hint`, `settings`, `keyGuide` and `dialogOpen`. `what=camera` returns the centre, zoom, pixels per metre, window size and visible rectangle; the formulas that turn a world point into the pixel `/api/input` takes are in "The camera view" in `docs/testing/README.md`. `what=construction` gained each foundation's corners and an `openings` list. PR 485 rewrote the drawing sections of the two building-construction docs as built (status lines, open questions and dated notes out, gaps between design and code turned into tickets), and PR 486 extended `docs/testing/README.md` into the contract for scenario authors.

## How it was checked

- Final counts on merged code: world-sim-tests 351, assets-tests 371 (PR 486), sim-tests 1486 (PR 478), Debug with asserts live, plain and shuffled.
- Every new rule was broken once on purpose and a named test fails; the counts of deliberate breaks per PR run from about 80 to 135 for the later slices.
- In the game at 60, 100 and 120 percent Interface size for the preview, rectangle, pause and key guide slices, with `/api/ui/lint` at zero with every tool on.
- Scenarios 03 (foundation, steps 12 to 61) and 04 (walls) read `what=drawing` and `what=construction` in the same line as what a person sees; scenario 20 covers the rest (Rectangle Backspace, right-click and level corners, Freeform with helpers off, each helper with its readback, the key guide's rows, the pause for opening and Add tools). Scenario 20 was run cold, in order, by testers who had only the scenario files and the README, twice.
- Only a person can run: scenario 03 step 44 (Alt held) and step 53 (hold `?`, the guide toggles once), and scenario 20 steps 32 to 34 (Alt with Freeform and Rectangle, Ctrl+click walling a whole edge). Injected input holds no modifier keys and sends no key repeat. Also not exercisable by injected input: a real focus loss and the pointer really leaving the window (a move to a pixel outside the window stands in for it).
- Not checked locally: Linux GCC and Clang builds (CI builds them). No perf was timed; each PR carries its `Perf:` line.

## What was learned

- Docs and task descriptions go wrong when written from a ticket's old description instead of the spec; read the spec first.
- A design doc keeps unbuilt design as design. The gap between it and the code is a ticket (WOR-1041 to WOR-1044, listed under Next steps), not a "not built yet" note in the doc.
- A README that testers run against is a contract. Every statement in it has to be traced to the code, and a cold run by someone holding only the docs finds what reading does not (the wrong wall state in scenario 04 step 2, a foundation too small for the wall steps, a `?` keydown that answers 400).
- For test builds in a fresh worktree, set `CCACHE_DISABLE=1`. Observed here, not written down elsewhere: ccache handed a new worktree objects carrying another worktree's absolute paths, and tests that locate fixtures from `__FILE__` then skipped or failed. The earlier cross-worktree poisoning fix is in the 2026-07-03 prototype polish entry.

## Related Documentation

- [Building and construction, design](../../design/game-systems/world/building-construction.md) (Drawing, Helpers, Drawing Settings, Key guide, and the decisions addendum)
- [Building and construction, architecture](../../technical/building-construction-architecture.md) (D12 to D16 and "Where the code lives")
- [Testing](../../testing/README.md) (the drawing and camera views, "Driving a drawing tool")
- [Scenario 03: build a wood foundation](../../testing/scenarios/03-build-wood-foundation.md)
- [Scenario 04: build walls](../../testing/scenarios/04-build-walls.md)
- [Scenario 20: drawing tools](../../testing/scenarios/20-drawing-tools.md)

## Next Steps

Open tickets:

- WOR-899, persist Drawing Settings and reach Settings from the pause menu; WOR-907, persist the drawing settings in `settings.json`.
- WOR-993, the status strip flashes a red refusal while the pointer rests on what was just placed (wall, edit and opening tools).
- WOR-1041, foundation strip: total material cost and work estimate while drawing.
- WOR-1042, config strip cards: door and window type picker, card icons, per-metre wall cost, hover stats, pick kept per tool.
- WOR-1043, building-construction docs: remove status, dates and open questions outside the drawing-tools sections.
- WOR-1044, show the refusal reason beside the cursor for every drawing tool.
- WOR-1045, stable ids in the ui tree for the Build menu's entries and the entity panel's action buttons.
- WOR-1002, the selected-entity panel covers the drawing strip's left card at 120 percent Interface size.
- WOR-505, structure multi-select; WOR-994, the Build menu's Furniture entries do nothing.

Look questions awaiting a ruling (the rest are in the "Known and not in this change" sections of PRs 468, 472, 473, and 478):

- The dotted guide is faint over the tan floor and the rejection reason's contrast is modest (config values to tune by eye).
- The spec's grid cross and the length end tick are hidden under the snapped-point marker's dot at the spec's sizes, as in the prototype.
- The degrees number can change side more than once along some pointer paths.
- Legend glyphs scale with the Interface size while the world's marks do not.
- With every helper on, the wall tool's key guide takes 29 percent of the window height at 100 percent Interface size, 35 at 120.
