# 2026-06-13 - Construction Epic D: walls

## Summary

Walls on top of foundations (draft PR #138), built on the merged Epic C and the `WallOffset` geometry module from Epic A. A player picks Build > Wall, draws a polyline chain on a foundation with snapping and live validation, and the segments commit into the `ConstructionWorld` topology (with T-junction splitting), spawn blueprint entities, and build via the shared goal-driven lifecycle once the host foundation is built. Segments render as trimmed bands with junction polygons, are individually selectable with an info panel, and demolish per segment. Verified end to end in the sandbox: draw a foundation, build it, draw walls on it, colonists haul Wood and build the wall segments, select and demolish a segment.

## Details

Built as a sequence of merged sub-branches, each reviewed/tested before merge:

- **Data model (D1).** `ConstructionWorld` gained Vertex/Segment/Opening tables with stable ids alongside the foundation store: `findOrCreateVertex` (exact-position merge), `commitSegment` with T-junction split (splits a host segment at an interior touch into two new ids, re-attaches openings by parameter range), exact X-crossing rejection via `intersectSegments`, `segmentAt`/`vertexAt` queries, adjacency, version-counter bump. Topology integrity enforced here; soft constraints deferred to the validator. Later hardened to make `commitSegment` atomic (a rejected commit performs no split and bumps no version). 18 + tests.
- **Config (D1b).** Thickness presets per material (Wood Light/Standard/Heavy → pre-quantized half-thickness mm for `WallOffset`), wall constraints (min segment length 0.5 m, junction angle 30°, parallel clearance 0.8 m), and `ConfigValidator` wall checks. Foundation and wall share `MaterialDef` (wall presets are an optional vector). 30 tests.
- **Snap + validate (D2).** `SnapEngine::snapWall` (open chain; priority wall-endpoint > foundation-vertex > point-on-wall/T-junction > foundation-edge > angle > raw), and `ConstructionValidator` wall checks (segment length, junction angle, open-chain self-intersection, host-foundation band containment, overlap/parallel-clearance vs existing wall bands, X-crossing). Later hardened: snap only reports a T-junction on a strictly-interior projection; the validator exempts T-junctions (exact `EndpointTouch`) while still rejecting parallel overlap. 27 + tests.
- **Tool + render + lifecycle (D3).** `WallTool` in the existing DrawingSystem (chain draw, Alt freeform, Ctrl+click edge fill, host-foundation determination, commit → segment blueprint entities), interim Primitives band/junction rendering via `WallOffset::resolveWallBands` with a build-progress alpha ramp, config-strip wall mode (thickness preset cards + readouts), the "Wall" Build-menu entry with tool mutual-exclusivity, and the `ConstructionSystem` wall gate (a wall stays blocked until its host foundation is Built, then flows through the same umbrella/material-haul/build machinery; completion flips the segment to Built). Verified in sandbox.
- **Selection + demolish (D4 + D6).** `WallSegmentSelection` variant (priority above foundation, below entities), `segmentAt` hit-test, `adaptWallSegment` panel (material/thickness/length/state/progress/Demolish), band-outline indicator, and immediate per-segment demolition through the deferred entity-removal queue. Verified in sandbox.

## Key decisions / fixes

- **Goal-collision umbrella** (from the Epic C review) carries over: per-blueprint umbrella goal with child harvest/haul goals, so multi-material and concurrent structures don't clobber each other's goals.
- **Carry-bounded harvest demand.** Construction harvest demand is bounded to `min(remaining, colonist carry capacity)`, so a structure needing more than one inventory stack of material (e.g. a 156 m² foundation at 313 Wood) builds over multiple harvest→deliver trips instead of the colonist hoarding at the stack cap and never delivering. Found during wall sandbox testing; fixes foundations and walls alike.
- **Demolition is immediate per segment** for this slice, matching the foundation precedent; the work-driven Deconstruct action exists (Epic C) but routing demolish through it with material refund and the host-can't-demolish-while-walls-stand guard is deferred polish.

## Related Documentation

- [Building & Construction Architecture](../../technical/building-construction-architecture.md) (D4 data model, D5 nav contract, D8 wall rendering)
- [Building & Construction design spec](../../design/game-systems/world/building-construction.md) (Walls)
- [Construction Epic C dev log](./2026-06-13-construction-epic-c.md)

## Next Steps

- **D5 obstacle/portal publication** to navigation is deferred until the nav epic lands (no consumer before then); the band footprints `resolveWallBands` already computes are what built segments will publish, carrying stable segment ids for belief filtering.
- Epic E (rooms) — face extraction over built wall centerlines via the `libs/geometry` arrangement/half-edge core, on built/demolish events.
- Epics F (openings) and G (editing). Navigation remains a separate epic and gates walls blocking movement.
