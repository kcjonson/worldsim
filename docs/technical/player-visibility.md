# Player visibility

**Status:** Spec, not built. Rules and design points settled with Kevin on 2026-11-13; waiting on his go-ahead to build.
**Created:** 2026-11-13
**Epic:** "Player visibility: unexplored areas (visible, seen, unseen)" on Specboard
**Companions:** [Vision Architecture](./vision-architecture.md) (where sight shapes come from), [Nav Mesh](./nav-mesh-variable-resolution.md) (the meshed area the seen area sits inside), [Memory System](../design/game-systems/colonists/memory.md) (what colonists know, a different thing from what the player sees)

The player sees only what colonists have seen. Ground no colonist has seen is black, ground seen before is darkened, and ground in a colonist's sight now is drawn as the world is drawn today. This document holds the rules, the record behind them, how the cover is drawn, and what the player can click.

Terms: **player visibility** for the feature, **unexplored area** for ground no colonist has seen. The three areas are **unseen**, **seen**, and **visible**. Visible ground is also seen. The nav mesh's extent is the **meshed area**, never "seen".

---

## 1. Rules

Each rule is Kevin's, with the date it was given. A later rule replaces an earlier one on the same point.

1. Three areas. Unseen is totally dark. Seen (before, not now) is darkened, with the things on it shown and selectable. Visible (in sight now) is brightest. (Epic, 2026-11-11.)
2. Sight is a radius, not a cone. Exploration happens as colonists move. (Epic.)
3. Only colonists reveal. The camera never does, and panning over unexplored ground shows nothing. (2026-11-11.)
4. Two classes of thing: those that persist in seen areas (static things, such as placed flora) and those shown only while visible (things that move, such as deer). (Epic.)
5. Building walls block sight. Most things don't; a colonist sees through trees. (Epic.)
6. No move order can target unseen ground, and all seen ground is in the mesh. (2026-11-12.)
7. The player places and draws only where they can see. (2026-11-12.)
8. The landing is seen from the start, 150 m round the drop: the crew saw it as they came down. (2026-11-12.)
9. "Seen" is a record of colonists' own sight, kept apart from the nav mesh. The mesh reaches 16 m past sight, holds ground other movers meshed, and ignores walls, so it is not what anyone saw. (2026-11-13.)
10. The seen record is an exact polygon union. All data is vector; there is no raster, grid, bitmap, or mask texture anywhere in this system. (2026-11-13.)
11. Edges are hard vector edges in the first build. Soft edges and pixel effects wait for a later style pass. (2026-11-13.)
12. "Where the player can see" means seen, for move orders and for placing and drawing alike. (2026-11-13.)
13. A seen static thing shows as it was last seen, not as it is. That is the rule; the first build draws live state, and the last-seen record is built with the first thing that can change while nobody watches (3.6). (2026-11-13.)
14. The camera pans freely. It is not held to the seen area. (2026-11-13.)
15. A move order onto unseen ground sends the colonist to the edge of seen ground on the way to the click. (2026-11-13.)
16. The seen area never shrinks. It is the colony's record, not one colonist's, and a colonist's death takes nothing from it. (2026-11-13.)
17. Every colonist of the landing crew knows what is in the landing area, the same as if they had seen it. (2026-11-13.)
18. A dev toggle turns player visibility off. Perf captures and tests that look away from colonists run with it off. (2026-11-13.)
19. The cover is drawn with the stencil buffer, which salal gains for this. (2026-11-13.)
20. The region minimap is not touched here. Showing the seen area on a map belongs to the Minimap epic. (2026-11-13.)

## 2. Current state

