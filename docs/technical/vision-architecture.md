# Vision & Line of Sight — Technical Architecture

**Status:** Design
**Created:** 2026-06-12
**Companion to:** [Pathfinding Architecture](./pathfinding-architecture.md) (shared geometry substrate; vision is belief's write path)

How agents see: wall occlusion, windows, discovery, witnessing, and the fog of war — on the same vector-geometry substrate pathfinding uses. Vision and navigation are two consumers of one world-geometry index, which is why they're specified as companions.

---

## 1. Requirements

1. **Belief's write path.** Memory is only ever written by observation ([Memory System](../design/game-systems/colonists/memory.md)); vision is where discovery, witnessing, and stale-memory reconciliation actually happen. Belief-filtered navigation is only as honest as this system.
2. **Occlusion.** Walls block sight. Windows pass sight while blocking movement (the designed security tradeoff). Door openings pass sight in v1 (no open/close simulation yet, consistent with always-pathable doors).
3. **Fog of war is a design goal.** "Areas not visible to the character are hidden" ([Game Overview](../design/game-overview.md)). The same data that feeds simulation must be renderable as the player-facing fog mask and the per-colonist memory debug overlay.
4. **Scale.** Dozens of observers, hundreds of candidate entities in radius, already throttled to every 5 frames. The outdoor case (no occluders anywhere near) is the overwhelmingly common one and must stay as cheap as today's radius query.
5. **Vector-native.** Occluders are exact segments from the construction graph; no raster visibility grid.
6. **Many consumers.** Memory/belief discovery, fog rendering, technology discovery (proximity-triggered, already designed against VisionSystem), and one-off AI queries ("can this raider see that colonist?").
7. **Determinism** (multiplayer future) and TimeSystem speed tolerance, same as everything else.

## 2. Current State

`VisionSystem` (priority 45, every 5 frames) does an AABB query of `memory.sightRadius` (default 30 m, `libs/engine/ecs/components/Memory.h`) against PlacementExecutor's spatial index, plus a shore-tile scan, and writes results into Memory. No occlusion of any kind: colonists see through walls, terrain, and each other. No line-of-sight code exists anywhere in the engine. msdfgen is fonts only.

## 3. Architecture Decisions

### D1: GeometryIndex — one perception substrate, three consumers

A per-chunk spatial index of **occluder/obstacle segments**: wall centerline segments and opening spans, each carrying its stable structure ID, a transparency flag (windows: transparent to sight, solid to movement), and the structure version counter. Fed by the construction contract's publication (the same one navigation consumes); terrain blockers (water has no sight implication, but future cliffs would) slot in the same way.

This is the shared architecture: **navigation** reads it as obstacles, **vision** reads it as occluders, **steering** reads it for distance/clearance/context rays (the perception layer from the pathfinding spec's ray-marching verdict). One index, one version counter, no drift between what blocks movement and what blocks sight — they're the same segments with per-consumer flags.

### D2: Visibility polygons, not per-entity rays

Per observer, per vision tick: gather occluder segments within sight radius; if none (**the outdoor fast path, most observers most of the time**), visibility is the sight circle and the system behaves exactly as today. Otherwise, build the **visibility polygon**: the classic rotational sweep over occluder endpoints, clipped to the sight circle — O(n log n) in occluder endpoints, and n near a base interior is dozens, not thousands. Candidate entities then test point-in-visibility-polygon, which is cheap enough to keep the existing candidate volumes.

Why polygons instead of casting a ray per candidate: the polygon is built once per observer and amortizes across all candidates, and it is *also* the fog-of-war mask (D5) and the debug overlay — three consumers for one computation. Per-entity LOS rays remain available as one-off queries for the AI (targeted checks between two specific agents), using the same index.

This is settled technique in 2D games: Red Blob's visibility article is the canonical construction ([2d visibility](https://www.redblobgames.com/articles/visibility/)); Monaco built an entire game on visibility polygons as both mechanic and signature look; Among Us ships the same idea. Roguelike shadowcasting is the grid ancestor; we sweep exact segments instead because the world is vector.

### D3: Caching and invalidation

The visibility polygon is cached per observer and recomputed only when: the observer has moved more than a threshold (~0.5 m) since the cached build, the structure version changed within their radius (something was built or demolished nearby), or the cache is older than the vision throttle allows. Stationary colonists working indoors — the common indoor case — pay for one polygon, then reuse it.

### D4: Discovery semantics

- **Entities:** in radius AND in the visibility polygon → written to Memory (the existing flow, now honest).
- **Structures are observables, not just occluders.** A wall segment or opening that bounds or intersects the visibility polygon is *seen*: its structure ID enters memory with observed state. Seeing one face of a wall means knowing that segment exists; it says nothing about what's behind it. Discovery granularity = segment/opening ID, matching belief filtering exactly.
- **Witnessing** (memory design: Bob sees Alice harvest the bush): state-change events apply to every observer whose cached polygon contains the event location — event-driven, no rescans.
- **Reconciliation:** an entity whose remembered position falls inside the current polygon but which isn't there anymore is the stale-memory moment — the entry invalidates, tasks fail gracefully, belief replans trigger. Vision is where "memory can be wrong" gets corrected, so this check lives here, not in the task system.

### D5: Fog of war and overlays are the same polygons, rendered

Player-facing fog (three states: unknown/hidden, explored-but-not-visible (dimmed), currently visible) renders from the union of the player faction's visibility polygons: triangulate each (they're star-shaped from the observer — a fan), rasterize additively into a mask texture (`RenderToTexture` exists), sample the mask in the terrain/entity passes. Explored-ever state accumulates into a persistent coarse mask. The per-colonist **memory debug overlay** (memory design doc) is the same render path scoped to one observer.

Simulation stays CPU-side and analytic; rendering consumes the polygon set. Fog UX (what dimming looks like, whether structures stay visible once seen, minimap interaction) needs its own design pass when player-facing fog is scheduled — this doc only establishes that the data comes for free.

### D6: Sight model

360° vision, radius per species from config (colonist default 30 m). No facing cone in v1 — cones arrive if/when stealth mechanics want them, and nothing here precludes them (the polygon clips to a wedge instead of a circle). Future modifiers, all config-shaped: night/light level, weather, concealment (trees as soft occluders — a forest you can hide in is a compelling future, but v1 occluders are walls only).

One assumption to not bake in: sight symmetry. With circles-and-walls, A-sees-B equals B-sees-A; cones and concealment break that later. Belief and AI code should always ask directionally.

### D7: Opening transparency

| Opening state | Sight | Movement |
|---------------|-------|----------|
| Doorway / door (v1, no open/close sim) | passes | passes |
| Window | passes | blocked |
| Wall segment | blocked | blocked |
| Closed solid door (future) | blocked | blocked per permissions |

Transparency is a flag on the opening's construction definition, carried into the GeometryIndex; future door states just flip it at runtime.

## 4. Consumers and Contracts

| Consumer | What it reads | Contract |
|----------|---------------|----------|
| Memory/belief | discovery + witnessing + reconciliation events | structure IDs match belief filtering's IDs (same construction publication) |
| Belief replanning | discovery events | a discovered structure intersecting an agent's path corridor triggers replan (pathfinding section 5) |
| Fog of war / overlays | polygon set per faction/observer | render-side, read-only |
| Technology discovery | proximity + visibility of trigger entities | already designed against VisionSystem; gains occlusion for free |
| AI one-off queries | `canSee(observer, target)` ray | same index, no polygon needed |

## 5. Performance Model

The work scales with occluder density around observers, not world size. Outdoors: zero occluders, fast path, today's cost. Indoors: polygon build over dozens of endpoints (microseconds), cached while stationary, shared across all candidates and the fog mask. Witnessing is event-driven. The 5-frame throttle stays; structure-change invalidation piggybacks the existing version counter. Validate with the perf tooling during implementation; the budget hypothesis is that vision stays unmeasurable next to rendering.

## 6. Phasing

Two separately planned pieces, deliberately:

**Vision System epic (near-term).** Must land with or before the navigation epic's P4 — belief filtering is hollow without it:

1. GeometryIndex (shared with nav's obstacle publication — build once, both consume)
2. Visibility polygon construction + outdoor fast path + caching
3. Discovery/witnessing/reconciliation rewired through the polygon
4. Window transparency, structure-as-observable discovery

**Fog of war (later, separate epic).** Player-facing fog rendering and the memory debug overlay ship with the broader overlay system, on their own schedule. D5 establishes that the polygon data they need falls out of the vision epic for free; nothing in the near-term work should anticipate them beyond keeping the polygon set accessible per faction/observer.

## 7. Risks

1. **Polygon robustness.** Rotational sweeps have the usual endpoint/collinearity edge cases; same integer-coordinate substrate and test-first discipline as the rest of `libs/geometry`.
2. **Dense-base cost.** Many observers among many segments; mitigations are the cache (stationary observers), the version-gated invalidation, and if needed a coarser shared-occluder-set partition per room. Measure before optimizing.
3. **Two-truths drift.** Vision occluders and nav obstacles must derive from the same construction publication. The GeometryIndex being singular is the design guarantee; don't let a second pipeline grow.
4. **Behavioral surprise.** Honest vision makes colonists *worse* at noticing things than players are used to (a colonist indoors no longer sees the berry bush outside the wall). That's the point, but expect tuning pressure on sight radii and memory-sharing cadence once it lands.
5. **Determinism.** Sweep ordering and event application order must be stable; no iteration-order dependence.

## 8. Related Documents

- [Pathfinding Architecture](./pathfinding-architecture.md) — belief filtering (the consumer that forced this), shared perception substrate
- [Memory System](../design/game-systems/colonists/memory.md) — what observation writes, snapshot semantics, navigation states UI
- [Building & Construction Architecture](./building-construction-architecture.md) — the structure publication (IDs, transparency) the GeometryIndex ingests
- [Technology Discovery](../design/game-systems/colonists/technology-discovery.md) — proximity-triggered discovery riding this system
- [Game Overview](../design/game-overview.md) — fog of war as a design goal
