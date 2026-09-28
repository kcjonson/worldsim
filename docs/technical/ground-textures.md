# Ground Texture System

Created: 2025-12-13
Updated: 2026-09-26 (WOR-462: surface edges come from the D16 land field; the tile-adjacency edge masks and surface-family hard/soft edges this doc once planned are gone)
Status: Implemented

The ground is painted by the tile pass: one quad per chunk whose fragment shader picks a land surface for every pixel, samples that surface's pattern from a texture atlas, and paints water and shore over the result. Which surface a pixel gets is decided by the land field of [terrain-polygons-architecture.md](./organic-terrain/terrain-polygons-architecture.md) D16, so no surface boundary follows the tile grid. This doc covers the patterns, the atlas, and the per-chunk data the pass reads, and summarizes the field; the spec holds the field's details and defaults.

## Layers

| Layer | What | Drawn by |
|-------|------|----------|
| Ground | each surface's pattern, the surface chosen per pixel by the land field | tile pass: `tile.frag`, `includes/land.glsl` |
| Water and shore | from the chunk's terrain distance field (D10) | tile pass: `includes/water.glsl` |
| Grass tufts | `Groundcover_Grass`, a procedural Lua tuft, GPU-instanced | `GroundcoverRenderer` |
| Flora and other entities | vector assets | `EntityRenderer` |

This is the original plan's two tiers as they shipped: the patterns are the static micro-detail (Tier 1 in [the vector-graphics architecture](./vector-graphics/INDEX.md)), and the grass tufts and flora are live vectors over them.

## Surface patterns

Each land surface has a tileable SVG at `assets/tiles/surfaces/<Surface>/pattern.svg`, where `<Surface>` is the name `surfaceToString` gives it, with a 512x512 viewBox: a base color, low-opacity organic color patches, then details such as pebbles, dried grass, and cracks. `Dirt/pattern.svg` is the fullest example.

| Surface | D13 level | Pattern |
|---------|-----------|---------|
| Mud | 1 | none, fallback checker |
| Sand | 2 | none, fallback checker |
| Dirt | 3 | yes |
| GrassShort | 4 | yes |
| Grass | 5 | yes |
| GrassMeadow | 6 | yes |
| GrassTall | 7 | yes |
| Rock | 8 | none, fallback checker |
| Snow | 9 | none, fallback checker |
| Water | not land | never sampled |

A surface without a pattern gets a checker of its `Chunk::getSurfaceColor` (8-texel squares at 1.05x and 0.85x). The land pass never samples Water's slot: a Water tile paints as its bed, and water itself is drawn from the distance field.

The shader stretches a pattern over each 1 m tile (the atlas UV is the point's offset within its tile), so it repeats every meter. The comments in the pattern files say a pattern covers 8x8 tiles; that was the plan, not what the shader does (see open questions). Near an edge, the fringe samples the upper surface's pattern at the same offset.

## Atlas

`AppLauncher` builds it once at startup through `Renderer::TileAtlasBuilder`. Each surface's SVG is rasterized on the CPU by NanoSVG's rasterizer (`bakeSvgToRgba` in `TilePatternBaker.cpp`) to 512x512 RGBA8 and shelf-packed into a 2048x2048 `TileTextureAtlas`: 16 slots, 10 used, 16 MB. The texture is linear-filtered and clamped, with no mipmaps; `land.glsl` samples level 0. The texture and each surface's UV rect reach the tile pass through `Renderer::Primitives::setTileAtlas`, as `u_tileAtlas` and `u_tileAtlasRects`. The atlas doesn't go through `RenderToTexture`, the FBO wrapper the asset renderer draws with.

## Render tiles

A chunk's generation worker builds its render tiles (`Chunk::computeRenderData`, with `paintSurfaces` and `buildRenderTiles` from `SurfaceField.cpp`): 518x518 two-byte tiles covering the chunk square plus `kRenderApronTiles` = 3 on every side, uploaded as one RG8UI texture.

- R, the paint surface: the tile's final surface, or for a Water tile its bed, the surface of the nearest land tile within 3 tiles (Sand when there is none).
- G, the edge byte: the edge surface in the low nibble (the highest surface within 2 tiles, whose warp amplitudes the tile takes) and the interior bit, 0x80, set when every tile within 3 is one surface.

Apron tiles come from the chunk's own apron, post-processed the way the neighbor post-processes its own tiles (D11), so both sides of a border read the same tiles and draw the same field. `ChunkRenderer` keeps an LRU cache of 32 chunks' textures and re-uploads a chunk's when its `renderDataVersion` changes. Render tiles cost 524 KB a chunk; the RGBA32UI tile data they replaced, with neighbor ids and edge masks, was 4 MB.

## The land field

A summary; D16 in the spec is the definition.

1. For each surface among the 4x4 tiles around a point, its indicator at tile centers is blurred with the 3x3 binomial, guarded so 1-tile patches and 1-wide paths survive (the axis rule, floor 0.85, ceiling 0.15), and read bilinearly between tile centers.
2. Every surface is read at one warped point: world-space vector fBm, a fine term (4 m base, 4 octaves) plus a low term (26 m base, 2 octaves), with amplitudes by surface blended from the tiles' edge surfaces (rock's are smaller, so its edges stay harder), each component clamped to 1.5 m.
3. Surfaces paint low to high (D13): the highest whose field passes 0.5, else the one with the largest field.
4. The edge is anti-aliased over one pixel from `u_metersPerPixel`, with a sparse fringe of the upper surface on the lower side whose width follows the warp noise along the edge, and a rim darkening just inside the upper surface (rock only by default). A pixel in an interior tile costs one render-tile fetch and one atlas sample.

