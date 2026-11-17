# 2026-11-17 - Drawing tools redesign: Rectangle, Freeform, helpers, and the key guide (WOR-741)

## Summary

The foundation and wall drawing tools were rebuilt in three levels, from a spec (PR 394) written around an approved interactive prototype. This is the record of the build, "Spec + build: angle-aware snap/align for rotated rectangular buildings" (WOR-741), which landed over 2026-11-16 and 2026-11-17 in eleven slices.

What a player has now that they did not:

- **Rectangle tool, the default.** The Build menu's Foundation entry opens the foundation tool last picked this session, Rectangle the first time. Two clicks, or a press-drag-release, make an axis-aligned foundation with its corners on a 1 m grid (0.5 m in Drawing Settings), or on an existing foundation's corner or edge. A `4 m x 3 m` label shows while the second corner is pending. The construction strip has a two-card tool picker, Rectangle and Freeform.
- **Freeform with helpers.** Freeform draws any polygon and, with every helper off, still lands on existing corners and edges, closes on its first point, and rounds lengths to 0.1 m. Each helper is a switch in Drawing Settings (Settings, main menu only) and turns on both its snapping and what it draws: Angle steps, Guides from the shape, Align to existing buildings, Guides from the world axes, Crossings, Common lengths, Guide distances, Angle readout. Defaults: all of those off; Round lengths to 0.1 m on; Show key guide while drawing on; Grid spacing 1 meter.
- **The look.** Alignment guides are dotted, muted teal and part transparent; the snapped point is a white dot with four ticks that turns red off buildable ground or when a close would be refused; a halo rings the first point when the click will close; length labels sit on dark plates.
- **Common lengths and the angle readout.** A length that matches one already used reads `4.00 m =` in the accent colour, with a tick across the snapped end and a band and double tick on each matching edge. The angle readout draws an arc and the degrees (`135.0` followed by a degree sign), or a right-angle marker for a square corner.
- **Pause off the window.** A drawing tool pauses while the pointer is outside the window or the game is in the background: the preview goes, placed points stay, nothing is cancelled.
- **Wall chain double-click.** The second click ends the chain, as Enter does, without a "wall too short" toast.
- **Key guide panel.** A panel above the construction strip lists the active tool's keys and one legend line per helper that is on, each with a small mark drawn the way the world draws it. `?` or H folds it to a "Keys ?" chip.

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
| 491 | open when this was written | Scenarios read the state views; scenario 20 (WOR-898) |

## Details

### Where the code lives

- **Config.** `assets/config/construction/snapping.xml` holds the tuning keys and the `<DrawingSettingsDefaults>` block; `rendering.xml` `<Preview>` holds the look keys. `libs/assets/assets/ConstructionRegistry.{h,cpp}` loads them (`SnappingConfig`, `PreviewStyle`), `ConfigValidator.cpp` bounds every number.
- **Engine, pure, in `libs/sim/construction`.** `SnapEngine` (`snap`, `snapWall`), `SnapGuidesDetail` (lines, capture, merge, crossings, ranking, edge squaring), `SnapLengthsDetail` (common lengths, length step), `RectangleSnap` (`snapRectangleCorner`, `rectangleFromCorners`), `CornerAngles`, `DrawingAids.h`. `ConstructionWorld` records a placement order on foundations and wall segments.
- **Settings.** `apps/world-sim/UserSettings.h` (`DrawingSettings`, `drawingAids()`), the pane in `scenes/settings/SettingsScene.cpp`.
- **Tools and feedback, `apps/world-sim/scenes/game/world`.** `construction/DrawingSystem` (the tools, the click verdict, the pause), `construction/DrawingFeedback` (the plan and the drawing), `WorldPointer` (pointer routing, `WindowPresence`, `WallDoubleClick`), `construction/DrawingStatusHint`, `construction/KeyGuideVisibility`.
- **UI.** `scenes/game/ui/views/DrawingKeyGuide` (rows, toggle, panel), `ConstructionConfigStrip` (tool picker, hint).
- **Dev views.** `scenes/game/dev/DrawingStateJson`, `CameraStateJson`, `ConstructionStateJson`, `DevJson`.

### Decisions and why

**Pure plans, so most of it is unit tested without a renderer.** The snap result, the preview, the key guide, the strip's hint and the dev view are each a pure function of plain data: `planSnapFeedback` (strokes, rings, dots, plates, labels in screen space), `keyGuideContent(tool, settings)`, `statusHint(status)`, and `drawingStateJson`. The thin drawing layer on top only replays a plan. Tests assert on the plan, not on pixels, which fits the project's rule against screenshot diffs.

