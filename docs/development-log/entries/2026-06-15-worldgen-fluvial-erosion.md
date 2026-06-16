# 2026-06-15 - Fluvial erosion (stream-power valley carving)

## Summary

Step 3 of the water roadmap (drainage fix -> water signal -> **erosion** -> 2D river rendering): a new `ErosionStage` carves valleys into the continental terrain so rivers, when rendered later from the climate drainage, land in real valleys instead of running across flat platforms. Detachment-limited stream-power incision solved with the Braun & Willett (2013) implicit scheme. PR [#149](https://github.com/kcjonson/worldsim/pull/149).

## Details

### Where it runs and why

A new stage between **Terrain and Atmosphere**. It rewrites `WorldData.elevation`, so everything downstream (atmosphere, precipitation, ocean, biomes, and the *final* climate-weighted `flowAccum`/`downhill`) runs on the carved terrain. The rivers the later 2D layer draws will follow the real drainage through valleys that already exist. No re-render, no chicken-and-egg.

### The drainage circularity

Erosion needs drainage; drainage needs elevation; the real climate precipitation isn't computed until the later stage. Resolved by giving ErosionStage its own **provisional, uniform-rainfall drainage** (upstream area accumulated under unit rainfall via the priority-flood routing), which drives the carving. The real climate-weighted `flowAccum` is recomputed downstream by PrecipitationStage on the eroded terrain.

### Method

Detachment-limited stream power, `dh/dt = -K * A^m * S` with `m = 0.5` (so `A^m = sqrt(A)`, which `det_math` evaluates exactly), `S` the slope to the receiver. Solved with the **Braun & Willett (2013) implicit O(n) scheme**: process nodes in drainage-stack order (receiver before tile), implicit-Euler, so it's unconditionally stable and a handful of sweeps suffice. The stack comes for free from the priority-flood: in `(filled ascending, TileId ascending)` order a tile's receiver was always popped earlier, so it precedes the tile. Base level is sea level; channels stay monotone downhill and never incise below sea level (no new ocean); a per-tile incision cap bounds how deep a belt-crossing channel can cut. Drainage area in km^2 keeps the result resolution-invariant.

Because channels (large drainage area) incise much faster than ridges and divides (small area), valleys deepen while the orogenic belt crests are preserved -- dissection, not flattening. The strength (incision sweeps) is the tunable knob; the sweeps are nearly free (the priority-flood dominates the cost), so it dials cheaply.

### Shared drainage routing (One-Path)

The priority-flood depression routing (Barnes 2014) was extracted out of `PrecipitationStage` into a new shared `DrainageRouting::routeDepressions` helper, used by both the erosion (provisional) drainage and the climate drainage. The refactor is **byte-identical** -- worldHash was verified unchanged before the erosion stage was added.

### Measurement

`WorldStats` + `worldgen-cli` gained dissection metrics: hypsometric integral, mean local relief, drainage density, fraction of land near a channel, and mean belt-crest elevation (the guard that erosion must not flatten the mountains).

### Files

- New: `worldgen/stages/ErosionStage.{h,cpp}`, `worldgen/stages/DrainageRouting.{h,cpp}`.
- Modified: `PrecipitationStage.cpp` (routing extracted to the helper), `pipeline/PlanetGenerator.cpp` (stage registered) + `.test.cpp` (stage count), `debug/WorldStats.{h,cpp,test.cpp}`, `apps/worldgen-cli/Main.cpp`, `libs/world/CMakeLists.txt`.

## Verification

- Determinism: worldHash bit-identical across thread counts (single-threaded, drainage-stack order, `det_math` only).
- Full `world-tests` green (174); downstream acceptance (hypsometry bimodality, biome fractions, drainage) stays in range on the carved terrain.
- The mechanism genuinely dissects: local relief and channel coverage rise with erosion while belt crests stay tall.
- Gen ~22s at n=1024 (within the 10-30s budget).

## Related Documentation

- `.claude/plans/erosion.md` -- the erosion plan and the chunk-time-rendering follow-up
- `/docs/status.md` -- Fluvial Erosion under In Progress

## Next Steps

- Mark PR #149 ready, clear CI + Copilot, merge.
- **Bathymetry "comb" artifact** (separate, pre-existing): the continental-shelf ramp and the crust-age depth field inherit hex-BFS distance terracing as comb "valleys" along coasts and trenches. Investigated during this work but deliberately deferred -- it is its own focused tuning task (shelf-edge terracing + crust-age depth steps + the shelf/sea-level/mountain test interactions), not erosion.
- **2D chunk-time river/lake rendering** -- draw rivers/lakes in the local tile map as a deterministic distance-field along the coarse `downhill` polyline + basin fill, now landing in the carved valleys.
