# World Generation Implementation

Created: 2026-06-09
Last Updated: 2026-06-12
Status: Active (M2 complete, full stages in M3; Goldberg hex conversion in feature/worldgen-hex-tiles)

Implemented by M2. For the higher-level pluggable architecture see
[world-generation-architecture.md](./world-generation-architecture.md).

---

## What M2 delivers

Seven concrete deliverables, all under `libs/world/worldgen/`, namespace `worldgen`:

1. `SphereGrid` — icosahedral rhombus grid
2. `Biome` enum (21 values) + helpers
3. `PlanetParams` + 6 presets + `DerivedPlanetValues`
4. `WorldData` (SoA) + `GeneratedWorld`
5. Pipeline — `IGenerationStage`, `PlanetGenerator`, 8 stub stages
6. `DebugImageExporter` — equirectangular BMP, no new deps
7. This doc

Tests live in `*.test.cpp` files discovered by CMake glob. Benchmarks in `*.bench.cpp`.

---

## SphereGrid

**Header:** `worldgen/grid/SphereGrid.h`

### Grid structure

20 icosahedron faces are paired into 10 rhombi (true Goldberg polyhedron layout).
Tile centers sit at chart vertices (i/n, j/n), not cell centers. Because chart
vertices are shared exactly across rhombus seams, the hex lattice closes cleanly
across all edges with no special-casing. Total tiles = 10·n·n + 2 (the +2 are the
north and south pole tiles).

5 northern rhombi (r = 0..4):

- A = north pole (vertex 0)
- B = upper ring vertex r (vertex 1+r)
- C = lower ring vertex r (vertex 6+r)
- D = upper ring vertex (r+1)%5 (vertex 1+(r+1)%5)

5 southern rhombi (5+r, r = 0..4):

- A = upper ring vertex (r+1)%5
- B = lower ring vertex r
- C = south pole (vertex 11)
- D = lower ring vertex (r+1)%5

Rhombus corners in (u,v): A=(0,0), B=(1,0), C=(1,1), D=(0,1). The two triangles
share the fold/short diagonal B-D:
- T1 (u+v ≤ 1): corners A, B, D
- T2 (u+v > 1): corners B, C, D

### TileId encoding

**(Contract amendment, 2026-06-12)** The original M2 encoding `r*n^2 + j*n + i` used
cell-centered tiles at (i+0.5)/n and counted 10n^2 tiles. The Goldberg vertex
conversion changed the layout to vertex-centered tiles at i/n, added the two pole
tiles, and adjusted ownership to avoid double-counting shared edges and corners:

```
owned vertex: rhombus * n*n + j*n + (i-1)
  rhombus in [0..9], i in [1..n], j in [0..n-1]

north pole:  10*n*n       (kNorthPole)
south pole:  10*n*n + 1   (kSouthPole)
```

`i` is the u-axis index (A→B direction), `j` is the v-axis index (A→D direction).
Each rhombus owns n² vertices (i in [1..n], j in [0..n-1]); the u=0 seam (i=0),
the v=n seam (j=n), and corner C=(n,n) are all owned by a neighboring rhombus and
canonicalize there. The poles sit outside every owned range and carry the special ids.

This was a deliberate breaking change (old-format files are rejected at load time;
see PlanetIO section). Downstream fields that index into the neighbor list (e.g.,
`downhill`) changed range from 0..7 to 0..5 at the same time.

### Forward mapping (uvToDir)

Given rhombus r and (u, v) ∈ [0,1]²:

- T1 (u+v ≤ 1): `p = (1-u-v)·A + u·B + v·D`, then normalize
- T2 (u+v > 1): `p = (1-v)·B + (u+v-1)·C + (1-u)·D`, then normalize

### Inverse mapping (dirToRhombusUV)

Precomputed per-rhombus: two 3×3 inverse matrices (T1: columns A, B, D;
T2: columns B, C, D). For a given direction `d`:

1. Sort 10 rhombi by dot product with their center
2. For each rhombus (best-first), try T1 solve: `(bA, bB, bD) = inv_T1 · d`. If all ≥ -ε → hit; u = bB, v = bD.
3. Try T2 solve: `(bB, bC, bD) = inv_T2 · d`. If all ≥ -ε → hit; u = bB + bC, v = bC + bD.
4. Fallback to best center match if all 10 rhombi reject (numerically degenerate input).

This replaces the earlier face-lookup approach that had winding-order bugs for
southern faces.

### Edge adjacency

