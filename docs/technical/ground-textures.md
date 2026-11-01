# Ground Texture System

Created: 2025-12-13
Updated: 2026-11-01 (ground tile variants: tiles become SVG/Lua assets baked to texture arrays and mixed by a hex lattice in the shader, and the atlas goes away; spike results and the perf plan added. Later: the bank, the hex sampler and the macro texture are in the tile pass, the atlas is deleted, and the surfaces without art paint placeholder defs. Then: the bake runs each generator once, on the pool, and keeps its bakes in a disk cache the splash reads)
Status: In progress

The ground is painted by the tile pass: one quad per chunk whose fragment shader picks a land surface for every pixel, samples that surface's baked ground tiles, and paints water and shore over the result. Which surface a pixel gets is decided by the land field of [terrain-polygons-architecture.md](./organic-terrain/terrain-polygons-architecture.md) D16, so no surface boundary follows the tile grid. This doc covers the ground tile assets, the baker, how the shader samples them, and the per-chunk data the pass reads, and summarizes the field; the spec holds the field's details and defaults.

On main each surface is one 512x512 pattern in a texture atlas, stamped on every 1 m tile. This doc specifies its replacement, which is built but not yet on main. The sections on render tiles and the land field describe what stays.

## Why change

Two problems with the pass on main:

1. Faint lines on every tile edge that flicker while panning. `land.glsl` samples each pattern at the point's offset inside its 1 m tile, from an atlas whose cells are packed edge to edge with linear filtering, no gutter and no mipmaps. Every tile boundary blends in half a texel of the neighboring atlas cell (Grass's right edge picks up Dirt, GrassTall's left edge picks up Mud). The camera isn't pixel-snapped, so the band comes and goes with the sub-pixel offset.
2. Repetition. The same pattern is stamped on every tile, so its soft ellipses read as a 1 m grid of spots, and the only variation is the surface id: Grass, GrassShort, GrassMeadow and GrassTall are flat greens that differ by tint.

