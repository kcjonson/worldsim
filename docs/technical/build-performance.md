# Build & Test Performance

**Status:** in progress (2026-07-03)
**Scope:** clean builds, incremental builds, fresh-worktree builds, local tests, CI.

The codebase (~170k LOC C++20, 8 static libs + 5 apps, ~286 translation units) outgrew the
original build configuration. This doc records the measured baseline, the decisions taken, and
the items deliberately deferred, so future changes don't re-litigate them.

## Baseline (measured 2026-07-03)

CI, per non-draft PR push. The Tests workflow was the merge gate at **49 minutes** wall:

| Step | Linux (clang Debug) | Windows (MSVC Debug) |
|------|--------------------:|---------------------:|
| Configure incl. vcpkg | 2:39 | 4:30 |
| Build (clean, every run) | 6:29 | 9:51 |
| Fast tests (1272 tests, sequential ctest) | 6:01 | 6:49 |
| Heavy worldgen tests (47 tests, one ctest entry) | 16:00 | 27:08 |

Local (Windows, 32 logical cores, VS 2022 generator): the generated projects had no `/MP`, so
each library's files compiled serially. The VS generator also cannot run compiler launchers,
which blocked object caching entirely. Every fresh worktree paid a full configure plus a full
compile (~2.5-2.9 GB build dir each).

Local before/after timings: see Results below.

## The four levers

1. **Ninja instead of MSBuild/Makefiles.** Schedules all TUs across all cores, supports
   compiler launchers, near-instant no-op builds. Local uses Ninja Multi-Config (keeps
   `--config` and the `build/<target>/<config>/` layout); Windows CI uses plain Ninja.
2. **Compiler caching.** ccache locally (cross-worktree hits via `base_dir` path
   normalization), sccache with the GitHub Actions cache backend in CI. This is what makes
   "build only what changed" true at the object level, including in fresh worktrees.
3. **Path-gated heavy tests.** Worldgen changes at a different cadence than game code, so the
   heavy worldgen bucket runs only when worldgen inputs changed (`libs/world/**`,
   `libs/foundation/**`, `vcpkg.json`, root `CMakeLists.txt`), plus a nightly backstop run.
4. **Parallel and sharded ctest.** `ctest -j` across the per-lib test exes; the heavy bucket
   split into 4 gtest shards (`GTEST_TOTAL_SHARDS`/`GTEST_SHARD_INDEX`) so one slow exe stops
   serializing the run.

## Decisions

### CI

- **sccache (GHA backend)** on every compiling job: `mozilla-actions/sccache-action` +
  `SCCACHE_GHA_ENABLED=true` + `CMAKE_{C,CXX}_COMPILER_LAUNCHER=sccache`. Each job prints
  `sccache --show-stats`; a Windows hit rate near zero means someone dropped the `/Z7` flags
  below.
- **Windows CI switched from the VS generator to Ninja** (`ilammy/msvc-dev-cmd` provides the
  MSVC environment). Required because the VS generator ignores compiler launchers. MSVC object
  caching also requires embedded debug info: `-DCMAKE_POLICY_DEFAULT_CMP0141=NEW
  -DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded` (that's `/Z7`).
- **Heavy bucket sharded, not suite-split:** 4 ctest entries per platform running gtest shards.
  Shards balance automatically by test index; a per-suite list would be dominated by
  PlateSimHeavy + TerrainStageHeavy and need maintenance. Shard count is one number in
  `libs/world/CMakeLists.txt`. Note the speedup is sublinear: pipeline-based heavy tests
  already use an internal thread pool.
- **Heavy tests moved to `.github/workflows/tests-heavy.yml`**, triggered by worldgen paths,
  nightly schedule, and manual dispatch. The job builds only the `world-tests` target. The
  nightly run on main is the safety net for regressions that slip past the path filter
  (toolchain drift, indirect breakage of the golden-hash tests).
- **build.yml artifact upload deleted.** It uploaded the entire multi-GB build tree with 1-day
  retention and had zero consumers.
- **vcpkg setup left exactly as is.** `lukka/run-vcpkg@v11` with the pinned vcpkg commit is
  what makes configure extraction-shaped instead of a 20+ minute source build. The pinned
  commit predates vcpkg's removal of the `x-gha` binary-cache backend; refreshing the pin
  will silently break binary caching. When a `vcpkg.json` baseline bump forces a newer vcpkg,
  switch binary caching to `VCPKG_DEFAULT_BINARY_CACHE` + `actions/cache` in the same PR.
- **tests.yml and build.yml also run on push to main.** Actions caches are branch-scoped: a
  PR run can only restore caches created on the same PR or on main. Without a main run, every
  new PR would start cold; the post-merge run is what seeds the warm start. (Public repo,
  minutes are free.)
- **Pool-spawning test exes declare `PROCESSORS`.** world-tests and planet-view-tests run
  PlanetGenerator pipelines whose internal pool is `hardware_concurrency - 1`; under
  `ctest -j4` on a 4-core runner they starved each other (planet-view's generate() blew its
  deadline and baked the gray sheet its own tests guard against). `PROCESSORS 3` keeps ctest
  from co-scheduling them. Any new test exe that runs full pipelines needs the same property.
- **Hygiene:** every workflow got a per-PR `concurrency` group with `cancel-in-progress` (a
  force-push cancels the superseded run) and `paths-ignore: docs/**, **.md, .claude/**`.
  Draft PRs still skip CI entirely.
- **`BUILD_DEVELOPER_CLIENT` is a tri-state** (`AUTO`/`ON`/`OFF`, default `AUTO` = build in
  single-config Debug/Development). Under the VS generator Windows CI never built the npm app;
  the Ninja switch would have silently added npm install + Vite to every Windows CI run.
  Windows CI passes `OFF`; local and Linux behavior are unchanged.
- Cache budget: vcpkg binaries ~1.3 GB + sccache objects ~4-6 GB steady state, inside the
  10 GB per-repo Actions cache limit. Don't set `SCCACHE_CACHE_SIZE` (ignored by the GHA
  backend).

