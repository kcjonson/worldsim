---
name: flora-asset
description: Generate engine-ready world flora assets (trees, bushes, plants, grasses) for world-sim, as either a procedural Lua generator + AssetDef XML or a layered static SVG + AssetDef XML, matching the existing RimWorld-style cel-shaded look. Use when asked to create, add, author, or generate a new tree, bush, shrub, plant, flower, or other vector world asset for the game.
---

# flora-asset

Authors a complete, drop-in world asset for world-sim: the art (procedural Lua generator or static SVG) plus its `AssetDef` XML, in the right folder, conforming to what the engine actually parses.

The art target is the game's own established look: cel-shaded, limited muted-earthy palette, strong readable silhouette, layered tones (dark rim -> base -> mid -> highlight). Match the existing assets, not generic flat-vector clip art.

## Source of truth

The parser wins over docs (docs have drifted). Before relying on any convention, confirm against:
- Lua API: `libs/engine/assets/lua/LuaEngine.cpp` (`registerBindings`)
- Canonical generators: `assets/shared/scripts/deciduous.lua`, `conifer.lua`, `palm.lua`
- Asset def parsing: `libs/engine/assets/AssetRegistry.cpp`
- Real examples: `assets/world/flora/OakTree/` (procedural), `assets/world/flora/GrassBlade/` (simple SVG)

Read the relevant `references/*.md` in this skill for the distilled, verified spec.

## Step 0 — pick the mode (one asset = one mode, never both)

An asset is `assetType=procedural` OR `assetType=simple`. The one-path rule applies: don't ship both an SVG and a Lua generator for the same asset.

- **Procedural (Lua)** — choose for anything that benefits from per-instance variation: trees, shrubs, most flora a player sees many copies of. Geometry and color vary per `variantIndex`. This is the workhorse.
- **Simple (SVG)** — choose for small static props or one-off plants where every instance looks identical (or where you pre-author a fixed handful of variants). Static hex fills, no runtime recolor.

When unsure, default to procedural for trees/bushes, simple for tiny ground detail.

## Asset packaging (script + sub-SVGs + XML)

An asset is a package in its folder: an XML def, plus either a generator script or an SVG, plus optional **vector sub-assets** (small authored SVGs the generator places). Don't procedurally approximate detail that's better drawn by hand: the apples on an apple tree, a flower's corolla, a distinctive leaf. Author those once as sub-SVGs (`apple.svg`) and have the Lua generator stamp them onto the canopy at random positions and scales per variant.

- Reference sub-SVGs from XML params (`<fruitSvg>apple.svg</fruitSvg>`), resolved relative to the asset folder.
- The generator loads and places them (see `references/lua-api.md` -> "Vector sub-assets").
- Script does layout and variation; SVGs do detail. Each to its strength.

## Life stages and seasons

Design each flora asset to read across its life and the year:
- **Growth stages** — seedling -> sapling -> mature (and dead/stump). Drive by a `growthStage` param that scales overall size, canopy fullness, and trunk thickness, and gates how many canopy layers and how much fruit appear.
- **Seasons** — spring (blossom), summer (full green), autumn (gold), winter (bare/leafless). Drive by a `season` param selecting the foliage palette and fruit/blossom presence (palettes in `references/style-and-view.md`).

Preview the whole matrix in the asset-manager (a stage x season grid) to judge the set at a glance. Engine status: generators don't yet read `growthStage`/`season` and the grid view isn't built; treat this as the authoring target and wire the params as they land.

## Step 1 — reuse before you author

Trees especially are usually **just an XML file**. The shared generators are parameterized:
1. Read the candidate shared script's `getFloat`/`getInt`/`getString` calls to discover its real params (don't trust a hardcoded list).
2. If an existing shared generator (`deciduous`, `conifer`, `palm`, or any new one) fits the growth form, write only the `AssetDef` XML pointing at `@shared/<script>.lua` with tuned `<params>`. No new code.
3. Only write a NEW shared generator when the growth form is genuinely new (e.g. a rounded shrub, a rosette, a cactus). Put it in `assets/shared/scripts/<form>.lua` so other assets can reuse it.

## Step 2 — author the art

Follow `references/style-and-view.md` for the palette, the per-subject view rules (trees = 3/4 top-down with trunk visible below the canopy; shrubs = mound seen slightly from above; ground plants/crops = top-down rosette), and the layered cel-shade construction.

- Procedural: write/extend the Lua generator per `references/lua-api.md` (exact API, coordinate system, blob/trunk/branch helpers, a complete template). Centered at origin, +Y is screen-down, units are meters. Colors are 0..1 floats. RNG is pre-seeded; just call `math.random()`.
- Simple: write the SVG per the layered template in `references/style-and-view.md`. Closed paths, ≥3 vertices, no self-intersection, solid fills, semantic groups (`#shadow`, `#trunk`, `#canopy`/`#body`).

## Step 3 — write the AssetDef XML

Per `references/xml-schema.md`. Folder + naming is load-bearing: `assets/world/flora/<Name>/<Name>.xml` (primary def filename MUST equal the folder name or it's skipped). SVG (simple) or `@shared/...lua` (procedural) referenced from inside the def. Fill in `placement` (biomes, distribution, groups, relationships), `capabilities` (harvest yields), `collision`, and `variantCount` for procedural.

## Step 4 — validate

Verify in the asset-manager, not by spawning in-game:
1. Build: `cmake --build build --config Debug --target asset-manager`
2. Run: `build/apps/asset-manager/Debug/asset-manager.exe` (debug server on port 8070)
3. Screenshot the new asset's thumbnail/detail; check the header's error/warning counts and the per-asset validation pane.
4. Hit Reload after edits (no restart). Close it when done: `curl "http://127.0.0.1:8070/api/control?action=exit"`.

Fix until zero validation errors and the art reads correctly at thumbnail size.

## Common failure modes

- Primary XML filename != folder name -> asset silently not loaded.
- Self-intersecting or <3-vertex paths -> silent bad tessellation, asset renders wrong or empty.
- Procedural script produces no paths -> logged warning, empty asset.
- Over-detailed art -> muddy at game zoom. Favor bold shapes and 3-4 tones per part.
- Adding a second code path for the same asset -> delete one; one mode per asset.

## References

- `references/style-and-view.md` — palette, view rules per subject, layered construction, SVG template, tessellation discipline.
- `references/lua-api.md` — exact Lua bindings, coordinate system, helper functions, full generator template.
- `references/xml-schema.md` — AssetDef field reference with procedural and simple worked examples.
