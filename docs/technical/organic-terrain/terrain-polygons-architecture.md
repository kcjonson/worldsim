# Terrain Polygons — Technical Architecture

**Status:** Design
**Created:** 2026-09-25
**Research:** [organic-terrain-geometry.md](../organic-terrain-geometry.md) (literature, current state, ranked options, the polygons-are-truth decision)

Each chunk computes one set of terrain polygons: the waterline of every biome water body, a
ribbon per river channel, an outline per pond. Those rings are the runtime truth. The nav mesh
blocks on them, the renderer fills them, vision discovers drinkable water along them, mud
banks form at a distance from them. The 1 m tile grid stays as storage and as the coarse
source the waterline is reconstructed from; a `Water` tile is never again the answer to "can I
stand here" or "is this shore."

This document fixes the decisions the research doc left open: where the extractor lives, the
exact field and smoothing recipe, the chunk-seam rule, how rivers and lakes coexist, what nav
and the other consumers read, the realism rules for banks and shores, and the tunables.

---

## 1. Current state (what changes)

| Area | Today | After |
|---|---|---|
| River geometry | `RiverNetwork2D::Segment` polylines with half-widths, gathered per chunk into `ChunkSampleResult::riverSegments`, then rasterized by `Chunk::computeTile` into `Surface::Water` tiles; `kRenderMinHalf = 0.8 m` fattens sub-tile streams so they stay 4-connected | Segments are stroked into ribbon rings at true width. Tiles still get `Water` for prefilter and tile-granular queries. `kRenderMinHalf` deleted. |
| Water edge, rendered | `tile.glsl` already does real edge work: higher-priority neighbors bleed 20% into water tiles with radial corners, diagonal corner weights, noise-perturbed edge darkening with hard cross-family edges, depth tint by `waterDepth`, plus a 3-tile mud band from `generateMud`. See 1.1. What remains is the 1 m staircase (visible at zoom 1 and above, because the bleed only sees the 8 neighbors) and the uniformity: the mud band and the bleed are the same width everywhere | Waterline ring from a softened field on the dual grid, Chaikin + multi-scale fBm, one polygon; every band width varies along the ring (D15) |
| Nav water | `NavInputBuilder::extractWaterObstacles` marches `TileData::surface` per area on every build | Reads chunk rings; the tile marcher is deleted |
| Vision shore | `Chunk::getShoreTiles()` = land tiles with a cardinal water neighbor | `shorePoints` sampled along the rings |
| Mud banks | `TilePostProcessor::generateMud`, cardinal waves out to 3 tiles, chunk-local | Distance-to-ring band over the chunk's own unclipped rings, which reach through its apron to the neighbors' shores |
| Point bars, cut banks, riffles, pools | none | Derived from ribbon curvature and width (sections 2.7, 2.9) |

### 1.1 Today, observed in the game (2026-09-25, quickstart landing)

Zoom 1 (8 px/m): the mud band, the depth tint, and the edge darkening are all there. The
staircase is still the dominant shape, the mud band is a constant three tiles, and a 0.6 m
tributary is drawn as a 1-tile stepped line because of the `kRenderMinHalf` floor.

Zoom 0.25 (2 px/m): the stairs vanish and the river reads well. The remaining tell is
uniformity: constant width, one bank color, one band width, no bars or pools.

---

## 2. Architecture decisions

### D1: Polygons are the truth

Restated from the research doc, now binding on every consumer in this spec. A system that
needs "is this water", "where is the shore", or "how far from water" reads the chunk's ring
set or a query over it. No new tile-granular water checks. `TileData::surface == Water`
remains as (a) the input to the waterline field for biome water and (b) a coarse prefilter
("does this chunk have water at all"). `TileData::waterDepth` stays cosmetic.

### D2: Two sources, one ring set

Biome water (ocean, lake, wetland from the sampled biome) is born on the grid and is
reconstructed from it (D5, D6). Rivers and ponds already have vector sources
(`riverSegments`, `pondBlobs`) and are stroked from them (D7, D8). Both land in the same
per-chunk container:

```cpp
namespace engine::world {
enum class TerrainRingKind : uint8_t { Waterline, Channel, Pond };
enum class WaterKind       : uint8_t { Ocean, Lake, Wetland, River, Pond };

// One per ring vertex. CPU-side truth; the GPU packing is defined in 10.1.
struct ShoreProfile {
    uint8_t slope;      // 0 gentle .. 255 steep, elevation gradient across the ring
    uint8_t exposure;   // 0 sheltered bay .. 255 exposed headland (ring concavity, 20 m window, + fetch for ocean)
    uint8_t sand;       // substrate weights, sand + mud + grass = 255; rock is a flag
    uint8_t mud;
    uint8_t moisture;   // TileData::moisture of the adjacent land tile
    uint8_t flags;      // bit0 rock, bit1 synthetic edge follows this vertex (D4), bit2 fordable cut (D7)
};

struct TerrainRing {
    geometry::Ring            ring;            // integer mm, world-absolute, simple, CCW outer / CW hole
    std::vector<ShoreProfile> profiles;        // same length as ring (D15)
    TerrainRingKind           kind;
    WaterKind                 water;           // Waterline: Ocean/Lake/Wetland from the biome; else River/Pond
    bool                      blocksMovement;  // false only for fordable channels (D7)
    bool                      holeCapable;     // Waterline: true (even-odd); Channel/Pond: false (solid)
    float                     meanHalfWidthM;  // Channel only
};

// Per channel, feeds the SDF bake (D10). `points` already carry the lateral offset toward the
// outer bank (D7 step 3), so this is the thalweg, not the centerline.
struct ThalwegPath {
    std::vector<geometry::Vec2i64> points;
    std::vector<float>             halfWidthM;     // bankfull half-width at each point
    std::vector<float>             widthRatio;     // w / w_mean over a 5 w window
    std::vector<float>             curvature;      // signed, 1/m
};

struct ChunkTerrainPolygons {
    std::vector<TerrainRing>        rings;         // extended region (chunk + apron), UNCLIPPED (D4)
    std::vector<TerrainRing>        navRings;      // the same rings clipped to the chunk square (D4, D9)
    std::vector<ThalwegPath>        thalwegs;
    TerrainEdgeIndex                edgeIndex;     // `rings` in 16 m lattice cells, for TerrainPolygonQuery (D3)
    std::vector<geometry::Vec2i64>  shorePoints;   // inside the chunk square only (D11)
    ExtendedTileBits                barTiles;      // one bit per tile of the EXTENDED region: point-bar Sand (D12)
    uint32_t                        version;       // bumped with the rings, read like renderDataVersion
};
}
```

`barTiles` covers the extended region, not just the chunk square, so a neighbor filling its
render apron (D16) reads the same bars the owning chunk paints (D11's border rule).

`Chunk` owns one `ChunkTerrainPolygons`, built in `Chunk::generate()` on the worker thread
(D4), exposed as `terrainPolygons()`, versioned like `m_renderDataVersion`.

### D3: Module placement

`engine` is one CMake target; chunk, nav, vision, the chunk renderer, and the tile
post-processor are all inside it, so the builder lives there with no cycle risk. The pure
math goes one layer down into `libs/geometry`, which links only `foundation` and already
owns `Ring`, `Vec2i64`, `simplifyRing`, the exact predicates, and the CDT.

New in `libs/geometry/contour/`:

- `marchingSquares(field, width, height, iso, originMm, cellMm) -> std::vector<Ring>`:
  interpolated crossings on the dual grid, saddle resolution by cell-center average, loops
  linked by exact edge keys, CCW outer / CW hole orientation. This replaces the marcher in
  `NavInputBuilder.cpp:29-176`, which is deleted in the same change (One Path Rule).
- `warpField(const ScalarField& coarse, cellMm, fineCellMm, fn(worldMm) -> Vec2 offsetMm)
  -> ScalarField`: bilinear resample of a coarse lattice onto a fine one with the sample
  position domain-warped by a world-space offset function (D6).
- `chaikin(Ring&, iterations)`, `resampleRing(Ring&, spacingMm)`, `isSimple(const Ring&)`
  (already in polygon/Polygon.h).
- `clipRingToRect(const Ring&, rectMm) -> std::vector<Ring>` (Sutherland–Hodgman against a
  convex rectangle, degenerate border runs collapsed by `simplifyRing`).
- `strokePolyline(centerline, halfWidths, capStyle) -> Ring` (offset both sides, round caps).

Also in `libs/engine/world/chunk/`: `TerrainDistanceField`, the per-chunk signed-distance
bake the renderer samples (D10). No polygon boolean is needed anywhere: rendering merges
shapes with `min(distance)`, and nav takes overlapping rings as solid containment.

New in `libs/engine/world/chunk/`:

- `TerrainPolygonBuilder` (`build(const Chunk&, const ChunkSampleResult&, const ApronField&)
  -> ChunkTerrainPolygons`), the only place the recipe below is spelled out.
- `TerrainPolygonQuery`: `distanceToWaterMm(point)` (0 in water, else to the nearest shore
  edge; an optional search radius bounds the work), `nearestShorePoint(point)`,
  `isInsideWater(point)`, and `isPointBar(tile)`, over one chunk's unclipped `rings` alone.
  Those rings already cover the extended region, and every ring edge in the lattice cell
  next to a border is identical in both chunks (D4, the seam test), so a chunk's own set
  answers every query in the chunk and several meters past it without touching a neighbor
  chunk (neighbors may not exist yet). The shore skips synthetic and fordable-cut edges;
  water is D9's classification with fordable channels in. The 16 m edge-bucket index is
  built once by the builder and stored with the polygons (`edgeIndex`, section 8): each
  entry keeps its edge's box, a cell no edge touches stores its containment, and each cell
  records whether shore lies within a cell of it, so most points cost one lookup. Used by mud
  (D11), shore points, the toilet spot rule, and any later gameplay query. From a point in
  water, `nearestShorePoint` can land on a bank submerged in another body (a channel's run
  into a lake), which the bake's union shoreline (10.1) leaves out.

Noise: `foundation::fractalNoise3` (HashNoise.h) already exists and is deterministic; the
builder uses it with z = 0 and a fixed seed per purpose. The three private noise copies
(RiverNetwork2D meanders, the grove field, `Chunk::fractalNoise`) are not touched by this
epic, but new code must not add a fourth.

### D4: Chunk region, apron, and seams

The chunk's polygon region is its 512 m square, `[origin, origin + 512 m)`, the same bounds
`ChunkRenderer` draws and `tile.frag` indexes. Dual cells (between tile centers) that straddle
a chunk border are computed on both sides from the apron and clipped at the border, so
adjacent regions partition the plane exactly and nothing is shifted by half a tile.

The builder works on an **extended region**: the chunk plus an apron of `kApronTiles = 20` on
every side. The SDF (D10) is clamped at `kSdfNearM = 8 m`, so a texel on the chunk border reads
every ring edge within 8 m, and the bake needs those edges bit-identical in both chunks. Every
ring is pinned on a world lattice (`kPinLatticeMm = 16 m`, D6), so each of those edges belongs
to a resampled run that can wander anywhere in the 16 m lattice cell next to the border. That
whole cell must therefore be free of the extended region's edge effect: the outermost coarse
sample's blur reads the forced land outside, the bilinear read uses that sample up to 1.5 tiles
in, the warp reads 1.45 m further, and a ring vertex moves with its marching cell and Chaikin
neighbor, `kEdgeEffectDepthMm` = 3.45 m at most (about 2.5 m measured). 16 m plus 3.45 m,
rounded up to a whole tile, is 20; the synthetic closure edges lie beyond the cell, so they
never reach a border texel either. (A per-chunk pin set, pins on the chunk's own border lines
only, fails here: a run starting at a border pin ends at some other line in each chunk, and
equal-arc-length resampling and simplification then place different vertices along the same
curve.)

- Apron samples come from the world sampler, not from neighbor chunks (neighbors may not
  exist yet, and generation runs on a worker). `GeneratedWorldSampler::sampleChunk` gains an
  apron: it already gathers rivers and ponds by AABB, and biome/elevation for any world
  position is available from `PlanetSampler::sampleAt`. The apron ring of tile surfaces is
  computed by the same `computeTile` logic and discarded after the build.
- Samples outside the extended region are land, as the nav marcher treats out-of-bounds
  today. A water body that reaches the extended boundary therefore closes along it. Every
  ring edge that lies on the extended boundary is a **synthetic edge**, flagged in the
  profile of its start vertex (`flags` bit1). The SDF bake, shore points, mud, and
  `TerrainPolygonQuery` skip synthetic edges; nav never sees them because the nav clip
  (below) removes everything outside the chunk square.
- Everything the recipe does is a function of world position and world-seeded noise: the
  same samples, the same blur, the same warp, the same crossings, the same Chaikin. Two
  chunks therefore produce identical geometry across their shared border. After `quantize`
  to integer mm, clipped rings share exact vertices along the border.
- Two representations are stored (D2): `rings`, unclipped over the extended region, for the
  SDF bake, shore points, mud, and queries, so no consumer ever sees a chunk border as a
  shoreline; and `navRings`, the same rings clipped to the chunk square, for nav (D9), where
  the border edge is a constrained edge shared bit-identically with the neighbor.

This is the 2D form of Gildea's seam rule (research doc) and removes the need for any
cross-chunk stitching pass. The tile adjacency stitching (`refreshAdjacencyAround`) is gone
too: the D16 render apron replaced its last reader.

### D5: The waterline field

The field is built from the **biome**, never from `TileData::surface`: `computeTile` sets
`surface = Water` for river and pond tiles too, and those are stroked at sub-tile precision
by D7/D8. The predicate is

```cpp
bool isBiomeWater(const TileData& t) {
    return isWater(t.primaryBiome)                       // Ocean, Lake (Biome.h)
        || t.primaryBiome == Biome::TemperateWetland
        || t.primaryBiome == Biome::TropicalWetland;
}
```

and the ring's `WaterKind` is Ocean, Lake, or Wetland from the same biome (a ring spanning
two, e.g. a lake with a wetland margin, takes the majority over its vertices).

