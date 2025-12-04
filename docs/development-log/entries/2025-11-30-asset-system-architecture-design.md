# Asset System Architecture Design

**Date:** 2025-11-30

**Summary:**
Designed comprehensive asset system architecture supporting both simple (designer-authored SVG) and procedural (Lua-generated) assets with moddability as a core requirement. This addresses the broader question of how game assets should be structured on disk.

**Design Decisions:**

1. **Simple vs Procedural Assets**
   - **Simple**: Hand-drawn SVG files (flowers, mushrooms, rocks). Load once, tessellate, render via GPU instancing with per-instance variation (color, scale, rotation).
   - **Procedural**: Lua script + parameters that generate vector graphics algorithmically (trees, bushes). Pre-generate N variants at load time, cache to disk.

2. **Data-Driven Definitions (inspired by RimWorld)**
   - XML asset definitions separate data from logic
   - Definitions support inheritance (`ParentDef`) for DRY
   - Later-loaded definitions override earlier ones (mod support)
   - Example: `<defName>Tree_Oak</defName>` with `<generator><script>deciduous.lua</script></generator>`

3. **Lua Scripting for Procedural Generation**
   - Lua chosen for: embeddable (~200KB), sandboxable, fast (LuaJIT), industry standard
   - Scripts define `generate(params, rng)` function returning `VectorAsset`
   - API exposes: `VectorAsset`, `VectorPath`, `Color`, `Vec2`, `Math`, seeded RNG
   - Sandbox restricts file I/O, OS access, network for mod safety

4. **Pre-Generation Strategy**
   - Procedural assets generate N variants at load time (e.g., 200 unique oaks)
   - Variants cached to binary files for fast reload
   - Variant selection by position seed ensures determinism
   - Zero generation cost during gameplay

**Research Findings:**
- RimWorld's Def system proves highly moddable and extensible
- Weber & Penn algorithm (SpeedTree) preferred for tree branching over L-systems
- Similar patterns applicable to: terrain features, buildings, weather effects, creatures, items

**Files Created:**
- `docs/technical/asset-system/README.md` - Architecture overview
- `docs/technical/asset-system/asset-definitions.md` - XML schema, inheritance, modding
- `docs/technical/asset-system/lua-scripting-api.md` - Lua API reference with examples

**Files Modified:**
- `docs/technical/INDEX.md` - Added Asset System section
- `docs/status.md` - Added Asset System Architecture epic

**Relationship to Animation Performance:**
This work complements the animation performance epic. The Asset System handles **what** assets exist and how they're generated, while the tiered renderer handles **how** they're rendered efficiently. Pre-generation at load time means procedural trees don't need runtime tessellation - they select from cached variants.

**Next Steps:**
Phase 1 implementation: Asset Registry, XML parser, simple asset loader, renderer integration.