The design keeps tiles as SVG files on disk, so they stay moddable, and uses the procedural-tree approach (a Lua generator and seeds) to make many unique variants of each. A baker rasterizes them once at load into mipmapped texture arrays, and the shader mixes variants through a hex lattice. The atlas (salal's `TileAtlasBuilder`, built by `GameBootstrap`) is removed, which removes problem 1 at its source; there's no separate stopgap.

Rollout: nothing from this work merges until it has been tested by hand. It lives on draft PRs in worldsim and salal, and the salal change needs a tag before the worldsim PR could land.

## Layers

| Layer | What | Drawn by |
|-------|------|----------|
| Ground | each surface's baked tile variants, mixed per pixel by a hex lattice, the surface chosen per pixel by the land field | tile pass: `tile.frag`, `includes/land.glsl` |
| Water and shore | from the chunk's terrain distance field (D10) | tile pass: `includes/water.glsl` |
| Grass tufts | `Groundcover_Grass`, a procedural Lua tuft, GPU-instanced | `GroundcoverRenderer` |
| Flora and other entities | vector assets | `EntityRenderer` |

This is the original plan's two tiers as they shipped: the ground tiles are the static micro-detail (Tier 1 in [the vector-graphics architecture](./vector-graphics/INDEX.md)), and the grass tufts and flora are live vectors over them.

## Ground tile assets

A ground tile is an asset with the role `ground`. Defs live at `assets/world/ground/<Surface>/<Surface>.xml`, under the scanned root, with the file stem matching the folder (the registry requires it). The asset-manager lists `ground/` as a category with no extra work.

```xml
<asset>
  <surface>Grass</surface>
  <assetType>procedural</assetType>
  <generator>
    <scriptPath>@shared/ground_grass.lua</scriptPath>
    <params>...</params>
  </generator>
  <variantCount>8</variantCount>
  <period>4</period>
  <pixelsPerMeter>128</pixelsPerMeter>
  <rotate>free</rotate>
  <macro>...</macro>
</asset>
```

| Field | Meaning |
|-------|---------|
| `<surface>` | the `world::Surface` it paints |
| `<assetType>` | `procedural` with a `<generator>`, or `simple` with one or more `<svgPath>`s, each SVG being one fixed variant |
| `<variantCount>` | variants baked, 1 to 32, default 8 (for `simple` the SVG list is the variant list) |
| `<period>` | meters one variant covers, default 4; it must divide 512 so it lines up across chunks |
| `<pixelsPerMeter>` | base texel density, default 64, so a 4 m variant is a 256x256 layer |
| `<rotate>` | `free`, `quarter`, `none` or `mirror`; `none` for art with baked top-left lighting, `mirror` (never turned, each cell flipped left to right or not) for art with an up, like grass blades |
| `<macro>` | two tint colors (dry, lush), plus the amplitude and wavelengths of the low-frequency layer the shader mixes between them |

Generators and SVGs draw in meters on a `[0, period]` square. Shapes may cross the edge, so authors never match edges by hand: `AssetRegistry::buildPaths` (and `buildMeshes`, a thin call over it) repeats every path of a procedural ground def that crosses an edge at the offsets it crosses into, right after the original. Paint order holds across the seam, and the meshes clipped to the square are the seamless tile. Drawing the whole mesh 3x3 instead would paint each copy's full-tile layers over the earlier copies' overhang, so the seams would show.

Validation (`AssetValidator.cpp`): exactly one ground def per land surface, `period` divides 512, `variantCount` is 1 to 32, and the role is known (`parseAssetRole`, `isKnownRole`).

Lua gets two noise bindings (`LuaEngine.cpp`, `registerBindings`), both thin wrappers over salal's `foundation/random/HashNoise.h`: `noise2(x, y, seed)`, and `periodicNoise2(x, y, period, seed)`, which wraps the gradient lattice so density fields tile. A Lua path also gets `path:setLod(N)` (see LOD below).

`tessellateAsset` currently wraps its `uint16` indices silently past 65,535 vertices (`AssetRegistry.cpp`). It splits into several meshes instead, the way `SVGLoader.cpp` guards. Dense ground tiles will hit the limit, and so could flora.

| Surface | D13 level | Source | px/m |
|---------|-----------|--------|------|
| Mud | 1 | small generator | 64 |
| Sand | 2 | small generator | 64 |
| Dirt | 3 | static SVG, ported from the old pattern | 64 |
| GrassShort | 4 | `ground_grass.lua` | 128 |
| Grass | 5 | `ground_grass.lua` | 128 |
| GrassMeadow | 6 | `ground_grass.lua` | 128 |
| GrassTall | 7 | `ground_grass.lua` | 128 |
| Rock | 8 | small generator | 64 |
| Snow | 9 | small generator | 64 |
| Water | not land | none, never sampled | |

A Water tile paints as its bed and water itself is drawn from the distance field, so Water has no ground tile. The small generators are `ground_sand.lua` and so on, in `assets/shared/scripts/`.

## Baker

`GroundTileBank` in `libs/world-view/world/rendering/` runs once the asset registry finishes its async load (the splash), on the GL thread, outside any frame.

For each def and each seed `0..variantCount-1` (salted per def name) `GroundTileBaker` runs the generator once and tessellates each path once with `AssetRegistry::buildPaths` (already wrapped, see above), every variant at once on a `TaskPool`. Each generator run builds its own Lua state and the registry is only read, so the runs share nothing; the GL work stays on the GL thread. Each mip then packs the paths its density keeps into meshes with `TessellatedPaths::meshesAt` (a dense tile is over 65,535 vertices, so several meshes) and draws them once into a `Renderer::RenderTarget` the size of the square, which clips the overhang. It supersamples (8x8 for mips 0 and 1, 4x4 below, since salal has no MSAA), Kaiser-downsamples in linear light, mean-locks each mip to a filtered copy of mip 0 and encodes sRGB. The coordinate system and viewport are restored afterwards. This is the mesh path flora uses, so gradients and colors match the asset-manager exactly.

Every mip level is rendered from the vectors, not generated (`glGenerateMipmap` would neither wrap nor drop LOD nodes); see LOD. The bank allocates each raw-GL `GL_TEXTURE_2D_ARRAY` level by level with `glTexImage3D` once it knows the layer counts, and writes each variant's chain in with `glTexSubImage3D` as soon as it's baked: `GL_SRGB8_ALPHA8` (so filtering and the hex blend run in linear light), `GL_REPEAT`, trilinear, `GL_TEXTURE_MAX_LEVEL` at the last baked mip. No anisotropic filtering; a top-down orthographic view has no anisotropy to filter.

The layers of one array share a size, and the grass surfaces (128 px/m, 512x512 layers) differ from the rest (64 px/m, 256x256), so there are two arrays, one per layer size; the tile shader has samplers for up to four, and a def needing a fifth is a bake error. The bank keeps a per-surface table of `{array, firstLayer, layerCount, invPeriod, texelsPerMeter, rotate, macro tints and amplitude}`. The baker logs each def's report; the bank logs the totals (its steps' time and the wall clock, how many defs came from the cache) and the bytes of each array.