1. Indicator `I(x,y) = 1` where `isBiomeWater`, else 0, over the extended region; outside it,
   land (D4).
2. Soften with a 3x3 binomial kernel (1 2 1 / 2 4 2 / 1 2 1, divided by 16).
3. Thin-feature guard: a sample is thin when both of its neighbors along either axis sit on
   the other side of the indicator. That is every sample with fewer than two same-side
   cardinal neighbors, plus the samples of a straight 1-wide run, which have exactly two,
   opposite each other. A thin water sample is floored at `kThinFeatureFloor = 0.85`, a thin
   land sample capped at `kThinFeatureCeil = 0.15`. This keeps 1-tile pools, 1-wide inlets
   and isthmuses, and 1-tile islets from dropping under the isoline while leaving every
   larger shape free to round; an L-corner's two same-side neighbors are adjacent, so it
   rounds too. "Fewer than two" alone misses the straight run, whose blur is exactly
   8/16 = 0.5 along its whole centerline, on the isoline, so the warp flickers an inlet in
   and out and floods an isthmus over. Guarded, the run is a band 1.17 m wide before the
   warp (0.85 on the run, 0.25 beside it), and the warp moves that width by up to about a
   fifth. The values are 0.85 / 0.15, not 0.70 / 0.30. At 0.70 a lone tile's loop falls
   under `kMinLoopAreaMm2` and is dropped; at 0.85 it is a pool ~0.7 m across (0.37 to
   0.49 m^2), not a dot and not a perfect circle once the D6 warp runs at its own scale. And
   a diagonal 1-wide run, which has no same-side cardinal neighbor and so is always thin,
   joins its tiles through bilinear saddles worth (2 x 0.70 + 2 x 0.25) / 4 = 0.475 at 0.70
   and breaks into beads; at 0.85 the saddle is 0.55. The guard is one implementation,
   `ThinFeatureGuard.h`, shared with the land field (D16 step 1).
4. Domain warp and fine march (D6): the softened lattice is resampled onto a 0.25 m lattice
   with the sample position offset by world-space noise, and marching squares runs there at
   iso 0.5. Straight runs still cross at the midpoint between tile samples, so a straight
   shore stays where the tiles put it; convex corners cut into the corner tile and concave
   corners fill out, which is the rounding.

Saddle cells resolve by the average of the four samples, which after the blur is almost
never exactly 0.5.

### D6: Warp, smoothing, simplification

Displacement is applied to the **field**, not to the rings. Isolines of one continuous field
can neither self-intersect nor cross each other, so two nearby water bodies can never overlap
after displacement (which, with even-odd nav classification, would have opened a walkable
hole), and no per-ring retry loop is needed.

1. Warp: resample the softened tile-lattice field onto a `kFineCellMm = 250` lattice over the
   extended region, reading each fine sample at `p + offset(p)` (bilinear on the coarse
   lattice). `offset` is a world-space vector noise, the sum of a fine term
   (`kBankNoiseAmpMm = 250`, 4 octaves, base wavelength 6 m; the 0.75 m octave is what gives
   a 1-tile pool its lopsidedness) and a low-frequency term (`kShoreLowAmpMm = 1200`, 2
   octaves, base 26 m; bays, headlands, the general unevenness of a shore). Both components
   of the vector use independent seeds. Because `|offset| <= 1.45 m`, a fine sample reads the
   coarse lattice within 2.45 m of itself; the lattice is padded by repeating its edge samples,
   so the only edge effect is the one D4 sizes the apron for. The same formula evaluates the
   field at any world point from a biome-water query (`WaterlineField`, one shared core with the
   lattice fill), which is how channels find their mouths (D7).
2. Marching squares on the fine lattice at iso 0.5 (D5 step 4).
3. Chaikin corner cutting, `kChaikinIterations = 1` (the fine march is already smooth at
   0.25 m; one pass removes the lattice facets). Local, never overshoots.
4. Pin every crossing of the world lattice, the lines `x = k·kPinLatticeMm` and
   `y = k·kPinLatticeMm` (16 m; chunk borders are lattice lines), then resample each run
   between pins to `kRingSpacingMm = 250`. A run between two lattice crossings stays inside
   one lattice cell and depends only on the curve there, so two chunks that compute the same
   curve in a cell compute the same vertices in it, whatever the rest of their rings look like.
   Costs at most one extra vertex per lattice crossing, about one per 16 m of shore: +4.5%
   nav vertices on the archipelago benchmark chunk, +19% on the staircase lake (a 16 m
   staircase crosses a lattice line at nearly every step).
5. Quantize to integer mm.
6. `simplifyRing` at `kRingSimplifyEpsMm = 100`, per run (pins are never removed). Not 500:
   nav and render use the same ring verbatim, and 500 would erase the bank detail. The vertex
   budget is in section 6.
7. Validate: `isSimple`. Quantization and simplification can in principle fold a tight
   feature; two runs can only cross inside the lattice cell they share, so the retry ladder
   steps per cell: a cell whose runs fold at 100 mm takes 50 mm, then the unsimplified runs,
   while every other cell keeps its own rung. A fold far from a border then never changes a
   run near it, which the neighbor, not seeing that fold, would not change either. The ladder
   never changes the shape (a per-chunk shape change would break the seam); a ring still
   folded unsimplified is dropped with a warning. Validation runs after the last geometric
   change, not before.
8. Drop loops with area under `kMinLoopAreaMm2 = 250 000` (a quarter tile), same as nav today.
9. Store in `rings`; clip a copy to the chunk square into `navRings` (D4), validating each
   clipped piece with `isSimple` as well.

### D7: River ribbons

Per river channel that intersects the extended region:

1. Centerline: Catmull–Rom through the segment endpoints, sampled every 0.5 m. Segments are
   already on a global 20 m arc-length grid so adjacent chunks sample the same points.
2. Half-width: linear along each segment (as `Segment` stores it), no floor. The riffle/pool
   width modulation `RiverNetwork2D` already applies stays; it is the source of the width
   variation the realism rules call for.
3. The ring is the **bankfull** outline. Bank asymmetry from curvature κ (turning angle per
   meter, signed): the outer bank moves out by `a = min(0.25, 2|κ|·hw)·hw` and the inner
   bank moves out by `0.4·a`, so the bankfull channel is widest at the apex (R5) and the
   deposit on the inner bank has room. The **wetted** narrowing at the apex is shading, not
   geometry: the thalweg (`ThalwegPath.points`) is the centerline offset toward the outer
   bank by `a`, and the shader paints the deep channel around it (10.2), leaving the inner
   side pale and shallow.
4. Bank noise: both offset curves displaced along their normals by fBm, 3 octaves, base
   4 m, amplitude `kChannelBankNoise = 0.15 · hw`, damped by local turning angle
   (`max(0.25, 1 - turn / 1.2 rad)`) so a tight bend cannot fold; the D6 validation catches
   the rest.
5. Fordability: `blocksMovement = (2·hw >= kFordableWidthM = 1.2)`. A channel whose width
   crosses the threshold is split at the crossing into pieces so the flag is per ring. A
   fordable ring is still drawn, still counts for vision and mud, and is skipped by nav.
6. Stroke each piece to a ring: left bank forward, right bank back. Round caps of radius hw
   only at the channel's true ends; at an internal fordable cut both pieces share a straight
   **butt** edge across the channel (the same two quantized vertices on each side), so
   neither piece overlaps the other and the nav boundary sits exactly at the 1.2 m crossing.
   The cut vertices carry `flags` bit2 so the SDF bake treats the cut edge as internal (no
   shoreline there).
