# 2026-06-18 - Vision System: honest sight and belief

## Summary

Colonists now see the world honestly: sight is occluded by walls, and a colonist only knows what it has actually observed. A colonist indoors no longer sees the bush outside the wall; it sees through doorways and windows; it remembers the structures and entities it has seen; and when it looks at a spot where it remembered something and finds it gone, it forgets. This is the near-term Vision System epic (spec `docs/technical/vision-architecture.md`) — everything except witnessing and player-facing fog of war, both deliberately deferred. It was the prerequisite for the navigation epic's belief-filtered planning (nav P4): belief filtering is hollow without an honest write path, and this is that write path.

Five PRs: #172 (visibility polygon), #173 (GeometryIndex + sight-transparency), #179 (occlusion-gated VisionSystem + debug overlay), #183 (structure-as-observable), #184 (stale-memory reconciliation).

## Details

The work splits into an exact-geometry primitive (`libs/geometry`) and the engine integration (`libs/engine`, `apps/world-sim`), the same shape as navigation. All vision geometry is integer millimeters; floats appear only at the meters↔mm boundary and in the sight-circle approximation.

### Geometry (libs/geometry/visibility)

- **`computeVisibilityPolygon(observer, sightRadiusMm, occluders)`** — the star-shaped region the observer can see, bounded by the nearest opaque occluder segments and clipped to the sight circle. A stateless angular sweep over candidate directions (every occluder endpoint plus sight-circle samples), each ray cast on both angular sides so a grazed endpoint splits cleanly into a near corner and the far-field continuation. The ray's far point is an exact integer multiple of the direction, so it passes precisely through endpoints; graze sides are classified by an exact cross-product; direction ordering uses the library's `angleLess` plus a squared-length/lexicographic tiebreak for a strict, deterministic total order. The sight circle is the one inexact step (a fine N-gon, float-snapped to mm — the codebase's standard treatment of curved geometry).
- **`hasLineOfSight(observer, target, radius, occluders)`** — the one-off "can A see B" query, for AI and cheap candidate tests.

Validated by a brute-force per-ray oracle (hundreds of random scenes, hundreds of angles each, tens of thousands of rays) plus named cases: single-wall shadow, closed room, doorway gap, observer on an endpoint, occluder crossing the circle.

### Engine (libs/engine)

- **Opening sight-transparency** — `OpeningTypeDef` gains `transparentToSight`, independent of `pathable`: doors and windows both pass sight (a window blocks movement but not vision), so both are gaps in the sight-occluder line.
- **GeometryIndex** (`libs/engine/vision/`) — the occluder source. It walks the built wall graph and, per segment, subtracts the centerline spans of its transparent openings (the same clear-width span formula `NavInputBuilder` cuts the band at, so sight gaps match navmesh gaps), emitting the remaining solid sub-spans as opaque occluder segments tagged with their source id. Version-gated against `ConstructionWorld::version()`, rebuilt synchronously (extraction is cheap). This is the honest realization of the spec's "one GeometryIndex": one source of truth (`ConstructionWorld` + its version counter), two derived projections — nav's thick bands and vision's thin occluder centerlines — rather than one shared structure.
- **VisionSystem rewire** — per observer, it queries the occluders in sight radius; with none (the outdoor case, overwhelmingly common) there's no polygon and discovery runs exactly as before (radius only). With occluders in range it builds `computeVisibilityPolygon` once, caches it per observer (rebuilt only on a >0.5 m move or a geometry-generation change), and gates every candidate by `pointInPolygon`. On top of the gate it runs structure-as-observable discovery and stale-memory reconciliation (below).
- **Memory** — new ID-keyed `knownSegments`/`knownOpenings` stores (no LRU — construction ids are stable, never reused, bounded). `clear()` resets them.
- **VisionOverlay** (`apps/world-sim`) — `V` toggles a draw of each observer's visibility polygon and the occluder segments, for verification.

### Discovery semantics (the "honest belief" half)

- **Structure-as-observable** — when the polygon is (re)built, every built segment or opening it sees (radius cull, then either endpoint in the polygon or the span crosses the polygon boundary — the edge-crossing fallback matters because a wall *is* part of the boundary) enters Memory by its stable id. Discovery granularity is the structure id, matching belief filtering exactly.
- **Reconciliation** — each tick, for every remembered entity whose spot the observer can currently see, it asks the placement index whether the entity is still there (`hasNearby`); a destructively removed entity is gone → forgotten, a regrowing/cooldowned one is still indexed → kept. A colonist watching a bush get cleared drops it from memory before walking over, but never forgets what it can't see. ActionSystem's on-arrival forgets stay as the synchronous backstop (removing them opens a 0–5 frame window where the AI re-selects the just-failed target and oscillates); vision is the continuous away-from-target corrector, ActionSystem the at-target one.

### Key decisions

- **GeometryIndex is vision-specific, not a shared output with nav.** Nav needs thick bands (thickness + junction offsetting), vision needs thin centerline occluders + a transparency flag. They share the source and the version counter, not the derived cache — which avoids both a second source of truth and a pointless refactor of the just-shipped navigation.
- **Outdoor fast path.** Most observers, most of the time, have no occluders in range; those pay only the (cheap) occluder query and behave exactly as before walls existed. The polygon machinery is the indoor exception, cached while stationary.
- **Determinism kept.** Sweep ordering, structure-set inserts, and collect-then-forget reconciliation are all order-independent.

### Verification

geometry-tests 216/216 (the visibility oracle sweep among them); engine-tests 679/679 across the new GeometryIndex and VisionSystem suites (occlusion, doorway, outdoor fast path, structure discovery incl. the long-wall edge-crossing case, reconciliation incl. occluded-not-forgotten). Sandbox-verified in the running game: a colonist's 30 m sight polygon clipped into a shadow wedge behind a wall.

## Related Documentation

- Spec: `/docs/technical/vision-architecture.md`
- Navigation (the consumer that forced this; shares the geometry substrate): [2026-06-16 - Navigation v1](./2026-06-16-navigation-v1.md)
- Memory design: `/docs/design/game-systems/colonists/memory.md`

## Next Steps

- **Navigation P4 — belief-filtered planning:** now unblocked. Memory-filtered path planning, discovery replans, door-permission costs.
- **Witnessing:** deferred. No event bus exists; the payoff is sub-100ms once reconciliation + cached polygons already give next-tick correction. A ~10-line subscription hook is noted for when it's built.
- **Structure-demolition reconciliation:** a small follow-up — the id-only structure store needs a stored position to spatially gate forgetting a demolished wall.
- **Fog of war:** its own later epic. D5 of the spec establishes that the per-faction polygon set it needs falls out of this work for free; nothing here anticipates it beyond keeping the polygons accessible.
