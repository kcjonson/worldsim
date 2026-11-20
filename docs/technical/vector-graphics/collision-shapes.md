# Collision Shapes

Created: 2025-10-24
Status: Implemented (2026-06-24)

An asset's collision geometry is separate from its render geometry. The render
mesh is hundreds of triangles for visuals; the collider is one simple shape, so
nav and physics stay cheap. A tree's canopy is ~500 triangles to draw and a
single trunk rect to collide with.

## The shape

`AssetDefinition::collisions` holds one `CollisionShape` per age class (one entry for a def without `<ageClasses>`; read it with `collisionFor(ageClass)`, which a single-class def answers for class 0). A `CollisionShape` (`libs/assets/assets/AssetDefinition.h`) is:

```cpp
enum class CollisionShapeType { None, Rect, Polygon };

struct CollisionShape {
    CollisionShapeType     type = None;
    glm::vec2              offsetMeters{0, 0};       // Rect center, local meters
    glm::vec2              halfExtentsMeters{0, 0};  // Rect half width/height, local meters
    std::vector<glm::vec2> pointsMeters;             // Polygon vertices, local meters, CCW

    bool blocks() const { return type != None; }
    std::array<glm::vec2, 4> rectCornersLocal() const; // 4 CCW corners, center +/- half-extents
};
```

Rect or polygon, nothing else. A **Rect** is the default (a trunk-base rect for
trees); a **Polygon**, convex or concave, follows an outline a box can't (a boulder's rim). There is no Circle and no
compound shape; an asset never carries more than one collider.

The rect is authored axis-aligned in local meters (`offsetMeters` center +
`halfExtentsMeters`). It is **not** a world-space AABB: entities rotate, so at
runtime the 4 local corners (`rectCornersLocal()`) are transformed by the
entity's scale, rotation, and position into a world-space **oriented quad**
(OBB), padded as below. There is no world-AABB fast path.

## Authoring (three homes, by asset type)

1. **Procedural (Lua) assets emit it from the generator.** A generator script
   calls `asset:setCollisionRect(halfWidthMeters, halfHeightMeters, centerXMeters,
   centerYMeters)` (`libs/assets/assets/lua/LuaEngine.cpp`). The trees emit a
   trunk rect from `trunkWidth`: `asset:setCollisionRect(trunkWidth/2,
   trunkWidth/2, 0, 0)` in the shared `deciduous.lua` / `conifer.lua` /
   `palm.lua`. Coordinates are in the script's `(0,0)`-centered meter frame (see
   the flora-asset skill's lua-api reference). Non-positive extents are ignored.

   Because the navmesh reads collision eagerly but a generator otherwise runs
   lazily at first render, `AssetRegistry::loadDefinitions` runs a post-pass that
   executes each procedural generator once at load (cheap; no tessellation) to
   capture the emitted rect, so nav sees the collider before the first render
   instead of racing it. A def with `<ageClasses>` is skipped there:
   `prewarmTreeVariants` takes each class's rect from the variant 0 it
   generates anyway, and both `loadDefinitions` and `loadDefinitionsFromFolder`
   end in it, so a def loaded from a single file gets its rects too. The rect
   is per age class and must not depend on the variant; `asset-cli validate
   --render-smoke` checks variants 0 and 1 of each class, a def without
   `<ageClasses>` counting as class 0 of `variantCount` variants
   (`AssetRegistry::checkCollisionVariantIndependence`).

2. **XML manual override.** `<collision><rect minX="" minY="" maxX="" maxY=""/></collision>`
   parses to center + half-extents (degenerate `max <= min` is rejected to
   `None`); `<collision><polygon><point x="" y=""/>...</polygon></collision>`
   needs >= 3 points. Parsed in `AssetRegistry.cpp`, and applied to every age class.
   **XML wins over a Lua emit** when both are present.

