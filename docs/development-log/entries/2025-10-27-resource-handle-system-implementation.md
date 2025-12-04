# Resource Handle System Implementation

**Date:** 2025-10-27

**Foundational Engine Pattern Complete:**

Implemented a production-ready resource handle system for safe asset management. Handles provide safe references to resources (textures, meshes, SVG assets) with automatic stale reference detection via generation counters.

**Implementation:**

Created header-only handle system in `libs/renderer/resources/` with two components:

1. **ResourceHandle** (`resource_handle.h`) - Core 32-bit handle type
   - 16-bit index (lower bits) + 16-bit generation (upper bits)
   - Packing/unpacking methods: `GetIndex()`, `GetGeneration()`
   - Factory methods: `Make()`, `Invalid()`
   - Comparison operators for equality checks
   - Type-safe aliases: `TextureHandle`, `MeshHandle`, `SVGAssetHandle`

2. **ResourceManager<T>** (`resource_manager.h`) - Generic template manager
   - Free list for index recycling (O(1) allocation/deallocation)
   - Generation tracking prevents stale handle access
   - `Allocate()` - Get new handle, reuses freed slots
   - `Free()` - Increment generation, add to free list
   - `Get()` - Retrieve resource with validation
   - Capacity: 65,536 resources max per type (16-bit index)

**Key Features:**
- Header-only (no build system changes needed)
- Generation validation (detects stale/dangling references)
- O(1) allocation, free, and lookup
- Compact (4 bytes vs 8-byte pointer)
- Serializable (save/load as 32-bit value)
- Type-safe via templates

**Testing:**

Created comprehensive test suite in `apps/ui-sandbox/demos/handle_demo.cpp`:

**Test Results:**
- ✅ Basic Allocation: Allocate 3 handles, set/get data correctly
- ✅ Free List Reuse: Freed indices 1,2,3 → reallocated as 3,2 (LIFO, gen incremented)
- ✅ Stale Handle Detection: Old handle returns null after free/realloc
- ✅ Handle Validation: Invalid handles, out-of-range indices handled safely
- ✅ Comparison Operators: Equality/inequality work correctly

**Use Cases:**
- Texture management with hot-reloading
- SVG asset caching
- Mesh resource pooling
- Any resource with potential lifetime issues

**Design Decisions:**

**Generation prevents stale access:** When resource freed, generation increments. Old handles with old generation return null on Get().

**Free list LIFO order:** Reuses most recently freed slot first. Improves cache locality for resources with similar lifetimes.

**Not thread-safe:** Each thread should use separate manager instance or add external synchronization. Documented as constraint.

**65,536 resource limit:** 16-bit index allows up to 65,536 resources per type. Can increase index bits if needed, trading generation bits.

**Files Created:**
- `libs/renderer/resources/resource_handle.h` - Handle type (75 lines, header-only)
- `libs/renderer/resources/resource_manager.h` - Manager template (115 lines, header-only)
- `apps/ui-sandbox/demos/handle_demo.cpp` - Test suite (270 lines)

**Files Modified:**
- `apps/ui-sandbox/CMakeLists.txt` - Use handle_demo for testing

**Integration:**
- All tests run automatically on ui-sandbox startup
- Console output shows detailed validation results
- No build system changes needed (header-only)

**Benefits:**
- Prevents dangling pointer bugs
- Enables hot-reloading (reload asset, handle stays valid)
- Half the memory of raw pointers (4 vs 8 bytes)
- Serialization-friendly for save files

**Next Steps:**
Vector graphics system can now use handles for SVG assets and rasterized texture caching.


