# Rotatable placed items: technical design

**Epic:** WOR-1131 (Freely rotatable placed assets: showing height at any angle)
**Design spec:** [Rotatable placed items](../../design/game-systems/world/rotatable-placed-items.md)

How a placed item (furniture, a workstation, storage, anything a colonist crafts and the player places) is defined, built into geometry at any turn, sorted against colonists and other items, placed, collided with, and previewed. The player-facing behaviour and the art contract are in the design spec.

## 1. What this builds on

| Area | What there is | What changes |
|---|---|---|
| Asset definitions | `AssetDefinition` (`libs/assets/assets/AssetDefinition.h`), parsed in `AssetRegistry.cpp`; `assetType` is `simple` (one SVG normalised by `worldHeight`) or `procedural` (a generator) | A third type, `parts`, for every player-placed item (section 4) |
| Instance transform | `assets::InstanceTransform`: `world = R(rotation) * (local * scale) + position`; `uprightInstance` builds one at rotation 0, "what the player places is drawn upright" | Placed items are placed at their turn; `uprightInstance` goes (section 3) |
| Placed render records | `assets::PlacedEntity` (`SpatialIndex.h`): position, rotation, scale, tint, `anchorY`, `templateId`, an optional `partTransforms` pointer | One record a depth strip, carrying a strip pointer (section 8) |
| Dynamic entities | Placed stations and storage are ECS entities (`Position`, `Rotation`, `Appearance`) drawn by `DynamicEntityRenderSystem`, which sets `placed.rotation = 0.0F` | The forced zero goes; parts items emit their strips (section 8) |
| Depth sort | The Y-sorted upright stream, one `anchorY` each ([World depth sorting](./world-depth-sorting.md)); `InstancedEntityRenderer::emitSorted` draws instanced runs and a CPU triangle batch (animated colonists) at their sorted positions | Strips go through the CPU triangle batch (section 9) |
| Placement | `PlacementMode` (no turn), `PlacementSystem::spawnEntity` (adds `ecs::Rotation{0.0F}`), `footprintAt` and `GhostRenderer` (both upright), `ecs::Packaged` (`targetPosition` only) | Turn, snapping, and a target turn (section 11) |
| Snapping | `construction::SnapEngine` (`libs/sim/construction/`) and `assets/config/construction/snapping.xml` (`angleIncrementDegrees` 15, `edgeSnapRadiusMeters` 0.3) | The turn and the flush snap read the same config (section 11) |
| Colliders | `CollisionShape` rect or polygon in the def's pivot frame, authored; `nav::placedObstacleOutline` puts it in the world through `InstanceTransform` | A parts def's collider is derived from its footprints (section 10) |
| Selection | Per-template silhouette rings and hit region (`AssetRegistry::getSilhouette`, raster of the template's triangles) ([Entity selection silhouette](./entity-selection-silhouette.md)) | An item view carries its own, built the same way (section 12) |
| SVG to mesh (salal) | `renderer::loadSVG` gives `LoadedSVGShape`s with flattened paths, fill, gradient, stroke colour and width; `appendShapeMesh` tessellates fills and builds stroke bands (`appendStrokeBand`) in the SVG's own space | A transform applied before stroking, and a brightness (section 17) |

World units are metres: 1 tile is 1 m (`kTileSize`), and the game camera draws 8 screen pixels a metre at zoom 1 (`kPixelsPerMeter` in `GameScene.cpp`). Art is authored at 64 pixels a metre, as ground tiles are (`GroundParams::pixelsPerMeter`).

## 2. The projection

Planometric oblique. The ground plan is drawn undistorted and turns freely; height moves a point straight up the screen. For a point at plan position `(x, y)` in metres, `y` down the screen, at height `z` metres:

```
screenX = x
screenY = y - z * heightRatio
```

Screen coordinates here are world metres in the camera's plan; the camera's scale and offset follow as for everything else. Consequences the rest of this document leans on:

- Screen x is plan x, so a thing's drawing spans exactly its footprint's x range. Two things whose footprints don't overlap in x never overlap on screen.
- Height only moves things up the screen, so a thing can cover only what stands behind it (smaller plan y on the same vertical).
- A side face's outward plan normal `n`, after the item's turn, faces the viewer when `n.y > 0`.

`heightRatio` is one value in `assets/config/rendering/view.xml`, read once at load:

```xml
<WorldView>
  <!-- Screen metres a metre of height rises. Shared by every thing with height. -->
  <heightRatio>0.35</heightRatio>
  <!-- The one light: from where on the screen (degrees clockwise from the top of the screen)
       and how high above the ground. Shading reads it; so does WOR-1135. -->
  <light azimuthDegrees="315" elevationDegrees="40"/>
  <!-- Side shading: shade = ambient + diffuse * max(0, n . toLight) * cos(elevation). -->
  <sideShading ambient="0.66" diffuse="0.6"/>
  <!-- Part outlines, in art pixels (64 a metre). -->
  <partOutline color="#33241A" width="2" topColor="#33241A" topWidth="1.5" topOpacity="0.8"/>
  <!-- Depth strips (section 8). -->
  <depthStripWidthMeters>0.25</depthStripWidthMeters>
  <depthStripMergeMeters>0.02</depthStripMergeMeters>
</WorldView>
```

Everything with height uses this projection and this ratio. Walls are the other thing with height; their height, how they draw their faces, and the tuning of `heightRatio` against them belong to WOR-740 (Finish buildings: rotated-shape snapping, wall styling, enclosed state). This spec defines the rule they draw by and does not draw them.

Flora keeps its billboard art (the tilt is in the drawing, as [World depth sorting](./world-depth-sorting.md) says); it does not use the projection.

## 3. Rotation convention

`ecs::Rotation::radians` is the turn of the entity's local frame, the `rotation` that `InstanceTransform` applies: radians, positive from +X toward +Y, which is clockwise on the screen since +Y is down. A placed item's front is its local +Y side, so at turn 0 the front faces down the screen, and an item at turn `t` faces `(-sin t, cos t)`. The comment in `libs/sim/ecs/components/Transform.h` ("0 = facing right") is replaced by this.

- `PlacementSystem::spawnEntity` takes the turn and stores it in `ecs::Rotation`.
- `DynamicEntityRenderSystem` reads `ecs::Rotation` into the record it builds (the `placed.rotation = 0.0F` line goes). Colonists, piles, and packaged crates carry 0 by data; colonists face by `FacingDirection`.
- `assets::uprightInstance` and its "the player places upright" comment go; `footprintAt` and the ghost build `InstanceTransform(position, turn, 1)`.
- Turns are stored as given. A snapped turn is an exact multiple of its step in radians (`step * k`, computed in double, stored as float), so items placed at the same snap share a view (section 6.7).
- XML authors turns in degrees clockwise (`turnDegrees`), converted at load.

## 4. Asset schema

### 4.1 The definition

A parts def is `<assetType>parts</assetType>`. It has no `svgPath`, `worldHeight`, `variantCount`, `placement`, `collision`, or `generator` other than the optional script below; the registry refuses a parts def that has any of them. Capabilities, category, `handsRequired`, and the rest of the def are as for any asset.

```xml
<parts>
  <part name="...">
    <footprint> <rect .../> | <round .../> </footprint>
    <height bottom="m" top="m"/>
    <top color="#RRGGBB" drawing="file.svg"?/>
    <sides color="#RRGGBB" drawing="file.svg"?>
      <face side="front|back|left|right ..." drawing="file.svg"/>*
    </sides>
  </part>*
</parts>
<placements>
  <place part="name" x="m"? y="m"? turnDegrees="deg"?/>*
</placements>
<standPoint x="m" y="m"/>?
```

- `<rect minX minY maxX maxY/>`, metres in the part's own frame, the same attributes as the `<collision><rect>` override. Its edges are, in order, back (`minY`), right (`maxX`), front (`maxY`), and left (`minX`): named as a viewer standing in front of the item sees them.
- `<round x y radius sides/>`: a regular polygon of `sides` (3 to 32) about `(x, y)`, vertex `i` at angle `(i + 0.5) * 2 pi / sides` from +X, so an even round has flat edges top and bottom. Its faces have no names; they all take the `<sides>` drawing.
- `<height bottom top/>` in metres above the ground. `bottom` 0 stands on the ground; a worktop on a carcass has `bottom` equal to the carcass's `top`. `top` equal to `bottom` is a flat part: a top only, no sides, no collider (a crafting spot, a rug).
- `<top>`: `drawing` if it has one, else a plain fill of `color`.
- `<sides>`: `color` is every side's plain colour. `drawing` on `<sides>` goes on every face not given one by a `<face>`; it is how a round's facet strip is given. `<face side="left right" drawing=.../>` puts one drawing on several faces, which must be the same size.
- `<place>`: the part at `(x, y)` metres in the item's frame, turned `turnDegrees` clockwise about its own origin. Defaults 0, 0, 0. A part placed nowhere is an error.
- `<standPoint>`: where a colonist stands to work at or use the item, in the item's frame. Default: the middle of the front edge of the item's footprint bounding box (largest `y` over every part's placed footprint), moved out by `standPointOffsetMeters` (0.35 m, half the construction config's 0.7 m pathing clearance, read from it).
- Colours are `#RRGGBB`; `outlineColor` on `<AssetDef>` replaces the config's part outline colour for this item.

The item's frame is its pivot: `(0, 0)` is the point the entity's position stands for, and an author puts it at the middle of the item's footprint, as every def has one pivot ([Collision shapes](../vector-graphics/collision-shapes.md#one-pivot-per-def)).

**An optional script.** `<generator><scriptPath>parts.lua</scriptPath></generator>` names a Lua script that runs once at load after the XML parts and placements are read. It may add parts and placements (`asset:addPart{...}`, `asset:place{...}`, with the XML's fields as table keys) and change a part's size or heights, for an item whose layout is easier computed than written out (a shelf whose shelf count is a parameter). It never draws; drawings are files. The XML alone is a complete item.

### 4.2 Folder layout

One SVG per drawing, in the item's folder. A drawing may be named by several faces or parts of the same item; a path outside the item's folder (including `@shared/`) is an error, since parts and their art belong to their item.

```
assets/world/stations/Workbench/
├── Workbench.xml
├── carcass-front.svg      181.76 x 46.08
├── carcass-back.svg       181.76 x 46.08
├── carcass-end.svg         53.76 x 46.08   (left and right)
├── worktop-top.svg        192    x 64
├── worktop-end.svg         64    x  5.12   (left and right)
├── vise-top.svg            20.48 x 19.2
├── vise-front.svg          20.48 x 11.52   (front and back)
├── vise-end.svg            19.2  x 11.52   (left and right)
├── toolbox-top.svg         29.44 x 17.92
├── toolbox-front.svg       29.44 x 12.8
├── toolbox-back.svg        29.44 x 12.8
└── toolbox-end.svg         17.92 x 12.8    (left and right)
```

### 4.3 Worked example: the workbench

```xml
<?xml version="1.0" encoding="UTF-8"?>
<AssetDefinitions>
  <AssetDef>
    <defName>Workbench</defName>
    <label>Workbench</label>
    <assetType>parts</assetType>
    <category>Furniture</category>
    <handsRequired>2</handsRequired>
    <capabilities>
      <craftable/>
    </capabilities>

    <parts>
      <part name="carcass">
        <footprint><rect minX="-1.42" minY="-0.42" maxX="1.42" maxY="0.42"/></footprint>
        <height bottom="0" top="0.72"/>
        <top color="#9A6A3F"/>
        <sides color="#9A6A3F">
          <face side="front" drawing="carcass-front.svg"/>
          <face side="back" drawing="carcass-back.svg"/>
          <face side="left right" drawing="carcass-end.svg"/>
        </sides>
      </part>
      <part name="worktop">
        <footprint><rect minX="-1.5" minY="-0.5" maxX="1.5" maxY="0.5"/></footprint>
        <height bottom="0.72" top="0.8"/>
        <top drawing="worktop-top.svg"/>
        <sides color="#B38654">
          <face side="left right" drawing="worktop-end.svg"/>
        </sides>
      </part>
      <part name="vise">
        <footprint><rect minX="-0.16" minY="-0.15" maxX="0.16" maxY="0.15"/></footprint>
        <height bottom="0.8" top="0.98"/>
        <top drawing="vise-top.svg"/>
        <sides color="#8D9193">
          <face side="front back" drawing="vise-front.svg"/>
          <face side="left right" drawing="vise-end.svg"/>
        </sides>
      </part>
      <part name="toolbox">
        <footprint><rect minX="-0.23" minY="-0.14" maxX="0.23" maxY="0.14"/></footprint>
        <height bottom="0.8" top="1.0"/>
        <top drawing="toolbox-top.svg"/>
        <sides color="#8B4A37">
          <face side="front" drawing="toolbox-front.svg"/>
          <face side="back" drawing="toolbox-back.svg"/>
          <face side="left right" drawing="toolbox-end.svg"/>
        </sides>
      </part>
    </parts>

    <placements>
      <place part="carcass"/>
      <place part="worktop"/>
      <place part="vise" x="1.18" y="0.39"/>
      <place part="toolbox" x="-1.09" y="-0.28"/>
    </placements>
  </AssetDef>
</AssetDefinitions>
```

Twelve drawings: three tops and nine sides. The worktop's long edges, 8 cm high, are plain. The vise overhangs the worktop's front edge by 4 cm (its front at y = 0.54), as a bench vise does. The stand point defaults to `(0, 0.54 + 0.35) = (0, 0.89)`.

A stool is two parts and five placements, one drawing:

```xml
<parts>
  <part name="leg">
    <footprint><rect minX="-0.035" minY="-0.035" maxX="0.035" maxY="0.035"/></footprint>
    <height bottom="0" top="0.43"/>
    <top color="#9A6A3F"/>
    <sides color="#9A6A3F"/>
  </part>
  <part name="seat">
    <footprint><rect minX="-0.25" minY="-0.25" maxX="0.25" maxY="0.25"/></footprint>
    <height bottom="0.43" top="0.5"/>
    <top drawing="seat-top.svg"/>
    <sides color="#B38654"/>
  </part>
</parts>
<placements>
  <place part="leg" x="-0.19" y="-0.19"/>
  <place part="leg" x="0.19" y="-0.19"/>
  <place part="leg" x="0.19" y="0.19"/>
  <place part="leg" x="-0.19" y="0.19"/>
  <place part="seat"/>
</placements>
```

## 5. Validation

At load, each failing rule is one error in the validation report naming the def, the part, and the face or file. The game stops at a validation error at startup, as for any def; the asset manager and `asset-cli validate` carry on and show them.

1. `assetType` is `parts`, and none of `svgPath`, `worldHeight`, `variantCount`, `placement`, or `collision` is present.
2. Part names are unique; every `<place>` names a part; every part is placed at least once; at least one placement.
3. A rect has `max > min` on both axes; a round has `radius > 0` and 3 to 32 sides.
4. `0 <= bottom <= top`. A flat part (`top == bottom`) has no `<sides>`.
5. Every face name is a face its footprint has (`front`, `back`, `left`, `right` on a rect only), named at most once in a part.
6. Every drawing exists, is inside the item's folder, and passes salal's art check (`renderer::svg::checkArt`: no filters, masks, clip paths, text, images, patterns, markers, `<use>`, style sheets, or dash arrays).
7. Sizes, at 64 pixels a metre, to within 0.5 pixel: a top drawing's viewBox is its footprint's bounding box (for a round, the bounding square of its vertices); a side drawing's viewBox width is its face's length and height is `(top - bottom) * 64`. A drawing on faces of different sizes fails on the first that doesn't match. The viewBox origin is `0 0`.
8. A drawing's geometry stays within its viewBox, to 1 pixel beyond, since nothing clips it (a warning, not an error).
9. Each depth strip's mesh fits a 16-bit index (section 6.6), checked on the turn-0 view and on the widest turn (the turn at which the item's x extent is largest); an item that fails is too detailed for one strip.
10. The derived collider (section 10) is a valid polygon by the existing rules (`AssetRegistry::admitCollisionShapes`).

The no-baked-light rule is an art rule; it is not checked.

## 6. Building an item view

An **item view** is everything drawn for one def at one turn: its depth strips (triangles with per-vertex colours, in metres relative to the pivot, rotation already applied), its silhouette rings and hit region, and its turn. It is built on the CPU once and drawn many times.

### 6.1 Load

At load the registry reads the def, runs the optional script, and loads each drawing once with `renderer::loadSVG` (curve tolerance as for other simple assets) into `LoadedSVGShape`s, cached per drawing in the def. Each part's footprint polygon and each face's endpoints, normal, and length are computed in the part's frame.

### 6.2 Per placed part

For a view at turn `t`, each placement `p` (offset `o`, turn `tp`) of part `q` gives the part's footprint in the item frame, `R(tp) * point + o`, then turned by the item, `R(t) * that`. Call the turned footprint `F`, its vertices in order, each face the edge `F[i] -> F[i+1]` with outward plan normal `n_i` (the edge's perpendicular pointing away from the footprint centroid). The composed turn is `t + tp`.

