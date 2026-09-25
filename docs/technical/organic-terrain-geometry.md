# Organic Terrain Geometry from Tile Data

Created: 2026-09-25
Status: Research / Planning (no implementation yet)
Related: [ground-textures.md](./ground-textures.md), [visual-style.md](../design/visual-style.md), [tile-transitions.md](../design/features/game-view/tile-transitions.md), [pathfinding-architecture.md](./pathfinding-architecture.md)

## The question

Terrain is stored as a 1 m tile grid and today it reads as a tile grid: rivers are runs of
`Water` tiles, shorelines are staircases with a little shader blur. We want the tiles to keep
defining *roughly where* things are while the rendered result looks organic: curved
shorelines, rivers as ribbons, biome edges that don't betray the lattice.

The specific idea on the table: stop treating a tile as a 1 m square and treat it as a sample
at its center, a vertex on a lattice, then reconstruct region boundaries *between* samples.

Short answer from the research: yes, that's the right mental model, and it's the standard one.
Every technique below is some version of "the tile centers are the vertices, the boundaries
live on the dual grid." The real decisions are (a) what field you reconstruct, (b) where the
reconstruction runs (fragment shader vs CPU contour extraction), and (c) which features should
skip the tile grid entirely because we still have their source vector geometry.

## Where we are today

Two findings from the code survey shape the whole plan.

### Rivers already have vector geometry; we throw it away

`RiverNetwork2D` (libs/world/worldgen/sampling/RiverNetwork2D.h) walks the 3D drainage graph
and emits meandered `Segment { x0,y0,x1,y1, halfWidth0, halfWidth1 }` polylines in world meters,
with width growing downstream (`w = k * sqrt(flowAccum)`, clamped 0.6 m to 110 m). Those segments
are gathered per chunk into `ChunkSampleResult::riverSegments` and retained on the chunk via
`Chunk::biomeData()`. `Chunk::computeTile` (Chunk.cpp:183) then rasterizes them: any tile whose
center falls inside a channel becomes `Surface::Water` with a cosmetic `waterDepth` byte. After
that point a river is indistinguishable from a lake except by its depth byte and its shape. No
river flag, no width, no flow direction, no centerline.

So for rivers the problem isn't "reconstruct organic geometry from tiles." The organic geometry
exists. We rasterize it to the grid for gameplay (nav, placement, vision) and then render the
rasterization. The fix is to render the source: stroke the centerline as a ribbon. Ponds are the
same story (`PondNetwork2D` blobs, `pondDepthAt`).

### Terrain is not per-tile quads any more

Since the 2026-06 render overhaul, `ChunkRenderer` draws one quad per visible chunk and reads a
512x512 `RGBA32UI` data texture of `TileRenderData` (surface id, 8 neighbor surface ids, edge
masks, depth). Everything about the look happens in `tile.frag` / `includes/tile.glsl`:

- `getSurfaceStackOrder()` is a paint-over priority (Water < Mud < Sand < Dirt < GrassShort <
  Grass < GrassMeadow < GrassTall < Rock < Snow).
- `computeHigherBleedWeights()` bleeds a higher-priority neighbor 20% of the way into a
  lower-priority tile, with radial rounding at corners; `computeDiagonalCornerWeights()`
  handles diagonals; `computeTileEdgeDarkening()` adds noise-perturbed edge darkening with
  hard edges across families (Ground / Water / Rock).

That is already a priority-layered, per-pixel edge treatment. It reads tile-like because the
blend is a fixed-width band hugging the tile edge, and because the band is derived from the 8
immediate neighbors only, so a boundary can never curve with a radius bigger than a tile.

### Nav is already vector; it derives its own water contours from the same tiles

The Tier 2 navmesh is a constrained Delaunay triangulation over vector obstacle polygons
(pathfinding-architecture.md: "no rasterization anywhere", "vector, not tile/grid").
`extractWaterObstacles` (libs/engine/nav/NavInputBuilder.cpp) runs marching squares over
`TileData::surface`, simplifies each loop with a 0.5 m epsilon, drops loops under a quarter
tile, and hands the polygons to the CDT as not-walkable. Water is never collided against; it is
simply not a face. `isValidPosition` is `isOnMesh`, the funnel shrinks by agent radius, Tier 3
collision is circle-vs-agent and circle-vs-wall-band.

