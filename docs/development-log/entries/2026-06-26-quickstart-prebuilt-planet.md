# Quickstart planet: prebuilt, shipped, max-resolution

## Summary

The Quick Start planet was generated on the player's first run at n=256 and
cached. It's now a prebuilt n=2048 (the resolution cap) planet baked at build
time and shipped with the game; no player ever generates it. The main menu
disables Quick Start when the file is absent, and the loading scene is load-only
with no in-game generation fallback.

## Details

### Why

Quick Start generation only happens once and the same world ships to everyone,
so the player-machine gen-time budget that caps the runtime default (n=1024)
doesn't apply. We generate it at author/build time and bake the highest
resolution we can hold in RAM (n=2048, ~42M tiles, ~1.3 GB). Higher n also tightens
the 2D landing fidelity (the n=256 quickstart dropped the player ~50km from
water).

### Build-time bake

- `worldgen-cli --save-planet <path>` generates and writes a `.wsplanet`, then
  exits — skips the BMP/stats export and doesn't require `--out`.
- New `quickstart-planet` CMake target (top-level): generates
  `planets/quickstart.wsplanet` (n=2048, seed 424242) via worldgen-cli if absent,
  then stages it next to the world-sim exe at `assets/planets/`. It is
  deliberately NOT part of ALL — generating n=2048 takes minutes, so routine
  builds don't pay it. CI/packaging and devs run it once:
  `cmake --build build --config RelWithDebInfo --target quickstart-planet`. Seed
  and n live in the CMake command (single source of truth). The file is
  gitignored (`*.wsplanet`, `/planets/`); it ships in the package, not the repo.

### Runtime

- `GameLoadingScene` is load-only: resolves `assets/planets/quickstart.wsplanet`
  via `findResource`, loads it, adopts it. No `PlanetGenerator` in the loading
  scene anymore — the generation path, cancel handling, and member were deleted
  (One Path Rule). A missing/corrupt file logs and goes to `ConfigError`.
- `MainMenuScene`: `MenuItem` gained an `enabled` flag. Quick Start is disabled
  (faint, non-clickable, with an explanatory hint) when the planet file isn't
  found, so a missing bake can't drop the player into a dead loading screen.
- Shared `kQuickstartPlanetResource` constant in `GameStartConfig.h` is the one
  path both the menu (existence check) and the loader use.

## Verification

- world-sim + worldgen-cli compile (Debug).
- `quickstart-planet` target generated the planet in 89.3s (worldHash
  0xf379e1969a9dbd07, 1,384,120,924 bytes) and staged it next to the
  RelWithDebInfo exe.
- Launched that build into Quick Start: planet loaded, landed beside a river
  (the high-res fidelity win), chunks + colonist placed. Game ran, then exited
  cleanly.
- Launched the Debug build (no staged planet): Quick Start is rendered faint and
  is non-clickable, while New Game / Settings / Exit are normal.

## Related Documentation

- `2026-06-26-worldgen-m7-hardening.md` (same session: validation, determinism
  gate, default-resolution sweep).
- Deferred: planet-DB streaming (n>=4096) would let the shipped planet exceed the
  current whole-RAM-residency cap.
