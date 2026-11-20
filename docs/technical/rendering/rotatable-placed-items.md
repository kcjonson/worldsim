# Rotatable placed items: technical design

**Epic:** WOR-1131 (Freely rotatable placed assets: showing height at any angle)
**Design spec:** [Rotatable placed items](../../design/game-systems/world/rotatable-placed-items.md)

How a placed item (furniture, a workstation, storage, anything a colonist crafts and the player places) is defined, built into geometry at any turn, sorted against colonists and other items, drawn, placed, collided with, used, and previewed. The player-facing behaviour and the art contract are in the design spec.

## 1. What the design rests on

| Area | What it rests on |
|---|---|
| Asset definitions | `AssetDefinition` (`libs/assets/assets/AssetDefinition.h`), parsed in `AssetRegistry.cpp`; `assetType` dispatches in `parseAssetType`. `simple` is one SVG normalised by `worldHeight`, `procedural` a generator (`<generator><scriptPath>`), run only for procedural defs. Load problems go into the registry's validation report (`m_validationReport`) |
| Detail levels | A def's `<detailLevels>` (screen densities in pixels a metre, `assets/DetailLevels.h`): the renderer draws each template at the level the camera's density picks (`detailLevelFor`, `InstancedEntityRenderer::settleLevel`), and everything that isn't drawing reads the reference level (24 px/m). A shape is tagged by SVG `data-lod` or Lua `path:setLod`; the ground SVG reader resolves `data-lod` in one pugixml pass (`GroundSvg.cpp`, [Ground textures, LOD](../ground-textures.md#lod)) |
| Instance transform | `assets::InstanceTransform`: `world = R(rotation) * (local * scale) + position` |
| Placed render records | `assets::PlacedEntity` (`SpatialIndex.h`) and `assets::Upright` (`WorldDepthSort.h`): position, rotation, scale, tint, anchor, `templateId`, an optional `partTransforms` pointer |
| Dynamic entities | Placed stations and storage are ECS entities (`Position`, `Rotation`, `Appearance`) drawn by `DynamicEntityRenderSystem` |
| Depth sort | The Y-sorted upright stream, one `anchorY` each ([World depth sorting](./world-depth-sorting.md)). `InstancedEntityRenderer::emitSorted` walks it once: templates go into instanced draws planned by `RunPlanner`, which lets anything that overlaps nothing on screen share a draw however late it sorts; animated colonists go through the CPU triangle batch, which closes the open instanced runs (`flushRuns`) and is flushed whole (`flushAnim`) before the next instanced entity |
| Template builds | `TemplateLevelBuilder` (`libs/world-view`), owned by `InstancedEntityRenderer`: builds (template, level) meshes on a worker pool, drops a request not wanted again by `endFrame`; the renderer uploads results within `kUploadVertexBudget` |
| Culling | `WorldDepthGather` pads the view by `AssetRegistry::maxReachMeters()`, raised as templates are built, and culls by origin |
| Placement | `PlacementMode`, `PlacementSystem::spawnEntity` (creates the ECS entity), `footprintAt` and `isAreaBuildableNow`, `GhostRenderer` (CPU triangle batch), `ecs::Packaged` (`targetPosition`) |
| Placed obstacles | Nav carves, and `PlacedObstacleCollisionSystem` pushes against, the colliders of entities in the placement executor's chunk index (`placement->getChunkIndex`, `NavigationSystem::serveLimit`), keyed by chunk, position, and def. Every ring in the mesh enters the placed occluder index sight reads. A work target inside a placed obstacle routes to `standPointBeside` (`routeEndOf` in `AIDecisionSystem.cpp`) |
| Snapping | `construction::SnapEngine` (`libs/sim/construction/`) and `assets/config/construction/snapping.xml`, read through `SnappingConfig` |
| Colliders | `CollisionShape` rect or polygon in the def's pivot frame; `nav::placedObstacleOutline` (`NavInputBuilder.h`) puts it in the world through `InstanceTransform` |
| Selection | Per-template silhouette rings and hit region (`AssetRegistry::getSilhouette`, `geometry::silhouetteOfTriangles`) ([Entity selection silhouette](./entity-selection-silhouette.md)); the outline is injected once into the stream at its anchor |
| SVG to mesh (salal) | `renderer::loadSVG` gives `LoadedSVGShape`s with flattened paths, fill, `GradientFill` (with its `xform`), stroke colour and width; `appendShapeMesh(shape, mesh)` (`SVGLoader.h`) tessellates the fill, colours it, and builds stroke bands (`appendStrokeBand`), and tags one `MeshPart` per shape. Indices are 16-bit; geometry past 65535 vertices is dropped with a warning. The art check `renderer::svg::checkArt` is in the build-tool library `salal_svg_build`. Foundation has no 2D affine matrix type |
| Instanced draws (salal) | `Primitives::uploadInstancedMesh` and `drawInstanced(handle, instances, count, camera)`, which draws every index of the mesh for each 32-byte `InstanceData` |

World units are metres: 1 tile is 1 m (`kTileSize`). Art has no pixel size. Every length in this document (part sizes, line widths, offsets, tolerances) is in metres, and the only pixels are screen densities (detail levels, `data-lod`) and screen-space input thresholds, each named where it is used.

## 2. The projection

Planometric oblique. The ground plan is drawn undistorted and turns freely; height moves a point straight up the screen. For a point at plan position `(x, y)` in metres, `y` down the screen, at height `z` metres:

```
screenX = x
screenY = y - z * heightRatio
```

Screen coordinates here are world metres in the camera's plan; the camera's scale and offset follow as for everything else. Consequences the rest of this document leans on:

- Screen x is plan x, so a thing's drawing spans its footprint's x range (and its outline's half width). Two things whose footprints don't overlap in x don't overlap on screen.
- A view ray (the points that land on one screen point) runs `(x, y + s * heightRatio, z + s)`: toward the viewer is south and up. Along one vertical screen line, of two solids whose plan footprints are disjoint, the one further south is nearer the viewer wherever both are crossed, whatever their heights; of two solids one above the other, the higher is nearer.
- A side face's outward plan normal `n`, after the item's turn, faces the viewer when `n.y > 0`.

`heightRatio` is one value in `assets/config/rendering/view.xml`, read once at load:

```xml
<WorldView>
  <!-- Screen metres a metre of height rises. Shared by every thing with height. -->
  <heightRatio>0.35</heightRatio>
  <!-- The one light: from where on the screen (degrees clockwise from the top of the screen)
       and how high above the ground. Shading reads it; so does WOR-1135. -->
  <light azimuthDegrees="315" elevationDegrees="40"/>
  <!-- Side shading (section 7). -->
  <sideShading ambient="0.66" diffuse="0.6" wrap="0"/>
  <!-- Part outlines, metres. -->
  <partOutline color="#33241A" widthMeters="0.03" topColor="#33241A" topWidthMeters="0.025" topOpacity="0.8"/>
  <!-- Item drawings (section 6). -->
  <drawings aspectTolerance="0.01" curveToleranceMeters="0.002" overflowMeters="0.01" faceDetailMinWidthMeters="0.04"/>
  <!-- Depth strips (section 6.7). -->
  <depthStripWidthMeters>0.25</depthStripWidthMeters>
  <depthStripMergeMeters>0.02</depthStripMergeMeters>
</WorldView>
```

Everything with height uses this projection and this ratio. Walls are the other thing with height; their height, how they draw their faces, and the tuning of `heightRatio` against them belong to WOR-740 (Finish buildings: rotated-shape snapping, wall styling, enclosed state). This spec defines the rule they draw by and does not draw them.

Flora keeps its billboard art (the tilt is in the drawing, as [World depth sorting](./world-depth-sorting.md) says); it does not use the projection.

## 3. Rotation convention

`ecs::Rotation::radians` is the turn of the entity's local frame, the `rotation` that `InstanceTransform` applies: radians, positive from +X toward +Y, which is clockwise on the screen since +Y is down. A placed item's front is its local +Y side, so at turn 0 the front faces down the screen, and an item at turn `t` faces `(-sin t, cos t)`. Its right side (as a viewer in front sees it) is local +X: at a small clockwise turn the right side swings toward the viewer and shows. This is the meaning of `ecs::Rotation` for every entity; colonists, piles, and packaged crates carry 0 and colonists face by `FacingDirection`.

- A placed item's turn is a whole number of quanta, `kTurnQuantumDegrees` (0.01 degrees): `k` in `[0, 36000)`, stored as `k * quantum` in radians (computed in double, stored as float). Everything that sets a turn (placement, relocation, install, dev spawn, load) wraps it to `[0, 360)` degrees and rounds it to the quantum, so the turn the view is drawn at and the turn the collider stands at are the same number, and the view cache keys on `k` (section 10). Snap stops (multiples of 15 degrees, a wall's direction rounded to the quantum) are exact quanta. A wall direction rounded to 0.01 degrees is off by at most 0.005 degrees, 0.26 mm over a 3 m edge.
- `PlacementSystem::spawnEntity(defName, worldPos, turn)` stores the turn in `ecs::Rotation`.
- `DynamicEntityRenderSystem` reads `ecs::Rotation` into the record it builds.
- `footprintAt` and the ghost build `InstanceTransform(position, turn, 1)`.
- XML authors turns in degrees clockwise (`turnDegrees`), converted at load.