**One feedback routine for every preview.** `drawSnapFeedback`, reached from `drawPointerFeedback`, replaced two copies of guide drawing in `DrawingSystem` (PR 431) and now serves every tool. Labels are placed by an obstacle scheme (the marker, halo, wall ring, a rejection reason's keep-clear box, the corner's own edges, the segment being drawn, each arc), and the key guide's legend glyphs are planned by the same stroke helpers (`planLegendGlyph`, drawn through `drawFeedbackPlan`), so a legend mark cannot drift from what the world draws.

**Refactors proved by bitwise comparison.** Where a change had to leave behaviour alone, the previous code was built as a second translation unit and compared over random inputs. The engine reshape: a verbatim copy of the old `snap` and `snapWall` against the new, 48,000 random cases, zero mismatches (swapping the guide order on purpose gave 1,538, so the comparison has teeth). The angle readout: with both settings off the plan was identical over 250,000 frames, then 260,000 and 320,000 with other generators. The key guide's shared stroke helpers: previous planner against new over 420,000 frames across all five tools with every setting, look value and interface scale randomised, no difference.

**The prototype is the source of truth for behaviour, with one stated difference.** The engine ports were compared to the prototype over tens of thousands of random inputs; remaining differences are exact ties decided by order, float against double at a boundary, and float resolution kilometres from the origin. One deliberate difference: at an exact tie between two corners or two edges the prototype's rectangle code keeps the first and the shared `nearestFoundationCorner` and `nearestFoundationEdge` keep the last. The game keeps one rule for both tools (two tests pin it).

**`cornerAngles` decides a corner's inside at the vertex.** The inside of a polygon corner is taken by a ray along the bisector at the vertex, counting crossings of the path's other edges with the even-odd rule. The first version probed a fixed fraction of an edge away, which read a nearly straight corner of a thin shape as reflex: a 1 degree bend read 181.0 where it now reads 179.0. Checked against an independent interior-angle computation over 663,613 corners (random simple polygons, thin triangles, squares, rectangles and a hexagon in both windings), no mismatch.

**The pause is decided in one place.** `pointerWithTool` says a tool follows the pointer when the pointer is in the window and the window is focused. `WindowPresence` tracks enter, leave, focus gained and focus lost event by event, and a key or button press proves focus. Event tracking alone was not enough: no event is dispatched while the dev server has the app paused, so an enter, leave or `FocusGained` can be lost. `GameScene::update` therefore calls `syncPointer` each frame with the frame's input snapshot, which is the window system's word at frame end. A `--background` launch ignores focus (it opens unfocused and is driven by injected input), the same way `Main.cpp` turns off `freezeTicksOnFocusLoss` for it; the flag reaches the scene through `GameContext`. `DrawingSystem::pause()` clears what the pointer proposed and keeps what is placed.

**The rest rule.** In the Freeform tool, a pointer resting on the point it just placed proposes nothing (`restsOnLastPoint`), so the strip does not flash "points too close" for a red the player did not cause by moving. It silences only that one refusal (`VerticesTooClose`); any other refusal shows as it would anywhere. The wall, edit and opening tools were not touched and still flash it (WOR-993).

**`WallDoubleClick`.** It holds the last wall release and answers click or end-the-chain. Two limits in `snapping.xml`: `doubleClickSeconds` (0.4) and `doubleClickDistancePx` (4 logical px, which is also the click-versus-drag threshold for the Rectangle tool), both inclusive, the second a straight-line distance. A double-click consumes the pair, so a third quick click starts a new chain. The scene forgets the record wherever a chain ends, the tool changes or the tool pauses.

**Drawing Settings are session scoped.** `DrawingSettings` is a struct in `UserSettings` seeded from `<DrawingSettingsDefaults>` in `snapping.xml`, with `drawingAids()` building the engine's input from it. They reset each launch (WOR-899, WOR-907). The dev view's settings JSON destructures every member of `DrawingSettings` by name, so adding a setting without its key does not compile.

**The degrees label.** It sits just beyond its arc, inside the corner. When the corner's own edges or the segment being drawn are in the way it moves to the outside; when neither side has room, neither the arc nor the number is drawn. A corner whose shorter edge is under 18 px on screen draws no readout. Nothing shows under Alt, in the Rectangle tool, or on the first segment.

**Units.** Everything is world metres, the same at every zoom, except two pixel values: the origin-close radius (floored at 8 screen pixels) and `doubleClickDistancePx`. In the preview only label text, its plate and the rejection reason follow the Interface size; strokes, dashes, markers, rings and catch distances stay in window pixels. The key guide's legend glyphs are in ui px (a 32 x 14 box), so they scale with the Interface size.

**Other decisions worth keeping.**
- One `closeVerdict()` (later `clickVerdict()` for every tool) feeds the click, the preview and the strip, so the halo promises a close only when the click closes. Before this the band could read red while the halo promised a close.
- The snap and point validation sit behind a memo keyed on tool, pointer world position, aids, construction world version, freeform, zoom, wall preset and host, edit target and placed points.
- Common lengths decide "newest first" by a placement order, the world version when placement began, stored on each foundation and wall segment; every segment of one drawn chain shares one value. A future save format must carry it.
- Ties between guides fall to generation order; `ConstructionWorld::getVertex` and `getSegment` became binary searches.
- Validator: a mistyped enum value in the config is now a startup error naming the key and token, where it used to warn and run in the default mode.
- Pointer routing (`pointerTarget`, `drawingPointerAction`) is pure and tested as a table.

