---
name: perf-nightly
description: Run the nightly perf batch for world-sim - wait for an idle machine, time main's tip against the last tested SHA, and on a major regression isolate the PR and open a draft revert. Use when the scheduled 02:00 task fires, or when asked to run the nightly perf batch or perf-nightly.
---

# perf-nightly

Runs the batch specced in `docs/testing/nightly-perf-batch.md`. Read that spec once before the first step; this file is the procedure.

Nobody is watching. Don't ask questions; make the call the spec implies, and write down what you did in the report.

## Rules that hold all night

- **The script decides, you don't.** Verdicts come from `perf.mjs compare`. Never read raw timings out of a capture file and form your own view of whether something regressed.
- **Never merge, revert, or push to `main`.** The outcome of a regression is a draft PR.
- **Never time anything outside `perf.mjs measure` and `perf.mjs bench`.** They hold the idle gate; a capture or benchmark you run by hand doesn't.
- **Stop at 07:00.** `measure` stops itself, a build in progress included. If you are about to start a step after 07:00, write the report instead.
- `idle-wait`, `measure` and `bench` run for a long time. Start them with the Bash tool's `run_in_background: true`, redirect stdout to a file in the night's folder, and wait for the completion notice. Don't poll, don't sleep.
- No AI attribution in any PR, comment, or commit.

## 0. Get on the current tooling

Everything runs from the batch's own worktree, `C:\Users\kevin\Code\worldsim\.claude\worktrees\perf-nightly`. It is permanent; never remove it.

```bash
cd C:/Users/kevin/Code/worldsim/.claude/worktrees/perf-nightly
git fetch origin
git checkout --detach origin/main
```

If the worktree is missing, run `node scripts/perf.mjs setup` from any checkout of the repo, then come back here. `measure` moves this worktree's HEAD while it builds and puts it back on `origin/main` when it finishes; if a command ever fails with a missing script, repeat the checkout above.

## 1. What needs a run

```bash
node scripts/perf.mjs range
```

It answers `base` (the last tested SHA), `tip`, every merge between them with its PR, `Perf:` line and whether it is perf-relevant, and the open regressions. Two lists decide the night:

- `relevant`: merges that could move frame time. Not empty means the frame captures run (`measure`).
- `benchTargets`: the libraries whose microbenchmarks a merge reached (`world`, `assets`, `worldgen`). Not empty means those run (`bench`). They time chunk and planet generation directly, with no game. `benchRewritten` names a target left out because a merge changed its `.bench.cpp`, so base and tip time different work: say so in the report.

Then:

- Both empty: `node scripts/perf.mjs state --set-tested <tip>`, write a one-line report (step 6), done. Don't wait for idle, don't build.
- Otherwise go on with whichever of the two the range calls for. The whole range is tested at once on the tip.

## 2. Wait for the machine

```bash
node scripts/perf.mjs idle-wait --until 06:00
```

In the background. It checks every 5 minutes and exits 0 on two idle checks in a row, or 3 at 06:00. On 3: write the report with what was busy (its `checks` list names the processes) and stop. `lastTested` stays where it was, so tomorrow's range includes today's.

## 3. Measure and get the verdict

```bash
node scripts/perf.mjs measure --refs <base>,<tip>
node scripts/perf.mjs bench --refs <base>,<tip> --targets <benchTargets>
node scripts/perf.mjs compare --night <night> --base <base> --tip <tip> --bench <benchTargets>
```

Run `measure` only when `relevant` is not empty and `bench` only when `benchTargets` is not empty, one after the other, never together. With no `measure`, give `compare` `--views none`; with no `bench`, leave `--bench` off.

`measure` (background) builds both SHAs, then captures river, river-cover (the river with the player visibility cover drawn), coast and desert, two runs each, interleaved, redoing a void capture once after the machine is idle again. `bench` (background) does the same with each target's benchmark executable. Each one's JSON names the night folder (`night`), what it took (`taken`), any build that failed, and whether it stopped early.

`compare` answers an overall `verdict`, one per view (`views`) and one per benchmark target (`benchmarks`), and writes the tables to a markdown file it names. It expects two runs a side of every view and every target it was given, and one short of that is `inconclusive`. After a `measure` or `bench` narrowed with `--views`, `--targets` or `--runs`, give `compare` the matching `--views`, `--bench` and `--runs`.