## 4. Asset schema

### 4.1 The definition

A parts def is `<assetType>parts</assetType>`, a third value for `parseAssetType`. It has no `svgPath`, `worldHeight`, `variantCount`, `placement`, `collision`, or `generator`; the registry refuses a parts def that has any of them. Capabilities, category, `handsRequired`, `<detailLevels>`, and the rest of the def are as for any asset.

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
<useAreas> area shapes </useAreas>?
<clearance marginMeters="m"?> area shapes </clearance>?
<partsScript>file.lua</partsScript>?
```

- `<rect minX minY maxX maxY/>`, metres in the part's own frame, the same attributes as the `<collision><rect>` override. Its edges are, in order, back (`minY`), right (`maxX`), front (`maxY`), and left (`minX`): named as a viewer standing in front of the item sees them.
- `<round x y radius sides/>`: a regular polygon of `sides` (3 to 32) about `(x, y)`, `radius` its circumradius, vertex `i` at angle `(i + 0.5) * 2 pi / sides` from +X toward +Y. It has a flat edge at the front and the back when `sides` is a multiple of 4, and flat ends left and right when `sides` is even and not a multiple of 4. Its faces have no names; they all take the `<sides>` drawing.
- `<height bottom top/>` in metres above the ground. `bottom` 0 stands on the ground; a worktop on a carcass has `bottom` equal to the carcass's `top`. `top` equal to `bottom` is a flat part: a top only, no sides, no collider (a crafting spot, a rug).
- `<top>`: `drawing` if it has one, else a plain fill of `color`.
- `<sides>`: `color` is every side's plain colour. `drawing` on `<sides>` goes on every face not given one by a `<face>`; it is how a round's facet strip is given. `<face side="left right" drawing=.../>` puts one drawing on several faces, which must have the same proportions.
- `<place>`: the part at `(x, y)` metres in the item's frame, turned `turnDegrees` clockwise about its own origin. Defaults 0, 0, 0. A part placed nowhere is an error.
- `<useAreas>` and `<clearance>`: section 4.2.
- Colours are `#RRGGBB`; `outlineColor` on `<AssetDef>` replaces the config's part outline colour for this item.
- `<partsScript>`: section 4.5.

Every polygon in the item's frame is wound the same way, vertex angle increasing from +X toward +Y (clockwise on the screen): a rect's corners back-left, back-right, front-right, front-left, and a round's vertices by `i`. Turns preserve it.

The item's frame is its pivot: `(0, 0)` is the point the entity's position stands for, and an author puts it at the middle of the item's footprint, as every def has one pivot ([Collision shapes](../vector-graphics/collision-shapes.md#one-pivot-per-def)).

### 4.2 Use areas and clearance

A use area and a clearance are the same kind of thing: an **area**, a set of shapes in the item's frame, in metres, unioned. An area's shapes come from two sources, mixed freely in one element:

- `<around distanceMeters="m"/>`: the item's collider (section 11) grown outward by the distance, corners rounded to the drawing curve tolerance.
- `<circle x="m" y="m" radius="m"/>`: a disc, flattened to the drawing curve tolerance.
- `<rect minX minY maxX maxY/>`: a rectangle.
- `<drawing file="area.svg" minX minY maxX maxY/>`: an SVG in the item's folder whose viewBox maps onto that rectangle of the item's frame, at turn 0, left edge to `minX` and top edge to `minY`. Its filled shapes, under their own fill rules, unioned, are the area; strokes are ignored, so an artist can keep an outline of the item's plan in the file as a stroke-only guide. The viewBox's proportions must match the rectangle's within `aspectTolerance`, as for any drawing.

At load every shape becomes polygons in the item's frame, and an area is their union, a set of simple polygons with holes (`geometry` boolean union, the one the construction code uses). After load there is one representation and one code path whatever the source; validation, the turn, the targeting, the placement checks, and the overlays read only the polygons.

`<useAreas>` holds one or more use areas, each `<area name="...">` with shapes, or shapes directly for a single unnamed one. With no `<useAreas>`, the item has one use area, `<around distanceMeters=pathingClearanceMeters/>` (0.7 m, read from `assets/config/construction/constraints.xml`). A flat-only item (a crafting spot) has no collider, so its default use area is its footprint grown by that distance.

`<clearance>` is one area. `marginMeters="m"` is shorthand for `<around distanceMeters="m"/>`, and the two may be combined with shapes. With no `<clearance>`, or margin 0 and no shapes, the item has none. An item's **reserved region** is its collider unioned with its clearance.

### 4.3 Folder layout

One SVG per drawing, in the item's folder. A drawing may be named by several faces or parts of the same item; a path outside the item's folder (including `@shared/`) is an error, since parts and their art belong to their item.

```
assets/world/stations/Workbench/
├── Workbench.xml
├── carcass-front.svg      face 2.84 m x 0.72 m
├── carcass-back.svg       face 2.84 m x 0.72 m
├── carcass-end.svg        face 0.84 m x 0.72 m   (left and right)
├── worktop-top.svg        top  3.0 m  x 1.0 m
├── worktop-end.svg        face 1.0 m  x 0.08 m   (left and right)
├── vise-top.svg           top  0.32 m x 0.3 m
├── vise-front.svg         face 0.32 m x 0.18 m   (front and back)
├── vise-end.svg           face 0.3 m  x 0.18 m   (left and right)
├── toolbox-top.svg        top  0.46 m x 0.28 m
├── toolbox-front.svg      face 0.46 m x 0.2 m
├── toolbox-back.svg       face 0.46 m x 0.2 m
└── toolbox-end.svg        face 0.28 m x 0.2 m    (left and right)
```

Each drawing's viewBox has the proportions of what it covers, at any scale.

### 4.4 Worked example: the workbench

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
    <detailLevels>8, 24, 60</detailLevels>

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

    <useAreas>
      <rect minX="-1.2" minY="0.5" maxX="1.2" maxY="1.2"/>
    </useAreas>
    <clearance>
      <rect minX="-1.5" minY="0.5" maxX="1.5" maxY="1.1"/>
    </clearance>
  </AssetDef>
