# Entity Selection Silhouette — Technical Spec

**Status:** Built. Since tree variety (`docs/technical/asset-system/tree-variety.md`, R3) the silhouette is per template, not per `defName`; see "Per-template since tree variety" below.
**Created:** 2026-07-02
**Depends on:** the entity depth-sort (world-depth-sorting Story A, PR #248) for the entity-layer sort key the outline draws against; the in-house planar arrangement (`libs/geometry/arrangement`) and `RingBoolean` reference; `AssetRegistry` template cache.

One computed primitive per asset, three consumers. When an entity is selected, draw a **relatively thick outline that hugs the asset's 2D silhouette**, and make every selectable asset's **clickable area precise — its actual filled shape with internal holes eliminated**. Both fall out of a single per-`defName` silhouette (union of the asset's paths, holes filled), computed once and cached beside the template, reusable later for shadows.

---

## 1. Requirements

From the product owner, not the literature:

1. **Thick silhouette outline.** The selected state is a relatively thick outline *around the 2D visual*, hugging the asset's shape. Not a ring, not a halo, not a tint.
2. **Precise, hole-free hit area for all selectable assets.** The clickable region is the asset's actual filled shape, with internal holes eliminated — a click landing in a canopy gap still selects. Today world entities have *no* indicator and a loose 2 m centroid-radius hit-test.
3. **Whole clump selectable.** For an asset that renders as several disjoint blobs (scattered flowers), a click in the bare gap between blobs still selects it. (PO decision — see §7.)
4. **Reusable for shadows.** The flattened outer silhouette is computed and cached so a later shadow pass can reuse it. Shadows themselves are out of scope.
5. **Applies to all SVG-asset-backed selectables:** world entities (trees/flora/rocks), colonists, crafting stations, furniture (including packaged), and loose ground piles (`ResourceStack` entities: hit-test and outline every sprite of the shared `pileLayout`, plus a 0.3 m pick slop around the pile center so a lone small item stays clickable; priority sits after furniture and before static world entities). Construction selectables (walls, openings, foundations, rooms) already hit-test and outline on exact topology geometry and are left untouched.

## 2. Current State (Baseline)

| Area | Fact | Source |
|------|------|--------|
| Selection state | `Selection` is a `std::variant` of 9. `WorldEntitySelection` is positional `{defName, position}` — no id, no rotation/scale | `.../selection/SelectionTypes.h:31-97` |
| Hit-testing | `gatherCandidates` priority ladder; colonists/stations/furniture/world-entities by centroid distance (`kSelectionRadius=2m`); openings/walls/foundations already exact | `.../selection/SelectionSystem.cpp:102-285` |
| World-entity indicator | **None.** Trees/flora/rocks get zero world-space affordance when selected | `SelectionSystem.cpp` renderIndicator — no `WorldEntitySelection` branch |
| Other indicators | Foundation/opening/wall stroke an exact ring via `Primitives::drawLine`; colonist/station/furniture draw a generic 1 m `drawCircle` ring that does **not** hug the art | `SelectionSystem.cpp:374-527` |
| Asset geometry | SVG → `LoadedSVGShape.paths` (beziers pre-flattened); procedural → `GeneratedAsset.paths`. Tessellated to a triangles-only `TessellatedMesh` (with per-shape `parts`), cached per `defName` in `AssetRegistry::templateCache` | `SVGLoader.h`, `AssetRegistry.{h,cpp}`, `Tessellator.h` |
| Silhouette notion | None. Only `MeshBounds::computeBounds` (AABB). No hull, outer-ring, outline, or tessellator boundary-only mode | `MeshBounds.h` |
| Geometry substrate | `buildArrangement` + `HalfEdge` face extraction (general planar union); `RingBoolean::unionRings` (2-ring reference, insert→classify→walk); `WallOffset` (centerline miter/bevel only, no general polygon offset); `pointInPolygon` (exact) | `libs/geometry/**` |
| Click plumbing | `/api/input?ev=click,x,y`; left MouseUp → `handleClick`; click-cycling already implemented (`cycleIndex`, `kClickCycleTolerancePx=6px`) | `GameScene.cpp:631-658`, `SelectionSystem.cpp:287-348` |

## 3. Design overview

```
AssetRegistry::getSilhouette(templateId) ──► AssetSilhouette { rings (local mm),
   (prewarmed, or built on first ask)          hitRegion (closed, for whole-clump),
        │                                       valid }
        │  the union of the template mesh's triangles
        ▼
geometry::silhouetteOfTriangles ──► outer CCW rings, holes filled, separate bodies kept apart
        │
        ├──► outline:   InstanceTransform(local→world→screen), stroke thick, entity-layer z
        └──► hit-test:  InstanceTransform⁻¹(click→local mm), pointInPolygon
```

The silhouette is computed **once per template**, in the template's pivot frame in integer millimetres, cached beside the tessellated template. One shared transform maps local↔world for every instance, static or dynamic, so the outline, the hit-test, and the renderer never disagree.

## 4. The silhouette primitive

`libs/geometry/silhouette/Silhouette.h`: `silhouetteOfTriangles(triangleVertices, closeRadius, tolerance)`, with `outlineOfUnion`, `outlineOfUnionInOnePass`, `dilateConvex`, `closeOutline`, `simplifyOutline`, and `silhouetteBoundMm` beside it. Every step is a vector operation on rings: no shape is ever sampled into cells. (The arrangement under the booleans indexes segment boxes in a spatial index only to choose which pairs it tests exactly.)

**Source: the tessellated mesh triangles, not the fill contours.** The silhouette is the union of the asset's **tessellated template mesh** (`TessellatedMesh`) triangles, so it is the *actual rendered footprint*: fills AND stroke bands. A reed's stem is a `stroke` (`fill="none"`), so a fill-contour silhouette misses it and fragments the reed into separate blobs (seed head vs leaf fan); the mesh union keeps the whole reed, and stroke-only assets (e.g. `PlantFiber`) get a real strand silhouette instead of an AABB fallback.

The pipeline, with L the longer side of the box round the opaque triangles:

1. **Union in halves.** The triangles, each wound CCW, are unioned by `geometry::unionOfFills` (nonzero rule) in halves of the draw order: eight triangles a leaf, then each half's outline with the other's. A path's triangles tile without crossing, so a triangle's inner edges are gone before they meet the rest of the shape, and only outlines are ever cut against outlines. A single union of every triangle would cut every leaf edge against every edge it crosses: on a mature tree that is about twenty times slower. The halves are not simplified between levels; each level's union simplifies by its own 1 mm (`kBooleanSimplifyEpsMm`). Holes are filled where the halves meet and at the end; a piece standing in another's hole goes with the fill.
2. **Simplify** each ring to within L / 512 (Douglas-Peucker, `simplifyRing`), then union again so every ring stays simple. A ring the simplification would flatten to nothing (a body or stroke thinner than the tolerance) is kept as it was, so no body is lost.
3. **Close** by L / 512: dilate by an octagon whose edges touch the circle of that radius (each edge swept by the octagon, unioned with the outline), then erode by the same octagon (the dilation of the complement inside a box clear of the outline, whose holes are the eroded outline). Gaps and notches under about L / 256 fill; the boundary elsewhere stays where it was. The result is simplified to L / 512 again. The closed outline is then unioned with the outline it was given, so the close only ever adds: every opaque triangle lies inside the outline or within `geometry::silhouetteBoundMm` of it, the tolerance plus a millimetre for each union level (about 23 mm on a 6 m tree, about 10 mm on a colonist; measured, 1 mm on a colonist and under 8 mm on any shipped tree). A body under about 2 mm across is the exception: each union's own 1 mm simplification can collapse it.

On a 6 m tree L / 512 is about 12 mm: wiggles below that smooth out and the outline has a few hundred vertices.

**Degenerate assets.** Only a mesh with no opaque triangle yields no silhouette (`valid=false`); stroke-only assets produce real geometry, so no AABB fallback is needed.

**Cost and when it is paid.** The union costs a few times the tessellation: 47 to 157 ms for a tree, 20 to 60 ms for a palm, pine, or the most detailed bushes, under 20 ms for the rest (rocks, colonist facings, simple items), 1 to 3 ms for a box. So `AssetRegistry::prewarmProceduralTemplates` builds the silhouette of every variant it prewarms (every age class and variant of a tree, and every variant of each selectable procedural def without age classes) on its task pool at load, beside the mesh, and files it in the cache. A click never builds one of those. Simple (SVG) assets, colonist facings and boxes build theirs on the first click or outline that asks, each under 20 ms.

## 5. Per-template cache

```cpp
struct AssetSilhouette {
    std::vector<geometry::Ring> rings;        // local mm, used for the outline + shadows
    std::vector<geometry::Ring> hitRegion;    // local mm, closed (whole-clump), used for hit-testing
    bool valid;
};
```

Stored in `AssetRegistry` in an `unordered_map<uint32_t, AssetSilhouette>` keyed by template id, guarded by its own mutex, mirroring the `getMotion` cache (own mutex, heavy work off-lock, short lock to check/insert). `getSilhouette(templateId)` returns the prewarmed one, or ensures `getTemplate` (so the mesh is materialized), builds `rings` and `hitRegion` from its triangles, and caches. Directional colonist variants (`_down/_up/_left/_right`) are separate defs, so each gets its own rest-pose silhouette. Safe to call from the chunk-bake worker (like `getTemplate`). Cleared with the templates on `clear()` / `clearDefinitions()`, which advance `generation()`; the load that follows prewarms again. One template serves every instance regardless of position/rotation/scale/tint.

For a single connected asset, `hitRegion` is `rings` closed a little wider. They diverge for multi-blob assets, where the close bridges the gaps (§7).

## 6. Shared instance transform (mandatory, one-path)

The #1 shared risk is the hit-test inverse or the outline forward-map drifting from what the renderer draws. Every template mesh is built about its def's pivot, and an entity's position is that pivot in the world (collision-shapes.md, "One pivot per def"), so there is one convention: `world = R(rotation)·(local·scale) + position`, with rotation 0 for a dynamic entity.

`assets::InstanceTransform` (`libs/assets/assets/placement/InstanceTransform.h`) is that transform, forward (local→world) and inverse (world→local). The baked statics, the batcher, the animated path, the outline, the hit-test, the placement ghost and footprint, the canopy measure, and nav's placed obstacles all place through it; the GPU instancing shader applies the same formula, and `InstanceTransform.BakedStaticsEmitIt` pins the bake to it. A `packagedLayout` helper (crate back, item fitted inside from its template bounds, crate front) and a `pileLayout` helper give the sprites render and selection share.

## 7. Whole-clump hit area (PO decision)

A click in the bare gap between disjoint blobs of one asset **selects** it. The outline's `rings` keep separate blobs apart (so a click *outside* every blob would miss), so the hit area is a **closed** region: the rings closed by L / 64 (`closeOutline`, the same octagon close as §4), which bridges gaps up to about L / 32, simplified to L / 256, and unioned with the rings again, so the hit region holds the whole outline and a click on anything outlined hits.

The **outline still strokes the exact `rings`** (each visible bloom gets a precise outline, so the highlight reads as "around the 2D visual"), while the **hit-test uses `hitRegion`** (forgiving clicks). Precise-looking selection, generous clicking. This is the one place `rings` and `hitRegion` intentionally differ.

## 8. Precise hit-testing

Broad phase stays a cheap cull but must be widened: today's single-chunk `queryRadius(2m)` would cull a click on a big canopy far from its trunk anchor before the precise test runs, and a tree whose anchor sits in an adjacent chunk but whose canopy overhangs the clicked point lives in the neighbor's index. So: size the broad-phase radius from the **registry's max silhouette half-extent** (per-`defName` max-extent keeps small assets tight) and query the click chunk **plus its 8 neighbors**.

Narrow phase replaces the centroid `dist < kSelectionRadius` accept for colonist / station / furniture / world-entity with `pointInPolygon(InstanceTransform⁻¹(click), hitRegion)`. Packaged furniture tests crate back, item, and crate front at their `packagedLayout` transforms. `kSelectionRadius` survives **only** as the broad-phase radius; the accept comparisons and the generic `kIndicatorRadius` circle are deleted (one-path).

**Overlapping entities cycle (PO decision).** With canopy-sized hit areas, overlapping tree canopies are the norm. `gatherCandidates` pushes **every** world entity whose `hitRegion` contains the click (not just the closest anchor), ordered topmost-first so repeated clicks step front-to-back through the existing cycle machinery. "Topmost" = entity draw/sort order (the depth-sort anchorY), falling back to smallest-area-containing on ties. `candidateKey` (currently quantized position) is extended to disambiguate co-located entities.

## 9. Thick outline rendering

Stroke each transformed `outerRing` in screen space with `Primitives::drawLine` at `kOutlineWidthPx`, plus a `drawCircle` dot of radius `width/2` at each vertex for round joins (an N-way union has sharp reflex corners a miter would spike; round is cheap and needs no general polygon-offset primitive). Gold `Color(1,0.85,0,~0.9)`, reusing the exact ring-walk the foundation/opening/wall indicators already use.

- **Thickness:** constant **screen-space** pixels (zoom-independent) with **min/max px clamps**, and the vertex join-dots gated on `width` vs edge length so a tiny flower doesn't blob into a solid dot and a big tree's outline still reads as thick.
- **Z-order — respect depth-sort (PO decision).** The outline is **not** a flat top pass. It draws in the entity render layer at the *selected entity's* sort key (anchorY, from world-depth-sorting Story A), so a nearer tree correctly occludes a farther selection's outline. This couples the epic to Story A / PR #248.
- **`WorldEntitySelection` branch (new).** Re-resolve the live `PlacedEntity` by `defName`+`position` each frame to recover rotation/scale; if the entity was felled/moved (chopping triggers a nav rebuild), the selection is stale — draw nothing.
- **Composite outlines (packaged, piles):** one outline around the whole thing. `SelectionSystem::unionCompositeRings` maps every part's silhouette rings to world millimeters (packaged: crate back, item, crate front; pile: every `pileLayout` sprite) and folds them pairwise through `RingBoolean::unionRings` until no pair merges. With the item fitted inside the crate the result is the crate's silhouette; a pile's sprites overlap by construction and merge into one ring. A pair the union rejects (disjoint, pinch, would-hole) stays two rings and both are stroked. The last input and result are cached, so a still selection doesn't rerun the boolean every frame.

## 10. Animated colonists (PO decision: rest-pose for v1)

The cached silhouette is rest pose. While walking, limbs sway a few degrees; the thick stroke hides the drift and the outline tracks the colonist's live `Position` exactly (translation is exact, only intra-body sway is unreflected). Hit-test accepts the rest-pose region (deltas within click slop). This keeps one cached silhouette per `defName` and no per-frame union.

Deferred (Tier-2, documented not built): a per-frame posed outline for the single selected colonist by re-running the clip on the posed contours — **not** by deforming the unioned ring (Steiner vertices belong to no `MeshPart`, so deforming the union tears at seams; a faithful posed outline must rebuild from posed contours, cheap at n=1).

## 11. Stories & tasks

**Story A — silhouette geometry primitive** (`libs/geometry`) — DONE, PR #250
- A1 `silhouetteOfTriangles`: the triangles' union in halves, simplified, closed by an octagon (§4). Robust for disconnected/nested/self-intersecting input.
- A2 Unit tests: square, L, separate bodies and a closing gap, hole filled, island in a hole, degenerate and duplicate input, convex dilation reach, thin strands and a thin separate body within the bound; the union's determinism is `UnionOfFills.SameInputSameRingsInTheSameOrder`. Tessellator coverage-oracle cross-check in Story B (renderer not linkable from `libs/geometry`).

**Story B — per-`defName` silhouette cache** (`AssetRegistry`) — DONE
- B1 `AssetSilhouette{rings, hitRegion, valid}` + `silhouetteCache` + mutex + `getSilhouette(templateId)`; `getMotion`-style (compute off-lock). The union of the **template mesh triangles** (quantized to mm) via `silhouetteOfTriangles` for `rings`, closed wider for `hitRegion` (D3). Captures stroke bands; no fill-contour path.
- B2 Only a truly empty mesh yields no silhouette (`valid=false`); stroke-only assets (`plant_fiber.svg`) now produce a real strand silhouette, so the AABB fallback was removed. `clear()`/`clearDefinitions()` wiring.

**Story C — shared instance transform** (extract, one-path)
- C1 shared instance transform forward+inverse, consumed by render, outline, hit-test (since 2026-11 `assets::InstanceTransform`, one convention about each def's pivot).
- C2 `packagedLayout` helper extracted from `DynamicEntityRenderSystem`, consumed by render + selection.
- C3 `world→local→world` round-trip tests.

**Story D — precise hit-testing** (`SelectionSystem`)
- D1 Widen broad phase: query radius from registry max silhouette half-extent (per-`defName`), query click chunk + 8 neighbors.
- D2 Replace centroid accept with `pointInPolygon(click→local, hitRegion)` for colonist/station/furniture/world-entity; packaged = crate OR item.
- D3 Whole-clump `hitRegion` (octagon close, §7) for multi-blob assets.
- D4 Overlapping-entity cycling: push all containing entities, topmost-first ordering, extend `candidateKey`.
- D5 Delete centroid accept comparisons + `kIndicatorRadius`; demote `kSelectionRadius` to broad-phase only; grep stragglers.

**Story E — thick silhouette outline** (`SelectionSystem::renderIndicator`)
- E1 New `WorldEntitySelection` branch (re-resolve live `PlacedEntity`; draw nothing if stale).
- E2 Switch colonist/station/furniture to stroke `rings`: local→world→screen, `drawLine` at clamped `kOutlineWidthPx` + width-gated round-join dots; delete the `kIndicatorRadius` circle.
- E3 Draw in the entity layer at the selected entity's depth-sort key (couples to Story A / PR #248).
- E4 Packaged outline: union crate+item (`RingBoolean`) or stroke both on reject.

**Story F — verify**
- F1 asset-manager (port 8070): silhouettes across flora/rocks/stroke-only for hole-fill + disjoint blobs; no first-selection hitch on dense trees.
- F2 ui-sandbox (`/api/input` + screenshot): canopy-gap click selects; thick outline hugs trees/rocks/flora/colonists/stations/packaged; click between disjoint blooms selects (whole clump); overlapping canopies cycle; walking-colonist outline tracks position; nearer entity occludes farther outline.

## 12. Out of scope / Tier-2

- Actual shadow rendering (only making the silhouette reusable).
- Per-part deforming silhouettes for walking colonists (rebuild-from-posed-contours, n=1).
- Changing construction hit-tests/outlines (walls/openings/foundations/rooms already exact).
- Coupling `CollisionShape` to selection: **the derived visual silhouette wins for hit-testing; `CollisionShape` stays nav-only** (it's the trunk, deliberately smaller than the canopy; its local frame differs from the template frame — never mix its coords into the silhouette test).
- Per-instance-seed silhouette variation (uses the fixed-seed shared template).

## Per-template since tree variety

Each tree draws its own (def, age class, variant) mesh, so the silhouette is cached per template id (`getSilhouette(templateId)`, or `getSilhouette(defName, traits)`). A placed entity uses its stored `templateId`; a dynamic entity resolves its `Appearance` defName (with the facing suffix) and traits. A simple (SVG) asset is one drawing, so all its traits share one template.

Triangles whose three vertices are all below `AssetRegistry::kSilhouetteMinAlpha` (0.5) are left out of the union, so a drop shadow is not part of the outline, the hit area, or the flora view's canopy cover.

A placed item (`parts` def) takes its silhouette from its footprints at its turn, already turned, so it is translated and never rotated, and its outline is injected into the stream in pieces, one at each depth strip ([Rotatable placed items, section 15](./rotatable-placed-items.md#15-selection)).

The world-entity broad phase queries out to `AssetRegistry::maxReachMeters` (the farthest any non-groundcover template reaches from its origin, times its def's largest placement scale, never under `kSelectionRadius`) instead of a fixed 8 m.

## 13. Decisions log

| Fork | Decision | Rationale |
|------|----------|-----------|
| Silhouette computation | **Vector union of the tessellated mesh triangles** (`unionOfFills`, in halves), simplified and closed by vector operations | All game data is vector, never a grid or mask, even a transient one. `unionOfFills` classifies faces by winding, which is sound on nested and disjoint input. Sourcing the *mesh triangles* (not fill contours) captures the actual rendered footprint incl. stroke bands, so strokes (a reed's stem) are in the silhouette and stroke-only assets need no AABB fallback |
| When it is built | With the template, at load, for prewarmed procedural templates; on demand for the rest | A tree's union costs 47 to 157 ms, too long for a click on the main thread; a box or colonist facing costs a few ms |
| Multi-blob gaps | Select whole clump (PO) | Closed `hitRegion` via an octagon close; outline still strokes exact per-blob rings |
| Overlapping entities | Cycle through all under the cursor (PO) | Whole-canopy hit areas make overlap common; single-best would hide occluded entities |
| Walk pose | Rest-pose v1 (PO) | Thick stroke hides sway; live-pose per-part is Tier-2 (rebuild-from-posed, not deform-union) |
| Outline z-order | Respect depth-sort (PO) | Draw in entity layer at the selected entity's anchorY; couples to Story A |
| CollisionShape vs silhouette | Silhouette wins; collider nav-only | "Actual filled shape" is the visual canopy, not the physical trunk |
