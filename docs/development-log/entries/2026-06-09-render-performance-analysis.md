# 2D Render Performance Analysis + Profiling Tooling

**Date:** 2026-06-09

## Summary

Profiled the live game scene end-to-end (scrolling, chunk loading, zoom levels) on Windows
RelWithDebInfo, identified the three root causes of current performance problems, and wrote
two plans: a performance overhaul and a follow-on "living environment" rendering plan (wind,
trampling, water). Added the debug-server tooling needed to do this from scripts.

## Measurements

Captured with the new `scripts/perf-capture.ps1` sweep (vsync off, results archived in
`perf-results/capture-2026-06-09-223132.json`):

- Idle at default zoom 3.0: 63 FPS, 9.4k tiles, 10.8k entities drawn.
- Zoom-out collapse: 29 FPS at 0.75x, 13 FPS at 0.5x, 4 FPS at 0.25x (1.1M tile quads
  rebuilt on CPU + 6.4M mostly sub-pixel entity triangles).
- Scrolling: smooth on average but single-frame hitches of 112ms (zoom 3) to 443ms
  (zoom 0.75) exactly when a new chunk first becomes visible - the synchronous
  `buildBakedChunkMesh` on the render thread.

## Root causes (details in plan)

1. Tile quads (4 x 96B vertices each) are rebuilt on CPU and fully re-uploaded every frame;
   tile LOD (`m_tileResolution`) exists but is hardcoded to 1.
2. Baked flora renders full per-blade geometry at every zoom; no impostor handoff.
3. First-visible-render entity mesh bake is synchronous on the render thread.

## Tooling added (this branch)

- `/api/control?action=camera&x=&y=&zoom=&panx=&pany=` - remote camera control (position,
  zoom, held-key style panning) consumed by GameScene each frame.
- `/api/control?action=vsync&value=0|1` - runtime vsync toggle for unthrottled measurement.
- Draw call/triangle metrics now include EntityRenderer's raw GL draws (previously the
  metrics only counted BatchRenderer flushes, reporting 3 draw calls).
- `scripts/perf-capture.ps1` - scripted zoom/scroll sweep that records `/api/metrics` to
  `perf-results/`.

## Known-broken metrics (documented, not yet fixed)

- `gpuRenderMs` is unusable (352ms readings; query window also misses the uber-batch flush).
- `memoryUsedBytes`/`cpuUsagePercent` read 0 on Windows (SystemResources not ported).
- Light-load frames pin at ~63 FPS with vsync off (suspected DWM/present pacing) while
  mid-load runs at 119 FPS; needs investigation.

## Related Documentation

- `.claude/plans/render-performance-overhaul.md` - fixes for the above, with phase gates
- `.claude/plans/living-environment-rendering.md` - wind/trampling/water/footsteps plan
- `docs/debug-swapbuffers-performance.md` - Dec 2025 session this builds on
- `docs/technical/vector-graphics/animation-performance.md` - CPU retessellation NO-GO

## Next Steps

Execute render-performance-overhaul Phase 1 (persistent tile geometry).
