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
| Mud banks | `TilePostProcessor::generateMud`, cardinal waves out to 3 tiles, chunk-local | Distance-to-ring band, sees neighbor chunks through the apron |
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

struct TerrainRing {
    geometry::Ring  ring;            // integer mm, world-absolute, simple, CCW outer / CW hole
    TerrainRingKind kind;
    bool            blocksMovement;  // false only for fordable channels (D7)
    bool            holeCapable;     // Waterline: true (even-odd); Channel/Pond: false (solid)
    float           meanHalfWidthM;  // Channel only, for shading and fordability
};

struct ThalwegPath { std::vector<geometry::Vec2i64> points; std::vector<float> halfWidthM; std::vector<float> widthRatio; };

struct ChunkTerrainPolygons {
    std::vector<TerrainRing>        rings;
    std::vector<ShoreProfile>       profiles;      // parallel to every ring's vertices (D15)
    std::vector<ThalwegPath>        thalwegs;      // one per channel, feeds the SDF bake (D10)
    std::vector<geometry::Vec2i64>  shorePoints;   // D11
    uint32_t                        version;       // bumped with the rings, read like renderDataVersion
};
}
```

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
- `chaikin(Ring&, iterations)`, `resampleRing(Ring&, spacingMm)`.
- `displaceAlongNormal(Ring&, fn(worldMm) -> mm, curvatureDamp)`.
- `clipRingToRect(const Ring&, rectMm) -> std::vector<Ring>` (Sutherland–Hodgman against a
  convex rectangle, degenerate border runs collapsed by `simplifyRing`).
- `strokePolyline(centerline, halfWidths, capStyle) -> Ring` (offset both sides, round caps).

Also in `libs/engine/world/chunk/`: `TerrainDistanceField`, the per-chunk signed-distance
bake the renderer samples (D10). No polygon boolean is needed anywhere: rendering merges
shapes with `min(distance)`, and nav takes overlapping rings as solid containment.

New in `libs/engine/world/chunk/`:

- `TerrainPolygonBuilder` (`build(const Chunk&, const ChunkSampleResult&, const ApronField&)
  -> ChunkTerrainPolygons`), the only place the recipe below is spelled out.
- `TerrainPolygonQuery`: `distanceToWaterMm(point)`, `nearestShorePoint(point)`,
  `isInsideWater(point)`, over one chunk's ring set plus the neighbor rings that reach into
  the apron. Used by mud (D11), vision, and any later gameplay query.

Noise: `foundation::fractalNoise3` (HashNoise.h) already exists and is deterministic; the
builder uses it with z = 0 and a fixed seed per purpose. The three private noise copies
(RiverNetwork2D meanders, the grove field, `Chunk::fractalNoise`) are not touched by this
epic, but new code must not add a fourth.

### D4: Chunk region, apron, and seams

Dual cells sit between tile centers. Each chunk owns the dual cells whose min-corner sample is
one of its tiles, so a chunk's polygon region is its 512 m square shifted by +0.5 m in x and
y. Adjacent regions partition the plane exactly.

The builder works on an extended region of the chunk plus a 4-tile apron on every side
(2 for the blur and Chaikin reach, 2 more for the low-frequency displacement in D6):

- Apron samples come from the world sampler, not from neighbor chunks (neighbors may not
  exist yet, and generation runs on a worker). `GeneratedWorldSampler::sampleChunk` gains an
  apron: it already gathers rivers and ponds by AABB, and biome/elevation for any world
  position is available from `PlanetSampler::sampleAt`. The apron ring of tile surfaces is
  computed by the same `computeTile` logic and discarded after the build.
- Everything the recipe does is a function of world position and world-seeded noise:
  the same samples, the same blur, the same crossings, the same Chaikin (local, 2 iterations,
  touches ±2 vertices), the same displacement. Two chunks therefore produce identical
  geometry across their shared border before clipping. After `quantize` to integer mm, the
  clipped rings share exact vertices along the border.
- A ring is clipped to the chunk's region at the very end (D6 step 7). Ribbons and ponds are
  clipped the same way.

This is the 2D form of Gildea's seam rule (research doc) and removes the need for any
cross-chunk stitching pass. `refreshAdjacencyAround` stays as it is for tile adjacency; it
does not touch rings.

### D5: The waterline field

For biome water tiles only (river and pond tiles are excluded here because D7/D8 stroke
them at sub-tile precision; a tile marked Water by a river counts as land in this field):

1. Indicator `I(x,y) = 1` if biome water, else 0, over the extended region.
2. Soften with a 3x3 binomial kernel (1 2 1 / 2 4 2 / 1 2 1, divided by 16).
3. Thin-feature guard: a water sample with fewer than two same-type cardinal neighbors is
   floored at `kThinWaterFloor = 0.70`; a land sample in the same situation is capped at
   `kThinLandCeil = 0.30`. This keeps 1-tile pools, 1-wide inlets, and 1-tile islets from
   dropping under the isoline while leaving every larger shape free to round. A lone water
   tile becomes a pool ~0.7 m across, not a dot and not a perfect circle once D6
   displacement runs at its own scale.
4. Marching squares at iso 0.5 with linear interpolation. Straight runs cross at the midpoint
   between samples (so a straight shore stays where the tiles put it); convex corners cut into
   the corner tile and concave corners fill out, which is the rounding.

Saddle cells resolve by the average of the four samples, which after the blur is almost
never exactly 0.5.

### D6: Smoothing, displacement, simplification

Per loop, in this order:

1. Chaikin corner cutting, `kChaikinIterations = 2`. Converges toward a quadratic B-spline,
   local, never overshoots.
2. Resample to `kRingSpacingMm = 250`.
3. Displace each vertex along its normal by the sum of two world-space terms: a fine fBm,
   `kBankNoiseAmpMm = 250`, 4 octaves, base wavelength 6 m (octaves at 6, 3, 1.5, 0.75 m;
   the last is what gives a 1-tile pool its lopsidedness), and a low-frequency term,
   `kShoreLowAmpMm = 1200`, 2 octaves, base wavelength 26 m, which is what makes bays,
   headlands, and the general unevenness of a shore. Both damped by local turning angle
   (`damp = max(0.25, 1 - turn / 1.2 rad)`) so tight concavities cannot self-intersect. The
   low term is why the apron is 4 tiles, not 2 (D4).
4. `isSimple` check; on failure halve the amplitude for that loop and redo step 3 (bounded to
   two retries, then skip displacement for the loop and log at debug).
5. Quantize to integer mm.
6. `simplifyRing` at `kRingSimplifyEpsMm = 100`. Not 500: nav and render use the same ring
   verbatim, and 500 would erase the displacement. The vertex budget this implies is in
   section 6.
7. Clip to the chunk region (D4).
8. Drop loops with area under `kMinLoopAreaMm2 = 250 000` (a quarter tile), same as nav today.

### D7: River ribbons

Per river channel that intersects the extended region:

1. Centerline: Catmull–Rom through the segment endpoints, sampled every 0.5 m. Segments are
   already on a global 20 m arc-length grid so adjacent chunks sample the same points.
2. Half-width: linear along each segment (as `Segment` stores it), no floor. The riffle/pool
   width modulation `RiverNetwork2D` already applies stays; it is the source of the width
   variation the realism rules call for.
3. Bank asymmetry from curvature κ (turning angle per meter, signed): the outer bank moves
   out by `min(0.25, 2|κ|·hw)·hw`, the inner bank moves in by the same amount. Thalweg
   hugs the cut bank; the wetted channel is narrowest at the apex against the outer bank,
   which matches the field observations (section 2.9, rules R2, R5).
4. Bank noise: both offset curves displaced by fBm, 3 octaves, base 4 m, amplitude
   `kChannelBankNoise = 0.15 · hw`, curvature-damped as in D6.
5. Stroke to a ring: left bank forward, right bank back, round caps of radius hw at the ends.
6. Fordability: `blocksMovement = (2·hw >= kFordableWidthM = 1.2)`. A channel whose width
   crosses the threshold is split at the crossing into two rings so the flag is per ring. A
   fordable ring is still drawn, still counts for vision and mud, and is skipped by nav.
7. Quantize, simplify at 100 mm, clip to region.

**Confluences** (channel meets channel) need no boolean. The two ribbons overlap, the
renderer fills both with the same water, nav treats Channel rings as solid containment
(`holeCapable = false`), so an overlap cannot flip even-odd parity into a walkable hole. The
junction corner bar and scour hole are shading (section 2.9, R7).

**River mouths** (channel meets Waterline or Pond) get a flare and nothing else:

1. Find the first centerline sample inside the receiving ring; call the arc length there
   `s_m`.
2. Over the reach `[s_m - kMouthFlareW · w, s_m]` widen the channel by up to
   `kMouthFlare = 1.6×` (estuary shape) and fade the bank asymmetry and point-bar deposits to
   zero. Extend the ribbon `1 w` past `s_m` so the two shapes overlap generously.
3. Emit the ribbon as its own Channel ring, overlapping the lake ring. Nav: solid containment,
   fine. Render: the distance field is the minimum over all rings (D10), so the union is
   implicit, the shore bands run continuously around the mouth, and the river's own bank
   features fade out over the flare because they are gated by the channel metadata, not by
   the ring.

No polygon boolean is required. A fordable channel entering a lake keeps its fordable ring up
to the flare start.

### D8: Ponds

`Pond {cx, cy, radius, phaseA, phaseB}` is sampled every 0.3 m of rim, displaced by the D6
fBm at half amplitude (the sinusoids already give the large-scale shape), quantized,
simplified, clipped. `kind = Pond`, `holeCapable = false`, `blocksMovement = true`.
`pondDepthAt` still drives the tile depth byte for the prefilter; shading reads distance to
the rim.

### D9: What nav reads

`NavInputBuilder::buildInput` replaces its tile marcher with:

1. For each ready chunk whose region intersects the area rect plus the existing 1-tile margin,
   take `terrainPolygons().rings`.
2. Skip rings with `blocksMovement == false`.
3. Emit each as `NavInputPolygon { ring, blocked = true, provenanceId = kProvenanceWater,
   holeCapable = ring.holeCapable }`.
4. Not-ready or missing chunks contribute nothing (today they read as land; unchanged).

Clipped rings from two chunks share their border edge exactly (D4). The CDT receives the same
constrained edge twice, once from each side; `buildNavMesh` must accept coincident constraint
segments from separate input rings. That is an implementation check for the nav task, not a
design risk: the vertices are bit-identical, so the arrangement sees one edge.

```cpp
// NavInputBuilder::buildInput, water section (replaces the tile marcher)
for (const ChunkCoordinate& cc : chunksIntersecting(areaRectMm.expanded(kTileMm))) {
    const Chunk* chunk = chunks.getChunk(cc);
    if (!chunk || !chunk->isReady()) continue;                 // missing chunk reads as land
    for (const TerrainRing& tr : chunk->terrainPolygons().rings) {
        if (!tr.blocksMovement) continue;                      // fordable creek
        input.polygons.push_back({.ring = tr.ring, .blocked = true,
                                  .provenanceId = kProvenanceWater,
                                  .holeCapable = tr.holeCapable});
    }
}
```

`regionObstaclesChanged` adds the max `terrainPolygons().version` over the area's chunks to
its signature so a terraform that rebuilds rings triggers a mesh rebuild.

### D10: The renderer samples a signed-distance field, not band polygons

The rings are the truth, but the picture is painted per pixel in the chunk fragment shader
from a **terrain distance field** baked from those rings. No tessellated water fills, no
offset-polygon bands. Every visual effect below is a function of distance to the waterline,
so it is smooth at every zoom, continuous around river mouths, and free to vary along the
shore.

#### 10.1 What the bake produces (C++, generation worker)

Two textures per chunk, covering the chunk plus the apron so border samples are valid:

| Texture | Format | Texel | Channels |
|---|---|---|---|
| `terrainSdf` | RG16F | 0.25 m near rings, 2 m far (two-level, see 10.4) | R: signed distance to the nearest ring, meters, negative in water, clamped ±8 m. G: distance to the nearest river thalweg divided by that channel's local half-width (0 on the thalweg, 1 at the bank), 2.0 where no channel is within 2 w |
| `shoreProfile` | RGBA8 | 1 m | R: slope 0..1. G: exposure 0..1. B: substrate weight sand. A: substrate weight mud (grass = 1 − sand − mud; rock is a flag in the tile data) |
| `channelFrame` | RG16F | 1 m | R: arc length `s` along the nearest thalweg, meters (wraps at 256 m). G: local width ratio `w / w_mean` of that channel (riffle > 1.1, pool < 0.9). Only valid where `terrainSdf.g < 2` |

Substrate is stored as weights, not an enum, so bilinear filtering cross-fades sandy into
muddy shore over the texel spacing instead of snapping. Slope comes from the elevation
gradient across the ring; exposure from ring concavity over a 20 m window (bays low,
headlands high) plus fetch for ocean.

```cpp
// TerrainDistanceField::bake, after TerrainPolygonBuilder::build
void bake(const ChunkTerrainPolygons& tp, const Chunk& c, DistanceFieldTextures& out) {
    // exact distance within kSdfNearM of any ring edge (edge list in a 4 m bucket grid),
    // chamfer sweep beyond; sign from ring containment (even-odd for Waterline, solid else)
    for (Texel t : out.sdf.near())  t.r = signedDistanceExact(tp.rings, t.worldPos);
    for (Texel t : out.sdf.far())   t.r = chamfer(out.sdf.near(), t);
    for (Texel t : out.sdf.all())   t.g = nearestThalwegDistanceOverHw(tp.thalwegs, t.worldPos);
    for (Texel t : out.profile.all()) t = nearestVertexProfile(tp, t.worldPos); // slope, exposure, sand, mud
    for (Texel t : out.frame.all())   t = nearestThalwegFrame(tp.thalwegs, t.worldPos);   // s, w/w_mean
}
```

Uploaded by `ChunkRenderer` beside the tile data texture, same LRU, keyed by
`terrainPolygons().version`.

#### 10.2 What we want, and how the shader does it

Every effect is written against `d` (signed distance, meters), `shore` (decoded profile) and
`frame` (channel arc length and width ratio). Widths are meters; nothing is in pixels, so the
look is zoom-independent, and anti-aliasing uses screen-space derivatives of `d`.

```glsl
vec2  sdf   = texture(u_terrainSdf,     sdfUv(worldPos)).rg;
vec4  prof  = texture(u_shoreProfile,   profUv(worldPos));
vec2  frame = texture(u_channelFrame,   profUv(worldPos)).rg;
float d     = sdf.r;
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

