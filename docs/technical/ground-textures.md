# Ground Texture System

Created: 2025-12-13
Updated: 2026-11-01 (ground tile variants: tiles become SVG/Lua assets baked to texture arrays and mixed by a hex lattice in the shader, and the atlas goes away; spike results and the perf plan added. Later: the bank, the hex sampler and the macro texture are in the tile pass, the atlas is deleted, and the surfaces without art paint placeholder defs. Then: the bake runs each generator once, on the pool, and keeps its bakes in a disk cache the splash reads. Then: the tile pass back under main in every cell, through a leaner sampler, a cheaper land field, no ground under open water and hex blending from 16 px/m. Then: round 1 of the real art for the eight surfaces that painted placeholders, the grass family from `ground_grass.lua` and Dirt, Sand, Mud, Rock and Snow from one new generator, `ground_bare.lua`; the bank reads its cached defs side by side and the encode keeps a small mip's mean)
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
| `<assetType>` | `procedural` with a `<generator>`. `simple` with one or more `<svgPath>`s, each SVG being one fixed variant, is planned (it needs the `data-lod` loader below); validation rejects it for now |
| `<variantCount>` | variants baked, 1 to 32, default 8 (for `simple` the SVG list is the variant list) |
| `<period>` | meters one variant covers, default 4; it must divide 512 so it lines up across chunks |
| `<pixelsPerMeter>` | base texel density, default 64, so a 4 m variant is a 256x256 layer |
| `<rotate>` | `free`, `quarter`, `none` or `mirror`; `none` for art with baked top-left lighting, `mirror` (never turned, each cell flipped left to right or not) for art with an up, like grass blades |
| `<macro>` | two tint colors (dry, lush), plus the amplitude and wavelengths of the low-frequency layer the shader mixes between them |

Generators and SVGs draw in meters on a `[0, period]` square. Shapes may cross the edge, so authors never match edges by hand: `AssetRegistry::buildPaths` (and `buildMeshes`, a thin call over it) repeats every path of a procedural ground def that crosses an edge at the offsets it crosses into, right after the original. Paint order holds across the seam, and the meshes clipped to the square are the seamless tile. Drawing the whole mesh 3x3 instead would paint each copy's full-tile layers over the earlier copies' overhang, so the seams would show.

Validation (`AssetValidator.cpp`): exactly one ground def per land surface, `assetType` is `procedural`, `period` divides 512, `period` x `pixelsPerMeter` is a whole number of texels from 1 to 1024 (`GroundLayer.h`, the same rule `GroundTileBaker::begin` and `GroundTileBank::begin` apply before they allocate), `variantCount` is 1 to 32, and the role is known (`parseAssetRole`, `isKnownRole`).

Lua gets two noise bindings (`LuaEngine.cpp`, `registerBindings`), both thin wrappers over salal's `foundation/random/HashNoise.h`: `noise2(x, y, seed)`, and `periodicNoise2(x, y, period, seed)`, which wraps the gradient lattice so density fields tile. A Lua path also gets `path:setLod(N)` (see LOD below).

`tessellateAsset` currently wraps its `uint16` indices silently past 65,535 vertices (`AssetRegistry.cpp`). It splits into several meshes instead, the way `SVGLoader.cpp` guards. Dense ground tiles will hit the limit, and so could flora.

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

For each def and each seed `0..variantCount-1` (salted per def name) `GroundTileBaker` runs the generator once and tessellates each path once with `AssetRegistry::buildPaths` (already wrapped, see above), every variant at once on a `TaskPool`. Each generator run builds its own Lua state and the registry is only read, so the runs share nothing; the GL work stays on the GL thread. Each mip then packs the paths its density keeps into meshes with `TessellatedPaths::meshesAt` (a dense tile is over 65,535 vertices, so several meshes) and draws them once into a `Renderer::RenderTarget` the size of the square, which clips the overhang. It supersamples (8x8 for mips 0 and 1, 4x4 below, since salal has no MSAA), Kaiser-downsamples in linear light, mean-locks each mip to a filtered copy of mip 0 and encodes sRGB, dithered. A dither leaves each texel unbiased but not a small level's mean: on a 4x4 mip 6 one code of a dark channel is several percent of its light, so a dark surface missed the 0.5% gate on dither luck alone. A channel whose mean lands more than 0.05% off has its codes stepped one at a time, the texels that rounded furthest the wrong way first, until no step brings it closer (`encodeSrgb8`); a bake's mean error is now under 0.1% at every mip. The coordinate system and viewport are restored afterwards. This is the mesh path flora uses, so gradients and colors match the asset-manager exactly.

Every mip level is rendered from the vectors, not generated (`glGenerateMipmap` would neither wrap nor drop LOD nodes); see LOD. The bank allocates each raw-GL `GL_TEXTURE_2D_ARRAY` level by level with `glTexImage3D` once it knows the layer counts, and writes each variant's chain in with `glTexSubImage3D` as soon as it's baked: `GL_SRGB8_ALPHA8` (so filtering and the hex blend run in linear light), `GL_REPEAT`, trilinear, `GL_TEXTURE_MAX_LEVEL` at the last baked mip. No anisotropic filtering; a top-down orthographic view has no anisotropy to filter.

The layers of one array share a size, and the grass surfaces (128 px/m, 512x512 layers) differ from the rest (64 px/m, 256x256), so there are two arrays, one per layer size; the tile shader has samplers for up to four, and a def needing a fifth is a bake error. The bank keeps a per-surface table of `{array, firstLayer, layerCount, invPeriod, texelsPerMeter, rotate, macro tints and amplitude}`. The baker logs each def's report; the bank logs the totals (its steps' time and the wall clock, how many defs came from the cache) and the bytes of each array.

