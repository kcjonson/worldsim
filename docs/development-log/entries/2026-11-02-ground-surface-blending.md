# 2026-11-02 - Ground surface blending: height-contested edges and organic biome edges (WOR-556)

## Summary

Where two land surfaces met, the old look was a thin fringe of the upper surface faded over the lower one, a dark rim on Rock and a 1 px anti-aliasing line. Now the ground arrays' alpha carries a baked height per texel, and in a band around the land field's isoline whichever surface stands taller at each texel shows: tufts over dirt, sand filling the joints in rock, snow settling in hollows. The fringe, rim and anti-aliasing line are deleted. Upstream, a tile's biome is picked per tile with world-space noise, so coasts and biome boundaries have bays, headlands and islands where they used to be a 16 m staircase. Rock, Mud, Sand and Snow were redrawn to suit. Merged as #333, which carries #332's commits.

## Details

Biome edges (WOR-558):
- `TileBiomes.h` and `ChunkBiomes` pick each tile's biome from corner weights interpolated at the tile's center, raised by gradient fBm (200, 50 and 12 m) plus an ecotone band that leaves 3 to 10 m islands. The sector grid is gone. The pick is pure in world tile and seed, so apron and seam tests pass unchanged.
- The generators' patch noise became gradient noise hashed once a chunk (`generation::PatchNoise`), and `NoiseUtils::valueNoise` was deleted. Chunk generation got cheaper: median 55 ms against 67.
- The longer shores roughly doubled waterline and nav ring vertices on coasts and lakes. Measured, not trimmed (WOR-563): a coast chunk generates in 113 ms on the worker against 86, its water-only nav regions build in 0.3 ms against 0.1, and the frame is untouched because water draws from the distance field. The 100 mm ring tolerance stays.

Heights (WOR-559):
- `path:setHeight`, defaulting to paint-order rank. A second render at mip 0 draws the heights, which are rank-normalized per surface to a uniform histogram and stored in alpha as plain data (`GroundTileBaker::kVersion` 3). Mips are filtered heights with a window that widens per mip, which beat per-mip equalization on a supersampled reference (zoom loss 0.032 against 0.103).
- The grass family co-bakes from one `<seedGroup>`, so variant k of each member shares a layout and the hex hash picks alike across a family edge.

Contest (WOR-560, WOR-569):
- `landColor` in `land.glsl` mixes the pair's two surfaces by their hex-blended heights, biased by the signed distance to the isoline. Each pair's width, offset, softness, breakup and four shading strengths come from the ground defs' `<transition>` blocks, packed on the CPU into a triangle of 36 pairs (`GroundPairs.cpp`) and kept out of the bake key, so tuning never re-bakes. A band is capped at 1.5 m, the field's reach.
- The band's distance comes from the field's values, not the gradient that stepped past 0.5 m. At three-surface junctions it follows the paint rule's lead, which Kevin's manual test found as a sliver of sand and a detached line of tufts out in the desert (WOR-569).
- The tile shader compiles as GLSL 4.10 so `textureGather` is core on macOS (WOR-565).

Art round 2 (WOR-561): weathered Rock with a few joints and lichen colonies, small sky-tinted Mud puddles, short Sand ripple fragments and wind streaks, quiet wind-packed Snow, all in `ground_bare.lua`, with pair recipes retuned on the real edges. The grass family's band is 0.2 m.

Perf, same method as the variants entry (RTX 3090, 3072x1728, P0, 27 cells):
- Final `tileGpuMs` is at or under main in 26 of 27 cells. Desert idle zoom 3 is the exception at 0.58 against main's 0.56, where WOR-558's organic edges put many more pixels on the field's full path. It read +0.06 after the contest and +0.04 after round 2, and a second near-interior test after the fine warp's first octave (WOR-564, bit for bit with the C++ twin) brought it to +0.01 to +0.02. Everything is under 1.5 ms.
- Cold bake about 21 s (21.1 to 21.3 across runs; the height pass alone is +10%), warm 33 to 37 ms, arrays still 56.0 MiB.
- Frame p50, p99 and scene GPU match main within run-to-run swing.

The 5-agent review. Fixes landed for:
- a biome pick that read past its candidates at a rounding tie (the prune and the settled test compared different expressions), now one lead with a regression tile;
- the bake cache key, which hashed the XML on disk rather than the def as loaded, and file names that could collide or leave the directory;
- two bake paths: every scene now goes through `stepGroundBake`, and a step that throws or that GL refuses fails its def instead of rerunning each frame;
- the pair table, stored as a triangle (72 vec4, was 162) so the shader's uniforms fit GL 4.1's guaranteed 1024 components;
- the shader's 1.5 x 2^23 bias, which is exact only in source order, so it no longer meets a sum a compiler could fold it through (`float()`, a floor, a mantissa; `precise` held the order too but cost 0.02 to 0.05 ms);
- the golden test, which now renders regions over negative tiles;
- validation of macro tints, variant counts and layer sizes, shared by the loader, the validator and the baker.

Smaller review items: a ground def's `<period>` is passed to its generator, thumbnails build at screen density in a bounded cache, and dead generation parameters were deleted.

Testing: `docs/testing/scenarios/09-ground-tiles.md`. The regression pass ran scenarios 01 to 04, 06 and 08 clean; WOR-566, WOR-567 and WOR-568 were filed, outside this branch's code. Kevin tested by hand and approved on 2026-11-02.

## Related Documentation

- `/docs/technical/ground-textures.md` (Surface transitions, Perf ledger)
- `/docs/technical/organic-terrain/terrain-polygons-architecture.md` (D13, D15, D16, biome edges, section 6)
- Plan: `/docs/development-log/plans/2026-11-02-ground-blending.md`
- Previous entry: `2026-11-02-ground-tile-variants.md`

## Next Steps

Ground Tile Follow-ups in status.md. Separately, desert zoom 3 is the one cell over main; what is left in it is the field's six noise octaves, which must stay bit for bit with the C++ twin.
