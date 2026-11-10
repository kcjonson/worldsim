# Ground Texture System

Created: 2025-12-13
Updated: 2026-11-01 (ground tile variants: tiles become SVG/Lua assets baked to texture arrays and mixed by a hex lattice in the shader, and the atlas goes away; spike results and the perf plan added. Later: the bank, the hex sampler and the macro texture are in the tile pass, the atlas is deleted, and the surfaces without art paint placeholder defs. Then: the bake runs each generator once, on the pool, and keeps its bakes in a disk cache the splash reads. Then: the tile pass back under main in every cell, through a leaner sampler, a cheaper land field, no ground under open water and hex blending from 16 px/m. Then: round 1 of the real art for the eight surfaces that painted placeholders, the grass family from `ground_grass.lua` and Dirt, Sand, Mud, Rock and Snow from one new generator, `ground_bare.lua`; the bank reads its cached defs side by side and the encode keeps a small mip's mean. Then: surface transitions specified, design only: baked heights in the ground arrays' alpha and a height contest that replaces the fringe and rim, pair parameters in the defs, and the band capped at the field's reach. Then: the heights are baked (WOR-559), the mip window is fitted and kept over per-mip equalization, and the grass family bakes from one seed group. Then: the height contest is built (WOR-560): the fringe and rim are deleted, every pair's band and shading come from the defs, the band reads a distance that stays continuous out to the field's reach, the grass family hashes its hex cells alike, and the field does less work a pixel, bit for bit. Then: round 2 of the bare surfaces' art, Rock, Mud, Sand and Snow redrawn from new `ground_bare.lua` layers (regions cut along spectral-field contours, joints, lichen colonies, sky-tinted puddles, ripple fragments, wind streaks), and the pair recipes retuned on the real edges, the grass family's band down to 0.2 m (WOR-561). Then: review fixes, the pair table stored as a triangle of 36 pairs, the shader's whole-number bias kept out of every sum a reordering compiler could fold it into, and the golden test run over negative tiles. Then: tested by hand and merged to main, #332 (tile variants) carried by #333 (surface blending), with the draft-state wording rewritten to what shipped)
Status: Built and merged. Open items are in Specboard under WOR-583, Ground tile follow-ups.

The ground is painted by the tile pass: one quad per chunk whose fragment shader picks a land surface for every pixel, samples that surface's baked ground tiles, and paints water and shore over the result. Which surface a pixel gets is decided by the land field of [terrain-polygons-architecture.md](./organic-terrain/terrain-polygons-architecture.md) D16, so no surface boundary follows the tile grid. This doc covers the ground tile assets, the baker, how the shader samples them, and the per-chunk data the pass reads, and summarizes the field; the spec holds the field's details and defaults.

Each surface used to be one 512x512 pattern in a texture atlas, stamped on every 1 m tile. This doc describes what replaced it: a set of baked variants a surface, mixed by a hex lattice in the shader. The atlas is deleted. The sections on render tiles and the land field describe what stayed.

## Why change

Two problems with the old pass:

1. Faint lines on every tile edge that flickered while panning. `land.glsl` sampled each pattern at the point's offset inside its 1 m tile, from an atlas whose cells were packed edge to edge with linear filtering, no gutter and no mipmaps. Every tile boundary blended in half a texel of the neighboring atlas cell (Grass's right edge picked up Dirt, GrassTall's left edge picked up Mud). The camera isn't pixel-snapped, so the band came and went with the sub-pixel offset.
2. Repetition. The same pattern was stamped on every tile, so its soft ellipses read as a 1 m grid of spots, and the only variation was the surface id: Grass, GrassShort, GrassMeadow and GrassTall were flat greens that differed by tint.

The tiles are assets on disk, so they stay moddable, and use the procedural-tree approach (a Lua generator and seeds) to make many unique variants of each (fixed SVG variants are planned, see Ground tile assets). A baker rasterizes them once at load into mipmapped texture arrays, and the shader mixes variants through a hex lattice. The atlas (salal's `TileAtlasBuilder`, built by `GameBootstrap`) is gone, which removed problem 1 at its source; there was no separate stopgap.

Rollout: the tile variants shipped as #332 and the surface blending as #333, which carries #332's commits, and both were tested by hand before they merged. Salal needed no change, so there was no tag to wait on and worldsim stays pinned at v0.34.0. The `data-lod` loader below is the one piece that would need a salal addition, and it isn't built.

## Layers

| Layer | What | Drawn by |
|-------|------|----------|
| Ground | each surface's baked tile variants, mixed per pixel by a hex lattice, the surface chosen per pixel by the land field | tile pass: `tile.frag`, `includes/land.glsl` |
| Water and shore | from the chunk's terrain distance field (D10) | tile pass: `includes/water.glsl` |
| Grass tufts | `Groundcover_Grass`, a procedural Lua tuft, GPU-instanced | `GroundcoverRenderer` |
| Flora and other entities | vector assets | `EntityRenderer` |

This is the original plan's two tiers as they shipped: the ground tiles are the static micro-detail (Tier 1 in [the vector-graphics architecture](./vector-graphics/INDEX.md)), and the grass tufts and flora are live vectors over them.

## Ground tile assets

A ground tile is an asset with the role `ground`. Defs live at `assets/world/ground/<Surface>/<Surface>.xml`, under the scanned root, with the file stem matching the folder (the registry requires it). The asset-manager lists `ground/` as a category with no extra work. Its Vectors view draws each preview's still art once into a render target and shows that, since a tile is a few hundred thousand triangles and the view shows it fifteen times (the 3x3 tiling and six variants): a frame there uploads under 1 MB where drawing the meshes uploaded 334 MB.

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
  <transition>...</transition>
</asset>
```

| Field | Meaning |
|-------|---------|
| `<surface>` | the `world::Surface` it paints |
| `<assetType>` | `procedural` with a `<generator>`. `simple` with one or more `<svgPath>`s, each SVG being one fixed variant, is planned (it needs the `data-lod` loader below); validation rejects it for now |
| `<variantCount>` | variants baked, 1 to 32, default 8 (for `simple` the SVG list is the variant list) |
| `<period>` | meters one variant covers, default 4; it must divide 512 so it lines up across chunks. The only period: the generator reads it as the Lua global `period` (nil for other roles) |
| `<pixelsPerMeter>` | base texel density, default 64, so a 4 m variant is a 256x256 layer |
| `<rotate>` | `free`, `quarter`, `none` or `mirror`; `none` for art with baked top-left lighting, `mirror` (never turned, each cell flipped left to right or not) for art with an up, like grass blades |
| `<macro>` | two tint colors (dry, lush, each channel 0 to 2), plus the amplitude (0 to 1) and wavelengths of the low-frequency layer the shader mixes between them. Not part of the bake cache key |
| `<transition>` | how this surface meets the lower ones: band width, offset, softness and shading strengths, a default block plus one per lower neighbor (Surface transitions). Not part of the bake cache key |
| `<seedGroup>` | salts the bake's generator seeds: defs sharing a group get one seed for each variant index (the grass family's is `grass`). The def's name when left out |

Generators and SVGs draw in meters on a `[0, period]` square. Shapes may cross the edge, so authors never match edges by hand: `AssetRegistry::buildPaths` (and `buildMeshes`, a thin call over it) repeats every path of a procedural ground def that crosses an edge at the offsets it crosses into, right after the original. Paint order holds across the seam, and the meshes clipped to the square are the seamless tile. Drawing the whole mesh 3x3 instead would paint each copy's full-tile layers over the earlier copies' overhang, so the seams would show.

Validation (`AssetValidator.cpp`): exactly one ground def per land surface, `assetType` is `procedural`, `period` divides 512, `period` x `pixelsPerMeter` is a whole number of texels from 1 to 1024 and `variantCount` is 1 to 32 (`GroundLayer.h`, the same rules `GroundTileBaker::begin` and `GroundTileBank::begin` apply before they allocate or generate), the defs together use at most four layer sizes (`kMaxGroundArrays`, one texture array each), the role is known (`parseAssetRole`, `isKnownRole`), each `<macro>` child is the right count of finite numbers in range, and the `<transition>` rules of Surface transitions. The loader and the validator read `<transition>` through one function, `readGroundTransitions` (`GroundTransition.cpp`), and `<macro>` through another, `readGroundMacro` (`GroundTint.cpp`).

Lua gets two noise bindings (`LuaEngine.cpp`, `registerBindings`), both thin wrappers over salal's `foundation/random/HashNoise.h`: `noise2(x, y, seed)`, and `periodicNoise2(x, y, period, seed)`, which wraps the gradient lattice so density fields tile; a period within 1e-6 of a whole number counts as that number, since `P / F` lands a hair off one in floating point. `path:addVertex` raises a Lua error for a NaN or infinite coordinate. A Lua path also gets `path:setLod(N)` (see LOD below) and `path:setHeight(h)` (see Surface transitions).

`tessellateAsset` used to wrap its `uint16` indices silently past 65,535 vertices (`AssetRegistry.cpp`). It splits into several meshes now, the way `SVGLoader.cpp` guards. Dense ground tiles hit the limit, and so could flora.

| Surface | D13 level | Source | px/m |
|---------|-----------|--------|------|
| Mud | 1 | `ground_bare.lua` | 64 |
| Sand | 2 | `ground_bare.lua` | 64 |
| Dirt | 3 | `ground_bare.lua` | 64 |
| GrassShort | 4 | `ground_grass.lua` | 128 |
| Grass | 5 | `ground_grass.lua` | 128 |
| GrassMeadow | 6 | `ground_grass.lua` | 128 |
| GrassTall | 7 | `ground_grass.lua` | 128 |
| Rock | 8 | `ground_bare.lua` | 64 |
| Snow | 9 | `ground_bare.lua` | 64 |
| Water | not land | none, never sampled | |

A Water tile paints as its bed and water itself is drawn from the distance field, so Water has no ground tile. Both generators are in `assets/shared/scripts/`, and every def has 8 variants.

## Baker

`GroundTileBank` in `libs/world-view/world/rendering/` runs once the asset registry finishes its async load (the splash), on the GL thread, outside any frame.

For each def and each seed `0..variantCount-1` (salted per seed group, the def's name unless it names one) `GroundTileBaker` runs the generator once and tessellates each path once with `AssetRegistry::buildPaths` (already wrapped, see above), every variant at once on a `TaskPool`. Each generator run builds its own Lua state and the registry is only read, so the runs share nothing; the GL work stays on the GL thread. Each mip then packs the paths its density keeps into meshes with `TessellatedPaths::meshesAt` (a dense tile is over 65,535 vertices, so several meshes) and draws them once into a `Renderer::RenderTarget` the size of the square, which clips the overhang. It supersamples (8x8 for mips 0 and 1, 4x4 below, since salal has no MSAA), Kaiser-downsamples in linear light, mean-locks each mip to a filtered copy of mip 0 and encodes sRGB, dithered. A dither leaves each texel unbiased but not a small level's mean: on a 4x4 mip 6 one code of a dark channel is several percent of its light, so a dark surface missed the 0.5% gate on dither luck alone. A channel whose mean lands more than 0.05% off has its codes stepped one at a time, the texels that rounded furthest the wrong way first, until no step brings it closer (`encodeSrgb8`); a bake's mean error is now under 0.1% at every mip. Alpha is each texel's height (Surface transitions, "Heights in alpha"): before the variants render, `bakeHeights` draws every variant's paths once more at mip 0 in flat gray at their heights and ranks them over the def, and each mip's alpha is those heights filtered down by the same Kaiser downsample, copied in before the mean lock so it leaves them be. The coordinate system and viewport are restored afterwards. This is the mesh path flora uses, so gradients and colors match the asset-manager exactly.

Every mip level is rendered from the vectors, not generated (`glGenerateMipmap` would neither wrap nor drop LOD nodes); see LOD. The bank allocates each raw-GL `GL_TEXTURE_2D_ARRAY` level by level with `glTexImage3D` once it knows the layer counts, and writes each variant's chain in with `glTexSubImage3D` as soon as it's baked: `GL_SRGB8_ALPHA8` (so filtering and the hex blend run in linear light), `GL_REPEAT`, trilinear, `GL_TEXTURE_MAX_LEVEL` at the last baked mip. No anisotropic filtering; a top-down orthographic view has no anisotropy to filter.

The layers of one array share a size, and the grass surfaces (128 px/m, 512x512 layers) differ from the rest (64 px/m, 256x256), so there are two arrays, one per layer size; the tile shader has samplers for up to four, and a def needing a fifth is a validation error and a bake error. The bank keeps a per-surface table of `{array, firstLayer, layerCount, invPeriod, texelsPerMeter, rotate, macro tints and amplitude}`. The baker logs each def's report; the bank logs the totals (its steps' time and the wall clock, how many defs came from the cache) and the bytes of each array.

The baker works in steps, so a caller can spread a def over frames: `begin` looks the def up in the disk cache (below) and hands back the whole bake when it's there, `buildPaths` runs every variant's generator on the pool, `bakeHeights` draws and ranks every variant's heights on the GL thread, `bakeVariant` renders and filters one variant there, and `finish` measures the variants, logs the report and stores the bake. `bakeDef` runs them in order in one call; the asset-manager's Bake view runs them a frame at a time. The bank runs a step a call, so the splash keeps drawing between them: the first call looks every def up in the cache at once, on the pool, since reading the cached bakes is most of a warm bake; a call uploads every def in a row that the cache held, and at the first it didn't, builds that def's paths (the GL thread waits on the pool, about 0.4 s for Grass) and draws its heights (0.1 to 0.4 s); each later call bakes and uploads one variant, and the one that bakes a def's last variant stores the def. The splash's boot log shows `Baking ground tiles n/N` and the Enter button waits for the last. Every scene steps the bake through one function, `world_sim::stepGroundBake` (`GameBootstrap.cpp`), which blocks on the asset library's validation errors before it runs a step: a `--scene` jump skips the splash, so `GameLoadingScene` finishes whatever is left before it loads the planet, and a dev switch straight to the game scene runs the rest at once and goes to the splash if it fails. A failed or throwing step, a land surface with no def, a def past the GPU's largest texture or array layers, or an array GL can't allocate fails with its reason in the bank's errors, which block the splash the way a validation error does; the bake moves on, no step runs twice, and a def that failed isn't stored.

The bake goes well past 0.5 s, so each def's baked chains are cached on disk (`GroundTileCache`) in the game's per-user data directory, `app::userDataDir("kcjonson", "world-sim")/cache/ground`, which on Windows is `%LOCALAPPDATA%\kcjonson\world-sim\cache\ground`. world-sim and the asset-manager both use it (the two names are in `world/AppIdentity.h`, which each app's `AppConfig::organization` comes from too), and so does every build tree and config, so a bake either app stores warms the other, a clean build doesn't start cold, and a read-only install keeps its bakes. When salal can't name the directory (it logs why) there is no disk cache and every def bakes from its generator.

A file holds one def under one key, `<stem>-<hash>-<key>.gtile` (the name's letters, digits, `_` and `-`, anything else as `_`, then 8 hex digits of a hash of the exact name, so no two names share a file and none leaves the directory, then the key in 16), with every variant's mips, the samples the report measures, and a checksum. Builds that key a def differently keep their files side by side instead of overwriting each other's. Each store then evicts the least recently used files, oldest write time first (a load touches its file), until the directory is at or under 1 GiB; a full set of nine defs is about 60 MB. A store writes a temporary file named for its process and renames it into place, so two apps baking one def at once never hand a reader half a file, and a temporary file an hour old is a crashed writer's, which the next store deletes.

The key is a hash of everything the bake reads: the def as the registry parsed it at load, which is what the generator runs (its params, name, seed group, variant count, period, density and generator), its generator script as it is on disk (an `@shared/` one included; scripts are sandboxed and can't load others) with `LuaEngine::kApiVersion`, the render tile size, salal's version, and `GroundTileBaker::kVersion`. `kApiVersion` is bumped when a Lua binding or builtin changes what a script generates, and `kVersion` when worldsim's own bake code (the baker, its filters, `TessellatedPaths` and `buildPaths`) changes its output for the same inputs. Salal's version is `git describe --tags --always --dirty` of the checkout the build compiles against, the FetchContent tag or a sibling checkout (`FETCHCONTENT_SOURCE_DIR_SALAL`), written into a generated header by a step that runs every build (`cmake/salal-version.cmake`) and rewrites it only when it changes; a dirty checkout adds 12 hex digits of a SHA-1 of `git diff HEAD`, so two different uncommitted states never share a key (untracked files are in neither). A git that fails fails the build. Nothing the bake doesn't read is in the key, so tuning a pair or a macro tint, or editing a comment, never re-bakes (an edited pair loaded all nine defs from the cache in 31 ms). Editing a def's XML or script moves its key, so it misses (the log says there's no bake of it under its current key) and bakes again: in the asset-manager on Reload, in the game on its next launch. A file whose contents don't match its name, shape or checksum is ignored and the next store overwrites it.

On macOS `userDataDir` is under Application Support, which is meant for files that can't be made again. A salal `userCacheDir` (`~/Library/Caches`, `$XDG_CACHE_HOME`) would be the proper root for this cache once salal has one.

Ground_Grass (8 variants, 512 px layers, about 230,000 vertices a variant), RelWithDebInfo on a 32-thread machine, 2026-11-01, three cold runs and three from the cache:

| | Def | A variant |
|---|---|---|
| Before: `buildMeshes` a mip, variants in turn | 19,621 ms | mesh 1,823 (7 generator runs), render 491, filter 134 |
| Generate once, variants on the pool, cold | 5,700 to 5,830 ms | paths 420 to 455 ms for all 8 at once (generate 240 to 275, tessellate 150 to 160 each), render 520 to 535, filter 131 to 135 |
| From the cache | 19 to 23 ms | |

What's left cold is the render, and nearly all of it is the GPU finishing salal's batched draws, which `readPixels` waits on: about 150, 120 and 100 ms for the three dense mips (166k, 132k and 120k triangles), against 10 to 14 ms of CPU to build and submit them. Those runs predate the grass's calibrated LOD stacks; today's grass draws fewer triangles and bakes cold in about 2.5 s (Performance, "Bake and macro fill").

## LOD

Built: Lua `path:setLod(N)` and the baked mip chain it feeds. Not built: the SVG `data-lod` attribute below and the salal load-from-string addition it needs (Specboard WOR-583, Ground tile follow-ups). Every shipped def is procedural, so none needs them yet.

An SVG element, or a `<g>`, may carry `data-lod="N"`: draw it only where the screen shows at least N pixels per meter. The game's zoom runs 2 to 160 px/m, default 24. The attribute inherits, and an element's effective N is the largest on itself and its ancestors. An element without one is always drawn. A Lua path gets the same through `path:setLod(N)`, a new field on `GeneratedPath`.

The LOD is baked into the mip chain, so it costs nothing per frame. Mip k of a layer has `pixelsPerMeter / 2^k` texels per meter, and the GPU picks it when the screen shows about that many pixels per meter. The baker renders each mip from the vectors (wrapped, 2x supersampled at that level's size), including only the nodes whose N is at or below that level's density. Pebbles, flower specks and blade strokes drop out cleanly at the zoom the author chose, instead of averaging into a muddy tint, and trilinear filtering fades each one over an octave of zoom. Rendering every level costs about 1.33x the base render at load.

NanoSVG drops attributes it doesn't know, so the loader filters in XML first. A pugixml pass resolves each element's effective `data-lod` and writes one filtered SVG text per distinct band, and each text is then parsed. salal's `loadSVG` only takes a path, so this needs a small salal addition: load from a string. That is one salal PR and a tag bump, which the worldsim change would pin.

Flora could adopt the same convention later; it's out of scope here.

The shader has its own, separate LOD for the hex blend (the single-sample cutoff below).

## Shader

The tile pass (`tile.vert`, `tile.frag`, everything `land.glsl` includes) is GLSL 4.10 core. `textureGather` is core from 4.00, so no extension is needed, and 4.10 is what macOS caps OpenGL at. salal asks GLFW for a 3.3 core forward-compatible context. macOS answers a core request of 3.2 or later with its newest core profile (4.1). The NVIDIA driver on the reference machine reports 3.3.0 for it and compiles the 4.10 shaders anyway; a driver that holds GLSL to the context version would need salal's request raised to 4.1. The rest of the renderer stays at 330.

`land.glsl` loses `u_tileAtlas`, `u_tileAtlasRects`, `u_tileAtlasRectCount` and `atlasColor`. In their place are one `sampler2DArray` per texel density and per-surface uniform arrays (array, first layer, layer count, inverse period, rotate mode, macro tints), indexed the way `u_landWarpFine[10]` already is.

`groundColor(surface, local, mpp)` is stochastic hex-tiling (after Mikkelsen 2022, practical hex-tiling):

- The lattice is hexagonal in chunk-local meters, with 4 m columns and 512/148 m rows (3.459 m against 3.464 m for a regular hex, 0.15% off). It repeats every 512 m, so it is continuous across chunk borders and precise anywhere, and each cell's id is an integer, so it hashes exactly.
- Each cell picks a variant layer, a rotation (per `rotate`) and a uv offset with `noiseHash3` of its world id and the surface's seed group: the lowest surface id sharing the def's `<seedGroup>` (`GroundTileBank::Surface::hashSurface`, packed above the rotate bits of `u_groundLayers.w`). The grass family's members therefore pick the same variant, flip and offset in every cell, and their co-baked layouts meet across a family edge. The weights are barycentric, sharpened by a contrast exponent (`terrain/ground/blendContrast`) so flat-color shapes don't ghost.
- The weights are Mikkelsen's: barycentric to the power `terrain/ground/blendContrast` (7), times 0.4 + 0.6 x the sample's baked height (alpha; Mikkelsen weighs by luminance, and the height keeps tuft tops across cell seams and saves three dot products), then his gain with r from `terrain/ground/blendGain`: 0.75 at 48 px/m and up, 0.65 midway in log2 px/m, 0.5 (none) at 16 and under. The CPU turns r into the gain's exponent once a frame.
- Where one cell dominates (its sharpened weight, before the luminance term, over 0.98: 58% of the area at contrast 7), the shader takes one sample. Only the transition bands between cells take 2 or 3, 1.48 samples a pixel on average. Every pixel takes its heaviest cell's sample from the same instructions and only the bands branch for more, so a warp straddling a band edge runs that sample once.
- Below a density (`terrain/ground/hexMinPxPerMeter`, 16) it skips blending and takes the dominant cell's sample, the way `u_landWarpFineOctaves` drops warp octaves a pixel can't show. Cell interiors are the same sample either side of the cutoff, so crossing it only hardens the bands between cells. The spike ran with 6 px/m and the first build with 12. At 12 px/m (zoom 1.5) the one-sample ground matched the blend to the eye in screenshots, the hard cell edges a few levels of 255 at most in speckled art, and it saves 0.05 to 0.10 ms there, so the blend starts at 16, where the gain ramp starts.
- Variants are picked uniformly per cell, in float on the hash's low 12 bits: the same pick as `h % count` for every power-of-two count, within 1/4096 of uniform otherwise. Weighted picks (mostly quiet, a few busy) wait for a def that asks for them.
- The sampler stays off the GPU's conversion unit, which runs at an eighth of the float rate and is what the pass is short of: cell ids stay floats and cross to int through a 1.5 x 2^23 bias, and the CPU folds the chunk's first cell id, the variant pick's constants, and the macro tint and value into per-draw uniforms. The bias is exact only in source order, so it never meets a sum a compiler could reorder it through: the variant pick rounds a floor, a cell's uv offset and angle come from a mantissa in [1, 2) (`byteFraction`), and the field's two whole numbers that meet a fraction (the noise's offset in its cell and the 4x4 block's gather corner) convert with `float()` (`hash_noise.glsl` has the reasoning; Perf ledger row 10).
- Samples use `textureLod` at a mip level computed once per draw on the CPU for each surface, `log2(texels per meter / framebuffer pixels per meter) + terrain/ground/lodBias` (0.25, research rule 9). The view is an orthographic top-down projection, so texel density is the same at every pixel and rotation doesn't change it. That avoids `textureGrad` and per-pixel derivatives, and nothing jumps at tile or hex edges.
- The samples come back linear (the arrays are sRGB) with the height in alpha, are blended linear, and `landColor` encodes the result to sRGB once, which water works in. The macro tint multiplies the sRGB color (`groundTint`).

The macro layer comes from a per-chunk texture, not per-pixel noise. The chunk's generation worker fills a 2 m-per-texel texture (`GroundMacro.h`: 257x257 RGBA8, 264 KB a chunk, uploaded with the render tiles under the same version and LRU) from salal `HashNoise` gradient noise, each lattice corner hashed once a chunk. Texel (i, j) sits on chunk-local (2i, 2j) m, so the last row and column are the next chunk's first and a bilinear read matches across the border. R is the dry to lush mix (octaves at 36 and 14 m), G and B value noise at 10 m and 5 m (5 m is the shortest wave 2 m texels carry without a lattice pattern). The shader takes one bilinear sample a pixel: each surface mixes its def's dry and lush tints by R, scaled by the def's amplitude times `terrain/ground/macroAmp`, and every surface's value moves by G and B times `macroMidAmp` (4.5%) and `macroFineAmp` (3.5%). The def's `<wavelengths>` aren't read: one texture serves every surface. Per-pixel fBm is what cost about 30 ms in the December tile shader (`docs/debug-swapbuffers-performance.md`) and still costs 0.10 to 0.15 ms of GPU at the spike's scale. The same texture is where phase 2's climate and moisture data lands, and its C++ source can later tint groundcover tufts to match.

At a surface edge, the band of Surface transitions samples both surfaces through `groundTexture`, hex-blended, so the contest compares hex-blended heights and no pattern cut shows where the band meets the interior. Dominant-cell single taps inside the band were tried and dropped: a hex seam crossing a wide band shows as a hard cut through the blades at zoom 20.

`landColor` samples the painted surface and, where a pixel lies in a band, the pair's other one, then runs the contest (Surface transitions). Of `evaluateLand` only the distance it reports for the edge look changed (`leadDistance`, Band cap); painted surface, field, margin and warp are bit for bit what they were. `SurfaceFieldGolden.test.cpp` holds the field to its C++ twin as before and the distance too since the contest (Testing has what it covers).

Under open water the ground isn't evaluated at all. `tile.frag` asks `water.glsl` for the pixel's water coverage first (`waterPixel`: the distance field, its wobble, the coverage) and calls `landColor` only where some ground shows; where the coverage is 1 the water mixes the ground out entirely, so the picture is the same.

What a ground pixel costs, against main:

| | Main | New |
|---|---|---|
| Render-tile fetch | 1 | 1 |
| Ground samples, interior | 1, mip 0 of a 512 px/m pattern | 1 where a cell dominates, 2 or 3 in blend bands, mipmapped |
| Macro | none | 1 bilinear sample |
| Per-pixel noise | none | none (one hash per hex cell) |
| Surface edges | 2 samples | in the band 2 to 6 (both surfaces hex-blended), outside it 1 |
| Under open water | the field and 1 sample | nothing |

Deleted with the atlas: `GameBootstrap::initializeTileAtlas`, `surfacePatternPath`, `assets/tiles/surfaces/`, and the now-dead `Chunk::getSurfaceColor` and `getBiomeColor`.

## Art direction

The look matches the flora: layered flat-color shapes in the palette family of `grass.lua`, seen from a hair off top-down. It is dense, much denser than RimWorld's flat dirt with 1 px grass wisps, and less detailed than the photos it's drawn from. The ground is lit from the top of the screen: what stands, stands up the screen, lit edges face up and shadows fall down. That holds under `mirror`, which flips a cell left to right, and not under a turn, so every def is `mirror`. LOD carries each surface across zooms: fine detail near (blades, grain, sparkle), clumps and lumps mid, tonal patches far, and mips 5 and 6 close to the tile mean, which the per-chunk macro tint then moves. No feature reaches 1.3 m: the largest are the regions' shapes, contours of a field with no wave under 3 cycles a tile (1.33 m), and puddles of 8 to 24 cm.

Two generators paint the nine surfaces, each def differing only in `<params>`, which `Grass.xml`, `Dirt.xml` and `Rock.xml` document:

- `ground_grass.lua`, the grass family: a far stack drawn from a tone model of the layout under a near stack of clumped tuft fans, undergrowth, blades, clover, rosettes, soil peeks and litter (see its header). Wildflowers are its one addition, for GrassMeadow: heads in blue-noise clusters and singles, each on a proxy disc in its average color kept to mip 3, drawn over everything and painted into the tone model. The far stack's mean is `farMean` and its contrast `farContrast` and `farCoarseContrast`, set from the bake report.
- `ground_bare.lua`, the unvegetated surfaces: the ground (base, blob tone patches, and regions) is laid out first and painted into a 6.25 cm tone grid that every ground-born layer above takes its color from (grain, crests, cracks, lumps, shadows), so they keep the ground's contrast at every mip, and the grid is scaled so each variant averages to `baseColor`. Mips 4 to 6 are that grid averaged into one rect a mip 4 texel, with puddles and lichen averaged in, under a near ground that covers it from mip 3; `farContrast`, `farCoarseContrast` and `farGain` set their contrast and mean from the bake report. Layers:
  - tone patches (Dirt and Mud): blue-noise blobs in nested cel steps;
  - regions (Rock, Sand, Snow): nested cel steps cut along the contours of a spectral field, a sum of waves with whole cycles across the tile drawn from an annulus of 3 cycles and up, stretched along x as a def asks (`periodicNoise2` has one period for both axes, so it can't stretch). Contours come from marching squares on a 128-node grid, clipped to the tile, so a shape reaching an edge closes along it and the next tile's copy carries it on; a level's loops are one polygon filled even-odd. Each level takes a share of the tile's area, so every variant gives it the same, and with a bevel a rise is lit along its upper edge and shaded along its lower one (a hollow the other way), two thin bands that cancel in linear light and stay out of the tone grid, so however far a level's outline runs it leaves the mean be;
  - joints and fractures (Rock): long wandering walks with side fractures, each a dark crack with a lit lip on its lower wall (it faces up the screen, into the light), inside a band of weathering stain the tone grid carries, so major joints stay faint dark lines in the far mips;
  - ripple fragments (Sand): crests in rows bending together, broken by a field into pieces of a few to a few tens of centimeters and gathered into fields with smooth sand between, each a lit strip over a shadowed lee as wide as cancels it, over a half step twice as wide that survives to mip 2;
  - wind streaks (Sand): thin lenses along the wind in light and dark pairs;
  - cracks with a lit lip (Dirt), paired light and dark grain, pits;
  - lichen colonies (Rock): ragged clusters of rosettes, each a lighter rim around its body with dark fruiting dots, most of them on a joint, kept to the mip where the colony is a texel across so it fades as a tint;
  - puddles (Mud): flat water in a sky-tinted blue-grey with a light lip along its far edge, in a glossy, darker wet apron, each at the lowest of several spots on a broad field, so they gather in groups;
  - lumps, then loose pieces (roots, straw, leaves, twigs, shells, stones on proxies, Rock's grit gathering in the joints) back to front, grass tips and sparkle.

| Surface | Look | Tile mean, sRGB | Macro: dry, lush, amplitude |
|---|---|---|---|
| Grass | dense clumped sward with clover, rosettes and earth peeks (unchanged) | 80.2, 105.9, 48.6 | (1, 0.94, 0.72), (0.86, 0.96, 0.86), 0 |
| GrassShort | dry short turf: an olive sward dotted with tussocks of short yellow-green tufts, more straw, dry tips and soil peeks, about 12% open ground | 93.1, 105.4, 52.0 | (1.06, 1.02, 0.86), (0.94, 0.98, 1.1), 0.4 |
| GrassMeadow | lush, saturated sward with more clover, under 3% open ground, and white, yellow, violet and pink flower heads 3 to 5 cm across, a near-zoom accent | 77.2, 115.0, 48.6 | (1.04, 1, 0.9), (0.96, 1, 1.08), 0.3 |
| GrassTall | long, dark, bluer grass: bigger clumps of taller tufts, long flopping blades, a share with tan seed-head tips, under 3% open ground | 65.0, 92.0, 40.5 | (1.05, 1.02, 0.88), (0.95, 0.98, 1.1), 0.3 |
| Dirt | packed soil as a dense crumble of clods lit from the top, faint damp and dry mottling, speckled grain, small stones, fine cracks with a lit lip, a few dead grass bits, roots and twigs | 107.4, 84.0, 64.1 | (1.08, 1.05, 1), (0.92, 0.95, 1), 0.5 |
| Sand | pale grain under short fragments of wind ripples, in fields with smooth sand between, wind-combed streaks, faint dune tones stretched across the screen, mixed mineral grains, a few pebbles, shell bits and dry twigs | 202.5, 175.7, 131.0 | (1.04, 1.02, 0.96), (0.96, 0.98, 1.04), 0.4 |
| Mud | dark wet ground, densely churned lumps with wet gloss dabs, small puddles in loose groups (flat water holding a blue-grey sky, a light lip along the far edge, a glossy dark wet apron), stones, twigs, leaves and soggy straw | 78.4, 63.5, 50.1 | (1.08, 1.06, 1.02), (0.92, 0.94, 0.98), 0.4 |
| Rock | weathered grey stone in broad, irregular tones with a few low ledges, a handful of long wandering joints and finer fractures (a dark crack, a lit lower lip, a band of stain), solution pits, mineral speckle, sparse ragged colonies of crustose lichen in muted grey-green, yellow-green, ochre and black that favor the joints, grit in the joints | 122.2, 117.3, 108.9 | (1.04, 1.02, 0.98), (0.96, 0.98, 1.02), 0.4 |
| Snow | quiet, wind-packed snow: faintly brighter mounds and cool blue hollows stretched by the wind, soft drifts lit along their upper edges over a blue lee, dense faint grain, ice sparkle at mip 0 only, the odd twig, stone or grass tip | 225.0, 230.7, 237.9 | (1.02, 1.01, 0.98), (0.98, 0.99, 1.02), 0.3 |

The tile mean is mip 0's, over the 8 variants; each macro's dry and lush average to about 1, so the mean is also the surface's average in game. The grass family's are from the co-bake (Surface transitions), each within 1 to 2% of round 1's (80.6, 106.8, 48.8; 93.1, 105.3, 51.9; 77.8, 116.0, 48.9; 65.4, 93.0, 40.9). Dirt's is round 1's, and it bakes bit for bit as it did. Round 2 (WOR-561) kept the other bare surfaces within about a level of round 1's (Sand 202.0, 176.0, 131.3; Mud 78.0, 63.4, 49.7; Rock 121.9, 117.0, 107.8; Snow 224.9, 231.0, 238.3), Rock's blue the furthest at +1.1. The grass family is 128 px/m and the rest 64: the warm bake reads every cached def at load (56 MiB of them at these densities) and 128 for all nine wouldn't fit its budget, and the bare surfaces are built from features of 1.5 cm and up, so they stay legible magnified 2.5 times at zoom 20.

Rotation: the grass family is `mirror` because blades stand up the screen; Dirt, Mud, Rock and Snow because their relief is lit from the top (clod caps and shadows, region and joint edges, pits, puddle lips); Sand and Snow also because their ripples, streaks and wind-stretched tones run across the screen, which a turn would point every way.

Blending risks, each surface's notes from round 1 with what round 2 changed. They feed the pair recipes in Surface transitions, where the height contest draws the edge:

- The grass family reads as one family: any pair's boundary is a change in sward, not a change of material. Short is olive-yellow and lighter, Tall darker and bluer, Meadow more saturated, and the far mips keep them apart by mean alone. Meadow's flower heads stand high, so they reach into a band as pale specks, which is fine.
- GrassShort shows Dirt-like earth between its tufts (ground #5C4D33), so a Dirt to GrassShort edge reads naturally hard or soft. Dirt to Grass is a bigger step (Grass's earth is darker), which a soft edge suits. Clods are 2.5 to 8 cm, so an edge through them doesn't show.
- Sand is the brightest land surface after Snow, about 2.5 times Grass's light, so a Sand to grass edge wants a soft, broken band. Round 1's long ripple rows broke at hex cell borders (a vertical seam through them at zoom 20) and streaked into a rock outcrop's edge, since their crests ranked highest and the band followed them across. Round 2's ripples are short fragments, so a cell border reads as one more break, and their crests stand only a little above the ground, under its broad rises and hollows, so a band follows those and the ripples only break its edge up.
- Mud's puddles are its glossiest features. A transition edge clips them, and a half-transparent reflection on grass looks wrong. So Mud is the stain pair: the upper surface darkens near the edge instead of the mud fading, and the puddles sit at height 0, below every other texel, so the contest keeps them out of the band. Round 1's dark puddles read as black stones and looked like the stain; round 2's are small, flat and sky-tinted, lighter than the mud, so they read as water and nothing like the stain's darkening. Mud is darker than any grass, so a Mud to grass bank reads as wet ground.
- Rock's stone is high ground and its joints and fractures the lowest texels on it, so soil, sand and snow fill the joints and hollows first and the tops poke through (rock's warp amplitudes are already smaller). Round 1's Voronoi slabs read as crazy paving; round 2 has no slabs. Rock to Snow is the strongest pair, about 1.9 times the light, and the tundra paints it constantly.
- Snow's heights are one flat tie group, so in a band the neighbor's heights alone decide where it lies: with a crisp window it fills the neighbor's low spots and leaves its tops, which reads as snow lying on the ground. Round 1's wide, soft, tinted recipe read as a foggy halo; round 2's is narrower, crisp and untinted, with a contact shadow and a ridge for a lip. Its sparkle is mip 0 only, so it never shows in a far band.

## Render tiles

A chunk's generation worker builds its render tiles (`Chunk::computeRenderData`, with `paintSurfaces` and `buildRenderTiles` from `SurfaceField.cpp`): 518x518 two-byte tiles covering the chunk square plus `kRenderApronTiles` = 3 on every side, uploaded as one RG8UI texture.

- R, the paint surface: the tile's final surface, or for a Water tile its bed, the surface of the nearest land tile within 3 tiles (Sand when there is none).
- G, the edge byte: the edge surface in the low nibble (the highest surface within 2 tiles, whose warp amplitudes the tile takes), the interior bit, 0x80, set when every tile within 3 is one surface, and the near-interior bit, 0x40, set when every tile within 2 is.

Apron tiles come from the chunk's own apron, post-processed the way the neighbor post-processes its own tiles (D11), so both sides of a border read the same tiles and draw the same field. `ChunkRenderer` keeps an LRU cache of 32 chunks' textures and re-uploads a chunk's when its `renderDataVersion` changes. Render tiles cost 524 KB a chunk; the RGBA32UI tile data they replaced, with neighbor ids and edge masks, was 4 MB. The macro texture joins them with the same key.

## The land field

A summary; D16 in the spec is the definition.

1. For each surface among the 4x4 tiles around a point, its indicator at tile centers is blurred with the 3x3 binomial, guarded so 1-tile patches and 1-wide paths survive (the axis rule, floor 0.85, ceiling 0.15), and read bilinearly between tile centers.
2. Every surface is read at one warped point: world-space vector fBm, a fine term (4 m base, 4 octaves) plus a low term (26 m base, 2 octaves), with amplitudes by surface blended from the tiles' edge surfaces (rock's are smaller, so its edges stay harder), each component clamped to 1.5 m.
3. Surfaces paint low to high (D13): the highest whose field passes 0.5, else the one with the largest field.
4. The edge look is the height contest of Surface transitions: in a band around the isoline, capped at the field's reach, the pair's two surfaces are mixed by their baked heights, the signed distance and the fine warp noise. It replaced the sparse fringe, the rim and the 1 px anti-aliasing line. A pixel in an interior tile costs one render-tile fetch, one macro sample and one ground sample.
5. In a near-interior tile the fine warp moves the read at most its amplitude from where the low warp puts it, so after the low noise the field checks whether that range keeps the read's 4x4 block inside the tile's one-surface reach; when it does the surface is the tile's, with field 1, and the fine noise and the block are skipped. Failing that, it checks again once the fine term's first octave is in: each octave's gradient noise stays within [-1, 1], so the other three move the read at most the amplitude times their share of the weight (0.54 at the default gain), around where the first octave puts it. That share counts every later octave, so it holds for a far-zoom picture that sums fewer. Surface, field and margin are unchanged (`SurfaceFieldTest.NearInteriorShortcutMatchesTheFullField`, which also runs a 1 m fine amplitude where only the second check can pass; `WarpNoiseOctaveStaysWithinOne` holds the bound); only the reported warp is zero there. The fine term is summed an octave at a time in `fractalNoise2SplitPair`'s order, on both sides, so its sums are that function's bits.

`world::evaluateSurfaceField` is the one evaluation. `land.glsl` does the same steps in the same order, and `SurfaceFieldGolden.test.cpp` renders the shader and compares the two. Placement asks the C++ evaluation, so flora grows on the surface that is drawn.

## Surface transitions

Status: built. The heights in alpha are WOR-559; the contest, the pair parameters, the shading and the band cap are WOR-560. The recipes were retuned on the real edges in round 2 (WOR-561), Recipes below.

Where two land surfaces meet, a band around the field's isoline shows whichever surface stands taller at each texel. The ground arrays' alpha carries a baked height. The contest compares the two surfaces' heights, biased by the signed distance to the isoline, so grass tufts stand over dirt, sand fills the joints in rock, snow settles in hollows and rock tops poke through. It replaced the fringe, its breakup noise, the rim and the 1 px anti-aliasing line, and the band's second surface takes the samples the fringe's did, hex-blended now.

The field is untouched: D13 levels, the warp, the block and the paint rule, and the C++ twin. So the contest is look-only (D1), placement still agrees with the painted surface, and inside the band the visible surface can differ from the painted one by at most the band width.

The edges it is tuned on come from chunk generation, not the field. A biome boundary was a 16 m staircase (one biome per 16 x 16 m sector), which no band can hide; it is picked per tile now, with bays, headlands and islands (terrain spec, D16, biome edges; WOR-558). The same edges about doubled the waterline and nav ring vertices on coasts and lakes. That was measured against the sector pick (WOR-563, terrain spec section 6): no frame cost, since water draws from the distance field; on the worker a coast chunk generates in 113 ms against 86, mostly the longer shore's polygons and bake, and its water-only nav regions build in 0.3 ms against 0.1. The 100 mm ring tolerance stays: the extra vertices are 1 m tile steps the blur rounds but keeps, and a looser tolerance thins the shore profiles first.

### Heights in alpha

- **Source.** Lua gets `path:setHeight(h)`, h in [0, 1] (outside it is a script error), a `GeneratedPath` field beside `minPixelsPerMeter`. A ground path without one takes its paint-order rank, (i + 1) / n for the i-th of n paths, set before the wrap so a copy across the edge keeps its original's; a texel no path covers is 0.
- **Hand-set surfaces.** Every path of a layer shares a height or takes one from its size, never from where it sits: the depth-sorted layers (tufts and blades, clods, pieces) would otherwise rise down the tile with their paint order. No height draws a random number, so the bare surfaces' colors are unchanged. The grass family (`ground_grass.lua`): soil and gaps 0.03 to 0.06, litter, pebbles and rosettes 0.1 to 0.23, undergrowth 0.28 to 0.36 by length, clover 0.36, tufts 0.5 to 0.68 by length, blades 0.45 to 0.55 at the base and 0.72 to 0.92 at the tip by length, flower heads 0.88 to 0.96. The bare surfaces (`ground_bare.lua`, params in `Dirt.xml`): the ground at `heightBase` (0.3), its relief `heightRelief` (1) times a fixed bump (patches +-0.06 and tints +-0.03 by step, regions each level's `Relief` with its bevel bands halfway, ripple crests +0.02 over a lee -0.02 and their half step +-0.01, streaks +-0.02, crack lips +0.02 and cracks -0.2, joint stains -0.08, lips +0.01 and cracks -0.45, light grain +0.06 and dark -0.06, pits -0.12 and -0.18, lichen +0.03 and +0.04), and what lies on it a fixed step above `heightBase` (clods 0.15 to 0.25 by size with caps 0.12 higher, straw, leaves, shells and twigs 0.25 to 0.32, stones 0.35 to 0.45 by size, grass tips 0.35). Puddle wet rings are `heightPuddleRim` (half `heightBase`) and the water 0. Round 2 (WOR-561) set them per surface: Rock's `heightBase` is 0.75, so its stone is high ground, its rises higher (+0.05 and +0.1, ledges +0.08) and its hollows lower (-0.05 and -0.12), and its joints and fractures cut to the bottom; Sand's regions rise and fall +-0.04 and +-0.08, more than its ripples, so a band follows the broad shapes and the ripple rows only break its edge up (round 1's crests at +0.1 ranked highest and streaked into a rock edge); Mud's puddles stay the lowest texels; Snow sets 0.5 with relief 0, one flat height out of which only twigs, stones and grass tips stand. Dirt keeps the defaults.
- **Baker.** One render a variant, at mip 0, draws each path in flat gray at its height, keeping its fill's alpha so a translucent shadow blends heights the way it blends color, at 2 x 2 supersampling (1024 px for the grass family, 512 for the rest). A 2 x 2 box brings it to texels, and a Gaussian of half a texel blurs it so the contour a threshold traces isn't jagged. The mips aren't drawn again: both generators' far stacks (mips 4 to 6) are a tone model with no heights to speak of, so each mip's heights are mip 0's filtered down by the color's Kaiser downsample, which keeps their mean (The mip window below compares that against equalizing each mip).
- **Rank normalization.** Per surface, across its variants, each height is replaced by its rank, a uniform histogram: a counting sort into 65,536 bins, each bin a tie group that takes the middle of its ranks plus blue noise times the lesser of 0.03 and half the group's span. Distinct heights keep their order and only flat areas dither, by at most +-0.03. The noise is one tileable tile (white noise less its own blur of sigma 1 texel, ranked to -1..1) that each variant reads at its own wrapped offset. Uniform heights make A's coverage along the band linear in the distance, so one (width, softness) pair behaves alike on every surface; the tie-break stops a flat area flipping at a single distance as a hard step. Only the order within a surface survives, so a flat surface sits at the middle of its tie group, and "low" or "high" is set by how much of its area lies below and above that group: Snow's mip 0 heights have a standard deviation of 0.05 against 0.29 for every other surface.
- **Filter.** Alpha stops meaning coverage. `downsampleSrgb8` (was `downsampleSrgb8Premultiplied`), the mean lock and `encodeSrgb8` treat RGB as opaque and alpha as plain data, with no premultiply and no dither; colors bake as before (round 1's tile means come back to 0.05 of a level). The opacity warning in `GroundTileBaker` is gone, `GroundTileBaker::kVersion` is 3 and the cache file format 2 (each mip's sample keeps its height spread where the minimum alpha was). `GL_SRGB8_ALPHA8` stores alpha linear, so trilinear filtering and the hex blend average heights linearly.
- **What the shader reads.** Alpha is round(255 h), linear and undithered. Mip 0 spans 0 to 1 with a uniform histogram (standard deviation 0.29) but for tie groups; coarser mips are filtered averages whose spread shrinks, for Grass 0.27, 0.24, 0.18, 0.12, 0.07 and 0.04 at mips 1 to 6 and for Dirt 0.26, 0.21, 0.14, 0.09, 0.06 and 0.04 (the report's `h sd` column).
- **Grass family co-bake.** Grass, GrassShort, GrassMeadow and GrassTall name `<seedGroup>grass</seedGroup>`, so variant k of each starts from one seed, and every layout stage, clump, tuft, blade and plant reseeds from it by a fixed id (`math.randomseed(seed, id)`, the id scrambled) before it draws. Clumps come from one candidate set at 0.12 m that each member thins, in candidate order, to its own `clumpSpacing`; a tuft draws its blades' arrival times from its own stream and the def's `nTuft` earliest win, so the count stays exact; loose blades are a candidate list, the first n kept. A member keeps a subset of the shared positions and restyles them (lengths, colors, shares). Mip 0 heights of the same variant correlate 0.35 to 0.54 between Grass, GrassMeadow and GrassTall and 0.10 to 0.14 with GrassShort, whose clumps are spaced wider, against 0.00 between different variants. The variant spread came down (dE 0.28 to 0.37 against 0.30 to 0.45), `farMean` and the far contrast were set again from the bake, and the far mips 5 and 6 miss the 70% contrast floor by a point or two in their lowest variant. The hex hash picks alike for every member: `groundCellSample` hashes the seed group's lowest surface id (`layers.w >> 2`, `GroundTileBank::Surface::hashSurface`) into each cell's variant, mirror and offset (Shader), so the co-baked layouts line up across a family edge.
- **Budget.** Measured on the splash, RelWithDebInfo, 32 threads: a cold bake takes 20.2 s of bake steps (20.24 s wall clock) against round 1's 18.3 s, +10%, of which the heights are 2.3 s (0.36 to 0.44 s a grass def, 0.1 to 0.2 s each other), nearly all their eight renders. A warm one takes 32 ms of steps (38 ms wall clock), unchanged: the files keep their size. The arrays stay 56.0 MiB.

### The contest

For a pixel in the band, A is the pair's upper surface by D13 and B the lower. `landColor` swaps painted and across into that order, so both sides of the isoline evaluate one expression.

```
b     = clamp((sigma + o) / w, -1, 1)
n     = 0.5 + 0.5 * along
D     = (hA - hB) + K * b + nu * (n - 0.5)
K     = 1 + W + nu / 2
s     = saturate(D / (2 * W) + 0.5)
color = mix(cB, cA, s)
```

| Term | Unit | Meaning |
|------|------|---------|
| `sigma` | m | signed distance to the painted boundary between the pair (the field's `distance`, from `leadDistance`, Band cap), positive into A. Read-space meters, so the warp's local stretch varies the on-screen width along the edge |
| `w` | m | band half-width |
| `o` | m | offset. The band spans sigma in [-w - o, w - o]; positive means A spills over B, negative means B spills over A. `abs(o)` stays under `w` |
| `hA`, `hB` | 0..1 | baked heights: the alpha of each surface's (hex-blended) sample |
| `W` | height | window half-width, how far apart two heights must be to decide. Small is a crisp cut that follows the height pattern, large degrades toward a plain cross-fade. At least the pair's softness `W0` (mip window below) |
| `nu` | height | breakup, the peak-to-peak amplitude of the along-edge noise |
| `along` | -1..1 | the fine warp noise's x, `e.along`, which the field already computes. The breakup costs no noise of its own |
| `K` | height | slope of b in D. `1 + W + nu/2` makes s exactly 0 at b = -1 and 1 at b = 1 for any heights in [0, 1] |
| `s` | 0..1 | weight of A |

The support is compact: at |b| = 1, s is exactly 0 or 1, so outside the band a pixel takes one ground sample, the one s selects, and nothing pops at the band's edge. The validator holds |o| under w, so the surface outside the band is always the painted one: `landColor` works out b first and samples the other surface only where 0 < (b + 1) / 2 < 1.

The colors mix in linear light, the way the hex blend mixes, and are encoded once (the single-encode A/B, kept). The macro tints multiply the sRGB result, each surface's mixed by the same weight the colors are.

The CPU folds everything per pair and per draw into two vec4 (`GroundPairs.h`, `packGroundPairs`). The first holds 0.5 / w and 0.5 + 0.5 o / w, so (b + 1) / 2 is one saturated multiply-add of sigma, then 1 / (2W) and nu / (4W), so s is `saturate((hA - hB) / (2W) + b (1 / (2W) + nu / (4W) + 0.5) + along nu / (4W) + 0.5)`, with W this draw's mip window; the second holds the four shading strengths. The shader divides nothing. `ChunkRenderer::applyGroundUniforms` uploads the 72 vec4 once a frame from the table `GroundTileBank` builds from the defs at load.

`nu` starts at 0.5 everywhere and is tuned until the band's width swings 30 to 40% along the edge, the D15 rule that no along-edge parameter is constant.

### The mip window

Trilinear filtering averages heights. At coarse mips their spread collapses and s(b) turns into a hard step at the mean-difference line: a crisp, shimmering line where the pixel's footprint should show a coverage ramp. The window widens with the mip instead:

```
mip = max(mipA, mipB)
W   = max(W0, mip / 6)
```

`mipA` and `mipB` are the per-surface mip levels the CPU already picks once per draw (`log2(texels per meter / pixels per meter) + lodBias`), so the window is folded into the pair table on the CPU. No `fwidth`, no derivatives, no helper-lane dependency.

The spec started from a ramp to a cap, `Wc * saturate(mip / mipC)`. Fitted, **`Wc` 1.0 and `mipC` 6**; but the fit only pins the slope, about 0.17 a mip: any `mipC` past the last mip with `Wc` scaled to match gives the same error, and the spec's starting 0.6 and 2.5 cost 12% more. So the code keeps the slope alone (`kLandWindowPerMip`, `GroundPairs.h`), with no saturate, and W runs a little past 1 at the chain's far end: 1.04 at mip 6.25, the grass family's 128 px/m at zoom 0.25 with the lod bias in. A cap at 1 isn't worth adding: from about 1 the window spans every height difference, so the contest is a plain cross-fade either way, and mip 6's heights spread only 0.04. `w` is also floored at one framebuffer pixel so the band never narrows below the line it replaced at far zoom, but no further than the Band cap allows (w plus |o| at most 1.5 m), where the field stops reporting the edge and a tinted band would cut off; the fit doesn't include that floor. In game at zoom 0.5 every pair's edge is a soft one or two pixel ramp with no crisp line.

Per-mip equalization (each mip's heights rank-equalized at bake time, Castano's alpha-mip method generalized from one threshold to all, `W` staying `W0`) was A/B'd against the window and dropped. The reference is the contest on mip 0's 8-bit heights at 256 samples a meter, box-averaged to each screen pixel; a candidate is the contest on one trilinear sample a pixel at the runtime's LOD (bias 0.25). Six pairs with their starting recipes' `w`, `o` and `W0` (Grass over Dirt, Rock over Sand, Snow over Rock, GrassShort over Dirt, Grass over Mud, Grass over GrassShort), `nu` 0.5 with `along` two sines down a straight 4 m edge, three variant pairs and offsets each. Coverage error is the RMS of candidate minus reference over the band's pixels, the profile error the RMS of their along-edge means, and the band width the 10 to 90% span of that mean:

| px/m | mip | Equalized, `W0` | Filtered, `W0` | Filtered, 0.6 / 2.5 | Filtered, 1.0 / 6 | Profile, equalized / 1.0 / 6 | Width, reference / equalized / 1.0 / 6 (m) |
|---|---|---|---|---|---|---|---|
| 128 | 0.17 | 0.029 | 0.029 | 0.029 | 0.029 | 0.002 / 0.003 | 0.55 / 0.54 / 0.54 |
| 64 | 0.92 | 0.067 | 0.068 | 0.064 | 0.062 | 0.008 / 0.015 | 0.55 / 0.53 / 0.49 |
| 32 | 1.92 | 0.114 | 0.101 | 0.089 | 0.083 | 0.016 / 0.026 | 0.56 / 0.50 / 0.42 |
| 16 | 2.92 | 0.138 | 0.126 | 0.095 | 0.091 | 0.027 / 0.030 | 0.56 / 0.46 / 0.41 |
| 8 | 3.92 | 0.144 | 0.129 | 0.081 | 0.078 | 0.039 / 0.026 | 0.58 / 0.44 / 0.46 |
| 4 | 4.92 | 0.147 | 0.124 | 0.068 | 0.065 | 0.048 / 0.030 | 0.63 / 0.47 / 0.54 |
| 2 | 5.75 | 0.154 | 0.128 | 0.054 | 0.044 | 0.053 / 0.024 | 0.84 / 0.73 / 0.79 |

Summed over the zooms, the squared error is 0.103 equalized, 0.036 at the starting window and 0.032 fitted. Equalized mips keep the spread, so a minified band speckles pixel by pixel, at 1.4 to 3.5 times the window's error from mip 2 down; they hold the along-edge mean a little closer at mips 1 to 3. Both narrow the band's 10 to 90% width at mips 1 to 3 (the window to 0.41 m against the reference's 0.56), which the contest step watches for. Every pair agrees except Grass over Mud at 4 and 2 px/m, where `W0` 0.2 already covers the mip term and the two tie.

### Pair parameters

A pair's parameters live in the upper surface's def. They resolve at load into a table of the 36 pairs whose upper D13 level is above the lower, stored as a triangle at `upper * (upper - 1) / 2 + lower` (`landPairIndex`, `buildGroundPairTable`), which each draw packs into a uniform array of 72 vec4 (a uniform array is fine on NVIDIA, unlike a run-time-indexed const array). The square table of 81 it replaced took 648 of the 1024 fragment uniform components GL 4.1 guarantees, for 36 pairs that were ever read; the fragment shader's uniforms now come to about 550. One formula serves every pair, with no branch per kind: a strength of 0 multiplies by 1.

```xml
<!-- Rock.xml: how Rock meets what lies below it. -->
<transition>                        <!-- default: Rock over any lower surface -->
  <width>0.5</width>
  <offset>0</offset>
  <softness>0.05</softness>
  <breakup>0.5</breakup>
  <shadow>0.25</shadow>
</transition>
<transition over="Sand">            <!-- override: fields left out keep the default -->
  <width>0.5</width>
  <offset>-0.15</offset>
  <softness>0.04</softness>
  <ridge>0.06</ridge>
</transition>
```

| Field | Unit | Range | Meaning |
|-------|------|-------|---------|
| `<width>` | m | 0.05 to 1.4 | `w`, the band half-width. `width + abs(offset)` is capped (Band cap) |
| `<offset>` | m | -w to w, exclusive | `o`. Positive: this surface spills over the lower one. Negative: the lower one spills over this one |
| `<softness>` | height | 0.03 to 0.3 | `W0`, the window with no mip term. Under 0.03 an 8-bit height steps visibly when magnified |
| `<breakup>` | height | 0 to 0.8 | `nu` |
| `<shadow>` | | 0 to 1 | strength of the contact shadow |
| `<stain>` | | 0 to 1 | strength of the stain |
| `<ridge>` | | 0 to 1 | strength of the ridge lightening |
| `<tint>` | | 0 to 1 | strength of the tint toward the upper surface |

A def with no `<transition>` gets width 0.4, offset 0, softness 0.1, breakup 0.5, and no shading. `over` names a surface lower in D13 than the def's own. An override is checked as resolved, its fields over the default block's. The validator rejects an `over` that isn't a lower land surface, a width plus offset past the cap, an offset not strictly inside the width, a field that isn't a number in its range, and a second default or a second block over one surface; it warns on unknown elements. The loader runs the same rules (`readGroundTransitions`) and drops a block that breaks one, so its pair keeps the default block. Nothing registers per-pair tunables, so editing a pair takes a restart, which doesn't re-bake (the blocks are outside the cache key).

### Shading terms

Four terms, the same for every pair, each scaled by its strength. Every one has no effect at |b| = 1, so compact support holds.

```
cA     = cA * (1 - stain * (1 - smoothstep(0, 1, b)))              before the mix
color  = mix(cB, cA, s)
color  = mix(color, cA, tint * (1 - s) * saturate(1 + b))
color  = color * (1 - shadow * (1 - s) * saturate(1 + 4 * b))
color  = color * (1 + ridge * saturate(1 - 5 * abs(b - 0.8)))
```

| Term | Where | Does |
|------|-------|------|
| shadow | the lower surface under the upper's edge, down to b = -0.25, scaled by 1 - s so it vanishes where the lower surface does | darkens; replaces the rim |
| stain | the upper surface near the edge | darkens it toward the edge, mud wetting its neighbor |
| ridge | a line at b = 0.8, near the upper surface's outer reach | lightens, a sand drift line |
| tint | the lower surface, falling off toward b = -1 | veils it with the upper's color, snow's cool cast |

The shapes are starting values, tuned by eye with the recipes. They run in linear light, before the one encode, so a strength darkens more than the same number would on the sRGB color: shadow 0.25 takes a quarter of the light, about 12% of the sRGB value. The tint folds into A's weight, `a = s + tint (1 - s) saturate(1 + b)`, and the macro tints mix by `a` too.

### Band cap

The field reports a boundary only within its reach. A surface's field is its tile indicator blurred by the 3 x 3 binomial and read bilinearly between tile centers. One tile of blur plus half a tile of bilinear footprint from the nearest tile of another surface (the "bilinear read spans the next tile center, and the blur one tile more" of `RenderTiles.h`), it is exactly 1 with a zero gradient. There the interior and near-interior bits return field 1 with no boundary (`across == painted`), and on the full path a field of 1 reads 1.5 m (below), the outer edge of any band the cap allows, so the pixel takes one sample. The reach constants:

| Constant | Value | Where |
|----------|-------|-------|
| `kMaxLandWarpM` | 1.5 m a warp component | `SurfaceField.h` |
| `kRenderApronTiles` | 3 | `RenderTiles.h` |
| `kInteriorReachTiles` | 3 | `SurfaceField.h` |
| `kNearInteriorReachTiles`, `kEdgeReachTiles` | 2 | `SurfaceField.h` |
| `kBedReachTiles` | 3 | `SurfaceField.h` |
| `kRenderSurfaceReachTiles` | 9 | `SurfaceField.h` |

**Cap: `width + abs(offset)` at most 1.5 m**, read-space meters from the isoline (0.5 m of bilinear footprint plus 1 m of blur; the warp clamp is also 1.5 m, but that bounds how far the read moves, not how far the field extends). The band's outermost pixel sits at |sigma| = width + |offset|; a band reaching past 1.5 m would still be partial where the field stops reporting, a visible cut. The recipes keep 0.1 m in hand (1.4 m). Widening the reach means a bigger render apron, interior reaches and `kApronTiles`, which costs every view, and is out of scope.

The distance, measured. `sigma` was `(F - 0.5) / |grad F|` of the bilinear cell the read falls in. Plotted offline against the true distance to the isoline (the field rebuilt from tile masks, no warp; scratch script, not kept): on a straight edge it is exact to 0.5 m and then jumps by 0.51 m, to 1.0, because the next cell's slope is half; on a diagonal it jumps by up to 3.7 m and on a convex corner by 2.5 m where the field flattens; at a concave notch and along a 1-wide path the gradient vanishes at the saddle and the estimate runs to the 1e4 clamp. In game the grass over Dirt band (0.65 m reach) showed it at zoom 20 as a straight cut through a dirt patch half a meter inside its edge.

So `sigma` comes from the fields alone (`leadDistance` in `land.glsl`): `4u - 2 clamp(u, -0.25, 0.25)`, with u the painted surface's lead over the surface that would take over, which is `F - 0.5` between two surfaces. Across a straight tile edge F runs from 0.5 to 0.75 over the first half meter and half as steep to 1 at 1.5 m, and this inverts that, so it is exact on a straight edge all the way to the cap, reaches 1.5 m exactly where F does 0 or 1, and is continuous everywhere (the largest step in the plot is the 1 cm sampling). Elsewhere it is close: within 0.04 m on a diagonal out to 1 m (1.21 at a true 1.26), 0.79 at a true 0.97 outside a convex corner, 0.66 at a true 0.48 across a 1-wide path. The cut is gone in game, and the widest bands of the starting recipes (the grass family's 1.4 m, Snow's 1.2 + 0.2) show no step at their outer edge at zoom 20. It is also cheaper: no gradient, no inverse square root per surface. The field the golden test checks (painted, field, margin, warp) is bit for bit unchanged; only `distance` reads differently, and only the edge look reads it. The cap stays 1.5 m.

Where three or more surfaces meet, u isn't any one surface's `F - 0.5`. Where nothing passes 0.5 the largest field paints, past every surface's own isoline, so a distance to an isoline doesn't vanish on the painted boundary, jumps across it, and has ridges where a field peaks just under 0.5. In the desert, where Grass and GrassShort mix tile by tile against Sand, the band ran along such a ridge out in the sand: a sliver of bare sand between the grass and a detached line of tufts and contact shadow, up to half a meter off the edge at notches and tight bends. So u is the lead the paint rule gives. The painted P beats a lower C while P passes or leads it, `u = max((fP - fC) / 2, fP - 0.5)`, and a higher C while C doesn't pass and P leads it, `u = min((fP - fC) / 2, 0.5 - fC)`; on each side the largest field is the nearest, and where nothing passes both come to half the gap to the second largest field. u is 0 exactly on the painted boundary and continuous across it, and the two sides name each other there. Between two surfaces both forms read `F - 0.5`, so every two-surface edge draws as before, and |u| stays within 0.5, so the cap holds. Where the surface with the nearest boundary changes, at a junction, the band's pair changes with it. `SurfaceFieldGoldenTest.BandDistanceMeetsAtThePaintedBoundary` renders the distance over the golden test's regions: on the two sides of each painted boundary it reads at most 0.25 m (two pixels at the test's step; 0.22 at worst), where the isoline distance read up to 0.53.

### Recipes

Rows are keyed the way they read on screen. The record column says where they live: the D13 upper surface's def, with `over` naming the lower. Sand over Rock is stored in Rock's def with a negative offset, because Rock paints above Sand. Tuned by eye in game at zoom 20, 3 and 1.5, and checked at 0.5 for shimmer, on round 2's art (WOR-561); the last column says what changed from the starting values, which came from the transition research trimmed to the cap.

| On screen | Record | w, m | o, m | W0 | Extras | From the start |
|-----------|--------|------|------|----|--------|----------------|
| Grass family over Dirt | each grass def, `over="Dirt"` | 0.5 | +0.15 | 0.06 | shadow 0.25; tufts spill over the dirt | unchanged |
| Grass family over Sand | each grass def, `over="Sand"` | 0.3 | +0.1 | 0.05 | shadow 0.12, a faint contact shadow on the bright sand | shadow was 0 |
| Grass family meeting Rock | Rock, default | 0.5 | 0 | 0.05 | shadow 0.25 (the old rim's job); tops high, grass lives in the joints | unchanged |
| Sand over Rock | Rock, `over="Sand"` | 0.5 | -0.15 | 0.04 | ridge 0.06; sand fills the joints and hollows first, tops stay as islands | w 0.7, o -0.2 |
| Dirt over Rock | Rock, `over="Dirt"` | 0.3 | 0 | 0.04 | like sand, narrower; shadow 0 | unchanged |
| Sand over Dirt | Dirt, `over="Sand"` | 0.3 | 0 | 0.10 | soft, no shadow | unchanged |
| Snow over anything | Snow, default | 0.8 | +0.25 | 0.04 | shadow 0.35 and ridge 0.08 for a lip, breakup 0.6, no tint; flat, mid height, so it lies in the neighbor's low spots | w 1.2, o +0.2, W0 0.15, tint 0.25, no shadow: a foggy halo |
| Anything over Mud | each def, `over="Mud"` | 0.25 | 0 | 0.20 | stain 0.25 | unchanged |
| Within the grass family | each member, `over=` the lower members | 0.2 | 0 | 0.15 | soft, no shadow, no ridge | w 1.4 |

The family band went from 1.4 m through 0.7 and 0.35 to 0.2: the co-baked layouts and the shared hex hash put the same blades in the same places in every member, so even at 0.2 m an edge reads as a change of sward with no line and no cut through a blade, at zoom 20 down to 1.5, where the field's warp is most of what a boundary's softness comes from. It's the widest band most of the grass shows and the cheapest change on the pass (Perf ledger). Snow's crisp window is safe at far zoom because the mip window still widens with the mip: at zoom 0.5 its edge is a soft one or two pixel ramp.

As shipped, the grass defs name every pair (no default block); Rock's default block is the grass recipe and its Sand, Dirt and Mud overrides set shadow 0; Dirt and Sand name theirs; Snow's default is its recipe and its Mud override takes the Mud recipe (offset 0, no ridge or shadow), since anything over Mud stains. Each def's lead comment says what its blocks do. In game: tufts break over dirt with a faint shadow under them and over sand with a fainter one; sand creeps into a rock outcrop's joints and hollows as ragged lobes, without the horizontal streaks round 1's ripple crests drew; snow lies crisp on rock, filling its low spots first, with a dark contact line and a bright lip and no veil; grass darkens toward a mud bank while the puddles stay inside the mud; and a grass family edge is a soft change of sward. The hand-off screenshots of each pair at zoom 20, 3, 1.5 and 0.5 went with the PRs.

### Perf ledger

**Deleted**

- The fringe's 2-octave breakup noise; `along` replaces it free. The 1 px anti-aliasing mix and the band's second sRGB encode.
- `u_landFringeW[10]`, `u_landRimDark[10]`, `u_landFringeOpacity`, `u_landFringeAlongAmp`, `u_landRimW`, `u_landBreakupWavelength`, `u_landBreakupInvWavelength` and `u_landBreakupSeed`, their `kShaderTunables` entries, the `terrain/land/fringe/*`, `rimDark/*`, `fringeOpacity`, `fringeAlongAmp`, `rimW` and `fringeBreakupWavelength` tunables, and their defaults and seed in `ChunkRenderer.cpp`.
- The field's gradient and its inverse square root a surface (`leadDistance`).
- The baker's opacity warning and the premultiplied treatment of ground alpha (WOR-559).
- `NoiseUtils` and its value noise, replaced by gradient patch noise hashed once a chunk (`generation::PatchNoise`, WOR-558).

**Added**

- Per band pixel: two uniform vec4 reads, the contest and the shading terms (a dozen or two ALU, no divide), the other surface through the hex sampler, and its macro tint.
- Per frame: the 72-vec4 pair table, packed with the zoom's mip window on the CPU.
- Bake: the height pass, 2.3 s cold (+10%) and nothing warm (WOR-559). Editing a `<transition>` never re-bakes.
- Memory: none.

**A/B, desert view.** One capture each (the noise floor is about 0.01 ms), each against the step before it; `tileGpuMs` in ms at idle zoom 20, 8, 3, 1.5, 0.75, 0.5, 0.25 and scroll 3, 0.75. The tip before blending read 0.65, 0.73, 0.63, 0.54, 0.48, 0.48, 0.42, 0.22, 0.68.

| # | Change | Delta per cell | Kept |
|---|---|---|---|
| 1 | The contest in place of the fringe, its breakup and the rim | 0, 0, 0, 0, -0.02, -0.02, -0.04, 0, -0.02 | yes |
| 2 | `sigma` from F (Band cap) | +0.02, +0.01, +0.01, 0, +0.01, 0, 0, 0, +0.01: the per-pixel cost drops, but the bands are now as wide as their recipes; the gradient's step had cut the wide ones short | yes, it is the fix |
| 3 | The band's second surface through the painted one's hex lattice and weights, and its cell taps where the pair hashes alike | -0.01, 0, 0, +0.01, +0.01, +0.01, +0.01, +0.01, +0.01 | no |
| 4 | The field off the conversion unit, bit for bit: the noise's gradient decode, cell floors and base through the 1.5 x 2^23 bias, the warped lattice read and the pixel's tile likewise | -0.01, 0, 0, -0.01, -0.01, 0, 0, 0, 0 | yes |
| 5 | The 4x4 block's paint ids gathered a 2x2 quad at a time (`textureGather`, core from GLSL 4.00): 4 texture instructions for 16, bit for bit | -0.01, -0.01, -0.01, 0, 0, 0, 0, 0, -0.01 | yes |
| 6 | Linear mix with one encode, and hex weights from the height instead of luminance; each alone is under the noise floor, so measured together | 0, 0, 0, 0, 0, -0.01, 0, -0.01, 0 | yes |
| 7 | Dominant-cell single taps in the band | not captured: a hex seam crossing a family band showed a hard cut through the blades at zoom 20 | no |
| 8 | The field's per-surface work, bit for bit: in a two-surface block the second surface's sums, indicator and thin bits are the first's complement (16 less, inverse, same), the thin guard a select, and the noise's floor division one unsigned divide | -0.01, -0.01, -0.01, -0.01, -0.01, 0, -0.01, 0, -0.01 | yes |
| 9 | The band's distance from the paint rule's lead, 0 on the painted boundary where three surfaces meet (Band cap), against the GLSL 4.10 tip (`capture-2026-11-02-bandfix-desert-1`) | 0, +0.01, 0, 0, 0, -0.01, 0, -0.01, -0.01; river and coast within 0.01 of the tip in every cell, and the tip's shader on the same build read river the same as the fix | yes |
| 10 | The pair table as a triangle of 36 pairs, 72 vec4 in place of 162 (`capture-2026-11-02-review-tipshader-desert-1`) | 0, 0, 0, 0, 0, 0, 0, 0, +0.01 | yes |
| 11 | `precise` on what the whole-number bias meets in a sum (the noise's offset in its cell, the gather corner, the variant pick, a cell's uv offset and angle), against row 10 | +0.05, +0.05, +0.04, +0.04, +0.03, +0.04, +0.03, +0.02, +0.05: the NVIDIA compiler drops fused multiply-adds well past the marked values, which also brought the field ten times closer to the C++ (worst warp difference 1.2e-7 m against 1.4e-6) | no |
| 12 | The same values made safe by construction, against row 10: `float()` for the field's two whole numbers that meet a fraction, a floor under the variant pick's rounding, a cell's uv offset and angle from a mantissa in [1, 2) (`capture-2026-11-02-review-desert-1`) | 0, 0, 0, 0, 0, 0, 0, 0, 0; the same values, and the golden test's worst differences the tip's to three digits | yes |
| 13 | The band's second surface on the painted surface's hex cells and sharpened weights (row 3 without its cell taps), against row 12 | +0.02, +0.02, +0.01, +0.01, +0.01, +0.01, +0.01, +0.01, +0.01, and +0.01 to +0.02 against row 10 too; bit for bit in screenshots, but every cell pays, hex-off zooms included | no |

Every bit-for-bit change was checked by hashing the golden test's GLSL output over its three regions before and after (identical hashes; the probe isn't kept). Rows 10 to 13 are one capture each in one sitting, the tip's shader re-read beside them, so desert zoom 3 stays where the tip had it, 0.58 against main's 0.56. A temporary debug mask (not kept) put the desert view's zoom 3 cost at about 0.13 ms in the 4x4 block and its per-surface loop, 0.09 in the painted surface's sampler (0.05 of it hex blending) and 0.04 in the band, and NVIDIA's program binary text showed the per-surface loop and the six noise octaves as mostly integer work, which is where 4, 5 and 8 went. Halving the grass family's band (1.4 to 0.7 m), probed at the four cameras below, saves 0.01 at desert zoom 8, 3 and 1.5 and 0.02 at coast zoom 3; round 2 took 0.2 m (Recipes, and the round 2 capture below).

**Final,** two runs per view against main's two (`perf-results/capture-2026-11-02-contest-final-*` against `perf-results/capture-2026-11-01-ground-main-*`, every sample at P0), `tileGpuMs` in ms, the tip column the branch before blending:

| Scenario | View | main r1 / r2 | tip | final r1 / r2 | final - main |
|---|---|---|---|---|---|
| idle zoom=20 | river | 0.74 / 0.74 | 0.57 | 0.54 / 0.55 | -0.19 |
| idle zoom=20 | coast | 0.73 / 0.73 | 0.30 | 0.29 / 0.30 | -0.43 |
| idle zoom=20 | desert | 0.81 / 0.82 | 0.65 | 0.65 / 0.65 | -0.16 |
| idle zoom=8 | river | 0.97 / 0.98 | 0.71 | 0.68 / 0.69 | -0.29 |
| idle zoom=8 | coast | 0.60 / 0.60 | 0.55 | 0.55 / 0.55 | -0.05 |
| idle zoom=8 | desert | 0.72 / 0.72 | 0.73 | 0.72 / 0.72 | +0.00 |
| idle zoom=3 | river | 0.66 / 0.67 | 0.50 | 0.48 / 0.49 | -0.18 |
| idle zoom=3 | coast | 0.66 / 0.66 | 0.68 | 0.67 / 0.67 | +0.01 |
| idle zoom=3 | desert | 0.56 / 0.56 | 0.63 | 0.62 / 0.62 | +0.06 |
| idle zoom=1.5 | river | 0.57 / 0.57 | 0.44 | 0.42 / 0.43 | -0.14 |
| idle zoom=1.5 | coast | 0.64 / 0.64 | 0.60 | 0.58 / 0.58 | -0.06 |
| idle zoom=1.5 | desert | 0.51 / 0.52 | 0.54 | 0.53 / 0.52 | +0.01 |
| idle zoom=0.75 | river | 0.46 / 0.46 | 0.40 | 0.37 / 0.38 | -0.09 |
| idle zoom=0.75 | coast | 0.59 / 0.59 | 0.56 | 0.53 / 0.53 | -0.06 |
| idle zoom=0.75 | desert | 0.50 / 0.50 | 0.48 | 0.46 / 0.45 | -0.04 |
| idle zoom=0.5 | river | 0.44 / 0.44 | 0.39 | 0.36 / 0.37 | -0.08 |
| idle zoom=0.5 | coast | 0.59 / 0.59 | 0.56 | 0.53 / 0.53 | -0.06 |
| idle zoom=0.5 | desert | 0.52 / 0.52 | 0.48 | 0.45 / 0.45 | -0.07 |
| idle zoom=0.25 | river | 0.36 / 0.35 | 0.34 | 0.31 / 0.31 | -0.04 |
| idle zoom=0.25 | coast | 0.50 / 0.51 | 0.46 | 0.42 / 0.42 | -0.09 |
| idle zoom=0.25 | desert | 0.43 / 0.44 | 0.42 | 0.37 / 0.37 | -0.07 |
| scroll zoom=3 | river | 0.41 / 0.41 | 0.37 | 0.37 / 0.37 | -0.04 |
| scroll zoom=3 | coast | 0.33 / 0.33 | 0.31 | 0.31 / 0.31 | -0.02 |
| scroll zoom=3 | desert | 0.22 / 0.23 | 0.22 | 0.21 / 0.21 | -0.02 |
| scroll zoom=0.75 | river | 0.39 / 0.37 | 0.37 | 0.36 / 0.35 | -0.03 |
| scroll zoom=0.75 | coast | 0.33 / 0.33 | 0.31 | 0.31 / 0.31 | -0.02 |
| scroll zoom=0.75 | desert | 0.74 / 0.74 | 0.68 | 0.65 / 0.66 | -0.08 |

24 of 27 cells are at or under main; the median is -0.06. Coast zoom 3 reads +0.01 and desert zoom 1.5 +0.01, both at the noise floor (desert zoom 1.5's two runs read 0.53 and 0.52 against main's 0.51 and 0.52); desert zoom 8 is level. Desert zoom 3 is +0.06 and stays over: the tip read +0.07 there before any of this, from WOR-558's organic biome edges, which put many more pixels of that view on the field's full path (the noise, the block and the per-surface loop), and the contest, at the fringe's cost, doesn't change that. The remaining levers are the field's six noise octaves, which must stay bit for bit with the C++ twin, and the family band's width. Everything is under 1.5 ms. Frame p50 and p99 and scene `gpuMs` match main and the tip within their run-to-run swing; the river view from zoom 0.75 out is the entity pass (1 to 3 ms between runs of one build), and coast scroll 3's p99 (66 against 62) and the far zooms' p50 were the tip's too, from the chunk content.

**Round 2** (WOR-561), the desert view, two runs (`perf-results/capture-2026-11-02-art2-desert-*`, every sample at P0) against the final above and main, `tileGpuMs` in ms. The shader is unchanged; the only GPU change is band width, mostly the grass family's 0.2 m against 1.4:

| Scenario | main r1 / r2 | final r1 / r2 | round 2 r1 / r2 | round 2 - final | round 2 - main |
|---|---|---|---|---|---|
| idle zoom=20 | 0.81 / 0.82 | 0.65 / 0.65 | 0.62 / 0.62 | -0.03 | -0.19 |
| idle zoom=8 | 0.72 / 0.72 | 0.72 / 0.72 | 0.69 / 0.69 | -0.03 | -0.03 |
| idle zoom=3 | 0.56 / 0.56 | 0.62 / 0.62 | 0.60 / 0.60 | -0.02 | +0.04 |
| idle zoom=1.5 | 0.51 / 0.52 | 0.53 / 0.52 | 0.51 / 0.51 | -0.02 | -0.01 |
| idle zoom=0.75 | 0.50 / 0.50 | 0.46 / 0.45 | 0.44 / 0.44 | -0.02 | -0.06 |
| idle zoom=0.5 | 0.52 / 0.52 | 0.45 / 0.45 | 0.44 / 0.44 | -0.01 | -0.08 |
| idle zoom=0.25 | 0.43 / 0.44 | 0.37 / 0.37 | 0.36 / 0.37 | -0.01 | -0.07 |
| scroll zoom=3 | 0.22 / 0.23 | 0.21 / 0.21 | 0.21 / 0.21 | +0.00 | -0.02 |
| scroll zoom=0.75 | 0.74 / 0.74 | 0.65 / 0.66 | 0.63 / 0.63 | -0.03 | -0.11 |

8 of 9 cells read 0.01 to 0.03 under the final, the zoom 3 scroll level; desert zoom 3, the one cell over main, is +0.04 where it was +0.06. Scene `gpuMs` and frame p50 and p99 match the final within their run-to-run swing.

**The near-interior test after the first fine octave** (WOR-564, The land field step 5). Where desert zoom 3's cost sits, from temporary probe shaders that colored each pixel by its path (not kept): 36% of the view's pixels are interior, 11% take the near-interior shortcut and 52% the full path. 38% of all pixels read a block of more than one surface, mostly Grass and GrassShort mixed tile by tile at the organic biome edges, so they need the full path whatever the shortcut does. The other 14% read a one-surface block the shortcut couldn't prove, in rings a few pixels wide between the near-interior tiles and those edges. A calibration build that returned one surface after the low noise for every non-interior pixel (wrong output) read 0.27 ms against 0.60, so the fine noise, the block, the loop and the band are 0.33 ms of the view; but a fragment warp saves only when all its pixels exit, and counted by 8 x 4 pixel block a test after the first fine octave could free 7.8% of the warps, after the second 3.0% more, after the third 2.1% and at the exact read 2.7%, while 40% hold an edge pixel.

Screening, one camera (the desert view at zoom 8, 3 and 1.5, 9 samples each, every one at P0), not full captures. The block bit is a render-tile bit tried and dropped (0x10: the 4x4 block at the tile's lattice cell is one surface); the range test asks, for a read range from a bound, the block bit of the one lattice cell it spans, else the near-interior bit (two cells an axis) or the interior bit (three) of the tile at its second cell:

| Variant | zoom 8 / 3 / 1.5 |
|---|---|
| Round 2 | 0.69 / 0.60 / 0.51 |
| The range test, from the low-warped bound, in every non-interior tile in place of the near-interior test | 0.70 / 0.61 / 0.51 |
| That, and the range test after fine octaves 0 and 1 | 0.69 / 0.60 / 0.51 |
| The near-interior test, and the range test after octaves 0 and 1 | 0.68 / 0.59 / 0.49 |
| The near-interior test, and the range test after every octave | 0.70 / 0.61 / 0.52 |
| The near-interior test, and the range test after octave 0 | 0.67 / 0.58 / 0.49 |
| The near-interior test, and it again after octave 0 (kept) | 0.68 / 0.58 / 0.49 |
| The near-interior test, and it again after octaves 0 and 1 | 0.69 / 0.58 / 0.49 |

Every test costs each pixel that reaches it, and most of those sit in warps with an edge pixel, so a second test after the first octave cost what its exits saved, and reaching past the tile's own bit won nothing measurable. The kept test needs no new bits, so the chunk worker is untouched.

Final, two desert runs (`perf-results/capture-2026-11-02-interior-oct0-desert-*`, every sample at P0), `tileGpuMs` in ms, differences of the means:

| Scenario | main r1 / r2 | round 2 r1 / r2 | final r1 / r2 | final - round 2 | final - main |
|---|---|---|---|---|---|
| idle zoom=20 | 0.81 / 0.82 | 0.62 / 0.62 | 0.60 / 0.60 | -0.020 | -0.215 |
| idle zoom=8 | 0.72 / 0.72 | 0.69 / 0.69 | 0.67 / 0.68 | -0.015 | -0.045 |
| idle zoom=3 | 0.56 / 0.56 | 0.60 / 0.60 | 0.57 / 0.58 | -0.025 | +0.015 |
| idle zoom=1.5 | 0.51 / 0.52 | 0.51 / 0.51 | 0.49 / 0.49 | -0.020 | -0.025 |
| idle zoom=0.75 | 0.50 / 0.50 | 0.44 / 0.44 | 0.43 / 0.43 | -0.010 | -0.070 |
| idle zoom=0.5 | 0.52 / 0.52 | 0.44 / 0.44 | 0.42 / 0.43 | -0.015 | -0.095 |
| idle zoom=0.25 | 0.43 / 0.44 | 0.36 / 0.37 | 0.37 / 0.37 | +0.005 | -0.065 |
| scroll zoom=3 | 0.22 / 0.23 | 0.21 / 0.21 | 0.20 / 0.20 | -0.010 | -0.025 |
| scroll zoom=0.75 | 0.74 / 0.74 | 0.63 / 0.63 | 0.60 / 0.60 | -0.030 | -0.140 |

River and coast, one run each (`...-interior-oct0-river-1`, `...-coast-1`, every sample at P0) against main's two:

| Scenario | river main r1 / r2 | river final | coast main r1 / r2 | coast final |
|---|---|---|---|---|
| idle zoom=20 | 0.74 / 0.74 | 0.54 | 0.73 / 0.73 | 0.27 |
| idle zoom=8 | 0.97 / 0.98 | 0.68 | 0.60 / 0.60 | 0.51 |
| idle zoom=3 | 0.66 / 0.67 | 0.48 | 0.66 / 0.66 | 0.62 |
| idle zoom=1.5 | 0.57 / 0.57 | 0.41 | 0.64 / 0.64 | 0.54 |
| idle zoom=0.75 | 0.46 / 0.46 | 0.36 | 0.59 / 0.59 | 0.51 |
| idle zoom=0.5 | 0.44 / 0.44 | 0.35 | 0.59 / 0.59 | 0.51 |
| idle zoom=0.25 | 0.36 / 0.35 | 0.31 | 0.50 / 0.51 | 0.42 |
| scroll zoom=3 | 0.41 / 0.41 | 0.35 | 0.33 / 0.33 | 0.31 |
| scroll zoom=0.75 | 0.39 / 0.37 | 0.33 | 0.33 / 0.33 | 0.31 |

Desert zoom 3 reads 0.57 and 0.58 against main's 0.56 and 0.56, +0.01 to +0.02 at the noise floor, where round 2 read +0.04; the other 26 cells are under main, coast zoom 3 by 0.04 now. Scene `gpuMs` and frame p50 match the references within their run-to-run swing. Bit for bit: over the golden test's three regions the shader's surface ids and fields are identical before and after, its warps identical except where the new test reports none (all with a field of exactly 1 before), and the C++ fine and low sums match `foundation::fractalNoise2SplitPair` bit for bit over 160,000 points (both probes temporary).

**Gates,** on every view and zoom (the method and noise floor are in Performance):

- `tileGpuMs` at or under main beyond the noise floor and under 1.5 ms: held in 26 of 27 cells, desert zoom 3 at the floor, +0.01 to +0.02 after WOR-564 (above; +0.04 after round 2).
- Frame p50 and p99 and scene GPU not worse beyond the content change: held.
- Chunk generation: unchanged by this step (WOR-558 reported it). WOR-564 leaves the chunk worker's code alone; on one fixed camera path through the three views, 63 chunks before read a 59.8 ms median and 166.8 ms p90 (render tiles 12.1 and 13.9), 62 after 60.9 and 133.8 (12.0 and 13.8).
- Cold bake reported and at most +20%: +10% (WOR-559), and round 2's art about 1 s more, 21.1 to 21.3 s against a 22 s budget (Bake and macro fill); warm under 100 ms: 31 to 37 ms.

## Tunables

All are live through the debug server: set one with `/api/dev/tunable?name=<name>&value=<v>`, read them with `/api/state?what=tunables`. The land defaults are in section 4 of the spec.

| Name | What |
|------|------|
| `terrain/land/warpFine/<Surface>`, `warpLow/<Surface>` | warp amplitudes in meters, by the edge's upper surface |
| `terrain/land/warpFineWavelength`, `warpLowWavelength` | base wavelengths, whole meters |
| `terrain/land/warpFineGain` | amplitude the fine term keeps per octave |
| `terrain/land/thinFloor`, `thinCeil` | the thin-feature guard |
| `terrain/ground/blendContrast` | exponent that sharpens the hex weights (7) |
| `terrain/ground/blendGain` | Mikkelsen's gain r near (48 px/m and up), mid and far (16 and under): 0.75, 0.65, 0.5 |
| `terrain/ground/hexMinPxPerMeter` | below this density the shader takes one sample (16) |
| `terrain/ground/lodBias` | added to every surface's mip level (0.25) |
| `terrain/ground/macroAmp` | scales every def's macro tint amplitude (1) |
| `terrain/ground/macroMidAmp`, `macroFineAmp` | value swing of the macro's 10 m and 5 m octaves (0.045, 0.035) |

The contest's parameters are not tunables. They are per pair, in the ground defs' `<transition>` blocks (Surface transitions), so nothing registers a 36-pair table. The ones the shader reads as they stand are in `kShaderTunables` (`ChunkRenderer.cpp`), `blendContrast` among them. The rest of the ground's are turned into per-frame uniforms on the CPU (`ChunkRenderer::applyGroundUniforms`), registered beside the field's: the gain's exponent from `blendGain`, the hex flag from `hexMinPxPerMeter`, mip levels from `lodBias`, and the macro tint and value folded from `macroAmp`, `macroMidAmp` and `macroFineAmp`.

## Memory

| Group | Surfaces | px/m | Layer | Layers | Base | With mips (x1.33) |
|-------|----------|------|-------|--------|------|-------------------|
| Grass | 4 | 128 | 512x512 | 32 | 32 MiB | 42.7 MiB |
| The rest | 5 | 64 | 256x256 | 40 | 10 MiB | 13.3 MiB |
| Total | 9 | | | 72 | 42 MiB | 56 MiB |

That is 8 variants per surface at 4 bytes a texel. The atlas it replaces is 16 MB. Render tiles stay 524 KB a chunk and the macro texture adds 264 KB, so 8.3 MiB over the 32-chunk LRU.

Measured with today's content (every def's 8 variants, mips 0 to 6): 56.0 MiB, 42.7 MiB for the grass family's 32 layers and 13.3 MiB for the other five surfaces' 40. With the placeholders it was 21.3 MiB.

Driver-side BC1 (S3TC) on upload would have been 8x smaller (about 7 MiB), but heights make alpha real data and BC1 has none. BC3 or BC7 is the open measurement, about 14 MiB for the arrays (Open questions).

## Performance

### Method

The method is WOR-463's (section 6 of the terrain spec), so the numbers compare directly with its tables: RelWithDebInfo, an RTX 3090, a 3072x1728 window, the quickstart planet, vsync off, `scripts/perf-capture.ps1`, the median of 9 samples per scenario after settling. Three views: river `-X 0 -Y 20` (mostly interior tiles), coast `-X 70030 -Y 0` (along a chunk border) and desert `-X 540 -Y 131480`. Each gets idle at zoom 20, 8, 3, 1.5, 0.75, 0.5 and 0.25, plus scroll at zoom 3 and 0.75. The script's zoom is one eighth of the screen's pixels per meter: zoom 3 is the default 24 px/m, zoom 0.25 is 2, zoom 20 is 160.

Recorded per run: `tileGpuMs` (the ground and water pass, GPU timestamp queries), scene `gpuMs`, frame p50 and p99 max, and the scroll scenarios' spikes. From the `[ChunkGen]` dev-tools log: chunk generation worker time (median, p90, max) and the main-thread upload per chunk. Plus the bake time added to the splash and the VRAM for the arrays and macro textures.

Captures hold the GPU at P0 with the game's GPU hold (WOR-553, below), never by changing driver clock settings. `scripts/perf-capture.ps1` turns it on for the whole capture (`-NoGpuHold` leaves it off), records each sample's P-state and graphics and memory clocks from a read-only `nvidia-smi --query-gpu=pstate,clocks.gr,clocks.mem` query, and warns in red if any sample wasn't P0. A capture with a sample off P0 gets rerun, not compared.

### Gates

Every one must hold, on every view and zoom:

- `tileGpuMs` no higher than main beyond the noise floor, which is main captured twice with the GPU hold on, taking the spread per row. The target is net neutral or faster, not a budgeted increase.
- `tileGpuMs` under 1.5 ms everywhere.
- Frame p50 and p99 and scene GPU not worse than main beyond noise, scroll scenarios included (they catch upload hitches).
- Chunk generation median and p90 not worse beyond the macro texture's own measured cost, and that cost reported.
- Bake time reported, with the 0.5 s rule above.

Runtime cost doesn't grow with art complexity. Tiles are sampled as textures, so a modder's denser SVG or busier generator only moves bake time, never frame time. The final capture with real content checks that.

Order of measurement: baseline main twice (the noise floor), profile today's pass before changing it, measure each change alone against the baseline so a regression has a name, then a final capture with real content before the PR. The tables are in this doc and the dev-log entries, and the JSON sits beside the WOR-463 captures in `perf-results/` (the ones this doc cites are committed).

### Clock state

On light views the driver drops the GPU to P3 or P2 (1035 to 1410 MHz against 1890 to 1950 at P0), and the same pass reads 1.4 to 2.1 ms instead of about 0.95. An unpinned capture then compares clocks, not code. The river zoom 8 and 20 "regression" against the WOR-463 table is this and not a code change; the captures committed with WOR-463 already show it. The unpinned baselines of main from 2026-10-31 (`perf-results/capture-2026-10-31-ground-main-<view>-{1,2}.json`, two runs per view) read river zoom 20 at 1.22 and 1.97 ms against 0.76 at P0, river zoom 8 at 1.51 and 1.53 against 0.99, and coast zoom 1.5 at 1.25 and 1.19 against 0.64. The gates compare against main captured with clocks held, and the table below is that.

### GPU hold (WOR-553)

The spike held P0 with ballast: the tile pass drawn again after its timer closed, as many times as kept the GPU near a utilization target. `GpuHold` (`apps/world-sim/scenes/game/dev/GpuHold.cpp`) is that idea made self-contained, so main and a branch run identical code and neither ChunkRenderer is involved. Each frame, after the scene's GPU timer closes, it draws a throwaway ALU-heavy shader into its own 512x512 target that nothing reads, one instanced draw of full-target passes, about 55 us a pass at P0. No GPU time the game reports counts it, neither `gpuMs` nor `tileGpuMs`; its few GL calls land in the CPU's `sceneRenderMs`, and the frame's image is untouched.

The pass count adapts every frame so the scene's GPU time plus the hold's sits near a fraction of the smoothed frame interval, 0.8 by default. It moves by a tenth of the relative error, at most 10% a frame, which stays stable under the GPU timers' four-frame lag, and a view whose scene span already fills the budget (zoom 0.75 and out, on the river view) gets no passes and stays at P0 on its own load. It's dev-only and off by default: `/api/control?action=gpuhold&value=0|1[&target=0.05..0.95]` toggles it, `/api/state?what=gpuhold` reads back the pass count and its GPU time, and a build without the dev server can't turn it on.

Measured 2026-11-01 on the river view at zoom 20, the same code in both builds, 15 one-second samples each:

| Build | Hold off | Hold on |
|---|---|---|
| main (1f46f16b) | P3, dipping to P5, 870 to 1170 MHz; `tileGpuMs` median 1.29 | P0, 1935 MHz, memory 9751 MHz; `tileGpuMs` 0.74 to 0.75 |
| ground tiles branch | P3, dipping to P5, 915 to 1215 MHz; `tileGpuMs` median 1.39 | P0, 1935 to 1950 MHz, memory 9751 MHz; `tileGpuMs` 0.80 to 0.81 |

The hold ran about 96 passes, 5.3 ms of GPU a frame, and main's 0.74 sits beside the 0.76 in the table below. With game time frozen and the app paused, screenshots with the hold off and on were bit-identical everywhere but the water, which animates while paused and differs between two hold-off screenshots as well. A full capture of the river view with the hold on read P0 in every sample of every scenario.

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

Reading it: the hypothesis that mip 0 of a 512 px/m texture thrashes the texture cache did not hold, so the array is free and not faster. The seams are fixed and the land field is where the pass's time goes. Hex ran with a 6 px/m cutoff, which is why zoom 0.75 pays and 0.5 doesn't; raising the cutoff to about 12 px/m was expected to remove the zoom 0.75 cost, and did (it's 16 now, below). Per-pixel fBm in place of the macro texture would cost 0.10 to 0.15 ms.

### Bake and macro fill (2026-11-01)

RelWithDebInfo, today's content: Grass and the eight placeholder defs, 40 variants. With the bake a variant at a time from `buildMeshes`, it added 20.6 s to the splash: Grass's 8 variants took 17.2 s, 2.15 s each, of which 1.9 s was the generator and tessellation, and each placeholder def 0.33 to 0.42 s. With the paths built once on the pool and the disk cache, measured on the game's loading scene (the same steps the splash runs): a cold cache takes 4.47 s of bake steps (4.52 s wall clock), Grass 2.5 s of it (paths 357 ms for all 8 at once, then render 151 and filter 117 a variant) and each placeholder 0.22 to 0.28 s. A warm one takes 36 ms (39 ms wall clock): Grass's 11 MB file reads in 14 ms, each placeholder's in 2, and every upload together takes 3 ms. So the 0.5 s rule holds whenever the cache is warm, and a clean build or an edited def pays the cold bake once.

With the real art (round 1), the same machine and build: a cold cache takes 18.3 s of the splash (18.25 s of bake steps, 72 variants), the grass family 3.0 to 3.5 s a def and the bare surfaces 0.9 to 1.3 s. Baked alone, with the rest cached, Grass takes 2.95 to 2.98 s, GrassMeadow 2.95, GrassShort 2.63 and GrassTall 2.62; in the full cold run a def baked right after another grass def runs 10 to 20% slower than alone, in generation and tessellation both, which looks like heap state rather than content. Dirt takes 1.0 s, Sand 1.0, Mud 1.2 to 1.3, Rock 1.0 and Snow 0.9. A warm cache takes 30 to 33 ms of bake steps (35 to 38 ms wall clock): the lookups read every def's file side by side on the pool in 20 to 21 ms (one after another they took 76 to 78 ms, 13 to 14 ms for each grass def's 11 MB file), and the uploads take 10 to 12 ms.

With round 2's bare art (WOR-561), two cold runs took 21.07 and 21.34 s of bake steps (21.16 and 21.41 s wall clock; paths 4.3 to 4.5 s, heights 2.5 to 2.6, render and filter 14.2) against 20.2 s before, and three warm runs 33 to 37 ms (39 to 42 ms wall clock). Dirt bakes as it did, 1.2 s; Rock takes 1.4 s, Sand 1.5 and Snow 1.3, a quarter more than round 1, nearly all generator time on the pool for the spectral fields (40 to 64 waves summed over a 128-node grid, a field) and the extra paths of streaks, lichen and joints; Mud's fewer, smaller puddles take it to 1.5 s, about what it was. The arrays stay 56.0 MiB: no density changed.

The macro fill on the chunk worker is 3.0 ms median and 6.6 ms max over 27 chunks, against a 65 ms chunk generation median (`[ChunkGen]` reports it as `macro`). Hashing four corners a texel for each octave cost 12.5 ms; hashing each lattice corner once a chunk gives the same bytes.

### Back under main (2026-11-01)

The first full build (e533e5b7) read 0.04 to 0.11 ms of `tileGpuMs` over main in every cell, median +0.06. A temporary bitmask uniform replaced one suspect at a time, interleaved at fixed cameras on the three views. The instrumented build itself read 0.03 to 0.06 ms over the clean one, so these are shares, not exact costs:

| Replaced | `tileGpuMs` saved |
|---|---|
| The whole ground sampler, by a constant color | 0.10 to 0.12 at zoom 20, 0.13 to 0.14 at 3, 0.15 to 0.17 at 1.5, 0.08 to 0.10 at 0.75 and 0.25 |
| Hex blending, by one sample everywhere | 0.03 to 0.04 at zoom 20, 0.05 at 3, 0.07 to 0.09 at 1.5, none below the cutoff |
| The cell hash, by a cheap stand-in | 0.01 to 0.03 |
| The variant pick's integer modulo | 0.01 to 0.02 |
| The sRGB encode | 0.00 to 0.02 |
| The macro sample and tint | 0.00 to 0.01 |
| Rotation and mirror | 0.00 to 0.01 |
| The hex lattice on the one-sample path, the four-way array choice, per-surface uniform indexing | nothing measurable |
| The land field's warp noise (for scale) | 0.14 to 0.45 |
| One fewer fine warp octave | 0.03 to 0.07 |

No single item made the far-zoom cost, where hex blending is off: the one-sample path paid about 0.05 ms in a dozen quarter-rate operations a pixel (int and float conversions, MUFU), each worth about 0.01. What was kept, each measured with the full capture against the commit before it:

| Commit | Change | `tileGpuMs` |
|---|---|---|
| fbfe6617 | Sampler off the conversion unit: float cell ids through the 1.5 x 2^23 bias, the variant pick in float, the cell origin and macro tint folded on the CPU | river 0.01 to 0.02 lower in every cell |
| 56caa3a6 | The field's blur summed by row, the thin test on the whole mask, one inversesqrt for the isoline distance (the field is bit-identical) | river 0.02 to 0.07 lower in every cell |
| e24ebf8d | The near-interior shortcut (render tile bit 0x40) | 0.03 to 0.06 lower at zoom 20 and 8, up to 0.03 elsewhere |
| 4f66be04 | The ground evaluated only where water doesn't cover it | river 0.10 to 0.24 lower at zoom 3 to 20; coast up to 0.07 idle and 0.06 to 0.11 on both sea scrolls; desert within 0.01 |
| 6e5abd53 | Hex blending from 16 px/m, was 12 | 0.05 to 0.10 lower at zoom 1.5 only |
| 84cfbe3a | The heaviest hex cell sampled first, from one place | the desert scroll at zoom 3 from 0.25 to 0.24; others within 0.01 |

Final, two runs per view against main's two, every sample at P0 (27 of 27 scenarios in each run), `tileGpuMs` in ms, the e533e5b7 column the mean of its two runs:

| Scenario | View | main r1 / r2 | e533e5b7 | final r1 / r2 | main | final | delta |
|---|---|---|---|---|---|---|---|
| idle zoom=20 | river | 0.74 / 0.74 | 0.81 (+0.07) | 0.60 / 0.60 | 0.74 | 0.60 | -0.14 |
| idle zoom=20 | coast | 0.73 / 0.73 | 0.80 (+0.07) | 0.67 / 0.67 | 0.73 | 0.67 | -0.06 |
| idle zoom=20 | desert | 0.81 / 0.82 | 0.89 (+0.07) | 0.73 / 0.73 | 0.81 | 0.73 | -0.08 |
| idle zoom=8 | river | 0.97 / 0.98 | 1.04 (+0.07) | 0.69 / 0.69 | 0.97 | 0.69 | -0.29 |
| idle zoom=8 | coast | 0.60 / 0.60 | 0.65 (+0.05) | 0.55 / 0.55 | 0.60 | 0.55 | -0.05 |
| idle zoom=8 | desert | 0.72 / 0.72 | 0.78 (+0.06) | 0.66 / 0.66 | 0.72 | 0.66 | -0.06 |
| idle zoom=3 | river | 0.66 / 0.67 | 0.74 (+0.07) | 0.53 / 0.53 | 0.67 | 0.53 | -0.14 |
| idle zoom=3 | coast | 0.66 / 0.66 | 0.74 (+0.08) | 0.59 / 0.59 | 0.66 | 0.59 | -0.07 |
| idle zoom=3 | desert | 0.56 / 0.56 | 0.63 (+0.07) | 0.54 / 0.54 | 0.56 | 0.54 | -0.02 |
| idle zoom=1.5 | river | 0.57 / 0.57 | 0.67 (+0.10) | 0.49 / 0.49 | 0.57 | 0.49 | -0.08 |
| idle zoom=1.5 | coast | 0.64 / 0.64 | 0.75 (+0.11) | 0.57 / 0.57 | 0.64 | 0.57 | -0.07 |
| idle zoom=1.5 | desert | 0.51 / 0.52 | 0.61 (+0.09) | 0.49 / 0.49 | 0.52 | 0.49 | -0.03 |
| idle zoom=0.75 | river | 0.46 / 0.46 | 0.50 (+0.04) | 0.42 / 0.42 | 0.46 | 0.42 | -0.04 |
| idle zoom=0.75 | coast | 0.59 / 0.59 | 0.64 (+0.05) | 0.54 / 0.54 | 0.59 | 0.54 | -0.05 |
| idle zoom=0.75 | desert | 0.50 / 0.50 | 0.54 (+0.04) | 0.48 / 0.48 | 0.50 | 0.48 | -0.02 |
| idle zoom=0.5 | river | 0.44 / 0.44 | 0.48 (+0.04) | 0.41 / 0.41 | 0.44 | 0.41 | -0.03 |
| idle zoom=0.5 | coast | 0.59 / 0.59 | 0.64 (+0.05) | 0.55 / 0.55 | 0.59 | 0.55 | -0.04 |
| idle zoom=0.5 | desert | 0.52 / 0.52 | 0.56 (+0.04) | 0.50 / 0.50 | 0.52 | 0.50 | -0.02 |
| idle zoom=0.25 | river | 0.36 / 0.35 | 0.40 (+0.05) | 0.35 / 0.35 | 0.35 | 0.35 | -0.01 |
| idle zoom=0.25 | coast | 0.50 / 0.51 | 0.56 (+0.06) | 0.46 / 0.46 | 0.51 | 0.46 | -0.04 |
| idle zoom=0.25 | desert | 0.43 / 0.44 | 0.48 (+0.04) | 0.42 / 0.42 | 0.43 | 0.42 | -0.02 |
| scroll zoom=3 | river | 0.41 / 0.41 | 0.48 (+0.07) | 0.41 / 0.41 | 0.41 | 0.41 | +0.00 |
| scroll zoom=3 | coast | 0.33 / 0.33 | 0.45 (+0.11) | 0.31 / 0.31 | 0.33 | 0.31 | -0.02 |
| scroll zoom=3 | desert | 0.22 / 0.23 | 0.29 (+0.06) | 0.23 / 0.24 | 0.23 | 0.23 | +0.01 |
| scroll zoom=0.75 | river | 0.39 / 0.37 | 0.42 (+0.04) | 0.39 / 0.38 | 0.38 | 0.39 | +0.01 |
| scroll zoom=0.75 | coast | 0.33 / 0.33 | 0.39 (+0.06) | 0.31 / 0.31 | 0.33 | 0.31 | -0.02 |
| scroll zoom=0.75 | desert | 0.74 / 0.74 | 0.79 (+0.05) | 0.71 / 0.71 | 0.74 | 0.71 | -0.03 |

Every cell is at or under main + 0.02 ms: 24 of 27 are lower, the desert scroll at zoom 3 and the river scroll at zoom 0.75 read +0.01 and the river scroll at zoom 3 +0.00, and the median is -0.04. The desert scroll at zoom 3 is the tight one; hex blending is 0.03 to 0.04 ms of it, and off it reads 0.21. Screenshots at x 97, y 27 match the e533e5b7 build on the ground at zoom 20 and 3 (the only differing pixels are the HUD's); at zoom 1.5 the hex cutoff leaves hard cell edges, a mean of 0.7 and at most 28 levels of 255 in a few pixels, which don't read at 1:1. Scene `gpuMs` from river zoom 0.75 out is the entity pass and swings by about 1 ms between runs of the same build, so it isn't a measure of this pass.

Tried and dropped:

- Skipping the sharpening pow where one barycentric weight guarantees the one sample (0.636 at contrast 7, 40% of hex pixels), with a 256-entry cosine and sine table for free rotation: within 0.01 everywhere, so the hex path isn't short of MUFU.
- Folding the below-cutoff path into the hex sample slots: hex-off zooms 0.01 to 0.02 slower, so it keeps its own early return.
- Bilinear on the nearest mip instead of trilinear, and a lod bias of 1.25: no change on the desert scroll, so filtering and the texture cache aren't the limit.
- A polynomial sRGB encode on sqrt, fit offline: 2 to 5 levels of 255 off, too far to be the same picture.

Not taken: storing the arrays as plain RGBA8 to drop the encode (about 0.01, but filtering and the hex blend would leave linear light, which the bake's downsampling is built around), and dropping a fine warp octave or a cheaper noise (0.03 to 0.07, but edges move, and the gate held without it).

## Testing

- Seams: world-sim on 8081, Grass ground, `camera` control at `zoom=20`, `x = n - 0.0034` (the case that lights every column on main). No tile lines. Repeat at `x = n + 0.003` and at zoom 1 and 3, and pan across a chunk border (a multiple of 512 m): no seam at the lattice or the chunk edge.
- Variety: screenshots at zoom 1, 3 and 20 on Grass, a GrassTall patch and a Dirt edge. No visible 1 m or 4 m repeat.
- LOD: zoom out step by step over GrassMeadow. Flower heads tagged with `path:setLod` fade near their threshold, with no muddy tint left behind and no popping.
- Asset-manager: the ground category lists all 9 land surfaces, the 3x3 preview shows no seam, an edit to a generator param followed by Reload shows the change, and validation is clean.
- Unit tests: validator rules, periodic noise wraps, a mesh over 65,535 vertices splits, paths built once match a generator run per density, a bake on the pool matches one in turn bit for bit, and the bake cache's round trip, key and damaged files. `SurfaceFieldGolden` and the existing suite pass.
- Transitions: in game at zoom 20, 3, 1.5 and 0.5, one view a pair: grass over Dirt (tufts over the edge, contact shadow), grass over Sand (the desert view), Sand over Rock (the joints fill), Snow over Rock (the tundra, x 0, y 6000000), Mud at a river (the stain), and a grass family edge (no visible line). At 0.5 no shimmering line: the mip window holds. Unit tests: the height bake (rank normalization, alpha survives the filter, `setHeight` default), the `<transition>` parse (defaults, overrides, a broken block dropped) and validator rules (`over`, the band cap, the offset, ranges, duplicates, unknown fields), the pair table's build and per-draw pack and its compact support (`GroundPairs.test.cpp`), and the cache key ignoring comments, `<macro>` and `<transition>` blocks.
- Golden: `SurfaceFieldGolden.test.cpp` runs on the dev box and on the Linux CI job, where Mesa llvmpipe behind Xvfb gives it a context and `WORLDSIM_REQUIRE_GL` makes a missing one a failure. It compiles the real `land.glsl` as GLSL 4.10 and renders five 32 m regions of a synthetic world in three chunks: the middle and two corners of one 70 km out, the middle of its mirror across the origin, and the far corner of chunk (-1, -1), whose reads cross zero, so the noise's floor division and hashing run on negative tiles too. `GlslMatchesCpp` holds the painted surface to the C++ wherever no field is within 1e-5 of flipping it, and the field and warp within 1e-4, except the warp of a pixel where only one side took the near-interior shortcut (`shortcutSplit`): both read one surface at field exactly 1 there, and the shortcut reports no warp, so rounding on one side of its bound would otherwise fail a pixel whose surface agrees. `BandDistanceMeetsAtThePaintedBoundary` holds the band's distance on both sides of each painted boundary to two pixels (0.22 m at worst), and `FarZoomLodMovesEdgesUnderAPixel` the far-zoom LOD to its bound.
- Performance: the final capture per the gates above, plus the transitions' own in Surface transitions.
- Manual script: `docs/testing/scenarios/09-ground-tiles.md`, one numbered line a step.

## Open questions

- Compressed formats: heights make alpha real data, so BC1 (no alpha) is out. BC3 or BC7 is twice BC1's size, about 14 MiB for the arrays against 56 MiB raw. Whether either is neutral or better, and looks right on dense blade art, is still to measure.
- The cold bake's render: the GPU time in salal's batched quad path, about 0.15 s a grass variant with today's art. A plain indexed-triangle draw for the baker is the next lever; the height pass's render at mip 0 adds 2.3 s to a cold bake, nearly all of the +10% it costs.
- The cost of organic biome edges at mid zoom: desert zoom 3 reads 0.01 to 0.02 ms over main after WOR-564's second near-interior test (0.04 after round 2's 0.2 m family band, 0.06 before; Surface transitions, Perf ledger), from the field's full path on the many more boundary pixels WOR-558 made. Shortcuts have little left to take: the one-surface reads they can't prove lie in rings a few pixels wide beside true edges, whose warps hold edge pixels, and 38% of that view's pixels read a block of more than one surface, mostly Grass and GrassShort mixed tile by tile. The six noise octaves, the block, the loop and the band on those pixels are what's left, and must stay bit for bit with the C++ twin; a cheaper twin (C++ and GLSL changed together, placement re-verified) is the lever.
- Grass family heights: rank-normalized per surface, as designed, or on one shared scale so taller blades overtop shorter ones across a family edge (short tips 0.35, grass 0.5, meadow 0.7, tall 0.95, ranked across the family only). Per surface holds so far: in round 2 a family edge reads as a change of sward even at 0.2 m, with no dissolve; the shared scale stays the fallback.
- Tuning pair values: edits to `<transition>` blocks take a restart, with no re-bake (measured: 31 ms of warm bake steps after an edit). A dev verb that sets one pair field live is the cheap fix if that proves slow.
- Two arrays by density against one array with every layer at 512x512 (the rest would cost about 40 MiB more). The spec picks two.
- Per-asset shader snippets, to explore. A ground folder could ship a GLSL function beside its XML (for example `assets/world/ground/Grass/surface.frag.glsl`, defining that surface's color from position, the baked sample and the macro tint), and the loader would assemble every snippet into the tile shader at first run, with a switch on surface id. Today the tile shader is one program loaded from a folder, and salal assembles its base shader from hooks at build time, so this moves assembly to load time for the tile pass. Questions: compile time and whether to cache the program binary (`glGetProgramBinary`); validating a modder's GLSL and reporting errors through asset validation; the per-pixel cost being unbounded for shader content, where baked tiles cost the same however detailed they are, so it needs a budget and the perf gates; how it sits with baked variants (a snippet that only post-processes the baked sample is cheap, one that replaces it isn't); and the Rust and Vulkan rewrite, where it becomes SPIR-V.

## Follow-ups

Planned, tracked in Specboard as WOR-583, Ground tile follow-ups. None blocks the shipped work.

- SVG ground defs with the `data-lod` filter (LOD). salal's `loadSVG` takes a byte span since v0.37, so the filtered markup can load from memory.
- BC3 or BC7 for the arrays (Open questions).
- A 4.1 core context from salal. The tile shaders are GLSL 4.10 and salal asks for 3.3; NVIDIA and macOS accept that, a strict driver may not (Shader).

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
