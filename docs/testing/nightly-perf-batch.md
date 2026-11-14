# Nightly perf batch

Status: built 2026-11-10 (`scripts/perf.mjs`, the `/perf-nightly` skill). See "What has run" for what has been exercised.

The full perf suite runs as one batch in the early morning, on `main`, once every session has finished and the machine is idle. An agent runs it. Nobody asks for it. A session may still time its own work during the day when the machine is quiet enough (R1), and the batch settles what a session couldn't.

## Why

The dev machine runs several agent sessions at once, each building, baking planets, or running a game. Its CPU and GPU are often maxed by something else, and a capture taken then measures the other sessions. Sessions have spent hours on captures like that, read the noise as a regression, and rerun to chase it. Days were lost that way. The GPU hold keeps the card at P0; it does nothing about another process taking the card or the cores.

Performance is still strict. A session can show its change is even or faster at any time; showing a regression, or setting a baseline, takes an idle machine, and the batch is the one run that always has one.

## Decisions

- The batch runs on `main`, after the day's PRs have merged. PRs don't wait for it. A regression is answered with a draft revert of the PR that caused it, not by holding work back before merge. Decided 2026-11-10 over an integration branch, which would hold PRs until morning, block sessions stacked on them, and merge to `main` unattended.
- Agents decide what needs a perf run, in a session and in the batch. Kevin never asks for one.
- A result that is even or faster stands on any machine. A worse one stands only from an idle machine. Decided 2026-11-10.
- The base is measured the same night as the tip. Numbers from different nights are never compared.
- The batch looks for major regressions. It does not set baselines or gate on hundredths of a millisecond.

## R1. Timing in a working session

A session may time its own work when the change calls for it. The rule is in the [testing README](./README.md#performance); in short:

- Scoped to what the change could move, not the full suite.
- `node scripts/machine-load.mjs` before and after, and no timing on `busy`.
- Even or faster is a good result on any machine, and the session moves on.
- Worse is the change or the machine. Unless both checks read `idle` it is not a finding: at most one more run once the machine is idle, otherwise "worse under load, unconfirmed" in the `Perf:` line, and the batch settles it.
- No new baselines from a run with any activity on the machine.

Every PR that could move frame time, sim tick time, or chunk generation carries a `Perf:` line: what could move, which views or zooms would show it, and what was measured under which verdict, or `Perf: none`. This is what the batch reads to decide relevance and where to look first. PRs merge on green CI; nothing waits for the batch.

## R2. Idle gate

The batch starts at 02:00 local and waits for the machine to be idle. Idle is measured, never assumed. It checks every 5 minutes with `scripts/machine-load.mjs`, the same check a session runs, and starts when two checks in a row answer `idle`:

- No build, test, or game process: `cl`, `link`, `ninja`, `cmake`, `ctest`, `clang*`, `ccache`, `world-sim`, `ui-sandbox`, `asset-manager`, `worldgen-cli`, any `*-tests`.
- CPU under 10%.
- GPU utilization under 10% (`nvidia-smi --query-gpu=utilization.gpu`, read-only).

The process list catches a session that is between commands; the CPU and GPU readings catch everything the list doesn't name. Thresholds are constants in the script, tuned from the first week's logs.

The gate holds for the whole batch, not only the start. `measure` rechecks the machine before every capture and waits again if it isn't idle. Every sample also records CPU and GPU use by processes other than the game (one `typeperf` for the capture: total CPU less the game's, and the 3D engine use of every other process). The process list is polled about once a second through the capture, so a build that starts and ends inside one scenario is still seen. A capture is void when more than a fifth of its samples saw over 10% of either, when another session's build, test or game process ran during it, when a sample was off P0, or when a scenario has no load readings or no process list (load nobody read is unknown, not low). The batch waits for idle again and redoes that one capture, once. Void twice and that view is inconclusive for the night.

