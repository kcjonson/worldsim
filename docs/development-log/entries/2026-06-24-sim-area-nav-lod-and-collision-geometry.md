# Simulation-area navigation LOD + per-asset collision geometry

Date: 2026-06-24

## Summary

Driving the real game surfaced a pre-existing nav-v1 bug: the navmesh never
built in a normal forested world. `NavInputBuilder` carved every loaded
collision-bearing tree into one merged loaded-area mesh (~88k tree polygons on a
forested quickstart), so the off-thread `buildNavMesh` never finished,
`hasMesh()` stayed false, the `N` debug overlay was empty, and colonists beelined
instead of pathfinding. The world is infinite, so the fix is architectural, not
"handle 88k": navigation fidelity became a level-of-detail structure anchored to
the viewport. Alongside it, the long-specced per-asset collision geometry landed.

Shipped as a new epic (separate from the merged P3 regional layer, which is what
makes the small area mesh fast and correct). Five merged PRs.

## What shipped

- **Simulation-area nav LOD** (PR #217). The navmesh now builds over a
  viewport-centered square AABB (the "simulation area") instead of all loaded
  chunks: only the in-area obstacles feed the build (~1-2k, not ~88k), so a
  forested world builds in well under a second. The area tracks the camera (pan
  recenters, zoom resizes; half-extent clamped to [30 m, 64 m] and the loaded
  extent; rebuild on center drift > 20 m, size change > 20%, construction-version
  change, or in-area placement completion). Off-area path queries beeline (the
  LOD0/beeline seam) instead of reading as stuck. Phases A (the fix), B (viewport
  tracking), C (query seam).

- **Rect collision primitive** (PR #220). `CollisionShapeType` is now
  `{None, Rect, Polygon}` — Circle removed. A rect is authored axis-aligned in
  local meters (center + half-extents) and transformed to a world-space oriented
  quad at runtime (`rectCornersLocal()`). Three authoring homes: procedural
  generators emit it via `asset:setCollisionRect` (captured eagerly at load so
  nav sees it before first render); XML `<collision><rect>`; SVG `<metadata>`
  (deferred, Phase D2). Oak/Maple/Palm/Pine migrated off XML circles to
  Lua-emitted trunk rects. See [collision-shapes.md](../../technical/vector-graphics/collision-shapes.md).

- **Tier-3 static-rect collision** (PR #221). `StaticRectCollisionSystem`
  (priority 270) pushes an agent center out of the rect inflated by the nav flora
  pad (0.05 m), so a shoved agent can't walk through a trunk. The pad is applied
  in local space before the entity scale, mirroring nav exactly, so the collision
  boundary coincides with the navmesh obstacle boundary at every scale and the
  two never fight.

- **Asset-manager collision overlay** (PR #225). The detail pane draws the
  collider as a cyan outline over the preview (aligned via the preview's own
  `fitToRect` transform — single source, no drift), with a "no collider" label
  for `None`; preview enlarged. An author can now confirm a trunk rect sits on
  the trunk.

## Technical decisions

- **LOD anchored to the viewport, not chunks.** Fidelity follows what the player
  is looking at. Outside the area: beeline now; a coarse far-field graph is a
  future LOD level.
- **Collision clearance = the nav pad, not the agent disc.** Tier-3 keeps the
  agent *center* out of the rect-inflated-by-0.05 m, matching what the navmesh
  guarantees. Using the 0.3 m agent radius would shove agents off valid routes.
- **Rect, not circle.** Rect-or-polygon only; a long thin obstacle needs a long
  thin rectangle, and Phase E's push-out wants the OBB form (center + axes +
  half-extents), not a generic polygon.
- **Eager Lua collision capture at load.** Collision is read eagerly by nav but a
  generator runs lazily at first render; a load-time post-pass runs each
  procedural generator once (no tessellation, cheap) to capture the emitted rect.

## Reviews caught real bugs pre-merge

Each phase went through adversarial multi-agent review:
- A/B/C: a rebuild-on-placement regression (a stationary player on load kept a
  mesh missing obstacles from spawn-ring chunks that placed after the first
  build) — fixed by rebuilding when the in-area processed-chunk set changes.
- E: a pad/scale ordering desync (the pad was added after scaling, so at the
  random flora scale 0.8-1.2 the collision boundary diverged from nav by up to
  0.01 m and spuriously pushed valid-path agents) — fixed to bake the pad in
  local space before scale.

## Files

- `libs/engine/nav/NavInputBuilder.{h,cpp}` — area-scoped build, rect obstacle, flora pad rename
- `libs/engine/ecs/systems/NavigationSystem.{h,cpp}` — simulation area, rebuild triggers, inSimArea
- `libs/engine/ecs/systems/AIDecisionSystem.cpp` — off-area beeline gate
- `libs/engine/assets/AssetDefinition.h` — CollisionShape (Rect), rectCornersLocal
- `libs/engine/assets/AssetRegistry.cpp` — XML rect parse + eager Lua capture
- `libs/engine/assets/lua/LuaEngine.cpp` — setCollisionRect binding
- `libs/engine/ecs/systems/StaticRectCollisionSystem.{h,cpp}` — Tier-3 push-out
- `apps/world-sim/scenes/game/GameScene.cpp` — area push + system wiring
- `apps/asset-manager/{AssetDetailView,AssetThumbnail}.{h,cpp}`, `libs/engine/assets/AssetRenderer.{h,cpp}` — overlay
- `assets/shared/scripts/{deciduous,conifer,palm}.lua`, the four tree XMLs — trunk rects

## Next steps

- **D2 — SVG-metadata collision authoring** for simple assets (no consumer yet;
  the subtlety is the lazy SVG->meter scaleFactor).
- **Visual-size obstacle LOD** (filed) to lift the 64 m cap so zoom-out covers
  the full view cheaply — the remaining zoom-out cost is rendering thousands of
  sprites, a render-LOD concern.
- A future coarse far-field nav layer (LOD1) for off-area long-haul routing.

## Related

- [collision-shapes.md](../../technical/vector-graphics/collision-shapes.md)
- [pathfinding-architecture.md](../../technical/pathfinding-architecture.md)
- PRs #217, #220, #221, #225
