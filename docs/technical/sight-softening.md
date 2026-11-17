# Sight softening

**Created:** 2026-11-16
**Companions:** [Player Visibility](./player-visibility.md) (the seen record and the cover this changes), [Vision Architecture](./vision-architecture.md) (where sight shapes come from), [Nav Mesh](./nav-mesh-variable-resolution.md) (the collision map sight reads)

Sight stops being exact. A colonist's eye is a disc 1 m wide instead of a point, so a thing narrower than the eye casts a shadow that closes a short way behind it, and every collidable thing in sight takes a share of the sight left in its direction, so a stand of trees shortens sight while a lone post does nothing a player would notice. What blocks or thins sight is read from the collision map, never from a list of kinds. The cover's two edges fade over about 1.5 m, inside the brighter area, drawn as vector geometry with alpha.

Terms are player-visibility.md's: **player visibility**, the **unexplored area**, and the three areas **unseen**, **seen**, and **visible**. An **occluder** is anything in the collision map that sight reads. A **hard** occluder hides the whole eye; a **soft** one hides part of it.

---

## 1. Rules

The rules are player-visibility.md's 23 to 27 (2026-11-16), which replace its rules 5 and 11:

- Collidable objects affect sight, read from the collision map less movers (23).
- A thing much narrower than the eye barely matters, trees thin sight slightly, and sight collapses behind narrow things (24).
- Both edges of the cover fade over about 1.5 m, inside the brighter area, so nothing unseen is ever shown even faintly (25). Rule 10, no raster anywhere, stands.
- One sight shape serves colonist knowledge and player visibility (26).
- The fade is drawn with alpha mask layers in salal under CSS's names, with no offscreen texture and no blur, after a sandbox prototype (27).

## 2. The sight model

### 2.1 Occluders

The occluders round an observer are the blocked rings the nav mesh holds within sight plus the rebuild step of him, sorted by provenance (`libs/sim/nav/NavInputBuilder.h`):

| Provenance | Ring | Sight |
|---|---|---|
| +segmentId | A wall's band, or a span of it | Occludes, less the spans of its openings that pass sight |
| `kProvenanceJunction` | Where walls meet | Occludes |
| `kProvenancePlaced` | A placed thing's collision outline, padded as nav pads it | Occludes |
| `kProvenanceWater` | Water | Passes |
| `kProvenanceBorder` | A mesh limit or the walkable bounds | Passes |
| `kProvenanceFord` | A fordable channel, cover-only | Passes |

Movers are never rings, so a colonist never hides another. Water, the border, and fords are in the mesh but are not objects: water has no height, and a limit is where the mesh stops, not a thing in the way. Openings keep vision's rules (vision-architecture.md D7): a door and a window pass sight. Nav cuts a wall band at its doors and tags the door span with its opening, so a span with an opening is dropped; a window leaves its band solid in nav, so its span is cut out of the band with the same `construction::openingGaps` and `geometry::wallSpans` call nav makes for doors. A closed solid door, when there is one, flips the same flag.

What lands in the set follows the collision data alone. A tree's trunk is a Rect collider, so it is in; a bush, groundcover, and most rocks have no collision, so they do nothing to sight; a BigRock is in. No code in vision names a kind of thing. Nav pads a Rect collider by `kPlacedObstaclePadMm` (50 mm) on every side, and sight reads that ring as it is, so a pine's 0.18 m trunk is a 0.28 m ring to sight.

Rings that touch or overlap are one occluder: a building's bands and junctions are one, and so are two trees whose padded rings meet. An occluder's **apparent width** from the eye, `w`, is its extent across the line from the eye to the mean of its vertices: the largest less the smallest cross product of that line with the vertices, over the line's length, one integer square root.

### 2.2 The eye, and the share an occluder hides

The eye is a disc of diameter `E` (`kEyeWidthMm`, 1 m) round the observer's position. Ground is out of sight only when it is hidden from the whole disc, its **umbra**. Ground hidden from part of the disc is partly hidden and counts as in sight.

Behind an occluder of apparent width `w` at distance `D`, seen across the eye's width, the umbra on the line through both centres runs from the occluder to

```
L = D * w / (E - w)          for w < E
```

behind it, and never closes for `w >= E`. A 0.1 m post 12.5 m off hides 1.4 m behind it; a 0.6 m rock 11.3 m off hides 17 m; a 0.25 m trunk 10 m off hides 3.3 m. Far behind an occluder narrower than the eye, the share of the eye it hides tends to `w / E`. That share is the one quantity the model carries:

```
eyeFraction = min(1, w / E)
```

An occluder with an eye fraction of 1 is **hard**: it hides the whole eye from what lies behind it, at any distance. One below 1 is **soft**: its umbra closes, and past it the ground is partly hidden.

### 2.3 Hard shadows

The hard occluders' ring edges go to `geometry::computeVisibilityPolygon` as occluder segments, as wall centrelines did: the shadow of each is the wedge from the eye's centre past it, out to sight plus the rebuild step. A wall seen face on, a building, and a BigRock (a 1.5 m by 1.17 m ring) are hard from every side; a mature oak's trunk (a 0.82 m ring) is soft. A long thin occluder is hard seen side on and soft seen end on.

A disc eye would also see a little round a hard occluder's corner: a fin of partly hidden ground beside each shadow edge, `(E / 2) * (R - D) / D` wide at range `R` behind a corner `D` away (1 m at a 30 m rim behind a corner 10 m off). The fins are not built. The ground in a fin lies behind the occluder on the line from the eye's centre, so a shape holding it is not star-shaped round the eye, and the seen record leans on star-shaped shapes (4.3). The shadow edge runs from the eye's centre instead, which is also why the eye cannot see through a wall it stands against.

### 2.4 The sight budget

Each direction has the sight radius to spend. Distance spends it one for one. Passing a soft occluder keeps only the share of the eye it leaves:

```
left after = left before * (1 - eyeFraction)
```

and the direction's range is where nothing is left. One soft occluder at distance `D` with sight `S` gives

```
range = D + (S - D) * (1 - w / E)
```

so a narrow thing far off costs little, and one near the eye costs a share of almost the whole radius.

**Directions.** The budget is kept for `kSightBudgetDirections` (288) directions from a table made at compile time, as `geometry::kSightDirections` is (`core/DirectionTable.h`), each standing for the wedge of 1.25 degrees round it. An occluder whose silhouette spans part of a wedge spends that part: its factor in that wedge is `1 - coverage * eyeFraction`, the coverage measured with cross products against the wedge's two bounding directions, a ratio of integers. A trunk 30 m off spans under half a wedge; a trunk at arm's length spans twenty. Within a wedge, crossings are taken nearest first, by the distance to the occluder's nearest point.

**Smoothing.** The ranges are then smoothed across neighbouring directions with a tent of `kSightBudgetSmoothingBins` (4) wedges either side, plus or minus 5 degrees, weights 1, 2, 3, 4, 5, 4, 3, 2, 1 over 25, in integers. A dent narrower than the window becomes a shallow scallop and never a slit. Smoothing can only spread a dent; ranges at the full radius stay there.

```cpp
for (std::size_t direction = 0; direction < kSightBudgetDirections; ++direction) {
    std::int64_t leftMm = budgetRadiusMm;
    std::int64_t reachedMm = 0;
    for (const BudgetCrossing& crossing : crossingsNearestFirst[direction]) {
        if (crossing.distanceMm >= reachedMm + leftMm) {
            break;
        }
        leftMm -= crossing.distanceMm - reachedMm;
        reachedMm = crossing.distanceMm;
        leftMm = leftMm * (kFractionOne - crossing.coverage * crossing.eyeFraction / kFractionOne) / kFractionOne;
    }
    rangeMm[direction] = reachedMm + leftMm;
}
smoothWithTent(rangeMm, kSightBudgetSmoothingBins);
```

Every quantity is an integer: millimetres, and fractions in `kFractionOne` parts. The result is the same on every toolchain.

### 2.5 The sight shape

```
range(direction) = min(hard range, smoothed budget range)
```

The budget shape is the ring through the 288 smoothed ranges, cut to the sight polygon of its radius (`starClippedToConvex`), so where nothing dents it its vertices are the sight polygon's and an undented stretch adds none of its own. With hard occluders in sight the shape is that ring cut to the hard polygon: both are star-shaped round the eye, so the cut takes the nearer of the two along each ray and is star-shaped round it too. Hard shadows are never smoothed.