If the machine never goes idle by 06:00, or the batch is still running at 07:00, it stops. A build still running at 07:00 is killed with its whole process tree, and no build starts after it. The report says what was busy (the processes that failed the gate) and the untested range carries to the next night.

## R3. What needs a run

State lives in `perf-results/nightly/state.json`: the last `main` SHA the batch finished on (`lastTested`) and the regressions still open.

1. The range is the first-parent merges in `lastTested..origin/main`. `main` takes merge commits, so each one is a PR.
2. A merge is perf-relevant if its PR's `Perf:` line says so, or its diff touches `libs/world-view`, `libs/world`, `libs/sim`, `libs/assets`, `apps/world-sim`, shaders, Lua generators or asset definitions, or the salal `GIT_TAG`. A `Perf: none` on a diff that touches those paths is overruled by the paths.
3. A merge reaches a library's microbenchmarks (`world-benchmarks`, `assets-benchmarks`, `worldgen-benchmarks`) when its diff touches that library, or `libs/worldgen` for `world`, whose chunk sampling reads worldgen's data, or the salal `GIT_TAG`. A target whose own `.bench.cpp` changed in the range is left out that night: base and tip would time different work.
4. No relevant merge and no benchmark target in the range: advance `lastTested`, write a one-line report, done. No build, no launch.

Everything relevant is already together on the tip, so the first run covers the whole day at once.

## R4. The run

A dedicated worktree, `perf-nightly`, with its own build directory. RelWithDebInfo, at the game's own window size on this machine (3072x1728; there is no flag for it).

The batch never bakes a planet. Each build copy gets a hard link to the main checkout's `planets/quickstart.wsplanet`, the planet the game is played on. A planet baked today from `main` is a different world, with the landing river drawn dry (WOR-701), and timings from it compare with nothing. A SHA that can't load the shared planet, after a worldgen format change, fails its captures and the report says so.

- Build `lastTested` (base) and the tip. ccache makes the base cheap.
- Capture both the same night, interleaved base, tip, base, tip: two runs each at the river, coast, and desert views (`organic-terrain/terrain-polygons-architecture.md`, section 6), and at the river with the player visibility cover drawn. Sixteen captures.
- Each capture of the three views is today's suite: idle at zooms 20, 8, 3, 1.5, 0.75, 0.5, 0.25, then the two scroll scenarios, with the GPU hold on. They run with the cover off (`/api/dev/reveal?on=1`), so coast and desert, 70 km and 131 km from any colonist, time the world and not a black frame, and the river's numbers stay comparable with history.
- The cover capture is a fourth view, `river-cover`: the river's place with the cover on, at zooms 3, 1.5 and 0.75 and no scroll scenarios. It is a cell of its own, so the cover's cost reads by itself, against the same night's base. The river is where colonists are, so the cover has seen ground, sight shapes and black ground to draw. The zooms are the ones a daytime comparison read the cost at; the cover is two full-view fills through stencil masks, so its cost does not grow with zoom, and the other four add launches without adding anything the three don't show. The scroll scenarios are left out because they time chunk generation as the view moves into black ground, which the river's scroll scenarios already do, and the cover's cost is read at rest. Coast and desert have no cover capture: from there the cover would be all black fill, which is the river's black region with nothing else in it. A night with all four views is sixteen launches, four more than before, and the four new ones take three scenarios each where a full view takes nine (12 scenario takes on top of 108); `node scripts/perf.mjs measure --plan` prints the launches and scenario takes without running anything.

The frame capture sees chunk generation only as scroll spikes, so the libraries' microbenchmarks run beside it (`perf.mjs bench`) on nights when a merge reached them (R3): the same two SHAs, two runs each, interleaved, under the same idle gate and machine watch. They are CPU-only, launch no game, and other processes' GPU use doesn't void them. Their executables are copied to `perf-results/nightly/bench-builds/<sha>/<target>/`. A night can run either measurement without the other.

