# 2026-06-21 - River tributaries: trickle heads + procedural springs

## Summary

Follow-up to the 2D-rivers work: give rivers a dendritic look — a trickle at the
head fed by many short springs and streams that follow the terrain slope and cross
chunk boundaries — without drawing absurd 50 km-long "streams."

The key constraint: at the quickstart resolution a 3D drainage tile is ~50 km, so
every edge of the coarse `downhill` tree is ~50 km long. Rivers and their main
tributaries legitimately run that far (and the existing top-4% `kFlagRiver` cut
already draws them), but small streams are hundreds of m. So rivers stay sourced
from the real 3D graph, and sub-river streams/springs are **procedural and short**.

## Details

All in `libs/world/worldgen/sampling/RiverNetwork2D.{h,cpp}`:

- **Trickle heads.** Lowered the channel minimum width to a sub-metre trickle
  (`kMinWidth` 1.5 -> 0.6 m) so headwaters and springs read as a thread of water.
  Added `isHeadwaterRiverTile` (a river tile with no upstream river neighbour) to
  anchor extra springs at a river's source.
- **Procedural feeders.** Off every rendered channel wider than ~3 m, spawn short
  feeders at hashed intervals along the parent (and forced at headwaters). Each runs
  from a spring point down to a confluence, oriented uphill — opposite the parent's
  flow, which is the 3D-derived slope — rotated to a bank by a hashed angle, capped
  to ~120-900 m, tapering from a fraction of the parent width down to a trickle, with
  a tapered meander. Fully determined by `hash(seed, parentTileA, parentTileB,
  index)`, so any chunk that gathers the parent emits identical feeders (seamless
  across chunk seams); the collect/cull reach is extended by the max feeder length.
- **Perf.** `ChunkSampleResult::isRiverAt` got an AABB reject before the
  distance/sqrt so the extra feeder segments stay affordable per tile.

Tests (`RiverNetwork2D.test.cpp`): wide-river-grows-feeders vs thin-river,
feeders-taper-to-trickle, and local/cross-seam gather consistency, plus the updated
width-clamp test. world fast + engine tests green.

## Related Documentation

- `docs/development-log/entries/2026-06-21-2d-rivers-and-water-landing.md` — the
  base 2D-river synthesis this extends.

## Next Steps

- Feeders are proportional to the parent river, so they show best on substantial
  rivers. The quickstart lands by a lake with only thin, low-flow nearby rivers,
  where feeders are correctly subtle. A landing bias toward *high-flow* river tiles
  (biggest nearby river) would put the colony on a river where the meander, width
  growth, and springs are all visible — the clearest next improvement.