So a smooth shoreline is not a nav problem. A Chaikin'd, noise-perturbed contour simplified at
the same epsilon has roughly the vertex count of the staircase after collinear collapse, and a
river ribbon outline is exactly the "rivers as polylines" the planned Phase 2 geography mesh
asks for. The problem is *two* shorelines: if the renderer computes the waterline one way (a
fragment shader) and nav another (raw tiles), they diverge by up to a tile, and colonists stand
on painted water or get blocked on painted sand. Factorio shipped that bug (FFF-344).

The rule that follows: the water polygon set per chunk is computed once, on the CPU, by one
function, and both the renderer and `NavInputBuilder` consume it. Vision's `m_shoreTiles`
(drinkable-water discovery, cardinal tile adjacency today) should read the same polygons.

## Literature

Sources are grouped by what they contribute. Fetched or search-confirmed URLs only.

### The dual grid: tile centers as vertices

- **Autotiling: interactive guide to procedural tile selection**, Amit Patel (Red Blob).
  https://www.redblobgames.com/articles/autotile/claude/
  Dual-grid autotiling as marching squares with sprites; display cells offset half a tile so
  each looks at four data corners. Gives the priority rule for 3+ terrains: rank terrains, a
  corner "matches" if its neighbor is equal-or-higher priority, one overlay per terrain, draw
  low to high. Our `getSurfaceStackOrder` is this rule already.
- **Quarter-tile autotiling** and **Marching squares (Sylves)**, Boris the Brave.
  https://www.boristhebrave.com/2023/05/31/quarter-tile-autotiling/
  https://www.boristhebrave.com/docs/sylves/1/articles/tutorials/marching_squares.html
  States the ceiling of sprite-based approaches plainly: no curve radius larger than half a
  tile, repetitive, and no clean answer to 3+ terrains in one cell besides stacking layers.
  Those are the limits a field/contour reconstruction removes.
- **Draw fewer tiles by using a dual-grid system**, jess::codes.
  https://www.youtube.com/watch?v=jEWFSv3ivTg
  Provenance for the technique in Godot circles. Two-terrain only.
- **TileMapDual** (Godot plugin). https://github.com/pablogila/TileMapDual
  README's answer to multiple terrains: multiple layers. Nobody solves triple junctions inside
  one tile; everybody stacks.
- **Organic towns from square tiles**, Oskar Stalberg, IndieCade 2019; Sylves Townscaper
  tutorial. https://www.youtube.com/watch?v=1hqt8JkYRdI
  https://boristhebrave.com/docs/sylves/1/articles/tutorials/townscaper.html
  The other route to organic shapes: warp the lattice itself (merge triangles, subdivide,
  relax with per-region deterministic seeds so infinite grids agree). Worth stealing the
  deterministic-seed-per-region trick; warping our data grid is off the table since gameplay
  depends on square meters.

Wang / blob tilesets (cr31) are sprite-selection schemes and don't help with geometry. Wave
function collapse generates content from adjacency rules; it does not render existing data.

### Contours from a scalar field, and smoothing them

- **Marching squares**, Wikipedia. https://en.wikipedia.org/wiki/Marching_squares
  The 16 cases, interpolated crossings, saddle resolution by center average, isobands. If the
  field is a blend weight rather than a 0/1 indicator, interpolation places crossings off the
  midpoint and shapes come out round for free.
- **CONREC**, Paul Bourke. https://paulbourke.net/papers/conrec/
  Per-cell contouring on four triangles around the center average; sidesteps saddle ambiguity.
- **Marching squares series, part 5 "Being colorful"**, Jasper Flick (Catlike Coding).
  https://catlikecoding.com/unity/tutorials/marching-squares-5/
  A working 2D multi-material marching squares with shared vertices and no gaps: several
  states per corner, crossings placed by comparing states, 3- and 4-material cells meet at
  the mean of the crossings. Reference implementation for the exact-partition route.