**Riffles at crossovers, pools at bends (R4).** Where the width ratio says riffle, draw
cross-channel streaks by the channel arc length; where it says pool, darken. Both animate
along `s` for flow:

```glsl
float riffle = smoothstep(1.1, 1.3, frame.g);
float pool   = 1.0 - smoothstep(0.8, 0.95, frame.g);
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
float foam = exposure * u_isOceanShore * (1.0 - smoothstep(0.0, u_foamW, -d))
           * smoothstep(0.2, 0.6, fbm2(worldPos / u_foamWavelength + u_time * u_foamDrift));
w = mix(w, u_foam, foam);
```

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

`terrainSdf` at 0.25 m over a whole chunk is 2048² × 4 B = 16 MB, too much for 32 cached
chunks. Store it two-level: 0.25 m texels only inside the union of ring bounding boxes
dilated by `kSdfNearM = 8 m` (sparse tiles of 64² texels), 2 m texels elsewhere; the shader
picks the level by a tile map. A typical chunk is a few MB. `shoreProfile` and
`channelFrame` at 1 m are 512² × 4 B = 1 MB each. The perf task measures bake time and
memory against the overhaul table.

#### 10.5 Tunables

All `u_*` above are debug-server tunables from day one so the look can be A/B'd live; the
constants in this section are starting points, not the design. Initial values: wobble
0.12 m at 1.6 m; along-shore ±0.35 at 6 m; dry/wet sand 1.6/0.9 m, mud 1.1 m, band tail
0.65; edge 0.12 m base, slope gain 2.5, opacity 0.15..0.85; shallows k 0.9 m, clamp
0.5..6 m, slopeMin 0.15, midAt 0.45, bed opacity 0.5, bed fade 0.35; thalweg 0.6..1.1,
depth 0.9; riffle freq 2π / 1.5 m, amp 0.3, pool amp 0.35; patches 7 m, amp 0.45; foam
band 0.4 m; LOD band 0.5 m.