Work in art pixels of the screen plan: multiply metres by `k = 64`. The finished mesh is scaled by `1 / k` into metres.

### 6.3 Visible sides

Face `i` is drawn when `n_i.y > 1e-6`. A convex prism's visible faces never overlap each other on screen, so they need no order among themselves.

For a drawn face with ground endpoints in art pixels `A` and `B`, where `A` is the one with the smaller screen x (the face's left end as seen from in front of it), `L = |B - A|`, and `Atop = (A.x, A.y - top * heightRatio * k)`:

```
faceMatrix = matrix((B.x - A.x) / L, (B.y - A.y) / L, 0, heightRatio, Atop.x, Atop.y)
```

A drawing point `(u, v)` (u along the face from its left end, v down from the part's top, in art pixels) lands at `(A.x + u * (B.x - A.x) / L, A.y + u * (B.y - A.y) / L - top * heightRatio * k + v * heightRatio)`. At `v = (top - bottom) * k`, the drawing's bottom edge, that is `A` raised to `bottom`. The map is exact for every angle, so one flat drawing covers all of them. A face with no drawing is a rectangle of the part's side colour, `L` by `(top - bottom) * k`, through the same matrix.

Each shape of the face's drawing is appended to the view's mesh with the face matrix applied to its geometry before stroking, and its colours multiplied by the face's shade (section 7):

```cpp
renderer::appendShapeMesh(shape, mesh, {
    .transform = faceMatrix,                              // CSS/SVG transform: matrix(a, b, c, d, e, f)
    .vectorEffect = renderer::VectorEffect::NonScalingStroke, // SVG vector-effect
    .brightness = shade,                                  // CSS filter: brightness()
});
```

### 6.4 Constant line width

salal builds a stroke as a band of half the stroke width either side of the path (`appendStrokeBand`), in whatever space the path's vertices are in. Skewing a finished band would thin a line by up to `heightRatio` across the face's height (a horizontal seam on a face drawn at 0.35 is 0.35 of its width tall). So the face matrix is applied to the path's vertices (the flattened centreline and the fill contours) and to the gradient's transform first, and the band is built afterwards from the transformed centreline at the shape's own stroke width, in art pixels. That is SVG's `vector-effect: non-scaling-stroke`: the stroke is drawn in the space outside the transform. The later scale from art pixels to metres is uniform, so it keeps widths in proportion with the rest of the art, as every asset's strokes are.

Fills don't care about the order: an affine map commutes with triangulation. Joins stay bevelled, as salal builds them.

### 6.5 Top and outline

The top drawing's viewBox origin sits at the footprint's bounding-box minimum corner in the part's frame. Its matrix is that offset, then the placement's turn and offset, then the item's turn, then a lift of `-top * heightRatio * k` in y: a rotation and translation, so widths are kept with or without the vector effect, and it is passed the same way. A plain top is the footprint polygon filled with `<top color>`. Tops are not shaded.

The part's outline is drawn after its top: a closed stroke round the convex hull of the footprint projected at `bottom` and at `top`, `partOutline width` (2 art pixels) in `partOutline color`, and a closed stroke round the top polygon, `topWidth` (1.5) at `topOpacity` (0.8). Both are built in screen space, never skewed.

### 6.6 Draw order and depth strips

Inside the view, placed parts are ordered by `bottom` ascending, then by the turned footprint centroid's `y` ascending; each part appends its visible sides, then its top, then its outline. So a worktop draws over its carcass, a vise over the worktop, and of two parts standing on the same ground the one behind draws first. A raised part meant to be hidden behind a taller part standing on the ground beside it draws over that part; the art contract keeps parts stacked or side by side.

The finished triangles are cut into **depth strips** for the sort (section 8). Strips are vertical bands of the screen, `depthStripWidthMeters` (0.25 m) wide, measured from the view's leftmost footprint x. Every triangle is clipped against the band edges (vertical lines, since screen x is plan x); a clipped piece is convex and is fanned back into triangles with its colours interpolated linearly. The first and last strip take everything left and right of the footprint (the outline's half width). Each strip records its `anchorLocalY`: the largest plan `y` of the item's turned footprints, all parts at any height, within the strip's x range (each convex footprint clipped to the band, its largest y taken). Neighbouring strips whose anchors differ by under `depthStripMergeMeters` (0.02 m) are merged, so an item square to the screen with a flat front is one strip, and a stool is one strip at every turn. Triangle order is kept within a strip.

### 6.7 Caching and threads

Views are cached in the registry by `(defName, turn)`, the turn's float bits the key: items at the same snapped turn share one. A view no live entity, ghost, or crate uses is dropped when its last user goes. Views are built on the registry's template build worker (the one `TemplateLevelBuilder` uses), never on the render thread: a placement requests its view when its install target is set (`Packaged.targetRotation`, section 11), so it is ready before a colonist installs it; at load every placed item's view is requested, the ones in view first. An entity whose view is not ready draws nothing that frame. The ghost builds its own view on the main thread when the turn changes (section 11.3).

`AssetRegistry::getTemplate` for a parts def returns its turn-0 view's strips joined into one mesh, one `MeshPart` a placed part. Every consumer of a template (the asset manager's thumbnails, the build menu, the item fitted inside a packaged crate) gets the item front-down with no change.