- **Smooth voxel terrain part 2 (surface nets)**, Mikola Lysenko.
  https://0fps.net/2012/07/12/smooth-voxel-terrain-part-2/
  One vertex per dual cell at the mean of its edge crossings, then join. Half the primitives
  of marching squares and already smoother; a good first pass before Chaikin.
- **Dual contouring tutorial**, Boris the Brave.
  https://www.boristhebrave.com/2018/04/15/dual-contouring-tutorial/
  Preserves sharp corners via QEF. Wrong tool for terrain (we want round), cited so nobody
  reaches for it.
- **smoothr vignette** (Chaikin vs Gaussian vs spline).
  https://cran.r-project.org/web/packages/smoothr/vignettes/smoothr.html
  Chaikin corner cutting converges to a quadratic B-spline, is local, and never overshoots,
  which matters when adjacent regions share an edge. It shrinks features, so 1-tile islands
  need guarding.
- **FitCurves.c**, Philip Schneider, Graphics Gems.
  https://github.com/erich666/GraphicsGems/blob/master/gems/FitCurves.c
  Least-squares cubic Bezier fit with recursive splitting; turns a dense polyline into a few
  Beziers if we want real SVG paths.
- **Potrace**, Peter Selinger. https://potrace.sourceforge.net/potrace.pdf
  Bitmap to vector: pixel-boundary path, optimal polygon, corner-vs-curve decision by
  threshold, Bezier joining. Structurally identical to "terrain grid to outline per material."
  GPL, concept only.

### 3+ materials meeting at a point

- **Multiple material marching cubes (M3C)**, Wu and Sullivan, IJNME 2003.
  https://onlinelibrary.wiley.com/doi/abs/10.1002/nme.775 (abstract only)
- **SurfaceNets for multi-label segmentations**, JCGT 11(1), 2022.
  https://jcgt.org/published/0011/01/03/paper.pdf
  Multi-label surface nets: patches per label pair, junctions shared. Translates to 2D.
- **GameDev.net: constrained multi-material marching squares** (search snippet).
  https://www.gamedev.net/forums/topic/692298-constrained-multi-material-marching-squares/
  The failure mode stated by a practitioner: marching each material as its own binary field
  leaves holes at triple points; dilating one to close the hole overlaps the others.
- **Voronoi maps tutorial**, Amit Patel.
  https://www.redblobgames.com/x/2022-voronoi-maps-tutorial/
  Jitter the sample points, build Voronoi, every cell is a polygon and junctions are Voronoi
  vertices, gap-free by construction. Merge same-material cells and smooth the outline.
- **Factorio Friday Facts #199, #214, #333**.
  https://factorio.com/blog/post/fff-199 https://www.factorio.com/blog/post/fff-214
  https://www.factorio.com/blog/post/fff-333
  Priority-ordered overlay transitions (and why water transitions had to move onto the water
  tile: 1-tile islands were otherwise unrepresentable), runtime alpha-mask blending instead of
  baked sprites, and terrain cached rather than redrawn per frame.

Two ways to make 3+ materials gap-free: an exact shared-vertex partition (Catlike / M3C /
multi-label surface nets), or priority layering where each material is a closed region drawn
over everything below it. Layering is what every shipped tile game does and it is what our
shader already does. Exact partitions are only worth it if overdraw matters or two sides need
to reference the same curve (foam and beach sharing the waterline).

### Rivers

- **Polygonal map generation for games** (2010) and part 3, plus mapgen2, Amit Patel.
  http://www-cs-students.stanford.edu/~amitp/game-programming/polygon-map-generation/
  https://simblob.blogspot.com/2010/09/polygon-map-generation-part-3.html
  https://github.com/redblobgames/mapgen2
  Rivers run along graph edges and are drawn as strokes whose width grows with accumulated
  flow, with noisy edges at the finest subdivision. That is our `RiverNetwork2D` already;
  what's missing is the stroke.
- **Noisy edges**, Amit Patel. https://www.redblobgames.com/maps/noisy-edges/
  Recursively subdivide each edge inside the quad formed by the two region centers and the
  two corners so wiggles can never cross a neighbor's. On our dual grid the quad is (tile
  center A, dual vertex, tile center B, dual vertex), same guarantee. He notes it can look
  too sharp for coasts and suggests dithered noise for biome borders instead.