Each SHA is built once in the worktree and its exe directory copied to `perf-results/nightly/builds/<sha>/`, without the debug files. Captures launch from the copies, a fresh game for each one, so interleaving costs no rebuilds and a bisect step reuses a SHA already built. The capture is `scripts/perf/capture.mjs`, the same code a session runs as `node scripts/perf.mjs capture`.

## R5. Verdict

The script decides. The agent never reads raw timings and forms its own view.

- Noise per cell (view x scenario x metric) is the spread between the two runs of the same SHA that night. A void run supplies none: load inflates it, and a spread that wide would pass a slower tip as clean.
- A cell regressed when the tip is worse than the base by more than three times that noise, and by at least 0.02 ms. With no noise to measure against, only a step the size of a major one counts.
- A regression is major when a regressed cell is worse by more than 10% and more than 0.25 ms on `gpuMs`, `tileGpuMs`, or frame p50, or a scroll scenario gains 33 ms spikes. Starting values, constants in the script.
- A microbenchmark is one cell, on real time (the planet pipeline benchmark works on another thread). Benchmarks run from nanoseconds to seconds, so their thresholds are shares of the base: regressed past three times the noise and 5% (two builds of unchanged code differ by 2 to 3% from layout alone), major past 25%. A major benchmark is handled like a major view.
- Regressed but not major is listed in the report and nothing else happens.
- Load only ever makes a run slower, so a void tip can't confirm a regression and a void base can't confirm a clean result. Either way the view is `inconclusive`. A void tip that is even or faster is still clean.
- A view short of the captures the night asked for (a failed capture, the 07:00 stop) is `inconclusive` unless what was captured already shows a major regression, so a partial night never advances `lastTested`. Captures that share no scenario are refused, not compared.
- The cover cell needs a cover on both sides. Each capture records whether the build answered the reveal verb (`revealVerb`). When only one of base and tip did, the two draw different frames, and the cell's verdict is `unjudged`: the report says the base predates the cover (or the tip lost it), the cell neither blocks `lastTested` nor reaches the revert flow, and it counts as clean in the night's overall verdict. That is the first night after the cover merged, with a base from before it. From the next night both SHAs have the cover and the cell is judged like any other, with the same noise, void and major rules. A cover cell with no captures on a side is `inconclusive` as for any view.
- Draw calls, triangles, and upright counts are reported per cell as context, and a count that moved is the first clue to the cause.

## R6. Isolating a major regression

1. Read what's already there before building anything. Which cells and metrics moved points at a subsystem (`tileGpuMs` at the ground shader, upright draws at trees, `updateMs` at the sim), and the range's `Perf:` lines and diffs say which PR touched it. That names a suspect.
2. With a suspect: build the suspect merge and its first parent, capture only the views that failed, two runs each. If the step is there, it's the offender.
3. Without one, or if the suspect was clean: bisect the range's perf-relevant merges on first-parent, halving, failing views only.
4. With the offender found, build the tip with that merge reverted, on a scratch branch that is never pushed, and capture again. Clean: done. Still regressed: there is a second offender, repeat from step 1 on what's left, adding its revert to the scratch branch. Each offender's draft PR (R7) is its own branch from the tip with only its own revert.
5. Salal's perf suite runs only when the offending merge changed the salal `GIT_TAG`, at the old and new tags, to say whether the engine's numbers moved (file it in salal) or worldsim's use of it did. Never otherwise.

The idle gate and the 07:00 stop apply throughout. A night that runs out reports what it narrowed the range to and picks up there the next night.

## R7. Outcome

- Clean: `lastTested` advances to the tip.
- Major regression: for each offender, a draft PR reverting its merge (`git revert -m 1`), with the table of cells, base and tip numbers, and noise; a comment on the original PR linking it; a bug in Specboard filed from the original item. Kevin decides whether the revert merges. `lastTested` advances to the tip either way, and the offender is recorded as open so later nights measure against it instead of reporting it again.
- Every night writes `perf-results/nightly/<date>/report.md` and the raw captures beside it: the range, what was relevant, the verdict per view, anything void or inconclusive, and open regressions. The agent ends its session with the report's summary.

