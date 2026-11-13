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
19. The cover is drawn with the stencil buffer, behind a salal API that follows CSS: `mask` and `mask-composite` as a property of a draw. An API that exposed stencil writes and tests was rejected, and so was a push and pop scope, which CSS does not have. (2026-11-13.)
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

That choice is gated on measurement (step 1, section 7). The known weak spot is the ring registry: every ring that ever extended the union is kept, and a patch reads every registered ring over the spot (the covered test no longer does, 6.2). The design below is shaped to register few rings. Indoors it does not yet: a visibility polygon that extends the record is registered whole, and in a base beyond seen ground those rings all overlap, so patches grow past 300 ms (6.2). Registering only what a polygon adds was prototyped (6.3) and works for two colonists but not for twenty or fifty, so how the record holds indoor ground beyond the landing area is open; a record kept as boundary loops with no ring registry is the next candidate, and the choice is Kevin's. The mesh also runs nav upkeep the record has no use for (labels, door tags, width lists); step 1 decides between a wrapper that pays for it and an option on `PatchableMesh` that turns it off for an instance.

**Sight polygons.** A sight disc is `geometry::sightPolygon` (`libs/geometry/visibility/SightPolygon.h`): a regular polygon of `kSightSides` sides inscribed in the circle, its directions from a table made at compile time with + - x / alone and pinned by a test, the table `geometry::nav::limitPolygon` uses for nav made from the same code (`core/DirectionTable.h`). Each vertex is truncated toward the centre and pulled in a millimetre at a time while it lies outside the circle, so the polygon never reaches past the disc it stands for. `computeVisibilityPolygon` takes its rim from the same function, so an observer's outdoor shape and indoor shape share their vertices on the circle, and no step of either uses trigonometry: the ring is the same on every toolchain by construction.

**Outdoors: legs.** A colonist walking in the open does not stamp a disc per step. He holds a **leg**: the straight segment from where the leg began to where he stands. The ground the leg has seen is its capsule, the hull of the sight polygons at its two ends, one ring of `kSightSides` + 2 vertices however long the leg is. A leg ends, and its ring is committed to the record, when:

- some position he has held since the leg began lies farther than `kSeenSlackMm` from the straight line (he turned), or
- some position he has held lies more than `kSeenSlackMm` farther from the leg's start than he now stands (he turned back), or
- he steps farther than `kSeenJumpMeters` in one frame (a teleport or a forward snap; no capsule bridges the jump), or
- he goes indoors, or is destroyed.

The held positions are not kept. What bounds the leg's end is kept instead: the wedge of directions from the start whose line passes within the slack of every held position (each position farther out than the slack narrows it by its two tangent directions, found with a square root and no trigonometry), and the farthest any lay from the start. A leg's position is the colonist's `Position` each frame; whether he is indoors is vision's last pass, so a leg can run on up to four frames after a wall comes into sight, by under 0.15 m at 1x. Below a sight radius of `kSeenLeastRadiusMm` no leg is held: the capsule is built in one pass over the sight polygon's sides, each staying at the start or moving to the end by whether it faces back along the leg, which needs the polygon convex, and a sight polygon under about half a metre can be pulled concave by its vertices' truncation.