Both are built out to sight plus `kSightRebuildStepMeters`, as vision's polygon is (player-visibility.md 3.2), and everything vision decides with the shape is held to the sight radius round where he stands. A shape is rebuilt when he is more than the rebuild step from its build or when the set of occluder rings in range changes (3.1). With no occluder ring in range, hard or soft, sight is the plain disc and no shape is built, the outdoor fast path as before.

### 2.6 One model

The wide eye and the budget are one rule over one quantity. An occluder hides `eyeFraction` of the eye from what lies past it, and the sight left past it is the sight left before it times `1 - eyeFraction`. At an eye fraction of 1 that leaves nothing at the occluder's face, which is a hard shadow; below 1 the umbra closes after `L` and the occluder takes its share of the sight in its directions. As `w` rises toward `E` the budget range in its directions falls to `D`, the hard shadow's range, so the two meet. The build keeps them apart for one reason: smoothing must not reach a hard shadow. What changes as `w` crosses `E` is that the dent's flanks stop being smoothed, so a long occluder turning from end on to side on sharpens its shadow's edges.

Two departures from the disc eye are taken on purpose, both for the star-shaped shape the record needs: the fins of 2.3 are not built, and a soft occluder's closed umbra counts as in sight. Its cost is carried by its budget dent instead. Behind the 0.6 m rock above, the dent leaves the range about 25 m in its direction, so ground in its umbra between 11.3 and 25 m counts as in sight, and none past 25 m does.

### 2.7 Worked examples

One occluder, a 30 m sight, `E` of 1 m. Widths are the ring as nav holds it. "Smoothed" is the range in the direction through the occluder's middle.

| Occluder | Distance | Eye fraction | Umbra closes after | Range, before smoothing | Smoothed |
|---|---|---|---|---|---|
| A 0.1 m post | 12.5 m | 0.1 | 1.4 m | 29.4 m in its wedge | about 29.9 m |
| A 0.25 m trunk | 10 m | 0.25 | 3.3 m | 25 m | about 28.9 m, back to 30 m 6 degrees either side |
| A 0.25 m trunk | 3 m | 0.25 | 1 m | 23.3 m | about 25.5 m, over about 15 degrees |
| A 0.35 m trunk, chopping it | 0.5 m | 0.35 | 0.3 m | 19.7 m over about 30 degrees | 19.7 m, back to 30 m 5 degrees past either edge |
| A 0.6 m rock | 11.3 m | 0.6 | 17 m | 18.8 m | about 25 m |
| A mature oak, 0.82 m | 10 m | 0.82 | 45.6 m | 13.6 m | about 19 m |
| A BigRock, 1.17 m or wider | 10 m | 1 | never | | a hard shadow, the wedge past it |

The post's dent is 0.1 m, under a tenth of the feather (5.1): it does not show. The trunk 10 m off leaves a scallop 1.1 m deep across about 6 m of rim.

**A stand.** Trunks of apparent width `w` at `n` to the square metre are met at `n * w` a metre along a direction, each keeping `1 - w / E` of what is left. In expectation that is

```
d(left) / d(distance) = -1 - lambda * left,   lambda = n * w * w / E
range = ln(1 + lambda * S) / lambda
```

For 0.25 m rings one per 4 m2 (2 m apart), `lambda` is 0.0156 a metre and the range about 24.6 m; one per 25 m2 (5 m apart), 0.0025 and about 28.9 m. The wedge's coverage factor is this expectation taken over the wedge, so a build's ranges scatter round it and the smoothing averages them. The landing's own density is read by the prototype (9).

## 3. Vision

### 3.1 The occluder source

`GeometryIndex` (`libs/sim/vision/`) stops extracting wall centrelines and reads the collision map: a query on `PatchableMesh` for the blocked rings whose boxes meet a box, by `RingId` with the ring and its provenance, reached through `NavigationSystem`. `ringsInBox` and the ring grid behind it are private; the query is the public form of that. The index keeps 2.1's table, cuts window spans, groups touching rings, and hands vision the hard occluders' edges as `OccluderSegment`s and the soft occluders as rings with their apparent widths. Rings are integer millimetres already, so nothing is converted.

The mesh exists wherever an observer can see: every mover meshes the disc of sight plus 16 m round him, and the landing 150 m. The landing's memory pass (player-visibility.md 3.8) runs after nav's landing patch so its occluders are in the mesh.

The mesh epoch moves on every patch, and movers patch it every 4 m of travel, so it cannot stand for "the occluders changed". An observer's cached shape is keyed by the sorted ids of the occluder rings in range instead: a felled tree, a new wall, or a moved one changes the list and rebuilds the shape; ground arriving does not.