7. Quantize, simplify at 100 mm, validate `isSimple`, store in `rings`, clip a copy to
   `navRings`.

**Confluences** (channel meets channel) need no boolean. The two ribbons overlap, the
renderer fills both with the same water, nav treats Channel rings as solid containment
(`holeCapable = false`), so an overlap cannot flip even-odd parity into a walkable hole. The
junction corner bar and scour hole are shading (section 2.9, R7).

**River mouths** (channel meets Waterline or Pond) get a flare and nothing else:

1. Find the first centerline sample inside the receiving body; call the arc length there
   `s_m`. "Inside" is read from the waterline field at the sample (the D5/D6 formula evaluated
   at that world point, from a biome-water query over the chunk's 3x3 neighborhood) or the
   pond rim before clipping (D8), never from the chunk's own rings: those end at the extended
   region, and a mouth one chunk sees and its neighbor does not would flare the river
   differently on either side of their border. The field and rims sit within a few cm of the
   rings built from them.
2. Over the reach `[s_m - min(kMouthFlareW · w, kMouthFlareMaxM), s_m]` widen the channel by up
   to `kMouthFlare = 1.6×` (estuary shape) and fade the bank asymmetry and point-bar deposits
   to zero. Extend the ribbon `min(1 w, kMouthExtendMaxM)` past `s_m` so the two shapes overlap
   generously. The caps (64 m, 24 m) only bind for rivers wider than 32 m and 24 m: a 100 m
   wide river flares over its last 64 m instead of 200 m. They bound how far along a river a
   mouth decision reaches, which bounds how much river each chunk must see (below).

**Channel reach.** A centerline sample is relevant to a chunk when it can shape a ring edge in
the lattice cell next to the chunk square, a ring edge anywhere in the extended region, or a
thalweg point a bake texel reads (D10): within `max(16 m, apron) + 2 m + 3.6 hw` of the square
(3.6 = (2 + 0.25) × 1.6: a texel reads a thalweg point 2 bankfull half-widths out, and the
thalweg sits up to the asymmetry off the centerline; a bank lies at most 2.8 raw half-widths
out). Every decision about a sample (flare, extension, the whole-crossing rule, the bend a
point bar spans, D12) reads the river at most `kChannelDecisionReachM = max(64, 3 × 24) + 2 =
74 m` along the centerline, so a chunk keeps every sample within that arc distance of a
relevant one. At the widest river
(`RiverNetwork2D::kMaxHalfWidthMeters = 103.5`: the 110 m clamped width times the 1.88 riffle
plus pool peak) that is 469 m from the chunk square, inside the 3x3 neighborhood the
biome-water query covers, and the gather margin (`kRiverGatherMarginM = 490 m`) keeps a 20 m
trunk step beyond it so every kept sample's Catmull-Rom span is intact. The width-ratio window
(R4) is capped at 64 m each side for the same reason.
3. Emit the ribbon as its own Channel ring, overlapping the lake ring. Nav: solid containment,
   fine. Render: the distance field is the minimum over all rings (D10), so the union is
   implicit, the shore bands run continuously around the mouth, and the river's own bank
   features fade out over the flare because they are gated by the channel metadata, not by
   the ring.

No polygon boolean is required. A fordable channel entering a lake keeps its fordable ring up
to the flare start.

### D8: Ponds

`Pond {cx, cy, radius, phaseA, phaseB}` is sampled every 0.3 m of rim with the rim radius
perturbed by the D6 fine noise at half amplitude (the sinusoids already give the large-scale
shape; a radial perturbation of a star-shaped rim cannot self-intersect), quantized,
simplified, validated, stored in `rings` and clipped into `navRings`. `kind = Pond`,
`water = WaterKind::Pond`, `holeCapable = false`, `blocksMovement = true`. The unclipped rim
is also what the D7 mouth test reads.
`pondDepthAt` still drives the tile depth byte for the prefilter; shading reads distance to
the rim.

### D9: What nav reads

`NavInputBuilder::buildInput` replaces its tile marcher with:

1. For each ready chunk whose region intersects the area rect plus the existing 1-tile margin,
   take `terrainPolygons().navRings` (the clipped set, D4).
2. Skip rings with `blocksMovement == false`.
3. Emit each as `NavInputPolygon { ring, blocked = true, provenanceId = kProvenanceWater,
   holeCapable = ring.holeCapable }`.
4. Not-ready or missing chunks contribute nothing (today they read as land; unchanged).

Clipped rings from two chunks share their border edge exactly (D4). The CDT receives the same
constrained edge twice, once from each side; `buildArrangement` merges exact duplicates and
collinear partial overlaps into one edge, so this needs nothing extra (tested in
`NavMesh.CoincidentConstraintEdgesFromSeparateRings`).

A navRing spans its whole chunk, so each emitted ring is also clipped to the sim area rect
(`clipRingToRect`, orientation kept) before it reaches the arrangement: the triangulation cost
tracks the area, not the chunk, and the clip lands its crossings exactly on the border ring.

```cpp
// NavInputBuilder::buildInput, water section (replaces the tile marcher)
for (const ChunkCoordinate& cc : chunksIntersecting(areaRectMm.expanded(kTileMm))) {
    const Chunk* chunk = chunks.getChunk(cc);
    if (!chunk || !chunk->isReady()) continue;                 // missing chunk reads as land
    for (const TerrainRing& tr : chunk->terrainPolygons().navRings) {
        if (!tr.blocksMovement) continue;                      // fordable creek
        input.polygons.push_back({.ring = tr.ring, .blocked = true,
                                  .provenanceId = kProvenanceWater,
                                  .holeCapable = tr.holeCapable});
    }
}
```

The rebuild signature (`NavigationSystem::areaChunkSignature`, via `nav::waterSignature`) folds every `(chunk coordinate, terrainPolygons().version)` pair
over the area's chunks into its signature hash (not the maximum: one chunk already at
version 2 would mask another moving 1 → 2), so any chunk rebuild triggers a mesh rebuild.

### D10: The renderer samples a signed-distance field, not band polygons

The rings are the truth, but the picture is painted per pixel in the chunk fragment shader
from a **terrain distance field** baked from those rings. No tessellated water fills, no
offset-polygon bands. Every visual effect below is a function of distance to the waterline,
so it is smooth at every zoom, continuous around river mouths, and free to vary along the
shore.

#### 10.1 What the bake produces (C++, generation worker)

Three textures per chunk, covering the chunk plus the apron so border samples are valid:

| Texture | Format | Texel | Channels |
|---|---|---|---|
| `terrainSdf` | RGB16F | 0.25 m near rings, 2 m far (two-level, see 10.4) | R: signed distance to the nearest non-synthetic ring edge, meters, negative in water, clamped ±8 m. G: distance to the nearest river thalweg divided by that channel's local half-width (0 on the thalweg, 1 at the bank), 2.0 where no channel is within 2 w. B: `WaterKind` of the nearest ring as a small integer (0 ocean, 1 lake, 2 wetland, 3 river, 4 pond), nearest-filtered |
| `shoreProfile` | RGBA8 | 1 m | R: slope. G: exposure. B: sand weight. A: mud weight. Packed from the CPU `ShoreProfile` of the nearest ring vertex: `{slope, exposure, sand, mud}`; grass = 255 − sand − mud; moisture and flags stay CPU-side |
| `channelFrame` | RGB16F | 1 m | R: arc length `s` along the nearest thalweg, meters (wraps at 256 m). G: width ratio `w / w_mean` (riffle > 1.1, pool < 0.9). B: signed curvature × hw, dimensionless (bend when |B| > 0.15). Only valid where `terrainSdf.g < 2` |

Substrate is stored as weights, not an enum, so bilinear filtering cross-fades sandy into
muddy shore over the texel spacing instead of snapping. `WaterKind` is sampled with nearest
filtering (it is categorical) and is what lets the shader give ocean, lake, and wetland
different band sets and foam. Slope comes from the elevation gradient across the ring;
exposure from ring concavity over a 20 m window (bays low, headlands high) plus fetch for
ocean.

```cpp
// TerrainDistanceField::bake, after TerrainPolygonBuilder::build
void bake(const ChunkTerrainPolygons& tp, const Chunk& c, DistanceFieldTextures& out) {
    // exact distance within kSdfNearM of any ring edge (edge list in a 4 m bucket grid),
    // chamfer sweep beyond; sign from ring containment (even-odd for Waterline, solid else)
    // tp.rings is the unclipped set; edges flagged synthetic (D4) or fordable-cut (D7) are skipped
    for (Texel& t : out.sdf.near())    t.r = signedDistanceExact(tp.rings, t.worldPos);
    for (Texel& t : out.sdf.far())     t.r = chamfer(out.sdf.near(), t);
    for (Texel& t : out.sdf.all())   { t.g = nearestThalwegDistanceOverHw(tp.thalwegs, t.worldPos);
                                       t.b = float(nearestRingWaterKind(tp.rings, t.worldPos)); }
    for (Texel& t : out.profile.all()) t = packProfile(nearestVertexProfile(tp.rings, t.worldPos));
    for (Texel& t : out.frame.all())   t = nearestThalwegFrame(tp.thalwegs, t.worldPos);   // s, w/w_mean, k*hw
}
```

Uploaded by `ChunkRenderer` beside the tile data texture, same LRU, keyed by
`terrainPolygons().version`.

#### 10.2 What we want, and how the shader does it

Every effect is written against `d` (signed distance, meters), `shore` (decoded profile) and
`frame` (channel arc length and width ratio). Widths are meters; nothing is in pixels, so the
look is zoom-independent, and anti-aliasing uses screen-space derivatives of `d`.

```glsl
vec4  sdf   = texture(u_terrainSdf,     sdfUv(worldPos));
vec4  prof  = texture(u_shoreProfile,   profUv(worldPos));
vec3  frame = texture(u_channelFrame,   profUv(worldPos)).rgb;
float d     = sdf.r;
int   kind  = int(texelFetch(u_terrainSdf, sdfTexel(worldPos), 0).b + 0.5);   // WaterKind, nearest
float isOcean = kind == 0 ? 1.0 : 0.0, isWetland = kind == 2 ? 1.0 : 0.0;
float aa    = fwidth(d);                         // one pixel, in meters, at this zoom
float slope = prof.r, exposure = prof.g, wSand = prof.b, wMud = prof.a, wGrass = 1.0 - wSand - wMud;
float along = 1.0 + u_alongAmp * fbm2(worldPos / u_alongWavelength);   // ±30-40 %, 4-9 m
```

