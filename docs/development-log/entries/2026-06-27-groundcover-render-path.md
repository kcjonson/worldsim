# 2026-06-27 - Groundcover: GPU-instanced grass from a procedural Lua asset

## Summary

Grass renders in the world again, this time as a first-class **groundcover** asset role with its own GPU-instanced render path. Shipped in [PR #238](https://github.com/kcjonson/worldsim/pull/238) (merged). This is Story 0 of the Living Environment epic: the dense-flora render path that wind depends on. Capacity was the open question, and it's answered with real in-game numbers: dense placement renders ~486k tufts / 13.2M triangles at 1.75ms / locked 120fps. The baked path could not hold that; instancing does it nearly free, so vertex-shader wind on top will be near-zero cost.

## Details

**Groundcover as a render role.** Added `AssetRole { WorldObject, Groundcover }` parsed from `<role>` (default WorldObject), validated like `assetType`; deleted the dead `RenderingTier` axis that routed nothing. `role == Groundcover` entities are skipped by the bake (the async worker returns a null template for them) and drawn through the instanced path instead.

**The grass look is a procedural Lua asset, not engine code.** Moved `assets/world/flora/GrassBlade` -> `assets/world/groundcover/Grass` (`Flora_GrassBlade` -> `Groundcover_Grass`), converted it from a static SVG to `assetType=procedural` with a `grass.lua` generator (a fan of tapered blades, dark base to lit tip, harmonized with the grass tile) and all look/feel in the Lua + XML `<params>`. Deleted the old SVG and the hardcoded C++ `GroundcoverMesh`. The engine generates `variantCount` distinct tufts via `AssetRegistry::buildMesh(defName, seed)` per seed (real generated geometry, not pre-baked) and GPU-instances them.

**The render path.** `EntityRenderer` routes `role==Groundcover` to an instanced path: lazily generate + upload the variant meshes, bucket placed tufts by a position hash into variants, `drawInstanced` per variant per visible chunk, and a tunable zoom LOD (`kGroundcoverLodCutoffPx` / `kGroundcoverHeightM`) that fades tufts out when blades shrink below a few px (the grass tile texture carries the appearance from there). Per-chunk instance buckets participate in an LRU like the baked path.

**Placement upright-rotation fix.** Placement was applying a random +/-0.3 rad rotation to every placed entity, which tilts upright billboards (trees, grass, bushes) so they read as falling over. Made placement rotation opt-in per asset via a new `<randomRotation>` field (degrees, default 0 = upright); the eight world-placed rocks (which have `variantCount=1`, so rotation was their only per-instance variety) opt in at 180.

**EntityRenderer decomposition.** The ~1000-line `EntityRenderer` god-class was split into per-path renderers — `BakedChunkRenderer`, `GroundcoverRenderer`, `InstancedEntityRenderer` (dynamic ECS), `BatchedEntityRenderer` (CPU fallback) — plus a shared `InstancingUniforms` helper and a `RenderContext` struct that bundles the per-frame inputs. `EntityRenderer` is now a thin orchestrator. Pure no-behavior-change refactor (identical draw calls / triangles / entity count / frame time).

**Review.** A local 5-agent review caught three real bugs, all fixed in the same PR: `variantCount` was nested in `<rendering>` but the parser reads it top-level, so grass was loading 1 variant not 24 (every tuft identical); the groundcover chunk cache had no LRU eviction (unbounded growth); and the variant meshes leaked on teardown. A follow-up cleanup pass (RenderContext struct, dedup helpers `computeVisibleBounds` / `TemplateMeshCache` / `isGroundcoverDef`, a single `kChunkWorldSize`) landed too, verified behavior-preserving by an adversarial diff + in-game check.

**Key files:** `libs/engine/assets/AssetDefinition.h`, `AssetRegistry.cpp`, `AssetValidator.cpp` (role + randomRotation); `libs/engine/world/rendering/*` (the five new renderer files + `RenderContext.h` / `TemplateMeshCache.{h,cpp}`); `libs/engine/assets/placement/{PlacementExecutor.cpp,AsyncChunkProcessor.h}`; `libs/renderer/shaders/includes/instancing.glsl` (groundcover deform uniforms, stubbed for wind); `assets/world/groundcover/Grass/{Grass.xml,grass.lua}`; eight `assets/world/misc/*` rocks.

## Related Documentation

- Plan: `.claude/plans/we-have-a-number-fuzzy-pearl.md` (grass capacity + groundcover plan), `.claude/plans/living-environment-rendering.md`
- Specboard: epic "Groundcover & dense-flora render path" (done); follow-up epic "Living Environment: reactive flora & water" (wind/deform/trampling/water); chore "Groundcover render perf" (upload-once buffers, warm variant meshes)

## Next Steps

- **M-A wind:** WindSystem + vertex-shader wind for the instanced groundcover and baked flora. The deform uniforms are already stubbed in `instancing.glsl` from the cursor-bend prototype; on this path it's near-zero added cost.
- **Groundcover render perf (deferred):** `drawInstanced` re-uploads static instance buffers every frame; add an upload-once path and warm the variant meshes at load before pushing density much higher.