</AssetDefinitions>
```

Twelve drawings: three tops and nine sides. The worktop's long edges, 8 cm high, are plain. The vise overhangs the worktop's front edge by 4 cm (its front at y = 0.54), as a bench vise does. The crafter works from the strip in front of the bench, and the clearance keeps other items and walls out of the 0.6 m in front of it; the bench's back and ends have none, so it backs flush onto a wall and can stand end to end with another.

Parts order (section 6.6): the worktop overlaps the carcass in plan and stands on it, so it draws later; the vise and the toolbox stand on the worktop and overlap it and the carcass in plan, so they draw after both; the vise and the toolbox share no vertical line, so their order between them is free.

A stool is two parts and five placements, one drawing, and the defaults for use and clearance (used from anywhere within 0.7 m, no clearance):

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

A smelter used only from in front of its mouth, with its use area and its clearance drawn:

```xml
<useAreas>
  <drawing file="smelter-use.svg" minX="-1.0" minY="0.6" maxX="1.0" maxY="1.6"/>
</useAreas>
<clearance marginMeters="0.2">
  <drawing file="smelter-clear.svg" minX="-1.2" minY="-1.2" maxX="1.2" maxY="1.8"/>
</clearance>
```

### 4.5 An optional script

`<partsScript>parts.lua</partsScript>` names a Lua script in the item's folder that runs once at load, after the XML is read and before validation. It may add parts, placements, and area shapes (`asset:addPart{...}`, `asset:place{...}`, `asset:addUseArea{...}`, `asset:addClearance{...}`, with the XML's fields as table keys) and change a part's size or heights, for an item whose layout is easier computed than written out (a shelf whose shelf count is a parameter). It never draws; drawings are files. The XML alone is a complete item. It is its own hook, not `<generator>`: the generator path builds meshes for procedural defs, and a parts script only edits the def.

## 5. Validation

At load, each failing rule is one error in the registry's validation report (`m_validationReport`) naming the def, the part, and the face, area, or file. The game stops at a validation error at startup, as for any def; the asset manager and `asset-cli validate` carry on and show them.

1. `assetType` is `parts`, and none of `svgPath`, `worldHeight`, `variantCount`, `placement`, `collision`, or `generator` is present.
2. Part names are unique; every `<place>` names a part; every part is placed at least once; at least one placement.
3. A rect has `max > min` on both axes; a round has `radius > 0` and 3 to 32 sides.
4. `0 <= bottom <= top`. A flat part (`top == bottom`) has no `<sides>`.
5. Every face name is a face its footprint has (`front`, `back`, `left`, `right` on a rect only), named at most once in a part.
6. Every drawing exists, is inside the item's folder, and passes salal's art check (`renderer::svg::checkArt`: no filters, masks, clip paths, text, images, patterns, markers, `<use>`, style sheets, or dash arrays). `libs/assets` links `salal_svg_build` for it.
7. Proportions, within `aspectTolerance` (1%, relative): a top drawing's viewBox width over height equals its footprint's bounding box's; a side drawing's equals its face's length over `top - bottom`; an area drawing's equals its rectangle's. A drawing on faces of different proportions fails on the first that doesn't match. Every `data-lod` is a number of 0 or more.
8. A drawing's geometry stays within its viewBox, to `overflowMeters` (0.01 m on what it covers) beyond (a warning, not an error); a side's art is clipped to its face (section 6.3), so art past the edge is lost.
9. Parts order: any two placed parts whose footprints overlap in plan (interiors meet) are separated in height (one's `bottom` at or above the other's `top`). The order of section 6.6 has no cycle.
10. The derived collider (section 11) is a valid polygon by the existing rules (`AssetRegistry::admitCollisionShapes`).
11. Areas: every shape is valid (positive radius, `max > min`, a drawing with at least one filled shape), and every use area, at turn 0 on open ground, holds a disc of the agent radius (0.3 m, `AgentRadius`) outside the collider.
12. Size: the item's view at every multiple of 15 degrees, at its finest level, is under 49,152 vertices, three quarters of the 16-bit index space, leaving room for the extra clipped vertices of turns between stops. A view built past 65,535 at run time is an error in the log and the build fails, so `appendShapeMesh`'s silent drop is never reached.

The no-baked-light rule is an art rule; it is not checked.

## 6. Building an item view

An **item view** is everything drawn for one def at one turn and one detail level: one mesh (triangles with per-vertex colours, in metres relative to the pivot, the turn already applied), cut into depth strips, each strip a contiguous index range with its anchor and its screen box; the view's silhouette rings and hit region; and its turn and level. `assets::buildItemView(def, turnQuanta, level)` in `libs/assets` builds one; it is a pure function of the def and the config, safe on any thread.

### 6.1 Load

At load the registry reads the def, runs the optional script, and loads each drawing once. Each drawing's text goes through the attribute pass the ground SVG reader uses (`readGroundSvg`'s pugixml walk, factored out so both readers share it): every element's effective `data-lod` (the largest on it and its ancestors, else 0) is resolved and carried through salal's `loadSVG` on the shape id. The shapes are then mapped from the viewBox onto what the drawing covers, in metres: `sx` = covered width over viewBox width, `sy` = covered height over viewBox height (equal to within the aspect tolerance). Vertices are scaled by `(sx, sy)`, stroke widths by `sqrt(sx * sy)`, and the gradient's `xform` is composed with the inverse scale, so after load every drawing is in metres. Curves are flattened at `curveToleranceMeters` (0.002 m), passed to `loadSVG` in the drawing's own units (divided by the scale). The shapes are cached per drawing in the def. Each part's footprint polygon and each face's endpoints, normal, and length are computed in the part's frame.

### 6.2 Per placed part

For a view at turn `t`, each placement `p` (offset `o`, turn `tp`) of part `q` gives the part's footprint in the item frame, `R(tp) * point + o`, then turned by the item, `R(t) * that`. Call the turned footprint `F`, vertices `P[0..n)` in the winding of section 4.1, each face the edge `P[i] -> P[i+1]` with outward plan normal `n_i = (d.y, -d.x) / |d|`, `d = P[i+1] - P[i]`. The composed turn is `t + tp`.

### 6.3 Visible sides

Face `i` is drawn when `n_i.y > 1e-6`. Seen from outside the face, its left end is its edge's end vertex: `A = P[i+1]`, `B = P[i]`, `L = |B - A|`, `e = (B - A) / L`. For a visible face `A.x < B.x`, but the ends are named from the winding, never from comparing x, which flips near edge-on. With `Atop = (A.x, A.y - top * heightRatio)`:

```
faceMatrix = matrix(e.x, e.y, 0, heightRatio, Atop.x, Atop.y)
```

A drawing point `(u, v)` in metres (u along the face from its left end, v down from the part's top) lands at `(A.x + u * e.x, A.y + u * e.y - top * heightRatio + v * heightRatio)`. At `v = top - bottom`, the drawing's bottom edge, that is `A` raised to `bottom`. The map is exact for every angle, so one flat drawing covers all of them. A face with no drawing is a rectangle of the part's side colour, `L` by `top - bottom`, through the same matrix.

A face whose projected width `|B.x - A.x|` is under `faceDetailMinWidthMeters` (0.04 m) is drawn as its plain rectangle, never its drawing: near edge-on the detail is a smear, and the face matrix's determinant (`e.x * heightRatio`) is near zero.

Each shape of the face's drawing is appended with the face matrix applied before stroking and its colours multiplied by the face's shade (section 7):

```cpp
renderer::appendShapeMesh(shape, faceMesh, {
    .transform = faceMatrix,                                  // CSS/SVG transform: matrix(a, b, c, d, e, f)
    .vectorEffect = renderer::VectorEffect::NonScalingStroke, // SVG vector-effect
    .brightness = shade,                                      // CSS filter: brightness()
});
```

Then everything the face's drawing produced is clipped to the face's parallelogram (its four projected corners): each triangle is clipped against the four edges, a clipped piece is convex and is fanned back into triangles with its colours interpolated linearly. A stroke on a face narrower than the stroke, or art drawn past the viewBox, so never spills onto a neighbouring face or past the outline. With every face's content inside its own parallelogram, and a convex prism's visible faces never overlapping on screen, the visible faces of one part need no order among themselves.

### 6.4 Constant line width

salal builds a stroke as a band of half the stroke width either side of the path (`appendStrokeBand`), in whatever space the path's vertices are in. Skewing a finished band would thin a line by up to `heightRatio` across the face's height (a horizontal seam on a face drawn at 0.35 is 0.35 of its width tall). So with `NonScalingStroke` the face matrix is applied to the flattened centreline first and the band is built afterwards at the shape's stroke width, in metres, in the output space: SVG's `vector-effect: non-scaling-stroke`, the stroke drawn in the space outside the transform.

A fill is tessellated and coloured in the drawing's own space (a gradient evaluated at each source vertex, as `appendShapeMesh` does without a transform) and its vertices are then mapped by the transform: an affine map commutes with triangulation and keeps colours linear across each triangle, so no inverse transform is ever taken. Joins stay bevelled, as salal builds them.

### 6.5 Top and outline

The top drawing is already in metres over the footprint's bounding box (section 6.1). Its matrix is the offset to the bounding box's minimum corner in the part's frame, then the placement's turn and offset, then the item's turn, then a lift of `-top * heightRatio` in y: a rotation and translation, so widths are kept with or without the vector effect, and it is passed the same way. A plain top is the footprint polygon filled with `<top color>`. Tops are not shaded.

The part's outline is drawn after its top: a closed stroke round the convex hull of the footprint projected at `bottom` and at `top`, `widthMeters` (0.03 m) in `partOutline color`, and a closed stroke round the top polygon, `topWidthMeters` (0.025 m) at `topOpacity` (0.8). Both are built in screen space, never skewed.

### 6.6 Order inside an item

Each placed part appends, in order, its visible sides, its top, and its outline. Parts are ordered pairwise, by the projection (section 2):

- Two parts whose footprints are disjoint in plan and share a vertical screen line: the one further south on that line draws later, whatever their heights. For disjoint convex footprints this is the same on every shared line, and the relation has no cycle (convex sets in the plane are never mutually blocking along one direction).
- Two parts whose footprints overlap in plan: the higher (the one whose `bottom` is at or above the other's `top`, as validation requires) draws later.
- Two parts sharing no vertical line: either order.

The order is a topological sort of those pairs, ties broken by placement order, so it is deterministic. A low shelf behind two front posts draws before them, though it starts higher; a long thin part at 45 degrees and a small tall box beside it are ordered by which is south where they share a vertical, not by their centroids.

### 6.7 Depth strips

The finished triangles are cut into **depth strips** for the sort (section 8). Strips are vertical bands of the screen. Their boundaries are every vertex x of the item's collider hull (section 11, the convex hull of the non-flat parts' footprints at this turn), and each span between neighbouring boundaries is split evenly into the fewest bands no wider than `depthStripWidthMeters` (0.25 m). The first and last strip also take everything left and right of the hull (the outline's half width). Every triangle is clipped against the band edges (vertical lines, since screen x is plan x) as in section 6.3. Each strip records:

- `anchorLocalY`: the largest plan y of the collider hull within the strip's x range. The hull's southern edge is linear within each span, so this is its y at one end of the strip.
- its screen box (x range, and the y range of its triangles), relative to the pivot.

Neighbouring strips are then merged left to right: a run grows while each next strip's anchor is within `depthStripMergeMeters` (0.02 m) of the run's first strip's anchor, so the anchors in a merged strip spread by at most 0.02 m however long the run. A merged strip takes the largest anchor of its run. A plain 3 m by 1 m bench square to the screen is one strip; a stool is one strip square and four at 45 degrees (two spans of 0.354 m, each in two strips); a 3 m by 1 m bench at 45 degrees is 12 (spans of 0.707, 1.414, and 0.707 m, in 3, 6, and 3 strips); a 6 m counter at 4.5 degrees merges its 0.25 m strips in pairs, since each differs from the next by 0.0197 m. Triangle order is kept within a strip, and strips are stored left to right, each one index range of the view's mesh.

### 6.8 Detail levels

A parts def's `<detailLevels>` lists the screen densities its views are built for, as for any def; with none it has one level. The view at level `l` keeps a drawing's shape when its effective `data-lod` is at or below the level's density (`levels[l]`), the rule `buildMeshes` applies to a Lua path's `setLod` and a ground SVG's `data-lod`. Plain faces, tops, and outlines are at every level. The renderer picks a view's level by the camera's density (`detailLevelFor`), as `settleLevel` does for templates; the silhouette, the cull reach, and the template read the reference level (24 px/m, `referenceDetailLevel`).

### 6.9 The template

`AssetRegistry::getTemplate` for a parts def returns its view at turn 0 and the reference level, its strips joined into one mesh. It carries the `MeshPart`s `appendShapeMesh` tags (one per SVG shape); a parts def is never animated, so nothing reads them. Every consumer of a template (the asset manager's thumbnails, the build menu, the item fitted inside a packaged crate) gets the item front-down with no change. Its size is checked by validation rule 12, which includes turn 0.

The cull reach of a parts def covers every turn: at load the registry raises `maxReachMeters` with the def's largest `|p| + top * heightRatio` over every placed part's footprint vertex `p` and its `top`, plus the outline's half width. A turned item's top can reach further from its pivot than the turn-0 template does, so the template's reach alone would cull a turned item at the edge of the view.

## 7. Shading

With the light's azimuth `a` (degrees clockwise from the top of the screen) the plan vector toward the light is `toLight = (sin a, -cos a)`; at 315 degrees it is `(-0.7071, -0.7071)`, the upper left. A visible side face's shade is

```
shade = ambient + diffuse * max(0, (n . toLight + wrap) / (1 + wrap)) * cos(elevation)
```

with `ambient` 0.66, `diffuse` 0.6, `wrap` 0, and `elevation` 40 degrees from the config. Every colour on the face (fills, gradient stops, strokes) is multiplied in RGB by `shade`, clamped to 1, alpha untouched. Visible faces have `n.y > 0`, so `n . toLight` is at most about 0.707: a face looking left (just turned toward the viewer) is shaded 0.985, and a face looking straight down, down and left at 45 degrees, or anywhere down and right is shaded 0.66. At turn 325 degrees a workbench's left end is 0.740 and its front 0.66.

At wrap 0 the two visible sides of an item at turns 0, 90, 180, 270, or 35 degrees are both 0.66, and adjacent sides of one colour read as one surface; the part outline separates the part from its neighbours but draws no line between its own faces. `wrap` lets the light reach round onto faces that turn away from it, to tune that without code: at wrap 1, a face looking straight down is 0.66 + 0.6 * 0.146 * 0.766 = 0.727, and one looking down and right at 45 degrees stays 0.66. Tops and part outlines are not shaded.

The light is the one `<light>` element. Nothing in an asset depends on it. Shading is baked into a view's colours, so a change to it rebuilds the views in use (section 10). WOR-1135 (Shadows: one soft-edge system for placed-item shadows, wall shadows, and sight edges) reads the same element.

## 8. Sorting against colonists and other items

One `anchorY` an item is wrong for a long item at an angle. A bench 3 m long turned 45 degrees has a footprint whose lowest point is one end. A colonist standing just in front of the other, higher end has feet well north of that lowest point, so he sorts before the bench and its side face paints over him. Two items have the same fault: a stool set in front of the high end of a diagonal bench draws behind it.

By section 2, on any vertical screen line, of two things whose plan footprints are disjoint and convex, the one further south is nearer, and things whose x ranges don't meet don't overlap. So each item enters the stream as its depth strips, each strip an ordinary upright with its own `anchorY = position.y + anchorLocalY`.

The rule holds only between things whose footprints are disjoint. Something standing under another thing's overhang (a chair under a desk) or inside its concave outline (a stool in the notch of an L-shaped counter) breaks it: on one vertical it is south of the desk's back and north of its front. Nothing with a body stands there. The strips are anchored on the item's collider hull, convex and covering every part with height, and that hull is what keeps other bodies out: `PlacedObstacleCollisionSystem` and the nav mesh keep colonists outside it (section 11), and placement refuses another item whose collider meets it (section 14). An L-shaped item's notch is inside its hull, so nothing stands there, and a use area inside the hull holds no stand point (section 12). A flat-only item has no hull and no body, so a table may stand on a rug; flat-only items enter the stream as one strip with the lowest anchor any upright can have, so they draw before every upright and are never between things that sort.

Within a strip the hull's southern edge varies by at most the strip's width times its slope, plus up to 0.02 m in a merged strip, so a colonist sorts correctly against every strip his drawing overlaps unless his feet are within that much of the item's edge. At 0.25 m that is 0.18 m for a face at 35 degrees to the screen and 0.25 m at 45 degrees, within a colonist's radius (0.3 m, `AgentRadius`), so only a colonist pressed against the item can sort wrongly; at steeper angles the strip covers mostly the item's end, where a colonist stands beside it rather than in front.

`DynamicEntityRenderSystem` pushes one `PlacedEntity` a strip for a parts item, each with its strip's `anchorY`, the item's position, `rotation` 0 (the turn is in the view), and a `const ItemStrip* strip` pointer beside `partTransforms`, naming the view and the strip index; `assets::Upright` carries it through. The gather, merge, and stable sort are untouched. Walls stay in the ground sub-layer as [World depth sorting](./world-depth-sorting.md) has them; how walls with height sort is WOR-740's (Finish buildings: rotated-shape snapping, wall styling, enclosed state).

A packaged item keeps the crate layout: crate back, the item's template (turn 0) fitted inside, crate front, all at the crate's baseline anchor.

## 9. The draw path

### 9.1 Budget

The path must hold for a base of several hundred items in view, interleaved in the stream with flora and colonists, at every zoom. Per frame, item drawing does no per-vertex work on the CPU and uploads no vertices: its CPU cost is one small record a strip in view, and its draw calls are at most one per distinct view in view, plus at most two for each thing (a colonist, a tree) that sorts between strips of an item it overlaps on screen. Vertices are uploaded once, when a view is built.

### 9.2 GPU-resident views drawn by index range

Each built view (def, turn, level) is uploaded once with `Primitives::uploadInstancedMesh` into a mesh slot, as a template level is, within the same per-frame `kUploadVertexBudget`. A strip is drawn through the instanced path with one `InstanceData` (the item's position, rotation 0, scale 1, its tint) over the strip's index range:

```cpp
Renderer::Primitives::drawInstanced(handle, instances, count, camera, {.first = strip.firstIndex, .count = strip.indexCount});
```

salal's `drawInstanced` gains that optional index range (`glDrawElementsInstanced` at an offset); without it every index draws.

In `emitSorted`, an upright with a strip pointer is handled before the `templateMesh == nullptr` check (a parts item is not drawn from a template mesh). Its view's level is settled as a template's is, its screen box comes from the strip's box plus the item's position, and it is placed in `RunPlanner` like any instanced entity, with the strip as its mesh. So items, flora, and every other template share one layer timeline: an item strip that overlaps nothing goes in the first layer however late it sorts, and an item and a tree that overlap on screen draw in depth order. No CPU batch flush is involved; colonists keep theirs.

Two rules keep the draw count at a view rather than a strip:

- When the first strip of a view appears in a frame, the planner gives every strip of that view consecutive mesh orders, in strip order.
- At `close`, neighbouring draws in one layer that are consecutive strips of one view with the same instance list are drawn as one ranged draw covering them.

A lone item that nothing overlaps on screen is one draw. Items of the same def at the same turn and level (a row of beds square to one wall) are one instanced draw together. A colonist standing in front of a diagonal bench splits it into at most three draws: the strips behind him, him, and the strips in front of him.

### 9.3 Rejected

- **The CPU triangle batch.** salal's `BatchRenderer::addTriangles` pushes one batch instance a triangle, transforms every vertex on the CPU each frame, and every `flushAnim` is a full flush; every CPU entry also closes the open instanced runs (`flushRuns`), splitting flora's runs around it. With a few thousand vertices an item, 200 items in view would transform 400,000 to 600,000 vertices on the CPU every frame, upload tens of megabytes, and break about 10 instanced draws into several hundred where items interleave with trees. It holds for one item (the ghost, section 14.4) and for colonists, and not for a base.
- **One full 2x3 affine per instance over one mesh per def.** The visible faces, their shading, and their order all change with the turn, so no single mesh drawn under one affine is right at two turns; it would need a mesh per turn anyway, which is what a view is.
- **One draw per strip, unmerged.** Correct, but a 3 m bench at 45 degrees is 12 draws, and a base of rotated furniture runs to thousands.

The selection outline is drawn at the item's strips (section 15).

## 10. Where views are built and kept

Views are built off the GL thread by `TemplateLevelBuilder`, generalised over its result and its key, so the item renderer owns a second instance whose build function is `assets::buildItemView`. The renderer `want`s each view the frame walk needs (the item's view at its turn and wanted level, and its reference level as the fallback), collects finished views, and uploads them within budget, as it does template levels. A request not wanted again by `endFrame` is dropped, which is right: an item out of view needs no view.

The cache key is `(templateId, k, level)`: the def's template id, the turn in quanta (section 3), and the level. Items at the same turn share one view. A view is evicted when it has not been drawn for `kEvictAfterFrames`, as template levels are.

Nothing pops in:

- **Load.** Before the world is first shown, the loading step builds and uploads every view the first frame's view needs (each item within the view and its cull margin, at its turn and wanted level), on the task pool, and waits for them.
- **A change of turn.** A turn is set by placement, whose ghost builds its view on the main thread (section 14.4); the ghost's view goes into the cache, so a pending install's ghost and the item a colonist then installs draw from it at once.
- **A change of light or shading config, or an asset reload.** Every cached view is rebuilt under a new generation. A slot keeps drawing its old view until its replacement is uploaded, then swaps.
- **A level change on zoom.** The nearest ready level draws until the wanted one is ready (`nearestReadyLevel`), as for templates.

## 11. Collision, nav, and the obstacle source

### 11.1 The collider

A parts def's collider is derived at load: the convex hull of the placed footprints of every part that is not flat, in the item's frame, stored as a `Polygon` `CollisionShape` through `setCollisionAllClasses`. It is authored geometry (footprints are written in the XML), so it is exact, and the hull keeps it one simple ring: the gaps between a stool's legs, under a table, and in an L's notch are not walkable, which is what a colonist would do with most of them anyway, and what keeps the sort exact (section 8). A def with only flat parts has no collider: a crafting spot does not block.

### 11.2 Placed items as obstacles

Nav carving and placed-obstacle collision read the placement executor's chunk index, which holds what the world places, and `PlacementSystem::spawnEntity` creates an ECS entity, so an installed item is not in that index. Placed items have their own source, and a target is never an item's pivot, which its collider covers; it is a stand point in a use area (section 12):

- **`PlacedItemIndex`** (`libs/sim`): every installed entity whose def has a collider, by entity: its def, position, and turn, bucketed by chunk, with each entry's collider ring and reserved region (section 4.2) in the world. It holds generic placed obstacles; nothing in it, in nav, or in vision names furniture. An entry goes in when an item is installed (`spawnEntity`), moves when it is relocated and installed again, and goes out when it is packaged for relocation or destroyed. At load it is rebuilt from the ECS entities the load restores, before the first mesh limit is served. A packaged crate is not in it.
- **Change list.** The index keeps its additions and removals since last read, and `NavigationSystem` takes them each update as it takes wall diffs from the construction world (`setConstructionWorld`): an item installed over meshed ground is added to the mesh at once (an Add, as a wall built is); one removed comes out (as a felled tree does). A mesh limit going in queries the index in its box beside the executor's chunk index, so items in ground not yet meshed go in with the limit. An item's mesh key is its entity id, beside the executor's chunk, position, and def keys in `obstacleInMesh`.
- **Reach.** Items are found by position, so the reach the limits and collision look out to (`nav::placedObstacleReachMeters`) also covers the largest parts collider reach the registry holds.
- **Collision.** `PlacedObstacleCollisionSystem` gathers its nearby obstacles from the index as well as the chunk index.
- **Sight.** A ring the mesh holds enters the placed occluder index, as every placed ring does, under the rules sight already applies to it; nothing is added for items.

From there the existing paths apply: `nav::placedObstacleOutline` puts the polygon in the world through `InstanceTransform(position, turn, 1)`, nav carves it, collision pushes against the same ring, and `isAreaBuildableNow` refuses ground under another item's collider, since a placed obstacle in the mesh refuses it.

### 11.3 The spatial query

The snap engine (section 14.3) and the placement check (section 14.2) read `PlacedItemIndex::query(box)`, which returns the entries whose reserved region meets the box, with their collider rings and reserved regions in the world. The wall tool reads the same query to refuse a wall through an item's clearance (section 13).

## 12. Use areas and targeting

An item's use areas are turned and moved by `InstanceTransform(position, turn, 1)`, as its collider is.

Whatever targets an item (the craft action and its successor in [Queries and chains](../queries-and-chains.md), hauling into storage, using furniture) asks for a stand point in its use areas instead of the item's position: `NavigationSystem::standPointIn(areas, from, agentRadius)` returns the point nearest `from` (the colonist), by straight line, that lies in one of the areas and where his disc stands on walkable ground clear of water, walls, and every placed obstacle, the item's own collider included, read as `standPointBeside` reads a spot. It walks the mesh faces the areas meet, nearest first. `routeEndOf` uses it for an item target and keeps its rule: a stand point counts only when connected to the colonist (`componentAt`), else there is no known way. It is the same rule that snaps every work target to a reachable point beside its entity, with the use areas narrowing where that point may be: a box is worked from wherever is nearest, a smelter only from its mouth.

An item is **blocked** when no point of any use area is a stand point by that test, whoever asks. It is evaluated when the item is installed and whenever a mesh patch lands that meets the box of its use areas. A blocked item shows a marker over it in the world, its info panel reads "Blocked: nowhere to stand", and every option that targets it reports "No room to use it" in the decision trace. An item that is not blocked but that a given colonist can't reach is that colonist's "no known way", as for any target. Discovery (a colonist uses only what he knows of) and reachability are otherwise unchanged.

## 13. Clearance

An item's clearance (section 4.2) is space other items' bodies and walls stay out of:

- Another item's collider may not meet this item's reserved region, and this item's collider may not meet another's reserved region.
- Two clearances may overlap; a clearance excludes only bodies. Two benches facing each other share an aisle.
- A wall, built or blueprint, may not cross any item's reserved region. The placement check reads walls through the construction world; the wall tool refuses a segment that crosses an item's clearance through the spatial query (section 11.3), as `isLineBuildableNow` already refuses one through its collider.
- Clearance is not a collider and is not in the mesh. Colonists walk through it.

The flush slide (section 14.3) moves an item until its reserved region touches the face, so a side with no clearance sits flush and a side with clearance stops that far off.

## 14. Placement and the ghost

### 14.1 State

`PlacementMode` gains the ghost's turn and passes it to its place callback: `PlaceCallback(defName, worldPos, turn)`. `ecs::Packaged` gains `std::optional<float> targetRotation`, set with `targetPosition`; the colonist installs the item at both, and `spawnEntity(defName, worldPos, turn)` writes `ecs::Rotation`. The last turn used for each def this game is kept by `PlacementSystem` and is the ghost's starting turn (0 the first time). Relocation starts from the entity's own turn. `/api/dev/spawn` takes `turn=<degrees>`, and the `what=stations` and `what=storage` views report `turnDegrees`.

### 14.2 Validity

`PlacementSystem::placeable(defName, worldPos, turn)` returns the first reason the item can't go, or none; the ghost shows the reason (14.4):

1. **Ground.** `isAreaBuildableNow(footprintAt(defName, worldPos, turn))`, outside an unbuilt site: clear, buildable ground, which another item's collider now refuses (section 11.2).
2. **Unexplored.** The footprint is covered by player visibility (`PlayerVisibilitySystem::covers`).
3. **Overlaps.** No other item's collider meets this collider (the spatial query; the same answer as 1 for items in the mesh, and the one that holds before a limit has meshed the ground).
4. **Clearance.** No other item's collider meets this reserved region, this collider meets no other item's reserved region, and no wall or blueprint wall crosses this reserved region.
5. **No room to use it.** Some use area holds a stand point by the test of section 12, with this item's collider counted as if placed. Connection to a colonist is not required: an item may be placed ahead of the paths to it, and is shown blocked or unreachable by section 12.

### 14.3 Input and snapping

Keys and gestures, while `Placing`:

- **Q / E** turn to the previous or next stop, counter-clockwise and clockwise on the screen (E increases the turn). Stops are multiples of `angleIncrementDegrees` (15 degrees, `snapping.xml`) from turn 0, plus the four quarter turns of the alignment direction when one is in range. With Alt held, Q and E turn by 1 degree.
- **Press, drag, release.** Press fixes the position; once the pointer is more than `doubleClickDistancePx` (4 logical pixels, `snapping.xml`) from the press, the turn follows the pointer: the front points from the pivot toward the pointer, `t = atan2(-dx, dy)` for a pointer at `(dx, dy)` from the pivot. Release places. A release within that distance is a click.

Turn snapping, first that applies; Alt drops both:

1. **Alignment.** The nearest wall face or foundation edge, blueprint or built, within `itemAlignRangeMeters` (2.0 m, `snapping.xml`) of the item's pivot gives a direction. A raw turn within `SnappingConfig::effectiveGuideAngleWindowDegrees()` (7.5 degrees, half the angle step while `tieAngleWindowToStep` holds) of that direction or one of its quarter turns snaps to it. The edges are found as the snap engine finds them (`nearestFoundationEdge`, and the wall queries the wall tool's snap uses).
2. **The angle step.** Otherwise the turn rounds to the nearest stop.

The angle step applies here regardless of the player's Angle steps setting, which starts off: that setting is a helper for the freeform drawing tools, whose basic level has the rectangle tool's grid to square buildings; an item has no grid, and an unsnapped turn is the stray-angle mistake the grid exists to prevent. Under Alt the turn is the raw drag direction, rounded to the quantum.

Then the position, the flush slide, which stays under Alt, as the wall tools keep their snaps to existing corners, edges, and walls under Alt ([Building and construction](../../design/game-systems/world/building-construction.md)): each edge of the item's reserved region (collider unioned with clearance) parallel to a wall face, or to an edge of another item's reserved region from the spatial query, within `guideDirectionToleranceDegrees` (1 degree) and within `edgeSnapRadiusMeters` (0.3 m) of it, moves the item along the face's normal until the edge's nearer end lies on the face; the closest such edge wins. With turns in quanta, a snapped edge is parallel to its face to within 0.005 degrees, so the far end sits off the face by under a millimetre over 3 m and never into it. No other position snap; the item otherwise sits at the pointer.

### 14.4 The ghost

`GhostRenderer` draws the ghost from a view at the ghost's turn: from the cache when built, else built on the main thread for that turn at the camera's level (one item's parts, a few dozen shapes) and put in the cache. During a free drag it is rebuilt at most once a frame. One item is cheap on the CPU batch, so the ghost keeps it: the view's strips joined, through `drawTriangles`, with the existing ghost alpha and invalid tint, on top (key 1000).

Under the ghost, on the ground and before the item, it draws:

- each use area, filled in the overlay's accent colour at low opacity with a solid edge;
- the clearance, hatched with diagonal lines in a neutral colour and a dashed edge, so it reads as kept clear rather than as a place;
- the footprint (the collider ring), a thin solid line.

All three follow the ghost's turn. Where placement is refused, the ghost and its overlays take the invalid tint and the reason (14.2) is drawn beside the ghost in a short label: "Not buildable ground", "Unexplored", "Overlaps an item", "Too close", "No room to use it". Pending installs (`Packaged.targetPosition` set) draw their ghost and overlays at their target turn. The overlay colours and line widths are keys in the placement config beside the ghost's alpha.

The key guide panel ([Building and construction, Key guide](../../design/game-systems/world/building-construction.md#key-guide)) gets a row for placing: "Q / E: turn. Drag: turn to face the pointer. Alt: hold to turn freely. Esc / Right-click: leave."

## 15. Selection

An item view carries its silhouette, built as a template's is: its triangles at the reference level rasterised by `geometry::silhouetteOfTriangles` into `rings` (close 0) and `hitRegion` (the small close), in pivot millimetres, with the view's turn already in them. Selection for a parts item reads the view of the entity's def and turn and translates by the position (no rotation: the view is turned already). The thick outline strokes `rings`; the hit-test tests `hitRegion`. A stool's legs and seat give one ring once closed. Click priority stays "Furniture and placed objects".

A single outline injected at the silhouette's largest y would draw over a colonist standing in front of a diagonal bench's high end. So the selected item's outline is cut by the view's strip bands into pieces, each with its strip's anchor, and `emitSorted` injects each piece before the first upright strictly nearer than its anchor, as it injects the one outline of any other entity.

## 16. Asset manager

The detail pane for a parts def shows:

- The item at a turn, with a turn slider and Q/E stepping, and a row of 24 small views every 15 degrees, so a face that reads wrong at some angle is seen at once. A level picker shows each detail level.
- Over the large view and every small one, at the view's turn, the same overlays as the placement ghost (section 14.4): use areas filled in the accent colour, the clearance hatched, the derived collider as the existing cyan outline. Each use area is labelled with its name. Toggles hide each kind.
- The parts list: each part's footprint, heights, and placements, and its tops and faces with the drawing each uses or "plain" and the colour, so an author sees which faces still need art; the use areas and clearance with each shape's source (XML or the drawing's file).
- The counts: unique parts, placements, drawings (tops, sides, areas), plain faces, and vertices at the finest level.
- Validation issues per drawing (proportions against faces, the art check) and per area.

Thumbnails are the turn-0 template (section 6.9). Reload rebuilds views, since drawings are files.

## 17. What a shadow system reads

A placed item's part footprints, placements, bottom and top heights, and turn are the data ground shadows are built from; they are on the def and the entity, nothing render-only. Ground shadows are designed in WOR-1135 (Shadows: one soft-edge system for placed-item shadows, wall shadows, and sight edges), which reads the same `<light>` element as side shading.

## 18. Config

| Key | File | Default | Meaning |
|---|---|---|---|
| `heightRatio` | `assets/config/rendering/view.xml` | 0.35 | Screen metres a metre of height rises; shared by everything with height |
| `light azimuthDegrees`, `elevationDegrees` | same | 315, 40 | The one light |
| `sideShading ambient`, `diffuse`, `wrap` | same | 0.66, 0.6, 0 | Side shading (section 7) |
| `partOutline color`, `widthMeters`, `topColor`, `topWidthMeters`, `topOpacity` | same | #33241A, 0.03, #33241A, 0.025, 0.8 | Part outlines |
| `drawings aspectTolerance`, `curveToleranceMeters`, `overflowMeters`, `faceDetailMinWidthMeters` | same | 0.01, 0.002, 0.01, 0.04 | Drawing proportions (relative), curve flattening, the overflow warning, the narrowest face that shows its drawing |
| `depthStripWidthMeters`, `depthStripMergeMeters` | same | 0.25, 0.02 | Depth strips |
| `itemAlignRangeMeters` | `assets/config/construction/snapping.xml` | 2.0 | How far a wall or edge turns an item square to it |
| `angleIncrementDegrees`, `guideAngleWindowDegrees` (read through `effectiveGuideAngleWindowDegrees()`), `edgeSnapRadiusMeters`, `guideDirectionToleranceDegrees`, `doubleClickDistancePx` | same (existing) | 15, 7.5, 0.3, 1, 4 | Shared with the drawing tools |
| `pathingClearanceMeters` | `assets/config/construction/constraints.xml` (existing) | 0.7 | The default use area's distance |

The turn quantum, 0.01 degrees, is part of the stored format, not tuning, and is a constant (`kTurnQuantumDegrees`).

## 19. salal and worldsim

**salal** owns the generic drawing capabilities:

- `renderer::appendShapeMesh(const LoadedSVGShape&, TessellatedMesh&, const ShapeMeshArgs&)` with `ShapeMeshArgs { AffineTransform2D transform; VectorEffect vectorEffect; float brightness; }`, names from CSS and SVG (`transform: matrix()`, `vector-effect`, `filter: brightness()`). The fill is tessellated and coloured in the shape's own space and its vertices are then mapped by `transform`. Strokes: with `VectorEffect::None` the band is built in the shape's space and then mapped (SVG's behaviour: the stroke's outline is transformed, so a skew thins it); with `NonScalingStroke` the centreline is mapped first and the band built at `strokeWidth` in the output space. `brightness` multiplies the fill colour, gradient stops, and stroke colour in RGB. The two-argument call is this with the identity and 1.
- `AffineTransform2D` in foundation (six floats, `a b c d e f` in SVG order), since foundation has no 2D affine type.
- `drawInstanced` with an optional index range.
- A `salal::svg_build` alias for `salal_svg_build`, so `checkArt` is linked by name.
- Tests: a horizontal stroke under `matrix(1, 0, 0, 0.35, 0, 0)` keeps its band width with `NonScalingStroke` and is 0.35 as tall with `None`; a gradient fill under a transform has, at each mapped vertex, the colour of its source vertex; an index range draws only its triangles.

**worldsim** owns everything specific to the game: the `parts` asset type, parser, validation, areas, and script hook (`libs/assets`); item views, strips, detail levels, and their silhouettes (`assets::buildItemView` in `libs/assets`, which links the renderer for tessellation as assets already does); the view config; `PlacedItemIndex`, its change list, `standPointIn`, the blocked state, and the clearance rules (`libs/sim`); the strip pointer through `PlacedEntity` and `Upright`, the item view cache and builder, and the strip draws in `InstancedEntityRenderer` and `RunPlanner` (`libs/world-view`); `DynamicEntityRenderSystem`; the turn in `PlacementMode`, `PlacementSystem`, `GhostRenderer`, `Packaged`, and the install action; the turn and flush snaps; the overlays; selection; the asset manager.

## 20. Testing

Unit tests (`assets-tests`, `world-view-tests`, `sim-tests`), merged into the suites that cover the same paths:

- The face matrix maps a drawing's four corners onto a face's four projected corners at turns 0, 35, 90, 180, 271, and 325 degrees. The visible-face set of a rect is front only at 0; front and right at 35; right only at 90; back only at 180; front and left at 271 and 325.
- Narrow faces: the workbench carcass front at turn 271 (projected width 0.050 m) carries its drawing; at 270.5 (0.025 m) it is plain. A stroke wider than a face stays inside the face's parallelogram.
- A stroke on a skewed face keeps its width: the band's perpendicular thickness on screen equals the stroke width within 1%.
- Shading: the four rect faces at turns 0, 45, 325, against section 7 (325: left end 0.740, front 0.66), and a face looking straight down at wrap 1 is 0.727.
- Parts order: a shelf at bottom 0.15 m behind two posts at bottom 0 draws before the posts; a 1.2 m by 0.1 m board at 45 degrees and a 0.2 m box beside it, south of the board where they share verticals, draw board first; validation rejects two parts that overlap in plan and in height.
- Depth strips: a plain 3 m by 1 m bench at 0 degrees is one strip; at 45 degrees it is 12 strips whose anchors fall along the front edge; strip boundaries include every hull vertex x of an L-shaped item; a 6 m counter at 4.5 degrees merges in pairs and no merged strip's anchors spread past 0.02 m; a colonist record with feet 0.35 m in front of the 45-degree bench's high end sorts after the strip over him and before the bench's low end's strip.
- Draws: with the planner, one item overlapping nothing is one draw; ten items of one def at one turn are one draw; a colonist in front of a 45-degree bench's middle splits it into three.
- The view cache key wraps turns (360.004 and 0.004 degrees share a view) and rounds them to the quantum.
- The derived collider is the hull of the non-flat footprints, turned with the entity, and is the ring nav carves (extend `PlacedObstacle.ColliderCornerLandsOnTheDrawnCorner` to a parts def).
- `PlacedItemIndex`: an installed item is carved into meshed ground; packaged, it leaves the mesh; after a reload the same ring is in the mesh; `PlacedObstacleCollisionSystem` pushes a colonist out of it.
- Use areas: a box's default area gives the stand point nearest the colonist on every side; a smelter's front rectangle never yields a point behind it; walling the front makes it blocked; an XML rectangle and a drawing of the same rectangle give the same polygons.
- Clearance: two benches with front clearance facing each other 1.0 m apart are both placeable (clearances overlap); a box placed in a bench's clearance is refused with "Too close"; a bench with no back clearance slides flush onto a wall, one with 0.1 m stops 0.1 m off.
- Turn snapping: a raw 92 degrees beside nothing snaps to 90; beside a wall at 20 degrees, 108 snaps to 110; under Alt 92 stays 92 and the flush slide still puts a parallel edge on a wall; E from 0 beside that wall stops at 15, then 20, then 30, and Q from 0 at 345. The flush snap puts the bench's back edge on the wall face within 1 mm.
- Validation rejects a side drawing 2% off its face's proportions, a drawing outside the folder, an unplaced part, `worldHeight` on a parts def, a use area with no room outside the collider, and a view over the vertex limit.
- `InstanceTransform.BakedStaticsEmitIt` and the selection round trip stay green.

Happy-path scenario: `docs/testing/scenarios/22-placed-item-turns.md` (spawn a workbench at turns 0, 35, 180 with `turn=`; it shows front, front and right end, back; click each, one outline; place a crafted box with Q twice beside a wall and see it square and flush; see a smelter's use area and clearance on its ghost and in the asset manager; wall off a box and see it marked blocked; walk a colonist round a 45-degree bench and see him draw in front of its front and behind its back). Scenario 01's crate steps cover installing a parts item.

These tests cover what the code decides: geometry, order, snapping, and the rules. Whether a drawing reads well at every angle is the asset manager's 24-view row, checked by eye.

Performance: what can move is the upright stream's draw count, the planner's work, and view builds and uploads. A scoped capture (`node scripts/perf.mjs capture`, after a `machine-load` check reads `idle` or `mostly-idle`) at zooms 1 and 0.25 over a room of 30 items at free turns with colonists walking among them, against `main` with the same room as upright items, read through `node scripts/perf.mjs compare` and `uprightDrawCalls` from `what=uprights`. View builds are timed on the builder: a workbench view, and the load-time build for 500 placed items.

## 21. Prior art

| Source | What it does | Why it is not this |
|---|---|---|
| RimWorld ([modding: textures](https://rimworldwiki.com/wiki/Modding_Tutorials/Textures)) | Four facings as separate textures (`_north`, `_south`, `_east`, west mirrored), a raster tile size, a dark outline; the front sliver is drawn per object by eye, with no published tilt; an interaction cell per building | Quarter turns only, four drawings per object, raster |
| The Sims 1 ([object creation](https://cyndersanity.github.io/sims-legacy-mod-guide/object-creation.html)) | Each object pre-rendered at 4 rotations and 3 zooms, with depth sprites | Fixed angles, raster |
| Unity ([2D or 3D](https://docs.unity.com/en-us/engine/current/manual/get-started/first-time-user/2dor3d)); oblique camera by a custom projection matrix ([Game Developer](https://gamedeveloper.com/programming/adding-some-perspective-to-your-unity-2d-game-)) | The engines' answer to any angle with height: real 3D meshes under an orthographic or oblique camera | Needs modelled meshes and a 3D pipeline beside the vector one |
| Sprite stacking ([GameMaker guide](https://medium.com/@dev_dwarf/intermediate-guide-to-sprite-stacking-using-gamemaker-studio-2-26f1e9101371)) | Slices of an object drawn offset up the screen | Gaps at steep angles, poor fine detail, many layers an object |
| Rudimentary 3D on a 2D canvas ([Petzold](https://charlespetzold.com/blog/2024/09/Rudimentary-3D-on-the-2D-HTML-Canvas.html)) | Faces mapped by affine transforms on a 2D canvas, convex shapes only | The same face mapping, which is why parts are convex prisms |
| Oblique projection ([Wikipedia](https://en.wikipedia.org/wiki/Oblique_projection)) | One plane drawn undistorted, depth drawn along a fixed direction | The projection this uses, with the plan as the undistorted plane |

No shipped game was found that lays 2D side art onto a freely turning prism. The industry norms are 3D meshes or a fixed set of pre-rendered angles. The first needs a modelling pipeline the vector art doesn't have and draws a different style; the second gives a fixed set of turns and multiplies the drawings by the number of angles. A prism with affine-mapped faces keeps hand-drawn vector art, one drawing a face for every angle, and the existing tessellation and instanced draw paths.

## 22. Related documents

- [Rotatable placed items](../../design/game-systems/world/rotatable-placed-items.md): the design spec
- [World depth sorting](./world-depth-sorting.md): the stream the depth strips enter
- [Entity selection silhouette](./entity-selection-silhouette.md): silhouettes and hit regions
- [Collision shapes](../vector-graphics/collision-shapes.md): colliders, the pivot, the nav ring
- [Nav mesh at variable resolution](../nav-mesh-variable-resolution.md): limits, placed obstacles by entity, adds and removals
- [Ground textures, LOD](../ground-textures.md#lod): `data-lod` and its reader
- [Asset definitions](../asset-system/asset-definitions.md) and [Folder-based assets](../asset-system/folder-based-assets.md): the other asset types
- [Building and construction architecture](../building-construction-architecture.md): the snap engine and its config
- salal `docs/shader-hooks.md` and `docs/masks.md`: the engine's rendering contract
