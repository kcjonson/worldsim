# Goldberg hex grid + two-tier planet rendering

## Summary

Converted the world-generation grid from cell-centered rhombus quads to a true
Goldberg polyhedron hex grid (vertex-centered tiles, 10n²+2 tiles, exactly 12
pentagons), and replaced the blurry fixed-size globe textures with a scalable
two-tier renderer: per-rhombus mipmapped base textures for the zoomed-out view,
plus a camera-following detail page cache for crisp per-pixel hex rendering at any
zoom and any subdivision n.

Work landed across four commits on `feature/worldgen-hex-tiles`:
- `9be388f` — SphereGrid: hex Voronoi assignment, 6-neighbor offsets, locateHex (first attempt, cell-centered)
- `11c3b38` — SphereGrid: Goldberg vertex-dual rework (vertex-centered, 10n²+2, seam cache deleted)
- `40c365a` — PlanetIO format v2, debug BMP export harness
- `f4a3de7` — planet-view two-tier renderer, planet.frag rewrite, OrbitCamera deep zoom

---

## Decision history

### Original intent

`docs/technical/technical-notes.md` and the early game design documents described
a hex planet from the start — Goldberg polyhedra, 12 pentagons, the Rimworld
globe as a reference. An earlier project (ColonySim, recovered at
`%TEMP%\colonysim-recover`) implemented a true Goldberg 10n²+2 grid (163,842
tiles at n=128) with 12 pentagons.

### M2: the quad-grid slip

When M2 built SphereGrid it used cell-centered tiles at (i+0.5)/n and a
floor-based quad-cell assignment (8 neighbors in a Moore-neighborhood-like set).
This produced 10n² tiles rather than 10n²+2, and it slipped through because
`data-model.md` carried a hedge — "may use geodesic subdivision or other methods"
— that left room for it. No recorded decision; it just happened. The technical
doc (`world-generation-implementation.md`) used a TileId formula of
`r*n^2 + j*n + i` that matched the cell-centered layout.

### First hex attempt (9be388f): cell-centered, didn't work

The first hex conversion kept cell centers at (i+0.5)/n and switched from floor
to cube-round assignment, with 8→6 neighbor offsets. The problem: a cell-centered
triangular lattice does NOT stitch across the 20 icosahedron-edge seams — the two
charts meet with a half-cell offset, creating pentagon rows along every seam and
degree-4/5 pinwheel artifacts at the poles. This required a precomputed
seam-neighbor cache.

### Goldberg rework (11c3b38): vertex-centered, seam cache deleted

The recovered ColonySim prototype was a vertex-centered Goldberg grid. Tile
centers at chart vertices (i/n, j/n) share the same physical position across
seams — the same property that makes the render mesh seam-free — so hexes close
cleanly across all 20 edges with no cache. The seam cache from 9be388f was
deleted. Exactly 12 pentagon tiles remain at the 12 icosahedron vertices
(including both poles). All other tiles are hexagons with exactly 6 neighbors.

---

## Contract amendments (deliberate breaking changes)

### 1. TileId encoding and tile count (2026-06-12)

The original "frozen" M2 encoding `r*n^2 + j*n + i` (cell-centered, 10n² tiles)
was replaced by the Goldberg vertex encoding:

```
owned vertex:  rhombus * n*n + j*n + (i-1),  i in [1..n], j in [0..n-1]
north pole:    10*n*n
south pole:    10*n*n + 1
total tiles:   10*n*n + 2
```

Rationale: vertex-centered tiles are the correct Goldberg layout; the
cell-centered approach produced seam artifacts that required a growing cache.
All consumers updated in the same PR; PlanetIO v2 rejects old files.

### 2. locate() deleted, replaced by locateHex() (2026-06-12)

The original `locate(latDeg, lonDeg) -> TileId` API was deleted (One Path Rule)
and replaced by `locateHex(latDeg, lonDeg) -> HexSample{tile, neighbor, edgeDistance}`.
`locateHex` returns the 2nd-nearest Voronoi center and the Voronoi-edge distance
in lattice units, which `PlanetSampler` uses directly for boundary blending.
The neighbor returned is the true geometric blend partner (continuous across all
edges), not a best-dot-of-neighbors fallback. API signature frozen going forward.

### 3. PlanetIO format v2

Format version bumped 1→2 on the hex conversion. The loader rejects v1 files.
`GameLoadingScene` auto-regenerates on any load failure, so old quickstart caches
rebuild silently. This was a deliberate "old saves not loadable" decision;
`downhill` changed from 0..7 range to 0..5, and the TileId space changed, making
v1 data structurally incompatible.

---

## Re-baselining note

The same seed now yields a different world: 6-neighbor generation stages (flood
fill, precipitation flow, etc.) produce different plate shapes and river networks
than the 8-neighbor version. Property test tolerances were updated; golden-output
tests are self-relative (they check cross-thread determinism, not a fixed bit
pattern). This was expected and accepted.

---

## Two-tier renderer design

See `docs/technical/planet-view-rendering.md` for the full architecture. Short
version:

- Base tier (`PlanetColorizer`): 10 per-rhombus RGBA8 textures, `baseSize = min(n, 1024)`, full mip chain. Baked async on a TaskPool worker; uploads ≤2 rhombi/frame. Dirty-flag coalescing prevents queuing more than one pending bake.
- Detail tier (`PlanetDetailCache`): 128×128-tile pages + 1-texel border ring = 130×130 texels, in a GL_TEXTURE_2D_ARRAY atlas (256 layers, ~17MB). Border texels use `canonicalTile` so cross-page seams agree with CPU assignment. Per-rhombus R16UI page table. LRU eviction.
- `planet.frag`: per-pixel cube-round mirroring `SphereGrid::hexRound` (floor(x+0.5) ties), Voronoi edge distance in unskewed Cartesian, fwidth-AA border band fading in at tilePx 14→40.
- GPU cost: O(screen pixels) at any n, <60MB total regardless of n.

---

## ColonySim recovery notes (from %TEMP%\colonysim-recover)

Worth preserving from the recovered prototype:

- The prototype used a true Goldberg 163,842-tile grid (n=128, 10·128²+2) with 12 pentagons. This was the design we eventually matched.
- Minor-plate flood-fill post-mortem: flood-fill produces blocky plates at low counts. Voronoi growth (assign each frontier tile to the nearest plate seed by geodesic distance) produces more natural boundaries. Worth adopting in a future PlateStage revision.
- Float-precision lesson: chunk-equality checks that used `==` on float elevation comparisons failed non-deterministically across compilers; switch to integer-keyed comparisons or epsilon bands.
- Perimeter-sampling optimization: sampling the chunk perimeter first (spiral outward) allows early-exit when all perimeter samples share one tile — the entire chunk is pure, skip interior sampling.
- Whittaker biome thresholds (temperature × precipitation) from the prototype are directionally correct but tuned for the old resolution. Re-tune for the current 10n²+2 grid if biome shapes look off at design-target n=1449.
- Old resolution target was 100–10M tiles; current design target is n=1449 (~21M tiles, ~5km/tile at Earth scale).

---

## Files modified

**Grid / world gen:**
- `libs/world/worldgen/grid/SphereGrid.h` — full Goldberg header, `canonicalTile`, `locateHex`, `locateRhombusUV`, `EdgeXform`
- `libs/world/worldgen/grid/SphereGrid.cpp` — vertex encoding, `hexRound`, `canonicalVertex`, `buildEdgeXforms`, new tests
- `libs/world/worldgen/grid/SphereGrid.test.cpp` — rewritten for Goldberg contracts
- `libs/world/worldgen/stages/PlateStage.cpp`, `TerrainStage.cpp`, `PrecipitationStage.cpp`, `BiomeStage.cpp` — `array<TileId,8>` → `<TileId,6>`
- `libs/world/worldgen/sampling/LandingSite.cpp`, `PlanetSampler.cpp` — 6-neighbor, `locateHex`
- `libs/world/worldgen/debug/DebugImageExporter.cpp` — 6-neighbor
- `libs/world/worldgen/data/WorldData.h` — `downhill` comment updated to 0..5
- `libs/world/worldgen/io/PlanetIO.h/.cpp` — format version 1→2

**Planet-view:**
- `libs/planet-view/PlanetColorizer.h/.cpp` — base tier, async bake, dirty-flag coalescing
- `libs/planet-view/PlanetDetailCache.h/.cpp` — detail tier, atlas, page table, LRU
- `libs/planet-view/PlanetDetailCache.test.cpp` — page math, border texels, LRU
- `libs/planet-view/PlanetLru.h` — LRU helper
- `libs/planet-view/PlanetPageMath.h` — page/texel layout constants
- `libs/planet-view/PlanetScheduler.h/.cpp` — per-frame visibility → page requests
- `libs/planet-view/PlanetRenderer.h/.cpp` — binds base + page table + atlas, sets u_n
- `libs/planet-view/OrbitCamera.h/.cpp` — dynamic near plane, n-aware min distance
- `libs/planet-view/shaders/planet.frag` — per-pixel hex assignment + two-tier blend

**Docs:**
- `docs/technical/world-generation-implementation.md` — Goldberg grid, TileId amendment, neighbor order, EdgeXform, locateHex, PlanetIO v2, frozen-contract amendments
- `docs/technical/planet-view-rendering.md` — new; two-tier architecture, budgets, scaling table
- `docs/technical/3d-to-2d-sampling.md` — locateHex addendum (FindClosestNeighbor/quadtree sketch superseded)
- `docs/technical/technical-notes.md` — hex grid now implemented; status note added
- `docs/design/features/world-generation/data-model.md` — Goldberg grid replaces "may use other methods" hedge
- `docs/status.md` — Phase 4 task marked complete; Phase 5 verification task added

---

## Next steps

- Phase 5: end-to-end verification — New Game flow, visual zoom checks, color-mode switching, n=1449 smoke test.
- Open PR from `feature/worldgen-hex-tiles` → `main`.
- M7: benchmark-gated default resolution, cross-platform determinism CI gate.
- Future: planet database streaming for n≥4096 (planned separate epic).