### Dev views and docs

`what=drawing` returns the tool, pointer, placed points, the snap (point, kind, each guide, the length step, whether the click closes), the verdict and reason, the strip's readouts and `hint`, `settings`, `keyGuide` and `dialogOpen`. `what=camera` returns the centre, zoom, pixels per metre, window size and visible rectangle, with the formulas that turn a world point into the pixel `/api/input` takes. `what=construction` gained each foundation's corners and an `openings` list. PR 485 rewrote the drawing sections of the two building-construction docs as built (status lines, open questions and dated notes out, gaps between design and code turned into tickets), and PR 486 extended `docs/testing/README.md` into the contract for scenario authors.

## How it was checked

- Final counts on merged code: world-sim-tests 351, assets-tests 371 (PR 486), sim-tests 1486 (PR 478), Debug with asserts live, plain and shuffled.
- Every new rule was broken once on purpose and a named test fails; the counts of deliberate breaks per PR run from about 80 to 135 for the later slices.
- In the game at 60, 100 and 120 percent Interface size for the preview, rectangle, pause and key guide slices, with `/api/ui/lint` at zero with every tool on.
- Scenarios 03 (foundation, steps 12 to 61) and 04 (walls) read `what=drawing` and `what=construction` in the same line as what a person sees; scenario 20 covers the rest (Rectangle Backspace, right-click and level corners, Freeform with helpers off, each helper with its readback, the key guide's rows, the pause for opening and Add tools). Scenario 20 was run cold, in order, by testers who had only the scenario files and the README, twice.
- Only a person can run: scenario 03 step 44 (Alt held) and step 53 (hold `?`, the guide toggles once), and scenario 20 steps 32 to 34 (Alt with Freeform and Rectangle, Ctrl+click walling a whole edge). Injected input holds no modifier keys and sends no key repeat. Also not exercisable by injected input: a real focus loss and the pointer really leaving the window (a move to a pixel outside the window stands in for it).
- Not checked locally: Linux GCC and Clang builds (CI builds them). No perf was timed; each PR carries its `Perf:` line.

## What was learned

- Briefs and docs go wrong when written from a ticket's old description instead of the spec; read the spec first.
- A design doc keeps unbuilt design as design. The gap between it and the code is a ticket (WOR-1041 to WOR-1044), not a "not built yet" note in the doc.
- A README that testers run against is a contract. Every statement in it has to be traced to the code, and a cold run by someone holding only the docs finds what reading does not (the wrong wall state in scenario 04 step 2, a foundation too small for the wall steps, a `?` keydown that answers 400).
- For test builds in a fresh worktree, set `CCACHE_DISABLE=1`. Observed here, not written down elsewhere: ccache handed a new worktree objects carrying another worktree's absolute paths, and tests that locate fixtures from `__FILE__` then skipped or failed. The earlier cross-worktree poisoning fix is in the 2026-07-03 prototype polish entry.

## Related Documentation

- [Building and construction, design](../../design/game-systems/world/building-construction.md) (Drawing, Helpers, Drawing Settings, Key guide, and the decisions addendum)
- [Building and construction, architecture](../../technical/building-construction-architecture.md) (D12 to D16 and "Where the code lives")
- [Testing](../../testing/README.md) (the drawing and camera views, "Driving a drawing tool")
- [Scenario 03: build a wood foundation](../../testing/scenarios/03-build-wood-foundation.md)
- [Scenario 04: build walls](../../testing/scenarios/04-build-walls.md)
- [Scenario 20: drawing tools](../../testing/scenarios/20-drawing-tools.md) (lands with PR 491)

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

Look questions awaiting a ruling, from the PRs' "Known and not in this change":

- The dotted guide is faint over the tan floor and the rejection reason's contrast is modest (config values to tune by eye).
- The spec's grid cross and the length end tick are hidden under the snapped-point marker's dot at the spec's sizes, as in the prototype.
- The degrees number can change side more than once along some pointer paths; keeping it steady would mean the label remembering its side.
- Zoomed far out, a corner with a shorter edge under 18 px shows no readout.
- Legend glyphs scale with the Interface size while the world's marks do not.
- With every helper on, the wall tool's key guide takes 29 percent of the window height at 100 percent Interface size, 35 at 120.
- The key guide has no separator above the legend and uses a key cap plus a dim word where the prototype has one cap.
- A hand-clicked rotated rectangle reads 89.9 and 90.1 with arcs, since the square tolerance is finer than one pixel of click.
