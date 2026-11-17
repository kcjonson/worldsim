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
- **The dev server's lint and tree are helpers, not tests.** While building or checking UI, call
  `/api/ui/lint` (expect `"count":0` before screenshotting) and read element bounds from
  `/api/ui/tree` rather than judging coordinates by eye. On demand only. The headless scene lint at a
  list of window and interface sizes (`SceneLayout.test.cpp`, through salal's `app::lintAt`) is a unit
  test and runs under ctest in CI, where it gates.

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

### Capture conditions

Every capture, a session's and the nightly batch's, runs world-sim with `--no-settings`: the player's
`settings.json` is neither read nor saved, so the window opens at its default size, 80% of the primary
monitor's work area (3072x1728 on this machine), with the interface at 100% and vsync on until the capture
turns it off. Nothing the capture changes, the vsync it sets included, reaches the player's settings.
`scripts/perf.mjs` passes the flag where it launches the game; a session launching its own passes it too.

### Timing your own work in a session

Allowed when the change calls for it, with three conditions.

1. **Scoped.** Time what the change could move: the view and the zooms it touches, against `main`.
   The full three-view suite is the nightly batch's job.
2. **Checked.** `node scripts/machine-load.mjs` before the capture and again after, with the game
   closed. It reads CPU, GPU load, and other sessions' build, test, and game processes, and
   answers `idle`, `mostly-idle`, or `busy`. GPU load is nvidia-smi's utilization, except while the
   graphics clock is at most a quarter of its maximum (the card at its idle clock), where it is
   scaled by clock over maximum: a card there reports 25 to 45% utilization of almost no capacity.
   At any higher clock the raw utilization stands. On `busy` don't time anything.
3. **Budgeted.** Every capture launches the game on Kevin's desktop and holds the machine while it
   runs. Decide the launches before the first one and stop when the question is answered.

With world-sim in the game scene on your port, started with `--no-settings`, once on `main` and once on the branch:

```
node scripts/perf.mjs capture --port <PORT> --view river --zooms 3,1.5 --no-scroll --out <file>.json
node scripts/perf.mjs compare --base <main>.json --tip <branch>.json
```

`--view` is `river`, `coast` or `desert` (or `--x` and `--y` in world meters), all with the player
visibility cover off, or `river-cover`: the river with the cover drawn, at zooms 3, 1.5 and 0.75
with no scroll. Leave out `--zooms` and `--no-scroll` for every zoom and the two scroll scenarios.
`compare` answers `unjudged` for a pair of cover captures when only one build has the cover. The capture also watches the machine
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
covers, the cover's cost at the river included; Kevin never asks for a perf run. [nightly-perf-batch.md](./nightly-perf-batch.md) is the
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
| 11 | Trees by zoom | [scenarios/11-trees-by-zoom.md](./scenarios/11-trees-by-zoom.md) |
| 12 | Colonists eat | [scenarios/12-colonists-eat.md](./scenarios/12-colonists-eat.md) |
| 13 | Colonists walk round obstacles | [scenarios/13-colonists-walk-round-obstacles.md](./scenarios/13-colonists-walk-round-obstacles.md) |
| 14 | Player visibility | [scenarios/14-player-visibility.md](./scenarios/14-player-visibility.md) |
| 15 | Builder work spots | [scenarios/15-builder-work-spots.md](./scenarios/15-builder-work-spots.md) |
| 16 | Rooms inside rooms | [scenarios/16-rooms-inside-rooms.md](./scenarios/16-rooms-inside-rooms.md) |
| 17 | Interface size | [scenarios/17-interface-size.md](./scenarios/17-interface-size.md) |
| 18 | Colonist thoughts | [scenarios/18-colonist-thoughts.md](./scenarios/18-colonist-thoughts.md) |

**Scenarios that look away from the colonists turn reveal on** (`/api/dev/reveal?on=1`), since ground no colonist has seen is black (player visibility); a launch starts with it off. Scenario 14 is the one that tests the cover itself.

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

world-sim's own test executable (the scene layout lint) builds to `apps/world-sim/tests/<config>/world-sim-tests.exe`.

### Pre-generated world

A worktree needs `planets/quickstart.wsplanet` (~1.3 GiB, gitignored). Junction it from the main
checkout; see the "Sharing the pre-generated world across worktrees" note in [../CLAUDE.md](../../CLAUDE.md).
Missing file causes an immediate `ConfigError` on launch.

### Launch

```
build\apps\world-sim\RelWithDebInfo\world-sim.exe --scene=game --http-port <PORT> --background --no-settings
```

**Every launch an agent makes passes `--background`**, whichever app the scenario names
(`world-sim`, `ui-sandbox`, `asset-manager`) and whatever other flags its launch line gives. Every
world-sim launch passes `--no-settings` too: the settings file (`%APPDATA%\kcjonson\world-sim\settings.json`)
is shared by every worktree's build and Kevin's own play, and a run that stops midway, a resize, or the
dev server's `vsync` control would otherwise leave its interface size, window size or vsync saved for
every later launch. ui-sandbox and asset-manager have no settings file. Scenario 17 tests that the
settings are saved, so it runs on the real file and restores it. The
window opens behind every other window and unfocused, so a pass never lands on top of Kevin's work
or takes his keyboard. Nothing else changes: the window is on the taskbar and drawn every frame,
and screenshots, injected input and state read the same. Both flags come off only for a launch made
for Kevin to test something by hand, or one he asked for. Perf captures are the scripted exception;
`scripts/perf.mjs` launches in the foreground, since a covered window isn't presented the same way.

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
**valid land** away from water; on-water placements are refused. Nothing is built in a stream a
colonist can wade (a ford): `foundation`, `walls`, and `spawn` of a station or storage container
refuse it as they refuse water, and those three answer a refusal as 400 `{"error":"..."}`, creating
nothing. Trees, reeds, and loose items still spawn in a ford: putting one there builds nothing.

A plain `curl` prints a 400's body without failing, so read every reply. `teleport`, `colonist`, and `give`
answer a 400 the same way when they move, spawn, or add nothing. `kill` answers `ok` even when it did
nothing, so check the state view after it.

| Verb | Key params | Notes |
|------|-----------|-------|
| `spawn` | `def=<asset>&at=x,y&n=1&scatter=0` | Spawns a world entity (tree, station, container, loose item). A station or storage container goes where the placement tool would take it: its footprint (its collision outline, else its centre) on clear ground (no water, ford, placed obstacle, or unmeshed ground). Anything else goes on clear ground, a ford included. Refused ground is a 400. A scattered spot is snapped off water and skipped if refused; when every spot is skipped the answer is a 400. A missing or unknown `def` is a 400 too. |
| `colonist` | `at=x,y&n=1&name=` | Spawns a colonist at the given position. Water or a placed obstacle is a 400; ground no mover has meshed takes him, and he meshes it. |
| `give` | `material=<defName>&n=<qty>&where=site\|loose\|colonist\|storage[&at=x,y][&slot=pack\|hands\|belt]` | Gives material to the colonist, a site, or storage; `loose` drops it on the ground at `at` as stack-capped piles, refused over 50 piles. A `material` that is not an item (an unknown name, furniture, a station) is a 400 for every `where`. `slot` is for `where=colonist` (pack by default). Answers `{"status":"ok","verb":"give","added":<count>,"asked":<n>}`, with `"shortfall":"<what was in the way>"` when it added fewer than asked (hands hold two one-hand items, or one armful of a two-hand material up to its weight). For `loose`, `added` is the count asked for, not one read back from the piles. A give that added nothing is a 400 naming why, e.g. `{"error":"give: added 0 of 10 Wood to the nearest colonist's hands: hands occupied by AxePrimitive"}`; so is one with no target, an unknown `where`, or a refused `at`. |
| `need` | `colonist=<id>&need=Hunger\|Thirst\|Energy\|...&value=0..100` | Sets a colonist's need to the given value. |
| `attribute` | `colonist=<id>&attribute=<name>&value=0..20` | Sets one of a colonist's attributes, named as conditions write it (`strength`, `kindness`, `social_need`, `work_ethic`, ...; the `attributes` of `/api/state?what=colonists` lists them). Every colonist spawns with each attribute at 10, so this is how a personality-keyed feeling or line (`attribute(kindness) >= 14`) is reached. An unknown colonist or attribute, or a value that is not a number from 0 to 20, is a 400. |
| `thought` | `colonist=<id>&type=<thought type id>[&subject=<entity id>]` | Gives a colonist a thought the way a source would: the type (`assets/config/thoughts`, e.g. `slept_on_ground`, `talked_with_friend`) picks the feeling from his attributes, the mood effect, and the line. A colonist subject is named in the line (`{subject}`). Answers `{"status":"ok","verb":"thought","created":true,"thought":<id>,"feeling":"<name>","line":"<text>"\|null}`, or `"created":false` when none of the type's feelings applies to him. An unknown colonist, type, or subject is a 400. |
| `time` | `speed=0..3` or `set=HH:MM` or `skip=Nh\|Nm` | Controls game time. `speed=3` accelerates. |
| `teleport` | `colonist=<id>&to=x,y` | Teleports a colonist and drops his task, route, and action, so he decides afresh at the next update (one the player controls stays controlled, without his move order). Water or a placed obstacle is a 400 `{"error":"teleport: (x, y) is water or a placed obstacle"}` and he stays put, as is an id that names nothing. Ground no mover has meshed takes him, and he meshes it. |
| `select` | `colonist=<id>` or `at=x,y` | Selects a colonist for the UI. |
| `kill` | `colonist=<id>` | Removes a colonist. |
| `complete` | `id=<entity>` | Instantly completes a task/entity. |
| `craft` | `recipe=<Recipe_*>&n=1&at=x,y` | Queues a recipe at the nearest crafting station. |
| `storage` | `at=x,y&item=<defName\|*>&category=RawMaterial\|Food\|Tool\|Furniture&priority=Low\|Medium\|High\|Critical&min=0&max=0` | Sets a StorageRule on the nearest container: a rule already there for the same item (same `item` and category) is replaced by the one given, so repeating the call with new `min`/`max`/`priority` leaves one rule, and a rule for another item is added beside it. Answers `{"status":"ok","verb":"storage","replaced":<n>}`, where `n` counts the same-item rules it replaced; the category wildcard a named item removes (as the storage dialog does) is separate and not counted. A named item's category comes from its definition (`category` is only for `item=*`, RawMaterial by default); an unknown item, or a `category` that isn't the item's, answers 400. |
| `foundation` | `pts=x0,y0;x1,y1;...&material=Wood&built=0\|1` | `built=0` = blueprint the colonist builds. Needs at least 3 points; fewer answers 400 `{"error":"foundation: pts needs at least 3 'x,y' pairs (got 2)"}`. Any part of the footprint off the nav mesh, over water or in a ford, or a commit the validator rejects, answers 400 and creates nothing. |
| `foundation-edit` | `id=<foundationId>&mode=add\|subtract&pts=x0,y0;x1,y1;...` | Foundation Add / Subtract through the tool's own validate + apply path. Answers `{status:"ok"\|"rejected", reason, target, extension, entity, area}`: an editable blueprint is reshaped in place (`extension` 0), an Add onto a built foundation returns the extension blueprint's id (it merges once built, e.g. with `freebuild?on=1`). |
| `flora` | `rect=x0,y0,x1,y1` (world metres; default the camera's view) | Reads, changes nothing. The trees in the rect (tall occluders whose def is in the `trees` placement group), as `{rect, rectAreaM2, measuredAreaM2, cellSizeM, trees, treesPer1000m2, canopyCover, woodPer1000m2, defs:[{def, count, per1000m2, woodPerTree, woodPer1000m2, ageClasses:[{name, count, per1000m2}]}], otherTallOccluders:[{def, count, per1000m2}]}`. `ageClasses` lists every age class of a def that has `<ageClasses>`, in its order, and is empty otherwise. The other tall occluders (reeds, big rocks) are listed apart and are in neither the totals nor the cover. `measuredAreaM2` is the part of the rect in loaded chunks, and densities and cover are over it. Cover is the union of each tree's own silhouette (its variant's, shadows left out) at 0.1 m cells; wood is each def's yield midpoint. A route, not a state view, because a view takes no parameters. A rect with a side over 500 m, a side of zero or less, or more than 25 million 0.1 m cells is a 400, as is any coordinate beyond 16777216 m (2^24) from the origin. |
| `flora` | `age=<name>&near=x,y&def=<defName>` (`def` optional) | Reads, changes nothing. The nearest trees of that age class within 200 m of `near` (a 400 m square), nearest first, at most 20: `{age, near, def, searchHalfSizeM, trees:[{def, x, y, ageClass, variant, distance}]}`. `x` and `y` print at full float precision, so a listed position names the same tree when read back. `age` without `near`, or the reverse, is a 400, as is a `near` coordinate beyond 16777216 m (2^24) from the origin. |
| `walls` | `pts=...&material=Wood&thickness=Standard&host=<foundationId>&built=0\|1&close=1` | `built` **defaults to 1** — pass `built=0` for a blueprint. Needs at least 2 points (400 otherwise). A chain any point of which is off the nav mesh, over water, in a ford, or in a placed obstacle answers 400, hosted or freestanding (`host=0`). |
| `ground` | `pts=x0,y0;x1,y1;...` | Reads, changes nothing. Per point: `onMesh`; `valid` (clear to stand on); `buildable`, where a foundation may go once a tree or rock on it is cleared (water, a ford, and unmeshed ground are false); `buildableNow`, where a wall or a station or storage container may go as the ground stands (a placed obstacle is false too, so over a tree `buildable` is true and `buildableNow` false); and the chunk terrain there (`chunk`, `inWater`, `toWaterM`, `rings`). No `pts`, or a point that is not finite, is a 400. |
| `opening` | `seg=<id>\|pt=x,y&type=Door\|Window&t=0.5&built=1` | Adds a door or window to a wall segment. `pt` snaps the way the opening tool does. A placement the tool's validator refuses (too near a corner or end, a wall too short, an overlap with another opening), an unknown type or segment, or no wall near `pt` answers 400 `{"error":...}` with the reason and creates nothing. |
| `reveal` | `on=1\|0` | Player visibility off (`1`) or back on (`0`): with it on the cover is not drawn, and `scripts/perf` captures run with it on. The seen record keeps growing either way; `/api/state?what=visibility` reads the flag as `revealed`. |

### Readback — `GET /api/state?what=<x>`

Synchronous JSON, read after the frame renders. The game scene registers these views; any other `what` is a 404. `what` defaults to `summary`. `what=scene` is every app's: `{"scenes":[{"name":"game","state":{"scene":"game","chunks":N}}]}`, top first.

| `what=` | Returns |
|---------|---------|
| `summary` | `{colonists, foundations, chunks}` |
| `colonists` | `[{id, name, x, y, traits[], needs{}, mood{value, target, band, cause: {need} or {thought} or null}, attributes{strength, agility, ...}, thoughts{held:[{id, type, feeling, mood, left, count}], recent:[{id, type, feeling, line, arrivedAt}]}, action, inventory{}, hands{}, belt{}, cargoKg, carryCapacityKg}]`. `attributes` is every attribute, 0 to 20, by the name conditions use (`social_need`). `thoughts` is there once the colonist has had one: `held` is every thought whose mood effect is running, `mood` its amount (times `count` for a stack), `left` the game hours to go or `"while"` for one that lasts while its condition holds; `recent` is the last five recorded, newest first, `line` the rendered quip or null. |
| `stations` | `[{id, x, y, jobs:[{recipe, completed, quantity}], store{}}]` |
| `storage` | `[{id, defName, x, y, inventory{}, slots, maxSlots, rules:[{item, category, priority, min, max}]}]` |
| `construction` | `{foundations:[{id, material, state, entity, area, corners:[{x, y}], required{}, delivered{}, progress, phase}], walls:[{id, material, state, entity, x0, y0, x1, y1, required{}, delivered{}, progress}], openings:[{id, type, material, wall, fractionAlongWall, state, entity, x, y, phase, required{}, delivered{}, progress}]}`. `state` is `Blueprint` or `Built`. `corners` is the foundation's ring in world meters, to three decimals, in ring order without repeating the first corner, so two foundations that share an edge share two corner coordinates exactly. An opening's `wall` is the wall segment id it sits on, `fractionAlongWall` how far along that wall it is (0 at the wall's `x0,y0` end, 1 at `x1,y1`), and `x,y` that point in world meters; they are left out when the wall or one of its ends is missing. `phase` and the material keys come with an `entity` that carries a blueprint, as on a foundation, and `phase` is `Clearing`, `AwaitingMaterials`, `UnderConstruction` or `Complete` |
| `drawing` | what the drawing tools hold; see [the drawing view](#the-drawing-view) |
| `camera` | `{center{x, y}, zoom, pixelsPerMeter, window{width, height}, visible{minX, minY, maxX, maxY}}`; see [the camera view](#the-camera-view) |
| `landing` | `{landingLatDeg, landingLonDeg, biome, waterClass, riverInTile}` |
| `time` | `{...}` |
| `tunables` | every registered tunable and its value |
| `gpuhold` | `{enabled, target, passes, holdGpuMs, sceneGpuMs, frameMs}`, the GPU hold's state; the GPU times are a few frames old |
| `uprights` | `{uprightDrawCalls, uprightTriangles, uprightOverlapSplits, ...}`, last frame's draws in the depth-sorted upright stream (tall statics and actors). `/api/metrics` already counts them in `drawCalls` and `triangleCount`; this splits them out |

### The drawing view

`what=drawing` answers what the drawing tools hold, so a check reads it instead of the screen. It is built when asked for and costs nothing between requests. The game scene answers it; any other scene answers 404. Every number is printed to three decimals, lengths and areas are in meters and square meters, points are `{x, y}` in world meters, and the enum names are the engine's own. Under the dev server's `control?action=pause` the view still answers, but injected input waits, so it shows the state before the pause.

- `tool`: `none`, `foundationRectangle`, `foundationFreeform`, `foundationEdit`, `wall` or `opening`. `lastFoundationTool` (`rectangle` or `freeform`) is the foundation tool picked last this session, which the Build menu's Foundation entry opens. With `none` the view holds nothing of the last tool: `snap`, `pointer` and the per-tool keys are null or absent, and `status` is `active` false and the material.
- `followingPointer`: false while the tool is paused (the pointer is outside the window, or the window is unfocused) and before its first move. Then `snap` and `pointer` are null and `verdict.proposed` is false, and the placed `points` stay.
- `pointer`: the pointer's world position as the tool last saw it, before any snap, or null when the tool is not following the pointer. A move over a HUD panel (the top bar, the zoom buttons, the strip, the key guide) leaves `pointer` and `snap` at their last values, and a click there is taken by the ui.
- `points`: the placed points. In the Rectangle tool, its first corner.
- `rectangle` (Rectangle tool only): `pressPending` (a press awaits its release), `firstCorner` or null, `corners` (the pending rectangle's four, empty when the two corners make none) and `sizeLabel`.
- `edit` (Add / Subtract only): `target` foundation id and `mode`, `Add` or `Subtract`.
- `wall`: `hostFoundation`, the foundation the chain is on, 0 until the first point is placed; a pointer over a foundation does not set it.
  - Before the first point, `verdict.proposed` is true with the pointer on a foundation and false off every one (a click there places nothing), and `status.valid` is true with the hint `Click a foundation to start` either way.
  - A refusal for the ground outranks both: over ground that cannot be built on, `verdict.valid` is false, `proposed` is true and `reason` says why.
- `opening`: `type`, and `snap` or null when no built wall is in range. The snap is `wall` (the segment id), `fractionAlongWall` (how far along that wall the opening sits: 0 at the wall's first vertex, 1 at its second) and `point`, which is on the wall's line.
- `snap`: the snapped point, `kind`, `guides`, the length step, and `closesShape` (the next click closes the shape; in the Rectangle tool, commits it). Null while paused, and for the opening tool, whose snap is under `opening`.
  - `kind` is a `SnapKind`: `None`, `Angle`, `Vertex`, `Edge`, `Origin`, `WallEndpoint`, `WallSegment`, `GuideLine` or `GuideCrossing`. In the Rectangle tool it is a `RectangleSnapKind`: `None`, `Vertex`, `Edge` or `Grid`. A close the rescue makes, with the pointer near the first point but not on it, has `kind` `None` and `closesShape` true.
  - Each guide has `source` (`DrawnShape`, `NearbyStructure` or `WorldAxes`), `relation` (`Parallel`, `Perpendicular` or `AngleStep`), `from` (the node it runs from) and `distanceFromNodeMeters`.
  - `lengthSnap` is `None`, `Rounded`, `RoundedAlongGuide` or `CommonLength`; `lengthMeters` is the length the step set, and 0 when it set nothing. `segmentLengthMeters` is the distance from the last placed point to the snapped point whatever set it, or null with no placed point, so at a guide crossing `lengthMeters` can be 0 while the segment is 4.000. `matchedEdgeCount` and `commonLengthsMeters` are the common-length matches.
- `verdict`: what the next click would get, read from the same check the strip and the preview read: `valid`, `proposed` (false when the click would place nothing), and `reason` when refused. The opening tool's verdict is the strip's own (`status.valid` and `status.message`), except while paused, where `verdict.valid` is true and `status.valid` false.
- `settings`: the Drawing Settings as they are now, there with or without a tool; see [the settings keys](#the-settings-keys). A setting changed in the Settings scene shows here at once.
- `keyGuide`: `{shown, collapsed}`, there with or without a tool. `shown` is true while a tool is active and `settings.showKeyGuide` is on, which is the panel's own rule; `collapsed` is true while it is folded to its chip (the `?` or H key, or the chip, toggles it), and stays what it was while the panel is hidden. A folded guide is still `shown`.
- `dialogOpen`: true while a dialog is open over the game (the pause menu, Settings), false otherwise, tool or not. While it is true the world's hotkeys and the key guide's toggle do not act.
- `status`: the strip's readouts: `active`, `material`, `pointCount`, `areaSquareMeters`, `valid`, `message`, `hint`, plus `thicknessPreset`, `segmentLengthMeters`, `totalLengthMeters`, `wallCost`, `wallWork` for a wall, `editLabel` and `areaDeltaSquareMeters` for Add / Subtract, `openingType` and `openingWidthMeters` for an opening. Each group is there only while its tool is active.
  - `hint` is the line the strip shows under its readouts, from the same function the strip calls. It is the refusal or an instruction, by tool:
    - Rectangle: `Click or drag from a corner` (no corner yet), `Click the opposite corner` (one corner placed).
    - Freeform, and Add / Subtract once a point is placed: `Keep placing points` (one or two points), `Click origin to close` (three or more, valid).
    - Add / Subtract with no point: `Draw a region across the foundation edge`.
    - Freeform with no point, and no tool: `Ready`.
    - Wall: `Click a foundation to start` (no point placed), `Right-click / Enter to finish` (one or more).
    - Opening: `Click a wall to place` (valid), `no wall in range` (the pointer is not near a built wall), `Hover a built wall` (paused, nothing proposed), or the validator's refusal.
  - How the keys relate:
    - `message` carries the refusal, and is empty when `valid` is true.
    - `hint` is the strip's line: the refusal or an instruction.
    - `hint` equals `message` when `valid` is false and `message` is not empty, whatever the tool and however many points are placed (a refusal beats `Keep placing points`).
    - When `valid` is false and `message` is empty, `hint` stays an instruction: the opening tool's `Hover a built wall`, a wall's or Rectangle's usual line, and for Freeform or Add / Subtract with three or more points `Ready`.
    - `verdict.reason` is the same text as `status.message` for every tool.
    - The view reports text only; the strip's colour for the line is not in it.
  - A scenario that checks a refusal asserts `status.valid` false and `status.message`.

A Freeform shape with two points placed and the pointer on a third:

```json
{"tool":"foundationFreeform","lastFoundationTool":"freeform","followingPointer":true,
 "pointer":{"x":26.248,"y":-2.304},
 "points":[{"x":16.875,"y":-8.625},{"x":26.275,"y":-8.625}],
 "snap":{"point":{"x":26.250,"y":-2.325},"kind":"None","guides":[],"lengthSnap":"Rounded",
         "lengthMeters":6.300,"segmentLengthMeters":6.300,"matchedEdgeCount":0,"commonLengthsMeters":[],"closesShape":false},
 "verdict":{"valid":true,"proposed":true,"reason":""},
 "status":{"active":true,"material":"Wood","pointCount":2,"areaSquareMeters":0.000,"valid":true,"message":"","hint":"Keep placing points"},
 "settings":{"angleSteps":false,"guidesFromShape":false,"alignToExistingBuildings":false,"guidesFromWorldAxes":false,"guideCrossings":false,
             "commonLengths":false,"guideDistances":false,"angleReadout":false,"roundLengths":true,"showKeyGuide":true,"rectangleGridSpacingMeters":1.000},
 "keyGuide":{"shown":true,"collapsed":false},"dialogOpen":false}
```

With the pointer over the creek the same shape answers `"verdict":{"valid":false,"proposed":true,"reason":"not on buildable ground"}`.

#### The settings keys

The defaults are `<DrawingSettingsDefaults>` in `assets/config/construction/snapping.xml`; the label is the row in the Settings scene.

| Key | Default | Settings label |
|-----|---------|----------------|
| `angleSteps` | false | Angle steps |
| `guidesFromShape` | false | Guides from the shape |
| `alignToExistingBuildings` | false | Align to existing buildings |
| `guidesFromWorldAxes` | false | Guides from the world axes |
| `guideCrossings` | false | Crossings |
| `commonLengths` | false | Common lengths |
| `guideDistances` | false | Guide distances |
| `angleReadout` | false | Angle readout |
| `roundLengths` | true | Round lengths to 0.1 m |
| `showKeyGuide` | true | Show key guide while drawing |
| `rectangleGridSpacingMeters` | 1.000 | Grid spacing (rectangle tool): 1 meter or 0.5 meters |

Every key but the last is true or false. `rectangleGridSpacingMeters` is 1.000 or 0.500.

### The camera view

`what=camera` answers where the world camera looks, so a check can turn a world point into the pixel `/api/input` takes, and a pixel back into a world point. The game scene answers it. Numbers are printed to three decimals.

- `center`: the camera's current position, `{x, y}` in world meters. It eases toward the position `/api/control?action=camera&x=&y=` or the pan keys set, so read the view again until `center` stops changing before converting. The view answers while the app is paused, but a control still pending has not applied.
- `zoom`: the zoom factor (3.000 is the 100 percent view). `pixelsPerMeter`: the screen scale at that zoom, in logical pixels per world meter.
- `window`: `{width, height}`, the window's logical pixels, the ones `/api/input` takes, in whole numbers. It does not change with the Interface size.
- `visible`: `{minX, minY, maxX, maxY}`, the world rectangle the window shows.

A screenshot is in framebuffer pixels (twice the logical size on a HiDPI display), and `/api/ui/tree` is in ui px (times `layoutScale` from `what=uiscale` for logical pixels); neither is `window`'s unit without that conversion.

Y runs down the screen and down the world alike: a larger world y is lower in the window, so pixel y grows with world y and there is no flip. The two formulas, with `c` the `center`, `s` the `pixelsPerMeter` and `w`, `h` the `window`:

```
pixelX = w / 2 + (worldX - c.x) * s        worldX = c.x + (pixelX - w / 2) / s
pixelY = h / 2 + (worldY - c.y) * s        worldY = c.y + (pixelY - h / 2) / s
```

A pixel is in the window when it is within `0..w` and `0..h`, but a tool's click is taken by the ui first where a panel covers it (the dock, the strip, the key guide), so aim at open ground. A point a tool places lands on its snap: with the Freeform tool and `roundLengths` on, a click at the pixel for (14.5, 22.0) read back as (14.499, 22.000), so allow the 0.1 m rounding.

### Driving a drawing tool

For a scenario author. Everything goes through `/api/input`, the way a player's hands do.

1. Open the Build menu (`build_dropdown`) and pick Foundation, Wall, Door or Window. The menu's entries have no ids; they are the `MenuItem` nodes of the open `Menu` in `/api/ui/tree`, in the order Foundation, Wall, Door, Window, with their `screenBounds` in ui px.
2. For Foundation, the strip's cards pick the tool: `config_strip_tool_Rectangle` or `config_strip_tool_Freeform`.
3. On a selected foundation's panel, `entity_action_Add`, `entity_action_Subtract` and `entity_action_Demolish` start the Add and Subtract tools and the demolition. Selecting the foundation takes a `ev=move` to it before the `ev=click`. A foundation that hosts walls offers `entity_action_Demolish building` instead of `entity_action_Demolish`.
4. Place points with `ev=click,x,y`, the pixel from the camera formulas above. A point that is buildable at the Quick Start landing is (30, 10); the Rectangle from (30, 10) to (34, 13) places there, while (10, 10) is water.
5. Read `what=construction` to see what was committed. The drawing view does not say that a shape was committed; it shows the tool's state, not the shapes it built.

Keys, from `ev=keydown,<key>`, as the key guide lists them:

| Key | Rectangle | Freeform and Add / Subtract | Wall | Door and Window |
|-----|-----------|-----------------------------|------|-----------------|
| `backspace` | drops the first corner | undoes the last point | undoes the last point | not listed |
| `escape` | drops the first corner; with none, leaves the tool | cancels the shape; with none, leaves the tool | ends the wall; with none, leaves the tool | leaves the tool |
| `enter` | not listed | not listed | ends the wall | not listed |

Injected input holds no modifier keys (no Alt for the raw cursor, no Ctrl for walling a whole edge), cannot repeat a key, and cannot move the pointer out of the window.

### Other endpoints

| Endpoint | Purpose |
|----------|---------|
| `GET /api/ui/screenshot` | Returns a PNG screenshot. |
| `GET /api/control?action=exit` | Shuts down the instance (blocking). |
| `GET /api/control?action=camera&x=&y=&zoom=&panx=&pany=` | Moves the camera (the game scene's control; applied at its next update, so not while paused). `x` and `y` together; `panx`/`pany` -1..1 hold until set to 0. A bad or missing parameter is a 400. |
| `GET /api/control?action=gpuhold&value=0\|1&target=0.8` | The game scene's GPU hold for perf captures: extra GPU work after the scene's timers, into its own offscreen target, sized each frame so scene plus hold GPU time sits near `target` (0.05 to 0.95) of the frame time, which keeps the card at P0. Off by default. Either parameter may be left out; it answers the hold's state, and a bad value is a 400. |
| `GET /api/input?ev=click,x,y` | Synthetic input (logical pixels). Event types: `move`/`down`/`up`/`click`/`scroll,x,y,delta`, `keydown,<key>`/`keyup,<key>`, `char,<codepoint>`. It goes the way real input does: keys reach the focused ui component, a scroll the ui doesn't take zooms the camera, and while paused it waits for resume. A malformed event is a 400. The injected pointer position holds (hover persists) until the real cursor moves over the window or a real mouse button or scroll event lands. Since salal v0.41.1 an injected move also sets the window's pointer-in-window flag, so hover-driven things (the placement ghost, the drawing tools' preview) follow it. A screenshot is in framebuffer pixels, which on a HiDPI display are twice the logical size (on Windows they're the same). `/api/ui/tree` and `/api/ui/lint` are in ui px, which the interface scale makes larger than a logical pixel: a tree position times `layoutScale` from `/api/state?what=uiscale` (`{factor, layoutScale, pixelScale}`, every scene, all three apps) is the logical position. A click that lands on nothing still answers 200. |

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
- Keep needs topped every ~2 minutes of sped-up time or hunger/thirst will interrupt work (once his
  pack has no room for more berries the give answers a 400, which is fine here):

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
- **A box below its minimum waits for a known tree.** The box's stocking cut is open only while
  some colonist knows a tree that yields its item; the storage goal system checks that on every
  poll, so cutting resumes as soon as a colonist discovers another tree. While nobody knows one, the box
  waits; that is discovery gating, not a stall.