### 3.2 What vision builds

`VisionSystem::update` builds 2.5's shape for every observer with an occluder ring in range, on the rebuild rule of 2.5. `sightShape(observer)` carries, beside the centre, radius, polygon, revision, and the walls and openings seen:

- whether a hard occluder was in range, which decides the record's path (4.1);
- the smoothed budget ranges, 288 integers, when no hard occluder was, which soft legs read (4.2).

Memory's rule is unchanged: a thing is seen when it lies within sight of where he stands and in the shape (player-visibility.md 3.2). In a forest that is a point-in-polygon test against a shape of up to a few hundred vertices, for every candidate. A wall or opening is seen when the shape bounds or crosses its band, where before it was the centreline. The landing's memory pass takes no soft occluders: the crew saw the landing from the air (player-visibility.md rule 8).

`VisionOverlay` (V) draws each observer's shape as before, hard occluders in one colour and soft ones shaded by eye fraction, and the eye's disc.

## 4. The seen record

The record keeps its form: an exact polygon union in a second `PatchableMesh`, star-shaped shapes in, the covered test, the frontier, and the skirt (player-visibility.md 3.3). What changes is which path a colonist's sight takes into it.

### 4.1 Three paths

| Sight in range | Path |
|---|---|
| No occluder | An open leg as before: one capsule a straight walk |
| Soft occluders only | A soft leg (4.2) |
| Any hard occluder | The walls path as before: each shape queued with its walk, the skirt, frontier pieces, and the sweep, its shape now 2.5's |

A leg of one kind ends when sight moves to another kind, as an open leg ends when walls come into sight. The sweep's builder (`SeenArea::setSightBuilder`) builds 2.5's shape with the soft occluders as well as the hard ones, so a sweep point sees what vision's own build at that point would.

### 4.2 Soft legs

An open leg is one ring however long the walk, and it rests on the outdoor shape being convex: the hull of two convex polygons holds every polygon between them, so the open leg only grows as he walks and nothing shown as seen is taken back (rule 16). A budget shape is not convex. Its dents are.

A soft leg holds a convex polygon fixed at its first build, the **leg polygon**: the intersection of one half-plane per budget direction, each at that build's range less `kSoftLegToleranceMm`, less half the rebuild step, less the slack, and less a 2 mm chord allowance (at 288 directions a chord between two directions sags under 2 mm from the arc), cut to the sight polygon of sight less the slack. It is found exactly as the dual of the convex hull of the points `direction / range`, in integers, its vertices rounded toward the centre. The leg's ring is the hull of the leg polygon at the leg's start and at where he stands, built in one pass over its sides as a capsule is, and it goes through the same frontier unskirted.

Each later build on the leg checks that its ranges still hold the leg polygon grown by half the rebuild step. The leg ends at the last position a build confirmed when one does not (a dent deeper than the tolerance where the leg polygon reaches), as well as by an open leg's rules: a turn, a turn back, a jump, or a change of path. The next leg starts there with the newest build's ranges. A leg whose polygon would hold less than `kSeenLeastRadiusMm` round its start is not held; its build's shape is queued as a sight polygon instead.

What this keeps:

- **Growth only.** The leg polygon is convex and fixed, so the open ring only grows. No pending ground is ever withdrawn.
- **Seen inside swept sight.** Every position on the leg lies within the slack of its line and within half the rebuild step of a build whose ranges hold the leg polygon grown by that half step, so the ring holds only ground in sight from where he walked.
- **One ring a leg**, of at most the sight polygon's vertices and the half-planes' flats.

What it gives up: ground in a scallop's outer part, past the flat its half-plane cuts, and the tolerance band, enters the record only from another leg. At the exploring edge in a forest the record trails sight by that much. The bright shape is still drawn whole, and the unseen fill covers what of it is not yet seen, as it does ahead of a colonist with walls in sight (player-visibility.md 3.1); a thing a colonist knows in that band can be under the black until the record takes its ground. When a leg ends the next leg's polygon starts from the newest ranges, so the record's edge there steps outward by up to the tolerance; the feather is 1.5 m wide, and the prototype shows whether a step under it reads.

### 4.3 Star-shaped shapes

