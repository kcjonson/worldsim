# Planet-view rendering

Created: 2026-06-12
Status: Implemented (feature/worldgen-hex-tiles)

Supersedes: the "M3f-2 chunked-LOD" placeholder from the M3f-1 commit and
`docs/development-log/entries/2026-06-10-worldgen-foundation-merged.md` note
("chunked-LOD deferred to M3f-2"). That approach — drawing tile geometry — was
replaced by a two-tier texture + per-pixel shader design that scales to any n
with O(screen pixels) GPU cost and constant GPU memory.

---

## Two-tier architecture

The globe renders in two tiers that blend seamlessly as the camera zooms:

**Base tier** (always resident): answers "zoomed out". Per-rhombus RGBA8 textures
at `baseSize = min(n, 1024)` with a full mip chain, `GL_LINEAR_MIPMAP_LINEAR` min
filter. When tiles are sub-pixel the mip hierarchy is the correct, shimmer-free
answer — no per-tile work needed. ~40MB + mips total for n≥1024.

**Detail tier** (camera-following page cache): answers "zoomed in". Pages are
128×128 tiles plus a 1-texel border ring = 130×130 texels, baked with
`SphereGrid::canonicalTile` so border texels match the CPU assignment across
rhombus seams. An LRU atlas (`GL_TEXTURE_2D_ARRAY`, 256 layers, ~17MB) holds
resident pages; a per-rhombus R16UI page table (one layer per rhombus, entries
= atlas layer+1 or 0-for-not-resident) lets the shader look up any page in
constant time.

The blend between tiers happens in the fragment shader based on `tilePx` (pixels
per tile from `fwidth(axial)`), fading from base-only below 2 px/tile to
detail-dominant above 5 px/tile.

---

## Per-pixel hex assignment (planet.frag)

The mesh stays a fixed ~166k-vertex sphere. No tile geometry exists. Hex
assignment is pure shader math:

```glsl
vec2 axial = v_uv * u_n;          // tile centers at integer lattice coords
ivec2 cell = hexRound(axial);     // cube round, floor(x+0.5) ties (matches SphereGrid::hexRound)
float tilePx = 1.0 / length(fwidth(axial));
```

Voronoi edge distance for the border line uses the 6 fixed neighbor offsets in
unskewed Cartesian (basis vectors equal length at 60°):

```glsl
vec2 cart(vec2 a) { return vec2(a.x + 0.5*a.y, a.y*0.8660254); }
```

The border band fades in over `tilePx` 14→40, is AA'd via `fwidth(edgeDist)`,
and darkens to 55% of the tile color. GPU cost is O(screen pixels) at any n.

---

## Base-tier baking (PlanetColorizer)

`PlanetColorizer` owns the 10 per-rhombus textures and async bake pipeline:

- `bake(world, mode)` runs on a `foundation::TaskPool` worker, producing CPU
  RGBA8 buffers for all 10 rhombi (thread-safe; no GL calls).
- `uploadPending()` pushes ≤2 rhombi per frame to GL, then calls
  `glGenerateMipmap`. Called once per frame on the render thread.
- **Dirty-flag coalescing**: a snapshot or color-mode change arriving mid-bake
  sets a `dirty` flag. On completion, if `dirty`, exactly one fresh bake is
  scheduled — never more than one queued.

---

## Detail-tier page cache (PlanetDetailCache)

`PlanetDetailCache` owns the atlas + page table:

- Per-frame flow: `beginFrame()` → scheduler calls `requestPage(rhombus, pi, pj)`
  for visible and prefetch pages → `endFrame()` bakes queued pages serially (each
  is ≤1ms), uploads ≤4 pages/frame, pushes R16UI page table updates.
- Pages bake on the render thread (no TaskPool): each 130×130×4 = ~68KB bake is
  sub-millisecond and serializes cheaply against the colorizer's worker.
- LRU eviction (`PlanetLru`) keeps atlas usage bounded regardless of n.
- `invalidate()` / `setWorld()`: drops all pages on color-mode change or new
  snapshot; they re-bake on demand.

