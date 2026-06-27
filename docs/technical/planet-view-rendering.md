# Planet-view rendering

Created: 2026-06-12
Updated: 2026-06-16 — detail-tier page cache removed; the globe now renders from a single smooth tier (see Historical Addendum).
Status: Implemented

Supersedes: the "M3f-2 chunked-LOD" placeholder from the M3f-1 commit and
`docs/development-log/entries/2026-06-10-worldgen-foundation-merged.md` note
("chunked-LOD deferred to M3f-2"). That approach — drawing tile geometry — was
replaced by a texture + per-pixel shader design with O(screen pixels) GPU cost;
the base textures scale with n² up to the 2048 product cap (see GPU memory budget).

---

## Architecture

The globe renders from a single **base texture** per rhombus (RGBA8 + full mips,
`baseSize = min(n, kBaseMax)`, `kBaseMax = 2048` = the product cap
`kMaxGridSubdivision`, so `baseSize == n` and mip 0 holds exactly one texel
per tile). That texture is just the per-tile color store — the hexagons are drawn
**analytically per pixel** in the shader (next section), so the globe stays crisp
vector hexagons at any zoom. The mip chain is used only to average tiles into a
shimmer-free image once they go sub-pixel.

> A second camera-following per-tile "detail tier" used to stream full-n pages at
> close zoom (see Historical Addendum). It sampled NEAREST (flat hex facets), was a
> single level, and stepped jarringly from the blurry base — removed 2026-06-16.
> Within the n≤2048 cap and the camera's px/tile clamp, the analytic-hex render
> over the full-resolution base needs no streaming or second tier.

---

## Crisp analytic hexagons (planet.frag)

The mesh is a fixed ~166k-vertex sphere with no tile geometry — the hexagons are
evaluated per pixel. Each fragment finds its cell and on-screen size:

```glsl
vec2  axial  = v_uv * u_n;        // tile centers at integer lattice coords
ivec2 cell   = hexRound(axial);   // cube round, matches SphereGrid::hexRound
float tilePx = 1.0 / length(fwidth(axial));
```

- **Crisp fill.** The cell's exact color at its texel center, forced to mip 0:
  `textureLod(u_baseTex, (vec2(cell)-vec2(1,0)+0.5)/u_n, 0.0)`. Sampling the texel
  center returns that texel un-blended. (The old blur came from
  `texture(u_baseTex, v_uv)` landing on tile *vertices* = texel boundaries, so
  bilinear mixed two tiles on every edge.)
- **Anti-aliased edges.** A Voronoi distance to the 6 neighbors (in `cart()`
  unskewed Cartesian, 60° basis) gives signed edge distances; the **two** nearest
  cells are blended by coverage `smoothstep(-w, +w, edgeDist)`,
  `w = clamp(0.5*fwidth(edgeDist), 1e-4, 0.7)`. Two-cell blending keeps the
  three-cell hex corners clean; the `fwidth` clamp stops the limb from smearing.
  ~1px vector-clean edges at any zoom.
- **Sub-pixel.** Below ~1.5 px/tile the per-pixel cell flickers, so cross-fade to
  the mipmapped `texture(u_baseTex, v_uv)` average; the two agree at the handoff.
- **Faint outline.** A subtle dark cell edge keeps the lattice legible between
  same-colored tiles (tunable `kGridDarken`, `kGridNearPx/FullPx`).

Tunable consts at the top of `planet.frag`. GPU cost is O(screen pixels) at any n.

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

## OrbitCamera deep zoom

`OrbitCamera` has two additions for deep-zoom correctness:

- **Dynamic near plane**: `near = max(kMinNear, 0.2*(distance - 1.0))` — keeps
  the near plane close enough to the surface for full depth precision as the
  camera approaches.
- **n-aware min distance**: settable via `setMinDistance(d)` so the caller can
  enforce that one tile stays ≥~50px; GlobeView computes this from n and the
  display size.

---

## GPU memory budget

| Resource | Size |
|---|---|
| Base textures (10 rhombi, n², RGBA8 + mips), n=1449 (default) | ~112MB |
| Base textures, n=2048 (Ultra, the product cap) | ~224MB |

Scales with n² up to the n=2048 cap, then fixed. 2048² is well within
`GL_MAX_TEXTURE_SIZE`. Renderer cost is O(screen pixels); zoomed-out frames sample
coarse mips, so FPS stays high. The binding constraint at the cap is CPU RAM for
`WorldData` (n=2048 ≈ 42M tiles ≈ 1.1GB), not the ~224MB of GPU textures.

Float precision note: `v_uv * u_n` in the shader stays accurate while the integer
part of n fits in float mantissa. At n~16384 the mantissa (23 bits) starts losing
the fractional part, and sub-tile assignment can wobble. The uint32 TileId
formula `r*n*n + j*n + (i-1)` overflows past n=20724 (10·20724²+2 > 2³²).

---

## Color modes

`ColorMode` modes (Terrain, Temperature, Precipitation, Biome, Plates, Snow,
Combined, Hydrology) are cycled by right-click. The base tier uses
`PlanetTileColor::colorForTile(world, mode, tileId)` as the pixel source.
Switching modes calls `requestBake` on the colorizer; the old base texture serves
as a fallback until the fresh bake arrives.

---

## Related documentation

- `libs/planet-view/PlanetColorizer.h` — base tier class
- `libs/planet-view/PlanetTileColor.h` — tile → RGBA color mapping
- `libs/planet-view/shaders/planet.frag` — per-pixel hex assignment + faint outline
- `docs/technical/world-generation-implementation.md` — SphereGrid contracts
- `docs/development-log/entries/2026-06-12-worldgen-hex-goldberg.md` — decision history

---

## Historical Addendum: detail-tier page cache (removed 2026-06-16)

The original design was two-tier: the base tier above plus a camera-following
**detail tier** (`PlanetDetailCache` + `PlanetScheduler`) that streamed per-tile
color pages for crisp close-up rendering.

- Pages were 128×128 tiles + a 1-texel border ring = 130×130 texels, in an LRU
  `GL_TEXTURE_2D_ARRAY` atlas (256 layers, ~17MB), addressed by a per-rhombus
  R16UI page table (entry = atlas layer+1, or 0 for not-resident).
- `PlanetScheduler` ray-cast the viewport each frame to request visible +
  prefetch pages; pages baked serially on the render thread (sub-ms each).
- Border texels used `SphereGrid::canonicalTile` so cross-page and cross-rhombus
  seams agreed with CPU tile assignment.
- The shader fetched the resident page (NEAREST, one flat color per hex) and
  blended base→detail over `tilePx` 2→5, then darkened Voronoi edges to draw the
  grid.

It was removed because the flat per-tile fill read as a jarring discrete step at
deepest zoom, and because within the n≤2048 cap a full-resolution mipmapped base
is a complete LOD (2026-06-16). The full implementation lives in git history
(pre-2026-06-16) alongside the decision record in the dev-log entry above.

**Future (only if n>2048 ever ships).** Once worldgen can generate above 2048 (a
planet-database/streaming epic — n=4096 is ~4.4GB of `WorldData` and ~895MB of
full-resident base mips), restore a single streamed finest level from the deleted
page cache, but sampled **mipped/LINEAR (not NEAREST)** and cross-faded into the
base above its resolution — reusing `PlanetLru` / the page table / the ray-cast
scheduler. The camera's px/tile clamp (`GlobeView kTargetPx`, currently 100) keeps
the streamed region to ~one octave above the base, so a full per-octave software
pyramid is not needed even then.