The skirt (`geometry::skirtedSightShape`), the frontier (`sightShapeFrontier`), the covered test's fan, and `simpleAround` all take a shape star-shaped round its centre. Every shape the record takes still is: a soft leg's ring is a convex hull; 2.5's shape is star-shaped round its build, the budget ring having one vertex a direction at a positive range and the cut to the hard polygon keeping it so. That is why the fins of 2.3 are not built and a closed umbra counts as in sight: either would make the shape hold ground past ground it does not, along a ray from the eye.

A budget ring's scallops are sides off the rim, so on the walls path the skirt grows them by `kSeenSkirtMm` like any shadow edge, and seen ground can reach that far past a scallop. On a soft leg nothing is skirted, as on an open leg.

### 4.4 What a forest costs the record

In a forest the common case moves off the open leg. A soft leg can be much shorter than an open one, since a trunk passing near enough to dent past the tolerance ends it, and each leg is a ring: rings per kilometre walked in forest, leg lengths, the registry's bytes per kilometre, and the record's tick are what the prototype measures (9), against the open-ground rows of player-visibility.md 6.1 and 6.5. Hard occluders in a forest (BigRocks, a base) put a colonist on the walls path, whose cost near buildings is measured in player-visibility.md 6.7; the share of builds that take it on the landing is measured too.

If soft legs register too many rings, the fallbacks, for Kevin to choose between:

1. A larger tolerance: fewer, longer legs, and a record that trails sight farther at the exploring edge.
2. A leg polygon of one radius, the least range of its first build in any direction: a capsule as an open leg's, much longer legs, and a record that trails sight by the deepest dent.
3. The record taking the disc in soft sight, as if soft occluders were not there. Seen ground would then hold ground sight never reached in thick stands, against rule 26.

## 5. The cover

### 5.1 The feather

Each edge of the cover fades over `kSightFeatherMm` (1.5 m), inside the brighter area: the tint is full at the edge of a visible shape and gone 1.5 m inside it, and the black is opaque at the edge of the record and gone 1.5 m inside it. Nothing unseen is ever shown even faintly, and the fully bright core of a 30 m sight is about 28.5 m round him. Rule 10 holds: the fade is vector geometry carrying alpha, and nothing is rendered to a texture or blurred.

A shape's mask is its **coverage**: 1 at least the feather inside its edge, falling linearly to 0 at its edge, and 0 outside. Each area's mask is the union of its shapes' coverage, composited by CSS's `add`, which for alpha is source over: where two shapes overlap the result is never less than either, so a shape's edge inside another's core leaves no line, and where two fringes cross the result is a little lighter than either. Nothing double-darkens.