**No seams**: border texels call `SphereGrid::canonicalTile(r, i, j)` for i,j
outside the [1..n] × [0..n-1] owned range, resolving to the neighbor chart's
owner. This is the same canonicalization the CPU uses for tile assignment, so
cross-page and cross-rhombus borders never disagree.

---

## Scheduler (GlobeView / PlanetScene)

Each frame, the scheduler:
1. Ray-casts the viewport against the sphere to get per-rhombus visible uv rects
   (using `SphereGrid::locateRhombusUV`).
2. Converts to page-index ranges; requests intersecting pages plus a 1-page
   prefetch ring when `tilePx > ~2`.
3. Calls `detail.requestPage(r, pi, pj)` — resident pages are just touched in the
   LRU; missing pages queue for baking.

---

## OrbitCamera deep zoom

`OrbitCamera` has two additions for deep-zoom correctness:

- **Dynamic near plane**: `near = max(kMinNear, 0.2*(distance - 1.0))` — keeps
  the near plane close enough to the surface for full depth precision as the
  camera approaches.
- **n-aware min distance**: settable via `setMinDistance(d)` so the caller can
  enforce that one tile stays ≥~50px; GlobeView computes this from n and the
  display size.

---

## GPU memory budgets

| Resource | Size |
|---|---|
| Base textures (10 rhombi, min(n,1024)², RGBA8 + mips) | ~40MB + mips |
| Detail atlas (256 pages × 130×130 × RGBA8) | ~17MB |
| Page tables (10 rhombi × ceil(n/128)² × R16UI) | <1MB at n=1449 |
| Total GPU (renderer) | <60MB at any n |

The base-tier size is fixed once n≥1024. Detail-tier and page-table sizes grow
with n but only logarithmically in the dominant dimension.

---

## Scaling table

| n | tiles | WorldData RAM | GPU (renderer) | CPU bottleneck |
|---|---|---|---|---|
| 256 | 655,362 | ~17MB | <60MB | generation |
| 1024 | 10,485,762 | ~273MB | <60MB | generation |
| 1449 | ~21M | ~547MB | <60MB | generation (design target) |
| 2048 | ~42M | ~1.1GB | <60MB | generation |
| 4096 | ~168M | ~4.4GB | <60MB | planet database streaming (future epic) |
| 16384 | ~2.7B | — | <60MB | float precision limit (shader) |
| 20724 | ~4.3B | — | <60MB | uint32 TileId cap |

Renderer cost is O(screen pixels) at every row. The GPU column doesn't grow with
n. At n≥4096 the binding constraint shifts to CPU RAM — the already-planned
planet-database streaming epic addresses that.

Float precision note: `v_uv * u_n` in the shader stays accurate while the integer
part of n fits in float mantissa. At n~16384 the mantissa (23 bits) starts losing
the fractional part, and sub-tile assignment can wobble. The uint32 TileId
formula `r*n*n + j*n + (i-1)` overflows past n=20724 (10·20724²+2 > 2³²).

---

## Color modes

Seven modes (`ColorMode` enum, 0–6): Terrain, Temperature, Precipitation, Biome,
Plates, Snow, Combined. Both tiers use `PlanetTileColor::colorForTile(world, mode,
tileId)` as the pixel source, so the same function drives base bakes and page
bakes. Switching modes calls `requestBake` on the colorizer and `invalidate` on
the cache; the old base texture and cached pages serve as a fallback until the
fresh bake arrives.

---

## Related documentation

- `libs/planet-view/PlanetColorizer.h` — base tier class
- `libs/planet-view/PlanetDetailCache.h` — detail tier class
- `libs/planet-view/PlanetPageMath.h` — page/texel layout constants
- `libs/planet-view/shaders/planet.frag` — per-pixel hex assignment
- `docs/technical/world-generation-implementation.md` — SphereGrid contracts
- `docs/development-log/entries/2026-06-12-worldgen-hex-goldberg.md` — decision history