A straight walk of any length is one ring. A ring the record already covers (the covered test below, with the leg's start as its centre) is dropped without being registered, so colonists moving about ground the colony knows add nothing.

**Radii.** Two tunables keep the containments of 3.1 exact while a leg stands in for a path that wobbled by up to the slack:

- a committed capsule has radius sight minus `kSeenSlackMm`, so it lies inside what was truly seen from the path;
- the visible disc is drawn with radius `drawnVisibleRadiusMm(sight)`, so every disc drawn along the path lies inside the capsule that gets committed. A held position lies within the slack of the leg's line and no more than the slack past its end, so within the slack times the square root of 2 of the segment, and the capsule reaches at least `sightInradius(sight - kSeenSlackMm)` from every point of the segment; the drawn radius is that inradius less 71 mm.

With 50 mm of slack the drawn disc is 29.84 m for a 30 m sight. Ground never goes from bright to black behind a colonist.

**Indoors: polygons.** With a wall in sight there is no hull to take. Each visibility polygon vision builds is committed as its own ring, with the colonist's position as its centre, covered ones dropped as above. Its rim is the 64-gon, so a polygon has as many vertices as the walls in sight call for and no more: 47 at the step 1 doorway against 440 before, 184 in an eight-room base (6.2). The visible shape drawn is that same cached polygon, so visible and seen agree by identity. The polygon is up to 0.5 m stale, which with hard edges shows as the lit area stepping while a colonist walks past a doorway; it is a known limit of the first build (section 8).

**Simple rings.** `computeVisibilityPolygon` is star-shaped about the observer only up to the millimetre snapping of its hits: a shadow edge's far end, snapped, can sit a hair behind its near end's angle. That alone leaves the ring simple, but nothing promises it, and `NavInputPolygon` needs a simple ring. `geometry::simpleAround(ring, centre)` is the exact check: the ring winds once round the centre, and every pair of edges whose angular spans round the centre meet (one pair a vertex on a near star-shaped ring) is tested with `intersectSegments`. A polygon that fails is skipped: it is not committed, the colonist's next polygon 0.5 m on picks the ground up, and the visible shape drawn that moment is still the polygon. The fuzz in 6.2 found 9 in 31,404 base polygons.

**The covered test.** `geometry::nav::sightShapeCovered(record, shape, centre, scratch)` (`libs/geometry/nav/SightCover.h`) is one test for every shape: a leg (centre: its start), the landing, and an indoor polygon (centre: the observer). The shape is fanned from its centre into a triangle per side; a triangle's core is the part of it farther than `kSeenCoverToleranceMm` from its side's line, strictly inside its wedge. The shape is covered when the centre is on the record and no edge of the record (a live triangle edge with nothing across it) meets a core. The cores and the centre are one connected piece, so they then lie in the record. The edges come from the mesh's own index of bounds edges with boundary intervals, through `PatchableMesh::meshEdgesNear`, so the test reads the union's edge near the shape and never the rings registered there, and its cost does not grow with them.

The tolerance is for walls. A new polygon's side along a wall and the record's edge along the same wall are both hits snapped to the millimetre from different rays, so they lie within about a millimetre of each other and cross; an exact test would call every such polygon uncovered and register a sliver for each step. What the record can then lack of a covered shape lies within 5 mm of one of its sides' lines, inside that side's triangle, plus the wedges a snapped shadow edge steps back over, which are under a millimetre wide. Nothing deeper is accepted. Shadow edges are the exception the tolerance does not take: they run along rays from the centre, and where the record's edge runs along the same ray (an earlier observer in line with the same corner) the mesh's snapping can put it a millimetre inside. Where the record's edge meets a core only within the tolerance of a ray, the answer is `groundCovers`', exact and costlier: once in 125 polygons at the doorway, never in a base inside seen ground, and in 5% of polygons in a base beyond it (6.2). `groundCovers` stays for nav.

**Pending shapes count as seen.** A colonist's open leg, and any ring waiting its turn, sit in a pending list. Queries and the cover read the record and the pending list together, so the tick a ring is committed on changes nothing the player sees or can click. `covers` asks the record (`groundCovers`) first, then a convex pending shape holding every vertex of the outline, and only then builds a scratch mesh of the record's triangles and the pending shapes near the outline and asks it.

**The interface.** `SeenArea` names no mesh type: it takes shapes (`commitNow` for the landing, `queue`, `setOpenLeg` and `closeLeg`), runs the budget (`update`), answers the queries below in millimetres, and hands the renderer the committed triangles, the pending shapes, and `epoch()`, which moves with the committed record only (pending shapes change every frame and are drawn on their own). `PlayerVisibilitySystem` holds the legs, gives the queries in metres, and adds `isVisible`, which reads vision.

**Budget.** At most `kSeenRingsPerTick` rings are tested and committed a tick, first in, first out, on the main thread. It is a count and never a time, so the result does not depend on the machine. If step 1's numbers call for a worker, the pending rule above already makes that invisible.

**The landing.** One inscribed polygon of `kLandingSightMeters` round the drop is committed before the crew exists, from the same constant nav's landing limit uses. Inscribed in the circle that nav's limit circumscribes, it lies inside the meshed area.

**It only grows.** Nothing is ever removed (rule 16). A colonist's death ends his leg and commits it.

**Queries**, all from the main thread:

| Query | Answers |
|---|---|
| `isSeen(point)` | The point is in the record or a pending shape |
| `isVisible(point)` | The point is in a player colonist's sight now, by vision's own test (the true radius or polygon) |
| `covers(outline)` | The whole outline is seen |
| `lastSeenToward(from, to)` | The seen point on the segment nearest `to`; `to` itself when it is seen; none when nothing on it is. The segment is cut where it meets the record's edge (`meshEdgesNear`) and the pending shapes' sides, the last piece whose middle is seen ends the answer, and its end is stepped back a millimetre at a time until it rounds onto seen ground |
| `epoch()` | Changes when the record does |

### 3.4 Drawing the cover

Three flat fills with hard edges: black on unseen ground, a dark tint on seen ground out of sight, nothing on visible ground. The cover is geometry the GPU fills. Nothing is rendered to a texture and nothing is blurred.

Sight shapes overlap and move every frame, so "seen but not visible" is not a shape worth computing. Two masked fills over the view do it, each mask a rectangle minus a union (salal's `docs/masks.md`):

1. **The tint.** A fill of black at `kSeenDimAlpha` whose mask is the view minus every player colonist's visible shape (a disc outdoors, the cached polygon indoors, each a polygon layer). It darkens everything out of sight, unseen ground included.
2. **The unseen fill.** A fill of `kUnseenColor`, opaque, drawn over the tint, whose mask is the view minus the record's mesh and the pending shapes.

```cpp
std::vector<MaskLayer> tintMask = {{.image = MaskImage::rect(view), .composite = MaskComposite::Subtract}};
for (const SightShape& sight : sights) {
    tintMask.push_back({.image = MaskImage::polygon(sight.screenPoints)});
}
Primitives::drawRect({.bounds = view, .style = {.fill = {0.0F, 0.0F, 0.0F, kSeenDimAlpha}}, .mask = tintMask, .key = coverKey});

std::vector<MaskLayer> unseenMask = {
    {.image = MaskImage::rect(view), .composite = MaskComposite::Subtract},
    {.image = MaskImage::mesh(seenMesh, camera)},
};
for (const PendingShape& pending : pendingShapes) {
    unseenMask.push_back({.image = MaskImage::polygon(pending.screenPoints)});
}
Primitives::drawRect({.bounds = view, .style = {.fill = kUnseenColor}, .mask = unseenMask, .key = coverKey + 1});
```

The order matters: the tint first, the opaque fill over it. It is correct because every visible shape lies inside seen ground (3.1) and the unseen fill hides the tint beneath it. Layers are CSS's: first on top, each composited with what is under it, `Subtract` being the layer where it falls outside those below. Rect and polygon layers are in screen space, as any batched draw is; the mesh layer is in world space under the camera.

The outdoor disc is drawn at the colonist's position that frame, so it moves smoothly; the record catches up underneath through the pending leg.

**Where in the frame.** In `GameScene::render`, after `EntityRenderer::render` and before the drawing preview. Terrain, construction, flora, groundcover, and every ECS thing are covered in one place, with no change to any world shader. The debug overlays, the selection indicator, the move marker, the placement ghost, and the UI draw after it and are never covered.

**Home.** A cover renderer in `libs/world-view`, which reads the record and the sight shapes from `sim`. The record's triangles are uploaded as a world-space mesh when `epoch()` changes; pending shapes and fans are a handful of triangles a frame.

**As built (step 4).** `worldview::VisibilityCoverRenderer` (`libs/world-view/world/rendering/`), with `kSeenDimAlpha` and `kUnseenColor` beside it. salal's instanced meshes have 16-bit indices, so the record is uploaded as as many meshes as it needs, each at most 65,535 vertices (21,845 triangles, three unshared vertices each), one mesh layer apiece; the old meshes are released at the upload, which salal holds back until any draw naming them is flushed. A colonist's bright shape is vision's cached polygon with walls in sight, and outdoors the 64-gon of `drawnVisibleRadiusMm` round his `Position` that frame; a colonist whose sight is too small to hold a leg (`kSeenLeastRadiusMm`) draws none, since nothing records it. Sight and pending shapes whose screen bounds miss the view are left out of the masks. The cover flushes what is pending, then draws its fills at keys 30 and 31: the lowest key drawn after it in the same flush is the terrain polygon overlay's 40, then the rooms (56), vision (62), and nav (66) overlays, the selection indicator and move marker (100), the drawing preview (898 and up), and the placement ghost (1000). Both fills run 4 pixels past the view.

**Consequences taken on purpose.** A tall thing that stands on seen ground and reaches over unseen ground is cut at the edge. A thing on unseen ground is still drawn underneath and then covered; skipping that work is a later step (section 8).

### 3.5 What salal gained

`RectArgs::mask`, a span of `MaskLayer`: each layer a `MaskImage` (a rect, a polygon filled by the nonzero rule, or an uploaded mesh under a camera) and a `MaskComposite` (`Add`, `Subtract`). Names, layer order, and operator meanings are CSS's `mask` and `mask-composite`; the stencil buffer underneath is named nowhere in the public API. Layers are read during the call; a mesh layer's mesh must live until the frame's flush, and releasing one mid-frame waits for it. Equal consecutive masks share one build. Not built until something asks: `Intersect`, `Exclude`, a circle image, nested masks, `mask-mode`, a mask on other draws, and the ui's version. It is merged to salal's main and ships as v0.41.0; the cover's PR moves worldsim's pin to that tag. Lighting ("Lighting: light emitters occluded like visibility" on Specboard) will want the same facility.

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

The same rules apply as for sight: a thing with no capability is not remembered, and the 10,000-entry cap holds. On the quickstart landing a crew member knows 6,984 things right after it, 70% of the cap, with nothing evicted. Once his walking fills the rest, the least recently seen entries go first, as for any memory. A colonist who joins later was not in the crash and starts with his own sight, as the memory design says.

### 3.9 Camera and minimap

The camera is not clamped (rule 14). It still loads the chunks round it; over unseen ground they are drawn and covered. The region minimap is unchanged (rule 20). The record can give the Minimap epic its outlines when that epic wants them; no outline walk is built here.

### 3.10 Dev tools and tests

- `/api/dev/reveal?on=1|0`, on the pattern of `freebuild`: with reveal on the cover is not drawn, the mover filter passes everything, and the input rules of 3.7 are off. The record keeps growing underneath, so turning reveal off shows the true state.
- `/api/state?what=visibility`: the reveal flag, the seen area in square metres, rings registered, shapes pending and dropped, and the epoch. salal's state views take no parameters, so the point query is `/api/dev/visibility?x=&y=`: the same totals, and whether that point is seen and whether it is visible.
- Dev verbs that take a position (`spawn`, `colonist`, `teleport`) ignore player visibility, as they ignore the player.
- `scripts/perf` turns reveal on before a capture, so existing views (two of them 70 km and 131 km from any colonist) and their numbers stay comparable, and off again after it; a build without the verb answers 404 and draws no cover, the same frame. `perf.mjs capture --no-reveal` keeps the cover, for the capture near the landing that measures it.
- Scenarios 09, 11, and 13 gain a reveal step where they look away from colonists. A new scenario, `14-player-visibility.md`, covers the feature itself: land and see bright ground round the crew and tinted ground out to 150 m, black beyond; fail to place on black ground; order a move into the black and watch the colonist stop at the edge and the ground open; select a tree on tinted ground.
- The dev overlay that prints the biome under the camera still does so over unseen ground. It is a dev overlay and stays.

### 3.11 Saving

There is no game save to put the record in. When one exists the record saves as its registered rings in order, and loading replays them; pending shapes are committed before a save. That work goes with the save, not with this epic.

## 4. Tunables

| Name | Start | What it must cover |
|---|---|---|
| `kSightSides` | 64 | Sides of a sight polygon (`geometry::kSightSides`, built). At 30 m a 64-gon's flat sits 36 mm inside the circle. More sides add vertices to the seen edge and to every indoor polygon |
| `kSeenCoverToleranceMm` | 5 mm | How far the record's edge may reach into a shape that is still called covered (`geometry::nav`, built). Above the millimetre two snapped hits on one wall face differ by; the record can lack strips this wide along a covered shape's sides |
| `kSeenSlackMm` | 50 mm | How far a path may wobble from a leg's straight line, or turn back from its farthest reach, before the leg ends. Smaller means more rings; larger shrinks the drawn disc (by this times 1 plus the square root of 2) |
| `kSeenLeastRadiusMm` | 1 m | The least leg radius; a colonist whose sight gives a smaller one holds no leg. A sight polygon under about half a metre can be concave |
| `kSeenJumpMeters` | 2 m | A step longer than this in one frame ends the leg without bridging it. Above a frame's travel at the fastest game speed, far below any teleport |
| `kSeenRingsPerTick` | 4 | Rings tested and committed a tick. Step 1 (6.1): 50 walkers at 10x make about 2 legs a frame, and 4 covered tests by the union's edge cost about 0.08 ms mean |
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

### 6.1 Measured: the seen record (step 1)

"Step 1: Prove a second PatchableMesh works as the seen record (benches, go/no-go)" on Specboard. Benches are `libs/geometry/nav/SeenRecord.bench.cpp` (`geometry-benchmarks --benchmark_filter=BM_Seen`), one RelWithDebInfo run each, headless, on the dev machine. Machine load: `busy` before the final run (CPU 6%, GPU 37% from another session, no builders) and `mostly-idle` after (CPU 5%, GPU 28%). Two earlier full runs, `busy` before and after (CPU 3 to 8%, GPU 36 to 38%), gave the same figures within 15%. These are single-threaded CPU benches and the busy verdicts came from the GPU, but by the rule in [testing](../testing/README.md) none of this is a baseline.

Shapes as 3.3 has them: a sight polygon is a 64-gon inscribed in its circle, and a leg is the hull of the 64-gons of radius 29.95 m at its two ends (66 vertices). Each shape is tested with `groundCovers` and patched in only when it is not covered. K is the number of registered rings whose boxes meet the shape's box. Patch times are `applyPatch` alone, with no width refresh, except where stated.

**Exploration**, legs walked eastward, each turning up to 30 degrees from the last, so almost every leg reaches new ground and every one registers:

| Legs | Walk | Rings | Registry | Triangles | Patch ms (mean / p99 / max) | `groundCovers` us (mean) | Live triangles / vertices | `locate` ns (mean / p99) |
|---|---|---|---|---|---|---|---|---|
| 20 m | 10 km | 500 | 246 KB/km | 37 KB/km | 0.19 / 0.25 / 0.94 | 24 | 2,203 / 2,205 | 123 / 262 |
| 50 m | 10 km | 200 | 98 KB/km | 15 KB/km | 0.17 / 0.23 / 0.92 | 14 | 910 / 912 | 109 / 156 |
| 100 m | 10 km | 100 | 49 KB/km | 8 KB/km | 0.18 / 0.23 / 0.87 | 9 | 491 / 493 | 98 / 138 |
| 200 m | 10 km | 50 | 25 KB/km | 5 KB/km | 0.17 / 0.69 / 0.69 | 8 | 282 / 284 | 96 / 131 |
| 20 m | 50 km | 2,500 | 246 KB/km | 37 KB/km | 0.19 / 0.26 / 0.75 | 18 | 11,021 / 11,023 | 121 / 206 |
| 50 m | 50 km | 1,000 | 98 KB/km | 15 KB/km | 0.18 / 0.24 / 0.78 | 12 | 4,383 / 4,385 | 108 / 162 |
| 100 m | 50 km | 500 | 49 KB/km | 7 KB/km | 0.19 / 0.25 / 0.84 | 10 | 2,203 / 2,205 | 101 / 144 |
| 200 m | 50 km | 250 | 25 KB/km | 4 KB/km | 0.18 / 0.26 / 0.66 | 9 | 1,146 / 1,148 | 99 / 131 |

The registry is about 4.9 KB a ring (outline, its 3x sample, edge ids, and the edge index) and is 6 to 7 times the triangles. Grids, hash tables, and interned lists are left out of both columns. Patch time does not grow with the walk's length. Every run validated clean, with one pass a patch and no unread covers.

**Covered test against K.** K legs of 500 m run out from one hub, all registered, all overlapping there; 1,000 short legs within 5 m of the hub are then tested, every one covered (K = 4: 852 covered).

| K | `groundCovers` us (mean / p99) | Boundary-edge test us (mean / p99) |
|---|---|---|
| 1 | 2.6 / 18 (none covered) | 0.8 / 4.9 |
| 4 | 74 / 96 | 12 / 56 |
| 16 | 515 / 616 | 9.8 / 12 |
| 64 | 1,159 / 1,794 | 12 / 21 |
| 256 | 5,021 / 7,360 | 15 / 25 |

`groundCovers` costs 18 to 32 us for every ring over the spot, because it runs each of the outline's 66 edges against every nearby ring's 66. That is the weak spot 3.3 named, and it is real. The boundary-edge test (below) does not grow with K.

**Wandering inside seen ground.** A region about 300 m across seen by a sweep (13 rings), then 5,000 legs of 2 to 15 m at random headings inside it. 4,966 were covered and 34 registered (13, 26, 32, 39, 47 rings after each 1,000 legs). `groundCovers` was 71 us mean, 247 us p99, rising with K: 40 us at K 2 to 3, 59 us at 4 to 7, 107 us at 8 to 15, 167 us at 16 to 25. Registered legs patched in 0.26 ms mean.

**A camp.** The landing polygon, then walkers at 2 m/s wandering legs of 2 to 15 m, turned back at 150 m from the drop, for one simulated hour:

| Walkers | Legs | Registered | Per walker-hour | By minute 10 / 30 / 60 | K (mean / max) | `groundCovers` us (mean / p99 / max) | Boundary-edge test us (mean / p99 / max) | Patch ms (mean / p99) | Registry |
|---|---|---|---|---|---|---|---|---|---|
| 10 | 8,843 | 273 | 27.3 | 27 / 150 / 273 | 27 / 80 | 215 / 706 / 1,033 | 21 / 313 / 839 | 0.41 / 0.79 | 1.3 MB |
| 50 | 44,085 | 562 | 11.2 | 226 / 434 / 562 | 54 / 159 | 403 / 1,145 / 1,779 | 20 / 65 / 1,591 | 0.63 / 1.43 | 2.7 MB |

Registration slows as the ring 150 to 180 m out fills in (50 walkers: 226 in the first 10 minutes, 42 in the last), but K keeps climbing, because every sliver that extended the edge stays in the registry and overlaps the next.

**Re-walks.** A 100 m leg at 17.3 degrees, 37 km from the origin, walked again 40 times offset alternately left and right by 1 to 40 mm (and by 1 to 40 cm), each re-walk's far end 3 mm further over: all 41 registered, patch 1.24 / 2.70 ms (mean / max) at millimetres and 1.80 / 4.22 ms at centimetres, validate clean, no unread covers. The millimetre case takes a second pass on 61% of patches.

**Indoors: a doorway.** `computeVisibilityPolygon` at 30 m from viewpoints 0.5 m apart, walking past a 1 m doorway in a 0.2 m wall, along one jamb's face, and through it straight and on a diagonal (125 viewpoints):

| | Mean | p99 | Max |
|---|---|---|---|
| Polygon vertices | 440 | | 638 |
| `computeVisibilityPolygon` us | 348 | 570 | 590 |
| `groundCovers` us | 12,030 | 65,439 | 73,549 |
| Patch ms | 43.0 | 84.9 | 89.5 |

95 polygons registered and 30 were covered; K reached 94. The mesh validated clean with no unread covers, and the unit test `PatchableMesh.DoorwayVisibilityPolygonsMatchFreshBuild` (51 of these viewpoints) equals a fresh build. Every polygon in the test was a simple ring. `computeVisibilityPolygon` does not promise that: its vertices are hits snapped to the millimetre and walked in angular order, so a snapped hit can in principle fall behind its neighbour's angle, and `NavInputPolygon` requires a simple ring. The indoor numbers come from the polygon's size: its sight circle is a 720-gon, so a mostly unoccluded view has hundreds of vertices, and patch and covered test both scale with vertices times K.

**Whether the toolchains agree.** `Visibility.DoorwaySceneIsTheSameOnEveryToolchain` pins two doorway scenes (at the origin and 41 km out) by vertex count and a hash of the vertices, as MSVC produces them. CI runs it on clang. From the code: the only float steps are `std::cos` and `std::sin` for the 720 circle directions, then `std::llround`; `sqrt` and division round the same everywhere. A last-bit difference in `cos` or `sin` moves a vertex only if the scaled value lies within about 1e-11 mm of a half, so the rings are expected to agree, but nothing guarantees it. A compile-time table, as `LimitPolygon.h` uses, would.

**Nav upkeep.** The 10 km walk in 20 m legs, 500 patches of 0.18 ms mean. Per patch, the component labels' update is 1.9 us mean (2.9 p99) and door tags with no doors 0.9 us (1.2 p99): 1.5% of the patch together. Widths never refreshed hold 19,860 triangle ids after 10 km, 79 KB, growing without bound at about 8 KB/km; one refresh then catches up in 2.2 ms. `refreshWidths` after every patch costs 0.021 ms mean (0.033 p99), 12% of the patch.

**Walking the union's edge.** One pass over the triangle slots collecting every edge with nothing across it: 1 us at 282 live triangles, 8 us at 2,203, 17 us at 4,383, 83 us at 11,021, about 7.5 ns a live triangle.

**A covered test that reads the edge, not the rings.** Prototyped in the bench with the public API only: the outline is covered when every vertex `locate`s on the mesh and no boundary edge meets its closed area; a boundary edge that comes near answers "unsure" and falls back to `groundCovers`. The boundary edges sit in a grid of 16 m cells, updated from each patch's report (3.7 to 39 us mean per patch). Over 64,000 shapes it never disagreed with `groundCovers`. Its time, fallback included, is in the tables above: 3 to 4 us while exploring, 13 us wandering, 20 us at the 50-walker camp. Where shapes share an edge with the union's boundary it always falls back; the doorway's covered polygons share wall faces, so indoors it saved nothing.

**Verdict: go, with conditions.** A second `PatchableMesh` is a sound basis for the record. It stayed exact through every run (validate clean, no unread covers, the doorway equal to a fresh build), patches of a leg cost 0.2 to 0.6 ms mean whatever the walk's length, `locate` stays near 100 ns, and memory is about 5 KB a registered ring. Use a wrapper, not an upkeep-off option on `PatchableMesh`: labels and door tags are 1.5% of a patch, and the wrapper refreshes widths after each patch (0.02 ms) so their list does not grow. Three conditions: the record's covered test reads the union's boundary edges and keeps `groundCovers` as the fallback, because `groundCovers` grows by about 20 us for every ring over the spot and the camp drives K to 159; indoor shapes must be far smaller than vision's polygons before they go in (built on the record's own 64-gon circle, or clipped to the leg's capsule), because at 440 vertices a patch is 43 ms mean and 90 ms worst, which no main thread can take, and that needs measuring again in step 3; and `computeVisibilityPolygon`'s output is checked or made simple before it reaches the mesh. `kSeenRingsPerTick` stays at 4: 50 walkers at 10x make 122 legs a second, 2 a frame at 60 frames a second, and with the edge test 4 tests cost about 0.08 ms mean; registrations are rare (at most 23 a minute at 50 walkers), so their patch time does not decide the budget.

### 6.2 Measured: indoor shapes and the covered test (step 3a)

"Step 3a: Indoor sight shapes and the covered test, proven in benches before the record is built" on Specboard (WOR-832). What changed: `computeVisibilityPolygon` takes its rim from `sightPolygon` (64 sides, compile-time table) instead of a 720-gon from `std::cos` and `std::sin`, and adds the crossings of occluders with the rim and with each other to its sweep directions; `simpleAround` checks a ring; `sightShapeCovered` is the covered test, on `PatchableMesh::meshEdgesNear` (all as 3.3 has them). Benches are the step 1 file, `geometry-benchmarks --benchmark_filter=BM_Seen`, one RelWithDebInfo run each, headless.

Machine load, by the rule in [testing](../testing/README.md). Run A (open ground, covered test against K, the doorway, re-walks, camp): `busy` before (CPU 5%, GPU 55%, another session's world-sim) and after (CPU 7%, GPU 56%). Run B (the base in seen ground and the ring fuzz, after `simpleAround` was sped up): `busy` before (CPU 66%, GPU 100%, another session building and running the game) and after (CPU 10%, GPU 64%). Run C (the base beyond seen ground): `busy` before (CPU 8%, GPU 47%) and after (CPU 32%, GPU 41%, a build again). These are single-threaded CPU benches and run B began with a build on the machine, so none of this is a baseline; the counts (rings, covered shares, vertices) do not depend on load.

**Headline, against step 1:**

| | Step 1 | Step 3a |
|---|---|---|
| Doorway polygon vertices (mean / max) | 440 / 638 | 47 / 62 |
| `computeVisibilityPolygon` at the doorway, us (mean / p99) | 348 / 570 | 39 / 42 |
| Covered test at the doorway, us (mean / p99 / max) | `groundCovers` 12,030 / 65,439 / 73,549 | 32 / 38 / 2,061 |
| Patch at the doorway, ms (mean / p99 / max) | 43.0 / 84.9 / 89.5 | 1.36 / 2.88 / 2.95 |
| Covered test at K = 256, us (mean / p99) | `groundCovers` 5,021 / 7,360 | 8.3 / 12 |
| Covered test, 50-walker camp, us (mean / p99 / max) | `groundCovers` 403 / 1,145 / 1,779 | 11 / 24 / 322 |
| Base in seen ground, 50 colonists: record's cost per simulated second | not measured | 4.9 ms |
| Base beyond seen ground, 2 colonists: patch, ms (mean / max) | not measured | 314 / 1,477 |

**The doorway walk again** (125 viewpoints, as 6.1): 95 registered and 30 covered, the same as step 1; validate clean, no unread covers, and the mesh equals a fresh build of its rings (same live vertices, same boundary). One polygon's covered test fell back to `groundCovers` (the 2 ms maximum): its viewpoint, (0, -6) m, lies on the line from an earlier viewpoint, (-10, -3) m, through the far end of the wall, so the two share a shadow edge. Every polygon was simple.

**Open ground, with the covered test in place of `groundCovers`** (run A; `groundCovers` timed beside it):

| Row | Covered test us (mean / p99 / max) | `groundCovers` us (mean) | Registered (step 1) | Held exactly, called uncovered | Called covered within the tolerance |
|---|---|---|---|---|---|
| Explore, 20 m legs, 10 km | 14 / 20 / 47 | 24 | 500 (500) | 0 | 0 |
| Explore, 200 m legs, 50 km | 8.7 / 12 / 24 | 9 | 250 (250) | 0 | 0 |
| Covered vs K, K = 1 / 4 / 16 / 64 / 256 | 5.8 / 4.9 / 9.6 / 7.7 / 8.3 (mean) | 2.7 / 119 / 608 / 1,176 / 4,838 | | 0 | 0 |
| Wander, 5,000 legs | 4.6 / 8.4 / 36 | 67 | 33 (34) | 0 | 2 |
| Camp, 10 walkers, 1 h | 9.7 / 23 / 197 | 247 | 270 (273) | 0 | 3 |
| Camp, 50 walkers, 1 h | 11 / 24 / 322 | 476 | 562 (562) | 0 | 0 |
| Re-walk, 1 to 40 mm | 34 / 57 / 57 | 13 | 18 of 41 (41) | 0 | 23 |
| Re-walk, 1 to 40 cm | 29 / 145 / 145 | 21 | 41 of 41 (41) | 0 | 0 |

The covered test does not grow with K, and outdoors it has no fallback: no leg in any open-ground row was answered by `groundCovers`. Patch times and registrations match step 1 (the six explore rows not shown agree too). The re-walks a few millimetres apart are where the tolerance does its work: 23 re-walks are dropped as covered that step 1 registered, and 12 of the 41 tests were answered by `groundCovers` (a capsule's flank along the same line as an earlier one's).

**A base** (runs B and C): eight rooms of 4 to 8 m in two rows, 23.5 by 10.5 m, walls 0.2 m thick, a 1 m door in every inner wall and in 6 of the 12 outer walls, turned 20 degrees and 41 km out. Colonists walk room to room at 2 m/s and now and then out of an outer door to points 3 to 60 m out. Within 30 m of the building (vision's rule, a wall in sight) each builds a visibility polygon every 0.5 m, committed with him as its centre; farther out he walks legs. Two cases: the landing polygon centred 40 m from the building goes in first (a colony's first base), or the record starts empty (a base beyond any seen ground).

| | 20 colonists, landed | 50 colonists, landed | 2 colonists, empty record |
|---|---|---|---|
| Polygons (covered) | 31,395 (100%) | 77,649 (100%) | 3,266 (77%) |
| Legs (covered) | 334 (100%) | 838 (100%) | 31 (26%) |
| Not simple, skipped | 9 | 38 | 0 |
| Polygon vertices (mean / max) | 184 / 220 | 184 / 220 | 186 / 219 |
| `simpleAround` us (mean / p99 / max) | 30 / 49 / 309 | 25 / 48 / 341 | 31 / 68 / 148 |
| Covered test, polygons, us (mean / p99 / max) | 16 / 25 / 206 | 13 / 25 / 1,207 | 3,384 / 18,382 / 900,479 |
| Covered test, legs, us (mean / p99 / max) | 6.9 / 10 / 19 | 5.9 / 10 / 18 | 236 / 1,154 / 1,154 |
| Answered by `groundCovers` | 0 | 0 | 171 |
| Rings registered | 0 | 0 | 775 |
| Patch, polygons not covered, ms (mean / p99 / max) | | | 314 / 1,056 / 1,477 |
| K where registered (mean / max) | | | 374 / 771 |
| Registry and live triangles at the end | 5 KB, 62 | 5 KB, 62 | 8,286 triangles |
| Record's main-thread cost per simulated second | 2.4 ms | 4.9 ms | 412 ms |
| Vision's polygon builds per simulated second | 66 ms | 137 ms | 6.4 ms |

The record's cost is `simpleAround`, the covered test, and patches. Vision's polygon builds, which it makes today whatever the record does, are listed for scale: 1.1 to 1.3 ms a polygon with the building's 160 wall faces in range. That is not slower than before: on the same building and 3,000 viewpoints, `main`'s construction (the 720-gon) takes 5,386 us mean, 7,470 us p99, and 12,450 us at most, with 748 vertices, and this one 1,457, 2,095, and 3,299 us with 193 (a bench-only copy of `main`'s function, not committed; `busy`, CPU 7 to 18%, GPU 52 to 59%, another session's tests). The occluder-pair crossings it adds cost less than the 656 circle rays it drops. In seen ground the registry and the live triangles stay flat for the whole 10 minutes, because nothing registers.

Rings registered per minute with 2 colonists and an empty record: 237, 108, 51, 85, 118, 57, 64, 33, 10, 12. It levels off, but late, and by then every ring overlaps hundreds of others: a 30 m polygon's box covers the whole base, so K is the registry. Of the 775 registrations, 379 added over 10 m2 (ground seen for the first time), 129 between 1 and 10 m2, 76 between 0.1 and 1 m2, 20 between 0.01 and 0.1 m2, and 171 under 0.01 m2. Those last are exactly the 171 tests answered by `groundCovers`: slivers along shadow edges, thinner than the tolerance, that the exact test does not forgive. The same run with 20 colonists did not reach its first simulated minute in 15 minutes of wall time and was stopped.

**Simple rings.** 10,000 polygons from 400 random buildings (2 to 4 by 1 to 3 rooms of 3 to 9 m, random angles, up to 40 km out), with observers on open floor, 1 to 300 mm off wall corners, and in doorways: 5 not simple, and `simpleAround` agreed with an exact test of every pair of edges on all 10,000. In the base, 9 of 31,404 and 38 of 77,687. Vertices 128 mean, 281 most; `simpleAround` 16 us mean, 34 us p99. A ring that is not simple has a snapped hit folded back past a neighbour; skipping it costs the 0.5 m until the next polygon.

**What the tolerance costs.** Of a shape called covered, the record can lack ground within 5 mm of one of the shape's sides' lines, inside that side's triangle, and the wedges a snapped shadow edge steps back over, under a millimetre wide. The value: a hit snapped to the millimetre lies within 0.71 mm of its wall face, so two polygons' sides along one face lie within 1.4 mm of each other, and the mesh's arrangement rounding can move its edge by up to 2.98 mm (the drift `PatchableMesh.h` cites); 4.4 mm in all, under 5. The unit test `SightCover.AgreesWithGroundCoversAndTheOracle` checks it on 213 shapes from random buildings: a shape held exactly is always called covered, the test agrees with `groundCovers` wherever no mesh edge comes within the band, and 8,873 sample points deeper than the tolerance inside covered shapes, at random and just past each nearby mesh edge, were all on the record. In every bench row, no shape `groundCovers` held was called uncovered. One case is not bounded by the tolerance alone: a shape whose only way from its centre to some part of it is a gap under twice the tolerance wide (a door seen almost edge-on). The shadow-edge fallback keeps the test exact there today; a tolerance on shadow edges would have to settle it first.

**Verdict: go on the shapes and the covered test; no-go on registering whole indoor polygons where they extend the record.** Inside seen ground, which is the landing area and wherever colonists have looked before, indoor polygons cost the record 2 to 5 ms a simulated second at 20 to 50 colonists, the covered test is flat in K at 10 to 30 us, a ring is simple or skipped, and nothing registers: that part of 3.3 can be built as written. Where indoor polygons extend the record, each one that adds ground is registered whole, its box spans the base, every ring overlaps every other, and patches pass 300 ms mean and 1.5 s worst within 10 minutes with only two colonists, which no main thread can carry; slivers along shadow edges are 171 of those 775 rings. Step 3 must register what a shape adds, not the shape: the part of the polygon outside the record, cut against the record's edge near it (`meshEdgesNear` gives that edge, and the geometry library's ring booleans can do the cut), so registered rings are small and overlap few others; and it must extend the tolerance to shadow edges, which needs the narrow-gap case settled to stay sound. If cut rings still overlap too much, the next thing to try is coalescing the registry: replacing a cluster of rings by the record's own outline over that ground as one ring. Both keep the record an exact vector union (rule 10).

### 6.3 Measured: registering only what a polygon adds (step 3b)

The idea from 6.2's verdict, built as a prototype in the bench file (`frontierOf` in `SeenRecord.bench.cpp`) and kept out of the library because it does not meet its acceptance. It starts from the covered test's fan: each triangle whose core the record's edge meets is split by rays at every end of a record edge inside it and wherever one crosses the side or the tolerance line. Between two such rays no record edge ends and none cross, so the nearest one is the same all the way across and everything nearer the centre is on the record. Each run of split wedges whose nearest record edge lies deeper than the tolerance becomes one ring: the shape's own boundary over the run, its two end rays turned outward by 100 mm at the run's reach, and an inner chain standing 100 mm back from every record-edge point near it (`kSeenPieceOverlapMm`), so a piece overlaps the record on every side. A polygon whose centre is off the record goes in whole. Pieces are checked with `isSimple`; a polygon whose piece is not simple goes in whole.

Runs D (all rows, 3,000 s of benches) and the earlier variant's run E were both `busy`: D before at CPU 6% and GPU 40% (another session building and baking a planet) and after at CPU 7% and GPU 39% (another session's game); E before and after at CPU 80% and 85% (a build). Counts do not depend on load; times are unconfirmed.

**The base with an empty record**, against 6.2 (whole polygons):

| | 2 colonists, whole (6.2) | 2 colonists, pieces | 20 colonists, pieces | 50 colonists, pieces |
|---|---|---|---|---|
| Polygons covered | 77% | 78% | 92% | 91% |
| Rings registered | 775 | 1,174 | 2,977 | 8,750 |
| Per minute | 237, 108, 51, 85, 118, 57, 64, 33, 10, 12 | 630, 124, 60, 91, 110, 35, 58, 36, 17, 13 | 1,071, 238, 166, 134, 261, 271, 200, 242, 214, 180 | 1,390, 238, 256, 500, 847, 759, 1,011, 1,152, 1,150, 1,447 |
| Pieces per polygon that gave pieces (mean / max) | | 1.58 / 8 | 1.12 / 11 | 1.17 / 11 |
| Whole polygons (centre on the record) | 752 | 3 (1) | 6 (1) | 8 (3) |
| Patch per ring, ms (mean / p99 / max) | 314 / 1,056 / 1,477 | 1.19 / 5.2 / 16.8 | 2.88 / 12.9 / 28.1 | 31.6 / 155 / 719 |
| K where registered (mean / max) | 374 / 771 | 115 / 489 | 453 / 1,829 | 2,165 / 7,685 |
| Rings under 0.01 m2 | 171 | 0 | 0 | 0 |
| Covered test per polygon, us (mean / p99) | 3,384 / 18,382 | 451 / 808 | 583 / 1,075 | 653 / 1,335 |
| Registry at the end | not taken | 2,602 KB | 4,065 KB | 8,073 KB |
| Union | 20,773 m2 | 20,773 m2 | 26,084 m2 | 26,660 m2 |
| Record's main-thread cost per simulated second | 412 ms | 4.9 ms | 46 ms | 543 ms |
| Oracle: points deeper than the tolerance off the record | | 0 of 12,130 | 0 of 114,202 | 0 of 189,403 |

Every run ended with `validate()` clean and no unread covers, and the oracle (every 25th polygon, 200 random points each, kept if deeper than the tolerance inside it) found nothing off the record: the record holds the union of the whole polygons, up to the tolerance. With 2 colonists it works: patches fall from 314 ms to 1.2 ms mean and registrations fall to about one every 4 s. It does not hold with more. At 20 colonists registrations stop falling at about 200 a minute; at 50 they climb, and the union grows by 576 m2 over the 5,800 rings the 50-colonist run registers past the 20-colonist one, so nearly all of those pieces cover ground the record already holds. K climbs with them, because a piece reaches from where the record first ends along its rays out to the polygon's rim, so a single pinhole or sliver in the record deep inside a polygon yields a piece of 1 to 10 m2 (6,073 of the 8,750), which overlaps everything near it. The covered test also costs more than in 6.2: 450 to 650 us a polygon here against 13 to 21 us in seen ground, because the record's edge runs through the whole base and each polygon reads all of it.

An earlier variant, without the turned end rays and the sideways pull-back, levelled off well (20 colonists: 1,115 rings, 987 then 58, 13, 20, 6, 3, 5, 9, 9, 5 a minute; 50 colonists: 1,291 rings, 1,158 then 42 to 3 a minute; patches 1 to 1.5 ms mean, at most 7 ms at 2 colonists), but its pieces abutted the record along their end rays and it left unrecorded wedges up to 16 mm wide and 25 m long, which the unit test's oracle caught. Its covered test cost 25 ms per simulated second at 20 colonists and 89 ms at 50.

Against the acceptance: the oracle, `validate()`, and the open-ground rows hold; registrations fall toward zero only at 2 colonists; the record's cost at 50 colonists is 543 ms per simulated second (9 ms a tick at 60 ticks a second) against a budget well under 1 ms a tick, and the worst patch is 719 ms against a few milliseconds. The two misses are the piece's shape (from the record's first edge out to the rim, so pinholes make large pieces) and the covered test's reading of every record edge in a 60 m box.

The open-ground rows with the prototype in place: exploration registers the same counts (each leg after the first as one piece), patches 0.24 to 0.30 ms mean against 0.17 to 0.19; the frontier costs 65 to 76 us a leg there against 8 to 16 us for the covered test, since every leg is uncovered; wander, camp, covered against K, and the landed base match 6.2 within noise (camp: 271 and 564 rings against 270 and 562). Re-walks a millimetre apart split into many pieces (420 rings for 41 re-walks), each under 1.2 ms to patch. The doorway registers 181 pieces for 95 polygons, 0.66 ms mean a patch against 1.36 ms, and still equals a fresh build.

**Verdict: no-go on frontier pieces as built.** They solve the case 6.2 failed on with two colonists, but at 20 and 50 they do not level off, and at 50 they run away. The next candidate is a record with no ring registry: the union kept as its own boundary loops, each new piece merged into the loops it touches by a polygon union, and the triangles drawn from the loops. It would need a robust many-ring union with snapping (the geometry library's `RingBoolean` handles two rings to one; this needs a loop set with holes), an index over the loops' edges for the covered test (which `meshEdgesNear` gives now), a triangulation of the changed loops for drawing, and a plan for the cost of merging into a loop that rims the whole colony. The decision is Kevin's.

### 6.4 Measured: the tick (step 3)

`sim-benchmarks --benchmark_filter=BM_PlayerVisibilityWalk` (`libs/sim/ecs/systems/PlayerVisibilitySystem.bench.cpp`), one RelWithDebInfo run, headless: the landing polygon, then colonists walking in open ground from the drop at 2 m/s on headings that turn up to 60 degrees every 2 to 15 s, turned back past 400 m; vision every 5th tick as in the game; 36,000 ticks; only `PlayerVisibilitySystem::update` timed. Machine load `busy` before (CPU 3%, GPU 36%, another session) and `mostly-idle` after (CPU 2%, GPU 29%), so the times are not a baseline.

| Colonists, speed | Tick ms (mean / p99 / max) | Rings registered | Shapes dropped as covered | Seen at the end |
|---|---|---|---|---|
| 1, 1x | 0.002 / 0.003 / 0.44 | 37 | 31 | 0.09 km2 |
| 10, 1x | 0.021 / 0.29 / 0.66 | 407 | 272 | 0.35 km2 |
| 50, 1x | 0.100 / 0.51 / 1.15 | 1,126 | 2,366 | 0.56 km2 |
| 1, 10x | 0.006 / 0.26 / 0.62 | 447 | 230 | 0.36 km2 |
| 10, 10x | 0.031 / 0.43 / 1.30 | 1,184 | 5,743 | 0.61 km2 |
| 50, 10x | 0.113 / 0.65 / 1.80 | 1,522 | 33,156 | 0.63 km2 |

## 7. Build steps

Nothing here starts before Kevin's go-ahead. Each step is its own PR with its own done line.

0. **Masks in salal.** As 3.5. Done: a tagged salal with a sandbox demo of the two masked fills; worldsim's pin moves to it.
1. **Prove the basis.** Benches of a second `PatchableMesh` used as the record, as section 6 lists. Done: numbers in this document; a decision on wrapper against an upkeep-off option; a no-go sends the record's form back to Kevin before anything else is built.
2. **One sight-shape accessor.** `VisionSystem` gives centre, radius, optional polygon, and rebuild counter in one call; `VisionOverlay` uses it and draws outdoor sight. Done: the old accessor is gone; the overlay shows discs outdoors.
3. **The record.** `SeenArea` and `PlayerVisibilitySystem`: legs, indoor polygons, the landing, the budget, the queries. Done: unit tests for a straight walk (one ring), a turn, a jump (no bridge), a covered leg (nothing registered), a doorway, the landing; a sim test that after a long walk every seen point is on the mesh and the record never lost ground.
4. **The cover.** The renderer of 3.4, the reveal verb, the state view, and reveal in `scripts/perf`. Needs steps 0 and 3. Done: the three areas show in the game; the capture of section 6; `Perf:` line in the PR.
5. **Input.** The rows of 3.7. Done: a unit test per row; a right-click into the black walks the colonist to the edge.
6. **Movers shown only while visible.** The filter and the selection rule. Done: a test with a mover that is not a colonist, absent from the render data out of sight and present in it.
7. **Landing memory.** As 3.8. Done: a test that a crew member knows a thing 100 m from the drop at tick 0, and a count of entries against the cap on a dense landing.
8. **Tests and docs.** Scenarios 09, 11, 13 updated; scenario 14 added and run; the nav document's notes on player visibility (1.1 and 4.11) brought up to date.

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
8. 2026-11-13, the salal API. A first version exposed the stencil (`setMask` with write and test modes, a value, a compare). Kevin: "this API is awful. rejected. we follow css naming and best practices. propose how we can follow mask and mask-composite." A `pushMask` and `popMask` scope was next: "push/pop arn't in the css language". The third, `mask` as a property of a draw with layers of `mask-image` and `mask-composite`, went to two reviewers at his request, who checked it against the CSS Masking spec and salal's docs; their changes (the property on the draw's args, shapes named as an SVG mask's content, the two fills reordered so neither needs a nested mask, only what the cover uses built) are in what he approved: "Yes, build it". (Rule 19, 3.4, 3.5.)
9. 2026-11-13, Kevin, on a `P::` namespace alias: "our guidelines should specifically say to always use human readable variable names". The rule is in both repos' standards; the code in this document follows it.

## Related documents

- [Vision Architecture](./vision-architecture.md): sight shapes, walls, doors and windows
- [Nav Mesh: One Mesh, Variable Resolution](./nav-mesh-variable-resolution.md): the meshed area, rules 17, 27, 35, and 39 to 42
- [Memory System](../design/game-systems/colonists/memory.md): what colonists know
- [Game Overview](../design/game-overview.md): the design goal
- [Testing](../testing/README.md): scenarios and the perf rule
