# Build & Test Speed Improvements (PRs #252, #253)

**Date:** 2026-07-03
**Epic:** Build & test speed improvements (Specboard)
**Spec:** /docs/technical/build-performance.md

## Summary

The CI gate was 49 minutes per non-draft push and local Windows builds compiled each
library's files serially (VS generator, no /MP, no launcher support). Two PRs land the
fix: CI moves to sccache + Ninja-on-Windows with the heavy worldgen bucket sharded and
path-gated into its own workflow (#252); local builds move to Ninja + ccache presets on
both OSes with a one-time MSVC environment script (#253).

## Measured

Local (32-core Windows, Debug, warm vcpkg):

| | MSBuild before | Ninja + ccache after |
|---|---:|---:|
| Clean build | 6m18s | 1m14s cold cache |
| Clean rebuild, warm cache (= fresh worktree) | 6m18s | 28s |
| No-op | 4.1s | 0.6s |
| Incremental (1 engine .cpp) | 4.6s | 2.6s |
| Fast tests | 2m58s sequential | 1m09s (ctest -j 13) |

CI before: Tests = 49 min wall (Windows heavy bucket alone 27 min, serialized). After:
see the Results table in the spec (filled from real runs).

## Decisions of note

- Heavy worldgen tests are path-gated (tests-heavy.yml): they run only when
  libs/world, libs/foundation, vcpkg.json, or the root CMakeLists change, plus a
  nightly backstop on main. Worldgen cadence != game-code cadence.
- Heavy bucket sharded via GTEST_TOTAL_SHARDS (4 ctest entries), not per-suite lists.
- ccache locally (base_dir normalizes worktree paths -> one shared cache), sccache in
  CI (GHA cache backend). Not interchangeable: sccache has no base_dir, ccache has no
  GHA backend.
- tests.yml/build.yml also run on push to main so main-scoped caches exist; GitHub
  Actions caches are branch-scoped, and without a main run every PR would start cold.
- Found by the first parallel CI run: test exes that spawn PlanetGenerator pools
  (hardware_concurrency - 1 threads each) starve each other under ctest -j4 on 4-core
  runners — planet-view's PlanetColorizerBake tests blew their generation deadline and
  baked the exact gray sheet they guard against. Fixed with PROCESSORS 3 on world-tests
  and planet-view-tests, a 300 s deadline, and a hard failure (instead of a silent
  partial-world bake) when generation doesn't complete.
- vcpkg action + pin deliberately untouched; refreshing the pin would break the binary
  cache backend it uses (removed in newer vcpkg). Migration path documented in the spec.
- Rejected: C++20 modules (tooling immature in 2026), unity builds (kill cache hit
  rates), build-system migration. Deferred with re-entry conditions: PCH, lld-link,
  changed-paths test selection.

## Files

- .github/workflows/{tests,tests-heavy,build,code-quality}.yml
- CMakePresets.json, CMakeLists.txt (tri-state BUILD_DEVELOPER_CLIENT, 3.25 minimum)
- libs/world/CMakeLists.txt (heavy shards), libs/*/CMakeLists.txt (/Zi removal x8)
- libs/foundation/CMakeLists.txt (httplib/OpenGL PRIVATE)
- scripts/setup-msvc-env.ps1 (new), README.md, docs/workflows.md
- docs/technical/build-performance.md (spec)

## Machine migration (each dev machine, once)

winget install Ccache.Ccache (or brew install ccache ninja), run
scripts/setup-msvc-env.ps1 on Windows, apply the ccache config from the README, delete
existing build/ dirs (generator switch). First rebuild repopulates the shared cache.

## Next steps

- PCH rollout after measuring cache interaction (epic task T3)
- Measurement week: fill the spec Results table from real PR runs, tune shard count if
  skewed, revisit lld-link (epic task T4)
- macOS smoke test of the default preset (brew install ccache ninja; cmake --preset default)