**A star-shaped shape** (a visible shape, a pending sight polygon, an open or soft leg's ring) is drawn as a partition: its **core**, the shape moved in by the feather, at alpha 1, and the strip between the shape and its core, triangulated between the two rings, alpha 0 on the shape's vertices and 1 on the core's. The core is found by the angular sweep the skirt runs, taking the nearest moved side along each ray instead of the farthest; for a leg it is the leg ring of the leg polygon moved in, which for a convex polygon is the same thing. A shape narrower than twice the feather has an empty core, so its strip never reaches alpha 1: a beam of sight through a 1 m doorway is never fully bright.

**The record** is not star-shaped, and moving it in is a polygon offset of the whole union. Its mask is the record's mesh at alpha 1 with a darkening strip under it instead: an inner stroke of the record's edge, `kSightFeatherMm` wide, alpha 1 on the edge and 0 inside, mitred at convex corners and fanned round reflex ones. The record's edge is its live triangle edges with nothing across them, the same edges `meshEdgesNear` reads. The strip is built when `epoch()` moves, with the mesh upload, and uploaded the same way, in as many meshes as 16-bit indices need. Where the record is narrower than twice the feather the strip's two sides overlap and darken it twice over; the prototype shows whether that reads, and if it does not, the strip's triangles combine by CSS's `mix-blend-mode: lighten` within the image instead.

A pending shape and the record's edge meet without a seam: where the pending shape's core covers the record's strip the result is 1, and the strip a pending shape carries along an edge is the strip the record gains along the same edge when that shape enters it, so a shape entering the record changes nothing drawn but at its corners.

### 5.2 Masks that carry alpha (salal)

salal's masks gain alpha, keeping CSS's names (salal `docs/masks.md`):

- A mask image can carry alpha: `MaskImage::mesh` takes a mesh whose vertex colours carry alpha, and `MaskImage::triangles` takes screen-space triangles with an alpha at each vertex, for shapes built each frame. In CSS's terms each is an SVG `<mask>` whose content is triangles filled with their vertices' alpha, read with `mask-mode: alpha`, the mode a layer reads alpha images in. A rect or polygon image is alpha 1 where it covers.
- `MaskComposite::Add` and `Subtract` keep CSS's meaning on alpha: `add` is source over, `a + b * (1 - a)`, and `subtract` is source out, the layer times one less what is under it.
- Hard coverage stays exactly as it is, so a mask of hard layers draws as before.

```cpp
std::vector<MaskLayer> unseenMask = {{.image = MaskImage::rect(view), .composite = MaskComposite::Subtract}};
for (const FeatheredShape& pending : pendingShapes) {
    unseenMask.push_back({.image = MaskImage::triangles(pending.screenTriangles)});
}
unseenMask.push_back({.image = MaskImage::mesh(seenMesh, camera), .composite = MaskComposite::Subtract});
unseenMask.push_back({.image = MaskImage::mesh(seenEdgeStrip, camera)});
Primitives::drawRect({.bounds = view, .style = {.fill = kUnseenColor}, .mask = unseenMask, .key = coverKey + 1});
```

Layers are listed top first. Read from the bottom: the edge strip, then the record subtracting it (the record faded at its edge), then each pending shape added, then the view subtracting the lot. The record's mesh layers, when it needs several, are added among themselves above the strip in the same way. The tint is the view minus the visible shapes' coverage, as before with feathered layers.

How salal composites alpha on the GPU is the prototype's to choose and is named nowhere in the API: a target's destination alpha as the accumulator, or the stencil for the hard layers with the fringes drawn as alpha geometry, or something else that holds the rules above with no offscreen texture. The prototype (step 0) is a sandbox scene of the cover's cases over a busy background: overlapping feathered shapes, a mesh layer with its edge strip, a pending shape abutting and overlapping it, a narrow shape, and one entering the mesh, with a test reading the mask's alpha at chosen pixels (cores, mid-fringe, overlaps, the abutting edge) against the values above. No golden image.

### 5.3 Consequences taken on purpose

- Narrow seen and visible ground (a doorway's beam, a strip between two shadows) is never fully bright or fully tinted.
- Sight, the record, and the bright shape are still exact vector shapes; the feather is drawn and decides nothing. A click on ground under the fade is ruled by the record as before.
- The draw bound (player-visibility.md 3.4) is unchanged: the feather lies inside the record, so nothing past it shows.

## 6. Input, things, and dev tools

The input rules (player-visibility.md 3.7) are unchanged: they read the record and `isVisible`, which reads vision's shape. In a forest a colonist's bright shape can reach past the record by a soft leg's tolerance at the exploring edge, under the black, where placing and ordering read unseen.

A mover that is not a colonist shows while `isVisible` holds, now against 2.5's shape, so an animal behind a stand of trees can be out of sight 25 m off.

The visibility state view (`/api/state?what=visibility`) adds builds with hard occluders, soft legs held and committed, and soft legs ended by a dent. `/api/dev/reveal` is unchanged.

## 7. Tunables

| Name | Start | What it must cover |
|---|---|---|
| `kEyeWidthMm` | 1000 mm | The eye's diameter. Sets which occluders are hard (a wall, a building, a BigRock) and which soft (a trunk, a mature oak at 0.82 m), every umbra's length, and every soft occluder's cost. Wider makes trees cost less and their shadows close sooner |
| `kSightBudgetDirections` | 288 | Wedges of 1.25 degrees. Fine enough that the smoothing window is whole wedges; more adds vertices to every dented shape |
| `kSightBudgetSmoothingBins` | 4 | Wedges either side the tent reaches, plus or minus 5 degrees. A dent narrower than this is spread into a scallop, never a slit |
| `kSightFeatherMm` | 1500 mm | The fade on both edges. The bright core of a 30 m sight is sight less this; a shape narrower than twice it is never fully bright |
| `kSoftLegToleranceMm` | 500 mm | How far a soft leg's polygon sits inside its first build's ranges, and so how deep a new dent ends it. Larger means fewer rings and a record that trails sight farther at the exploring edge. Set by the prototype |
| `kSightRebuildStepMeters` | 0.5 m | As before. Half of it is the margin a soft leg keeps for positions between builds |
| `kSeenSlackMm`, `kSeenLeastRadiusMm`, `kSeenSkirtMm` | as before | As player-visibility.md 4; soft legs take the slack and the least radius as open legs do |

## 8. Prior art

| Source | What it does | What carried over |
|---|---|---|
| Orangy Tang, "Dynamic 2D Soft Shadows" | A light with a radius casts a hard umbra and a penumbra beside it, the penumbra drawn as geometry at each shadow edge | The eye as a disc, its umbra and partial shadow. The penumbra here counts as lit, and its fins are not built |
| Scott Lembcke, "Super Fast Soft Shadows" | Shadow geometry per light with the penumbra of a light of given radius worked out per pixel | The same geometry. Its per-pixel shading is raster work this game does not do |
| Company of Heroes 2 | Sight blockers are an authored class: a thing blocks sight or does not | Set aside: what thins sight is read from the collision map, and by how much from its width |
| Combat Mission, tabletop wargame rules | Line of sight through woods falls off with the depth of woods crossed | The budget: depth of trees spends sight |
| League of Legends | A coarse grid upscaled and blurred (player-visibility.md 5) | Set aside: raster |
| Godot 2D lights, Unity URP Light 2D | Shadow edges softened by a filter and softness settings on the light, in the renderer | Set aside: raster. The feather is vector geometry with alpha |

## 9. Performance plan

Measured, not assumed, under the machine-load rule in [testing](../testing/README.md): times count only with `idle` checks both sides, and counts do not depend on load.

What could move:

- **Vision's tick.** In forest every observer builds a budget shape each rebuild step: the rings in range, their silhouettes into wedges, a sort a wedge, the smoothing, the cut. Walls arrive as bands, more segments than centrelines. Memory tests candidates against the shape instead of the radius.
- **The record's tick.** Soft legs, their ring count, and the walls path wherever a BigRock is in sight.
- **The cover.** Alpha compositing in place of the stencil's two bits; on the CPU a core and strip for each visible and pending shape a frame, and the record's edge strip at each epoch.
- **Views with reveal on** draw no cover and build no strips; captures should come out even with `main` apart from vision's tick.

To measure:

- **Step 0, the sandbox.** GPU time of the feathered cover against the hard one in `perf_mask` at the same size; whether every case of 5.2 holds.
- **Step 1, the prototype.** "Prototype: sight ray casting across the nav mesh with tree attenuation, measure perf" on Specboard. A bench of 2.5's build at viewpoints in the quickstart landing's forest and in synthetic stands of known density: us a build, rings in range, vertices out. The sim bench with 1, 10, and 50 colonists walking the landing's forest at 1x and 10x: vision's tick, the record's tick, rings registered per kilometre, soft leg lengths, the share of builds with a hard occluder, and how far the record trails sight at the exploring edge, at two or three tolerances. Overlay screenshots of shapes among trees for Kevin to judge `E`, the window, and the tolerance.
- **Step 5, the cover.** A capture near the landing with `--no-reveal` at each zoom with the crew, 10, and 50 colonists; the strip upload when the record changes.

No-go at step 1 goes back to Kevin with the numbers: for the build's cost, fewer directions (144) or a longer rebuild step in soft sight; for the record, 4.4's fallbacks.

## 10. Build steps

Each step is its own PR with its own done line and a `Perf:` line.

0. **Alpha masks in salal.** The sandbox scene and test of 5.2, then the API. Done: a tagged salal whose `mask` scene shows feathered shapes blending without seams over every case of 5.2, the pixel test passing on the GL backend, `docs/masks.md` brought up to date (the `mask-mode` row), and worldsim's pin on that tag.
1. **Prototype the sight shape.** As 9, on a branch that builds 2.5's shape from the collision map and soft legs in the benches only. Done: numbers in this document; Kevin's call on `E`, the window, and the tolerance from the screenshots; a go or a fallback for the record.
2. **Occluders from the collision map.** The mesh query, `GeometryIndex` on rings with window spans cut, grouping, apparent width, the cache keyed on ring ids, walls seen by their bands. Done: vision's tests for rooms, doors, and windows pass with band occluders; a placed obstacle occludes and a mover, water, and a ford do not; felling a tree rebuilds an observer's shape and walking over new ground does not.
3. **The sight shape.** 2.4 and 2.5 in `libs/geometry/visibility`, `SightShape`'s new fields, the overlay. Done: unit tests for the post, a trunk, a stand, a rock just under `E` and one over it, a wall unchanged; rings pinned and identical on MSVC, Clang, and GCC.
4. **The record.** Soft legs and the three paths; the sweep's builder on 2.5's shape. Done: tests for a straight walk through a stand (one leg while no dent passes the tolerance), a dent ending a leg, a soft leg too small to hold, a walk from open ground into forest and to a BigRock; the record oracle (no point on the record outside a true shape) and the sim test that every seen point is on the mesh and the record never lost ground, run in forest.
5. **The cover.** Feathered visible shapes, pending shapes, and the record's edge strip, through step 0's masks. Done: soft edges on both edges in the game, scenario 14's new steps passing, the capture of 9.
6. **Tests and docs.** Scenario 14's steps (11), player-visibility.md's tunables and measured sections brought up to date. Done: scenario 14 run.

## 11. Tests and scenario steps

Unit and sim tests are named in each step's done line. Before they are added, check the existing vision and record suites for the same paths and extend those.

Scenario 14 (`docs/testing/scenarios/14-player-visibility.md`) gains these steps after its step 2, all at the quickstart landing:

1. Zoom 3 on Bob among trees: the bright area's edge is a fade about 1.5 m wide with no hard line, and the tinted area's edge against the black is a fade on the tinted side; past it the black is solid.
2. Press V: Bob's sight has shallow scallops toward nearby trunks, no slits, and trunks show as soft occluders.
3. `/api/dev/spawn` a BigRock 8 m from Bob: with V on, a hard shadow wedge runs from it to the rim, with straight edges.
4. Walk Bob past a trunk at arm's length (a move order beside one): his rim dents on that side as he passes and recovers behind him; the seen area behind him does not shrink.
5. Draw a room with a door and a window, and stand Bob inside: sight passes the doorway and the window and stops at the walls' faces, and both edges of the light through the doorway fade.

## 12. Decisions log

1. 2026-11-16, Kevin's brief: "we currently have hard edges on the sight cones, and visibilty edges, which we need to soften greatly ... to be less ... precise. for instance if there was just a single wall block a 0.1m in diameter, it really shouldn't effect sight. and right now trees have no effect, but they very slightly should and the cones/rays should kinda ... collapse behind things. and visual softening around the edges as well".
2. 2026-11-16, after a design canvas comparing sight models and edge treatments, Kevin: "lets go with C + 2". C is the wide eye for hard occluders combined with a sight budget; 2 is the feather.
3. 2026-11-16, question round, Kevin's answers. The model is C combined: an eye of 1 m, ground out of sight only when hidden from the whole eye, partly hidden ground in sight; the budget smoothed over plus or minus 5 degrees, the range the lesser of the hard range and the smoothed budget, hard shadows never smoothed. The feather is about 1.5 m, vector with alpha, and lies inside the brighter area on both edges. One sight shape serves colonist knowledge and player visibility. On what blocks or thins sight, offered a list: "collidable objects. this needs to use the true collision map (minus movers) not bound to an arbitrary list." The feather is drawn with alpha mask layers in salal extending its CSS mask API, no offscreen texture and no blur, prototyped in salal's sandbox first.
4. 2026-11-16, in writing this, the author proposed, awaiting Kevin: the budget spent by the collision map through the eye fraction (2.4), so the wide eye and the budget are one rule (2.6); the fins not built and a closed umbra counted as in sight, for star-shaped shapes (2.3, 4.3); "collidable objects" as walls, junctions, and placed obstacles, with water, the border, and fords passing sight, doors and windows keeping vision's rules, and the ring read as nav pads it (2.1); walls occluding by their bands; the landing's memory pass unsoftened (3.2); soft legs and their fallbacks (4.2, 4.4); the feather's partition into core and strip, the record's edge strip, and the layer order (5.1, 5.2).

## Related documents

- [Player Visibility](./player-visibility.md): the rules, the seen record, the cover, and the input rules this builds on
- [Vision Architecture](./vision-architecture.md): sight shapes, discovery, doors and windows
- [Nav Mesh: One Mesh, Variable Resolution](./nav-mesh-variable-resolution.md): the collision map and where it exists
- salal `docs/masks.md`: the mask API this extends
- [Testing](../testing/README.md): scenarios and the perf rule
