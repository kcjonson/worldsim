# 2026-06-14 - Construction Epic E: rooms

## Summary

Room detection for the Building & Construction system. When a closed loop of built walls forms, the enclosed region becomes a room: a room ECS entity is spawned, its identity (id + name) persists across edits, and a "Room formed" toast fires. This is the detection-and-notification slice the design spec scopes for Epic E; the rooms overlay UI (tint, labels, clicking, info panel) and room functions/types ship later with the broader overlay system.

## Details

### What was built

- **`RoomDetection` (pure, `libs/engine/construction/RoomDetection.{h,cpp}`)** — gathers BUILT wall centerlines into `geometry::InputSegment`s (tagged with the wall `SegmentId`), runs `buildArrangement` + `extractFaces`, and returns each bounded face as a `RoomFace{area, representativePoint, ring, boundingSegmentIds}`. Geometry-only, no ECS. Openings don't split a centerline, so a doored wall still encloses (no special handling). Reuses the half-edge face-extraction core from Epic A unchanged.
- **`RoomDetectionSystem` (`libs/engine/ecs/systems/RoomDetectionSystem.{h,cpp}`, priority 59)** — each tick cheaply polls `ConstructionWorld::version()`; when topology changed it re-detects and reconciles against its persistent room records. New faces are matched to existing rooms by representative-point containment (exact integer `pointInPolygon`) so names survive edits; genuinely new faces get a stable `RoomId` + "Room N" name and spawn a room entity (`Position` + `Structure{Room}` + `Room`); faces that vanished have their entities destroyed. A newly formed room fires a `RoomFormedCallback` — the cross-layer seam so the engine lib never touches UI.
- **`Room` component (`libs/engine/ecs/components/Room.h`)** — POD: `roomId`, `area` (m²), `boundingSegmentIds`, `name`.
- **GameScene wiring** — registers the system, injects the `ConstructionWorld`, and wires the room-formed callback to `gameUI->pushNotification("Room formed", ...)`, mirroring the existing `setStructureCompletedCallback` seam.
- **`/api/dev/walls` dev command (`DrawingSystem::devCommitWalls`)** — stamps a wall chain straight into the topology (bypassing the draw tool and soft validator, like `/api/dev/foundation`); `close=1` encloses, `built=1` flips to Built, `host=0` is freestanding. Makes rooms testable in one curl; reused for sandbox verification and useful for Epics F/G.

### Technical decisions

- **Version-poll over event hooks.** A single `version()` check in the system catches build *and* demolish (both bump the counter) without wiring into each completion/deconstruct callback — simpler and matches the "consumers cache against the version and rebuild when it moves" contract.
- **Representative-point identity matching.** Exact-integer, deterministic (no float in the match decision, no unordered iteration), and gives the right behavior for the common edits: splitting a room keeps the name on the rep-containing half; merging keeps the dominant room's name. A divider drawn exactly through a stored rep reports `OnBoundary` (not `Inside`) for both halves, so a second fallback pass assigns it to one side deterministically rather than resetting both.
- **Rooms as entities.** Matches the architecture data model (`RoomId → {face, area, entity}`) and gives the deferred overlay a clean `view<Room>()` query surface; the system owns the create/refresh/retire lifecycle.

### Adversarial review

A 5-dimension review (determinism, identity correctness, lifecycle/leaks, dev-api/geometry, standards) with each finding independently verified surfaced 5 real issues, all addressed: the OnBoundary identity fallback; `devCommitWalls` force-building only the chain segments (not split halves of pre-existing blueprint walls it T-splits); a stale comment; a dead test variable; and the nested-room limitation below.

### Known limitation (deferred)

A room fully nested inside another with no connecting wall (a loop inside a loop) is mislabeled, because `extractFaces` represents a face-with-a-hole by its outer cycle only (the inner loop is a separate component), so the enclosing face's ring/area/rep don't account for the inner room. Correct nested-room identity needs hole-aware face extraction; deferred until the overlay actually consumes room identity/area. Pinned by a test so the eventual fix is a visible, intentional change.

### Files

- New: `libs/engine/ecs/components/Room.h`, `libs/engine/construction/RoomDetection.{h,cpp,test.cpp}`, `libs/engine/ecs/systems/RoomDetectionSystem.{h,cpp,test.cpp}`
- Modified: `libs/engine/CMakeLists.txt` (two new sources), `apps/world-sim/scenes/game/GameScene.cpp` (system registration, room-formed toast, `/api/dev/walls`), `apps/world-sim/scenes/game/world/construction/DrawingSystem.{h,cpp}` (`devCommitWalls`)
- Tests: 532 engine tests green (12 room tests across the two suites)

## Related Documentation

- Design: [`docs/design/game-systems/world/building-construction.md`](../../design/game-systems/world/building-construction.md) (Rooms section)
- Architecture: [`docs/technical/building-construction-architecture.md`](../../technical/building-construction-architecture.md) (D6: room detection)

## Next Steps

Epic F (openings): OpeningTool, parameterized door/window assets, wall-blueprint reservation + retrofit cut, portal publication. Then Epic G (editing & polish). The rooms overlay and hole-aware nested-room support ship with the overlay system.
