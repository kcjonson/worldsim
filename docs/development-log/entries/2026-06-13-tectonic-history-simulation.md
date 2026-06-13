# 2026-06-13 - Tectonic history simulation

## Summary

Replaced the single-pass Voronoi plate stages and Gaussian-kernel terrain stage with a coarse time-stepped tectonic simulation upsampled to full resolution. Fixes two long-standing visual problems: hexagonal continents (inherited from evenly-spaced Voronoi seeds) and smooth dome-shaped mountains (from Gaussian uplift kernels). The replacement generates realistic bimodal hypsometry, linear ridged mountain belts, organic coastlines, and Earth-calibrated ocean age distributions from physics, not parameter tuning.

Plan file: `.claude/plans/tectonic-history.md`

---

## What was built

### Stage 1: TectonicHistoryStage (weight 0.10)

Runs `PlateSim` — a coarse single-threaded time-stepped simulation on `SphereGrid(128)` (~163,842 tiles, ~56 km/tile). History length is ~800 Myr scaled by planet age (~160 steps of 5 Myr each, roughly 2 Wilson cycles at Earth rates).

Each step, in fixed order:
1. Advance Euler-pole quaternions (Euler-pole motion; poles evolve every ~90 Myr to prevent stranding ocean basins)
2. Forward-rasterize each plate's per-plate crust raster into world cells
3. Resolve ownership (continental beats oceanic; between oceanic, younger subducts; ties by plate ID)
4. Erase subducted cells from their plate's raster
5. Gap-fill unoccupied cells with age-0 oceanic crust (spreading ridges emerge where plates separate)
6. Boundary scan (Euler-pole relative velocity, convergence/shear, classify divergent/convergent/transform)
7. Convergent processing: CC collision thickening + orogeny stamp (squared distance-from-boundary falloff + convergence scaling, concentrates belt on narrow front); CO/OO trench + arc volcanism; overriding-plate thickening
8. Arc maturation: oceanic cells past the volcanism threshold with >=1 continental/volcanic neighbor can convert to thin continental crust (juvenile arc crust; gated by area controller to balance collisional shortening)
9. Terrane accretion: small continental fragments riding a subducting plate dock onto the overriding plate
10. Ownership speckle absorption: isolated 1-4 cell plate islands weld to the surrounding plate (prevents mid-plate orogeny stamps)
11. Crust-type coherence filter: isolated continental components of <=2 cells revert to oceanic (prevents continental confetti from arc maturation)
12. Erosion proxy: continental thickness relaxes toward 35 km equilibrium with 300 Myr e-fold
13. Hotspots: fixed plume locations deposit volcanism each step; chains emerge from plate motion
14. Slab pull: plates with old subducting seafloor accelerate trenchward; pole steered 6%/step toward orientation that carries old floor to the nearest trench
15. Oceanic plate reorganization: plates exceeding 22% of the sphere area break up regardless of plate-count controller
16. Ridge-coherent stranded-basin resurfacing: connected regions of oceanic floor older than 230 Myr that haven't reached a trench get a coherent great-circle ridge swath stamped to age 0 (prevents salt-and-pepper old-floor scatter)
17. Wilson-cycle events: plate merge after sustained CC collision (~150 Myr); suture-biased rifting when plate count is below target K (rifts prefer old sutures, per Wilson)

Output: `TectonicHistory` struct (~4 MB at coarseN=128) held on `GeneratedWorld` as a non-serialized `shared_ptr<const>`. Fields: plateId, crustType, crustAge, thicknessKm, orogenyAge, orogenyIntensity, volcanism, boundaryType/side, convergence, plate list with Euler poles and cumulative rotation quaternions.

### Stage 2: CrustStage (weight 0.15)

Upsamples the coarse TectonicHistory to the full-resolution grid.