### D11: Vision and mud

**Shore points.** After the rings are built, walk every blocking ring (Waterline and
non-fordable Channel, plus Pond) at `kShorePointSpacingMm = 1000` and emit a point offset
`kShoreOffsetMm = 300` toward the land side into `shorePoints`. `VisionSystem` pass 3 iterates
`shorePoints` instead of `getShoreTiles()`; the synthetic `Terrain_Shore` def and its
Drinkable capability are unchanged. `Chunk::computeShoreTiles` and `getShoreTiles` are
deleted.

**Mud.** `generateMud` becomes: for each eligible tile (Grass variants, Dirt, Sand), take
`d = TerrainPolygonQuery::distanceToWaterMm(tileCenter)` over the chunk's rings and the
neighbor rings reaching into its apron; the tile becomes Mud with probability
`kMudProb(d) = 0.95` for `d <= 1 m`, `0.80` for `d <= 2 m`, `0.65` for `d <= 3 m`, using the
same per-tile hash roll as today. Fordable channels count. Because the apron carries neighbor
water, banks no longer stop at chunk borders. Point bars (D12) are exempt from mud.

**Order in `Chunk::generate()`:** computeTile → build rings (D5–D8) → point bars (D12) →
mud (D11) → adjacency → shore points → render data → version bumps.

