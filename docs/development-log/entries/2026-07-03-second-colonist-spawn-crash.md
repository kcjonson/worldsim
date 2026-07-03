# Second-colonist spawn crash: Memory LRU dangled across ECS pool relocation

**Date:** 2026-07-03
**Bug:** Specboard 805e17fb (spawning a second colonist hard-crashes the game)

## Summary

Spawning any colonist beyond the first crashed the game within a frame, 100%
reproducible. Root cause was nowhere near the suspects (renderer, colonist SVG
loading): the `Memory` component's LRU stored `std::list` iterators in a map,
and the ECS `ComponentPool` relocates components by value when its dense
vector grows — which is exactly what adding a second colonist's Memory does.
On MSVC the component's implicit move is not noexcept (it holds
`std::unordered_map`), so vector growth falls back to copying; the copied
`lruMap` iterators still pointed into the original component's list nodes,
which died with the old buffer. The first colonist's next vision tick called
`touchLRU`, erased a freed node, and corrupted the heap.

## Diagnosis

The crash log ended right after the `Colonist_up` SVG template load, which
pointed everyone at the asset/render path — a red herring caused by stdout
pipe buffering losing the trailing lines. A headless unit test of the full
directional template + motion pipeline passed cleanly. The definitive answer
came from running the game under cdb (WinDbg's, installed via winget) with the
dev-API repro: break-on-heap-corruption inside
`std::list::erase` ← `Memory::touchLRU` ← `Memory::rememberWorldEntity` ←
`VisionSystem::update`.

## Fix

- `libs/engine/ecs/components/Memory.h` — replaced the `std::list<uint64_t>`
  + iterator-map LRU with an index-linked LRU in flat vectors (node pool with
  free-slot recycling, head/tail indices, key→index map). Indices stay
  meaningful in a deep copy, so the component now survives any relocation
  strategy the pool's vector picks — copy or move. Also drops the per-node
  heap allocation.
- `libs/engine/ecs/ComponentPool.h` — documented the storage contract:
  components relocate (and may be copied) on dense-vector growth, so they must
  never hold pointers/iterators into their own members.
- `libs/engine/ecs/components/Memory.test.cpp` (new) — regression tests: the
  relocation/copy shape that crashed, copy independence, LRU eviction order
  respecting touches, and clear() resetting the LRU.
- `libs/engine/assets/ColonistDirectionalAssets.test.cpp` (new) — written
  while chasing the red herring but kept: loads template + motion for all four
  colonist directions headlessly, guarding the lazy directional-asset path.

## Verification

- Original repro: spawned 2nd and 3rd colonists via `/api/dev/colonist`; game
  survived minutes of wandering/foraging (vision + memory churn on all three),
  directional sprites render and animate (screenshot captured).
- engine-tests 937/937, renderer-tests 105/105 (RelWithDebInfo).

## Related Documentation

- /docs/design/game-systems/colonists/memory.md (design; LRU internals changed,
  behavior unchanged)
