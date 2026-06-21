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

## Feedback round: realism fixes

Playtest feedback on the first feeder pass drove a second set of changes, all in
`RiverNetwork2D.cpp` plus a small engine/shader depth path:

- **Feeders no longer cross the channel.** They were rotated off the upstream
  axis by 25-65 deg (a shallow angle, near-parallel to the parent) and started on
  the centerline, so they ran alongside or across the river. Now each leaves the
  bank near-perpendicular, tilted 18-42 deg toward downstream (a natural acute
  confluence), and anchors a little *inside* the chosen bank (`kFeederBankInset`),
  so it cannot reach the far side.
- **No more broken, sub-tile water.** Tiles are 1 m, so a channel under ~1.5 m
  rasterized to a dotted line. A render contiguity floor (`kRenderMinHalf` 0.8 m
  half-width) holds every emitted channel/feeder at a 4-connected ribbon;
  thinness now reads through depth, not a sub-tile width.
- **Width varies more, with pools.** `widthVariation()` adds riffles plus
  cubed one-sided pool crests along both channels and feeders.
- **Headwater springs.** A river source now emits 2-3 trickles fanning to
  alternating banks (`kHeadwaterFeeder{Min,Max}`), bypassing the along-channel
  width gate, so the head reads as a convergence rather than an abrupt start.
- **Water depth.** `Chunk::computeTile` maps channel width -> a depth byte stored
  in `TileData::attributes`, carried in `TileRenderData::waterDepth` (texel `a`);
  `tile.frag` tints water from pale-shallow to deep-blue by it. Streams render
  shallow, trunks deeper, lakes/oceans deepest. Range tuned for the small rivers
  at gameplay scale (fully deep by ~14 m).

New tests: `HeadwaterSproutsConvergingSprings` (springs enter from both banks),
plus `FeedersTaperToTrickle` updated to assert the contiguity floor.

## Related Documentation

- `docs/development-log/entries/2026-06-21-2d-rivers-and-water-landing.md` — the
  base 2D-river synthesis this extends.

## Next Steps

- Feeders are proportional to the parent river, so they show best on substantial
  rivers. The quickstart lands by a lake with only thin, low-flow nearby rivers,
  where feeders are correctly subtle. A landing bias toward *high-flow* river tiles
  (biggest nearby river) would put the colony on a river where the meander, width
  growth, and springs are all visible — the clearest next improvement.