What the code does on `main` after the nav stack merged (PRs #369 to #373), read on 2026-11-13.

- **Vision.** `VisionSystem` runs every 5 frames for every entity with `Position` and `Memory`, which today is colonists only. Outdoors (no wall within sight) sight is a circle of `Memory::sightRadius`, 30 m by default, and no shape is built. With walls near it builds a wall-clipped polygon (`geometry::computeVisibilityPolygon`), cached per observer and rebuilt after 0.5 m of travel or a change to the walls. `sightShape(entity)` gives an observer's shape in both cases since step 2: the centre, the radius, the polygon when there is one, and a revision that changes when the polygon is rebuilt. Sleeping colonists observe like any other.
- **No area is recorded.** `Memory` holds remembered things per colonist (placed things, wall segments, openings), capped at 10,000 entries, and skips things with no capability. Nothing records ground, and nothing is colony-wide.
- **The meshed area.** Every mover (an entity with `Position` and `AgentRadius`, today colonists only) adds a 16-sided limit holding the disc of sight plus `kSightBufferMeters` (16 m) to the nav mesh every 4 m of travel. The landing adds one of `kLandingSightMeters` (150 m). The mesh ends at the union's edge, never shrinks, and ignores walls. `NavigationSystem::inSimArea` answers whether a point is on it.
- **Who is who.** There is no faction or owner. A colonist is an entity with the `Colonist` tag. There are no animals or other movers.
- **Drawing.** `GameScene::render` draws terrain (its own shader), construction, then `EntityRenderer` (baked short flora, groundcover, the depth-sorted uprights and ECS things), then overlays, the selection indicator, the move marker, and the placement ghost. Nothing tints or hides by area. salal has one blend mode, no stencil, and no depth test.
- **Input.** Placement, the drawing tools, and move orders already refuse ground off the mesh, because they ask the nav system. A refused move order is silent. Selection asks nothing: a placed thing anywhere under the camera can be selected.
- **Camera.** Unbounded. It loads the chunks round it (3 by 3) to draw them.
- **Saving.** No game save exists. Only the planet is saved.

## 3. Design

### 3.1 The three areas

A point is **visible** when it lies in some player colonist's sight shape now. It is **seen** when it lies in the seen record. Everything else is **unseen**. Visible ground is always seen (3.3 holds that exactly).

Four areas nest, each inside the next, and each containment is held by construction, not by tolerance:

1. what is drawn as visible,
2. the seen record,
3. the ground sight actually swept,
4. the meshed area.

The last containment is what makes rule 6 true: every seen point is on the mesh, so a move order to it can be routed.

### 3.2 Who reveals, and with what shape

An entity reveals when it has the `Colonist` tag and `Memory`. The rule is written by the tag so that an animal given `Memory` later observes for itself and reveals nothing. One colony is assumed; a second player faction is out of scope.

`VisionSystem::sightShape(observer)` is the one accessor for an observer's sight shape (built in step 2, replacing the polygon-only one): the centre, the radius, the wall-clipped polygon when one exists, and a revision that changes when the polygon is rebuilt. `VisionOverlay` uses it and so draws outdoor sight too. Player visibility adds no sight computation of its own: what it calls visible is what vision used.

A sleeping colonist still sees, because vision does not skip one. Whether sleep should close a colonist's eyes is a vision question (section 9).

### 3.3 The seen record

**Home.** `libs/sim/vision/SeenArea.{h,cpp}` is the record. `PlayerVisibilitySystem` (an ECS system that runs each frame, after `VisionSystem`) feeds it and answers the queries. `sim` links neither the renderer nor the ui, and this keeps to that.

**Form.** An exact union of polygons in the geometry library's integer millimetres. The basis is a second, independent `geometry::nav::PatchableMesh` that holds only sight outlines as ground rings: no obstacles, no water, no walls. It already keeps an exact, grow-only union of overlapping rings, with enclosed pockets, separate pieces, and point touches; `locate` answers a point, `groundCovers` answers a whole outline, its live triangles are the union ready to draw, and `epoch()` says when it changed. Where rings overlap, the edges inside the union dissolve and only the union's boundary keeps vertices, so the record's size follows the length of the seen edge and not the area.

That choice is gated on measurement (step 1, section 7). The known weak spot is the ring registry: every ring that ever extended the union is kept, and a covered test or a patch reads every registered ring over the spot. The design below is shaped to register few rings. The mesh also runs nav upkeep the record has no use for (labels, door tags, width lists); step 1 decides between a wrapper that pays for it and an option on `PatchableMesh` that turns it off for an instance.

**Sight polygons.** A sight disc is a regular polygon of `kSightSides` sides inscribed in the circle, its directions from a table made at compile time and pinned by a test, as `geometry::nav::limitPolygon` does for nav. Inscribed, so the polygon never reaches past the disc it stands for.

**Outdoors: legs.** A colonist walking in the open does not stamp a disc per step. He holds a **leg**: the straight segment from where the leg began to where he stands. The ground the leg has seen is its capsule, the hull of the sight polygons at its two ends, one ring of `kSightSides` + 2 vertices however long the leg is. A leg ends, and its ring is committed to the record, when:

- some position he has held since the leg began lies farther than `kSeenSlackMm` from the straight line (he turned), or
- he steps farther than `kSeenJumpMeters` in one frame (a teleport or a forward snap; no capsule bridges the jump), or
- he goes indoors, or is destroyed.

A straight walk of any length is one ring. A ring the record already covers (`groundCovers`) is dropped without being registered, so colonists moving about ground the colony knows add nothing.

**Radii.** Two tunables keep the containments of 3.1 exact while a leg stands in for a path that wobbled by up to the slack:

- a committed capsule has radius sight minus `kSeenSlackMm`, so it lies inside what was truly seen from the path;
- the visible disc is drawn with radius sight minus twice `kSeenSlackMm`, so every disc drawn along the path lies inside the capsule that gets committed.

With 50 mm of slack the drawn disc is 29.9 m for a 30 m sight. Ground never goes from bright to black behind a colonist.

**Indoors: polygons.** With a wall in sight there is no hull to take. Each visibility polygon vision builds is committed as its own ring, covered ones dropped as above. The visible shape drawn is that same cached polygon, so visible and seen agree by identity. The polygon is up to 0.5 m stale, which with hard edges shows as the lit area stepping while a colonist walks past a doorway; it is a known limit of the first build (section 8).

**Pending shapes count as seen.** A colonist's open leg, and any ring waiting its turn, sit in a pending list. Queries and the cover read the record and the pending list together, so the tick a ring is committed on changes nothing the player sees or can click.

**Budget.** At most `kSeenRingsPerTick` rings are tested and committed a tick, first in, first out, on the main thread. It is a count and never a time, so the result does not depend on the machine. If step 1's numbers call for a worker, the pending rule above already makes that invisible.

**The landing.** One inscribed polygon of `kLandingSightMeters` round the drop is committed before the crew exists, from the same constant nav's landing limit uses. Inscribed in the circle that nav's limit circumscribes, it lies inside the meshed area.

**It only grows.** Nothing is ever removed (rule 16). A colonist's death ends his leg and commits it.

**Queries**, all from the main thread:

| Query | Answers |
|---|---|
| `isSeen(point)` | The point is in the record or a pending shape |
| `isVisible(point)` | The point is in a player colonist's sight now, by vision's own test (the true radius or polygon) |
| `covers(outline)` | The whole outline is seen |
| `lastSeenToward(from, to)` | The seen point on the segment nearest `to`; `to` itself when it is seen |
| `epoch()` | Changes when the record does |

### 3.4 Drawing the cover

Three flat fills with hard edges: black on unseen ground, a dark tint on seen ground out of sight, nothing on visible ground. The cover is geometry the GPU fills. Nothing is rendered to a texture and nothing is blurred.

Sight shapes overlap and move every frame, so "seen but not visible" is not a shape worth computing. The stencil buffer does it:

1. Clear the stencil to 0.
2. Draw the record's live triangles and the pending shapes, writing 1, colour off.
3. Draw each player colonist's visible shape as a fan from where he stands (a disc outdoors, the cached polygon indoors), writing 2, colour off.
4. Fill the view where the stencil is 0 with `kUnseenColor`, opaque.
5. Fill the view where the stencil is 1 with black at `kSeenDimAlpha`.

The outdoor disc is drawn at the colonist's position that frame, so it moves smoothly; the record catches up underneath through the pending leg.

**Where in the frame.** In `GameScene::render`, after `EntityRenderer::render` and before the drawing preview. Terrain, construction, flora, groundcover, and every ECS thing are covered in one place, with no change to any world shader. The debug overlays, the selection indicator, the move marker, the placement ghost, and the UI draw after it and are never covered.

**Home.** A cover renderer in `libs/world-view`, which reads the record and the sight shapes from `sim`. The record's triangles are uploaded as a world-space mesh when `epoch()` changes; pending shapes and fans are a handful of triangles a frame.

**Consequences taken on purpose.** A tall thing that stands on seen ground and reaches over unseen ground is cut at the edge. A thing on unseen ground is still drawn underneath and then covered; skipping that work is a later step (section 8).

### 3.5 What salal must gain

salal has no stencil today. The cover needs: stencil bits on the default framebuffer, a clear, a way to draw batched triangles and an uploaded world-space mesh writing a reference value with colour off, a way to draw with a test against a reference value, and off again. The API is salal's to design and lands as a tagged release before the cover's PR is marked ready. Lighting ("Lighting: light emitters occluded like visibility" on Specboard) will want the same facility.

### 3.6 Things

**Shown only while visible.** A thing that moves by itself: an entity with `AgentRadius` and without the `Colonist` tag. `DynamicEntityRenderSystem` skips it unless `isVisible` holds at its position. Player colonists always draw; each stands in his own sight. No such thing exists today, so only a test exercises the filter until the first animal lands.

**Persist once seen.** Everything else: placed flora and rocks, things on the ground, piles, stations, storage, blueprints, foundations, walls, and openings. On seen ground they draw under the tint and can be selected. On unseen ground they are under the black and cannot.

**Last seen, not live (rule 13).** The rule is that a seen thing out of sight shows as it was when last seen, as colonist memory does. The first build shows live state instead, because nothing changes unwatched today except a harvested bush coming back into fruit. The last-seen record is its own task, built with the first thing that can change while nobody looks (animals that eat, another colony that builds, growth). It will need per-area frozen copies that the baked-flora and groundcover renderers cannot give per thing today.

One existing bug touches this: a placed thing felled or harvested out comes back when its chunk unloads and reloads ("Felled placed entities come back when their chunk unloads and reloads" on Specboard), so seen ground the camera returns to can show trees that were cut. It is not this epic's to fix.

### 3.7 Input

| Action | Rule | Where |
|---|---|---|
| Place a thing | Valid only where the nav check passes and the footprint is seen (`covers`) | `PlacementSystem::handleMouseMove`, `PlacementMode::tryPlace` |
| Draw a foundation, wall, or opening | Every point and the whole shape seen; the refusal toast says the area is unexplored | `DrawingSystem`: `pointOnMesh`, `footprintBuildable`, `requirePlaceable` |
| Move order, seen goal | As today | `GameScene` right-click, `AIDecisionSystem::issuePlayerMoveOrder` |
| Move order, unseen goal | The goal becomes `lastSeenToward(colonist, click)` and is then ordered as any seen goal; the move marker shows at the goal ordered | `GameScene` right-click |
| Select | Colonists always. Another mover only while visible. Anything else only where its position is seen | `SelectionSystem::gatherCandidates` |

Ground on the mesh but not seen (the 16 m past sight, a room nobody has looked into) is unseen for every row.

The move order to the seen edge is one order to one point. The colonist stops there, his sight opens the ground beyond, and the player orders again. Carrying on toward the click as ground opens is route planning past meshed ground, "Nav: build a path toward a goal whose ground is not meshed yet" on Specboard, and not part of this. This settles "A move order onto unseen ground does nothing and tells the player nothing".

### 3.8 The landing crew's memory

At landing every colonist of the crew is given what vision would have given him had he looked over the whole landing area: the placed things, ECS things, and shore points within `kLandingSightMeters` of the drop, through the same passes `VisionSystem` runs each tick, run once with that centre and radius. Without it the player sees a berry bush 100 m off, tinted and selectable, that no colonist will walk to unprompted.

The same rules apply as for sight: a thing with no capability is not remembered, and the 10,000-entry cap holds. A dense landing held 5,622 placed obstacles when measured for nav, so the cap is near enough to check (step 7). A colonist who joins later was not in the crash and starts with his own sight, as the memory design says.

### 3.9 Camera and minimap

The camera is not clamped (rule 14). It still loads the chunks round it; over unseen ground they are drawn and covered. The region minimap is unchanged (rule 20). The record can give the Minimap epic its outlines when that epic wants them; no outline walk is built here.

### 3.10 Dev tools and tests

- `/api/dev/reveal?on=1|0`, on the pattern of `freebuild`: with reveal on the cover is not drawn, the mover filter passes everything, and the input rules of 3.7 are off. The record keeps growing underneath, so turning reveal off shows the true state.
- `/api/state?what=visibility`: the reveal flag, the seen area in square metres, rings registered, shapes pending, and with `x` and `y` given, whether that point is seen and whether it is visible.
- Dev verbs that take a position (`spawn`, `colonist`, `teleport`) ignore player visibility, as they ignore the player.
- `scripts/perf` turns reveal on before a capture, so existing views (two of them 70 km and 131 km from any colonist) and their numbers stay comparable. One new capture near the landing with reveal off measures the cover.
- Scenarios 09, 11, and 13 gain a reveal step where they look away from colonists. A new scenario, `14-player-visibility.md`, covers the feature itself: land and see bright ground round the crew and tinted ground out to 150 m, black beyond; fail to place on black ground; order a move into the black and watch the colonist stop at the edge and the ground open; select a tree on tinted ground.
- The dev overlay that prints the biome under the camera still does so over unseen ground. It is a dev overlay and stays.

### 3.11 Saving

There is no game save to put the record in. When one exists the record saves as its registered rings in order, and loading replays them; pending shapes are committed before a save. That work goes with the save, not with this epic.

## 4. Tunables

| Name | Start | What it must cover |
|---|---|---|
| `kSightSides` | 64 | Sides of a sight polygon. At 30 m a 64-gon's flat sits 36 mm inside the circle. More sides add vertices to the seen edge |
| `kSeenSlackMm` | 50 mm | How far a path may wobble from a leg's straight line before the leg ends. Smaller means more rings; larger shrinks the drawn disc (by twice this) |
| `kSeenJumpMeters` | 2 m | A step longer than this in one frame ends the leg without bridging it. Above a frame's travel at the fastest game speed, far below any teleport |
| `kSeenRingsPerTick` | 4 | Rings tested and committed a tick. Set from step 1's numbers |
| `kSeenDimAlpha` | 0.5 | How dark seen ground is |
| `kUnseenColor` | black | The colour of unseen ground |
| `kLandingSightMeters` | 150 m | Shared with nav; one constant for both |

## 5. Prior art

Researched 2026-11-13. Every shipped example found stores the explored record as a raster. That was weighed and set aside (rule 10): this game's data is vector throughout, and the nav mesh already keeps an exact union of the same kind of shape.

| Source | What it does | Record | Out of sight | Link |
|---|---|---|---|---|
| Factorio | Radar and players chart chunks; the chart is black where uncharted | Per chunk (32 by 32 tiles), per force | Last known state, refreshed when covered again | [Radar](https://wiki.factorio.com/Radar), [Map structure](https://wiki.factorio.com/Map_structure) |
| League of Legends | CPU grid upscaled and blurred, applied to the scene | 128 by 128 grid for the map, upscaled 4x | Not stated | [Riot engineering post](https://www.riotgames.com/en/news/story-fog-and-war) |
| UE4 tutorial (2015) | Texture mask with line traces at walls, three levels (0, 100, 255) | 0.5 m texels plus an ever-seen array | Mask only | [Forum tutorial](https://forums.unrealengine.com/t/tutorial-fog-of-war/18222) |
| 0 A.D. (2011 prototype) | One texel per tile, box blur, multiplied over terrain | Tile raster | Not stated | [Forum thread](https://wildfiregames.com/forum/topic/14325-fog-rendering) |
| Red Blob Games | Exact visibility polygon per viewer, per frame; no explored memory | Polygon, not kept | n/a | [2D visibility](https://www.redblobgames.com/articles/visibility/) |
| Dota 2 | Trees are hard sight blockers, not attenuators | Not stated | n/a | [Liquipedia](https://liquipedia.net/dota2/Fog_of_war) |

What carried over: Factorio's last-known-state model is the precedent for rule 13; the UE4 write-up's warning that a coarse mask leaks across thin walls is one more reason sight is clipped exactly before anything is recorded; visibility polygons per viewer are what vision already builds. Nothing was found on camera rules for very large maps or on orders into unexplored ground, so rules 14 and 15 were decided on this game's own terms.

## 6. Performance plan

Measured, not assumed, and under the machine-load rule in [testing](../testing/README.md).

What could move: every view, since the cover adds stencil draws and two fills to the frame; and the sim tick, by the record's upkeep. With reveal on nothing is drawn, so existing captures should come out even with `main`.

To measure:

- **The record (step 1, benches).** Covered-test time against the number of rings over a spot; rings registered and bytes per kilometre walked, straight and wandering; patch time for a leg that extends the union in a seen area several kilometres across; `locate` there; nav upkeep per patch, or the saving from turning it off; indoor polygons from viewpoints 0.5 m apart at a doorway (the nearly parallel edges `PatchableMesh.h` warns of), with `validate()` clean and the result equal to a fresh build; whether `computeVisibilityPolygon`, which builds its circle with float trigonometry, gives the same ring on MSVC, Clang, and GCC.
- **The cover (step 4, capture).** GPU time of the pass near the landing at each zoom with reveal off, with the crew, 10, and 50 colonists; the upload when the record changes.
- **The tick (step 3).** `PlayerVisibilitySystem` main-thread time with 1, 10, 50 colonists walking, at 1x and 10x.

## 7. Build steps

Nothing here starts before Kevin's go-ahead. Each step is its own PR with its own done line.

0. **Stencil in salal.** As 3.5. Done: a tagged salal with a ui-sandbox demo of a masked fill; worldsim's pin moves to it.
1. **Prove the basis.** Benches of a second `PatchableMesh` used as the record, as section 6 lists. Done: numbers in this document; a decision on wrapper against an upkeep-off option; a no-go sends the record's form back to Kevin before anything else is built.
2. **One sight-shape accessor.** `VisionSystem` gives centre, radius, optional polygon, and rebuild counter in one call; `VisionOverlay` uses it and draws outdoor sight. Done: the old accessor is gone; the overlay shows discs outdoors.
3. **The record.** `SeenArea` and `PlayerVisibilitySystem`: legs, indoor polygons, the landing, the budget, the queries. Done: unit tests for a straight walk (one ring), a turn, a jump (no bridge), a covered leg (nothing registered), a doorway, the landing; a sim test that after a long walk every seen point is on the mesh and the record never lost ground.
4. **The cover.** The renderer of 3.4, the reveal verb, the state view. Needs steps 0 and 3. Done: the three areas show in the game; the capture of section 6; `Perf:` line in the PR.
5. **Input.** The rows of 3.7. Done: a unit test per row; a right-click into the black walks the colonist to the edge.
6. **Movers shown only while visible.** The filter and the selection rule. Done: a test with a mover that is not a colonist, absent from the render data out of sight and present in it.
7. **Landing memory.** As 3.8. Done: a test that a crew member knows a thing 100 m from the drop at tick 0, and a count of entries against the cap on a dense landing.
8. **Tests and docs.** Reveal in `scripts/perf`; scenarios 09, 11, 13 updated; scenario 14 added and run; the nav document's notes on player visibility (1.1 and 4.11) brought up to date.

## 8. Not in the first build

- **The last-seen record** for seen things (rule 13, 3.6).
- **The style pass:** soft edges, a textured or stylised cover, desaturation, a reveal animation. "Visual design: the look of unexplored and seen areas" on Specboard becomes that pass and no longer gates the build.
- **Vegetation thinning sight.** "Prototype: sight ray casting across the nav mesh with tree attenuation" stays a later exploration; the first build's sight is vision's, walls only.
- **The seen area on a map** (Minimap epic).
- **Saving** the record (3.11).
- **Not loading or drawing chunks with no seen ground.** The camera loads and draws them and the cover hides them.
- **Smooth indoor sight.** The lit area steps with vision's 0.5 m polygon cache.
- **More than one player faction.**

## 9. Open questions

None blocks the build.

1. Does a sleeping colonist see? Today he does, for memory and so for player visibility. It is a vision rule, and changing it changes what colonists learn while asleep.
2. Should a move order into the black carry on as ground opens? Today's answer is no (3.7); it waits on route planning past meshed ground.
3. Does a tall thing on seen ground show whole where it reaches over unseen ground? Today it is cut at the edge. A style-pass question.

## 10. Decisions log

1. 2026-11-11, Kevin: the name is player visibility; only colonists reveal; the camera never does. (Rules 1 to 5.)
2. 2026-11-12, Kevin, during the nav work: no move orders to unseen area; placement only in the viewable area; the landing sees 150 m. (Rules 6 to 8.)
3. 2026-11-13, round 1: a separate record; an exact polygon union, chosen over a recommended fine raster; "viewable" means seen for moves and placement; nav's area is the "meshed area". (Rules 9, 10, 12.)
4. 2026-11-13, Kevin, unprompted: "everything data wise we do is vector, NO RASTER. if we want smooth edges or pixel effects we'll do that later in a style pass, for v1 I just want hard vector edges." That removed the earlier draft's 1 m bit raster and its blurred mask composite. (Rules 10, 11.)
5. 2026-11-13, round 2: last seen is the rule and is built later; free pan, chosen over a recommended clamp to the seen bounds; a move order into unseen ground walks to the seen edge, chosen over a recommended refusal cue; the minimap is left to its epic, chosen over drawing the seen area on it now. (Rules 13, 14, 15, 20.)
6. 2026-11-13, round 3: the seen area never shrinks; the landing crew knows the landing area; a reveal toggle, with perf captures run revealed; the cover is drawn by stencil in salal. (Rules 16 to 19.)
7. 2026-11-13, in writing this, the author proposed: legs in place of a disc per step, the two radii, pending shapes counting as seen, the one-order reading of rule 15, and the first build not waiting on the visual design or the attenuation prototype. Kevin, the same day: "They stand."

## Related documents

- [Vision Architecture](./vision-architecture.md): sight shapes, walls, doors and windows
- [Nav Mesh: One Mesh, Variable Resolution](./nav-mesh-variable-resolution.md): the meshed area, rules 17, 27, 35, and 39 to 42
- [Memory System](../design/game-systems/colonists/memory.md): what colonists know
- [Game Overview](../design/game-overview.md): the design goal
- [Testing](../testing/README.md): scenarios and the perf rule