### Local (Windows and macOS)

- **Presets:** `windows` = Ninja Multi-Config, MSVC (`cl` pinned), ccache launcher, embedded
  debug info for Debug/RelWithDebInfo. `default` (macOS/Linux) = Ninja, ccache launcher.
  Previously the default preset had no generator at all, so macOS used serial Makefiles.
  `binaryDir` stays `build/`; all documented commands and per-config output paths are
  unchanged.
- **ccache required on dev machines** (`winget install Ccache.Ccache` / `brew install ccache
  ninja`). Per-machine config:

  ```
  ccache --set-config max_size=30G
  ccache --set-config base_dir=<dir containing checkout and worktrees>
  ccache --set-config hash_dir=false
  ccache --set-config sloppiness=pch_defines,time_macros
  ```

  `base_dir` + `hash_dir=false` normalize absolute paths so every worktree shares one cache:
  a fresh worktree's first build is mostly cache replay. Caveat: replayed objects embed the
  original worktree's paths in debug records; the debugger opens a sibling worktree's file
  with identical content. Harmless for iteration.
- **`scripts/setup-msvc-env.ps1`** (Windows, one-time, idempotent): persists the vcvars64
  environment (PATH additions, `INCLUDE`, `LIB`) plus `VCPKG_ROOT` to the User environment so
  `cl`, `ninja`, and vcpkg resolve from any plain shell. Re-run after a VS Build Tools update;
  the failure mode is `cannot open include file: 'corecrt.h'`.
- **`cmake_minimum_required` 3.20 → 3.25** for `CMAKE_MSVC_DEBUG_INFORMATION_FORMAT`
  (CMP0141).
- **Removed hardcoded `/Zi`** from the 8 `<lib>-tests` targets (`/Od /Zi` → `/Od`). An
  explicit `/Zi` overrides the embedded format and silently disables ccache for every test TU.
  Don't reintroduce it.
- **foundation links httplib and OpenGL PRIVATE** (were PUBLIC, propagating their include dirs
  to every TU in the repo; `DebugServer.h` forward-declares, only the .cpp includes httplib).
  glm stays PUBLIC deliberately: public headers expose glm types.
- **Local test invocation:** `ctest --test-dir build -C Debug -LE heavy -E benchmarks -j 13
  --output-on-failure` (omit `-C Debug` on single-config macOS/Linux). Don't use `-L fast`:
  only world-tests carries labels, so it would silently run one exe.

## Considered and rejected

- **C++20 modules / `import std;`**: CMake+MSVC tooling still immature in 2026, sccache can't
  cache MSVC module builds, and adoption means restructuring every header. Newer language
  standards (C++23/26) don't change compile times otherwise. Revisit ~2027.
- **Unity builds**: conflict with compiler-cache hit rates (batch contents shift as files are
  added). Caching won.
- **Build-system migration (Bazel etc.)**: complexity out of proportion at this size.
- **Repo-wide include cleanup**: the expensive transitive includes (glm in 44 headers, GL in
  21) are exactly what a PCH amortizes; dozens of file edits for near-zero residual win.

## Deferred, with re-entry conditions

| Item | Do it when |
|------|-----------|
| Precompiled headers (`cmake/pch.cmake`, per-target PRIVATE std+glm+GL sets) | After measuring MSVC PCH x ccache and PCH x sccache interactions on a branch; CI escape hatch is `-DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON` |
| lld-link / mold | Debug link of world-sim exceeds ~15 s post-Ninja (`CMAKE_LINKER_TYPE LLD`, CMake ≥3.29) |
| Changed-paths test selection script | Full fast suite exceeds ~5 min wall locally |
| vcpkg pin refresh + files-based binary cache | A vcpkg.json baseline bump forces it (do both together) |
| npm stamp for developer-client on macOS Debug | macOS iteration is annoyed by npm install on every build |

## Results

Local numbers measured 2026-07-03 (32-core Windows box, Debug, warm vcpkg binary cache).
CI warm numbers to be filled from real PR runs during the measurement week.

| Metric | Before | After |
|--------|-------:|------:|
| CI gate, non-worldgen PR (warm) | 49:00 | tbd (target 10-13 min) |
| CI gate, worldgen PR (warm) | 49:00 | tbd |
| Local clean Debug build | 6:18 (MSBuild) | 1:14 (Ninja, cold ccache) |
| Local clean rebuild, warm ccache (= fresh worktree) | 6:18 | 0:28 |
| Local no-op build | 4.1 s | 0.6 s |
| Local incremental (1 engine .cpp) | 4.6 s | 2.6 s |
| Local configure | 13.9 s | 11.9 s |
| Local fast test suite | 2:58 (sequential) | 1:09 (`-j 13`) |

The warm-rebuild number still includes ~110 real recompiles (test targets' hardcoded `/Zi`
made them uncacheable in the measurement); it improves further once the /Zi removal and the
presets PR are both in.

## Related

- Epic: Specboard "Build & test speed improvements"
- [Monorepo Structure](./monorepo-structure.md)
- CI workflows: `.github/workflows/{tests,tests-heavy,build,code-quality}.yml`