The baker works in steps, so a caller can spread a def over frames: `begin` looks the def up in the disk cache (below) and hands back the whole bake when it's there, `buildPaths` runs every variant's generator on the pool, `bakeVariant` renders and filters one variant on the GL thread, and `finish` measures the variants, logs the report and stores the bake. `bakeDef` runs them in order in one call; the asset-manager's Bake view runs them a frame at a time. The bank runs a step a call, so the splash keeps drawing between them: the first call looks every def up in the cache at once, on the pool, since reading the cached bakes is most of a warm bake; a call uploads every def in a row that the cache held, and at the first it didn't, builds that def's paths (the GL thread waits on the pool, about 0.4 s for Grass); each later call bakes and uploads one variant, and the one that bakes a def's last variant stores the def. The splash's boot log shows `Baking ground tiles n/N` and the Enter button waits for the last. A `--scene` jump skips the splash, so `GameLoadingScene` finishes whatever is left before it loads the planet, and a direct switch to the game scene bakes it all at once. A failed variant or a land surface with no def blocks the splash the way a validation error does, and a def that failed isn't stored.

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
- Where one cell dominates (its sharpened weight, before the luminance term, over 0.98: 58% of the area at contrast 7), the shader takes one sample. Only the transition bands between cells take 2 or 3, 1.48 samples a pixel on average. Every pixel takes its heaviest cell's sample from the same instructions and only the bands branch for more, so a warp straddling a band edge runs that sample once.
- Below a density (`terrain/ground/hexMinPxPerMeter`, 16) it skips blending and takes the dominant cell's sample, the way `u_landWarpFineOctaves` drops warp octaves a pixel can't show. Cell interiors are the same sample either side of the cutoff, so crossing it only hardens the bands between cells. The spike ran with 6 px/m and the first build with 12. At 12 px/m (zoom 1.5) the one-sample ground matched the blend to the eye in screenshots, the hard cell edges a few levels of 255 at most in speckled art, and it saves 0.05 to 0.10 ms there, so the blend starts at 16, where the gain ramp starts.
- Variants are picked uniformly per cell, in float on the hash's low 12 bits: the same pick as `h % count` for every power-of-two count, within 1/4096 of uniform otherwise. Weighted picks (mostly quiet, a few busy) wait for a def that asks for them.
- The sampler stays off the GPU's conversion unit, which runs at an eighth of the float rate and is what the pass is short of: cell ids stay floats and cross to int through a 1.5 x 2^23 bias, and the CPU folds the chunk's first cell id, the variant pick's constants, and the macro tint and value into per-draw uniforms.
- Samples use `textureLod` at a mip level computed once per draw on the CPU for each surface, `log2(texels per meter / framebuffer pixels per meter) + terrain/ground/lodBias` (0.25, research rule 9). The view is an orthographic top-down projection, so texel density is the same at every pixel and rotation doesn't change it. That avoids `textureGrad` and per-pixel derivatives, and nothing jumps at tile or hex edges.
- The samples come back linear (the arrays are sRGB), are blended linear, and `groundColor` encodes the result to sRGB, which the rest of the pass (fringe, rim, water) works in. The macro tint multiplies the sRGB color.

