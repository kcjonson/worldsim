# Organic Terrain Geometry

**Date:** 2026-09-26
**Epic:** Organic Terrain Geometry (Specboard WOR-455)
**PRs:** #268 (research), #269 (spec), #274 (WOR-457), #275 (WOR-458), #276 (WOR-465), #277 (WOR-459), #278 (WOR-460), #281 (WOR-461), #282 (WOR-462), and the WOR-463 perf PR that carries this entry

## Summary

Terrain stopped reading as a 1 m tile grid. Every chunk builds one set of terrain polygons on its generation worker: the biome waterline as the isoline of a softened, domain-warped field; a ribbon per river channel stroked from RiverNetwork2D's segments at true width; an outline per pond. Those rings are the runtime truth for water. Nav blocks on them, the renderer paints water and shore per pixel from a distance field baked from them, vision drinks at points sampled along them, and mud banks and point bars form by distance to them. Then the same treatment went to every land-on-land boundary: grass variants, dirt, sand, rock, snow, and mud meet along the isoline of a warped per-surface field, drawn in the shader and evaluated once in C++ so placement agrees with the paint. A `Water` tile is no longer the answer to "can I stand here" or "is this shore"; `TileData::surface` is generation input and a coarse prefilter.

## What was built, by task

- **WOR-457, waterline rings (#274).** `libs/geometry/contour/`: marching squares on the dual grid with exact loop linking, the warp-field resample, Chaikin, world-lattice pins (16 m), per-run resample and simplify, ring clip. `TerrainPolygonBuilder` builds waterline rings over the chunk plus a 20-tile apron that `ApronField` computes from the chunk's own sample data, never from neighbor chunks. Rings are stored twice: unclipped over the extended region for every consumer, and clipped to the chunk square for nav.
- **WOR-458, river and pond ribbons (#275).** Catmull-Rom centerlines at 0.5 m, linear half-width with no floor (`kRenderMinHalf` deleted), curvature-driven bank asymmetry, bank fBm, fordable split at 1.2 m with shared butt edges, round caps, mouth flare into lakes and ponds read from the waterline field at world positions. Thalweg paths kept for the bake.
- **WOR-465, distance-field bake (#276).** `TerrainDistanceField`: two-level signed distance (sparse near tiles within 8 m of a ring, 2 m far level), shore profile (slope, exposure, sand, mud) and channel frame (arc length, width ratio, curvature) textures per chunk.
- **WOR-459, nav reads rings (#277).** `NavInputBuilder` takes `navRings` from ready chunks; the tile marcher is deleted. Colonists stop at the smooth waterline and wade creeks under 1.2 m.
- **WOR-460, water and shore shader (#278).** `water.glsl` paints the waterline, shallows and depth ramp, thalweg channel, pools, bands by shore profile, bottom patches, shimmer, and ocean-only foam from the three textures. The tile water path is deleted. A small `Foundation::Tunables` registry puts every shader constant on `/api/dev/tunable`. Shore slope became a heuristic because tile elevation is corner-interpolated per chunk and holds no local slope.
- **WOR-461, vision, mud, point bars (#281).** `TerrainPolygonQuery` (distance to water, nearest shore point, containment) over a chunk's own rings with a 16 m edge index. Shore points every 1 m along the rings, fordable creeks included. `TilePostProcessor::finalSurface`: point-bar sand on inner bends, then mud with probability by distance band, as a pure function of the world tile. Shore tiles and cardinal-wave mud are deleted.
- **WOR-462, land boundaries (#282).** `SurfaceField` in C++ and `land.glsl` in GLSL: per-surface indicator, D5 blur with an axis thin-feature guard, one world-space warp per point with per-surface amplitudes, priority paint, a fringe and rim. HashNoise's gradient fBm is ported to GLSL on split whole-tile + offset inputs, so C++ and GLSL agree 70 km from the origin (golden test within 1.1e-6 m). Render tiles are 2 bytes (RG8UI) over the chunk plus a 3-tile apron, post-processed like the neighbor's own tiles. Flora and groundcover ask the field and the rings. `TileAdjacency`, `TileData::adjacency`, and the neighbor bleed are deleted.
- **WOR-463, perf validation.** GPU timestamp timers (the old GPU timer always read 0), far-zoom octave LOD in the land pass, a nav recenter that no longer blocks the main thread on a running build, a faster waterline lattice fill and bake walk, and the near SDF texel settled at 0.25 m. Spec section 6 now holds measured numbers.

## Technical decisions

- **Seams by construction, not stitching.** Everything a chunk computes near its border is a function of world position and world-seeded noise over a 20-tile apron built from the chunk's own sample data. Pins on a world lattice make every resampled run depend only on its lattice cell. Border-invariance tests (TerrainPolygonSeamsTest, the distance-field seam tests, RenderTileSeams, the finalSurface border tests) compare adjacent chunks bit for bit.
- **Warp the field, not the rings.** Isolines of one continuous field can't cross, so two nearby water bodies never overlap after displacement and nav's even-odd parity never opens a walkable hole.
- **Paint from a distance field instead of tessellating bands.** Every shore effect is a function of distance to the waterline, so it is smooth at every zoom and continuous around river mouths without any polygon boolean.
- **One evaluator for the land field.** The shader and placement share the algorithm, checked by a GLSL-vs-C++ golden test, so what grows on a spot matches what is drawn there.
- **Keyed by world tile.** Mud rolls hash the owning chunk and local tile, not chunk-local x,y, so a neighbor's apron reproduces them.
- **Spec corrections made along the way**, all in `terrain-polygons-architecture.md`:
  - The query needs only the chunk's own rings.
  - The point-bar test is width-relative: |κ|·hw > 0.15, the channel frame's bend measure.
  - The land thin guard is an axis rule at 0.85/0.15.
  - Render tiles read final surfaces 9 tiles past the square.
  - The near SDF texel is 0.25 m. At 0.5 m a thin feeder beads into blobs at zoom 3.

## Measured (RelWithDebInfo, 3072x1728, RTX 3090)

- **Tile pass on the GPU** (land and water together): 0.36 to 1.43 ms at every zoom from 20 to 0.25 on the river, coast, and desert views. Before the epic it was 0.04 to 0.51 ms. Water is 0.05 to 0.19 ms of it.
- **Frame time:** unchanged within noise against the pre-epic build at every zoom. At zoom 0.25 over the forested landing the frame is about 118 ms, bound by the entity pass (about 24k draws, 4.6M triangles), the same as before the epic. Coast and desert hold 120 fps down to zoom 0.5.
- **Scrolling:** p99 at zoom 0.75 went from 920 ms to 25 ms once nav recentering stopped blocking. At zoom 3 it went from 168 ms to 16.6 ms.
- **Chunk generation on the worker:** median 56 ms and p90 126 ms. A landing river chunk takes about 285 ms, most of it the 0.25 m bake. A chunk is ready a median 91 ms after its request.
- **Memory per chunk:** tile render data went from 4 MB to 537 KB. The water textures add 0.4 MB (ocean) to about 7 MB (river). The LRU held 57 MB at zoom 0.25.
- **Nav:** the landing region rebuild takes 763 ms on the worker for 5,434 triangles, vs 499 ms for 3,082 with the tile marcher.

## Related documentation

- `docs/technical/organic-terrain/terrain-polygons-architecture.md` (the contract, D1 to D16, measured budgets in section 6)
- `docs/technical/organic-terrain-geometry.md` (research)
- `docs/technical/ground-textures.md` (atlas and the land field as shipped)

## Next steps

These were found during the epic and aren't started:
- **Deferred in the epic:** WOR-467 (large-scale coastline variety at the tile layer: biome water still follows 16 m sectors) and WOR-489 (the waterline's thin guard leaves 1-wide inlets on the iso knife edge).
- **Bugs:**
  - WOR-488: a colonist stalls on "Waiting for the area to settle" after the camera pans far away and back.
  - Tree positions differed between two runs at the same spot, possibly placement non-determinism.
- **Performance outside this epic's shaders:**
  - The far-zoom entity pass.
  - Main-thread placement store and unload (10 to 14 ms and about 50 ms).
  - Coast warp noise (about 60 ms per chunk).
  - The one-chunk load radius, which leaves a blank strip at zoom 0.25.