## 7. Shading

With the light's azimuth `a` (degrees clockwise from the top of the screen) the plan vector toward the light is `toLight = (sin a, -cos a)`; at 315 degrees it is `(-0.7071, -0.7071)`, the upper left. A visible side face's shade is

```
shade = ambient + diffuse * max(0, n . toLight) * cos(elevation)
```

with `ambient` 0.66, `diffuse` 0.6, and `elevation` 40 degrees from the config. Every colour on the face (fills, gradient stops, strokes) is multiplied in RGB by `shade`, clamped to 1, alpha untouched. Visible faces have `n.y > 0`, so `n . toLight` is at most about 0.707: a face looking left (just turned toward the viewer) is shaded 0.985, and a face looking straight down, down and left at 45 degrees, or anywhere down and right is shaded 0.66. Tops and part outlines are not shaded.

The light is the one `<light>` element. Nothing in an asset depends on it, so a day and night system can write it at run time; a change rebuilds the views in use (shading is baked into their colours). WOR-1135 (Shadows: one soft-edge system for placed-item shadows, wall shadows, and sight edges) reads the same element.

## 8. Sorting against colonists and other items

One `anchorY` an item is wrong for a long item at an angle. A bench 3 m long turned 45 degrees has a footprint whose lowest point is one end. A colonist standing just in front of the other, higher end has feet well north of that lowest point, so he sorts before the bench and its side face paints over him. Two items have the same fault: a stool set in front of the high end of a diagonal bench draws behind it.