The batch never merges or reverts on `main` itself.

## How it's built

An agent orchestrates and a script measures.

- `scripts/perf.mjs` (Node, ESM, no dependencies) has one subcommand per deterministic step: `idle-wait`, `range`, `measure`, `bench`, `compare`, `state`, and `setup`, plus `capture` and `bench --exe` for a session's own change. Its header lists the options. `scripts/perf/capture.mjs` is the capture, `scripts/perf/bench.mjs` the microbenchmark run, and `scripts/perf/compare.mjs` the verdict, with R5's thresholds as constants.
- `.claude/skills/perf-nightly/SKILL.md` is the procedure the agent follows: it calls the subcommands in order and does the parts that take judgement, which are reading the `Perf:` lines, picking a suspect, and writing the revert PR, the bug, and the report.
- A Claude scheduled task starts a local agent session at 02:00 that runs the skill.
- The build worktree is `.claude/worktrees/perf-nightly` in the main checkout. The tooling runs from it too, at `origin/main`: `measure` moves its HEAD to build a SHA and puts it back when it finishes.
- State and results live in the main checkout's `perf-results/nightly/`, which git ignores: `state.json`, `builds/<sha>/`, `bench-builds/<sha>/`, and one folder a night with its captures, build logs, comparison tables, and report.

`node scripts/perf.mjs setup` creates the worktree and the first `lastTested`.

## What has run

As of 2026-11-10:

- `compare` against the repo's earlier captures: the 2026-11-01 `main` pair against the 2026-11-02 ground-blending pair at the desert view answers `regressed` with no major cell, and names the one `tileGpuMs` cell the ground-textures ledger records. The unpinned 2026-10-31 `main` captures against the held 2026-11-01 ones answer `major`, which is the GPU downclocking WOR-553 found.
- `range` against `main`'s history: eight merges, six relevant by path, one salal bump, one `Perf: none` read from its PR.
- `idle-wait` and the gate inside `measure`, on a working day: a capture voided itself when another session's `cmake` started, and the batch then held for 25 minutes while the machine alternated between `idle` and `mostly-idle`, never reaching two idle checks in a row.
- `measure` and `compare` on two real SHAs at the river view, one run each, with the gate off: both built in the batch worktree, the game launched from the build copies, and `compare` answered `clean` with the tip's capture marked loaded. That run found two faults, both fixed: a base build older than the `uprights` view answered 404 and stopped the capture, and the batch baked its own planet (R4).
- `bench` and `compare --bench`, with the gate off: the assets benchmarks built at two real SHAs in the batch worktree and run twice each, and the world benchmarks run twice from one build, which agreed within about 3%. On `main`'s history `range` names `world` and `worldgen` and leaves `assets` out, because the tree-variety merge changed its benchmark.
- The cover cell (2026-11-13): `compare.test.mjs` covers the first-night rule (a cover on one side only is `unjudged`, either way round; no `revealVerb` counts as no cover; both sides with it are judged and a step is major) and that an unjudged cell never lifts a night above clean. `measure --plan` prints sixteen launches. Functionally, with the numbers ignored: two fresh launches of the game from a build of this branch each took a `river-cover` capture (`revealVerb` true, `reveal` false, `/api/state?what=visibility` read `revealed:false` during and after, zooms 3, 1.5 and 0.75, no scroll scenarios). `compare` judged that pair, and with `revealVerb` falsified on the base, as a build without the verb would record it, both `compare` and `compare --night` (on a throwaway folder) answered `unjudged` for the cell and `clean` overall. `measure` itself has not run end to end.
- Not run with numbers that mean anything. Every capture and benchmark so far was taken with other sessions working.
- Not run: a scheduled night end to end, isolation (R6), and filing an outcome (R7). The thresholds in R2 and R5 are first values.

## Related

- [Testing strategy](./README.md#performance)
- [Ground textures: clock state and the GPU hold](../technical/ground-textures.md#gpu-hold-wor-553)