The macro layer comes from a per-chunk texture, not per-pixel noise. The chunk's generation worker fills a 2 m-per-texel texture (`GroundMacro.h`: 257x257 RGBA8, 264 KB a chunk, uploaded with the render tiles under the same version and LRU) from salal `HashNoise` gradient noise, each lattice corner hashed once a chunk. Texel (i, j) sits on chunk-local (2i, 2j) m, so the last row and column are the next chunk's first and a bilinear read matches across the border. R is the dry to lush mix (octaves at 36 and 14 m), G and B value noise at 10 m and 5 m (5 m is the shortest wave 2 m texels carry without a lattice pattern). The shader takes one bilinear sample a pixel: each surface mixes its def's dry and lush tints by R, scaled by the def's amplitude times `terrain/ground/macroAmp`, and every surface's value moves by G and B times `macroMidAmp` (4.5%) and `macroFineAmp` (3.5%). The def's `<wavelengths>` aren't read: one texture serves every surface. Per-pixel fBm is what cost about 30 ms in the December tile shader (`docs/debug-swapbuffers-performance.md`) and still costs 0.10 to 0.15 ms of GPU at the spike's scale. The same texture is where phase 2's climate and moisture data lands, and its C++ source can later tint groundcover tufts to match.

The fringe samples the other surface once, without hex blending: it's a thin band and the blend isn't visible there.

`landColor` calls `groundColor` for the painted, upper and lower surfaces. The fringe and rim logic is unchanged, and `evaluateLand` is untouched, so `SurfaceFieldGolden.test.cpp` stays valid (it only checks the field).

Under open water the ground isn't evaluated at all. `tile.frag` asks `water.glsl` for the pixel's water coverage first (`waterPixel`: the distance field, its wobble, the coverage) and calls `landColor` only where some ground shows; where the coverage is 1 the water mixes the ground out entirely, so the picture is the same.

What a ground pixel costs, against main:

| | Main | New |
|---|---|---|
| Render-tile fetch | 1 | 1 |
| Ground samples, interior | 1, mip 0 of a 512 px/m pattern | 1 where a cell dominates, 2 or 3 in blend bands, mipmapped |
| Macro | none | 1 bilinear sample |
| Per-pixel noise | none | none (one hash per hex cell) |
| Surface edges | 2 samples | 2 to 4 samples, the fringe's other surface takes 1 |
| Under open water | the field and 1 sample | nothing |

Deleted with the atlas: `GameBootstrap::initializeTileAtlas`, `surfacePatternPath`, `assets/tiles/surfaces/`, and the now-dead `Chunk::getSurfaceColor` and `getBiomeColor`.

## Art direction