Built by matching shared vertex pairs across rhombus edges:
- Edge 0 (i=0): shares vertices A, D
- Edge 1 (i=n-1): shares vertices B, C
- Edge 2 (j=0): shares vertices A, B
- Edge 3 (j=n-1): shares vertices D, C

The `reversed` flag in `EdgeAdj` captures whether the shared edge runs in the
opposite direction, needed to correctly map the coordinate along that edge.

### Assignment

`fromUnitVector` and `fromLatLon` cube-round the fractional axial coordinate
(u·n, v·n) via `hexRound` — the same `floor(x+0.5)` tie-break that `planet.frag`
mirrors exactly, keeping GPU rendering consistent with CPU picking. The result is
then passed through `canonicalVertex` to resolve ownership across seams.

### Neighbor lookup

`neighbors(t, out)` returns the 6 hex neighbors in the **fixed offset order**
`(+1,0) (-1,0) (0,+1) (0,-1) (+1,-1) (-1,+1)`, canonicalized across rhombus
edges, deduplicated, and `kInvalidTile`-free. The order is load-bearing:
`WorldData::downhill` stores an index 0..5 into this list.

Interior tiles return 6 neighbors. The 12 icosahedron-vertex tiles (including both
poles) are pentagons with exactly 5 neighbors; no other tile count exists.

### EdgeXform seam maps

For each of the 40 (rhombus, edge) pairs, `edgeXform` stores an integer affine
transform `(i', j') = M*(i,j) + t` that maps a vertex in the current chart to
the identical physical vertex in the neighbor chart across that edge. The 2×2
block M is a triangular-lattice automorphism (60° fold); the translation t scales
with n. Used by `canonicalVertex` and `canonicalTile` to resolve out-of-range and
unowned vertices without any floating-point geometry.

### rhombusPointOnSphere (added M3f)

`rhombusPointOnSphere(rhombus, u, v)` is a thin public wrapper around the
internal `uvToDir()` forward mapping. Added for `planet-view` so mesh vertex
positions use the same icosahedral barycentric math as tile placement — neighboring
rhombi share exact edge vertices, eliminating mesh seam artifacts.

Contract: `rhombusPointOnSphere(r, i/n, j/n) == tileCenter(r*n*n + j*n + (i-1))`
for owned vertices (i in [1..n], j in [0..n-1]).

Tested by `SphereGrid.RhombusPointMatchesTileCenter`.

### Performance

At n=1024 on a 32-core 3.4 GHz machine (Release):
- `fromUnitVector`: ~59 ns
- `tileCenter`: ~14 ns
- `neighbors`: ~37 ns
- Pipeline n=256 (8 stub stages, 655,360 tiles): ~46 ms

---

## Biome

**Header:** `worldgen/data/Biome.h`

21 values (0–20) as `enum class Biome : uint8_t`. Helpers: `biomeToString`,
`isWater`, `isForest`. The enum value is used as a direct index into
`kBiomeColors` in `DebugImageExporter`.

---

## PlanetParams and DerivedPlanetValues

**Headers:** `worldgen/data/PlanetParams.h`, `worldgen/data/PlanetParams.cpp`

### PlanetParams fields

| Field | Type | Description |
|---|---|---|
| starMass | double | Solar masses |
| starRadius | double | Solar radii |
| starTemperature | double | Kelvin |
| starAge | double | Years |
| planetRadius | double | Earth radii |
| planetMass | double | Earth masses |
| rotationRate | double | Earth days per rotation |
| tectonicPlateCount | int | 2..30 |
| waterAmount | double | 0..1, fraction of surface covered |
| atmosphereStrength | double | Earth atmospheres |
| planetAge | double | Years |
| semiMajorAxis | double | AU |
| eccentricity | double | |
| seed | uint64_t | |
| gridSubdivision | uint32_t | n; tiles = 10·n·n + 2 |

### 6 presets

EarthLike, DesertWorld, OceanWorld, FrozenWorld, VolcanicWorld, AncientGarden.

### DerivedPlanetValues

Computed from PlanetParams by `derive()`:

- `planetRadiusMeters` = radius × 6.371e6
- `gravity` = G·M / r² (m/s²)
- `solarConstant` = L_star / (4π d²) in W/m² (L_star from Stefan-Boltzmann)
- `equilibriumTemperatureK` = (S·(1-0.3) / (4σ))^0.25
- `rotationPeriodSeconds` = rotationRate × 86400 (rotationRate is in Earth days)
- `lapseRateCPerKm` = 6.5 (fixed)