- **GPU-friendly stroke expansion**, Levien and Uguray 2024; lyon `StrokeTessellator`.
  https://arxiv.org/html/2405.00127v1
  https://docs.rs/lyon_tessellation/latest/lyon_tessellation/struct.StrokeTessellator.html
  Centerline + width to fill outline with joins and caps. We already tessellate fills, so a
  river is centerline spline, offset curves with per-vertex width, perturbed banks, fill.
- **Procedural river generation**, dfsek. https://dfsek.com/blog/rivers
  Caution about confluences: union the two strokes with round joins; don't grow width by
  summed flow at the junction or you get fat blobs.

Nothing substantive surfaced on how RimWorld, Songs of Syx, or Dwarf Fortress draw rivers.

### Noise, domain warping, blending

- **Domain warping**, Inigo Quilez. https://iquilezles.org/articles/warp/
  `f(p + h(p))` with fBm. Warp the sample position or displace along the normal by fBm of
  world position. Deterministic in world space, so chunk borders agree with no coordination.
- **Fast biome blending, without squareness**, KdotJPG.
  https://noiseposti.ng/posts/2021-03-13-Fast-Biome-Blending-Without-Squareness.html
  Grid-aligned blending shows creases and 45-degree borders; the fix is a jittered point set
  with a sparse `max(0, r^2 - d^2)^2` kernel, normalized. This is the blend-weight field that
  would feed either the shader or marching squares.
- **Here Dragons Abound: making islands**, Scott Turner.
  https://heredragonsabound.blogspot.com/2016/10/making-islands.html
  Coastline = base shape + low-frequency noise + high-frequency noise; amplitude ratio moves
  between "stretched blob" and "fjords."
- **Terrain shader experiments**, Amit Patel.
  https://www.redblobgames.com/x/1730-terrain-shader-experiments/
  Biome weight per vertex, barycentric blend shaped by `weight^k` (linear to hard edge), noise
  in the fragment stage rather than extra vertices; weight-range stripes give borders and
  lines. The cheapest feather recipe for a gradient-capable renderer.
- **Advanced terrain texture splatting**, Mishkinis (Game Developer).
  https://www.gamedeveloper.com/programming/advanced-terrain-texture-splatting
  Plain linear cross-fades read as mush; shaped blends (power or height-based) read natural.
- **2D water shader breakdown**, Cyanilux.
  https://www.cyanilux.com/tutorials/2d-water-shader-breakdown/
  Shore foam from distance-to-edge modulated by scrolling sine and noise. Offset contour rings
  are that distance field.

### Chunking and caching

- **Dual contouring: seams and LOD for chunked terrain**, Nick Gildea.
  http://ngildea.blogspot.com/2014/09/dual-contouring-chunked-terrain.html
  Seams come from contouring each chunk alone; fix is a seam mesh from border cells,
  regenerated only when an edit touches the border. In 2D the equivalent is a one-tile apron
  from neighbors so border dual cells compute identically on both sides.
- Factorio #333 (above) for caching the rendered terrain; Townscaper (above) for
  per-region seeding.

## Options

Ranked. The recommendation is the hybrid, option 1.

### 1. Hybrid: shared CPU contours for anything walkability depends on, shader field for the rest (recommended)

The split is by consequence, not by material. If where the line falls changes what a colonist
can do (water, later cliffs), the line is a CPU polygon computed once per chunk and shared with
nav. If it is purely a look (grass into dirt, sand into grass, biome tint), it is a per-pixel
field in the shader and can be as soft and noisy as art direction wants.

**Water/land boundary (lakes, ocean, wetland):** CPU contour extraction as in option 2, but
only for the water layer: softened field at tile centers with a one-tile apron, interpolated
marching squares on the 0.5 isoline, Chaikin, world-space fBm displacement, simplify at the
nav epsilon. The polygon set is the chunk's waterline. Renderer fills it (plus feather and foam
rings) and nav's `extractWaterObstacles` takes it as its obstacle input instead of marching the
raw tiles itself.