The look matches the flora: layered flat-color shapes in the palette family of `grass.lua`, seen from a hair off top-down. It is dense, much denser than RimWorld's flat dirt with 1 px grass wisps, and less detailed than the photos it's drawn from. The ground is lit from the top of the screen: what stands, stands up the screen, lit edges face up and shadows fall down. That holds under `mirror`, which flips a cell left to right, and not under a turn, so every def is `mirror`. LOD carries each surface across zooms: fine detail near (blades, grain, sparkle), clumps and lumps mid, tonal patches far, and mips 5 and 6 close to the tile mean, which the per-chunk macro tint then moves. No feature reaches 1.3 m: the largest are rock slabs of about 0.6 m and puddles of 16 to 40 cm.

Two generators paint the nine surfaces, each def differing only in `<params>`, which `Grass.xml` and `Dirt.xml` document:

- `ground_grass.lua`, the grass family: a far stack drawn from a tone model of the layout under a near stack of clumped tuft fans, undergrowth, blades, clover, rosettes, soil peeks and litter (see its header). Wildflowers are its one addition, for GrassMeadow: heads in blue-noise clusters and singles, each on a proxy disc in its average color kept to mip 3, drawn over everything and painted into the tone model. The far stack's mean is `farMean` and its contrast `farContrast` and `farCoarseContrast`, set from the bake report.
- `ground_bare.lua`, the unvegetated surfaces: the ground (base, broad tone patches, rock plates) is laid out first and painted into a 6.25 cm tone grid that every ground-born layer above takes its color from (grain, crests, cracks, lumps, shadows), so they keep the patches' contrast at every mip, and the grid is scaled so each variant averages to `baseColor`. Mips 4 to 6 are that grid averaged into one rect a mip 4 texel, with puddles and lichen averaged in, under a near ground that covers it from mip 3; `farContrast`, `farCoarseContrast` and `farGain` set their contrast and mean from the bake report. Layers: tone patches, rock plates (Voronoi slabs with joints, merged neighbors and lit and shaded bevels), ripples and drifts (rows of crests, each a lit strip over a shadowed lee as wide as cancels it in linear light), cracks with a lit lip, paired light and dark grain, pits, lichen, puddles, lumps, then loose pieces (roots, straw, leaves, twigs, shells, stones on proxies) back to front, grass tips and sparkle.

| Surface | Look | Tile mean, sRGB | Macro: dry, lush, amplitude |
|---|---|---|---|
| Grass | dense clumped sward with clover, rosettes and earth peeks (unchanged) | 80.6, 106.8, 48.8 | (1, 0.94, 0.72), (0.86, 0.96, 0.86), 0 |
| GrassShort | dry short turf: an olive sward dotted with tussocks of short yellow-green tufts, more straw, dry tips and soil peeks, about 12% open ground | 93.1, 105.3, 51.9 | (1.06, 1.02, 0.86), (0.94, 0.98, 1.1), 0.4 |
| GrassMeadow | lush, saturated sward with more clover, under 3% open ground, and white, yellow, violet and pink flower heads 3 to 5 cm across, a near-zoom accent | 77.8, 116.0, 48.9 | (1.04, 1, 0.9), (0.96, 1, 1.08), 0.3 |
| GrassTall | long, dark, bluer grass: bigger clumps of taller tufts, long flopping blades, a share with tan seed-head tips, under 3% open ground | 65.4, 93.0, 40.9 | (1.05, 1.02, 0.88), (0.95, 0.98, 1.1), 0.3 |
| Dirt | packed soil as a dense crumble of clods lit from the top, faint damp and dry mottling, speckled grain, small stones, fine cracks with a lit lip, a few dead grass bits, roots and twigs | 107.4, 84.0, 64.1 | (1.08, 1.05, 1), (0.92, 0.95, 1), 0.5 |
| Sand | pale grain under rows of wind ripples meandering across the screen and broader faint swells, low dune-tone patches, mixed mineral grains, a few pebbles, shell bits and dry twigs | 202.0, 176.0, 131.3 | (1.04, 1.02, 0.96), (0.96, 0.98, 1.04), 0.4 |
| Mud | dark wet ground, densely churned lumps with wet gloss dabs, small puddles in hollows (lit lower bank, shaded upper edge, sky streaks, a glint), stones, twigs, leaves and soggy straw | 78.0, 63.4, 49.7 | (1.08, 1.06, 1.02), (0.92, 0.94, 0.98), 0.4 |
| Rock | weathered bedrock: grey slabs of uneven size along wandering joints, lit along their upper edges and shaded along their lower ones, fractures, speckle, pits, crustose lichen in pale yellow-green, grey-green and ochre, grit and pebbles | 121.9, 117.0, 107.8 | (1.04, 1.02, 0.98), (0.96, 0.98, 1.02), 0.4 |
| Snow | soft off-white with wind-sculpted drifts and fine ripples (a bright crest over a cool blue lee), soft blue hollows, ice sparkle at mip 0 only, the odd twig or grass tip | 224.9, 231.0, 238.3 | (1.02, 1.01, 0.98), (0.98, 0.99, 1.02), 0.3 |

