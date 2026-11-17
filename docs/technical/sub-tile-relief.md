# Sub-Tile Relief: Terrain Detail That Reads as Geology

**Status:** Draft (planned, not implemented)
**Created:** 2026-11-17
**Epic:** Sub-tile relief that follows structure and erosion (see Specboard)
**Related:** [3d-to-2d-sampling.md](./3d-to-2d-sampling.md) (Elevation Sampling, Procedural Detail Generation), [planet-view-rendering.md](./planet-view-rendering.md) (detail page cache), `libs/worldgen/worldgen/sampling/PlanetSampler.h`, `libs/world/world/chunk/GeneratedWorldSampler.h`

## Problem

A worldgen tile at n=2048 is a few kilometres across. Below that, elevation comes from interpolation plus isotropic noise. Isotropic noise looks like noise: ranges have no grain, slopes have no gullies, and nothing in the detail says which way the mountains run or which way water flows. This shows wherever sub-tile relief is drawn or used: the globe's deep zoom, the landing's mountain and rock surfaces, river paths at landing scale.

## Goal

Detail added below tile resolution follows the structure the planet already has:

1. **Strike-aligned ridges.** Inside orogenic belts, ridge noise is stretched along the belt's strike (the reference implementation used about 8x), so ranges are made of parallel ridges and valleys, not blobs. The strike direction comes from the tectonic history (the boundary orientation at the time of the orogeny), carried to full resolution by CrustStage.
2. **Massifs.** A low-frequency fractal mask breaks long belts into massifs with saddles between them, so a range has peaks and passes rather than a uniform wall.
3. **Erosion-looking detail.** An erosion filter (in the style of Rune Skovbo Johansen's, which sums oriented stripe patterns aligned to the local downhill gradient, branching across octaves) adds gully and spur texture that points downhill, so fine relief reads as water-carved.

## Non-Earth requirement

Detail strength is scaled by the planet's own processes: stretch and ridge amplitude from orogeny age and intensity; erosion-filter strength from the tile's precipitation and the planet's erosion rate (a dry or airless world gets crisp, unweathered relief; a wet world gets deep dissection). No Earth relief statistics are hard-coded.

## Where it runs

One function in worldgen sampling (`PlanetSampler`) evaluates sub-tile elevation at a position; every consumer calls it (one path rule). It must be a pure function of position and seed so chunks agree across seams with nothing stored, matching RiverNetwork2D and PondNetwork2D.

First task is an audit: list every place sub-tile elevation is produced or consumed today (globe detail pages, chunk corners, mountain generator, river and pond placement) and confirm they can all move onto the one function.

## Data needed from worldgen

A per-tile strike direction (u8 angle) and orogeny intensity, stored in `WorldData`. PlanetIO version bumps.

## Performance

Evaluated on the chunk generation worker. The erosion filter's octave count is the main cost lever; the spike sets a per-chunk budget against the current chunk generation benchmark before the filter is enabled everywhere. Follow the repo's timed-perf rules (machine-load check, `perf.mjs compare`) for any claims.

## Out of scope

- Simulated erosion at landing scale (this is a filter, not a simulation).
- Caves, overhangs, cliffs as geometry.

## Reference

Strike-stretched ridge noise, fractal massif masks, and the use of an erosion filter for view-scale detail come from World Orogen (orogen.studio, GPL-3.0). Ideas only; no code is copied.