**Land-on-land surfaces (Grass, Dirt, Sand, Rock, Snow, Mud, biome tint):** keep the
one-quad-per-chunk data-texture renderer and replace the neighbor-band bleed in `tile.glsl`
with a real field reconstruction. Per pixel, for each material in the stack order, gather the
tile centers within radius r (r of 2-3 tiles, so 5x5 to 7x7 taps, with the interior-tile
early-out keeping most pixels at one tap), weight them with KdotJPG's kernel or a Gaussian,
and domain-warp the sample position with world-space fBm first. Paint materials low to high
with a shaped threshold (`smoothstep` around 0.5, width = feather). That is literally "tile
centers are lattice samples, evaluate the field between them," done per pixel with no
geometry, no caching, no chunk seams (the data texture would need a one-tile apron or a
neighbor-chunk lookup at borders), and no change to the render overhaul's architecture.
Curve radius is now bounded by r, not by half a tile. Triple points need no special case
because layering already handles them.

**Rivers, streams, ponds:** stop rendering them from tiles. Stroke `riverSegments` into a
ribbon mesh per chunk (Catmull-Rom through segment endpoints, offset by half-width, perturb
banks with world-space fBm, round joins at confluences), tessellate once at chunk-processed
time like the baked flora meshes, and draw it over the ground pass with the vector pipeline.
Width, flow direction, and depth are all available for shading (flow lines, foam at banks,
depth tint). Ponds get the same treatment from their blob outlines. The ribbon's outline
polygon is also the nav obstacle, so `Chunk::computeTile` no longer has to be the only place
rivers become walkability.

**Shore foam, cliff edges, roads later:** these are contour-band features. Either a second
threshold ring in the shader (a `smoothstep` band just outside the water isoline) or, when
they need geometry (placing decorations along the shore), a CPU contour extraction from the
same field, see option 2.

Trade-off: the ground look lives in GLSL, which is fine for the current team but means the
"organic edge" is a fragment effect, not a polygon we can hand to other systems. When a
system needs the polygon (shore decoration placement, nav alignment), extract it on the CPU
with the same field and threshold so the two agree.

### 2. CPU contour extraction per chunk, tessellated vector regions