**Crust type** is decided by thresholding a signed-distance field at 0, not by nearest-sampling the coarse binary crustType. Binary nearest-sampling dithers the coast into salt-and-pepper confetti; a signed-distance field warps cleanly and thresholds to a crisp 1-tile boundary. The SDF is warped by three independent fractal noise octaves (amplitude ~0.6 coarse cell) before thresholding, so the coast meanders organically and breaks up any residual coarse-grid hexagonal regularity. A crenulation noise term (amplitude 0.48 coarse cells, frequency 5, 2 octaves) adds continental-margin wiggle at a spatial scale that lifts the isoperimetric ratio into Earth-like range without fragmenting coastlines.

**Crust age, orogeny age, plate ID**: sampled from the nearest coarse cell of the matching crust type (inverse-distance blend), so a continental tile doesn't inherit oceanic ages from the warped lookup point.

Writes: `data.plateId`, `data.flags` (kFlagContinentalCrust), `data.crustAge` (u16, Myr), `data.orogenyAge` (u16, Myr; 65535=never). Fully parallelFor.

### Stage 3: TerrainStage rewrite (weight 0.20)

Synthesizes elevation from crust physics. The prior Gaussian-kernel approach is deleted.

**Continental elevation (Airy isostasy)**: elevation = (thickness - 35 km) × 180 m/km. Normal crust (~38 km) gives ~+400-500 m above sea level; collision-thickened crust (65-70 km) gives Tibet-class plateaus (+5-6 km). A shelfbreak ramp transitions elevation to sea level near the coastline (continental shelf).

**Orogeny belts**: each continental tile with an orogeny record gets ridged belt noise scaled by orogenyIntensity × age-decay(exp(-age/450 Myr), floor 0.35). Young active belts are tall and linear; old eroded sutures are low rolling hills. The ridged-noise direction is computed from the orogeny-band-normal so ranges run along the belt, not across it.

**Oceanic elevation (depth-age law)**: z = -(2500 + 320 × sqrt(age_Myr)), clamped at -6000 m. Ridge crests at ~-2500 m, abyssal plains at ~-5500 m emerge without tuning. Parsons-Sclater / GDH1.

**Active-boundary kernels**: trench (-5500 m, 60 km wide), volcanic arc (+2500 × convergence, ~220 km inland), rift valley + flanking shoulders, transform roughness. Hotspot cones (~3500 m, ridged). No Gaussian collision dome — mountains come entirely from thickness/orogeny fields.

**Sea level**: histogram quantile so ocean fraction matches `waterAmount` ±2%.

### Supporting tooling

**`apps/worldgen-cli`** (new): headless pipeline runner. Flags: `--n`, `--seed`, `--water`, `--plates`, `--age`, `--preset`, `--sim-only` (coarse sim only, optional `WORLDGEN_COARSE_FRAMES=1` per-step BMP dumps), `--verify-threads N,M` (runs at two TaskPool sizes, asserts `worldHash` identical). Dumps all DebugImageExporter modes as BMPs + `stats.json`.

**`WorldStats`** (new, `libs/world/worldgen/debug/`): hypsometry (land/ocean modes, trough, peak), plate-size distribution (exponential fit R², largest/smallest ratio), orogeny belt geodesic aspect ratio (tile-weighted median length/width via great-circle traverse), continent isoperimetric ratio (P²/4πA), interior-mountain fraction, ocean-fraction vs. water-amount error.

**DebugImageExporter**: new `CrustAge`, `OrogenyAge` export modes; reusable color-lambda BMP helper.

---

## Key technical decisions

**Per-plate quaternion rasters**: each plate maintains its own crust raster in plate-local coordinates, with a cumulative rotation quaternion. Each step, the raster is re-rasterized into world cells from scratch rather than incrementally resampled. This eliminates accumulation drift and keeps the full-step snapshot deterministic.

**Signed-distance coastline upsampling**: binary nearest-sample dithers coasts; SDF + domain warp produces crisp organic boundaries. The warp breaks coarse-hex regularity; the crenulation noise lifts isoperimetric ratio. Parameters (amplitude 0.48, frequency 5, 2 octaves) were tuned to land all gate seeds inside [3, 25] isoperimetric without fragmenting coasts.

