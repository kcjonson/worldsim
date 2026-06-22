# 2026-06-22 - Hydrology ponds + oasis, proper forests, riparian plants

## Summary

Replaced the noise-flood ponds with sparse, hydrology/biome-driven ponds (plus desert
oases), made `Distribution::Spaced` actually space (it was a dead no-op), shaped forests
into dense stands with open glades and rarer extra-dense thickets, and fixed waterside
plants so reeds line the mud bank and bushes line forest riverbanks.

## Part A: hydrology ponds + oasis

- The biome generators painted `Surface::Water` from per-tile fractal noise
  (`GrasslandGenerator` >0.82 ≈ 18%, `ForestGenerator` >0.85 ≈ 15%) -- far too many,
  everywhere, unrelated to hydrology. Deleted those branches; `WetlandGenerator` and
  `OceanGenerator` (Ocean/Lake biomes) keep their water.
- New `worldgen::PondNetwork2D` (`libs/world/worldgen/sampling/PondNetwork2D.{h,cpp}`),
  modeled on `RiverNetwork2D` (deterministic, seam-safe, self-contained). Ponds sit on a
  ~380 m lattice so density is ~0-2 per chunk, not a flood. Each cell's spawn chance is
  weighted by the 3D biome + precipitation sampled at that position (wetland > forest >
  grassland; desert/tundra ≈ none).
- **Oasis = a pond, not a separate thing.** There is one `Pond` type with two spawn
  triggers: the common biome/precip-weighted one, and -- when a cell is far (past a
  biome-scaled threshold, via a bounded ring BFS) from any 3D water -- a rare spring-fed
  pond, which in a desert reads as an oasis. Same struct, same depth, sized by local
  wetness (so a desert spring is a small pool).
- Geometry is a closed wobble-edged blob (radius modulated by two angle sinusoids), a
  pure function of cell + seed → identical across chunk seams. Plumbed like rivers:
  `ChunkSampleResult::pondBlobs` + `pondDepthAt`, gathered in `GeneratedWorldSampler`
  (under a Precipitation+Biome guard so old saves degrade to no ponds), rasterized in
  `Chunk::computeTile` after the river override (rivers win; ponds only convert land).

## Part B: proper forests

- **`Distribution::Spaced` implemented.** It was a `// TODO` falling through to Uniform,
  so `minDistance` did nothing and "spaced" trees placed randomly. Now it is jittered
  dart-throwing with a `SpatialIndex::hasNearby` rejection. Oak/Maple/Palm/Pine already
  requested `spaced`.
- **Grove field → glades, forest, thickets.** A low-frequency, domain-warped two-octave
  value-noise field (in `PlacementExecutor`) varies spaced flora across three tiers from
  one field: its low tail thins to open **glades** (via spawn chance), its high tail
  tightens spacing into rarer extra-dense **thickets** (via a `minDistance` scale), and
  the broad middle is normal dense forest. Domain warp keeps the shapes organic (no
  square, grid-aligned patches).
- **Denser forest trees.** Tightened spacing for a fuller canopy: Pine boreal/temperate-
  rainforest 3.0→2.4 m, montane 0.6→0.7 chance @ 3.5→3.0 m; Oak 0.03→0.25 @ 6→3 m; Maple
  0.05→0.22 @ 5→2.8 m; dropped the redundant same-species `avoids` (minDistance owns it).

## Part B: riparian plants

- The mechanic was never broken (a placement test confirms `near=` flora cluster at a
  waterline); the bare banks were a placement detail. `TilePostProcessor` turns the bank
  into `Surface::Mud` before placement reads surfaces, and `near="Water" distance="3"`
  let reeds sit up to 3 tiles out on grass.
- Fix: all reed blocks now use **`near="Mud" distance="1"`** so reeds hug the muddy
  waterline, with forest chances bumped. Mud forms around rivers, ponds, and oases alike,
  so reeds ring all of them.
- Added bankside **bushes**: BerryBush and WoodyBush gained `near="Mud"` bank blocks in
  BorealForest, MontaneForest, and the tropical forests (biomes where they had no upland
  presence), so forest riverbanks/pond shores grow berries and woody bushes set a step
  behind the reeds -- where most plants grow.

## Tests

`PondNetwork2D.test.cpp` (wet basin → pond, desert-far → oasis, desert-near-water → far
fewer, count bounded, determinism, seam-straddling agreement, biome gating, depth in/out).
`PlacementExecutor.test.cpp` (Spaced honors the spacing floor; riparian flora clusters
near water). Full suite green: 167 world-tests, 714 engine-tests.

## Notes / follow-ups

- The 3D drainage grid is coarse (~tens of km/tile), so literal per-sink ponds would be
  far too sparse for 0-2/chunk; the lattice weighted by sampled biome/precipitation is the
  scale-appropriate way to stay hydrology-influenced. The oasis distance BFS is ring-capped
  and only runs for dry cells that produced no natural pond.
- Not in scope: cross-chunk Spaced spacing (per-chunk for now), pond inlet/outlet streams,
  water-loving riparian trees, strictly-on-mud reeds (a tile of grass adjacent to mud can
  still grow one).
