# Collision Shapes

Created: 2025-10-24
Status: Implemented (2026-06-24)

An asset's collision geometry is separate from its render geometry. The render
mesh is hundreds of triangles for visuals; the collider is one simple shape, so
nav and physics stay cheap. A tree's canopy is ~500 triangles to draw and a
single trunk rect to collide with.

## The shape

`AssetDefinition::collision` is a `CollisionShape` (`libs/engine/assets/AssetDefinition.h`):

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
trees); a **Polygon** is the rare complex case. There is no Circle and no
compound shape; an asset never carries more than one collider.

The rect is authored axis-aligned in local meters (`offsetMeters` center +
`halfExtentsMeters`). It is **not** a world-space AABB: entities rotate, so at
runtime the 4 local corners (`rectCornersLocal()`) are transformed by the
entity's scale, rotation, and position into a world-space **oriented quad**
(OBB). `rectCornersLocal()` is the single source of the corner set. There is no
world-AABB fast path.

## Authoring (three homes, by asset type)

1. **Procedural (Lua) assets emit it from the generator.** A generator script
   calls `asset:setCollisionRect(halfWidthMeters, halfHeightMeters, centerXMeters,
   centerYMeters)` (`libs/engine/assets/lua/LuaEngine.cpp`). The trees emit a
   trunk rect from `trunkWidth`: `asset:setCollisionRect(trunkWidth/2,
   trunkWidth/2, 0, 0)` in the shared `deciduous.lua` / `conifer.lua` /
   `palm.lua`. Coordinates are in the script's `(0,0)`-centered meter frame (see
   the flora-asset skill's lua-api reference). Non-positive extents are ignored.

   Because the navmesh reads collision eagerly but a generator otherwise runs
   lazily at first render, `AssetRegistry::loadDefinitions` runs a post-pass that
   executes each procedural generator once at load (cheap; no tessellation) to
   capture the emitted rect, so nav sees the collider before the first render
   instead of racing it.

2. **XML manual override.** `<collision><rect minX="" minY="" maxX="" maxY=""/></collision>`
   parses to center + half-extents (degenerate `max <= min` is rejected to
   `None`); `<collision><polygon><point x="" y=""/>...</polygon></collision>`
   needs >= 3 points. Parsed in `AssetRegistry.cpp`. **XML wins over a Lua emit**
   when both are present.

3. **SVG `<metadata>` for simple (SVG-backed) assets** — `<metadata><collision>
   <shape type="aabb" min="x,y" max="x,y"/></collision></metadata>` (or a
   `type="polygon" vertices="x1,y1 ..."`), read via a pugixml second pass over the
   .svg (NanoSVG ignores `<metadata>`) at load. The coords convert to the *same*
   local-meter frame the render mesh uses — `(svgPoint - svgBboxCenter) *
   scaleFactor`, where `scaleFactor = worldHeight / svgHeight` over the loaded
   path-vertex bbox — via the shared `computeSvgMeterFrame` helper that
   `getTemplate` also uses, so the collider can't drift off the sprite. SVG
   metadata wins over XML for simple assets. First consumer:
   `assets/world/misc/BigRock` (a tree-sized boulder you can't walk through).

## Consumers (the collider feeds two systems at the same clearance)

- **Navigation.** `NavInputBuilder` (`libs/engine/nav/`) carves the rect into the
  viewport-anchored simulation-area navmesh as an obstacle ring, inflated outward
  by the flora pad `kFloraColliderPadMm` (0.05 m) so a routed agent clears the
  trunk. Polygon and rect both transform through the same scale/rotate/translate.
  Agents route around the obstacle.

- **Tier-3 static collision.** `StaticRectCollisionSystem` (priority 270) pushes
  an agent **center** out of the rect inflated by the **same** 0.05 m pad (a
  point-vs-oriented-OBB push-out), so an agent shoved into a trunk by other
  forces can't walk through it. It does not use the agent's disc radius as the
  clearance.

The two boundaries coincide on purpose: the pad is applied in local space
*before* the entity scale in both (`scale * (halfExtent + pad)`), so nav and
Tier-3 agree at every entity scale and never fight (no boundary jitter). Keep any
future Tier-3 clearance `<=` the nav pad.

## Tooling

The asset-manager detail pane draws the collider as a cyan outline over the
preview (aligned via the preview's own `fitToRect` transform), with a "no
collider" label when `type == None`, so an author can confirm a trunk rect sits
on the trunk. See `apps/asset-manager/AssetDetailView.cpp`.

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