The tile mean is mip 0's, over the 8 variants; each macro's dry and lush average to about 1, so the mean is also the surface's average in game. The grass family is 128 px/m and the rest 64: the warm bake reads every cached def at load (56 MiB of them at these densities) and 128 for all nine wouldn't fit its budget, and the bare surfaces are built from features of 1.5 cm and up, so they stay legible magnified 2.5 times at zoom 20.

Rotation: the grass family is `mirror` because blades stand up the screen; Dirt, Mud, Rock and Snow because their relief is lit from the top (clod caps and shadows, slab bevels, pits, puddle banks); Sand and Snow also because their ripples and drifts run across the screen, which a turn would point every way.

Blending (today the fringe samples the neighboring surface once over a thin band):

- The grass family reads as one family: any pair's boundary is a change in sward, not a change of material. Short is olive-yellow and lighter, Tall darker and bluer, Meadow more saturated, and the far mips keep them apart by mean alone. Meadow's flower heads faded to 50% in a fringe read as pale specks, which is fine.
- GrassShort shows Dirt-like earth between its tufts (ground #5C4D33), so a Dirt to GrassShort edge reads naturally hard or soft. Dirt to Grass is a bigger step (Grass's earth is darker), which a soft edge suits. Clods are 2.5 to 8 cm, so an edge through them doesn't show.
- Sand is the brightest land surface after Snow, about 2.5 times Grass's light, so a Sand to grass edge wants a soft, broken fringe. A hex cell border breaks its ripples: in game at zoom 20 that shows as a vertical seam through them where two variants meet. A transition edge through ripples reads as ripples ending, and a 50% fade only halves them. Snow's drifts break at cell borders the same way.
- Mud's puddles are its largest and glossiest features. A transition edge clips them (visible on the river banks at zoom 20, where the grass edge cuts a puddle), and faded to 50% their sky streaks and glints look wrong, half-transparent reflections on grass. Mud wants a hard edge, or blending that keeps its puddles out of the band. It's darker than any grass, so a Mud to grass bank reads as wet ground.
- Rock's slabs are the largest shapes in the set; faded at an edge they read as rock going under soil, so a hard edge suits it (rock's warp amplitudes are already smaller). Rock to Snow is the strongest pair, about 1.9 times the light, and the tundra paints it constantly: Snow's fringe on Rock reads as snow lying on rock, and a soft edge looks natural.
- Snow faded to 50% over anything reads as a thin cover, which is natural; its sparkle is mip 0 only, so it never shows in a far fringe.

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
4. The edge is anti-aliased over one pixel from `u_metersPerPixel`, with a sparse fringe of the upper surface on the lower side whose width follows the warp noise along the edge, and a rim darkening just inside the upper surface (rock only by default). A pixel in an interior tile costs one render-tile fetch, one macro sample and one ground sample.
5. In a near-interior tile the fine warp moves the read at most its amplitude from where the low warp puts it, so after the low noise the field checks whether that range keeps the read's 4x4 block inside the tile's one-surface reach; when it does the surface is the tile's, with field 1, and the fine noise and the block are skipped. Surface, field and margin are unchanged (`SurfaceFieldTest.NearInteriorShortcutMatchesTheFullField`); only the reported warp is zero there.

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
| `terrain/ground/hexMinPxPerMeter` | below this density the shader takes one sample (16) |
| `terrain/ground/lodBias` | added to every surface's mip level (0.25) |
| `terrain/ground/macroAmp` | scales every def's macro tint amplitude (1) |
| `terrain/ground/macroMidAmp`, `macroFineAmp` | value swing of the macro's 10 m and 5 m octaves (0.045, 0.035) |