**A smooth, sub-tile waterline at every zoom.** Bilinear sampling of an SDF reconstructs
straight edges exactly and curves to well under a texel. The water/land split is a single
smoothstep over one pixel:

```glsl
float water = 1.0 - smoothstep(-aa, aa, d);
```

**A line that never reads as a clean vector outline.** Wobble the distance itself with a
small world-space noise before anything else uses it. Amplitude stays under a texel so it
cannot open gaps against nav:

```glsl
d += u_wobbleAmp * fbm3(worldPos / u_wobbleWavelength);       // 0.12 m, 1.6 m
```

**Shore bands that change along the shore (L1, L2, D15).** A band is a coverage function of
`d` with a width that depends on the profile and on the along-shore noise; a width of zero
removes the band. Bands are painted low to high and each fades over its own tail, so a beach
turning into a mud bank cross-fades instead of stepping:

```glsl
float band(float d, float w) {                    // 1 at the waterline, 0 past w
    return w > 0.0 ? 1.0 - smoothstep(w * u_bandTail, w, d) : 0.0;
}
float gentle = 1.0 - slope;
float wDry = u_drySandW * gentle * wSand * along;
float wWet = u_wetSandW * gentle * wSand * along;
float wMudB = u_mudW    * gentle * wMud  * along;
col = mix(col, u_drySand, band(d, wDry));
col = mix(col, u_wetSand, band(d, wWet));
col = mix(col, u_mudBank, band(d, wMudB));
```

**Steep shores read as a cut edge, gentle ones almost no line (R2, L1).** The waterline
stroke is a distance band whose width and darkness grow with slope, drawn on both sides:

```glsl
float edgeW = u_edgeW * (1.0 + u_edgeSlopeGain * slope);
float edge  = (1.0 - smoothstep(0.0, edgeW, abs(d))) * mix(u_edgeMin, u_edgeMax, slope);
col = mix(col, u_edgeColor, edge);
```

**Shallows width from shore slope, depth as a color ramp (L2, L3).** Depth is inferred from
distance and slope, not stored: a gentle shore has wide shallows, a steep one a sliver. The
bed (the adjacent substrate's color) shows through the first stretch:

```glsl
float shallowsW = clamp(u_shallowsK / max(slope, u_slopeMin), u_shallowsMin, u_shallowsMax);
float t = clamp(-d / (shallowsW * along), 0.0, 1.0);      // 0 at shore, 1 deep
vec3 bed = wSand * u_drySand + wMud * u_mudBank + wGrass * u_bedGrass;
vec3 w   = mix(u_waterShallow, u_waterMid, smoothstep(0.0, u_midAt, t));
w        = mix(w, u_waterDeep, smoothstep(u_midAt, 1.0, t));
w        = mix(mix(bed, w, u_bedOpacity), w, smoothstep(0.0, u_bedFade, t));
```

**A river whose deep channel follows the thalweg, not the centerline (R2, R4, R5).** The
bake stores distance to the thalweg in units of the local half-width, so one threshold
serves a 1 m stream and a 60 m river:

```glsl
float channel = 1.0 - smoothstep(u_thalwegInner, u_thalwegOuter, sdf.g);   // 0.6, 1.1
w = mix(w, u_waterDeep, channel * u_thalwegDepth);
```

**Riffles at crossovers, pools at bends (R4).** A riffle is a wide, straight reach; a pool
is a narrow reach in a bend. Width ratio and curvature are both in the frame, so each rule
uses both. Riffles get cross-channel streaks by arc length; pools darken. Both animate along
`s` for flow:

```glsl
float bend   = smoothstep(0.10, 0.25, abs(frame.b));                 // curvature * hw
float riffle = smoothstep(1.1, 1.3, frame.g) * (1.0 - bend);
float pool   = (1.0 - smoothstep(0.8, 0.95, frame.g)) * bend;
float streak = 0.5 + 0.5 * sin(frame.r * u_riffleFreq - u_time * u_flowSpeed + fbm2(worldPos));
w = mix(w, u_riffleLight, riffle * streak * u_riffleAmp * (1.0 - channel));
w = mix(w, u_waterDeep,   pool * u_poolAmp);
```

**Water that is not one flat ramp: bars, weed, shimmer (D15).** Coarse world-space noise,
gated by depth so it never touches the shore bands, plus a fine animated shimmer:

```glsl
float v = fbm3(worldPos / u_patchWavelength);                 // 7 m
w = mix(w, u_barColor,  smoothstep(u_barT0, u_barT1, v)   * smoothstep(0.3, 0.5, t) * u_patchAmp);
w = mix(w, u_weedColor, smoothstep(u_weedT0, u_weedT1, -v) * smoothstep(0.3, 0.5, t) * u_patchAmp);
w += u_shimmerAmp * fbm2(worldPos * u_shimmerFreq + u_time * u_shimmerDrift);
```

**Foam only where waves break (R6, L4).** Exposed ocean shore, in a thin band, broken up by
animated noise; none on lakes and sheltered bays:

```glsl
// water side only: rises from zero at -u_foamW to full at the waterline, zero on land
float foamBand = water * smoothstep(-u_foamW, 0.0, d);
float foam = exposure * isOcean * foamBand
           * smoothstep(0.2, 0.6, fbm2(worldPos / u_foamWavelength + u_time * u_foamDrift));
w = mix(w, u_foam, foam);
```

Wetland rings (`isWetland`) take a different band set: no sand bands, the mud band wider,
reeds everywhere the exposure is low, no foam. Lakes get the sand/mud/grass bands and no
foam. Ocean gets everything.

**River mouths and confluences look continuous.** Nothing to do: `terrainSdf.r` is the
minimum over all rings, so bands follow the merged outline, and the thalweg term fades over
the flare because `sdf.g` rises to 2 there.

**Far zoom stays cheap.** When one pixel spans more than a band (`aa > u_lodBandM`), skip
wobble, patches, riffles, and shimmer; the depth ramp and bands still read at 2 px/m.

Composite:

```glsl
vec3 land = groundPass(worldPos);           // existing tile shading (a Water tile paints as bed)
land = applyBands(land, d, shore, along);   // dry sand, wet sand, mud
land = mix(land, u_edgeColor, edge);
vec3 wat = waterColor(d, t, shore, frame, worldPos);
wat = mix(wat, u_edgeColor, edge);
fragColor = vec4(mix(land, wat, water), 1.0);
```

#### 10.3 What stays geometry

Point bars (D12), reeds and overhanging grass (placement keyed to `distanceToWater`, ring
side, curvature, and the same exposure/substrate profile), boulders on rocky shore. These
are the things that need to occlude or be walked around; everything else is paint.

#### 10.4 Memory and resolution

`terrainSdf` at 0.25 m over a whole chunk would be 2048² texels, 25 MB of RGB16F, so it is
two-level: 0.25 m texels only in the 16 m tiles within `kSdfNearM` (8 m) plus a texel of the
shoreline, each 64² texels plus a gutter (66² × 6 B = 26 KB), and 2 m texels over the whole
square (258² × 6 B = 399 KB, left out when it is all land); the shader picks the level by a
32² tile map. `shoreProfile` (RGBA8, 514² × 4 B = 1.06 MB) and `channelFrame` (RGB16F, 514² ×
6 B = 1.59 MB) are dense, stored whenever the chunk has a shore vertex or a thalweg in reach.
The render tiles beside them are 518² × 2 B = 537 KB (D16 step 5).

Measured per chunk (WOR-463, the quickstart landing and coast): a dry chunk 0.54 MB (render
tiles only), open ocean 0.94 MB (plus the far level), a chunk with a pond or a short creek
2.1 to 2.4 MB, the coast chunk with a pond 4.5 MB, the landing's river chunks 6.9 to 7.8 MB.
The dense detail textures are the whole water cost of a small-water chunk and 2.6 MB of a
river chunk's 7.2; they are the lever if memory ever matters (sparse tiles like the near
level). The texture LRU holds 32 chunks, more than the 25 the chunk manager keeps loaded, so
nothing loaded is ever evicted; at zoom 0.25 after a 12-chunk scroll it held 57 MB (18 MB of
render tiles, 39 MB of distance field). Its ceiling, 32 river chunks, is about 250 MB. The
count stays right; a byte budget would be the change if a smaller GPU needs one.

**The near texel is 0.25 m.** WOR-465 shipped 0.5 m to save memory. At 0.5 m a channel under
a meter wide is two texels across, and the bilinear read of its distance valley beads it along
its length at the default zoom (zoom 3, 24 px/m): the landing's fordable feeder drew as a chain
of blobs. At 0.25 m it is a continuous ribbon of even width. River banks, ponds, and the coast
are the same at either size, and so is the tile pass's GPU time. The cost, measured in game:
the near level takes 3.7 times the memory (the landing's river chunks 0.85 to 0.96 MB of near
tiles become 3.2 to 3.6 MB, the coast chunk 0.5 to 1.9 MB) and about three times the bake
(those river chunks' 48 to 70 ms become 170 to 225 ms on the worker, the coast chunks' 9 to 13
ms become 21 to 27), all off the frame.

#### 10.5 Tunables

All `u_*` above are debug-server tunables from day one so the look can be A/B'd live; the
constants in this section are starting points, not the design. Initial values: wobble
0.12 m at 1.6 m; along-shore ±0.35 at 6 m; dry/wet sand 1.6/0.9 m, mud 1.1 m, band tail
0.65; edge 0.12 m base, slope gain 2.5, opacity 0.15..0.85; shallows k 0.9 m, clamp
0.5..6 m, slopeMin 0.15, midAt 0.45, bed opacity 0.5, bed fade 0.35; thalweg 0.6..1.1,
depth 0.9; riffle freq 2π / 1.5 m, amp 0.3, pool amp 0.35; patches 7 m, amp 0.45; foam
band 0.4 m; LOD band 0.5 m.

### D11: Vision and mud

**Shore points.** The builder, once the rings are built and indexed, walks every ring in
`rings` (Waterline, every Channel including fordable ones, Pond), skipping synthetic and
fordable-cut edges, at `kShorePointSpacingMm = 1000`, and emits a point offset
`kShoreOffsetMm = 300` toward the land side (every ring keeps water on its left) into
`shorePoints`, keeping only points inside the chunk square and not in water: a bank
submerged in the body it runs into (a channel's run into a lake, a tributary's bank inside
the trunk) would stand in water. The spacing restarts at every ring vertex on the pin
lattice, so a border run spaces its points as the neighbor's copy does. A fordable creek is
still drinkable water, so it is not filtered on `blocksMovement`. `VisionSystem` pass 3 iterates
`shorePoints` instead of `getShoreTiles()`; the synthetic `Terrain_Shore` def and its
Drinkable capability are unchanged. `Chunk::computeShoreTiles` and `getShoreTiles` are
deleted.