Under this projection the true order is per vertical line of the screen (section 2): on any vertical, the thing whose footprint is further south draws later, and things whose x ranges don't meet don't overlap. So each item enters the stream as its depth strips, each strip an ordinary upright with its own `anchorY = position.y + anchorLocalY`. Within a strip the footprint's southern edge varies by at most the strip width times its slope, so a colonist sorts correctly against every strip his drawing overlaps unless his feet are within that much of the item's edge. At 0.25 m that is 0.18 m for a face at 35 degrees to the screen and 0.25 m at 45 degrees, within a colonist's radius (0.3 m, `AgentRadius`), so only a colonist pressed against the item can sort wrongly; at steeper angles the strip covers mostly the item's end, where a colonist stands beside it rather than in front.

Nothing in the stream changes. `DynamicEntityRenderSystem` pushes one `PlacedEntity` a strip for a parts item, each with its strip's `anchorY`, the item's position, `rotation` 0 (the turn is in the view), and a new `const ItemStrip* strip` pointer beside `partTransforms`; `assets::Upright` carries the pointer through. The gather, merge, and stable sort are untouched. Walls stay in the ground sub-layer as [World depth sorting](./world-depth-sorting.md) has them until WOR-740 gives them height.

A packaged item keeps the crate layout: crate back, the item's template (turn 0) fitted inside, crate front, all at the crate's baseline anchor.