| Verdict | Meaning | Do |
|---|---|---|
| `clean` | nothing past noise, or only improvements | set tested, report |
| `regressed` | past noise, under the major threshold | set tested, list the cells in the report, nothing else |
| `unjudged` | only the `river-cover` cell can be this: base and tip differ in having the player visibility cover (the base predates it) | treat as clean; say in the report that the base has no cover and the cell had no verdict. Never isolate or revert it |
| `inconclusive` | a capture was loaded twice over, or the night stopped or failed short of its captures (the view's `incomplete` says which are missing) | report it; leave `lastTested` alone so the range is retried |
| `major` | a major regression | step 4 |

`measure` and `bench` list a take that couldn't run with an `error`. "The game exited before its scene was ready" on every capture of one SHA usually means that SHA can't load the shared planet (a worldgen format change): report it, don't bake a planet, and leave `lastTested` alone.

A build failure of the tip is not a perf result: report it and leave `lastTested` alone. A build failure of the base: use the newest earlier SHA from `git log --first-parent` that builds, and say so.

## 4. Isolate a major regression

Work from what the night already produced before building anything.

A major benchmark is isolated the same way as a major view, with `bench --targets <the target that failed>` in place of `measure --views`, `compare --views none --bench <target>`, and only the merges whose `bench` list holds that target as candidates. Its `moved` cells name the benchmarks, and the benchmark's name says what it times.

1. **Name a suspect.** `compare`'s `moved` cells say what got slower (`tileGpuMs`: the ground and water pass; `gpuMs` without it: entities, trees, UI; `entityMs`, `updateMs`, `sceneUpdateMs`: CPU side), and its `counts` say whether draw calls, triangles or upright draws moved with it. `range`'s `Perf:` lines and `perfFiles` say which PR touched that. A PR whose `Perf:` line predicted the cost is the first suspect.
2. **Test the suspect.** `measure --refs <suspect>^1,<suspect> --views <the views that failed>` then `compare` those two with the same `--views`. Major: that's the offender. Clean: go to 3.
3. **Bisect.** Over the range's relevant merges in first-parent order, halve: measure the middle merge against `base` on the failing views. Major means the offender is at or before it, clean means after. `measure` reuses captures already taken that night, so `base` is not re-captured.
4. **Check for a second one.** On a scratch branch that is never pushed, in the batch worktree: `git checkout -B perf/revert-check <tip> && git revert -m 1 --no-edit <offender>`. Measure `perf/revert-check` against `base` on the failing views. Clean: one offender. Still major: repeat from 1 on the remaining merges, adding each new offender's revert to the scratch branch. `measure` leaves the worktree detached on `origin/main`; finish with `git branch -D perf/revert-check`.
5. **Salal.** Only if the offender's `salalBump` is true: run salal's perf suite at the old and new tags (the `GIT_TAG` values in that merge's `CMakeLists.txt` diff) from `C:\Users\kevin\Code\salal`, per its own docs, to say whether the engine's numbers moved. If they did, file it in salal's board too. Never run salal's suite otherwise.

If a revert doesn't apply cleanly, here or in step 5, don't resolve conflicts: `git revert --abort`, report the offender and the conflict, and skip its PR.

If 07:00 arrives mid-isolation, report how far the range was narrowed. Leave `lastTested` alone; tomorrow continues.

## 5. File the outcome

For each offender:

1. Make the offender's own branch from the tested tip, holding only its revert: `git checkout -B perf/revert-<pr> <tip> && git revert -m 1 --no-edit <offender>`, push it, then `git checkout --detach origin/main`. Open a **draft** PR against `main`: title `Revert "<original title>" (perf regression)`, body with what regressed, `compare`'s tables for base against tip and for the offender alone, the date, and `Perf:` saying what the revert restores. With several offenders that is one branch and one PR each; the scratch branch from step 4 held them together only to measure.
2. Comment on the original PR with a link to the revert PR and the two or three worst cells.
3. File a bug on the board (Specboard) titled with the regression and the PR, `discovered_from` the original PR's item when you can find it by branch or key, linking the revert PR.
4. `node scripts/perf.mjs state --open-regression '{"pr":<pr>,"merge":"<sha>","revertPr":"<url>","bug":"<key>"}'`

Then `node scripts/perf.mjs state --set-tested <tip>`: the tip is the new base either way, so tomorrow measures against it and doesn't report the same regression again.

Each night, check the open regressions `range` lists: if a revert PR was merged or closed, `state --close-regression <pr>`.

## 6. Report

Write `<night>/report.md` (for a no-run night, `perf-results/nightly/<date>/report.md` in the main checkout, which `measure` would have created):

- The range: base, tip, each merge with its PR and whether it was relevant and why.
- The verdict per view and per benchmark target, with `compare`'s markdown tables pasted in.
- Anything void, inconclusive, skipped, or that failed to build.
- What was isolated and how (suspect or bisect, which merges were measured).
- Links to every PR, comment and bug filed.
- Open regressions still waiting on Kevin.

End the session with a three or four line summary of the report: verdict, offender and revert PR if any, and the report's path.
