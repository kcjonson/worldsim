# Lua Scripting for Procedural Asset Generation

**Date:** 2025-12-01

**Summary:**
Implemented Phase 2 of the Asset System: Lua scripting for procedural asset generation. Created a deciduous tree generator demonstrating 3/4 top-down view (Rimworld style) with extensive visual variation across 40 unique trees.

**What Was Accomplished:**
- Integrated sol2 (Lua 5.4.7) into vcpkg dependencies
- Created `LuaEngine` class for script execution with seeded randomness
- Created `LuaGenerator` implementing `IAssetGenerator` interface
- Exposed Path API to Lua (`Path:new()`, `path:addVertex()`, `path:setColor()`, `path:close()`, `asset:addPath()`)
- Created deciduous.lua tree generator with:
  - Canopy with variable layers (1-3 back layers, 1-3 front clusters)
  - Tapered trunk with dark edge highlights
  - Branches from various positions on trunk (0-70% down)
  - Shadow layer for depth
  - Highlight blob for lighting
  - Multiplicative variation (60-140%) for distinct trees
  - `canopyStretch` factor for tall vs wide trees
- Created TreeScene demo rendering 40 trees in 8x5 grid

**Files Created:**
- `libs/engine/assets/lua/LuaEngine.h/.cpp` - Lua script executor
- `libs/engine/assets/lua/LuaGenerator.h/.cpp` - IAssetGenerator implementation
- `assets/generators/trees/deciduous.lua` - Procedural tree generator
- `assets/definitions/trees/tree.xml` - Asset definition with scriptPath
- `apps/ui-sandbox/scenes/TreeScene.cpp` - Demo scene

**Files Modified:**
- `vcpkg.json` - Added sol2 dependency
- `libs/engine/CMakeLists.txt` - Added Lua library linking
- `libs/engine/assets/AssetRegistry.cpp` - scriptPath support in XML parser
- `libs/engine/assets/GeneratorRegistry.cpp` - LuaGenerator registration

**Technical Decisions:**
1. **sol2 over LuaJIT**: sol2 provides cleaner C++ bindings with type safety; Lua 5.4 is sufficient for our needs
2. **Seed-based randomness**: C++ seeds Lua's `math.randomseed()` ensuring deterministic generation from same seed
3. **Path-based API**: Lua scripts build `VectorPath` objects via `Path` usertype, mirroring C++ `IAssetGenerator` output
4. **Multiplicative variation**: Using `* (0.6 + random * 0.8)` instead of additive offsets creates more visually distinct trees
5. **Draw order layering**: Back canopy → branches → front canopy ensures branches are visible but partially covered for depth

**Lessons Learned:**
- Coordinate system confusion (-Y is "up" in 3/4 view) caused branch disconnection - fixed by understanding canopy vs trunk positioning
- Branches covered by canopy layers - fixed by interleaving draw order
- Additive variation (±20%) was too subtle for visual distinction - multiplicative (60-140%) works better

**PR:** https://github.com/kcjonson/worldsim/pull/41

**Next Steps:**
- Phase 3: Variant caching (binary cache for pre-generated variants)
- Phase 4: Full tree demo (Weber & Penn branching, mixed flora scene)