## 9. The draw path

Strips are drawn on the CPU triangle path that animated colonists already use in `InstancedEntityRenderer::emitSorted`: when the walk meets a strip it closes the open instanced runs behind it (`flushRuns`), appends the strip's vertices to the batch, offset by the item's position and mapped by the camera's scale and offset, with its colours and indices, and flushes the batch before the next instanced entity (`flushAnim`) as for a colonist. Strips of items next to each other in the stream share one batch and one draw. The 16-bit index flush applies to strips as to colonists. No new pass, shader, or renderer call.

Rejected:

- **A full 2x3 affine per instance on the instanced path.** The visible faces, their shading, and their order all change with the turn, so no single mesh drawn under one affine is right at two turns; it would need a mesh per turn anyway, which is what a view is.
- **Uploading each strip as a GPU mesh and drawing it instanced.** One draw a strip (13 for a 3 m bench at 45 degrees), and items are mostly one of a kind at a free turn, so instancing buys nothing. The batch merges neighbouring strips into one draw at the cost of copying their vertices each frame, a few thousand vertices an item in view, the same per-vertex work the batch already does for colonists.

The selection outline is injected at the selected item's frontmost strip anchor, as it is for any entity at its anchor.

## 10. Collision, nav, and stand points

A parts def's collider is derived at load: the convex hull of the placed footprints of every part that is not flat, in the item's frame, stored as a `Polygon` `CollisionShape` through `setCollisionAllClasses`. It is authored geometry (footprints are written in the XML), so it is exact, and the hull keeps it one simple ring: the gaps between a stool's legs and under a table are not walkable, which is what a colonist would do with them anyway. A def with only flat parts has no collider: a crafting spot does not block.

