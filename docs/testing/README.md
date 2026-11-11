# Testing

Manual and AI-automated scenario tests for worldsim, driven via the debug HTTP API. A human or
an agent can run these by issuing the curl commands listed in each scenario doc; no source changes
are needed.

See [TEMPLATE.md](./TEMPLATE.md) for the scenario format and [regressions.md](./regressions.md)
for the fixed-bug repro checklist.

---

## Strategy

We test for actual regressions. The art is prototype and changes all the time, so nothing is
protected pixel for pixel: no golden images, no screenshot diffs, in CI or locally. Decided
2026-10-30, the same strategy as salal's (`salal/docs/testing/README.md`).

| Layer | Where | When |
|---|---|---|
| Unit tests | `*.test.cpp` (one `<lib>-tests` per library: `sim-tests`, `world-tests`, `assets-tests` and so on) | CI on non-draft PRs, pushes to `main`, nightly, and by hand, less what already passed with the same inputs ([CI test selection](#ci-test-selection)) |
| Happy-path scenarios | [scenarios/](./scenarios/) and [regressions.md](./regressions.md) | before every commit that touches what they cover |
| Gameplay perf | `node scripts/perf.mjs capture`, with the game's GPU hold on so the card stays at P0 and nobody locks clocks; each sample records the P-state and what the rest of the machine was doing, a capture taken under load is marked void, and it records the depth-sorted upright stream's draws (`uprightDrawCalls`, `uprightTriangles`, from `what=uprights`) so trees read apart from everything else ([ground-textures.md](../technical/ground-textures.md#gpu-hold-wor-553)) | in a session for its own change, after a machine-load check; the full suite in the nightly batch on `main` ([Performance](#performance)) |

- **Scenarios are the happy-path scripts.** Before committing, the relevant ones are run as a
  manual regression pass by the cheapest subagent fleet that can (Haiku-class), one agent per
  scenario, driving the game through the dev API with judgement and fuzzy matching the way a
  human tester would, and reporting pass or fail with a screenshot of what failed. A feature that
  lands adds steps or a scenario. New scenarios are as short as they can be: a numbered list,
  one line a step, the action and what you should see.
- **Performance is strict, and a worse number needs an idle machine.** Rendering performance
  dropping in gameplay is the regression this project has hit most. Even or faster is a result on
  any machine; worse is only a finding on an idle one. See [Performance](#performance).
- **GL tests run on the Linux CI job.** It installs Xvfb and Mesa llvmpipe and sets `WORLDSIM_REQUIRE_GL=1`, so `SurfaceFieldGolden`, `GroundTileBaker` and `AssetRenderer` fail there if no context comes up instead of skipping. Unset, they skip without one, as on the Windows job (GDI's GL 1.1) or a box with no GL.
- **Unit tests are bounded.** Before adding one, look for a test covering the same path, and
  merge duplicates when touching a suite.
- **Lint and tree are helpers, not tests.** While building or checking UI, call `/api/ui/lint`
  (expect `"count":0` before screenshotting) and read element bounds from `/api/ui/tree` rather
  than judging coordinates by eye. On demand only; never in CI.

---

## CI test selection

The Tests workflow (`.github/workflows/tests.yml`) builds everything on both jobs, then runs only
the ctest entries that haven't already passed with exactly the same inputs: test-result caching,
per test exe. `scripts/ci/test-select.mjs` does it, from what CMake reports after configure, so a
new library or test needs no edit to the script or the workflow.

A test's key is a hash of:

- the git tree of the source dir of every in-repo target in its exe's dependency closure, from
  CMake's file API, custom targets included (the base shader, shader staging, the font atlases'
  `tools/generate_sdf_atlas`), and the tree of the dir of any source a target lists from outside
  its own dir. A submodule counts by its commit;
- every keyed top-level entry, claimed ones included, when the closure has a target defined in the
  root `CMakeLists.txt` (`font-atlas`, so `theme-tests`), since such a target's inputs can be
  anywhere;
- any in-repo include dir those targets compile with that isn't inside one of their own dirs, whole:
  planet-view exports `libs/`, so `planet-view-tests` reruns on any change under `libs/`. A
  library whose include root is its own dir adds nothing;
- every `CMakeLists.txt` in a dir above one of those targets' dirs (a target's own is in its tree);
- its command line and the properties that change what runs (`ENVIRONMENT`, `WORKING_DIRECTORY`;
  not `TIMEOUT`, `PROCESSORS` or `LABELS`);
- the data dirs it declares;
- a global part, in every key: every top-level file and dir except `docs/`, `.claude/`,
  `perf-results/`, `*.md`, `.vscode/`, `.clang-format`, `.clang-tidy`, `.gitignore`, `LICENSE`,
  `scripts/` and `.github/`, and except the dirs targets and any test's data dirs account for
  (`libs/`, `apps/`, `tools/`, `assets/`, `fonts/`); `tests.yml`, `.github/actions/` and
  `scripts/ci/` added back; every CMake file configure reads that isn't a `CMakeLists.txt`
  (`cmake/test-data.cmake`, a helper anywhere, ignored dirs included); and every `CMakeLists.txt`
  with no target at or below its dir. So the root `CMakeLists.txt` (salal's tag), `cmake/`,
  `vcpkg.json` and any new top-level file rerun everything;
- the OS, the compiler and its version, and the build type.

A pass is a marker file named for the test and its key, kept in the Actions cache per job
(`test-markers-linux-`, `test-markers-windows-`). A run restores the newest set on its branch,
else main's, skips each test whose marker is there and was recorded in the last 48 hours, and
saves a new set when it changed: one marker for each test that passed in this run or was skipped
on a valid one, nothing else, so a failed, cancelled, or missing test never has one. A test
planned to run with no result fails the job and saves nothing. A run where nothing passed saves an
empty set too, so the passes it restored don't outlive it. A carried marker keeps the time it was
recorded, so a branch whose own set shadows main's reruns each test at least every 48 hours and
picks up a failure the nightly run found. On main the nightly run re-records every pass, which
keeps main's set fresh. On a pull request the checkout is the merge commit, so keys describe the
merged tree.

**Declaring data dirs.** A test that reads repo files at run time names their dirs next to its
`add_test`, in the same `CMakeLists.txt`, relative to the repo root:

```cmake
add_test(NAME sim-tests COMMAND sim-tests)
worldsim_test_data(sim-tests assets/config)
```

- Not needed for files inside the dir of a target in the exe's closure: your own library's
  fixtures found through `__FILE__`, or a dir passed in a compile definition such as
  `WORLD_VIEW_SHADER_DIR`. That dir's tree is already in the key.
- Needed for everything else, including files the test reaches through a copy staged next to an
  exe or into the build dir: declare the source dir the copy comes from.
- The dir has to be tracked source; an untracked or gitignored one fails the plan.
- A new reader of a top-level dir that another test already declares (`assets`, `fonts`) has to
  declare it too: once declared, that dir is out of the global part, so nothing else reruns the
  test when it changes.
- A script that a custom command runs is invisible to CMake's file API. Keep it in a target's dir
  or in a top-level dir the global part keys, not under `scripts/` or `docs/`.

`cmake/test-data.cmake` stores the dirs as the test's `REQUIRED_FILES`, which ctest's JSON reports
and the script reads back; ctest also reports the test Not Run if one is missing. Today
`assets-tests` declares `assets`, `sim-tests` `assets/config`, and `theme-tests` `fonts`.

**Forcing a full run.** Run the workflow by hand (Actions, Tests, Run workflow) with "Ignore
recorded passes and run every test" ticked. The nightly run at 09:00 UTC does the same and records
the passes, which catches what a key can't see: a new runner image, Mesa, the system GL.

**Reading it.** Each job's summary has a table: every test, run or skipped, its key, and why. A
skipped test names the commit, run, and time where it passed with that key; a test that runs names
the inputs that changed since its last recorded pass, or says the recorded pass expired. The
`test-results` artifacts carry `test-select/plan.json` with every key's inputs. Locally, against a
build dir that had `query` run before its configure, with every planned test built (exclude the
rest):

```bash
node scripts/ci/test-select.mjs query --build-dir build   # then configure and build
node scripts/ci/test-select.mjs plan --build-dir build --config Debug \
  --exclude "(benchmarks|worldgen-tests-heavy)" --explain [--rev <rev>]
```

`--config` is needed on the Ninja Multi-Config `windows` preset. Keys hash the tree of `HEAD` (or
`--rev`), never uncommitted edits. `--explain` prints each test's inputs and key; `--rev` keys
another commit's tree with this build's graph.

---

## Performance

The dev machine runs several agent sessions at once, each building, baking planets, or running a
game, so its CPU and GPU are often maxed by something else. A timing taken then measures the other
sessions as much as the change. Decided 2026-11-10, after sessions lost days to captures taken
under load and to re-runs chasing regressions that were never there. The GPU hold keeps the card
at P0; it does nothing about another process taking the card or the cores.

### Timing your own work in a session

Allowed when the change calls for it, with three conditions.

1. **Scoped.** Time what the change could move: the view and the zooms it touches, against `main`.
   The full three-view suite is the nightly batch's job.
2. **Checked.** `node scripts/machine-load.mjs` before the capture and again after, with the game
   closed. It reads CPU, GPU utilization, and other sessions' build, test, and game processes, and
   answers `idle`, `mostly-idle`, or `busy`. On `busy` don't time anything.
3. **Budgeted.** Every capture launches the game on Kevin's desktop and holds the machine while it
   runs. Decide the launches before the first one and stop when the question is answered.

With world-sim in the game scene on your port, once on `main` and once on the branch:

```
node scripts/perf.mjs capture --port <PORT> --view river --zooms 3,1.5 --no-scroll --out <file>.json
node scripts/perf.mjs compare --base <main>.json --tip <branch>.json
```

`--view` is `river`, `coast` or `desert` (or `--x` and `--y` in world meters); leave out `--zooms` and
`--no-scroll` for every zoom and the two scroll scenarios. The capture also watches the machine
while it runs: CPU and GPU use by everything that isn't the game, and other sessions' build, test
and game processes. Past a threshold it marks itself void, and `compare` then answers
`inconclusive` where a loaded capture can't settle it. Take the verdict from `compare`; don't read
timings out of the files by eye. With one capture a side it has no noise to measure against and
flags only a step the size of a major regression; two a side measures the noise.

Chunk and planet generation have microbenchmarks (`*.bench.cpp`, built as `world-benchmarks`,
`assets-benchmarks` and `worldgen-benchmarks`), which time them directly and launch no game. For a
change to that code, build the target in RelWithDebInfo on `main` and on the branch and run it
through the same watch:

```
node scripts/perf.mjs bench --exe build/libs/world/RelWithDebInfo/world-benchmarks.exe --out <file>.json
node scripts/perf.mjs compare --base <main>.json --tip <branch>.json
```

The same rules apply, with thresholds as shares of the base time: past the noise and 5% is
regressed, past 25% is major.

Reading the result:

| Result | Machine | What it means | What to do |
|---|---|---|---|
| Even with `main`, or faster | `idle` or `mostly-idle` | Good. Load only makes a change look slower | Record it, move on. No repeat to confirm |
| Worse | both checks `idle` | Real | Act on it |
| Worse | either check `mostly-idle` | The change, or the machine. Not a finding | One more run is fine once the check reads `idle`. Otherwise write "worse under load, unconfirmed" in the `Perf:` line and leave it to the nightly batch |

On a worse result that isn't confirmed, don't loop on re-runs, bisect, or start investigating, and
don't report it as a regression. The same goes for a timing seen in passing (the metrics overlay,
a slow launch, a test that took long).

**No new baselines with any activity on the machine.** A number is written into a doc or a perf
ledger for later comparison only when both checks read `idle`. Anything else is a same-run
comparison and stays in the PR. That includes a win: faster under load says "no regression", and
the PR says so with the verdict beside it. It isn't a measured speedup to quote in docs or on the
board.

Counts read the same on a busy machine and need no check: `drawCalls` and `triangleCount` from
`/api/metrics`, `/api/state?what=uprights`, entity and chunk counts. One launch per side, fixed
camera, read, exit. Often they answer the question without a timing.

Every PR that could move frame time, sim tick time, or chunk generation carries a `Perf:` line in
its body: what could move, which views or zooms would show it, and what was measured under which
verdict. A change that can't move anything says `Perf: none`. Nothing waits on perf to merge.

### The nightly batch

The backstop, and where an unconfirmed result gets settled. In the early morning, once the machine
measures as idle, an agent times `main`'s tip against the last SHA it tested, and on a major
regression finds the PR behind it and opens a draft revert. Agents run it and decide what it
covers; Kevin never asks for a perf run. [nightly-perf-batch.md](./nightly-perf-batch.md) is the
spec, and the `/perf-nightly` skill runs it.

---

## Scenarios

| # | Name | File |
|---|------|------|
| 01 | Craft axe and box | [scenarios/01-craft-axe-and-box.md](./scenarios/01-craft-axe-and-box.md) |
| 02 | Stock box with wood | [scenarios/02-stock-box-with-wood.md](./scenarios/02-stock-box-with-wood.md) |
| 03 | Build wood foundation | [scenarios/03-build-wood-foundation.md](./scenarios/03-build-wood-foundation.md) |
| 04 | Build walls | [scenarios/04-build-walls.md](./scenarios/04-build-walls.md) |
| 05 | ui-sandbox scene menu and gallery | [scenarios/05-ui-sandbox-menu-and-gallery.md](./scenarios/05-ui-sandbox-menu-and-gallery.md) |
| 06 | asset-manager browse | [scenarios/06-asset-manager-browse.md](./scenarios/06-asset-manager-browse.md) |
| 07 | Pause menu | [scenarios/07-pause-menu.md](./scenarios/07-pause-menu.md) |
| 08 | HUD text fits | [scenarios/08-hud-text-fits.md](./scenarios/08-hud-text-fits.md) |
| 09 | Ground tiles | [scenarios/09-ground-tiles.md](./scenarios/09-ground-tiles.md) |
| 10 | World creator coasts | [scenarios/10-world-creator-coasts.md](./scenarios/10-world-creator-coasts.md) |

**Run each scenario in isolation** from a fresh launch, or use `give` / `need` dev verbs to
set preconditions from a running instance. The fully-combined four-scenario flow now runs
reliably end-to-end with one colonist; see [Known-open caveats](#known-open-caveats) for
remaining cosmetic issues.

---

## Environment

### Build

Config: **RelWithDebInfo**. Debug is too slow for a ready game scene; RelWithDebInfo keeps the
dev HTTP API and reaches the game scene in ~5 s.

```powershell
$env:VCPKG_ROOT = 'C:\vcpkg'
cmake --build build --config RelWithDebInfo --target world-sim -j8
```

Unit tests (independent of the scenario suite):

```powershell
cmake --build build --config Debug --target sim-tests
build\libs\sim\Debug\sim-tests.exe
# every library's tests: ctest --test-dir build -C Debug -LE heavy -E benchmarks
```

### Pre-generated world

A worktree needs `planets/quickstart.wsplanet` (~1.3 GiB, gitignored). Junction it from the main
checkout; see the "Sharing the pre-generated world across worktrees" note in [../CLAUDE.md](../../CLAUDE.md).
Missing file causes an immediate `ConfigError` on launch.

### Launch

```
build\apps\world-sim\RelWithDebInfo\world-sim.exe --scene=game --http-port <PORT>
```

Use a **unique non-default port** (default 8081) to avoid collision with other worktrees or a
running game instance. Reaches the game scene in ~5 s; colonist "Bob" spawns in
TemperateDeciduousForest near a river, with no axe.

### Confirm ready

Poll until it answers 200 with JSON; before the game scene registers its views it's a 404 (`{"error":"no state provider for 'colonists'"}`):

```
GET /api/state?what=colonists
```

### Shut down

```
GET /api/control?action=exit
```

This call blocks and returns `{"status":"ok","action":"exit","shutdown":"complete"}` only after the scenes have exited, GL is torn down and the port is free; no sleep needed after it.

---

## Dev API reference

All calls below are GET requests. Substitute `<PORT>` with the port you launched on. Commands
shown as `curl.exe` (Windows); drop `.exe` on Linux/macOS.

### Write verbs — `GET /api/dev/<verb>?<params>`

Run on the game thread at the start of the next frame, paused or not; the request waits for the
verb to run and answers with its JSON result (`{"status":"ok","verb":...}` for most verbs). Only the
game scene registers them: elsewhere, or for a verb it doesn't have, the request is a 404 at once. Place things on
**valid land** away from water; on-water placements are refused.

| Verb | Key params | Notes |
|------|-----------|-------|
| `spawn` | `def=<asset>&at=x,y&n=1&scatter=0` | Spawns a world entity (tree, station, container, loose item). |
| `colonist` | `at=x,y&n=1&name=` | Spawns a colonist at the given position. |
| `give` | `material=<defName>&n=<qty>&where=site\|loose\|colonist\|storage[&at=x,y]` | Gives material to the colonist, a site, or storage; `loose` drops it on the ground at `at` as stack-capped piles, refused over 50 piles or for a def that isn't an item (furniture, stations). |
| `need` | `colonist=<id>&need=Hunger\|Thirst\|Energy\|...&value=0..100` | Sets a colonist's need to the given value. |
| `time` | `speed=0..3` or `set=HH:MM` or `skip=Nh\|Nm` | Controls game time. `speed=3` accelerates. |
| `teleport` | `colonist=<id>&to=x,y` | Teleports a colonist. |
| `select` | `colonist=<id>` or `at=x,y` | Selects a colonist for the UI. |
| `kill` | `colonist=<id>` | Removes a colonist. |
| `complete` | `id=<entity>` | Instantly completes a task/entity. |
| `craft` | `recipe=<Recipe_*>&n=1&at=x,y` | Queues a recipe at the nearest crafting station. |
| `storage` | `at=x,y&item=<defName\|*>&category=RawMaterial\|Food\|Tool\|Furniture&priority=Low\|Medium\|High\|Critical&min=0&max=0` | Adds a StorageRule to the nearest container. |
| `foundation` | `pts=x0,y0;x1,y1;...&material=Wood&built=0\|1` | `built=0` = blueprint the colonist builds. Refuses if any part of the footprint is off the nav mesh or over water. |
| `foundation-edit` | `id=<foundationId>&mode=add\|subtract&pts=x0,y0;x1,y1;...` | Foundation Add / Subtract through the tool's own validate + apply path. Answers `{status:"ok"\|"rejected", reason, target, extension, entity, area}`: an editable blueprint is reshaped in place (`extension` 0), an Add onto a built foundation returns the extension blueprint's id (it merges once built, e.g. with `freebuild?on=1`). |
| `flora` | `rect=x0,y0,x1,y1` (world metres; default the camera's view) | Reads, changes nothing. The trees in the rect (tall occluders whose def is in the `trees` placement group), as `{rect, rectAreaM2, measuredAreaM2, cellSizeM, trees, treesPer1000m2, canopyCover, woodPer1000m2, defs:[{def, count, per1000m2, woodPerTree, woodPer1000m2, ageClasses:[{name, count, per1000m2}]}], otherTallOccluders:[{def, count, per1000m2}]}`. `ageClasses` lists every age class of a def that has `<ageClasses>`, in its order, and is empty otherwise. The other tall occluders (reeds, big rocks) are listed apart and are in neither the totals nor the cover. `measuredAreaM2` is the part of the rect in loaded chunks, and densities and cover are over it. Cover is the union of each tree's own silhouette (its variant's, shadows left out) at 0.1 m cells; wood is each def's yield midpoint. A route, not a state view, because a view takes no parameters. A rect with a side over 500 m, a side of zero or less, or more than 25 million 0.1 m cells is a 400, as is any coordinate beyond 16777216 m (2^24) from the origin. |
| `flora` | `age=<name>&near=x,y&def=<defName>` (`def` optional) | Reads, changes nothing. The nearest trees of that age class within 200 m of `near` (a 400 m square), nearest first, at most 20: `{age, near, def, searchHalfSizeM, trees:[{def, x, y, ageClass, variant, distance}]}`. `x` and `y` print at full float precision, so a listed position names the same tree when read back. `age` without `near`, or the reverse, is a 400, as is a `near` coordinate beyond 16777216 m (2^24) from the origin. |
| `walls` | `pts=...&material=Wood&thickness=Standard&host=<foundationId>&built=0\|1&close=1` | `built` **defaults to 1** — pass `built=0` for a blueprint. Same off-mesh/water refusal. |
| `opening` | `seg=<id>\|pt=x,y&type=Door\|Window&t=0.5&built=1` | Adds a door or window to a wall segment. |

### Readback — `GET /api/state?what=<x>`

Synchronous JSON, read after the frame renders. The game scene registers these views; any other `what` is a 404. `what` defaults to `summary`. `what=scene` is every app's: `{"scenes":[{"name":"game","state":{"scene":"game","chunks":N}}]}`, top first.

| `what=` | Returns |
|---------|---------|
| `summary` | `{colonists, foundations, chunks}` |
| `colonists` | `[{id, name, x, y, needs{}, action, inventory{}, hands{}, belt{}, cargoKg, carryCapacityKg}]` |
| `stations` | `[{id, x, y, jobs:[{recipe, completed, quantity}], store{}}]` |
| `storage` | `[{id, defName, x, y, inventory{}, slots, maxSlots, rules:[{item, category, priority, min, max}]}]` |
| `construction` | `{foundations:[{id, material, state, entity, area, required{}, delivered{}, progress, phase}], walls:[{id, material, state, entity, x0, y0, x1, y1, required{}, delivered{}, progress}]}` |
| `landing` | `{landingLatDeg, landingLonDeg, biome, waterClass, riverInTile}` |
| `time` | `{...}` |
| `tunables` | every registered tunable and its value |
| `gpuhold` | `{enabled, target, passes, holdGpuMs, sceneGpuMs, frameMs}`, the GPU hold's state; the GPU times are a few frames old |
| `uprights` | `{uprightDrawCalls, uprightTriangles, uprightOverlapSplits, ...}`, last frame's draws in the depth-sorted upright stream (tall statics and actors). `/api/metrics` already counts them in `drawCalls` and `triangleCount`; this splits them out |

### Other endpoints

| Endpoint | Purpose |
|----------|---------|
| `GET /api/ui/screenshot` | Returns a PNG screenshot. |
| `GET /api/control?action=exit` | Shuts down the instance (blocking). |
| `GET /api/control?action=camera&x=&y=&zoom=&panx=&pany=` | Moves the camera (the game scene's control; applied at its next update, so not while paused). `x` and `y` together; `panx`/`pany` -1..1 hold until set to 0. A bad or missing parameter is a 400. |
| `GET /api/control?action=gpuhold&value=0\|1&target=0.8` | The game scene's GPU hold for perf captures: extra GPU work after the scene's timers, into its own offscreen target, sized each frame so scene plus hold GPU time sits near `target` (0.05 to 0.95) of the frame time, which keeps the card at P0. Off by default. Either parameter may be left out; it answers the hold's state, and a bad value is a 400. |
| `GET /api/input?ev=click,x,y` | Synthetic input (logical pixels). Event types: `move`/`down`/`up`/`click`/`scroll,x,y,delta`, `keydown,<key>`/`keyup,<key>`, `char,<codepoint>`. It goes the way real input does: keys reach the focused ui component, a scroll the ui doesn't take zooms the camera, and while paused it waits for resume. A malformed event is a 400. The injected pointer position holds (hover persists) until the real cursor moves over the window or a real mouse button or scroll event lands. |

---

## Def names and recipes

**Stations:** `CraftingSpot` (crafting), `BasicBox` (container/storage).

**Flora (yield Wood on fell, require an axe):** `Flora_TreeOak` (~20-30 wood), `Flora_TreeMaple`,
`Flora_TreePine`. Sticks from `Flora_WoodyBush` (harvest, no tool). Fiber from `Flora_Reed`.

**Loose items:** `SmallStone` (picked up directly).

**Item defNames:** `Wood` (two-hand, stack 40, 2.5 kg), `Stick`, `PlantFiber`, `SmallStone`,
`Berry`, `AxePrimitive`.

**Recipes:**

| Recipe defName | Inputs |
|---------------|--------|
| `Recipe_AxePrimitive` | 2 SmallStone + 1 Stick + 1 PlantFiber |
| `Recipe_BasicBox` | 8 Stick + 3 PlantFiber |

---

## Hygiene for every scenario

- Place everything on **valid land**, a few meters from the river bank (on-water is refused).
- Use `time?speed=3` to accelerate. At speed 3 a harvest-to-craft cycle takes ~1-2 real minutes.
- Keep needs topped every ~2 minutes of sped-up time or hunger/thirst will interrupt work:

```
curl.exe "http://127.0.0.1:<PORT>/api/dev/give?material=Berry&n=10&where=colonist"
curl.exe "http://127.0.0.1:<PORT>/api/dev/need?colonist=<id>&need=Hunger&value=95"
curl.exe "http://127.0.0.1:<PORT>/api/dev/need?colonist=<id>&need=Thirst&value=95"
curl.exe "http://127.0.0.1:<PORT>/api/dev/need?colonist=<id>&need=Energy&value=95"
```

---

## Known-open caveats

These are **open bugs**, not test failures. Do not flag them as regressions.

- **Combined flow: reliable as of 2026-06-29.** The phantom harvest-loop and queued-job stall
  are fixed (colonist task arbitration epic). Running all four scenarios back-to-back with one
  colonist now completes end-to-end.
- **Crafted box placement.** A freshly crafted `BasicBox` lands on the crafting station
  (cosmetic; the box is still usable). `findValidPositionNear` follow-up in flight.
- **Box won't fill to full minimum unaided.** Once the colonist exhausts the trees he's
  discovered, the storage box stalls below its minimum, the system needs to re-arm harvest
  goals when new trees are discovered. Discovery-gating follow-up in flight.