### D12: Point bars as tile overrides

On the inner side of a bend where `|κ| > kBarCurvature = 0.05 /m` for at least 8 samples
(4 m), build a crescent: the inner bank offset landward by `sin(π·t)·kBarWidth·hw`,
`kBarWidth = 0.5` (peak ~0.25 w, inside the field range of ~0.4 w for the bankfull bar; the
wetted-side part of the bar is shading). Tiles whose center falls inside the crescent get
`Surface::Sand`. The bar is land-on-land, so its edge is handled by the ground shader's field
blend (the separate land-transition task), which is the right resolution for a gentle,
vegetation-fringed deposit. Bars are walkable.

### D13: Land-on-land priority order

Unchanged: Mud < Sand < Dirt < GrassShort < Grass < GrassMeadow < GrassTall < Rock < Snow,
higher paints over lower. Water is no longer in the stack; it is a polygon drawn on top.

### D15: Variance, or why a shore must never read as a stroke

Constant-width bands along the whole ring with a single noise amplitude read as a thick
stroke around the tile mask. A real shore varies along
its length in kind, not just in wobble. The rule for every renderer and placement consumer:
no along-ring parameter is a constant. Every band width, stroke weight, color, and density is
a function of position on the ring and of a per-vertex **shore profile**, which the builder
computes once and stores alongside the ring:

```cpp
struct ShoreProfile {      // one per ring vertex, packed to 4 bytes
    uint8_t slope;         // 0 gentle .. 255 steep, from the elevation gradient across the ring
    uint8_t substrate;     // Sand / Mud / Grass / Rock, from the adjacent land surface + biome
    uint8_t exposure;      // 0 sheltered bay .. 255 exposed headland, from ring concavity
                           //   over a 20 m window (+ fetch across the water body for oceans)
    uint8_t moisture;      // TileData::moisture of the adjacent land tile
};
```

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

Waterline displacement itself is multi-scale (D6): the 26 m term shapes bays and headlands,
the 6 m fBm shapes the bank. A single amplitude at a single scale is exactly the stroke look.

### D14: Determinism

Every step is a pure function of (tile data, sample result, world position, fixed seeds).
No dependence on generation order, thread count, or which neighbors are loaded. Hash-seeded
choices (mud rolls) use the existing per-tile hash. This is required for multiplayer later
and for D4's seam guarantee now.

---

## 2.9 Realism rules for banks and shores

From the geomorphology survey (references in section 9). [S] is supported by the cited
sources, [I] is our inference. Each rule names its consumer.

**Rivers** (w = local width, hw = w/2):

- R1 [S] Meander wavelength 10–12 w, bend radius 2–3 w, belt width ~4–6 w. `RiverNetwork2D`
  already meanders; the constants should be checked against these ratios (worldgen, later).
- R2 [S] Outer bend is a cut bank: crisp, darker edge, deepest water against it, undercut
  just downstream of the apex. Renderer: a thin dark stroke along the outer bank where
  `|κ| > 0.05`, offset 0–0.3 m downstream of the apex; pool shading (darker) hugging that
  bank. Geometry: D7 step 3.
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
  biome/elevation for chunk + 2-tile apron
  riverSegments, pondBlobs for AABB + apron
        │