**Mud.** `generateMud` is replaced by one per-tile function,
`TilePostProcessor::finalSurface(raw surface, world tile, TerrainPolygonQuery, world seed)`:
a point-bar tile (D12) is Sand; else an eligible tile (Grass variants, Dirt, Sand) takes
`d = TerrainPolygonQuery::distanceToWaterMm(tileCenter)` over the chunk's own rings (0 in
water) and becomes Mud with probability `kMudProb(d) = 0.95` for `d <= 1 m`, `0.80` for
`d <= 2 m`, `0.65` for `d <= 3 m`. Fordable channels count. The roll is
`Chunk::tileHash(owning chunk, local tile, seed ^ kMudSalt)`, keyed by the world tile. The old
roll hashed chunk-local coordinates, which a neighbor reading the tile into its apron could
not reproduce, so "the same per-tile hash as today" could not stand. Because the unclipped
rings extend into the apron, banks no longer stop at chunk borders. The bar check comes
first; `barTiles` is what carries the exemption, since a bar tile's surface is Sand and Sand
is otherwise eligible.

`finalSurface` is a pure function of its arguments, so it answers for any tile of the
extended region: `Chunk::generate` runs it over its own tiles, and the D16 render tiles run
it over the neighbor's raw tiles from `ApronField` with the chunk's own query. The render
tiles are a function of the final surfaces `kRenderSurfaceReachTiles` = 9 tiles past the
square (the 3-tile render apron, the interior reach around it, a bed reach beyond), and a
tile that close to a border comes out the same from either chunk: its 3 m disc ends at most
12.5 m past the border, so it reads only ring edges in the lattice cells next to the border,
identical in both (D4), and its bar comes from centerline samples both chunks keep with the
whole bend decision (D12). `TerrainPolygonSeamsTest.BorderTilesPostProcessTheSameFromEitherChunk`
and `RealRiverBorderTilesPostProcessTheSameFromEitherChunk` hold it for every tile within
nine of a horizontal, vertical, and diagonal border.

**Order in `Chunk::generate()`:** computeTile → `TerrainPolygonBuilder::build` (rings
D5–D8, point-bar tiles D12, the edge index, shore points D11) → distance-field bake →
`finalSurface` per tile (bar Sand, then mud) → render data (the apron's tiles through
`finalSurface` too, D16) → version bumps.

### D12: Point bars as tile overrides

On the inner side of a bend where `|κ|·hw > kBarBend = 0.15` for at least 8 samples (4 m),
build a crescent: the inner bank offset landward by `sin(π·t)·kBarWidth·hw`,
`kBarWidth = 0.5` (peak ~0.25 w, inside the field range of ~0.4 w for the bankfull bar; the
wetted-side part of the bar is shading). Tiles whose center falls inside the crescent get
`Surface::Sand` and their bit in `barTiles` (D2), which mud reads (D11). The bar is
land-on-land, so its edge is handled by the ground shader's field
blend (the separate land-transition task), which is the right resolution for a gentle,
vegetation-fringed deposit. Bars are walkable.

The bend test is relative to width, hw the bankfull half-width: the same `|κ|·hw` the channel
frame stores and the shader draws pools by (10.1, `smoothstep(0.10, 0.25, |B|)`), so bars and
pools agree on what a bend is. R1 puts ordinary meander radii at 2–3 w, `|κ|·hw` of about
0.17–0.25, and R3 wants a bar on every inner bend. An absolute threshold such as 0.05 /m fires
only under a 20 m radius, so real rivers would never grow one.

As built (TerrainChannelBuilder, per reach, from the ribbon points the stroke uses):

- A bend is a run of ribbon points turning one way with `|κ|·hw` over `kBarBend`, at least
  `kBarMinPoints = 8` long; t is arc length along it over its length.
- A bend longer than `kBarArchMaxM = 64 m` ramps up and down over half that at each end
  (`sin(π/2 · min(1, s/32, (L − s)/32))`) instead of spanning the sine. So the bar at a sample
  reads the river at most 64 m along, a decision like a mouth's, inside the 74 m every chunk
  keeps around a relevant sample (D7 channel reach): two chunks draw the same bar near their
  border, whichever ends of the bend each sees.
- The width fades out over a mouth flare with the bend asymmetry's share (D7 mouths), so a
  mouth's extension into the lake grows none.
- The crescent is held under the local radius of curvature, as the inner bank is: past the
  center of curvature it would fold.
- Its inner edge is the stroke's own inner bank point; the crescent is the union of the
  quads between consecutive samples, tested per tile center with exact orientation
  predicates.
- A tile that is `Water` in the raw tiles is never a bar, and the bits cover the whole
  extended region (D2).

### D13: Land-on-land priority order

Unchanged: Mud < Sand < Dirt < GrassShort < Grass < GrassMeadow < GrassTall < Rock < Snow,
higher paints over lower. Water is no longer in the stack; it is a polygon drawn on top.
How the boundaries between these surfaces are drawn is D16.

### D15: Variance, or why a shore must never read as a stroke

Constant-width bands along the whole ring with a single noise amplitude read as a thick
stroke around the tile mask. A real shore varies along
its length in kind, not just in wobble. The rule for every renderer and placement consumer:
no along-ring parameter is a constant. Every band width, stroke weight, color, and density is
a function of position on the ring and of a per-vertex **shore profile**, which the builder
computes once and stores alongside the ring:

The struct is `ShoreProfile` in D2 (slope, exposure, sand and mud weights, moisture,
flags); 10.1 defines how the first four are packed into the `shoreProfile` texture.

What each source drives (renderer task and placement rules consume this):

- **Slope** sets the width of every band: wet sand and shallows wide on gentle shores, a
  sliver on steep ones; the waterline stroke weight and darkness rise with slope (a steep
  shore is a cut edge, a gentle one has almost no line).
- **Substrate** picks the band set: sandy beach (dry sand, wet sand, pale shallows), mud bank
  (dark mud band, brown-green shallows, reeds), grassy edge (grass to the waterline, a thin
  wet line, overhanging tufts), rocky (no sediment band, dark edge, boulders from placement).
  Substrate changes along the ring where the adjacent surface or biome changes, and the
  bands cross-fade over ~2 m so no band starts with a step.
- **Exposure** gates reeds and floating plants (sheltered only), foam and wrack (exposed
  only), and biases substrate toward sand and rock on exposed stretches, mud in bays.
- **Along-ring noise** modulates every width by ±30–40% at 4–9 m wavelength so even a
  uniform stretch of beach is not uniform.
- **Bottom** is patchy: sandbars (pale) and weed beds (green) from a coarse noise gated by
  depth, drawn over the depth gradient, so open water is not one flat ramp of blue.
- **Rivers**: the deep channel follows the thalweg (offset toward the outer bank by
  curvature, wandering in straights), not the centerline, so the two banks never look alike;
  width varies from the segment riffle/pool modulation plus fBm; deposits appear on inner
  bends and, from a deposit noise, on straights; wide straight reaches occasionally split
  around a vegetated island; cut-bank strokes are broken by noise rather than continuous;
  the bank's pale edge varies in width along its length.
- **Vegetation** is clustered by a 6 m patch noise times the profile gates, never uniform
  along the bank.

The waterline warp itself is multi-scale (D6): the 26 m term shapes bays and headlands, the
6 m fBm shapes the bank. A single amplitude at a single scale is exactly the stroke look.

### D14: Determinism

Every step is a pure function of (tile data, sample result, world position, fixed seeds).
No dependence on generation order, thread count, or which neighbors are loaded. Hash-seeded
choices (mud rolls) use `Chunk::tileHash` keyed by the world tile (its owning chunk and local
coordinates) with a per-purpose salt, never chunk-local coordinates alone, so a neighbor
evaluating the tile rolls the same. This is required for multiplayer later and for D4's seam
guarantee now.

### D16: Every land boundary gets the waterline treatment

Water was the loudest staircase, not the only one. Grass against dirt, sand against grass,
rock outcrops, snow lines, and the mud band all still read as 1 m tiles, and next to a smooth
waterline the contrast is worse than before. The rule: no surface boundary is drawn on the
tile grid. Every one is the isoline of a softened, domain-warped field, the same recipe as
D5/D6, so a grass/dirt edge and a shoreline are of a piece.

Land boundaries are look-only (nothing walkable, D1), so they stay in the shader instead of
becoming rings. `engine::world::evaluateSurfaceField` (`SurfaceField.cpp`) is the one
evaluation; `shaders/includes/land.glsl` is the same steps in the same order, and
`SurfaceFieldGolden.test.cpp` renders it and compares.