The ones the shader reads as they stand are in `kShaderTunables` (`ChunkRenderer.cpp`), `blendContrast` among them. The rest of the ground's are turned into per-frame uniforms on the CPU (`ChunkRenderer::applyGroundUniforms`), registered beside the land look's: the gain's exponent from `blendGain`, the hex flag from `hexMinPxPerMeter`, mip levels from `lodBias`, and the macro tint and value folded from `macroAmp`, `macroMidAmp` and `macroFineAmp`.

## Memory

| Group | Surfaces | px/m | Layer | Layers | Base | With mips (x1.33) |
|-------|----------|------|-------|--------|------|-------------------|
| Grass | 4 | 128 | 512x512 | 32 | 32 MiB | 42.7 MiB |
| The rest | 5 | 64 | 256x256 | 40 | 10 MiB | 13.3 MiB |
| Total | 9 | | | 72 | 42 MiB | 56 MiB |

That is 8 variants per surface at 4 bytes a texel. The atlas it replaces is 16 MB. Render tiles stay 524 KB a chunk and the macro texture adds 264 KB, so 8.3 MiB over the 32-chunk LRU.

Measured with today's content (every def's 8 variants, mips 0 to 6): 56.0 MiB, 42.7 MiB for the grass family's 32 layers and 13.3 MiB for the other five surfaces' 40. With the placeholders it was 21.3 MiB.

Driver-side BC1 (S3TC) on upload would be 8x smaller (about 7 MiB) and cost less bandwidth per sample. It gets measured in the bake and shader step and stays only if it's neutral or better and looks right.

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

Order of measurement: baseline main twice (the noise floor), profile today's pass before changing it, measure each change alone against the baseline so a regression has a name, then a final capture with real content before the PR. The tables go into this doc and the dev-log entry, and the JSON sits beside the WOR-463 captures in `perf-results/`.

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
- Variety: screenshots at zoom 1, 3 and 20 on Grass, a GrassTall patch and a Dirt edge. No visible 1 m or 4 m repeat, and the fringe and rim still read at surface boundaries.
- LOD: zoom out step by step over GrassMeadow. Flower specks tagged `data-lod` fade near their threshold, with no muddy tint left behind and no popping.
- Asset-manager: the ground category lists all 9 land surfaces, the 3x3 preview shows no seam, an edit to a generator param followed by Reload shows the change, and validation is clean.
- Unit tests: validator rules, periodic noise wraps, `data-lod` inheritance and per-band filtering, a mesh over 65,535 vertices splits, paths built once match a generator run per density, a bake on the pool matches one in turn bit for bit, and the bake cache's round trip, key and damaged files. `SurfaceFieldGolden` and the existing suite pass.
- Performance: the final capture per the gates above.
- Manual script: `docs/testing/scenarios/ground-tiles.md`, one numbered line a step, written at hand-off.

## Open questions

- Linear art across hex cells: Sand's ripples and Snow's drifts break where two cells meet, a vertical seam through the crests at zoom 20. Cells take a random offset each way, so rows can't line up; options are crests broken often enough that a cell border reads as one more break, or a per-def offset snapped to the crest spacing along y (a shader and schema change).
- BC1 on upload: neutral or better, and does it look right on dense blade art.
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
