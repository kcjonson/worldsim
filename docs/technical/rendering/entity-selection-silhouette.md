# Entity Selection Silhouette — Technical Spec

**Status:** Design
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
5. **Applies to all SVG-asset-backed selectables:** world entities (trees/flora/rocks), colonists, crafting stations, and furniture (including packaged). Construction selectables (walls, openings, foundations, rooms) already hit-test and outline on exact topology geometry and are left untouched.

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
AssetRegistry::getSilhouette(defName) ──► AssetSilhouette { outerRings (local mm),
   (lazy, on bake worker)                    hitRegion (closed, for whole-clump),
        │                                     boundsCenterMeters, valid }
        │  built from contour rings (NOT triangle boundary)
        ▼
geometry::silhouetteOfRings(rings) ──► outer CCW loops, holes filled, disjoint preserved
        │
        ├──► outline:   assetInstanceTransform(local→world→screen), stroke thick, entity-layer z
        └──► hit-test:  assetInstanceTransform⁻¹(click→local mm), pointInPolygon
```

The silhouette is computed **once per resolved `defName`**, in template-local integer millimetres, cached beside the tessellated template. A single shared transform maps local↔world for both the static (baked) and dynamic (ECS) render conventions, so the outline, the hit-test, and the renderer never disagree.

## 4. The silhouette primitive

`libs/geometry/silhouette/Silhouette.h` (implemented, PR #250): `silhouetteOfRings(rings, res)` and `silhouetteOfTriangles(triangleVerts, res, closeRadiusPx)` — both raster; `getSilhouette` uses the **triangle** entry point (see below).

**Raster, not the exact planar arrangement.** The first cut proposed unioning the asset's contour rings via `buildArrangement` + `HalfEdge` + a keep-CCW/drop-CW/prune-nested post-pass. Reading `extractFaces` killed that: for **disconnected** input (a donut, nested island, scattered blobs — exactly what this feature is for) the enumerated face cycles geometrically *overlap*, and a cycle's representative point can land inside a nested hole, dropping the silhouette entirely (`RingBoolean`'s documented disconnected-input unsoundness, `RingBoolean.cpp:122-128`). Fixing it exactly needs true-unbounded-face identification or a containment forest — research-grade for a selection highlight. Raster is robust for every hard case by construction and is the same morphological substrate the whole-clump hit area needs (§7).

**Source: the tessellated mesh triangles, not the fill contours.** `getSilhouette` rasterizes the asset's **tessellated template mesh** (`TessellatedMesh`) triangles, so the silhouette is the *actual rendered footprint* — fills AND stroke bands. This matters: a reed's stem is a `stroke` (`fill="none"`), so a fill-contour silhouette misses it and fragments the reed into disjoint blobs (seed head vs leaf fan); rasterizing the mesh reconnects the whole reed, and stroke-only assets (e.g. `PlantFiber`) get a real strand silhouette instead of an AABB fallback. The pipeline:
1. Integer bbox over the triangle vertices; ceil-div pixel size (≥1 mm) from `targetResolution`; a padded, guaranteed-empty border (`closeRadiusPx + 1` cells).
2. **Coverage:** each pixel is covered iff its centre is inside **any** triangle (three exact `orientation` tests, boundary-inclusive so adjacent triangles tile gaplessly). Degenerate triangles skipped; iterate each triangle over its own bbox. (`silhouetteOfRings` instead computes coverage by nonzero winding over rings — used only by the geometry fixtures.)
3. **Optional morphological close** (`closeRadiusPx > 0`, box dilate-then-erode) bridges gaps ≤ `2·closeRadiusPx` between disjoint blobs without growing the outer boundary — this is the whole-clump `hitRegion` (§7).
4. **Fill holes:** 4-connected exterior flood-fill from the border; everything unreachable (covered *or* an enclosed void) is filled.
5. **Trace + simplify:** directed unit boundary edges (filled pixel on the left → CCW loops), chained, saddles split by turn rank, one loop per component (disjoint blobs → separate rings), `simplifyRing`, `ensureCounterClockwise`.

Blockiness is bounded by `targetResolution` and invisible under a thick stroke. Compute-once per `defName`, cached, off-thread — no runtime cost. The rings double as the later shadow-caster geometry. The shared grid/mask/trace tail (`makeGrid` + `maskToRings`) backs both entry points.

**Fixtures (all green):** 8 `silhouetteOfRings` cases (square; donut → hole filled; disjoint → 2 rings; concave L; nested solid → no spurious inner; overlap → merged; figure-eight; empty → `{}`) + 4 `silhouetteOfTriangles` cases (single triangle; two-triangle square; triangulated annulus → hole filled; disjoint blobs → 2 rings at close 0, bridged to 1 with a close radius). Story B adds a tessellator **coverage-oracle** test on real assets (silhouette encloses the tessellated fill; `libs/geometry` can't link the renderer, so it lives there).

**Degenerate assets.** Only a truly empty mesh (0 triangles, doesn't render) yields no silhouette (`valid=false`); stroke-only assets now produce real geometry, so no AABB fallback is needed.

## 5. Per-`defName` cache

```cpp
struct AssetSilhouette {
    std::vector<geometry::Ring> rings;        // local mm — crisp; used for the outline + shadows
    std::vector<geometry::Ring> hitRegion;    // local mm — closed (whole-clump); used for hit-testing
    glm::vec2 boundsCenterMeters;             // == the render centerOffset, stored to stay drift-free
    bool valid;
};
```

Stored in `AssetRegistry` in a `unordered_map<string, AssetSilhouette>` guarded by its own mutex, mirroring the `getMotion` cache (own mutex, heavy raster off-lock, short lock to check/insert). `getSilhouette(defName)` ensures `getTemplate` (so the mesh is materialized), rasterizes its triangles for `rings` (close 0) and `hitRegion` (small close), caches. Key = resolved `defName`, so directional colonist variants (`_down/_up/_left/_right`) each get their own rest-pose silhouette. Safe to call from the chunk-bake worker (like `getTemplate`). Invalidated with `templateCache` on `clear()` / `clearDefinitions()`. `boundsCenterMeters` = `computeBounds(template)` centre == the dynamic render `-centerOffset` (drift-free). One template per `defName`, so one silhouette serves every instance regardless of position/rotation/scale/tint.

For a single connected asset, `hitRegion ≈ rings`. They diverge for multi-blob assets, where the close bridges the gaps (§7).

## 6. Shared instance transform (mandatory, one-path)

The #1 shared risk is the hit-test inverse or the outline forward-map drifting from what the renderer draws — the same collider-drift failure class the `SvgMeterFrame` comments already warn about. There are two conventions, and they must live in exactly one place:

- **Static / baked** (`BakedEntityMesh`): `world = R(rot)·(local·scale) + position`, **no centering**.
- **Dynamic / ECS** (`DynamicEntityRenderSystem`): `world = local·scale + position + centerOffset`, **rotation forced 0**, where `centerOffset = -(min+max)/2`. Note `getTemplate` returns `nullptr` for a 0-shape asset and `centerOffset` silently stays 0 — the inverse must reproduce that exact behavior.

`assetInstanceTransform` provides forward (local→world) and inverse (world→local mm) for both conventions, consumed by `BakedEntityMesh`, `DynamicEntityRenderSystem`, the outline, and the hit-test. The duplicated `centerOffset` math in `DynamicEntityRenderSystem` is deleted (one source of truth). A `packagedLayout` helper is extracted the same way (crate + shrunk-item offsets/scale) so the packaged composite matches the sprite. Round-trip tests (`world→local→world`) per convention, including the nullptr-template path and the `FacingDirection` suffix.

## 7. Whole-clump hit area (PO decision)

A click in the bare gap between disjoint blobs of one asset **selects** it. The exact `outerRings` preserve disjoint blobs (so a click *outside* every blob would miss), so the hit area is a **closed** region: rasterize the outer rings, morphological-close by a bounded radius (bridges inter-blob gaps up to the threshold), flood-fill, marching-squares → `hitRegion`. Gated to multi-blob assets — for a single connected blob the close is a no-op and `hitRegion == outerRings`.

The **outline still strokes the exact `outerRings`** (each visible bloom gets a precise outline, so the highlight reads as "around the 2D visual"), while the **hit-test uses `hitRegion`** (forgiving clicks). Precise-looking selection, generous clicking. This is the one place `outerRings` and `hitRegion` intentionally differ.

## 8. Precise hit-testing

Broad phase stays a cheap cull but must be widened: today's single-chunk `queryRadius(2m)` would cull a click on a big canopy far from its trunk anchor before the precise test runs, and a tree whose anchor sits in an adjacent chunk but whose canopy overhangs the clicked point lives in the neighbor's index. So: size the broad-phase radius from the **registry's max silhouette half-extent** (per-`defName` max-extent keeps small assets tight) and query the click chunk **plus its 8 neighbors**.

Narrow phase replaces the centroid `dist < kSelectionRadius` accept for colonist / station / furniture / world-entity with `pointInPolygon(assetInstanceTransform⁻¹(click), hitRegion)`. Packaged furniture tests crate-ring OR item-ring at their `packagedLayout` transforms. `kSelectionRadius` survives **only** as the broad-phase radius; the accept comparisons and the generic `kIndicatorRadius` circle are deleted (one-path).

**Overlapping entities cycle (PO decision).** With canopy-sized hit areas, overlapping tree canopies are the norm. `gatherCandidates` pushes **every** world entity whose `hitRegion` contains the click (not just the closest anchor), ordered topmost-first so repeated clicks step front-to-back through the existing cycle machinery. "Topmost" = entity draw/sort order (the depth-sort anchorY), falling back to smallest-area-containing on ties. `candidateKey` (currently quantized position) is extended to disambiguate co-located entities.

## 9. Thick outline rendering

Stroke each transformed `outerRing` in screen space with `Primitives::drawLine` at `kOutlineWidthPx`, plus a `drawCircle` dot of radius `width/2` at each vertex for round joins (an N-way union has sharp reflex corners a miter would spike; round is cheap and needs no general polygon-offset primitive). Gold `Color(1,0.85,0,~0.9)`, reusing the exact ring-walk the foundation/opening/wall indicators already use.

- **Thickness:** constant **screen-space** pixels (zoom-independent) with **min/max px clamps**, and the vertex join-dots gated on `width` vs edge length so a tiny flower doesn't blob into a solid dot and a big tree's outline still reads as thick.
- **Z-order — respect depth-sort (PO decision).** The outline is **not** a flat top pass. It draws in the entity render layer at the *selected entity's* sort key (anchorY, from world-depth-sorting Story A), so a nearer tree correctly occludes a farther selection's outline. This couples the epic to Story A / PR #248.
- **`WorldEntitySelection` branch (new).** Re-resolve the live `PlacedEntity` by `defName`+`position` each frame to recover rotation/scale; if the entity was felled/moved (chopping triggers a nav rebuild), the selection is stale — draw nothing.
- **Packaged:** union the crate + item rings via `RingBoolean::unionRings` (its exact 2-ring reference use) into one clean outline; on reject (item floats above the crate, disjoint) stroke both.

## 10. Animated colonists (PO decision: rest-pose for v1)

The cached silhouette is rest pose. While walking, limbs sway a few degrees; the thick stroke hides the drift and the outline tracks the colonist's live `Position` exactly (translation is exact, only intra-body sway is unreflected). Hit-test accepts the rest-pose region (deltas within click slop). This keeps one cached silhouette per `defName` and no per-frame union.

Deferred (Tier-2, documented not built): a per-frame posed outline for the single selected colonist by re-running the clip on the posed contours — **not** by deforming the unioned ring (Steiner vertices belong to no `MeshPart`, so deforming the union tears at seams; a faithful posed outline must rebuild from posed contours, cheap at n=1).

## 11. Stories & tasks

**Story A — silhouette geometry primitive** (`libs/geometry`) — DONE, PR #250
- A1 `silhouetteOfRings` (winding coverage) + `silhouetteOfTriangles` (triangle coverage, + `closeRadiusPx` morphological close): shared `makeGrid` + `maskToRings` (exterior flood-fill → directed boundary trace, filled-on-left → CCW → saddle-split by turn rank → `simplifyRing`). Robust for disconnected/nested/self-intersecting input.
- A2 Unit tests: 8 ring fixtures + 4 triangle fixtures (§4). Tessellator coverage-oracle cross-check in Story B (renderer not linkable from `libs/geometry`).

**Story B — per-`defName` silhouette cache** (`AssetRegistry`) — DONE
- B1 `AssetSilhouette{rings, hitRegion, boundsCenterMeters, valid}` + `silhouetteCache` + mutex + `getSilhouette(defName)`; `getMotion`-style (compute off-lock). Rasterize the **template mesh triangles** (quantized to mm) via `silhouetteOfTriangles` — `rings` at close 0, `hitRegion` at a small close (D3). Captures stroke bands; no fill-contour path.
- B2 Only a truly empty mesh yields no silhouette (`valid=false`); stroke-only assets (`plant_fiber.svg`) now produce a real strand silhouette, so the AABB fallback was removed. `clear()`/`clearDefinitions()` wiring.

**Story C — shared instance transform** (extract, one-path)
- C1 `assetInstanceTransform` forward+inverse (static uncentered; dynamic centered rot-0; nullptr-template/0-offset; `FacingDirection` suffix), consumed by `BakedEntityMesh`, `DynamicEntityRenderSystem`, outline, hit-test. Delete the duplicated `centerOffset` math.
- C2 `packagedLayout` helper extracted from `DynamicEntityRenderSystem`, consumed by render + selection.
- C3 `world→local→world` round-trip tests per convention incl. nullptr-template.

**Story D — precise hit-testing** (`SelectionSystem`)
- D1 Widen broad phase: query radius from registry max silhouette half-extent (per-`defName`), query click chunk + 8 neighbors.
- D2 Replace centroid accept with `pointInPolygon(click→local, hitRegion)` for colonist/station/furniture/world-entity; packaged = crate OR item.
- D3 Whole-clump `hitRegion` (raster close, §7) for multi-blob assets.
- D4 Overlapping-entity cycling: push all containing entities, topmost-first ordering, extend `candidateKey`.
- D5 Delete centroid accept comparisons + `kIndicatorRadius`; demote `kSelectionRadius` to broad-phase only; grep stragglers.

**Story E — thick silhouette outline** (`SelectionSystem::renderIndicator`)
- E1 New `WorldEntitySelection` branch (re-resolve live `PlacedEntity`; draw nothing if stale).
- E2 Switch colonist/station/furniture to stroke `outerRings`: local→world→screen, `drawLine` at clamped `kOutlineWidthPx` + width-gated round-join dots; delete the `kIndicatorRadius` circle.
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

## 13. Decisions log

| Fork | Decision | Rationale |
|------|----------|-----------|
| Silhouette computation | **Raster of the tessellated mesh triangles** → flood-fill → boundary trace | The exact planar-arrangement path is unsound on nested/disjoint input (overlapping face cycles; rep point lands in a hole and drops the silhouette — confirmed in `extractFaces`). Raster is robust by construction. Sourcing the *mesh triangles* (not fill contours) captures the actual rendered footprint incl. stroke bands, so strokes (a reed's stem) are in the silhouette and stroke-only assets need no AABB fallback |
| Multi-blob gaps | Select whole clump (PO) | Closed `hitRegion` via raster close; outline still strokes exact per-blob rings |
| Overlapping entities | Cycle through all under the cursor (PO) | Whole-canopy hit areas make overlap common; single-best would hide occluded entities |
| Walk pose | Rest-pose v1 (PO) | Thick stroke hides sway; live-pose per-part is Tier-2 (rebuild-from-posed, not deform-union) |
| Outline z-order | Respect depth-sort (PO) | Draw in entity layer at the selected entity's anchorY; couples to Story A |
| CollisionShape vs silhouette | Silhouette wins; collider nav-only | "Actual filled shape" is the visual canopy, not the physical trunk |
