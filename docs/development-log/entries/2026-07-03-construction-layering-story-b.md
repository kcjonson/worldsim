# Construction layering (Story B): ground sub-layer + world/UI flush split

**Date:** 2026-07-03
**Epic:** Handle Visual Layering in the game world (Specboard)
**Spec:** /docs/technical/rendering/world-depth-sorting.md

## Summary

Fixed the two remaining layering bugs from the visual-layering epic: committed
construction (foundations and walls, blueprint and built) painted over the game
UI, and always painted over world entities, so a tree in front of a building
disappeared under the floor. Committed construction now draws in the spec's
ground sub-layer (above terrain, below groundcover and every Y-sorted upright),
and a flush barrier before the UI pass makes it structurally impossible for any
world primitive to sort above the UI.

## Root cause

`BatchRenderer::flush()` stable-sorts draw groups by zIndex, and everything
submitted after the entity pass's last interleaved flush shared one endFrame
flush: committed construction (z 50-64), drawing previews (z 899-910), world
overlays, and the entire game UI (panels 0, context menus 400, dialogs 500,
dropdown menus 1000). Within that single sort domain the construction z values
beat every plain panel, and previews beat even dialogs. Draw-call order in
`GameScene::render` was irrelevant because the sort ran across the whole batch.

## Changes

- `libs/renderer/primitives/Primitives.{h,cpp}` — new `Primitives::flush()`, a
  draw-order barrier: groups only z-sort against each other within one flush.
- `DrawingSystem::render` split into `renderCommitted` (foundations, walls,
  openings) and `renderPreview` (rubber-band, snap guides, opening ghost).
- `GameScene::render` — `renderCommitted` + flush now runs between the terrain
  and entity passes (the ground sub-layer); `renderPreview` stays after the
  entities with the placement ghost; a flush before `gameUI->render()` splits
  the world and UI z domains.

Mechanism decision (task B1): neither ECS mirror entities nor a per-frame
PlacedEntity adapter. Walls were already cut from the Y-sorted stream (angled
walls need per-segment anchoring), so nothing in committed construction needs
an anchorY; pass order alone produces the spec's layer model, and the ECS
render representation would be throwaway once C6's baked element-emitter
replaces this interim path. Consequence: construction is always behind
entities, so a colonist never hides behind a wall — accepted until walls get
per-segment anchoring.

## Verification

In-engine (RelWithDebInfo, dev API): built an 18x12 m foundation with a closed
wall loop and a blueprint foundation among TemperateDeciduousForest trees.
Trees north and south of the building draw over the floor, the wall bands, and
the blueprint fill; the top bar, storage/tasks buttons, and toasts draw over
construction everywhere they overlap. Same scene on the pre-change main binary
shows every tree buried under the foundation fill. Colonists render in the same
Y-sorted pass as the trees, downstream of the same flush, so colonist-over-
foundation follows from the tree result.

Found while verifying, pre-existing on main (f2f43f8): spawning a second
colonist (`/api/dev/colonist`) hard-crashes the game right after the
`Colonist_up` SVG template load. Filed as a Specboard bug; not related to this
change.

## Performance (measured)

Controlled A/B, RelWithDebInfo, identical protocol per binary (fresh launch,
dev-API stamp of a 10x10 grid = 90 accepted buildings / 360 wall segments,
90 s settle, paused sim, n=24 samples at a fixed camera, vsync off; note the
app has a hard 120 fps pacing cap):

- Frame time: 8.32 ms avg on both (cap-bound); fps 120 locked on both. No
  user-visible regression.
- Draw calls: 391 -> 392. The entire committed pass adds ONE draw call; flush
  count is per-frame constant, it does not scale with building count.
- Scene-render CPU (includes all flushes; endFrame runs inside this timer):
  before median 2.19 ms, after median 3.11 ms at 90 buildings. A real
  +0.9 ms: the committed batch is now uploaded/drawn mid-frame in its own
  flush instead of merged into endFrame, and entityRenderMs rises 0.75 ->
  1.44 ms despite untouched entity-pass code, pointing at driver-side buffer
  sync on the shared batch VBO (the same-VBO in-frame reuse hazard already
  flagged in the Story A review, tracked under Story C).
- Scale math: committed-construction render costs ~24 us/building/frame after
  (~14 before), linear, no visibility culling. A ~300-building city would
  break the 8.3 ms budget on the after build (~450 on before, but rendered
  wrongly). The interim per-frame CPU re-triangulation is the bottleneck in
  both builds; C6's baked element-emitter is the real fix, and Story C now has
  a measured baseline.

## Related Documentation

- /docs/technical/rendering/world-depth-sorting.md (status + implementation
  decision updated)

## Next Steps

- Story C perf items (per-species VBO retention) and the dead
  BatchedEntityRenderer fallback decision remain on the epic.
- C6 replaces the interim DrawingSystem committed-construction rendering with
  the baked element-emitter.
