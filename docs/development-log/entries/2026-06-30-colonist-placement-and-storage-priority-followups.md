# 2026-06-30 - Colonist follow-ups: furniture placement, drops, and storage priority

## Summary

Follow-up work surfaced by the colonist task-arbitration epic (see
`2026-06-29-colonist-task-arbitration.md`). Two PRs, both stacked on the arbitration branch:
packaged-furniture placement + drop-routing + re-package (PR #243), and storage priority + the
no-magic-discovery feedback (PR #244). The arbitration acceptance test had flagged a crafted box
landing on the crafting station and a colonist stalling on undiscovered sources; these close those
out (the latter as feedback, not autonomy).

## Details

### PR #243 - placement, drops, re-package

- **`findValidPositionNear`** added to `NavigationSystem`: the nearest valid walkable nav point at
  least `minDist` from an origin (region gate, deterministic 32-sample ring probe, nearest-pathable
  fallback). The one reusable drop/place primitive.
- **Crafted furniture comes out packaged** and installs a short distance from the station via
  `findValidPositionNear`, instead of being seated in the colonist's hands or dropped overlapping
  the station. Keyed on `ItemCategory::Furniture` so two-hand resources like Wood stay carryable.
- **Colonist drops route through `findValidPositionNear`**: a felled tree's uncarried remainder,
  craft overflow, and a cancelled build-site's salvage all snap to walkable ground, so a drop whose
  raw origin sits in a river ends up on land. Dev-spawn scatter validates each scattered position.
- **Move command** on an installed box: re-adds the `Packaged` component and reuses the existing
  `beginRelocation` pipeline, so the box can be relocated with its contents intact (the dead
  `onPackage` callback was simply never wired).

### PR #244 - storage priority + no-magic-discovery feedback

- **Storage priority** (Phase 2 of `storage-system.md`, previously an inert field): a higher-priority
  box pulls an item from strictly-lower-priority storage. The strict-`<` source gate makes items
  flow monotonically up the Low/Medium/High/Critical ladder, so A->B->A cycles are structurally
  impossible without a reservation system. Reuses the umbrella-Haul goal + deposit leg; the only new
  action is a `Withdraw`-from-container pickup. Clamped by the source's own min and the destination's
  max. Storage priority orders stocking within tier 6 via a score bias, never preempting a need or a
  work order.
- **Capability-mask fix**: the capability bitmask was `uint8_t` sized for 7 capabilities, so Storage
  (bit 8) and Craftable (bit 7) overflowed and were never recorded in colonist Memory. Widened to
  `uint16_t`/9. Without it, colonists could never know a storage box, so the discovery-gated pull
  never fired.
- **Known-source affordance + unknown-source toast**: the storage config dialog shows green
  "Source known [OK]" / red "No known source [X]" per item rule (reusing crafting's exact predicate,
  refactored into shared memory-source + priority-filtered inventory-source helpers; crafting is
  unchanged), and a de-duped toast when a configured item has no known source.
- **Two-hand-carry continuity fix**: a colonist carrying a two-hand armful for an in-flight pull holds
  the delivery against any same-or-lower-priority challenger (a competing pull, idle wander) so the
  load completes its deposit; a higher-priority need or work order still preempts.

## Key decisions

- **No magic discovery is a core mechanic, for every fetch in every job.** A colonist only fetches
  from a source the colony has seen; when it can't, the game surfaces the unknown-source state and
  the player takes direct control. The fix is feedback, never auto-pathing to unseen resources.
- **A box keeps its contents when moved** (the Inventory rides with the entity), rather than dumping.
- **Storage priority orders within the opportunistic tier**, not as a tier of its own, so a Critical
  box's restock can outrank a Low box's but can never preempt eating or building.

## Verification

893 engine-tests green. In-game (RelWithDebInfo): the storage pull fills a High box to its minimum by
pulling two-hand Wood from a Low box across multiple trips with nothing stranded; two same-priority
boxes never shuffle; a tree felled at a river bank drops its wood on land, not in the water; the box
installs beside the station and shows a Move button; the affordance shows red/[X] + toast for an
unsourced item and green/[OK] when a source is known.

## Related Documentation

- `docs/technical/colonist-task-arbitration.md` - the arbitration epic these follow up
- `docs/design/features/storage-system.md` - storage priority (Phase 2, now implemented)
- PRs #242 (arbitration), #243 (placement/drops/re-package), #244 (storage priority)

## Next Steps

- Merge order: #242, then #243 and #244 (siblings off the arbitration branch; both touch
  `ActionSystem`, so the second to merge needs a small reconcile).
- Deferred: full reservation wiring for the pull (live-quantity clamping suffices for now); a
  proactive Priority-Changed redistribution event (the per-tick re-scan covers it implicitly);
  carry-weight of a moved box's contents (only the box itself is the two-hand carry today).