Per chunk, with a one-tile apron: build a softened per-material field at tile centers, run
interpolated marching squares (or surface nets) on the 0.5 isoline of "priority >= k" for
each layer k, Chaikin 1-2 iterations, displace along normals with world-space fBm (amplitude
< half a dual cell so layers can't cross), optionally fit Beziers, tessellate, cache per chunk
per layer, draw layers in order with gradient feather bands underneath each fill. Rivers as in
option 1.

This is the "geometric regions" version the question asked for and it is a sound design; the
reasons it ranks second are local. The render overhaul just removed CPU tile geometry on
purpose; at zoom 0.25x there are 1.1M visible tiles, and a chunk-cached contour mesh is far
smaller than per-tile quads but still a new mesh-management path (upload, LRU, invalidation on
terraform, seam handling). It also buys nothing over option 1 for the *look* of the ground
partition, only for having polygons. Keep it as the path to take if the shader approach hits a
wall (tap count at high feather radius, or systems that need contour polygons anyway).

### 3. Exact multi-label partition (Catlike part 5 / multi-label surface nets)

Gap-free, overdraw-free, one shared curve per boundary. Smoothing a shared-edge partition is
fiddly (each edge smoothed as one polyline with junction vertices pinned, noise applied
identically from both sides). Only worth it if overdraw from layering measurably matters, or
for the single waterline where foam and beach want to reference the same curve.

### 4. Jittered Voronoi cells

Deterministically jitter tile centers, Voronoi, merge same-material cells, smooth. Organic cell
shapes for patchy materials (rock outcrops) but replaces a case table with a mesh library, and
lattice jitter fights the 1 m gameplay grid everywhere else. Not recommended as the base;
maybe as a per-material texture trick later.

## Decision: polygons are the truth (2026-09-25)

Wherever a system can be driven by the terrain polygon instead of the tile, it is. The tile
grid stays as storage and as the coarse source the polygons are reconstructed from (for
biome water) or rasterized from (for rivers and ponds, whose source is already vector). Every
runtime consumer reads the polygon:

- **Movement and validity:** nav obstacle input is the waterline polygon set and the river
  ribbon outlines. `isValidPosition` already means `isOnMesh`, so placement, spawn, drops, and
  build previews follow for free.
- **Rendering:** the same polygons are filled, feathered, and given foam. One waterline.
- **Vision / drinkable water:** `m_shoreTiles` becomes a distance-to-polygon query (nearest
  point on the water boundary within reach), not cardinal tile adjacency.
- **Terrain post-processing:** `TilePostProcessor::generateMud` ("land within 3 tiles of
  water") becomes "tile center within 3 m of the water polygon," so mud banks follow the
  smooth shore rather than the staircase.
- **Gameplay queries that arrive later** (fishing spots, fording, flood zones, bridge
  endpoints) are specified against the polygon and its offset bands from the start.

`TileData::surface == Water` is not deleted; it is demoted. It is a generation-time input to
the contour extractor for biome water, and a cheap coarse prefilter ("is this chunk region
near water at all"), never the answer to "can I stand here" or "is this shore." This is of a
piece with the existing world-data-vs-runtime boundary in pathfinding-architecture.md: world
data spawns chunk data, the nav mesh answers runtime validity, and now the polygon is the
single thing both the mesh and the picture derive from.

Consequence for the module layout: the contour/ribbon extraction must live somewhere both
`libs/engine/world/chunk` (post-processing, vision inputs) and `libs/engine/nav` can depend on
without a cycle, and the renderer reads its output from the chunk rather than recomputing.
The polygon set is chunk state, versioned like `m_renderDataVersion`, rebuilt when tiles or
river segments change.

## Answering the original question directly

Treat each tile as a sample at its center, yes. Ignore its square footprint for rendering, yes.
But don't reconstruct everything from the tiles: where the source is already vector (rivers,
ponds, eventually roads), render the source and let the tiles remain the gameplay
rasterization. Reconstruct only the things that are born on the grid (surface partition,
biome blend).

## Open questions to settle before implementation

- **Module placement.** Decided that polygons are the truth (above). Still open: where the
  extractor lives (libs/geometry has the ring simplifier; libs/engine/world/chunk has the tile
  data and river segments; nav must consume without a dependency cycle), and how the biome
  water polygon and the river ribbon polygon union where a river meets a lake (a polygon
  boolean in libs/geometry, or the ribbon end cap simply drawn and meshed under the lake).
- **Priority order is a look decision.** Sand-over-grass vs grass-over-sand changes every
  beach. Factorio's water-onto-grass reversal shows the order also decides which 1-tile
  features can appear at all. Current `getSurfaceStackOrder` is a starting point, not settled.
- **Smoothing radius vs fidelity.** r = 2 tiles rounds a 1-tile pond into a dot; r = 3 erases
  it. Either exempt small features (island area check) or let the design accept that a lake
  needs to be a few tiles wide to read.
- **Rivers as ribbons over a water layer.** River banks (ribbon offset curve) and lake shores
  (field isoline) become two code paths with two smoothing behaviors. Fine, but they must meet
  cleanly where a river enters a lake (ribbon end cap under the lake fill).
- **What TileData needs to carry.** Nothing for option 1's ground pass. For rivers, the
  ribbon needs `riverSegments` at render time, which the chunk already retains. If we ever
  want per-tile "this is river water, flow = v" for gameplay, `TileData` has no spare bytes
  (16 B, 3 pad bytes in the render mirror), so that would be a separate sparse structure.
- **Feather cost.** Per-pixel kernel taps scale with radius squared; measure on the 0.25x zoom
  case from the overhaul table before picking r. The interior-tile early-out keeps the
  common case cheap.
- **Art direction still open.** visual-style.md's "edge visual treatment" question (crisp
  geometric vs soft alpha, how irregular) is unanswered. The shader route makes those cheap
  to A/B with uniforms, which is a point in its favor for the exploration phase.

## Suggested next planning step

A short design spec (not code) that fixes the priority order, the smoothing radius, the
extractor's home and data flow (tiles + river segments in, versioned polygon set on the chunk
out, nav / render / vision / mud reading it), with a mock or two (an SVG mock of a
shoreline and a confluence at zoom 1 and zoom 0.25) so the art direction question gets
answered on a picture rather than in prose.
