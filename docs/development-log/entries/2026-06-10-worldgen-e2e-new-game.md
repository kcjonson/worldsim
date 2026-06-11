# World Gen M4-M6: end-to-end New Game + Quick Start

**Date:** 2026-06-10

## Summary

The full flow now works and was verified in the running game: Main Menu -> New
Game -> World Creator (generate, 3D globe review) -> Choose Landing Site (click
a land tile on the globe) -> Confirm -> 2D gameplay world sampled from the
generated planet at the landing site. A separate Quick Start button bypasses
world creation entirely by loading a cached pre-generated planet, generating
and caching it on first run.

## What was built

**M4 sampling core** (`libs/world/worldgen/sampling/`, `libs/engine/world/chunk/`)
- `SphericalProjection`: 2D world meters (origin = landing site) <-> lat/lon,
  equirectangular local mapping with cos(lat) longitude scaling, wrap at
  +/-180, pole clamping. Deterministic math only.
- `PlanetSampler`: samples biome weights + elevation from a GeneratedWorld at
  arbitrary 2D positions. Elevations convert to meters-above-sea-level; water
  tiles clamp to <= 0. Edge blending within 500 m of tile boundaries.
- `GeneratedWorldSampler`: engine-facing IWorldSampler adapter; replaces
  MockWorldSampler in GameLoadingScene (Mock remains for tests only).
- `findDefaultLandingSite`: deterministic tiers — temperate coast (land,
  |lat| <= 45, water neighbor), temperate inland, any land, (0,0). Coast is
  computed from grid neighbors because no stage sets kFlagCoast yet.

**Planet save/load** (`libs/world/worldgen/io/PlanetIO.h/.cpp`)
- Versioned little-endian binary format ("WSPL" v1): params field-by-field,
  summary, plates, then per-field SoA arrays for validFields bits only.
  Atomic write (temp + rename). Load rebuilds SphereGrid from subdivision,
  recomputes derived values via derive(), and rejects on magic/version/size/
  hash mismatch (FNV-1a recomputed over loaded arrays).
- Fixed-size records mean any tile in any field is offset-addressable — this
  is the designated path to an mmap/streamed "planet database" for n>=4096
  planets (future task in status.md) instead of whole-planet RAM residency.

**App flow** (`apps/world-sim/`)
- `GameStartConfig`: SetPending/Take handoff (source, world, landing lat/lon).
- `GlobeView` (`scenes/shared/`): reusable globe widget (mesh/colorizer/
  renderer/camera, render-into-rect, orbit/zoom/pick) shared by WorldCreator
  and LandingSite scenes.
- `LandingSiteScene`: globe + click-to-pick (water tiles rejected with a
  hint), site info line (lat/lon, biome, elevation), Back/Confirm. Confirm
  persists the planet to `planets/planet-<seed>.wsplanet` (best-effort) and
  hands off to GameLoading.
- `GameLoadingScene`: new PreparingPlanet phase. Creator flow brings its own
  world; Quick Start (or direct scene jump) loads
  `planets/quickstart.wsplanet`, generating (EarthLike, n=256, fixed seed)
  with progress and caching it on first run. MockWorldSampler path deleted.
- MainMenu: New Game -> WorldCreator; new Quick Start button.

**WorldCreator/UI fixes (same branch, earlier commits)**
- Globe renders in the creator during generation (progressive snapshot
  colorization) and review; blank seed randomizes per run; invalid seed
  disables Generate with inline error; generation failure surfaces a message;
  review stats gated on validFields; Select gained a disabled state; slider
  value column no longer overlaps the track; dropdowns paint above later
  widgets.

## Technical decisions

- GameLoading always lands on a generated planet (one path); the quickstart
  cache makes that fast for dev flows like `--scene=game`.
- Colorizer texture size clamped to 1024 to avoid UI-thread hitches at High
  (1449) grid resolution.
- Landing-site coast detection from neighbors rather than kFlagCoast (not yet
  produced by any stage); revisit when OceanStage sets it.
- Input injection routes through the debug server like all dev helpers:
  `/api/input?ev=click,x,y` queues synthetic events (move/down/up/click/
  scroll, logical UI coordinates) that the main loop dispatches through the
  same SceneManager path as real mouse input. The endpoint accepts multiple
  `ev` params per request, so sequences batch into one round trip; a
  streaming (WebSocket) channel was considered and deferred until something
  needs server push — HTTP keep-alive plus batching covers current usage,
  and curl-ability is worth keeping.

## Verification

- world-tests: 81 tests pass (projection, sampler, landing site, PlanetIO
  roundtrip/corruption).
- Quick Start: cache miss generated + cached planet (~1 s at n=256), second
  run loaded from cache; landed at lat 38.2 temperate coast; 9 chunks; colonist
  spawned.
- New Game: clicked through menu -> creator -> generate -> landing site ->
  confirm; planet saved; chunks sampled at the chosen site; gameplay running.

## Known gaps / next steps

- Climate stages are stubs (M3c-M3e): planets average -25C so almost all land
  samples as ArcticTundra. The flow is correct; the data is cold.
- Landing scene lacks the UX spec's local preview + difficulty rating.
- Antimeridian chunk wrap + polar scaling deferred (per M4 spec).