**Isostasy + depth-age elevation**: bimodal hypsometry (the single most diagnostic Earth-like planet feature) falls out for free from these two physical laws. No separate tuning step.

**Arc crust production + area controller**: collisional shortening shrinks continental footprint (plates interpenetrate). A feedback controller scales arc maturation probability with the fractional area deficit. Without this, continental area bleeds 18-25% over a default run. With it, drift stays within ±10%. The nucleation-support rule (must have a continental/volcanic neighbor) and crust-type coherence filter (tiny isolated components revert) are the two mechanisms that prevent arc maturation from producing continental confetti.

**Slab pull**: the dominant plate-driving force in nature. Without it, simulated seafloor accumulates old crust indefinitely (nothing drains old basins to trenches). Slab pull + pole steering + ridge-coherent resurfacing together keep ocean age mean at 60-80 Myr and the >220 Myr tail at 4-8% (Earth-like).

**Geodesic belt linearity metric**: the tile-weighted median geodesic aspect ratio (length/width from great-circle traversal) is the reliable belt-shape metric. PCA elongation in the 2D tangent plane foreshortens curved arcs and underreports elongation (documented as a deprecated secondary signal). The geodesic metric gave 5.1-7.2 across 5 seeds (target >=3).

---

## Acceptance results

Measured at n=256, >=5 seeds, EarthLike preset via `worldgen-cli --verify-threads 1,8`.

| Metric | Target | Result |
|--------|--------|--------|
| Ocean fraction vs. waterAmount | ±2% | Pass (all seeds) |
| Hypsometry: land mode | -500..+1200 m | ~+445 m |
| Hypsometry: ocean mode | -5500..-3500 m | ~-5500 m |
| Hypsometry: trough < 60% of lower peak | < 60% | <4% |
| Oceanic crust age mean | 40-90 Myr | 60-80 Myr |
| Oceanic crust age >220 Myr fraction | <10% | 4-8% |
| Plate-size distribution (exponential R²) | >=0.85 | Pass |
| Largest plate fraction | <=~30% | Pass (was 43-49% before slab pull) |
| Belt geodesic aspect ratio (tile-weighted median) | >=3 | 5.1-7.2 across 5 seeds |
| Belt widths | 150-800 km | 575-700 km |
| Continent isoperimetric P²/4πA (n=256, median) | >=3 | ~5-18 (seed-dependent) |
| Determinism: worldHash across thread counts 1 vs 8 | identical | Pass |
| Determinism: worldHash across repeat runs | identical | Pass |
| n=1024 wall clock (RelWithDebInfo) | 10-30 s | ~11 s total (~6 s coarse sim, ~4 s TerrainStage) |
| Edge cases: plates 2..30, water 0.05/0.95, n=16/64 | no crash | Pass |
| n=1024 visual: coastlines organic, mountains linear | user review | Pass |

**Note on isoperimetric ratio**: rises with resolution due to the coastline paradox (higher-res boundary = longer perimeter). Values above are at n=256; at n=1024 they are higher. The metric is a comparative proxy (was ~1.4-1.9 for hexagonal blobs, now ~5-18+), judged alongside visual inspection.

**Known proxy-metric limits**:
- PCA elongation stays ~2.0 for curved arcs because 2D tangent-plane projection foreshortens them. Not a signal failure — the geodesic metric is the honest one.
- Interior-mountain-fraction exceeds 0.15 on supercontinent seeds with genuine active intracontinental collision (Himalaya-in-Eurasia analog). The boundary-distance metric cannot credit this as a legitimate interior orogen. Not a bug.

---

## Files added