All transcendental math uses `foundation::det_math::*`. No `std::sin`, `cos`,
`tan`, `exp`, or `pow` anywhere in worldgen code. `std::sqrt` is allowed because
IEEE-754 requires sqrt to be correctly rounded (same result on all conforming
platforms); it appears in multiple geometry helpers across the library.

---

## WorldData

**Header:** `worldgen/data/WorldData.h`

SoA (structure of arrays) layout. 26 bytes per tile total:

| Field | Type | Bytes/tile | Units |
|---|---|---|---|
| elevation | float | 4 | meters above mean radius |
| temperatureMean | int16 | 2 | 0.1 °C |
| temperatureRange | int16 | 2 | 0.1 °C (half-amplitude) |
| precipitation | uint16 | 2 | mm/yr |
| windDir | uint8 | 1 | 256 = 360° (0=N, 64=E) |
| windSpeed | uint8 | 1 | m/s |
| plateId | uint8 | 1 | 0..254; 255=unassigned |
| boundaryType | uint8 | 1 | 0=interior,1=divergent,2=convergent,3=transform |
| boundaryDistance | uint16 | 2 | tiles from nearest boundary |
| biome | uint8 | 1 | Biome enum |
| flags | uint8 | 1 | kFlag* bits |
| waterDepth | uint16 | 2 | meters (0 = land) |
| flowAccum | float | 4 | upstream tile count |
| downhill | uint8 | 1 | neighbor index 0..5 (SphereGrid::neighbors() order); 0xFF = sink |
| snowCover | uint8 | 1 | 0=bare, 255=full |

`WorldField` is a `uint32_t` bitmask; `kAllWorldFields = 0x7FFFu` (bits 0..14,
one per field above). Stages OR their bit into `world.validFields` on completion.

`flags` bits: `kFlagOcean=0x01`, `kFlagLake=0x02`, `kFlagRiver=0x04`,
`kFlagCoast=0x08`, `kFlagPermanentSnow=0x10`, `kFlagGlacier=0x20`.

---

## GeneratedWorld

**Header:** `worldgen/data/GeneratedWorld.h`

```cpp
struct GeneratedWorld {
    PlanetParams params;
    DerivedPlanetValues derived;
    shared_ptr<const SphereGrid> grid;
    WorldData data;
    float seaLevelMeters;
    vector<PlateInfo> plates;
    WorldSummary summary;
    uint32_t validFields;
    uint64_t worldHash;
};
```

`WorldSummary` has landFraction, biomeHistogram (21 bins), meanTemperatureC,
riverTileCount, habitability.

`worldHash` is computed after all stages via FNV-1a over all valid field arrays
in fixed order (see `PlanetGenerator::computeFieldChecksums`). It is the
canonical bit-reproducibility check for the whole planet.

---

## Pipeline

### IGenerationStage

**Header:** `worldgen/pipeline/GenerationStage.h`

```cpp
class IGenerationStage {
  public:
    virtual const char* name()   const = 0;
    virtual float       weight() const = 0;
    virtual void        run(StageContext& ctx) = 0;
};
```

`StageContext` provides: params, derived, grid (const), data (mutable), world
(mutable), pool (TaskPool ref), stageSeed, reportProgress callback, and a const
ref to the cancel flag.

### PlanetGenerator

**Header:** `worldgen/pipeline/PlanetGenerator.h`

Runs the 8 stages on a `std::jthread`. Progress is exposed via atomics (no lock
for reads). The latest snapshot is a mutex-guarded `shared_ptr<GeneratedWorld>`,
replaced after each stage.

**Snapshot immutability contract:** once a stage publishes its fields
(by setting `validFields` bits), those arrays are read-only for all subsequent
stages. Stages only write fields that aren't yet valid. This is enforced by
convention and verified by the `SnapshotImmutability` test.

**Cancellation:** any stage lambda calls `throwIfCancelled(ctx)` per slab.
Cancel propagates via `CancelledException` caught in the runner. P99 latency
from `cancel()` to state=Cancelled is bounded by the slab grain size
(kGrainSize=4096 tiles) times per-tile cost.

**Per-stage seeds:** `foundation::deriveSeed(params.seed, stageIndex)`.

### 8 stub stages

| Stage | Weight | Fields set |
|---|---|---|
| PlateStage | 0.10 | PlateId |
| PlateMovementStage | 0.05 | BoundaryType, BoundaryDistance |
| TerrainStage | 0.25 | Elevation |
| AtmosphereStage | 0.15 | TemperatureMean, TemperatureRange, WindDir, WindSpeed |
| PrecipitationStage | 0.20 | Precipitation |
| OceanStage | 0.05 | WaterDepth (writes kFlagOcean into flags but does not set Flags bit) |
| BiomeStage | 0.15 | Biome |
| SnowStage | 0.05 | SnowCover, Flags (writes kFlagPermanentSnow, then sets Flags bit as last flags writer), FlowAccum, Downhill (stub defaults from allocate()) |

