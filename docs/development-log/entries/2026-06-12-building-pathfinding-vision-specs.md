# 2026-06-12 - Building, Pathfinding, and Vision Specs

## Summary

Spec-writing session, no code. Designed the freeform building/construction system end to end (design + technical architecture), then the two systems it forced into existence: a four-tier pathfinding architecture and a vision/line-of-sight architecture. Revised the memory design along the way: memory is now a snapshot that can be wrong, which makes belief-filtered navigation (nobody magically knows your floor plan) the connective tissue across all three specs.

## Details

**New documents:**
- `docs/design/game-systems/world/building-construction.md` — freeform polygon foundations (no grid), walls with thickness presets and snapping, doors/windows as parameterized assets, room detection, deliver-then-build loop where colonist work drives procedurally rendered progress. All constraints derive from a pathing-clearance constant (anti stuck-entity pockets); all units in meters; everything configurable in `assets/config/construction/`.
- `docs/technical/building-construction-architecture.md` — integer-quantized geometry (int64 mm), in-house geometry library (no Clipper2/earcut: booleans via the same arrangement + face-extraction core room detection needs), topology graph as source of truth with ECS mirror, walls center-on-line except outer-flush at foundation edges, single host foundation per wall, rendering through the existing baked uber-shader path with construction progress as an index-buffer prefix.
- `docs/technical/pathfinding-architecture.md` — four resolution tiers: planet hex graph (already RAM-resident from worldgen) for abstract raid parties, chunk connectivity components for O(1) reachability, dynamic CDT navmesh consuming exact wall-band polygons, collision circles + velocity-obstacle steering so agents take up space. Ray marching evaluated honestly: rejected as a router (potential-field local minima), adopted as the local perception layer (LOS, clearance, steering rays). Belief-filtered navigation: path queries take the agent's memory as a filter over structure IDs; freespace-assumption replanning; search behaviors (door discovery, LKP hunting, stale-target reconcile).
- `docs/technical/vision-architecture.md` — visibility polygons (rotational sweep, outdoor fast path) over a shared GeometryIndex that also feeds nav and steering; structures are observables at segment granularity; windows pass sight while blocking movement; discovery/witnessing/stale reconciliation all live in vision. Fog of war established as a free byproduct, deferred to its own epic.

**Revised:**
- `docs/design/game-systems/colonists/memory.md` — "State Is Always Current" superseded by "Memory Is a Snapshot, and Can Be Wrong"; search & discovery behaviors promoted from future-feature stub; structures added to memory; current-task navigation states UI (searching as a mild warning state); vision occlusion as a named dependency.
- `docs/status.md` — planned epics added: Building & Construction System, Navigation & Pathfinding (6 phases), Vision System: Occlusion & Discovery (fog of war explicitly excluded, later).

**Key decisions:**
- Roll our own geometry; borrow concepts (integer coords, miter limits, post-boolean simplification) not libraries.
- Navigation is not a build-order prerequisite for construction; it gates walls *shipping as gameplay*. The D5 interface contract (obstacles with stable IDs, portals, reachability, version counter) decouples the workstreams.
- Vision occlusion ships with belief filtering or belief is hollow; the vision epic must land with or before nav P4.
- Structures never unload; only chunk-derived data (meshes, nav tiles, components) does.

**Gap analysis findings (recorded in the specs):** bulk material logistics needs a design decision; structure damage/repair is a known later dependency of raider door-breaking; the material economy (choppable trees, wood/stone items) extends the early crafting implementation as needed; goal-driven task generation PR #115 already contains the BuildGoalSystem hook construction plugs into.

## Related Documentation

- [Building & Construction (design)](../../design/game-systems/world/building-construction.md)
- [Building & Construction Architecture](../../technical/building-construction-architecture.md)
- [Pathfinding Architecture](../../technical/pathfinding-architecture.md)
- [Vision Architecture](../../technical/vision-architecture.md)
- [Memory System](../../design/game-systems/colonists/memory.md)

## Next Steps

- Geometry Foundations epic (libs/geometry) is the root dependency for construction, navigation, and vision.
- Navigation approach is committed (vector-native CDT); implementation phases P1-P6 defined in the pathfinding spec.
- Fog of war and the rooms overlay wait for the overlay system.