1. **Field per surface, on the dual grid.** For every surface in the point's 4x4 tile
   neighborhood (the blur's reach plus the bilinear footprint), the indicator is sampled at tile
   centers, softened with the D5 3x3 binomial, guarded by D5's thin-feature guard, and read
   bilinearly. The blur and guard are the waterline's own code (`ThinFeatureGuard.h`); floor
   and ceiling are live tunables here, defaulting to D5's 0.85 / 0.15. Unwarped, a lone tile or
   1-tile hole becomes a rounded blob 0.97 m across on the axes and 0.78 m on the diagonals, a
   straight 1-wide path a band 1.17 m wide, and a diagonal 1-wide path is 0.58 m wide at its
   narrowest saddle. Floor plus ceiling is 1, so where only two surfaces meet their fields
   still sum to 1 and the guard never opens a gap between them.
2. **One warp per point.** Every surface's field is read at the same warped point
   `q = p + W(p)`, so isolines of different surfaces cannot drift apart into gaps or overlaps.
   `W` is the D6 world-space vector noise: a fine term (4 octaves, gain 0.6, base wavelength
   4 m) plus a low term (2 octaves, gain 0.5, base 26 m), each an fBm pair
   (`foundation::fractalNoise2SplitPair`, two fields sharing each lattice corner's hash) giving
   x and y, each component clamped to +-1.5 m. The noise is read 0.375 m off the tile grid:
   gradient noise is zero at its lattice points, which at whole-meter wavelengths sit on tile
   corners, the finer octaves' on every corner, and tile corners are where unwarped edges run.
   Amplitudes are per surface, not per pair: at a point they blend bilinearly between tile
   centers, each tile taking its edge surface's, the highest surface within 2 tiles, so an edge
   takes the character of its upper surface. Per pair was impractical: the warp has to be one
   per point (above), a point near a junction sees several pairs, and 45 pairs x 2 amplitudes is
   not a set anyone can tune. Defaults: grass variants, dirt, sand, and mud 0.55 m fine / 0.7 m
   low; snow 0.45 / 0.7; rock 0.2 / 0.4, a harder edge. The waterline's 0.25 m at 6 m barely
   bends the 2 to 4 tile straight runs of a small land patch. Positions are a whole world tile
   plus the offset inside it and every noise input is formed from the two
   (`fractalNoise2SplitPair`), so the field's precision does not depend on the distance from the
   origin, in C++ and GLSL alike.
3. **Priority paint.** Low to high per D13: the highest surface whose field passes 0.5 paints.
   Where none passes (three or more surfaces meeting) the largest field paints, ties to the
   higher surface. Anti-aliased over one pixel (`u_metersPerPixel`, as the water pass does)
   against the nearest boundary of the painted region, its distance `(F - 0.5) / |grad F|` from
   the bilinear gradient. The edge look is a function of that distance only: a sparse fringe of
   the upper surface on the lower side (0.35 m for grass, 0.25 sand, 0.2 dirt and mud, none for
   rock) whose width follows the fine warp noise +-35 % along the edge (D15) and is broken up by
   a 1 m noise, and a rim darkening just inside the upper surface (rock only, 30 % over
   0.12 m). No term reads a tile edge.
4. **Mud** (D11 makes it distance-to-water) and **point-bar sand** (D12) are ordinary surfaces
   in the stack once they are tiles, so they get the same edge.
5. **Seams and render tiles.** The shader reads RG8UI render tiles over the chunk square plus
   `kRenderApronTiles` = 3 on every side (518 x 518, 524 KB a chunk; the RGBA32UI tile data it
   replaces, with edge, corner, and family masks and eight neighbor ids, was 4 MB). R is the paint
   surface, G the edge surface (low nibble) and an interior bit (0x80). A point reads its field
   at most 3 tiles out (1.5 m warp, the bilinear footprint, the blur), exactly the apron. The
   chunk fills it from its `ApronField`, each tile post-processed with
   `TilePostProcessor::finalSurface` over the chunk's own `TerrainPolygonQuery` (D11), so apron
   tiles equal the neighbor's own final tiles and border pixels on both sides evaluate one world
   function. Building them reads final surfaces 9 tiles past the square
   (`kRenderSurfaceReachTiles`: the edge surface and interior bit of an apron tile read 3
   further, a Water tile's bed 3 more), which D11 holds that far. A Water tile paints as its bed:
   the surface of the nearest non-Water tile within 3 tiles, least squared distance first, then
   least world y, then least world x, and Sand when there is none. A function of the tiles
   around it and nothing else, so every chunk that holds the tile agrees on it.
   `RenderTileSeamsTest` generates a dry grassland and the carved-river world, whose river runs
   along a chunk border, and asserts every apron render tile equals its owner's own and the
   field is bit-identical from the owner's tiles and the world's at 0.25 m steps within 2 m of
   every border and corner (26 800 points on the river world, a fifth of them painted mud).
6. **Agreement with placement.** Groundcover and flora placement read surfaces; a tuft on
   painted dirt looks wrong. The per-point field evaluation exists once in C++ (a pure
   function shared with the shader by construction and checked by golden-value tests against
   the GLSL), and placement asks it instead of the raw tile surface.
   `ChunkPlacementContext` answers two questions of a world position: `isWater`, the chunk's
   `TerrainPolygonQuery::isInsideWater` (D1), and `landSurface`, `evaluateSurfaceField` over a
   snapshot of the chunk's render tiles with the land tunables as the game thread holds them.
   Every water test is `isWater` at the instance, or at the center of the sampled tile before
   any roll; a near rule (`near="Mud"`) reads the surface drawn at the tile centers within its
   distance, water where the rings hold it, else the painted land. The snapshot shares the
   chunk's polygons (`Chunk::sharedTerrainPolygons`) because a placement task can outlive the
   chunk. Cost, measured in game: about 40 ms of a chunk's 140 to 180 ms of placement on the
   quickstart landing, about 80 of 550 ms in dense grassland (150 000 instances).
7. **Cost.** A tile is interior when every tile within 3 of it is one surface; its pixels cost
   one fetch and one atlas sample, as before. Elsewhere a pixel costs 21 texel fetches, six
   pair-noise octaves, and the per-surface blur and guard of the surfaces present. The shader
   holds no arrays: a const array indexed at run time is copied to local memory in every
   fragment on NVIDIA. Measured at 3072x1728 on an RTX 3090 (section 6), the land pass is 0.3
   to 0.9 ms on the landing's river view, where most tiles are interior, and 0.6 to 1.3 ms on
   the busier coast and desert views; the six warp octaves are 0.3 to 0.5 ms of that, the
   fringe path 0.05 to 0.1 ms. At far zoom the shader drops a fine warp octave whose
   wavelength is under `kLandLodMinWavelengthPx` = 4 pixels (`landWarpFineOctaves`): zoom
   0.25 sums the 4 m and 2 m octaves, 0.5 and 0.75 three, zoom 1 and in all four. The kept
   octaves weigh what they do in the full sum, so an edge moves by at most the dropped
   octaves' share of the widest fine amplitude, 0.146 m at 0.5 m a pixel (0.099 m measured):
   under a third of a pixel, detail the pixel cannot show. Placement always evaluates the
   full field; `SurfaceFieldGolden.test.cpp` holds the shader to it on the full-detail path
   and holds the LOD to that bound, and `SurfaceFieldTest.FarZoomDropsFineOctavesUnderFourPixels`
   pins where it kicks in. It saves about 0.1 ms at zoom 0.25.

Biome water edges themselves are still quantized to 16 m sectors upstream of all this
(WOR-467); D16 smooths what the tiles give it, it does not add large-scale variety.

---

## 2.9 Realism rules for banks and shores

From the geomorphology survey (references in section 9). [S] is supported by the cited
sources, [I] is our inference. Each rule names its consumer.

**Rivers** (w = local width, hw = w/2):

- R1 [S] Meander wavelength 10–12 w, bend radius 2–3 w, belt width ~4–6 w. `RiverNetwork2D`
  already meanders; the constants should be checked against these ratios (worldgen, later).
- R2 [S] Outer bend is a cut bank: crisp, darker edge, deepest water against it, undercut
  just downstream of the apex. Renderer: a thin dark stroke along the outer bank where
  `|κ|·hw > 0.15` (the channel frame's bend, the one the point bars use), offset 0–0.3 m
  downstream of the apex; pool shading (darker) hugging that bank. Geometry: D7 step 3.
- R3 [S] Inner bend is a point bar: light sand/gravel crescent from the apex tapering
  downstream, grading into the water with no hard edge, ~0.4 w wide. D12 for the land part;
  renderer shades the wetted part as pale shallows.
- R4 [S] Riffles at crossovers every 5–7 w, pools at outer bends. Riffle water is rough,
  lighter, wide; pool water smooth, dark, narrow. Renderer: use the width modulation already
  in the segments (`kWidthWavelen`, `kPoolWavelen`) as the signal: width above the local mean
  and low curvature = riffle streaks; high curvature = pool.
- R5 [S/I] Bankfull outline widest at the apex, wetted channel narrowest there. D7 step 3.
- R6 [S] Foam only at the pool head below a riffle, trailing 1–3 w along the current seam,
  and against the outer bank through a bend. Nowhere else. Renderer.
- R7 [S/I] Confluence: tributary enters at an acute angle (the feeder generator already
  does this); trunk widens below the junction only if the tributary is ≥ 0.6–0.7 of the
  trunk; a light bar on the downstream junction corner, a dark scour in the middle, a color
  seam fading over a few widths. Renderer shading; no geometry.
- R8 [S] Streams under 2 m are narrow, deep, sinuous, and their edges are vegetation, not
  sediment: grass and sedge overhang and hide the waterline. Renderer plus placement: reeds
  and overhanging grass keyed to `distanceToWater` on the land side; the ribbon itself drawn
  dark with no cut-bank stroke. Never braided.
- R9 [S] Bank vegetation: emergent reeds only in shallow calm water (inner bends, backwaters,
  sheltered bays); bare soil and roots on eroding outer banks; grass to the edge on stable
  straight reaches. Placement rules keyed to ring side and curvature.

**Lakes and seas** (from land to water):

- L1 [S] Band sequence: dry backshore → wrack line (dark irregular debris stripe) → wet sand
  (darker) → swash → surf/breakers → shallows → deep. Renderer: three clipped strokes on the
  land side (wrack 0.2 m, wet sand 0.8 m, dry sand 0.4 m) and three on the water side.
- L2 [S] Shore slope sets band widths: steep shores narrow, gentle shores wide. We have
  `TileData::elevation` (cm); the local slope across the ring is the gradient of elevation
  between the land-side and water-side tiles. Renderer: `shallowsWidth = k / slope`,
  clamped to [0.5 m, 6 m], `k` tuned per water kind (ocean larger than lake).
- L3 [S] Water color is a depth gradient: very pale under ~0.5 m, turquoise over sand to a few
  meters, then saturated dark; bottom type tints it. Renderer: exponential toward the deep
  color by distance-to-ring scaled by slope, bed color from the adjacent surface.
- L4 [S] Foam only in a surf zone and on the lee shore after onshore wind; none on calm lake
  shores. Renderer: foam ring on Waterline rings whose water biome is ocean, none otherwise.
- L5 [S] Reeds and cattails on sheltered shores with soft sediment in < 1.5 m of water;
  exposed shores show bare sand/gravel. Placement keyed to ring concavity (bays are
  concave toward the water) and biome.

---

## 3. Data flow

```
GeneratedWorldSampler::sampleChunk(coord)          main thread
  biome/elevation for chunk + kApronTiles apron
  riverSegments, pondBlobs for AABB + apron
        │
Chunk::generate()                                   worker thread
  computeTile ×(512 + 2·kApronTiles)²   (apron tiles discarded after the build)
  TerrainPolygonBuilder::build  ← D5 field, D6 warp+march, D8 ponds, D7 channels; rings + navRings,
                                  barTiles (D12), edgeIndex, shorePoints (D11)
  TerrainDistanceField::bake    ← rings, thalwegs → terrainSdf, shoreProfile, channelFrame (10.1)
  TilePostProcessor::process    ← finalSurface per tile: bar Sand, then mud by
                                  TerrainPolygonQuery::distanceToWaterMm (D11)
  render data                   ← render tiles, the apron's tiles through finalSurface (D16)
  m_terrainPolygons.version++, m_renderDataVersion++
        │
        ├── NavInputBuilder::buildInput      navRings → NavInputPolygon (D9)
        ├── ChunkRenderer                    uploads the textures; tile.frag paints (D10, D16)
        ├── VisionSystem pass 3              shorePoints (D11)
        ├── ToiletLocationFinder             distanceToWaterMm (not on the shore)
        └── PlacementExecutor                isInsideWater, the land field (D16); later
                                             distanceToWater, ring side, curvature (R8, R9, L5)
```

```cpp
// TerrainPolygonBuilder::build, on the generation worker
ChunkTerrainPolygons build(const Chunk& c, const ChunkSampleResult& sr, const ApronField& apron) {
    const RectMm region   = chunkSquareMm(c.coordinate());               // [origin, origin + 512 m)
    const RectMm extended = region.expanded(kApronTiles * kTileMm);
    ChunkTerrainPolygons out;

    // D5: biome water only, by primaryBiome; surface==Water from rivers/ponds is NOT water here
    ScalarField coarse = indicator(c, apron, isBiomeWater);              // outside `extended` = land
    coarse = binomial3x3(coarse);
    applyThinFeatureGuard(coarse, kThinFeatureFloor, kThinFeatureCeil);

    // D6: domain-warp onto the fine lattice, then march once
    ScalarField fine = geometry::warpField(coarse, kTileMm, kFineCellMm, shoreWarpOffset);
    for (geometry::Ring loop : geometry::marchingSquares(fine, 0.5f, extended.min, kFineCellMm)) {
        geometry::chaikin(loop, kChaikinIterations);
        geometry::resampleRing(loop, kRingSpacingMm);
        quantizeInPlace(loop);
        geometry::simplifyRing(loop, kRingSimplifyEpsMm);
        if (!validateSimple(loop)) continue;                             // D6 step 7 retries inside
        if (areaMm2(loop) < kMinLoopAreaMm2) continue;
        TerrainRing tr{loop, {}, Waterline, waterKindOf(loop, c, apron), true, true, 0};
        markSyntheticEdges(tr, extended);                                // D4
        out.rings.push_back(std::move(tr));
    }

    // D8; the D7 mouth test reads the pond rims and the waterline field, not these rings
    for (const Pond& pond : sr.pondBlobs) emitPondRing(out, sampleRim(pond));

    // D7: channels from source segments
    for (const ChannelPath& ch : joinSegments(sr.riverSegments)) {        // Catmull-Rom, 0.5 m
        ChannelPath flared = flareIntoReceivingBody(ch, out.rings);       // Waterline or Pond
        for (const ChannelPiece& piece : splitAtFordableWidth(flared, kFordableWidthM))
            emitChannelRing(out, strokeChannel(piece), piece.blocks);     // butt joins at cuts
        out.thalwegs.push_back(thalwegOf(flared));                        // offset toward outer bank
        markPointBars(flared, apron, out.barTiles);                       // D12, extended region
    }

    computeShoreProfiles(out, c, apron);                                  // D15, per ring
    for (const TerrainRing& tr : out.rings)
        for (geometry::Ring piece : geometry::clipRingToRect(tr.ring, region))
            if (validateSimple(piece)) out.navRings.push_back(tr.withRing(std::move(piece)));
    out.edgeIndex   = TerrainPolygonQuery::buildIndex(out.rings, extended);
    out.shorePoints = sampleShorePoints(out, region, kShorePointSpacingMm, kShoreOffsetMm);  // on land only
    return out;
}
```

Terraform (later): a tile edit marks the chunk dirty; the builder reruns for that chunk and
for any neighbor whose apron contains the edited tile (up to eight, since the apron is 20 m). Ring version bump
invalidates the render cache and, through the nav signature, the mesh.

---

## 4. Parameters

| Name | Value | Where |
|---|---|---|
| `kApronTiles` | 20 | D4 |
| `kPinLatticeMm` | 16 000 | D4, D6 |
| `kFineCellMm` | 250 | D6 |
| `kThinFeatureFloor` / `kThinFeatureCeil` | 0.85 / 0.15, axis rule; the land field's tunables default to them | D5, D16 |
| `kChaikinIterations` | 1 | D6 |
| `kRingSpacingMm` | 250 | D6 |
| `kBankNoiseAmpMm` | 250 (waterline warp), 0.15·hw (channel banks), 125 (pond rim) | D6, D7, D8 |
| `kShoreLowAmpMm` / wavelength | 1200 / 26 m (waterline), 0.35·hw / 14 m (channels) | D6, D7 |
| noise octaves / base wavelength / gain | 4 / 6 m / 0.5 (waterline); 3 / 4 m / 0.5 (channels) | D6, D7 |
| `kRingSimplifyEpsMm` | 100 | D6 |
| `kMinLoopAreaMm2` | 250 000 | D6 |
| `kFordableWidthM` | 1.2 | D7 |
| `kMouthFlare` / `kMouthFlareW` | 1.6× / 2 w, capped at 64 m | D7 |
| `kMouthExtendW` | 1 w, capped at 24 m | D7 |
| width-ratio window | 5 w, each half capped at 64 m | D7 |
| `kRiverGatherMarginM` | 490 m | D7 |
| `kSdfNearTexelMm` / `kSdfNearM` / far texel | 250 (0.25 m, why in 10.4) / 8 m / 2 m | D10, 10.4 |
| shader `u_*` | see 10.5 | D10 |
| `kBarBend` / `kBarWidth` | 0.15 (`|κ|·hw`, dimensionless) / 0.5 | D12 |
| `kBarMinPoints` / `kBarArchMaxM` | 8 samples / 64 m | D12 |
| `kShorePointSpacingMm` / `kShoreOffsetMm` | 1000 / 300 | D11 |
| `kMudProb(d)` | 0.95 / 0.80 / 0.65 at ≤1 / ≤2 / ≤3 m (`TilePostProcessor::kMudBands`) | D11 |
| `TerrainPolygonQuery::kCellMm` | 16 000 (the pin lattice) | D3 |
| `kRenderApronTiles` / `kRenderSurfaceReachTiles` | 3 / 9 | D16 |
| land warp, fine / low | 4 octaves, gain 0.6, base 4 m / 2 octaves, gain 0.5, base 26 m; clamp 1.5 m a component; read 0.375 m off the tile grid | D16 |
| land warp amplitude by upper surface, fine / low | grass variants, dirt, sand, mud 0.55 / 0.7 m; snow 0.45 / 0.7; rock 0.2 / 0.4 | D16 |
| edge surface / interior / bed reach | 2 / 3 / 3 tiles | D16 |
| land fringe width / rim | grass 0.35, sand 0.25, dirt and mud 0.2, rock 0 m; opacity 0.55, +-35 % along the edge, 1 m breakup / rock 30 % over 0.12 m | D16 |
| `kLandLodMinWavelengthPx` | 4 px: the shader drops a finer fine-warp octave | D16 step 7 |

All of these are candidates for the debug server's tunables so the look can be A/B'd live.

---

## 5. Seams and determinism checklist

- Same samples on both sides of a border: apron from the sampler, not from neighbors.
- Same noise: world-space fBm, fixed seeds, `foundation::fractalNoise3`.
- Same warp: a function of world position only, so the fine field is identical on both
  sides; Chaikin is local; the 20-tile apron holds the border lattice cell clear of the edge
  effect.
- Same runs: pins on the world lattice, so every resample and simplify run is one lattice
  cell's worth of curve; the fold-retry ladder steps per cell.
- Same river decisions: mouths read the waterline field and pond rims at world positions;
  each chunk keeps every centerline sample a border ring edge or bake-region thalweg point
  depends on, plus the 74 m of river its decisions read, all inside the gather.
- Same integers: quantize before clipping; clip against an integer rectangle.
- Synthetic edges never reach a consumer: the bake, shore points, mud, and queries skip them;
  nav gets `navRings`, which end at the chunk square.
- Same surfaces: mud rolls hash the world tile, a point bar reads at most 64 m of river
  (inside what every chunk keeps), and a tile's distance to water within 3 m reads only the
  border lattice cells, so `finalSurface` gives a border tile the same surface from either
  chunk (D11).
- Test (`TerrainPolygonSeamsTest`): build adjacent chunks (horizontal, vertical, diagonal)
  independently and assert every non-synthetic ring edge within 8.25 m of the shared border and
  every thalweg point a texel in both bake regions reads are identical, and identical to a
  48-tile-apron build; `TerrainPolygonBuilderTest` builds the nav mesh over both and asserts
  no face touches a border gap. The surface tests evaluate every tile within nine tiles
  (`kRenderSurfaceReachTiles`) of each border from both chunks (own tile, apron tile) on a
  hand-built world with lakes, wetland pools, and tight meanders on the borders, and on the
  carved-river world through `Chunk::generate`, and assert the same bar bit, distance to
  water, and final surface. `RenderTileSeamsTest` then checks the render tiles and the land
  field built from them (D16 step 5).

---

## 6. Performance budget and risks

Measured in WOR-463 (2026-09-26): RelWithDebInfo, an RTX 3090, a 3072x1728 window, the
quickstart planet, vsync off. Three views: the landing river and its fordable feeder at
(0, 20), the ocean coast along a chunk border at (70030, 0), and a desert edge at
(540, 131480). Each figure is the median of nine samples once the view has settled. The frame
pacer caps the game at 120 fps (8.33 ms) and has no switch, so GPU cost comes from timestamp
queries: `/api/metrics` reports `gpuRenderMs` (the scene) and `tileGpuMs` (the ground and
water pass), and `scripts/perf-capture.ps1` records both. The before/after capture pair is in
`perf-results/capture-2026-09-26-wor463-*.json`; "before" is main at 9153fc44, the last
commit before the shader work (WOR-460), with the same GPU timers. That pair predates the
script's default bump to nine and was taken with 7 idle samples per scenario (25 for the
scroll scenarios, unchanged); the Idle rows in the frame table below come from it, not from a
nine-sample run.

**The tile pass (land and water), GPU ms.** Budget: under 1.5 ms at every zoom. It holds.

| Zoom | River | Coast | Desert |
|---|---|---|---|
| 20 | 0.94 | 1.09 | 1.22 |
| 8 | 0.93 | 1.12 | 1.22 |
| 3 | 0.83 | 0.67 | 0.65 |
| 1.5 | 0.58 | 1.43 | 1.23 |
| 0.75 | 0.46 | 1.20 | 1.03 |
| 0.5 | 0.44 | 1.00 | 0.89 |
| 0.25 | 0.36 | 1.06 | 0.87 |

Water is 0.05 to 0.19 ms of it (the pass with the water half cut out of the shader); the land
field is the rest (D16 step 7). The pass it replaced cost 0.04 to 0.51 ms.

**The frame, river view** (the heaviest), frame p50 / p99 max and scene GPU, ms:

| Scenario | Before | After | GPU before | GPU after |
|---|---|---|---|---|
| Idle, zoom 20 | 8.32 / 8.78 | 8.32 / 8.89 | 1.22 | 1.63 |
| Idle, zoom 3 | 8.32 / 8.42 | 8.32 / 8.43 | 2.65 | 2.79 |
| Idle, zoom 1.5 | 8.32 / 8.60 | 8.32 / 8.75 | 4.98 | 5.15 |
| Idle, zoom 0.75 | 17.2 / 20.5 | 18.7 / 21.3 | 18.0 | 17.9 |
| Idle, zoom 0.5 | 37.2 / 39.9 | 37.4 / 41.6 | 37.3 | 37.9 |
| Idle, zoom 0.25 | 115.6 / 124.5 | 117.7 / 124.8 | 115.7 | 118.8 |
| Scroll, zoom 3 | 8.32 / 167.8 | 8.32 / 16.6 | 2.18 | 2.40 |
| Scroll, zoom 0.75 | 262.4 / 920.3 | 15.6 / 25.2 | 16.8 | 16.2 |

The ground pass adds 0.3 to 1 ms of GPU and leaves the frame where it was. From zoom 0.75 out
the frame belongs to the entity pass, before and after: over the landing forest the tree pass
is GPU-bound (24k draw calls, 4.6M triangles at zoom 0.25), and on the coast and desert views,
which hold 120 fps down to zoom 0.5, the entity pass costs 16 to 17 ms of CPU at zoom 0.25
(frames of 17 to 19 ms either way). The 2026-06-10 overhaul table's 120 fps at zoom 0.25 was
measured on an earlier world without that forest. Scrolling used to stall the main thread on
nav: a region recentering waited on its running build (a std::async future blocks in its
destructor), up to 1.2 s. A region now recenters once its build lands
(`NavigationSystem::reconcileRegions`); a 15 s pan at zoom 0.5 across the landing tops out at
53 ms a frame (1183 before), at zoom 0.25 at 148 (1271).

**Chunk generation, worker ms** (192 generations, `[ChunkGen]` in the dev-tools log): median
56, p90 126, max 314. By kind, medians:

| Chunk | Total | Tiles | Apron | Polygons | Bake | Surfaces | Render tiles |
|---|---|---|---|---|---|---|---|
| Land (110) | 52 | 26 | 5 | 6 | 0 | 7 | 9 |
| A pond or creek (56) | 62 | 28 | 5 | 6 | 8 | 7 | 9 |
| Open ocean (3) | 70 | 10 | 3 | 43 | 2 | 1 | 10 |
| Coast (3) | 146 | 30 | 5 | 60 | 30 | 7 | 10 |
| Landing river (10) | 285 | 49 | 10 | 14 | 196 | 10 | 9 |

The tile raster doubles on river chunks (every tile scans the nearby segments). An ocean or
coast chunk's polygons are the waterline's fine lattice: the warp noise along the shore (12
noise octaves a warped sample) and, before WOR-463, a 40 ms walk over the one-sided rest, now
filled in spans (ocean 88 to 96 ms became 43, coast 87 to 120 became 60). A river chunk's
time is the 0.25 m bake (10.4). Chunks are ready a median 91 ms after their request (p90
173, max 343). At zoom 0.25's fastest pan, 800 m/s, a new chunk column is due every 0.64 s and
the workers keep up; the far edge of a zoom 0.25 view is still blank, because it is 1536 m
wide and the chunk manager keeps one chunk around the camera's (1536 m), and flora appears
when placement lands (140 to 550 ms a chunk).

**Nav.** Rebuilding the landing region (128 m square, 643 blocking rings, most of them trees):
763 ms on the worker for 5434 triangles, against 499 ms for 3082 with the tile marcher
(39f35c45); the input extraction on the main thread is 0.5 ms (1.4 with the marcher). The
coast region builds in 0.7 to 1.4 ms, the desert in 0.4 to 0.6. Region rebuilds while
scrolling take 190 to 550 ms, now off the main thread entirely.

**Memory.** Per chunk, 0.54 MB dry to 7.8 MB on the landing river; the texture LRU held 57 MB
at zoom 0.25 (10.4).

Risks left:

- The far-zoom entity pass above, which this epic does not touch: trees are one draw call per
  depth-sorted run, and short flora's cutoff does not reach them.
- Placement on the main thread while streaming: storing a chunk's placement costs 10 to 14 ms,
  unloading a column about 50 ms.
- Coast chunks: the warp noise along the shore (about 60 ms). Caching each noise cell's corner
  hashes along a lattice row would cut it without changing a value.
- Divergence between drawn and walked water is zero by construction. The residual gameplay
  risk is the fordability threshold: a 1.1 m stream walked across while the picture shows
  knee-deep water. Tune with the debug tunables; the threshold is one constant.

---

## 7. Phasing

Maps one-to-one onto the epic's tasks:

1. This spec (done when merged).
2. `libs/geometry/contour/` + `TerrainPolygonBuilder` for the waterline (D3–D6), with the
   seam test from section 5. Nav still marches tiles at this point; nothing user-visible yet.
3. Channel and pond rings (D7, D8), fordable split with butt joins, mouth flare. Includes
   the worldgen change: `RiverNetwork2D` stops flooring emitted half-widths at
   `kRenderMinHalf` (RiverNetwork2D.cpp, the emit path and the feeder-mouth width), so
   sub-1.2 m streams reach the builder at their true width.
4. `TerrainDistanceField` bake per chunk (10.1, 10.4): `terrainSdf`, `shoreProfile`,
   `channelFrame`, two-level storage, seam test extended to the textures (border texels
   identical on both sides).
5. Nav reads rings, tile marcher deleted (D9). First user-visible behavior change: colonists
   stop at the smooth waterline, step over creeks.
6. Shader: `tile.frag` implements 10.2 against the three textures, all `u_*` as tunables;
   delete the tile water branch and the water bleed.
7. Vision shore points and distance-based mud, point bars (D11, D12).
8. Every land-on-land boundary drawn as a warped field isoline (D16): tile-data apron,
   per-surface field with the D5/D6 recipe, priority paint, fringe bands, placement reading
   the same field. Replaces the neighbor bleed in `tile.glsl`.
9. Perf validation across zoom levels.

---

## 8. Open questions

- Resolved (WOR-461): `TerrainPolygonQuery`'s 16 m edge-bucket index is built up front in
  the builder and stored with the polygons (`edgeIndex`), with each edge-free cell's
  containment, so the 262k mud queries per chunk mostly cost one cell lookup. Measured in
  RelWithDebInfo over 27 chunk generations (quickstart landing, coast at x = 70 km):
  index 0.04 ms median (0.15 max), bars under 0.03 ms, shore points under 0.21 ms, the
  per-tile bar and mud pass 7.7 ms median (1.1 to 12.5), against 69 ms median for
  generation through that pass.
- Resolved (WOR-461): the point-bar bend test is width-relative, `|κ|·hw > kBarBend = 0.15`,
  the channel frame's bend (D12), replacing an absolute 0.05 /m that only fired under a 20 m
  radius, so the quickstart landing river grew no bar at all. With it, the landing
  neighborhood (chunks -1..1 around the landing) grows two: 10 tiles at (23, -31) m and 20
  tiles at (-348, 361) m, both on inner banks. That river's bends are gentle: its curvature
  times half-width peaks at 0.27, and of its seven apexes past 0.15 only these two stay past it
  for `kBarMinPoints` (4 m of arc); the rest cross it for 0.5 to 2.5 m. More bars there
  would mean a lower threshold or a shorter minimum, a look decision for the tuning pass.
- Resolved (WOR-489): the waterline's guard was "fewer than two same-type cardinal
  neighbors", which left a straight 1-wide inlet of biome water, or a straight 1-wide land
  isthmus between two water bodies, blurred to exactly 0.5 along its centerline (the inlet
  vanished, the isthmus flooded over). It now takes the axis rule, one implementation with the
  land field's (`ThinFeatureGuard.h`, D5 step 3), and its point evaluator `WaterlineField`
  still shares the lattice fill's core. No quickstart ring changed: biome water follows 16 m
  sectors (WOR-467), so none of it runs 1 tile wide, and the ring vertices of every chunk
  loaded at the landing (0, 20) and the coast (70030, 0) hash the same before and after.
- Ocean vs lake distinction for L2/L4: the biome tells us, but wetland water needs its own
  band set (no beach, reeds everywhere). Decide when the renderer task starts.
- Whether the fine SDF band (±8 m) is enough for the widest shallows on very gentle ocean
  shores (L2 says up to 6 m); if not, widen the band for ocean chunks only.
- Whether the meander constants in `RiverNetwork2D` (feature length 9× width, amplitude
  0.3× feature) should move toward the field ratios in R1. Worldgen change, out of scope here.

---

## 9. References

Full annotated list in the research doc. Geomorphology sources behind section 2.9:

- Leopold & Wolman 1957, USGS PP 282-B, https://pubs.usgs.gov/publication/pp282B
- Williams 1986, USGS, https://pubs.usgs.gov/publication/70015687
- Wikipedia: Alluvial river, Cut bank, Point bar, Riffle-pool sequence, Riffle, Beach, Sea foam
- Southard, LibreTexts 5.9, meandering streams, https://geo.libretexts.org/Bookshelves/Geography_(Physical)/The_Environment_of_the_Earth's_Surface_(Southard)/05:_Rivers/5.09:_Morphology_and_Dynamics_of_Meandering_Streams
- River Habitat Survey manual, marginal bank features, https://www.riverhabitatsurvey.org/RHSfiles/RHSmanual/MarginalBankFeatures.html
- Montgomery & Buffington 1995, pool spacing, https://fs.usda.gov/rm/pubs_journals/1995/rmrs_1995_montgomery_d001.pdf
- Best 1987 confluence zones (via https://www.researchgate.net/figure/River-confluence-zones-adopted-from-Best-1987_fig1_363844991); Benda et al. 2004, https://agupubs.onlinelibrary.wiley.com/doi/full/10.1029/2003WR002583
- USDA FS Stream Simulation ch. 5, headwater channels, https://www.fs.usda.gov/biology/nsaec/fishxing/publications/PDFs/AOP_PDFs/Chapter5.pdf
- Missouri Dept. of Conservation, stream edges, https://mdc.mo.gov/discover-nature/habitats/stream-edges
- LibreTexts 12.2 shoreline features, https://geo.libretexts.org/Bookshelves/Geology/Book:_An_Introduction_to_Geology_(Johnson_Affolter_Inkenbrandt_and_Mosher)/12:__Coastlines/12.02:_Shoreline_Features
- NOAA sea foam, https://oceanservice.noaa.gov/facts/seafoam.html
- Minnesota DNR, where aquatic plants grow, https://www.dnr.state.mn.us/shorelandmgmt/apg/wheregrow.html
- Paris et al. 2023, Authoring and Simulating Meandering Rivers, https://github.com/aparis69/Meandering-rivers
- Génevaux et al. 2013, Terrain Generation Using Procedural Models Based on Hydrology, https://dl.acm.org/doi/10.1145/2461912.2461996
