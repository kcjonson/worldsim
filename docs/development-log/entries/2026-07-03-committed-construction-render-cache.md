# Committed construction render: version-keyed geometry cache + viewport culling

**Date:** 2026-07-03
**Epic:** Handle Visual Layering in the game world (Specboard) — city-scale
follow-up to Story B
**Spec:** /docs/technical/rendering/world-depth-sorting.md

## Summary

The interim committed-construction renderer re-ran resolveWallBands over the
whole wall graph and re-triangulated every foundation ring, wall band, and
opening footprint on the CPU every frame, with zero visibility culling — cost
linear in the total built world (~24 us per building per frame measured on
Story B; a ~300-building city would blow the 8.3 ms frame budget). Committed
geometry is now cached in world space against ConstructionWorld::version()
(which every commit/edit/demolish/state/entity mutation already bumps) and
culled per item to the viewport, so steady-state per-frame cost scales with
what is visible, not with how much has ever been built.

## Changes

- `CommittedGeometryCache.{h,cpp}` (new, app construction folder) — world-space
  geometry + state-derived styling per foundation (dequantized ring + fan
  indices), wall band (trimmed band or opening-gap sub-bands), junction
  polygon, and opening footprint, each with a world AABB; also the
  resolveWallBands reject fallback (bare centerlines). Rebuilt only when the
  topology version moves. Material palette colors resolve at rebuild; build
  progress stays a per-frame read off the cached ECS entity handle.
- `DrawingSystem` — renderCommitted refreshes the cache, expands the camera's
  visible rect by a 2 m margin, and draws only intersecting items; the
  geometry helpers (segmentLengthMm, openingIntervalsForSegment,
  footprintWidthMeters, material color resolution) moved into the cache.
  Styling (progress alpha ramps, blueprint-vs-built outlines, junction tint)
  unchanged.

## Performance (measured, same protocol as the Story B numbers)

90 buildings / 360 wall segments, RelWithDebInfo, paused sim, n=24, 120 fps
pacing cap in place:

- City on-screen, scene-render median: 3.11 ms (Story B) -> **1.93 ms**, also
  below main's 2.19 ms (which drew the city over trees and UI).
- City off-screen: **0.71 ms** scene render, draw calls 392 -> 280 — committed
  construction contributes only ~600 AABB tests when out of view. The old
  path's cost was position-independent (no culling).
- Rebuild cost (resolveWallBands + retriangulation) is paid once per
  construction change instead of every frame.

Remaining Story C item: entityRenderMs stays elevated (~1.1 vs main's ~0.75)
with a visible city — the mid-frame batch-VBO reuse sync flagged on PR #251.
C6's baked element-emitter remains the long-term replacement for this path.

## Verification

- Visual parity in-engine: built foundation + wall loop + blueprint + door +
  window; bands, junction corners, opening gap sub-bands with door leaf and
  window glass all render as before (screenshots). Cache invalidation proven
  by the scene assembling across frames (each dev commit bumps the version
  and the next frame shows it).
- world-sim builds clean; no engine/renderer library changes (app-level only).

## Related Documentation

- /docs/technical/rendering/world-depth-sorting.md