`engine::world::evaluateSurfaceField` is the one evaluation. `land.glsl` does the same steps in the same order, and `SurfaceFieldGolden.test.cpp` renders the shader and compares the two. Placement asks the C++ evaluation, so flora grows on the surface that is drawn.

## Tunables

All are live through the debug server: set one with `/api/dev/tunable?name=<name>&value=<v>`, read them with `/api/state?what=tunables`. Defaults are in section 4 of the spec.

| Name | What |
|------|------|
| `terrain/land/warpFine/<Surface>`, `warpLow/<Surface>` | warp amplitudes in meters, by the edge's upper surface |
| `terrain/land/warpFineWavelength`, `warpLowWavelength` | base wavelengths, whole meters |
| `terrain/land/warpFineGain` | amplitude the fine term keeps per octave |
| `terrain/land/thinFloor`, `thinCeil` | the thin-feature guard |
| `terrain/land/fringe/<Surface>`, `rimDark/<Surface>` | fringe width in meters and rim darkening, by upper surface |
| `terrain/land/fringeOpacity`, `fringeAlongAmp`, `rimW`, `fringeBreakupWavelength` | the fringe's strength, its variation along the edge and its breakup, and the rim's width |

## Cost

- Atlas: 16 MB, built once at startup.
- Render tiles: 524 KB a chunk, on the GPU and in the chunk.
- Land pass at 3072x1728 on an RTX 3090, in game: 0.3 to 0.9 ms on a view of mostly interior tiles, 0.6 to 1.3 ms on busy ones; with the water on top the pass stays under 1.5 ms at every zoom, and at far zoom the shader drops the warp octaves a pixel cannot show. Measured tables in the spec, section 6 and D16 step 7.

## Open questions

- Patterns for Mud, Sand, Rock, and Snow, which draw the fallback checker.
- A pattern repeats every tile rather than every 8 tiles as drawn, and at close zoom its patches read as a 1 m grid of spots. Sampling at 8 m coverage would use the whole-tile-plus-offset split the field already uses for precision.
- The atlas has no mipmaps, so at far zoom a meter's 512 texels land on a few pixels.

## Related documentation

- [Terrain Polygons Architecture](./organic-terrain/terrain-polygons-architecture.md): D10 (water), D13 (surface order), D16 (the land field)
- [Vector Graphics Architecture](./vector-graphics/INDEX.md): Tier 1/3 system
- [Visual Style](../design/visual-style.md): art direction
- [Entity Placement System](./entity-placement-system.md): flora and groundcover placement
- [Asset System](./asset-system/README.md): asset definitions

## Rimworld research sources

- [Rimworld-style tilemap shader (Godot)](https://godotshaders.com/shader/rimworld-style-tilemap-shader-with-tutorial-video/)
- [RimWorld Wiki - Mod Textures](https://rimworldwiki.com/wiki/Modding_Tutorials/Textures)
