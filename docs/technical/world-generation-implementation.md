# World Generation Implementation

Created: 2026-06-09
Last Updated: 2026-06-09
Status: Active (M2 complete, stub stages only; full stages in M3)

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

20 icosahedron faces are paired into 10 rhombi. Each rhombus is an n×n quad
grid projected to the unit sphere. Total tiles = 10·n·n.

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

Each rhombus is subdivided into two triangles by the diagonal AC:
- T1 (u+v ≤ 1): corners A, B, D
- T2 (u+v > 1): corners B, C, D

### TileId encoding

`TileId = rhombus * n*n + j*n + i`

where `i` is the u-axis index (0 = A side, n-1 = B/D side) and `j` is the
v-axis index (0 = A side, n-1 = D/C side).

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

### Neighbor lookup

`neighbors(t, out)` generates all 8 Moore neighbors (4 edge + 4 diagonal) via
`canonicalize()`, which follows the edge adjacency table to hop across rhombus
boundaries. Interior tiles return 8 neighbors; boundary tiles 5–6; the 12
icosahedron vertex tiles return 5 (one from each adjacent rhombus, deduplicated).

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
| starTemperatureK | double | Kelvin |
| starLuminosity | double | Solar luminosities |
| orbitRadiusAU | double | Astronomical units |
| planetMass | double | Earth masses |
| planetRadius | double | Earth radii |
| rotationPeriodHours | double | Hours |
| axialTiltDeg | double | Degrees |
| atmosphericPressure | double | Earth atmospheres |
| waterAmount | double | 0..1, fraction of surface covered |
| tectonicPlateCount | int | 1..20 |
| seed | uint64_t | |
| gridSubdivision | uint32_t | n; tiles = 10·n·n |

### 6 presets

EarthLike, DesertWorld, OceanWorld, FrozenWorld, VolcanicWorld, AncientGarden.

### DerivedPlanetValues

Computed from PlanetParams by `derive()`:

- `planetRadiusMeters` = radius × 6.371e6
- `gravity` = G·M / r² (m/s²)
- `solarConstant` = L_star / (4π d²) in W/m² (L_star from Stefan-Boltzmann)
- `equilibriumTemperatureK` = (S·(1-0.3) / (4σ))^0.25
- `rotationPeriodSeconds` = rotationPeriodHours × 3600
- `lapseRateCPerKm` = 6.5 (fixed)

All transcendental math uses `foundation::det_math::*`. No `std::sin`, `cos`,
`exp`, `pow`, etc. in any worldgen code (`std::sqrt` is allowed and is used
only in `SphereGrid.cpp` for the one normalize operation before the
deterministic functions are ready).

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
| downhill | uint8 | 1 | neighbor index 0..7; 0xFF = sink |
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
| OceanStage | 0.05 | WaterDepth, Flags (kFlagOcean) |
| BiomeStage | 0.15 | Biome |
| SnowStage | 0.05 | SnowCover, Flags (kFlagPermanentSnow) |

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

## Frozen contracts (M2 → M3 interface)

These are the fixed interfaces that M3 sub-stages must respect:

- **TileId encoding**: `rhombus * n*n + j*n + i`. Never changes.
- **WorldData field units**: as tabulated above. M3 writes within the same types/ranges.
- **WorldField bits 0..14**: fixed allocation per field; do not reuse bits.
- **kAllWorldFields = 0x7FFFu**: M3 does not add new fields without expanding this.
- **worldHash**: FNV-1a in fixed field order, same as `computeFieldChecksums`. M3 must produce the same hash given the same seed.
- **Snapshot immutability**: once a field bit is set in validFields, that array is read-only forever. M3 stages must not write fields already set by earlier stages.
- **SphereGrid API**: `fromUnitVector`, `tileCenter`, `latLonOf`, `neighbors`, `locate`, `tileWidthMeters` — signatures frozen.
- **PlanetParams fields**: all fields used by stubs are the same fields M3 uses; adding a new field requires updating `preset()` and `derive()`.
- **8 stage pipeline order**: Plates → PlateMovement → Terrain → Atmosphere → Precipitation → Ocean → Biome → Snow. M3 replaces each stub in-place; the order and count do not change.