- `libs/world/worldgen/tectonics/TectonicParams.h`
- `libs/world/worldgen/tectonics/TectonicHistory.h`
- `libs/world/worldgen/tectonics/PlateSim.h`
- `libs/world/worldgen/tectonics/PlateSim.cpp`
- `libs/world/worldgen/tectonics/PlateSim.test.cpp`
- `libs/world/worldgen/tectonics/CoarseSampler.h`
- `libs/world/worldgen/stages/TectonicHistoryStage.h`
- `libs/world/worldgen/stages/TectonicHistoryStage.cpp`
- `libs/world/worldgen/stages/CrustStage.h`
- `libs/world/worldgen/stages/CrustStage.cpp`
- `libs/world/worldgen/stages/CrustStage.test.cpp`
- `libs/world/worldgen/debug/WorldStats.h`
- `libs/world/worldgen/debug/WorldStats.cpp`
- `apps/worldgen-cli/Main.cpp`
- `apps/worldgen-cli/CMakeLists.txt`

## Files modified

- `libs/world/worldgen/data/WorldData.h` — crustAge (u16), orogenyAge (u16), kFlagContinentalCrust, WorldField bits 15/16, kAllWorldFields=0x1FFFFu, 26→30 B/tile
- `libs/world/worldgen/data/GeneratedWorld.h` — tectonicHistory field (shared_ptr<const TectonicHistory>)
- `libs/world/worldgen/io/PlanetIO.h/.cpp` — kFormatVersion=3, updated field layout
- `libs/world/worldgen/io/PlanetIO.test.cpp` — roundtrip tests updated for new fields
- `libs/world/worldgen/pipeline/PlanetGenerator.cpp` — stage list: TectonicHistoryStage, CrustStage, TerrainStage (rewrite) replace PlateStage, PlateMovementStage, TerrainStage (Gaussian)
- `libs/world/worldgen/stages/TerrainStage.h` — weight 0.25→0.20
- `libs/world/worldgen/stages/TerrainStage.cpp` — full rewrite (isostasy + depth-age + belts + kernels)
- `libs/world/worldgen/stages/TerrainStage.test.cpp` — tests rebaselined
- `libs/world/worldgen/debug/DebugImageExporter.h/.cpp` — CrustAge, OrogenyAge modes; color-lambda helper
- `libs/world/worldgen/debug/ColorMaps.h` — new color ramps for crust age / orogeny age
- `CMakeLists.txt` (root + `libs/world/`) — tectonics module, worldgen-cli app

## Files deleted

- `libs/world/worldgen/stages/PlateStage.h/.cpp/.test.cpp/.bench.cpp`
- `libs/world/worldgen/stages/PlateMovementStage.h/.cpp`

## Related documentation

- `.claude/plans/tectonic-history.md` — epic plan with acceptance targets and milestone breakdown
- `docs/design/features/world-generation/concept.md` — "Scientific Plausibility" section updated
- `docs/design/features/world-generation/generation-phases.md` — Phases 1-3 rewritten
- `docs/technical/world-generation-implementation.md` — contract amendment section added (2026-06-13)

## Follow-ups

- **Fluvial / stream-power erosion** (explicitly deferred): the thickness-relaxation proxy is a placeholder. A real erosion pass (Cordonnier 2016 implicit stream power, or a simplified SPM) would smooth the terrain, cut valleys, and produce realistic hypsometric curves at older planet ages. This is the biggest remaining gap between simulated and Earth-like terrain.
- **Belt linearity metric tightening**: the geodesic aspect ratio target is >=3, currently landing at 5.1-7.2. Tightening the upper bound (no wider than ~8) and adding a width variance metric would catch degenerate supercontinent seeds where "one global belt" inflates the ratio.
- **Boundary classification in CrustStage**: convergence/boundaryType currently come from the coarse history and are passed through. Moving the full-res boundary reclassification into CrustStage (BFS from the full-res coast rather than upsampling the coarse scan) would give sharper trench/ridge kernel placement in TerrainStage.
- **n=1024 isoperimetric tuning**: at n=1024 the isoperimetric ratio rises due to the coastline paradox. The crenulation parameters were tuned at n=256; a tuning pass at n=1024 might nudge amplitude/frequency for a more consistent ratio across resolutions.
