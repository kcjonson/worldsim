# 2026-11-02 - Ground tile variants: baked, hex-mixed ground arrays (WOR-545)

## Summary

The ground used to be nine 512x512 patterns in a texture atlas, stamped on every 1 m tile. That drew faint lines on tile edges while panning (linear filtering across packed atlas cells, no gutter, no mips) and repeated visibly. Every land surface is now a Lua-generated ground def with 8 variants, baked once at load into mipmapped texture arrays and mixed per pixel by a hex lattice in `land.glsl`. The atlas is deleted. All nine surfaces have real art, and the bake is cached on disk. Merged as #332, carried by #333.

## Details

Assets and Lua:
- New `ground` asset role, defs at `assets/world/ground/<Surface>/<Surface>.xml` (variant count, period, density, rotate mode, macro tints), validated in `AssetValidator.cpp`; limits live in `GroundLayer.h`.
- Lua gets `noise2`, `periodicNoise2` and `path:setLod`. `tessellateAsset` split meshes past 65,535 vertices, where it used to wrap its `uint16` indices without a word. `AssetRegistry::buildPaths` repeats any path that crosses the tile edge, so authors never match edges by hand.
- Two generators paint the nine surfaces: `ground_grass.lua` (the grass family) and `ground_bare.lua` (Dirt, Sand, Mud, Rock, Snow).

Baker and bank (`libs/world-view/world/rendering/`):
- `GroundTileBaker` runs each generator once per variant on the pool, then renders every mip from the vectors (LOD is baked into the chain), supersamples, Kaiser-downsamples in linear light, mean-locks and sRGB-encodes. `GroundTileBank` uploads into two `GL_TEXTURE_2D_ARRAY`s by layer size (grass family 128 px/m, the rest 64), 56.0 MiB in all, one step a call so the splash keeps drawing.
- `GroundTileCache` keeps one file a def beside the executable, keyed on the def as loaded, its script, the render tile size and `GroundTileBaker::kVersion`.
- A per-chunk 257x257 macro texture (264 KB, filled on the chunk worker, 3.0 ms median) carries the dry-to-lush tint and value drift, in place of per-pixel noise.

Shader:
- Stochastic hex tiling on a lattice that repeats every 512 m, one sample where a cell dominates and 2 or 3 in blend bands, `textureLod` at a mip computed once per draw on the CPU. Below 16 px/m it takes the dominant cell's sample. The tile shader compiles as GLSL 4.10.

Tools:
- Asset-manager gets a 3x3 wrapped preview, a variant grid, a Bake view, and thumbnails drawn in a fixed tile frame.
- The in-game GPU hold (WOR-553) draws throwaway ballast after the scene's GPU timer closes, so perf captures sit at P0 without anyone locking clocks.

Decisions:
- Defs are procedural only; an SVG ground def is a validation error until the `data-lod` loader exists.
- Art lit from the top of the screen uses `mirror` rotation (flipped left to right, never turned), which is every def.
- BC1 is out: heights (next entry) make alpha real data. BC3 or BC7 is about 14 MiB against 56 MiB raw and is still unmeasured.
- The atlas path went in the same change, so there is one way to paint ground.

Perf, RTX 3090, 3072x1728, GPU held at P0, 27 cells (3 views, idle zoom 20 to 0.25, two scrolls):
- First full build read 0.04 to 0.11 ms of `tileGpuMs` over main in every cell. The win-back was a sampler kept off the GPU's conversion unit, a cheaper land field, no ground evaluated under open water, and hex blending only from 16 px/m.
- At the close of this epic, every cell was within +0.01 ms of main, 24 of 27 lower, median -0.04. With blending on top the merged branch reads at or under main in 26 of 27 cells, desert idle zoom 3 at 0.58 against main's 0.56 (see the blending entry).
- Bake: the first version, a variant at a time through `buildMeshes`, added 20.6 s to the splash with only Grass real and eight placeholders. One generator run a variant, on the pool, took that cold run to 4.5 s. With round 1's art a cold bake is 18.3 s (about 21 s with round 2), and a warm cache is 33 to 37 ms.

Review: the PR's review (#332) tightened one layer-size rule shared by the validator, the baker and the bank, rejected SVG ground defs at validation, and stopped `periodicNoise2` overflowing int32 near `INT32_MAX`. The 5-agent review of the merged work is in the blending entry.

Files: `libs/assets/assets/GroundLayer.h`, `GroundTint.cpp`, `AssetValidator.cpp`; `libs/world-view/world/rendering/GroundTileBaker.cpp`, `GroundTileBank.cpp`, `GroundTileCache.cpp`, `shaders/includes/land.glsl`; `libs/world/world/chunk/GroundMacro.cpp`; `assets/shared/scripts/ground_grass.lua`, `ground_bare.lua`; `apps/asset-manager/GroundBakeView.cpp`; `apps/world-sim/scenes/game/dev/GpuHold.cpp`.

## Related Documentation

- `/docs/technical/ground-textures.md` (assets, baker, shader, perf ledger)
- `/docs/technical/organic-terrain/terrain-polygons-architecture.md` (D13, D16)
- `/docs/testing/scenarios/09-ground-tiles.md`
- Plan: `/docs/development-log/plans/2026-11-02-ground-tile-variants.md`
- Next entry: `2026-11-02-ground-surface-blending.md`

## Next Steps

Ground Tile Follow-ups in status.md: the `data-lod` loader (needs a salal load-from-string addition), BC3 or BC7, the golden test on CI, a 4.1 core context from salal, a per-user bake cache directory, a salal tag in the cache key, and the asset-manager Bake view's speed.
