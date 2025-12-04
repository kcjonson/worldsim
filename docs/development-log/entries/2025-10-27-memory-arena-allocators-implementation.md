# Memory Arena Allocators Implementation

**Date:** 2025-10-27

**Foundational Engine Pattern Complete:**

Implemented a production-ready memory arena allocator system for fast temporary allocations. Arenas provide 10-100× faster allocation/deallocation compared to standard malloc/new by using simple pointer-bump allocation and bulk deallocation.

**Implementation:**

Created header-only arena system in `libs/foundation/memory/arena.h` with three classes:

1. **Arena** - Core linear allocator
   - Allocates large buffer once via `malloc()`
   - Bump-pointer allocation with alignment support
   - Type-safe templated methods: `Allocate<T>()`, `AllocateArray<T>(count)`
   - Instant reset via pointer reset (no per-object cleanup)
   - Capacity tracking: `GetUsed()`, `GetSize()`, `GetRemaining()`

2. **FrameArena** - Per-frame wrapper
   - Designed for per-frame temporary data
   - `ResetFrame()` method for end-of-frame cleanup
   - Same allocation interface as Arena

3. **ScopedArena** - RAII wrapper
   - Saves checkpoint on construction
   - Automatically resets arena on destruction
   - For scoped temporary allocations

**Key Features:**
- Header-only (no build system changes needed)
- Non-copyable (deleted copy constructor/assignment)
- Alignment-aware (respects `alignof(T)` for all types)
- Debug-friendly (`assert()` on out-of-memory)
- ~170 lines total

**Performance Testing:**

Created comprehensive test suite in `apps/ui-sandbox/demos/arena_demo.cpp`:

**Performance Test Results:**
- Arena: 70 microseconds for 10,000 Vec2 allocations
- Standard: 652μs allocation + 342μs deallocation = 994μs total
- **Speedup: 14.2× faster than standard allocation**
- Reset: Instant (< 1 microsecond for any size)

**Validation Tests:**
- ✅ Alignment Test: All alignments correct (1, 4, 8, 16-byte)
- ✅ Capacity Test: Correctly tracked 800 bytes for 100 uint64_t allocations
- ✅ Reset Test: Instant reset to 0 bytes used
- ✅ Scoped Test: RAII automatic reset on scope exit (80 bytes → 0 bytes)

**Use Cases:**
- Per-frame temporary data (UI layout, debug rendering)
- Chunk generation scratch space (noise buffers, tile processing)
- Vector graphics tessellation (upcoming)
- Algorithm temporary buffers
- String building and formatting

**Design Decisions:**

**Does NOT call destructors:** Arenas are for POD types or manual cleanup. Documented as a constraint.

**Not thread-safe:** Each thread should use its own arena. Documented as a constraint.

**Assert on out-of-memory:** Fail-fast in debug builds instead of graceful degradation. Forces proper arena sizing during development.

**Files Created:**
- `libs/foundation/memory/arena.h` - Complete implementation (170 lines, header-only)
- `apps/ui-sandbox/demos/arena_demo.cpp` - Comprehensive test suite (208 lines)

**Files Modified:**
- `apps/ui-sandbox/CMakeLists.txt` - Switched from shapes_demo to arena_demo for testing

**Integration:**
- All tests run automatically on ui-sandbox startup
- Console output shows performance comparison and test results
- No build system changes needed (header-only)

**Next Engine Pattern:**
Resource handles (32-bit IDs with generation counter) for safe asset management.