3. **SVG `<metadata>` for simple (SVG-backed) assets** — `<metadata><collision>
   <shape type="aabb" min="x,y" max="x,y"/></collision></metadata>` (or a
   `type="polygon" vertices="x1,y1 ..."`), read via a pugixml second pass over the
   .svg (NanoSVG ignores `<metadata>`) at load. The coords convert into the
   def's pivot frame by `SvgMeterFrame::toPivotMeters`, `(svgPoint - pivot) *
   scaleFactor` with `scaleFactor = worldHeight / svgHeight` over the loaded
   path-vertex bbox, the same conversion the template mesh and the motion pivots
   go through. SVG metadata wins over XML for simple assets. First consumer:
   `assets/world/misc/BigRock` (a tree-sized boulder you can't walk through), whose
   collider is a polygon on its rim path's eleven points, the outline seen from above.

A `parts` def (a player-placed item) authors no collider; its collider is the convex hull of its parts' footprints, derived at load ([Rotatable placed items, section 10](../rendering/rotatable-placed-items.md#10-collision-nav-and-stand-points)).

## One pivot per def

Every def has one pivot: the point an entity's position stands for, as a Unity or Godot
sprite's pivot is. The registry builds every template mesh about it and stores every
collision shape about it, so nothing downstream converts anything:

- A simple (SVG) def's pivot is the centre of its drawing's path-vertex bbox.
  `buildTemplate` moves the mesh there; its metadata collider, an XML collider, and its
  motion pivots are in the same frame.
- A procedural (Lua) def's pivot is its generator's `(0,0)`. The trees put their trunk
  foot there, which is where their collider, depth anchor, crown spacing, and harvest
  stand points want the position to be.

Every draw path (baked and instanced statics, dynamic entities, piles, the packaged
crate, the placement ghost, the asset manager), the selection outline and hit-test, and
the collider place the pivot-frame shape at the entity's position by
`world = R(rotation) * (local * scale) + position`
(`assets::InstanceTransform`, `libs/assets/assets/placement/InstanceTransform.h`; the GPU instancing shader applies the same formula). The depth anchor reads the
footprint in the same frame, turned by the entity's rotation (`anchorLocalY`).
`PlacedObstacle.ColliderCornerLandsOnTheDrawnCorner` pins the collider to the drawing for
both asset types, and `InstanceTransform.BakedStaticsEmitIt` pins the bake to the transform.

## Consumers (one outline, two systems)

Both consumers read one ring: `nav::placedObstacleOutline` (`libs/sim/nav/NavInputBuilder.h`),
the placed entity's collider in world mm. It takes the shape for the entity's age class from
the registry and puts it in the world through `assets::InstanceTransform`, one rule for every
shape:

- a **Rect** is its four corners, each pushed out by the placed-obstacle pad
  `kPlacedObstaclePadMm` (0.05 m) in the local frame *before* scale, so the world boundary is
  `|scale| * (halfExtent + 0.05)` and a routed agent clears the trunk rather than grazing it;
- a **Polygon** is its points as authored, convex or concave, with no pad and no
  simplification: the polygon is drawn on the solid footprint, and an author who wants
  clearance draws it in.

The ring is normalised counter-clockwise (a negative scale mirrors it) and dropped if it has
no area. The consumers:

- **Navigation.** `NavInputBuilder` (`libs/sim/nav/`) carves the ring into the nav mesh as a
  blocked obstacle (`placedObstacleRing`). Agents route their centre around it.

- **Tier-3 placed-obstacle collision.** `PlacedObstacleCollisionSystem` (priority 270,
  `libs/sim/ecs/systems/`) pushes an agent **center** out of that same ring, so an agent
  shoved into an obstacle by other forces can't walk through it. It does not use the agent's
  disc radius as the clearance: the nav mesh routes the centre, so a disc push would stand
  the agent off the path the mesh gives it and the two would fight. It finds obstacles by
  entity position within `nav::placedObstacleReachMeters` (the farthest any def's collider
  stands from its pivot at the def's largest placement scale,
  `AssetRegistry::maxCollisionReachMeters`, recorded as each def's shapes are admitted, plus
  the pad's corner at the largest colliding scale, taken for every shape so it covers both),
  the same reach nav grows its limits' obstacle query by, so a collider far from its pivot is
  never missed.

Because collision reads the ring nav cuts, mm for mm, the two boundaries coincide at every
position, rotation, and scale and never fight (no boundary jitter);
`PlacedObstacleCollisionSystemTest.CollisionOutlineIsTheNavRing` pins it.

### The push-out

`resolveCenterAgainstOutline` is the usual circle-versus-polygon contact with a circle of
radius zero: if the centre is strictly inside the ring (`geometry::pointInPolygon`, exact, on
the centre rounded to mm), move it to the nearest point on the ring's boundary
(`geometry::closestOnSegment` over every edge, from the unrounded centre, so the coordinate
along the edge stays where it was). That nearest point is the push along the outward normal by
the penetration depth. On a slanted edge it is off the mm lattice, and rounded back to the mm
next tick it would be inside about half the time: pushed again, and taken by the nav mesh for
standing in the obstacle, which hands the mover to the forward snap. So the landing steps
outward until the stored position, rounded as nav rounds it, is strictly outside. Outward is
judged from the unrounded centre: one a hair outside a slanted edge whose rounding is inside is
moved further out, never in. A step is a quarter mm, or the float spacing at that position when
that is coarser, so near the landing site a push ends within about a millimetre of the boundary
(rounding moves a point at most 0.71 mm, plus one step). Positions are float metres from the
landing site with no rebasing, so far out the spacing sets the scale: 4 mm 50 km out, half a
metre 6,000 km out, where every millimetre rule here (this one, the mm rounding of nav rings)
works on that coarse lattice. If no step within 16 lands outside (heading out of a corner at
the bottom of a slot a few mm wide, every rounded step can land on a wall), the nearest point
of a 9 by 9 grid round the boundary point that rounds outside is taken instead, which on a
concave shape need not be in the same pocket the centre was pushed toward; failing that
the push is skipped rather than leaving the mover inside, and the forward snap has it. Box2D (`b2CollidePolygonAndCircle`) finds the same
contact by separating axes, which needs a convex polygon, so Box2D, Godot
(`CollisionPolygon2D`), and Unity (`PolygonCollider2D`) split a concave collider into convex
pieces. A push out of one piece can land inside its neighbour, across the diagonal the split
made, so for a zero-radius centre the nearest point on the whole outline is the exact answer
and needs no split: next to a notch it goes into the notch, and beside a reflex corner it is
the corner. Nothing is normalised, so no input gives a NaN; a centre exactly on an edge is not
inside and is left alone; edges equally near (a centre at a box's middle) go to the first in
ring order. A rect is this four-point case: the nearest face.

Each agent gathers the obstacles whose positions are within the query reach, skips any whose
own collider can't reach it (a squared-distance test against the shape's reach plus the pad's
corner at the instance's scale, plus 2 mm for the mm rounding near the landing site, which is
not enough far out where float spacing is coarser (WOR-972), before any ring is built; the reach is
`CollisionShape::reachMeters`, the one the registry records too), sorts the rest by position, and resolves them in
that order, twice, so an agent wedged between two settles against both.

### What a polygon must be

The nav mesh takes a simple closed ring: no crossing edges, no repeated closing point, area,
and no zero-length edge (it asserts on one). It normalises winding itself. The registry checks
each polygon as it admits a def's shapes (`AssetRegistry::admitCollisionShapes`) at the def's
smallest placement scale: at least 3 points, simple, with area, and consecutive points and
non-adjacent edges at least 3 mm apart, measured on the points rounded to the mm. Rounding
moves a point at most 0.71 mm, so that is over 1.58 mm as authored, and a rotated instance,
each point rounded to the mm again, keeps its points distinct and its edges uncrossed. One that
fails is one error per def in the validation report (field `collision`, naming every failing
age class when a def has several), is logged, and is dropped rather than handed to the mesh.
In the game an error stops startup: the loading scene fails on any validation error and goes
back to the splash, so a bad polygon never reaches a running world. The drop matters only
where loading carries on past errors: the asset manager, asset-cli, and tests. A clockwise
polygon is fine and is stored counter-clockwise.

## Tooling

The asset-manager detail pane draws the collider as a cyan outline over the
preview (aligned via the preview's own `fitToRect` transform), with a "no
collider" label when `type == None`, so an author can confirm a trunk rect sits
on the trunk (age class 0's, since it draws the default traits). See `apps/asset-manager/AssetDetailView.cpp`.

## Not built (and intentionally so)

The original 2025-10-24 design (in git history) sketched a broader menu --
Circle, compound shapes, and automatic collider generation from the render path
(bounding box, Graham-scan convex hull, Douglas-Peucker simplification). None of
that shipped. Colliders are **authored**, not auto-derived, and the runtime
shape set is just Rect + Polygon. Circle was dropped outright (a "super long
snake needs a long skinny rectangle, not a disc"). Revisit only if a real asset
needs a shape the rect/polygon pair can't express.

## Related

- [/docs/technical/pathfinding-architecture.md](../pathfinding-architecture.md) — the simulation-area navmesh that consumes the collider
- The flora-asset skill (`.claude/skills/flora-asset/`) — how authors declare collision on flora
