# Windows / MSVC Port + Clean-Checkout First Run

**Date:** 2026-06-09

## Summary

First time building the project on a second machine (Windows 11, Visual Studio 2022 / MSVC). The
code had only ever run on macOS/Clang. Got world-sim and ui-sandbox building and running on Windows,
fixed the portability gaps MSVC surfaced, and closed the setup traps that stop a clean checkout from
running on its own (either OS). All 7 test suites pass on Windows.

## What broke and why

Three classes of issue, all rooted in Clang being lenient where MSVC is strict or in macOS-only
assumptions:

1. **Out-of-order designated initializers** (C7560). C++20 requires `.field =` designators in struct
   declaration order; Clang accepted any order, MSVC rejected ~30 sites. Reordered the call sites.
2. **POSIX / non-standard APIs in tests.** `mkstemp`, hardcoded `/tmp` paths, and `M_PI` are not
   portable. Replaced with `<filesystem>` (`temp_directory_path()`) and `<numbers>`.
3. **UTF-8 source.** A Unicode char literal failed under MSVC's default codepage (C2015).

Two setup traps, neither fixable purely in source, that block a fresh checkout:

4. **Stale vcpkg.** A vcpkg checkout older than `vcpkg.json`'s `builtin-baseline` can't resolve the
   pinned versions (`no version database entry for ...`). Documented the fix.
5. **Missing generated artifacts.** `fonts/Roboto-SDF.png` is gitignored, so it never transfers
   between machines; `assets/config/...` wasn't reaching the exe dir under multi-config generators.

## Changes

- **Source (8 files):** reordered designated initializers to declaration order in
  `LayoutScene`, `ScrollScene`, `TextInputScene`, `TextWrapScene` (ui-sandbox) and `GameScene`,
  `BuildMenu`, `GameplayBar`, `TaskListView` (world-sim).
- **Tests:** `Tessellator.test.cpp` (`M_PI` -> `std::numbers::pi_v<float>`, `/tmp` ->
  `temp_directory_path()`), `WorkConfig.test.cpp` (`mkstemp` -> `temp_directory_path()`).
- **CMakeLists.txt:** `-Werror=reorder-init-list` on Clang/GCC (catch the designator bug at its
  source); `/utf-8` on MSVC; a `font-atlas` target that auto-generates the SDF atlas when missing.
- **apps/world-sim/CMakeLists.txt:** asset sync now targets `$<TARGET_FILE_DIR:world-sim>` instead
  of `CMAKE_CURRENT_BINARY_DIR`, so assets land next to the exe under multi-config generators.
- **.gitignore:** ignore `.claude/` local files (keep shared `commands/` tracked).
- **CMakePresets.json:** `default` (macOS/Linux) and `windows` (VS 2022) configure/build presets,
  host-conditioned, both wiring the vcpkg toolchain from `$env{VCPKG_ROOT}`. Unifies the build
  command (`cmake --preset <name>`) without changing the raw-cmake workflow.
- **CI:** added a `test-windows` (MSVC) job to `tests.yml` that builds with `BUILD_TESTING=ON` and
  runs ctest. The existing Linux build job uses `BUILD_TESTING=OFF`, so it would not have caught the
  test-file portability breaks; the Windows test job does.
- **Docs:** README cross-platform build steps + vcpkg-baseline note; filled in `docs/setup.md`.

## Technical decisions

- **Reorder call sites, not struct declarations.** The codebase writes designators in inconsistent
  orders (some `.margin` before `.id`, some after), so no single struct ordering satisfies all sites.
  Call sites must match declaration order; the struct is the fixed reference.
- **`-Werror=reorder-init-list` over an MSVC CI job (for now).** Promoting the existing Clang warning
  to an error makes the most common portability break fail on the machine developers actually use. An
  MSVC CI job is the stronger backstop and remains a follow-up.
- **Generate the atlas into the source `fonts/` dir.** Keeps the existing fonts-copy step unchanged
  and the atlas alongside its font; the PNG is gitignored, the JSON regenerates deterministically.

## Verification

Simulated a clean checkout (deleted the atlas PNG), full rebuild regenerated it, all 7 ctest suites
pass, both apps build, and `world-sim.exe` launches and renders. Changes are macOS/CI-safe: the
asset-path fix is a no-op on single-config generators, and the test fixes use standard C++20.

## Follow-ups

- `metrics/SystemResources.cpp` is macOS-only; on Windows it returns a zeroed snapshot. A Win32
  implementation (`GetProcessMemoryInfo`, `GetProcessTimes`) would restore real metrics. This is a
  feature, not a port fix (the game runs without it), so it's deliberately separate.
- The new `test-windows` CI job and the macOS `-Werror=reorder-init-list` path can only be confirmed
  green by an actual CI run / a macOS build. Scanned the three `#ifdef __APPLE__` files here: none use
  designated initializers, so the guard should be safe on macOS, but a real macOS build confirms it.
