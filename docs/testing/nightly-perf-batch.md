# Nightly perf batch

Status: spec, not built. Created 2026-11-10.

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

The gate holds for the whole batch, not only the start. Every capture records CPU and GPU use by processes other than the game alongside its samples. A capture that saw foreign load, or a sample off P0, is void: the batch waits for idle again and redoes that one capture, once. Void twice and that view is inconclusive for the night.

If the machine never goes idle by 06:00, or the batch is still running at 07:00, it stops. The report says what was busy (the processes that failed the gate) and the untested range carries to the next night.

## R3. What needs a run

State lives in `perf-results/nightly/state.json`: the last `main` SHA the batch finished on (`lastTested`) and the regressions still open.

1. The range is the first-parent merges in `lastTested..origin/main`. `main` takes merge commits, so each one is a PR.
2. A merge is perf-relevant if its PR's `Perf:` line says so, or its diff touches `libs/world-view`, `libs/world`, `libs/sim`, `libs/assets`, `apps/world-sim`, shaders, Lua generators or asset definitions, or the salal `GIT_TAG`. A `Perf: none` on a diff that touches those paths is overruled by the paths.
3. No relevant merge in the range: advance `lastTested`, write a one-line report, done. No build, no launch.

Everything relevant is already together on the tip, so the first run covers the whole day at once.

## R4. The run

A dedicated worktree, `perf-nightly`, with its own build directory and its own `planets/` (a real directory, not a junction, so a worldgen format change can bake there without touching the shared planet). RelWithDebInfo, 3072x1728.

- Build `lastTested` (base) and the tip. ccache makes the base cheap.
- Capture both the same night, interleaved base, tip, base, tip: two runs each at the river, coast, and desert views (`organic-terrain/terrain-polygons-architecture.md`, section 6). Twelve captures.
- Each capture is today's suite: idle at zooms 20, 8, 3, 1.5, 0.75, 0.5, 0.25, then the two scroll scenarios, with the GPU hold on.

`scripts/perf-capture.ps1` is ported to Node as part of this and deleted; the capture becomes a function the batch calls.

## R5. Verdict

The script decides. The agent never reads raw timings and forms its own view.

- Noise per cell (view x scenario x metric) is the spread between the two runs of the same SHA that night.
- A cell regressed when the tip's median is worse than the base's by more than three times that noise.
- A regression is major when a regressed cell is worse by more than 10% and more than 0.25 ms on `gpuMs`, `tileGpuMs`, or frame p50, or a scroll scenario gains 33 ms spikes. Starting values, constants in the script.
- Regressed but not major is listed in the report and nothing else happens.
- Draw calls, triangles, and upright counts are reported per cell as context, and a count that moved is the first clue to the cause.

## R6. Isolating a major regression

1. Read what's already there before building anything. Which cells and metrics moved points at a subsystem (`tileGpuMs` at the ground shader, upright draws at trees, `updateMs` at the sim), and the range's `Perf:` lines and diffs say which PR touched it. That names a suspect.
2. With a suspect: build the suspect merge and its first parent, capture only the views that failed, two runs each. If the step is there, it's the offender.
3. Without one, or if the suspect was clean: bisect the range's perf-relevant merges on first-parent, halving, failing views only.
4. With the offender found, build the tip with that merge reverted and capture again. Clean: done. Still regressed: there is a second offender, repeat from step 1 on what's left.
5. Salal's perf suite runs only when the offending merge changed the salal `GIT_TAG`, at the old and new tags, to say whether the engine's numbers moved (file it in salal) or worldsim's use of it did. Never otherwise.

The idle gate and the 07:00 stop apply throughout. A night that runs out reports what it narrowed the range to and picks up there the next night.

## R7. Outcome

- Clean: `lastTested` advances to the tip.
- Major regression: for each offender, a draft PR reverting its merge (`git revert -m 1`), with the table of cells, base and tip numbers, and noise; a comment on the original PR linking it; a bug in Specboard filed from the original item. Kevin decides whether the revert merges. `lastTested` advances to the tip either way, and the offender is recorded as open so later nights measure against it instead of reporting it again.
- Every night writes `perf-results/nightly/<date>/report.md` and the raw captures beside it: the range, what was relevant, the verdict per view, anything void or inconclusive, and open regressions. The agent ends its session with the report's summary.

The batch never merges or reverts on `main` itself.

## How it's built

An agent orchestrates and a script measures. The script, `scripts/perf-nightly.mjs` (Node, ESM, no dependencies), has one subcommand per deterministic step: `idle-wait`, `range`, `measure <sha> [--views]`, `compare <base> <tip>`, `report`. A Claude scheduled task starts a local agent session at 02:00, which runs a `/perf-nightly` skill that calls them in order and does the parts that take judgement: reading the `Perf:` lines, picking a suspect, writing the revert PR and the bug.

Setup is scripted, with no manual steps: creating the worktree and its planet, the schedule, and the first `lastTested`.

## Work breakdown

1. Port the capture to Node with the foreign-load recording; delete the `.ps1`.
2. `idle-wait` on top of `scripts/machine-load.mjs` (built; the one-shot check sessions use), with its log of what failed the gate.
3. `range` and the relevance rules.
4. `measure` in the `perf-nightly` worktree, and `compare` with the verdict.
5. The `/perf-nightly` skill: isolation, revert PR, bug, report.
6. Setup script and the Claude scheduled task.

## Related

- [Testing strategy](./README.md#performance)
- [Ground textures: clock state and the GPU hold](../technical/ground-textures.md#gpu-hold-wor-553)