From there nothing is new. `nav::placedObstacleOutline` puts the polygon in the world through `InstanceTransform(position, turn, 1)`, nav carves it, `PlacedObstacleCollisionSystem` pushes against the same ring, and `footprintAt` in `PlacementSystem` turns the same shape for the buildable and visibility checks, so the ghost's red and the colonists' paths agree at every turn.

The stand point (section 4.1) is turned and moved by the same transform: `InstanceTransform(position, turn, 1).toWorld(standPoint)`. Whatever targets an item (the craft action and its successor in [Queries and chains](../queries-and-chains.md), hauling into storage) goes to that point and snaps it to the nav mesh as every work target is snapped. The rule that the target snaps to a reachable point beside the entity stays; the stand point is where it starts from, so a bench is worked from its front.

## 11. Placement and the ghost

### 11.1 State

`PlacementMode` gains the ghost's turn and passes it to its place callback: `PlaceCallback(defName, worldPos, turn)`. `ecs::Packaged` gains `std::optional<float> targetRotation`, set with `targetPosition`; the colonist installs the item at both, and `spawnEntity(defName, worldPos, turn)` writes `ecs::Rotation`. The last turn used for each def this game is kept by `PlacementSystem` and is the ghost's starting turn (0 the first time). Relocation starts from the entity's own turn.

### 11.2 Input and snapping

Keys and gestures, while `Placing`:

- **Q / E** turn to the previous or next stop. Stops are multiples of `angleIncrementDegrees` (15 degrees, `snapping.xml`) from turn 0, plus the four quarter turns of the alignment direction when one is in range. With Alt held, Q and E turn by 1 degree.
- **Press, drag, release.** Press fixes the position; once the pointer is more than 4 logical pixels from the press (the rectangle tool's threshold), the turn follows the pointer: the front points from the pivot toward the pointer, `t = atan2(-dx, dy)` for a pointer at `(dx, dy)` from the pivot. Release places. A release within 4 pixels is a click.
- **Alt** drops all snapping below.

Snapping, first that applies:

1. **Alignment.** The nearest wall face or foundation edge, blueprint or built, within `itemAlignRangeMeters` (2.0 m, a new key in `snapping.xml`) of the item's pivot gives a direction. A raw turn within half the angle step (the existing `guideAngleWindowDegrees` rule, 7.5 degrees) of that direction or one of its quarter turns snaps to it. The edges are found as the snap engine finds them (`nearestFoundationEdge`, and the wall queries the wall tool's snap uses).
2. **The angle step.** Otherwise the turn rounds to the nearest stop.

Then the position: with the turn snapped to an alignment direction, each of the item's derived collider's edges parallel to that wall face (within `guideDirectionToleranceDegrees`, 1 degree) and within `edgeSnapRadiusMeters` (0.3 m) of it moves the item along the face's normal until the edge lies on the face; the closest such edge wins. Faces of other placed items' colliders count the same way as walls, so items line up flush with each other. No other position snap; the item otherwise sits at the pointer.

Validity is `PlacementSystem::placeable`, with `footprintAt(defName, worldPos, turn)`.

### 11.3 The ghost

`GhostRenderer` draws the ghost from a view at the ghost's turn: from the registry cache when the turn is a stop already built, else built on the main thread for that turn (one item's parts, a few dozen shapes) and kept until the turn changes. During a free drag it is rebuilt at most once a frame. It draws the view's strips joined, with the existing ghost alpha and invalid tint, on top (key 1000). Pending installs (`Packaged.targetPosition` set) draw their ghost at their target turn.

