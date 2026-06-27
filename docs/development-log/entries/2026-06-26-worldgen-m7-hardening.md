# Worldgen M7: hardening, determinism gate, benchmark-calibrated default

## Summary

Closed out the last engineering milestone of the **World Generation & Creator**
epic. Three independent pieces: the pipeline now fails loud on bad input instead
of relying on debug-only asserts that compile out in release; a pinned golden
worldHash turns the existing dual-platform CI into a cross-platform determinism
gate; and the default grid resolution is now a measured, documented constant
rather than a guess.

## Details

### Hardening (fail loud, fail fast)

- `PlanetParams::validate()` ([PlanetParams.cpp](../../../libs/world/worldgen/data/PlanetParams.cpp))
  checks every input envelope (gridSubdivision, plate count, waterAmount,
  eccentricity, star/planet/orbit scalars) and returns a human-readable reason.
  `PlanetGenerator::start()` calls it synchronously before spawning the worker,
  so a bad request ends in `Failed` with a reason on the calling thread — no
  worker, no crash. The old silent in-pipeline plate-count clamp is gone
  (validation owns the range now).
- New failure-reason channel: `PlanetGenerator::failureReason()` accessor
  (mutex-guarded so `progress()` stays lock-free); `fail()` logs every reason
  centrally via `LOG_ERROR`, and `worldgen-cli` surfaces it. A shared
  `GenerationError` exception
  ([GenerationError.h](../../../libs/world/worldgen/pipeline/GenerationError.h))
  carries the reason out of any stage; the runner catches it, plus a dedicated
  `std::bad_alloc` catch that reports the n and estimated MB.
- Pre-flight memory estimate (33 B/tile) logged at start; rejects absurd n
  before allocating.
- NaN/Inf sweep over the two float fields (elevation, flowAccum) at end of
  pipeline in all builds; per-stage in debug to localize which stage introduced
  it.
- assert -> runtime where a release build would otherwise UB:
  `TerrainStage` null-history deref now throws `GenerationError`; `LandingSite`
  null-grid / missing-field paths log and return a safe default instead of
  dereferencing null. (`CrustStage`'s plateId assert was left: it already clamps
  safely in release, no UB.)
- `PlanetIO` already brackets file size via truncation + tile-count + trailing
  checks; tightened the trailing-data check to read one byte and confirm EOF
  rather than trusting `peek()`, and unified its subdivision cap with the new
  shared `kMaxGridSubdivision` so loads and generation reject the same range.

### Cross-platform determinism gate

- A critical-path audit found **no** stray `std::` transcendentals in
  stages/tectonics/grid — it's already fully on `det_math` (SphereGrid uses only
  `std::sqrt`/`floor`/`abs`, which are exact IEEE and permitted). No conversions
  needed.
- `PlanetGeneratorHeavy.GoldenWorldHash` pins the full-pipeline worldHash at
  n=64, seed 0x5151515151515151 to `0xc296bb3b9c7debe8`. The heavy bucket runs
  on both Windows and Linux CI, so a single committed golden both must match IS
  the cross-platform gate — no inter-runner artifact comparison. Update policy
  mirrors `PlateSimHeavy.GoldenProductHash`.
- `worldgen-cli --expect-hash <hex>` (exit 5 on mismatch) for manual
  cross-machine spot checks, alongside the existing `--verify-threads`.
- macOS is not in CI (no runner); left as a follow-up.

### Benchmark-calibrated default resolution

Full-pipeline gen-time sweep on the reference dev machine (RelWithDebInfo, seed
424242):

| n    | tiles  | gen time |
|------|--------|----------|
| 256  | 0.65M  | 8.4s     |
| 384  | 1.5M   | 9.8s     |
| 512  | 2.6M   | 10.8s    |
| 768  | 5.9M   | 15.6s    |
| 1024 | 10.5M  | 25.2s    |
| 1449 | 21M    | 44.2s    |

`gridSubdivision` default is now the named `kDefaultGridSubdivision = 1024`,
commented with the sweep so the next person re-picks from data, not a guess.
1024 was kept (the quality/time point chosen against the measured curve). The
sweep is reproducible via the CLI (`worldgen-cli --n <n>`), which already prints
gen time, so no separate bench harness was added. WorldCreator already exposes
the full preset range (256/512/1024/1449/2048), so no UI work was needed.

## Verification

- 168 fast world-tests green; `PlanetGeneratorHeavy.GoldenWorldHash` and
  `CrustStageHeavy.DeterministicAcrossThreadCounts` green; new
  `PlanetGenerator.RejectsInvalidParams` green.
- `worldgen-cli --n 5000` and `--water 2.0` fail loud (reason + exit 1, no
  crash); `--verify-threads 1,2` PASS; `--expect-hash` PASS on match / exit 5 on
  mismatch.
- world-sim builds; default n unchanged (1024) so the New Game happy path is
  behaviorally identical, interactive click-through not re-run.

## Related Documentation

- Epic: World Generation & Creator (Specboard); `/docs/design/features/world-generation/`
- Determinism foundation: `libs/foundation/math/DeterministicMath.h`,
  `libs/foundation/utils/WorldHash.h`

## Next Steps

- macOS CI runner + add it to the determinism gate.
- Deferred epics (separate): planet-DB streaming for n>=4096; landing-site
  local preview + difficulty rating UX.