The baker works in steps, so a caller can spread a def over frames: `begin` looks the def up in the disk cache (below) and hands back the whole bake when it's there, `buildPaths` runs every variant's generator on the pool, `bakeVariant` renders and filters one variant on the GL thread, and `finish` measures the variants, logs the report and stores the bake. `bakeDef` runs them in order in one call; the asset-manager's Bake view runs them a frame at a time. The bank runs a step a call, so the splash keeps drawing between them: one call uploads every def in a row that the cache holds, and at the first it doesn't, builds that def's paths (the GL thread waits on the pool, about 0.4 s for Grass); each later call bakes and uploads one variant, and the one that bakes a def's last variant stores the def. The splash's boot log shows `Baking ground tiles n/N` and the Enter button waits for the last. A `--scene` jump skips the splash, so `GameLoadingScene` finishes whatever is left before it loads the planet, and a direct switch to the game scene bakes it all at once. A failed variant or a land surface with no def blocks the splash the way a validation error does, and a def that failed isn't stored.

The bake goes well past 0.5 s, so each def's baked chains are cached on disk (`GroundTileCache`): one file a def, `cache/ground/<defName>.gtile` beside the executable, holding every variant's mips and the samples the report measures, with a checksum. Beside the executable means each build tree (worktree, config) and each app (world-sim, asset-manager) keeps its own, so two builds whose baker code differs never read each other's bakes, and a clean build starts empty. The key is a hash of the def's XML file, its generator script (an `@shared/` one included; scripts are sandboxed and can't load others), name, variant count, period, density, the render tile size, and `GroundTileBaker::kVersion`, which is bumped when the bake's output changes for the same inputs. A file whose key, shape or checksum doesn't match is ignored and the next bake overwrites it, so editing a def's XML or script bakes it again: in the asset-manager on Reload, in the game on its next launch.

Ground_Grass (8 variants, 512 px layers, about 230,000 vertices a variant), RelWithDebInfo on a 32-thread machine, 2026-11-01, three cold runs and three from the cache:

| | Def | A variant |
|---|---|---|
| Before: `buildMeshes` a mip, variants in turn | 19,621 ms | mesh 1,823 (7 generator runs), render 491, filter 134 |
| Generate once, variants on the pool, cold | 5,700 to 5,830 ms | paths 420 to 455 ms for all 8 at once (generate 240 to 275, tessellate 150 to 160 each), render 520 to 535, filter 131 to 135 |
| From the cache | 19 to 23 ms | |

What's left cold is the render, and nearly all of it is the GPU finishing salal's batched draws, which `readPixels` waits on: about 150, 120 and 100 ms for the three dense mips (166k, 132k and 120k triangles), against 10 to 14 ms of CPU to build and submit them. Those runs predate the grass's calibrated LOD stacks; today's grass draws fewer triangles and bakes cold in about 2.5 s (Performance, "Bake and macro fill").

## LOD

An SVG element, or a `<g>`, may carry `data-lod="N"`: draw it only where the screen shows at least N pixels per meter. The game's zoom runs 2 to 160 px/m, default 24. The attribute inherits, and an element's effective N is the largest on itself and its ancestors. An element without one is always drawn. A Lua path gets the same through `path:setLod(N)`, a new field on `GeneratedPath`.

The LOD is baked into the mip chain, so it costs nothing per frame. Mip k of a layer has `pixelsPerMeter / 2^k` texels per meter, and the GPU picks it when the screen shows about that many pixels per meter. The baker renders each mip from the vectors (wrapped, 2x supersampled at that level's size), including only the nodes whose N is at or below that level's density. Pebbles, flower specks and blade strokes drop out cleanly at the zoom the author chose, instead of averaging into a muddy tint, and trilinear filtering fades each one over an octave of zoom. Rendering every level costs about 1.33x the base render at load.

NanoSVG drops attributes it doesn't know, so the loader filters in XML first. A pugixml pass resolves each element's effective `data-lod` and writes one filtered SVG text per distinct band, and each text is then parsed. salal's `loadSVG` only takes a path, so this needs a small salal addition: load from a string. That is one salal PR and a tag bump, which the worldsim PR pins.

Flora could adopt the same convention later; it's out of scope here.

The shader has its own, separate LOD for the hex blend (the single-sample cutoff below).

## Shader

`land.glsl` loses `u_tileAtlas`, `u_tileAtlasRects`, `u_tileAtlasRectCount` and `atlasColor`. In their place are one `sampler2DArray` per texel density and per-surface uniform arrays (array, first layer, layer count, inverse period, rotate mode, macro tints), indexed the way `u_landWarpFine[10]` already is.

`groundColor(surface, local, mpp)` is stochastic hex-tiling (after Mikkelsen 2022, practical hex-tiling):

- The lattice is hexagonal in chunk-local meters, with 4 m columns and 512/148 m rows (3.459 m against 3.464 m for a regular hex, 0.15% off). It repeats every 512 m, so it is continuous across chunk borders and precise anywhere, and each cell's id is an integer, so it hashes exactly.
- Each cell picks a variant layer, a rotation (per `rotate`) and a uv offset with `noiseHash3`. The weights are barycentric, sharpened by a contrast exponent (`terrain/ground/blendContrast`) so flat-color shapes don't ghost.
- The weights are Mikkelsen's: barycentric to the power `terrain/ground/blendContrast` (7), times 0.4 + 0.6 x the sample's luminance, then his gain with r from `terrain/ground/blendGain`: 0.75 at 48 px/m and up, 0.65 midway in log2 px/m, 0.5 (none) at 16 and under. The CPU turns r into the gain's exponent once a frame.
- Where one cell dominates (its sharpened weight, before the luminance term, over 0.98: most of a cell's interior), the shader takes one sample. Only the transition bands between cells take 2 or 3. This is the same coherent early-out the field uses for interior tiles.
- Below a density (`terrain/ground/hexMinPxPerMeter`, 12) it skips blending and takes the dominant cell's sample, the way `u_landWarpFineOctaves` drops warp octaves a pixel can't show. Cell interiors are the same sample either side of the cutoff, so crossing it only hardens the bands between cells. The spike ran with 6 px/m; 12 is the working value because dense blade art would show cut edges on the one-sample fallback.
- Variants are picked uniformly per cell. Weighted picks (mostly quiet, a few busy) wait for a def that asks for them.
- Samples use `textureLod` at a mip level computed once per draw on the CPU for each surface, `log2(texels per meter / framebuffer pixels per meter) + terrain/ground/lodBias` (0.25, research rule 9). The view is an orthographic top-down projection, so texel density is the same at every pixel and rotation doesn't change it. That avoids `textureGrad` and per-pixel derivatives, and nothing jumps at tile or hex edges.
- The samples come back linear (the arrays are sRGB), are blended linear, and `groundColor` encodes the result to sRGB, which the rest of the pass (fringe, rim, water) works in. The macro tint multiplies the sRGB color.

The macro layer comes from a per-chunk texture, not per-pixel noise. The chunk's generation worker fills a 2 m-per-texel texture (`GroundMacro.h`: 257x257 RGBA8, 264 KB a chunk, uploaded with the render tiles under the same version and LRU) from salal `HashNoise` gradient noise, each lattice corner hashed once a chunk. Texel (i, j) sits on chunk-local (2i, 2j) m, so the last row and column are the next chunk's first and a bilinear read matches across the border. R is the dry to lush mix (octaves at 36 and 14 m), G and B value noise at 10 m and 5 m (5 m is the shortest wave 2 m texels carry without a lattice pattern). The shader takes one bilinear sample a pixel: each surface mixes its def's dry and lush tints by R, scaled by the def's amplitude times `terrain/ground/macroAmp`, and every surface's value moves by G and B times `macroMidAmp` (4.5%) and `macroFineAmp` (3.5%). The def's `<wavelengths>` aren't read: one texture serves every surface. Per-pixel fBm is what cost about 30 ms in the December tile shader (`docs/debug-swapbuffers-performance.md`) and still costs 0.10 to 0.15 ms of GPU at the spike's scale. The same texture is where phase 2's climate and moisture data lands, and its C++ source can later tint groundcover tufts to match.

The fringe samples the other surface once, without hex blending: it's a thin band and the blend isn't visible there.

`landColor` calls `groundColor` for the painted, upper and lower surfaces. The fringe and rim logic is unchanged, and `evaluateLand` is untouched, so `SurfaceFieldGolden.test.cpp` stays valid (it only checks the field).

What a ground pixel costs, against main:

| | Main | New |
|---|---|---|
| Render-tile fetch | 1 | 1 |
| Ground samples, interior | 1, mip 0 of a 512 px/m pattern | 1 where a cell dominates, 2 or 3 in blend bands, mipmapped |
| Macro | none | 1 bilinear sample |
| Per-pixel noise | none | none (one hash per hex cell) |
| Surface edges | 2 samples | 2 to 4 samples, the fringe's other surface takes 1 |

Deleted with the atlas: `GameBootstrap::initializeTileAtlas`, `surfacePatternPath`, `assets/tiles/surfaces/`, and the now-dead `Chunk::getSurfaceColor` and `getBiomeColor`.

## Art direction

The look matches the flora: layered flat-color shapes in the palette family of `grass.lua`. The ground is dense and grass-dominant: blades in several tones, clover and broadleaf rosettes, small soil peeks, tiny pebbles. That is much denser than RimWorld's dirt with 1 px grass wisps, and it can be less detailed than the photo it's drawn from. LOD carries it across zooms: individual blades and clover near, clumps mid, tonal patches far.

The four grass defs share `ground_grass.lua` and differ only in params. It layers a base fill, clumps of 2 or 3 tones of blade strokes as thin polygons, sparse straw and dirt specks, and for GrassMeadow small flower specks. Density comes from `periodicNoise2`.

Dirt ports `assets/tiles/surfaces/Dirt/pattern.svg` to a static-SVG def, rescaled to the period canvas, which exercises the SVG path. Sand, Mud, Rock and Snow get small generators in place of today's fallback checkers.

## Render tiles

A chunk's generation worker builds its render tiles (`Chunk::computeRenderData`, with `paintSurfaces` and `buildRenderTiles` from `SurfaceField.cpp`): 518x518 two-byte tiles covering the chunk square plus `kRenderApronTiles` = 3 on every side, uploaded as one RG8UI texture.

- R, the paint surface: the tile's final surface, or for a Water tile its bed, the surface of the nearest land tile within 3 tiles (Sand when there is none).
- G, the edge byte: the edge surface in the low nibble (the highest surface within 2 tiles, whose warp amplitudes the tile takes) and the interior bit, 0x80, set when every tile within 3 is one surface.

Apron tiles come from the chunk's own apron, post-processed the way the neighbor post-processes its own tiles (D11), so both sides of a border read the same tiles and draw the same field. `ChunkRenderer` keeps an LRU cache of 32 chunks' textures and re-uploads a chunk's when its `renderDataVersion` changes. Render tiles cost 524 KB a chunk; the RGBA32UI tile data they replaced, with neighbor ids and edge masks, was 4 MB. The macro texture joins them with the same key.

## The land field

A summary; D16 in the spec is the definition.

1. For each surface among the 4x4 tiles around a point, its indicator at tile centers is blurred with the 3x3 binomial, guarded so 1-tile patches and 1-wide paths survive (the axis rule, floor 0.85, ceiling 0.15), and read bilinearly between tile centers.
2. Every surface is read at one warped point: world-space vector fBm, a fine term (4 m base, 4 octaves) plus a low term (26 m base, 2 octaves), with amplitudes by surface blended from the tiles' edge surfaces (rock's are smaller, so its edges stay harder), each component clamped to 1.5 m.
3. Surfaces paint low to high (D13): the highest whose field passes 0.5, else the one with the largest field.
4. The edge is anti-aliased over one pixel from `u_metersPerPixel`, with a sparse fringe of the upper surface on the lower side whose width follows the warp noise along the edge, and a rim darkening just inside the upper surface (rock only by default). A pixel in an interior tile costs one render-tile fetch, one macro sample and one ground sample.

`world::evaluateSurfaceField` is the one evaluation. `land.glsl` does the same steps in the same order, and `SurfaceFieldGolden.test.cpp` renders the shader and compares the two. Placement asks the C++ evaluation, so flora grows on the surface that is drawn.

## Tunables

All are live through the debug server: set one with `/api/dev/tunable?name=<name>&value=<v>`, read them with `/api/state?what=tunables`. The land defaults are in section 4 of the spec.

| Name | What |
|------|------|
| `terrain/land/warpFine/<Surface>`, `warpLow/<Surface>` | warp amplitudes in meters, by the edge's upper surface |
| `terrain/land/warpFineWavelength`, `warpLowWavelength` | base wavelengths, whole meters |
| `terrain/land/warpFineGain` | amplitude the fine term keeps per octave |
| `terrain/land/thinFloor`, `thinCeil` | the thin-feature guard |
| `terrain/land/fringe/<Surface>`, `rimDark/<Surface>` | fringe width in meters and rim darkening, by upper surface |
| `terrain/land/fringeOpacity`, `fringeAlongAmp`, `rimW`, `fringeBreakupWavelength` | the fringe's strength, its variation along the edge and its breakup, and the rim's width |
| `terrain/ground/blendContrast` | exponent that sharpens the hex weights (7) |
| `terrain/ground/blendGain` | Mikkelsen's gain r near (48 px/m and up), mid and far (16 and under): 0.75, 0.65, 0.5 |
| `terrain/ground/hexMinPxPerMeter` | below this density the shader takes one sample (12) |
| `terrain/ground/lodBias` | added to every surface's mip level (0.25) |
| `terrain/ground/macroAmp` | scales every def's macro tint amplitude (1) |
| `terrain/ground/macroMidAmp`, `macroFineAmp` | value swing of the macro's 10 m and 5 m octaves (0.045, 0.035) |

The ones the shader reads as they stand are in `kShaderTunables` (`ChunkRenderer.cpp`); `blendGain`, `hexMinPxPerMeter`, `lodBias` and `macroAmp` are turned into per-frame uniforms on the CPU, registered beside the land look's.

## Memory

| Group | Surfaces | px/m | Layer | Layers | Base | With mips (x1.33) |
|-------|----------|------|-------|--------|------|-------------------|
| Grass | 4 | 128 | 512x512 | 32 | 32 MiB | 42.7 MiB |
| The rest | 5 | 64 | 256x256 | 40 | 10 MiB | 13.3 MiB |
| Total | 9 | | | 72 | 42 MiB | 56 MiB |

That is 8 variants per surface at 4 bytes a texel. The atlas it replaces is 16 MB. Render tiles stay 524 KB a chunk and the macro texture adds 264 KB, so 8.3 MiB over the 32-chunk LRU.

Measured with today's content (Grass's 8 variants at 128 px/m, the eight placeholder defs' 4 each at 64 px/m, mips 0 to 6): 21.3 MiB, 10.7 MiB an array.

Driver-side BC1 (S3TC) on upload would be 8x smaller (about 7 MiB) and cost less bandwidth per sample. It gets measured in the bake and shader step and stays only if it's neutral or better and looks right.

## Performance

### Method

The method is WOR-463's (section 6 of the terrain spec), so the numbers compare directly with its tables: RelWithDebInfo, an RTX 3090, a 3072x1728 window, the quickstart planet, vsync off, `scripts/perf-capture.ps1`, the median of 9 samples per scenario after settling. Three views: river `-X 0 -Y 20` (mostly interior tiles), coast `-X 70030 -Y 0` (along a chunk border) and desert `-X 540 -Y 131480`. Each gets idle at zoom 20, 8, 3, 1.5, 0.75, 0.5 and 0.25, plus scroll at zoom 3 and 0.75. The script's zoom is one eighth of the screen's pixels per meter: zoom 3 is the default 24 px/m, zoom 0.25 is 2, zoom 20 is 160.

Recorded per run: `tileGpuMs` (the ground and water pass, GPU timestamp queries), scene `gpuMs`, frame p50 and p99 max, and the scroll scenarios' spikes. From the `[ChunkGen]` dev-tools log: chunk generation worker time (median, p90, max) and the main-thread upload per chunk. Plus the bake time added to the splash and the VRAM for the arrays and macro textures.

Captures must hold the GPU clocks (see the finding below): lock them from an admin shell with `nvidia-smi -lgc <min>,<max>` (undo with `-rgc`), or run a ballast load outside the tile timer.

### Gates

Every one must hold, on every view and zoom:

- `tileGpuMs` no higher than main beyond the noise floor, which is main captured twice with clocks held, taking the spread per row. The target is net neutral or faster, not a budgeted increase.
- `tileGpuMs` under 1.5 ms everywhere.
- Frame p50 and p99 and scene GPU not worse than main beyond noise, scroll scenarios included (they catch upload hitches).
- Chunk generation median and p90 not worse beyond the macro texture's own measured cost, and that cost reported.
- Bake time reported, with the 0.5 s rule above.

Runtime cost doesn't grow with art complexity. Tiles are sampled as textures, so a modder's denser SVG or busier generator only moves bake time, never frame time. The final capture with real content checks that.

Order of measurement: baseline main twice (the noise floor), profile today's pass before changing it, measure each change alone against the baseline so a regression has a name, then a final capture with real content before the PR. The tables go into this doc and the dev-log entry, and the JSON sits beside the WOR-463 captures in `perf-results/`.

### Clock state

On light views the driver drops the GPU to P3 or P2 (1035 to 1410 MHz against 1890 to 1950 at P0), and the same pass reads 1.4 to 2.1 ms instead of about 0.95. An unpinned capture then compares clocks, not code. The river zoom 8 and 20 "regression" against the WOR-463 table is this and not a code change; the captures committed with WOR-463 already show it. The unpinned baselines of main from 2026-10-31 (`perf-results/capture-2026-10-31-ground-main-<view>-{1,2}.json`, two runs per view) read river zoom 20 at 1.22 and 1.97 ms against 0.76 at P0, river zoom 8 at 1.51 and 1.53 against 0.99, and coast zoom 1.5 at 1.25 and 1.19 against 0.64. The gates compare against main captured with clocks held, and the table below is that.

### Main at P0

Today's atlas path with the GPU held at P0, `tileGpuMs` in ms, idle:

| Zoom | River | Coast | Desert |
|---|---|---|---|
| 20 | 0.76 | 0.74 | 0.83 |
| 8 | 0.99 | 0.60 | 0.74 |
| 3 | 0.67 | 0.66 | 0.57 |
| 1.5 | 0.58 | 0.64 | 0.52 |
| 0.75 | 0.47 | 0.59 | 0.50 |
| 0.5 | 0.44 | 0.59 | 0.52 |
| 0.25 | 0.36 | 0.51 | 0.44 |

The coast and desert rows read 0.3 to 0.8 ms below the WOR-463 table (coast zoom 1.5 was 1.43 there). The 1.5 ms ceiling stays, but it's no longer tight.

### Spike results (2026-11-01)

RTX 3090, RelWithDebInfo, 3072x1728, GPU held at P0. Each change measured alone against the base above. The spike branch is the local `kj/ground-tiles-spike`, kept for reference.

Where today's pass spends its time:

| Part | Cost |
|---|---|
| Atlas sample | -0.02 to +0.01 ms, so the pass isn't texture-cache bound |
| Water | 0.04 to 0.22 ms idle, about 0.3 on an open-ocean scroll |
| Land field | 0.29 to 0.76 ms, 60 to 90% of the pass |

What each change costs:

| Change | Result |
|---|---|
| Mipmapped array, one sample, CPU-computed LOD | within 0.02 ms of base everywhere; removes the tile-edge seam lines |
| Hex-tiling | +0.02 to +0.08 ms at zoom 1.5 and closer, +0.08 to +0.12 at zoom 0.75, zero at 0.5 and 0.25; worst cell 1.07 ms |
| Macro texture, GPU | +0.00 to +0.03 ms |
| Macro texture, worker fill | 3.0 to 5.2 ms a chunk, about 6 to 8% of the 56 ms chunk-generation median |
| Fringe samples once | -0.00 to -0.02 ms |
| Packed 4x4 neighborhood | bit-exact, no gain, 4.3 MB a chunk (137 MB over the 32-chunk LRU), 7 to 13 ms to build: dropped |

Reading it: the hypothesis that mip 0 of a 512 px/m texture thrashes the texture cache did not hold, so the array is free and not faster. The seams are fixed and the land field is where the pass's time goes. Hex ran with a 6 px/m cutoff, which is why zoom 0.75 pays and 0.5 doesn't; raising the cutoff to about 12 px/m is expected to remove the zoom 0.75 cost, and is not yet measured. Per-pixel fBm in place of the macro texture would cost 0.10 to 0.15 ms.

### Bake and macro fill (2026-11-01)

RelWithDebInfo, today's content: Grass and the eight placeholder defs, 40 variants. With the bake a variant at a time from `buildMeshes`, it added 20.6 s to the splash: Grass's 8 variants took 17.2 s, 2.15 s each, of which 1.9 s was the generator and tessellation, and each placeholder def 0.33 to 0.42 s. With the paths built once on the pool and the disk cache, measured on the game's loading scene (the same steps the splash runs): a cold cache takes 4.47 s of bake steps (4.52 s wall clock), Grass 2.5 s of it (paths 357 ms for all 8 at once, then render 151 and filter 117 a variant) and each placeholder 0.22 to 0.28 s. A warm one takes 36 ms (39 ms wall clock): Grass's 11 MB file reads in 14 ms, each placeholder's in 2, and every upload together takes 3 ms. So the 0.5 s rule holds whenever the cache is warm, and a clean build or an edited def pays the cold bake once.

The macro fill on the chunk worker is 3.0 ms median and 6.6 ms max over 27 chunks, against a 65 ms chunk generation median (`[ChunkGen]` reports it as `macro`). Hashing four corners a texel for each octave cost 12.5 ms; hashing each lattice corner once a chunk gives the same bytes.

## Testing

- Seams: world-sim on 8081, Grass ground, `camera` control at `zoom=20`, `x = n - 0.0034` (the case that lights every column on main). No tile lines. Repeat at `x = n + 0.003` and at zoom 1 and 3, and pan across a chunk border (a multiple of 512 m): no seam at the lattice or the chunk edge.
- Variety: screenshots at zoom 1, 3 and 20 on Grass, a GrassTall patch and a Dirt edge. No visible 1 m or 4 m repeat, and the fringe and rim still read at surface boundaries.
- LOD: zoom out step by step over GrassMeadow. Flower specks tagged `data-lod` fade near their threshold, with no muddy tint left behind and no popping.
- Asset-manager: the ground category lists all 9 land surfaces, the 3x3 preview shows no seam, an edit to a generator param followed by Reload shows the change, and validation is clean.
- Unit tests: validator rules, periodic noise wraps, `data-lod` inheritance and per-band filtering, a mesh over 65,535 vertices splits, paths built once match a generator run per density, a bake on the pool matches one in turn bit for bit, and the bake cache's round trip, key and damaged files. `SurfaceFieldGolden` and the existing suite pass.
- Performance: the final capture per the gates above.
- Manual script: `docs/testing/scenarios/ground-tiles.md`, one numbered line a step, written at hand-off.

## Open questions

- BC1 on upload: neutral or better, and does it look right on dense blade art.
- The hex cutoff at about 12 px/m: confirm it removes the zoom 0.75 cost and that the one-sample fallback shows no cut edges.
- The cold bake's render: the GPU time in salal's batched quad path, about 0.15 s a grass variant with today's art. A plain indexed-triangle draw for the baker is the next lever, if a cold bake (a clean build, an edited def) needs to get faster.
- Two arrays by density against one array with every layer at 512x512 (the rest would cost about 40 MiB more). The spec picks two.
- Per-asset shader snippets, to explore. A ground folder could ship a GLSL function beside its XML (for example `assets/world/ground/Grass/surface.frag.glsl`, defining that surface's color from position, the baked sample and the macro tint), and the loader would assemble every snippet into the tile shader at first run, with a switch on surface id. Today the tile shader is one program loaded from a folder, and salal assembles its base shader from hooks at build time, so this moves assembly to load time for the tile pass. Questions: compile time and whether to cache the program binary (`glGetProgramBinary`); validating a modder's GLSL and reporting errors through asset validation; the per-pixel cost being unbounded for shader content, where baked tiles cost the same however detailed they are, so it needs a budget and the perf gates; how it sits with baked variants (a snippet that only post-processes the baked sample is cheap, one that replaces it isn't); and the Rust and Vulkan rewrite, where it becomes SPIR-V.

## Later

- A per-chunk ground-params texture (planet `temperatureMean` and `precipitation` tint, coherent moisture to replace today's per-tile white noise, lusher near water from the distance field), and groundcover tufts tinted by a C++ twin of the macro field.
- Flora adopting `data-lod`.
- A salal ticket to drop `TileAtlasBuilder`, `TilePatternBaker` and `setTileAtlas` if nothing else uses them.
- Doc drift: `lua-scripting-api.md` documents functions that don't exist, and `variantIndex` is always 0.
- The entity pass, which owns the frame from zoom 0.75 out (section 6 of the terrain spec), is not touched here; findings from profiling go in as their own items.

## Related documentation

- [Terrain Polygons Architecture](./organic-terrain/terrain-polygons-architecture.md): D10 (water), D13 (surface order), D16 (the land field), section 6 (the perf method and tables)
- [Vector Graphics Architecture](./vector-graphics/INDEX.md): Tier 1/3 system
- [Visual Style](../design/visual-style.md): art direction
- [Entity Placement System](./entity-placement-system.md): flora and groundcover placement
- [Asset System](./asset-system/README.md): asset definitions
- [Testing](../testing/README.md): strategy and the performance rule

## Rimworld research sources

- [Rimworld-style tilemap shader (Godot)](https://godotshaders.com/shader/rimworld-style-tilemap-shader-with-tutorial-video/)
- [RimWorld Wiki - Mod Textures](https://rimworldwiki.com/wiki/Modding_Tutorials/Textures)