The key guide panel ([Building and construction, Key guide](../../design/game-systems/world/building-construction.md#key-guide)) gets a row for placing: "Q / E: turn. Drag: turn to face the pointer. Alt: hold to place without snapping. Esc / Right-click: leave."

## 12. Selection

An item view carries its silhouette, built as a template's is: its triangles rasterised by `geometry::silhouetteOfTriangles` into `rings` (close 0) and `hitRegion` (the small close), in pivot millimetres, with the view's rotation already in them. Selection for a parts item reads the view of the entity's def and turn and translates by the position (no rotation: the view is turned already). The thick outline strokes `rings`; the hit-test tests `hitRegion`. A stool's legs and seat give one ring once closed. Click priority stays "Furniture and placed objects".

## 13. Asset manager

The detail pane for a parts def shows:

- The item at a turn, with a turn slider and Q/E stepping, and a row of 24 small views every 15 degrees, so a face that reads wrong at some angle is seen at once.
- Over the large view, the derived collider (the existing cyan outline) and the stand point.
- The parts list: each part's footprint, heights, and placements, and its tops and faces with the drawing each uses or "plain" and the colour, so an author sees which faces still need art.
- The counts: unique parts, placements, drawings (tops, sides), plain faces.
- Validation issues per drawing (sizes against faces, the art check).

Thumbnails are the turn-0 template (section 6.7). Reload rebuilds views, since drawings are files.

## 14. What a shadow system reads

A placed item's part footprints, placements, bottom and top heights, and turn are the data ground shadows are built from; they are on the def and the entity, nothing render-only. Ground shadows are designed in WOR-1135 (Shadows: one soft-edge system for placed-item shadows, wall shadows, and sight edges), which reads the same `<light>` element as side shading.

## 15. Config

| Key | File | Default | Meaning |
|---|---|---|---|
| `heightRatio` | `assets/config/rendering/view.xml` | 0.35 | Screen metres a metre of height rises; shared by everything with height |
| `light azimuthDegrees`, `elevationDegrees` | same | 315, 40 | The one light |
| `sideShading ambient`, `diffuse` | same | 0.66, 0.6 | Side shading (section 7) |
| `partOutline color`, `width`, `topColor`, `topWidth`, `topOpacity` | same | #33241A, 2, #33241A, 1.5, 0.8 | Outlines, art pixels |
| `depthStripWidthMeters`, `depthStripMergeMeters` | same | 0.25, 0.02 | Depth strips |
| `itemAlignRangeMeters` | `assets/config/construction/snapping.xml` | 2.0 | How far a wall or edge turns an item square to it |
| `angleIncrementDegrees`, `guideAngleWindowDegrees`, `edgeSnapRadiusMeters`, `guideDirectionToleranceDegrees` | same (existing) | 15, 7.5, 0.3, 1 | Shared with the drawing tools |
| `standPointOffsetMeters` | derived | 0.35 | Half the construction config's pathing clearance |

Art density, 64 pixels a metre, is part of the asset format, not tuning, and is a constant (`kArtPixelsPerMeter`).

## 16. Migration

Every placed, player-built item uses this format; `simple` and `procedural` stay for everything else (flora, rocks, loose items, tools, the colonist, the packaging crate). The items that convert, in the same change that adds the type:

| Def | Becomes | Notes |
|---|---|---|
| `CraftingSpot` (`assets/world/stations/`) | One flat part, a 1.0 m round of 16 sides at height 0, its top the existing drawing redrawn at 64 x 64 | No collider |
| `BasicBox` (`assets/world/storage/`) | One part, about 0.8 m by 0.5 m, 0.5 m high; a top drawing and a front drawing, other sides plain | Sizes set when it is drawn |
| `BasicShelf` (`assets/world/storage/`) | One part, about 1.0 m by 0.4 m and as high as the walls allow (its `worldHeight` is 1.5); the front drawing shows the shelves and goods, the top plain or drawn | Height set against WOR-740's wall height |

Their `svgPath`, `worldHeight`, `rendering`, and `variantCount` elements go, and their old SVGs are deleted. Code deleted in the same change: `assets::uprightInstance`, the forced zero in `DynamicEntityRenderSystem`, the upright-only comments in `PlacementSystem` and `GhostRenderer`, and the `Transform.h` comment. Dev spawn (`/api/dev/spawn`) gains `turn=<degrees>`; `what=stations` and `what=storage` entries gain `turnDegrees`.

## 17. salal and worldsim

**salal** (one change, a minor version):

- `renderer::appendShapeMesh(const LoadedSVGShape&, TessellatedMesh&, const ShapeMeshArgs&)` with `ShapeMeshArgs { Matrix2D transform; VectorEffect vectorEffect; float brightness; }`, names from CSS and SVG (`transform: matrix()`, `vector-effect: non-scaling-stroke`, `filter: brightness()`). It applies `transform` to the flattened path vertices and to the gradient's transform (the gradient's `xform` composed with the inverse) before fill tessellation and stroke expansion; with `NonScalingStroke` the band is built at `strokeWidth` in the output space, and with `VectorEffect::None` the width is scaled by the transform's area scale, as SVG does. `brightness` multiplies the fill colour, gradient stops, and stroke colour in RGB. The existing two-argument call is this with the identity and 1.
- `Matrix2D` (six floats, `a b c d e f` in SVG order) if salal's foundation has no 2D affine type.
- Tests: a horizontal stroke under `matrix(1, 0, 0, 0.35, 0, 0)` keeps its band width with the effect and is 0.35 as tall without; a gradient under a transform evaluates the same colour at a mapped point.

**worldsim**, everything else: the `parts` asset type, parser, validation, and script hook (`libs/assets`); item views, strips, and their silhouettes (`libs/assets` for the geometry, which links the renderer for tessellation as assets already does); the view config; the strip pointer through `PlacedEntity` and `Upright` and the strip emit in `InstancedEntityRenderer` (`libs/world-view`); `DynamicEntityRenderSystem`; the turn in `PlacementMode`, `PlacementSystem`, `GhostRenderer`, `Packaged`, and the install action; the turn and flush snaps; the derived collider and the stand point; selection; the asset manager; the asset migration. The new salal call requires bumping `GIT_TAG` in the root `CMakeLists.txt`.

## 18. Testing

Unit tests (`assets-tests`, `world-view-tests`, `sim-tests`), merged into the suites that cover the same paths:

- The face matrix maps a drawing's four corners onto a face's four projected corners at turns 0, 35, 90, 180, and 271 degrees, and the visible-face set is front only at 0, front and left at 35, back only at 180.
- A stroke on a skewed face keeps its width: the band's perpendicular thickness on screen equals the stroke width within 1%.
- Shading values for the four rect faces at turn 0 and 45 against section 7.
- Depth strips: a 3 m bench at 0 degrees is one strip; at 45 degrees its strips' anchors fall along the front edge; a colonist record with feet 0.35 m in front of the bench's high end sorts after the strip over him and before the bench's low end's strip.
- The derived collider is the hull of the non-flat footprints, turned with the entity, and is the ring nav carves (extend `PlacedObstacle.ColliderCornerLandsOnTheDrawnCorner` to a parts def).
- Turn snapping: a raw 92 degrees beside nothing snaps to 90; beside a wall at 20 degrees, 108 snaps to 110; under Alt 92 stays 92; E from 0 beside that wall stops at 15, then 20, then 30, and Q from 0 at 345. The flush snap puts the bench's back edge on the wall face within 1 mm.
- Validation rejects a side drawing 1 pixel wrong, a drawing outside the folder, an unplaced part, `worldHeight` on a parts def.
- `InstanceTransform.BakedStaticsEmitIt` and the selection round trip stay green.

Happy-path scenario: a new `docs/testing/scenarios/22-placed-item-turns.md` (spawn a workbench at turns 0, 35, 180 with `turn=`; it shows front, front and end, back; click each, one outline; place a crafted box with Q twice beside a wall and see it square and flush; walk a colonist round a 45-degree bench and see him draw in front of its front and behind its back). Scenario 01's crate steps are re-run on the converted `BasicBox`.

Performance: what can move is the upright stream's CPU batch (strip vertices copied each frame) and the batch's flush count. A scoped capture (`node scripts/perf.mjs capture`, after a `machine-load` check reads `idle` or `mostly-idle`) at zooms 1 and 0.25 over a room of 30 items at free turns, against `main` with the same room as upright items, read through `node scripts/perf.mjs compare` and `uprightDrawCalls` from `what=uprights`. View builds are timed on the worker: a workbench view, and load with 500 placed items.

## 19. Prior art

| Source | What it does | Why it is not this |
|---|---|---|
| RimWorld ([modding: textures](https://rimworldwiki.com/wiki/Modding_Tutorials/Textures)) | Four facings as separate textures (`_north`, `_south`, `_east`, west mirrored), 64 px a tile, an outline of about 2 px; the front sliver is drawn per object by eye, with no published tilt | Quarter turns only, and four drawings per object |
| The Sims 1 ([object creation](https://cyndersanity.github.io/sims-legacy-mod-guide/object-creation.html)) | Each object pre-rendered at 4 rotations and 3 zooms, with depth sprites | Fixed angles, raster |
| Unity ([2D or 3D](https://docs.unity.com/en-us/engine/current/manual/get-started/first-time-user/2dor3d)); oblique camera by a custom projection matrix ([Game Developer](https://gamedeveloper.com/programming/adding-some-perspective-to-your-unity-2d-game-)) | The engines' answer to any angle with height: real 3D meshes under an orthographic or oblique camera | Needs modelled meshes and a 3D pipeline beside the vector one |
| Sprite stacking ([GameMaker guide](https://medium.com/@dev_dwarf/intermediate-guide-to-sprite-stacking-using-gamemaker-studio-2-26f1e9101371)) | Slices of an object drawn offset up the screen | Gaps at steep angles, poor fine detail, many layers an object |
| Rudimentary 3D on a 2D canvas ([Petzold](https://charlespetzold.com/blog/2024/09/Rudimentary-3D-on-the-2D-HTML-Canvas.html)) | Faces mapped by affine transforms on a 2D canvas, convex shapes only | The same face mapping, which is why parts are convex prisms |
| Oblique projection ([Wikipedia](https://en.wikipedia.org/wiki/Oblique_projection)) | One plane drawn undistorted, depth drawn along a fixed direction | The projection this uses, with the plan as the undistorted plane |

No shipped game was found that lays 2D side art onto a freely turning prism. The industry norms are 3D meshes or a fixed set of pre-rendered angles. The first needs a modelling pipeline the vector art doesn't have and draws a different style; the second gives a fixed set of turns and multiplies the drawings by the number of angles. A prism with affine-mapped faces keeps hand-drawn vector art, one drawing a face for every angle, and the existing tessellation and draw paths.

## 20. Related documents

- [Rotatable placed items](../../design/game-systems/world/rotatable-placed-items.md): the design spec
- [World depth sorting](./world-depth-sorting.md): the stream the depth strips enter
- [Entity selection silhouette](./entity-selection-silhouette.md): silhouettes and hit regions
- [Collision shapes](../vector-graphics/collision-shapes.md): colliders, the pivot, the nav ring
- [Asset definitions](../asset-system/asset-definitions.md) and [Folder-based assets](../asset-system/folder-based-assets.md): the other asset types
- [Building and construction architecture](../building-construction-architecture.md): the snap engine and its config
- salal `docs/shader-hooks.md` and `docs/masks.md`: the engine's rendering contract
