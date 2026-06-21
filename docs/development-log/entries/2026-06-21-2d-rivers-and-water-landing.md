# 2026-06-21 - 2D rivers + water-biased landing + conifers

## Summary

The 3D drainage network now reaches the 2D world, and the colony lands somewhere
it can survive. Three pieces:

1. **2D rivers.** Worldgen stays coarse (per spherical tile it stores only water
   *amount* `flowAccum` and drainage *direction* `downhill`, plus `kFlagRiver`).
   The 2D layer synthesizes the actual channel at chunk load: `RiverNetwork2D`
   walks the `downhill` graph into 2D polylines via the landing projection,
   meanders them, sizes each by flow (hydraulic geometry, clamped 3-140 m), and
   the chunk generator rasterizes them to `Surface::Water`. This is the
   "distance-field along the coarse downhill polyline" model the water-availability
   epic deferred to chunk time.
2. **Water-biased, non-desert landing.** `findDefaultLandingSite` scores temperate
   *green* tiles (forest/grassland/savanna/wetland; beach/desert/tundra excluded)
   by clean-water proximity plus a habitability rating, preferring a river running
   *through* the tile -- that tile's center is the 2D origin, so the channel passes
   through spawn at any resolution. `findRiverbankSpawn` then drops the colonist on
   dry land beside the water. Quick Start went from a SemiDesert/Beach river-mouth
   to BorealForest with the colonist ~5 m from fresh water.
3. **Conifers.** New `conifer.lua` generator + `PineTree` asset fill BorealForest,
   MontaneForest, and TemperateRainforest, which had no flora configured.

## Details

New:
- `libs/world/worldgen/sampling/RiverNetwork2D.{h,cpp}` (+ `.test.cpp`) -- channel
  synthesis from the drainage graph; deterministic meander + flow-based width;
  per-chunk segment gather via a bounded local BFS (no global index).
- `libs/world/worldgen/sampling/SpawnSite.{h,cpp}` (+ `.test.cpp`) --
  `findRiverbankSpawn`: closest dry-land cell beside clean water (riverbank > lake
  shore > coast).
- `libs/engine/world/chunk/GeneratedWorldSampler.test.cpp` -- integration test:
  water appears in a generated chunk iff drainage is present.
- `assets/shared/scripts/conifer.lua`, `assets/world/flora/PineTree/PineTree.xml`.

Modified:
- `GeneratedWorldSampler` (owns an optional `RiverNetwork2D`; gathers per-chunk
  segments into `ChunkSampleResult`), `ChunkSampleResult` (carries segments +
  `isRiverAt`), `Chunk::computeTile` (river override ahead of the biome surface).
- `LandingSite.{h,cpp}` -- green-biome + water + habitability scoring.
- `GameWorldState`/`GameLoadingScene`/`GameScene` -- thread `spawnPosition` through.

Decisions / notes:
- River geometry is a deterministic function of (drainage graph + world position),
  computed at chunk load; nothing fine-grained is stored in worldgen.
- Continuity across chunk seams is automatic: each segment's meander vanishes at
  the shared tile centers, so neighbours rasterize the identical curve.
- Tree density is capped by the placement sampler's stride; `Distribution::Spaced`
  still falls through to Uniform (Poisson disk is the lever for dense forests).

Tests: world 154 fast + engine 711, all green. Verified in the sandbox.

## Related Documentation

- `docs/development-log/entries/2026-06-15-worldgen-water-and-plate-realism.md` -- the
  coarse water model this builds on (W-series).

## Next Steps

- River realism: more wander within a chunk, width that starts small and grows
  downstream (in progress).
- Poisson-disk placement for genuinely dense forests; flora for the remaining bare
  biomes (savanna, alpine grassland, tropical forests beyond palms).
- Hydrology-only ponds (replace the per-tile noise water).