Stub stages produce geographically plausible placeholder output using hash
noise and latitude-band formulas. They will be replaced by M3 sub-stages with
physically based algorithms.

---

## DebugImageExporter

**Header:** `worldgen/debug/DebugImageExporter.h`

`exportEquirectangularBmp(world, mode, path, width)` writes a 24-bit BMP
(width × width/2 pixels). Modes: Elevation, Temperature, Precipitation,
Biome, PlateId. No new library deps — the BMP header (54 bytes) is written
by hand (14-byte file header + 40-byte DIB header, BGR byte order,
rows bottom-to-top as BMP requires).

Color maps live in `worldgen/debug/ColorMaps.h`.

---

## Build

```
cmake --build build --config Debug   --target world-tests
cmake --build build --config Release --target world-benchmarks
```

CMakeLists: `libs/world/CMakeLists.txt`. The `world` library links only
`foundation` (no renderer, no nlohmann_json).

Test files: `*.test.cpp` (28 tests, all passing).
Benchmark files: `*.bench.cpp` (5 benchmarks).

---

## PlanetIO format

`libs/world/worldgen/io/PlanetIO.h` — binary format "WSPL", little-endian.

**Format version 2** (current). Version 1 is rejected at load time; the
`GameLoadingScene` auto-regenerates on any load failure, so old quickstart caches
silently rebuild. Bumped when the Goldberg conversion changed the TileId encoding
and the neighbor-derived `downhill` field range.

The format spec table in the header documents all field offsets and sizes.
`downhill` range is 0..5 (neighbor index into the 6-offset fixed order) or 0xFF
for a sink tile.

---

## Frozen contracts (M2 → M3 interface)

These are the fixed interfaces that M3 sub-stages must respect. Two deliberate
amendments were made during the Goldberg hex conversion (2026-06-12); both are
called out below.

- **TileId encoding** *(amended)*: `rhombus * n*n + j*n + (i-1)` for owned
  vertices; `10*n*n` / `10*n*n+1` for the poles. The original M2 encoding
  (`r*n^2 + j*n + i`, cell-centered, 10n^2 tiles) was replaced wholesale when
  vertex-centered Goldberg tiles were adopted. Old-format PlanetIO files are
  rejected; all consumers updated in the same PR.
- **WorldData field units**: as tabulated above. M3 writes within the same types/ranges.
- **WorldField bits 0..14**: fixed allocation per field; do not reuse bits.
- **kAllWorldFields = 0x7FFFu**: M3 does not add new fields without expanding this.
- **worldHash**: FNV-1a in fixed field order, same as `computeFieldChecksums`. M3 must produce the same hash given the same seed. Note: same seed yields a *different* world after the hex conversion (6-neighbor stages replace 8-neighbor stages).
- **Snapshot immutability**: once a field bit is set in validFields, that array is read-only forever. M3 stages must not write fields already set by earlier stages.
- **SphereGrid API** *(amended)*: `fromUnitVector`, `tileCenter`, `latLonOf`,
  `neighbors` (returns 6, fixed offset order), `locateHex`, `tileWidthMeters`,
  `canonicalTile` — signatures frozen. `locate()` was deleted and replaced by
  `locateHex` (returns `HexSample{tile, neighbor, edgeDistance}`); this was a
  deliberate One-Path deletion, not a deprecation.
- **PlanetParams fields**: all fields used by stubs are the same fields M3 uses; adding a new field requires updating `preset()` and `derive()`.
- **8 stage pipeline order**: Plates → PlateMovement → Terrain → Atmosphere → Precipitation → Ocean → Biome → Snow. M3 replaces each stub in-place; the order and count do not change.

---

## Contract amendment — tectonic history simulation (2026-06-13)

PR #136 (`feature/worldgen-tectonic-history`) replaced the first three stages and expanded `WorldData`. The frozen contracts from M2 and the Goldberg amendment still hold for stages 4-8; the amendments below cover what changed.

### Stage replacements

`PlateStage` (weight 0.10) and `PlateMovementStage` (weight 0.05) are **deleted**. `TerrainStage` is rewritten (weight changes from 0.25 to 0.20). Two new stages take the first two slots:

| Slot | Old | New | Weight |
|------|-----|-----|--------|
| 0 | `PlateStage` | `TectonicHistoryStage` | 0.05 |
| 1 | `PlateMovementStage` | `CrustStage` | 0.15 |
| 2 | `TerrainStage` (Gaussian kernels) | `TerrainStage` (isostasy + depth-age) | 0.20 |

`TectonicHistoryStage` runs `PlateSim` on a `SphereGrid(128)` (~163,842 tiles, ~56 km/tile) for ~160 steps of 5 Myr each. Output is a `TectonicHistory` held on `GeneratedWorld` as a non-serialized `shared_ptr<const TectonicHistory>`. `CrustStage` upsamples the coarse history to the full-res grid and writes `WorldData` fields. `TerrainStage` synthesizes elevation from crust physics rather than Gaussian kernels.

All constants for the tectonic simulation live in `libs/world/worldgen/tectonics/TectonicParams.h`.

### WorldData: 26 → 30 bytes/tile

Two u16 fields added:

| Field | Type | Bytes/tile | Units |
|---|---|---|---|
| crustAge | uint16 | 2 | Myr; range 0..65534, cap 65534 (no unset sentinel; initialized to 0) |
| orogenyAge | uint16 | 2 | Myr since last orogeny; 65535 = never |

`WorldField` bits 15 and 16 are assigned to these fields. `kAllWorldFields = 0x1FFFFu` (bits 0..16). Both fields are appended to `forEachFieldArray`, which means they participate automatically in `worldHash` and `PlanetIO` serialization.

`flags` bit `kFlagContinentalCrust = 0x40` is now written by `CrustStage` (the bit existed before this PR; only the writer changed).

### PlanetIO format version 3

`kFormatVersion = 3`. Version 2 (Goldberg, 26 B/tile) is rejected at load time with the existing `IOErrorCode::FormatVersionMismatch` path. `GameLoadingScene` auto-regenerates on any load failure, so old quickstart caches silently rebuild. Existing v1 files are also rejected (unchanged behavior).

The on-disk layout is the same SoA format as v2, extended by the two new field arrays at the end of the tile data block.

### New module: `libs/world/worldgen/tectonics/`

Contains:
- `TectonicParams.h` — all simulation constants, Earth-anchored with citations
- `TectonicHistory.h` — `TectonicHistory` struct (coarse per-tile arrays + plate list), `TectonicPlate`, `CrustType`, and `computeTectonicHistoryHash`
- `PlateSim.h/.cpp` — the simulation engine; single-threaded for determinism
- `PlateSim.test.cpp` — unit tests including the golden determinism hash
- `CoarseSampler.h` — domain-warp lookup, signed-distance field builder, smooth sampling helpers; shared by `CrustStage` and `TerrainStage`

### New app: `apps/worldgen-cli`

Headless pipeline runner. Accepts `--n`, `--seed`, `--water`, `--plates`, `--age`, `--preset`, `--sim-only` (coarse sim only, optional per-step frame dumps), `--verify-threads N,M` (runs pipeline at two thread counts and asserts `worldHash` is identical). Dumps all `DebugImageExporter` modes as BMPs plus a `stats.json` containing `WorldStats` output.

### New: `libs/world/worldgen/debug/WorldStats`

Computes and reports: hypsometry (land/ocean elevation modes, trough depth, peak heights), plate-size distribution (exponential fit R², largest/smallest ratio), orogeny belt geodesic aspect ratio (tile-weighted median length/width), continent isoperimetric ratio (P²/4πA), interior-mountain fraction, and ocean-fraction vs. water-amount error.

### DebugImageExporter additions

New export modes: `CrustAge`, `OrogenyAge`. The exporter also gained a reusable color-lambda BMP helper so adding new single-field modes no longer requires duplicating the BMP write loop. Optional env-gated per-step coarse frame dumps (set `WORLDGEN_COARSE_FRAMES=1`) for history-animation debugging.

### Same-seed worlds change

Worlds generated with the same seed produce a different result than before this amendment. This is expected and intentional, analogous to the Goldberg hex conversion (2026-06-12). The world hash will differ; any cached quickstart planets in PlanetIO format will be auto-regenerated.

### Determinism contract (unchanged, stricter)

`worldHash` is still FNV-1a in fixed field order. The coarse sim is single-threaded so its output is bit-identical across TaskPool sizes. `--verify-threads 1,8` passes. A separate golden product-hash test in `PlateSim.test.cpp` pins the coarse TectonicHistory to a known value at a fixed seed.
