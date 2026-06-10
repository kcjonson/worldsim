# Render Performance Overhaul (Phases 1-3)

**Date:** 2026-06-10

## Summary

Executed the first three phases of the render performance plan plus the Windows
frame pacing fix. Result, measured with `scripts/perf-capture.ps1` on
RelWithDebInfo with vsync off:

| Scenario | Before (session start) | After |
|---|---|---|
| Idle zoom 3 | 63 FPS (pacing-capped) | 120 FPS, p99 8.3ms |
| Idle zoom 0.25 | 4.1 FPS, 248ms frame | 120 FPS, p99 8.4ms |
| Scroll zoom 3 | p99 112ms | p99 16.7ms |
| Scroll zoom 0.75 | p99 443ms | p99 11.0ms |
| 4x grass density, idle, any zoom | n/a | 120 FPS |

The frame pacer's 120 FPS cap is now the limiter everywhere.

## What changed

1. **Tiles render from per-chunk data textures** (Phase 1). One quad per
   visible chunk; the fragment shader (`tile.vert`/`tile.frag`) fetches
   surface, masks, and all 8 neighbor IDs from an RGBA32UI texture mirroring
   `TileRenderData` byte-for-byte. Replaced the per-frame CPU rebuild of up to
   1.1M tile quads (96B/vertex, full re-upload every frame). The old drawTile
   path, the uber-shader tile branch, and the tile-only data3 vertex attribute
   were deleted (UberVertex 96 -> 80 bytes).
2. **Windows frame pacing fix.** The 120 FPS cap slept with `sleep_for`, which
   rounds up to the ~15.6ms OS timer tick on Windows: light frames were
   silently capped at ~60 FPS with 25ms spikes. Fixed with `timeBeginPeriod(1)`
   plus sleep-then-spin (2ms spin window).
3. **Chunk generation moved to workers** (Phase 2). `ChunkManager::loadChunk`
   generated 262k tiles inline on the main thread; crossing a chunk row loaded
   five chunks in one update (~110ms frame). Generation now runs via
   `std::async` (the `isReady()` gate already existed); border adjacency
   stitching happens on completion, one chunk per update, with the 3x3
   neighborhood cached. Chunks mid-generation are never unloaded.
4. **Entity mesh baking moved to placement workers** (Phase 2). The bake
   (per-vertex transform of up to ~90k flora per chunk) runs as a continuation
   of the async placement task in `AsyncChunkProcessor`; CPU bake code lives in
   `BakedEntityMesh.cpp`, shared with the synchronous re-bake path for
   LRU-evicted chunks. `AssetRegistry::getTemplate` got a mutex (workers
   tessellate templates lazily). GPU uploads are budgeted at 2MB/frame.
5. **Far-zoom impostor handoff** (Phase 3). Baked meshes split into height
   buckets; short flora (<1m) fades out below ~3px on-screen height since the
   grass tile texture carries the appearance. Tall flora always draws. At
   0.25x zoom: 344k drawn entities -> 3.6k, 5.0M triangles -> 287k. Far-zoom
   cost is now independent of grass density (validated at 4x clump density).
6. Stale tile-texture re-uploads capped at 1/frame; `captureChunkData`
   snapshots Surface enums instead of building 262k strings per chunk launch.

## Files

New: `libs/renderer/shaders/tile.vert`, `tile.frag`,
`libs/engine/world/rendering/BakedEntityMesh.h/.cpp`.
Rewritten: `ChunkRenderer.h/.cpp`. Modified: `EntityRenderer`,
`AsyncChunkProcessor`, `ChunkManager`, `Chunk`, `AssetRegistry`,
`BatchRenderer`, `Primitives`, `uber.vert/.frag`, `Application` (pacing),
`GameScene`, `GameLoadingScene`.

## Known follow-ups

- 4x-density scrolling shows p99 ~64ms single-frame hitches (avg stays 120
  FPS); needs per-frame attribution tooling (Phase 4 max-breakdown metrics).
- gpuRenderMs metric still broken; SystemResources still returns 0 on Windows.
- Tile invalidation on future terrain edits: bump `Chunk::renderDataVersion`
  (full 4MB re-upload per edit; consider dirty-rect upload when edits land).

## Related Documentation

- `.claude/plans/render-performance-overhaul.md` (plan; phases 1-3 complete)
- `docs/development-log/entries/2026-06-09-render-performance-analysis.md`
- Baselines in `perf-results/capture-2026-06-10-*.json`