Chunk::generate()                                   worker thread
  computeTile ×(512+4)²        (apron tiles discarded after the build)
  TerrainPolygonBuilder::build  ← D5 waterline, D7 channels, D8 ponds, clip to region
  point bars (D12)              → Surface::Sand overrides
  generateMud (D11)             ← TerrainPolygonQuery::distanceToWaterMm
  adjacency, shorePoints (D11), render data
  m_terrainPolygons.version++, m_renderDataVersion++
        │
        ├── NavInputBuilder::buildInput      rings → NavInputPolygon (D9)
        ├── ChunkRenderer / water pass       rings → tessellated fills + band strokes (D10)
        ├── VisionSystem pass 3              shorePoints (D11)
        └── PlacementExecutor (later)        distanceToWater, ring side, curvature (R8, R9, L5)
```

```cpp
// TerrainPolygonBuilder::build, on the generation worker
ChunkTerrainPolygons build(const Chunk& c, const ChunkSampleResult& sr, const ApronField& apron) {
    const RectMm region = chunkRegionMm(c.coordinate());                 // chunk square + 0.5 m
    const RectMm extended = region.expanded(kApronTiles * kTileMm);
    ChunkTerrainPolygons out;

    // D5/D6: biome water only; river/pond tiles count as land here
    ScalarField f = indicator(c, apron, [](const TileData& t){ return isBiomeWater(t); });
    f = binomial3x3(f);
    applyThinFeatureGuard(f, kThinWaterFloor, kThinLandCeil);
    for (geometry::Ring loop : geometry::marchingSquares(f, 0.5f, extended.min, kTileMm)) {
        geometry::chaikin(loop, kChaikinIterations);
        geometry::resampleRing(loop, kRingSpacingMm);
        displaceWithRetry(loop, waterlineNoise);                          // fine + low-freq fBm
        geometry::simplifyRing(loop, kRingSimplifyEpsMm);
        for (geometry::Ring piece : geometry::clipRingToRect(loop, region))
            if (areaMm2(piece) >= kMinLoopAreaMm2)
                out.rings.push_back({piece, Waterline, /*blocks*/true, /*holeCapable*/true, 0});
    }

    // D7: channels from source segments
    for (const ChannelPath& ch : joinSegments(sr.riverSegments)) {        // Catmull-Rom, 0.5 m
        ChannelPath flared = flareIntoReceivingBody(ch, out.rings);       // D7 mouths
        for (const ChannelPiece& piece : splitAtFordableWidth(flared, kFordableWidthM))
            emitChannelRing(out, strokeChannel(piece), piece.blocks, region);
        out.thalwegs.push_back(thalwegOf(flared));                        // for the SDF bake
    }
    // D8: ponds
    for (const Pond& pond : sr.pondBlobs) emitPondRing(out, sampleRim(pond), region);

    computeShoreProfiles(out, c, apron);                                  // D15
    out.shorePoints = sampleShorePoints(out.rings, kShorePointSpacingMm, kShoreOffsetMm);
    return out;
}
```

Terraform (later): a tile edit marks the chunk dirty; the builder reruns for that chunk and
for any neighbor whose apron contains the edited tile (at most four). Ring version bump
invalidates the render cache and, through the nav signature, the mesh.

---

## 4. Parameters

| Name | Value | Where |
|---|---|---|
| `kApronTiles` | 4 | D4 |
| `kThinWaterFloor` / `kThinLandCeil` | 0.70 / 0.30 | D5 |
| `kChaikinIterations` | 2 | D6 |
| `kRingSpacingMm` | 250 | D6 |
| `kBankNoiseAmpMm` | 250 (waterline), 0.15·hw (channels), 125 (ponds) | D6, D7, D8 |
| `kShoreLowAmpMm` / wavelength | 1200 / 26 m (waterline), 0.35·hw / 14 m (channels) | D6, D7 |
| noise octaves / base wavelength / gain | 4 / 6 m / 0.5 (waterline); 3 / 4 m / 0.5 (channels) | D6, D7 |
| `kRingSimplifyEpsMm` | 100 | D6 |
| `kMinLoopAreaMm2` | 250 000 | D6 |
| `kFordableWidthM` | 1.2 | D7 |
| `kMouthFlare` / `kMouthFlareW` | 1.6× / 2 w | D7 |
| `kSdfTexelM` / `kSdfNearM` / far texel | 0.25 m / 8 m / 2 m | D10 |
| shader `u_*` | see 10.5 | D10 |
| `kBarCurvature` / `kBarWidth` | 0.05 /m / 0.5 | D12 |
| `kShorePointSpacingMm` / `kShoreOffsetMm` | 1000 / 300 | D11 |
| `kMudProb(d)` | 0.95 / 0.80 / 0.65 at ≤1 / ≤2 / ≤3 m | D11 |

All of these are candidates for the debug server's tunables so the look can be A/B'd live.

---

## 5. Seams and determinism checklist

- Same samples on both sides of a border: apron from the sampler, not from neighbors.
- Same noise: world-space fBm, fixed seeds, `foundation::fractalNoise3`.
- Same smoothing: Chaikin is local; the 2-tile apron covers its reach and the displacement's.
- Same integers: quantize before clipping; clip against an integer rectangle.
- Test: build two adjacent chunks independently, assert the multiset of border-edge vertices
  is identical, then build the nav mesh over both and assert no face touches a border gap.

---

## 6. Performance budget and risks

- Distance-field memory and bake time (10.4): 0.25 m texels over a full chunk is 16 MB per
  chunk; the two-level scheme (fine within 8 m of a ring, 2 m elsewhere) is the budget. Bake
  is an exact-distance pass over bucketed ring edges plus a chamfer sweep, on the worker.
- Vertex count. A lake with 300 m of shoreline at 0.25 m spacing, simplified at 100 mm, lands
  around 600–900 vertices; a 512 m chunk of coastline with a river might reach 5k. The CDT
  already takes wall bands at that scale. Measure in the perf task against the overhaul table
  (zoom 0.25 is the case that matters) and, if needed, simplify at a larger epsilon for the
  *render* tessellation only (bands hide 0.2 m). Nav keeps the 100 mm ring.
- Build time. The 516² blur and march are trivial; Chaikin and displacement are linear in
  ring length; `isSimple` is the only superlinear step and runs per loop. Runs on the
  generation worker, so the frame never sees it.
- Apron sampling. Four extra rows/columns of `computeTile` per chunk (≈1.6% more tiles) plus
  the sampler answering biome/elevation off-chunk. Confirm `PlanetSampler::sampleAt` cost at
  that count.
- Coincident constraint edges in the CDT (D9). Verify; if the arrangement dedups exact
  duplicates, done; if not, dedup in `buildInput` by sorted edge key.
- Divergence between drawn and walked water is zero by construction. The residual gameplay
  risk is the fordability threshold: a 1.1 m stream walked across while the picture shows
  knee-deep water. Tune with the debug tunables; the threshold is one constant.

---

## 7. Phasing

Maps one-to-one onto the epic's tasks:

1. This spec (done when merged).
2. `libs/geometry/contour/` + `TerrainPolygonBuilder` for the waterline (D3–D6), with the
   seam test from section 5. Nav still marches tiles at this point; nothing user-visible yet.
3. Channel and pond rings (D7, D8), `kRenderMinHalf` deleted, fordable split, mouth flare.
4. `TerrainDistanceField` bake per chunk (10.1, 10.4): `terrainSdf`, `shoreProfile`,
   `channelFrame`, two-level storage, seam test extended to the textures (border texels
   identical on both sides).
5. Nav reads rings, tile marcher deleted (D9). First user-visible behavior change: colonists
   stop at the smooth waterline, step over creeks.
6. Shader: `tile.frag` implements 10.2 against the three textures, all `u_*` as tunables;
   delete the tile water branch and the water bleed.
7. Vision shore points and distance-based mud, point bars (D11, D12).
8. Land-on-land shader field blend (separate task).
9. Perf validation across zoom levels.

---

## 8. Open questions

- Whether `TerrainPolygonQuery` should build a small per-chunk spatial index (a 16 m grid of
  ring-edge buckets) up front or lazily; mud calls it 262k times per chunk, vision far less.
  Lean: build it in the builder, it's cheap and deterministic.
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
