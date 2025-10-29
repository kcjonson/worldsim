# Component Storage Patterns: shared_ptr vs Value Semantics

Created: 2025-10-29
Status: Research & Analysis

## Overview

This document analyzes different approaches to storing UI components and game objects in C++ game engines, comparing Object-Oriented patterns (`shared_ptr` in containers) with Data-Oriented patterns (value semantics, contiguous arrays).

**Context**: During colonysim→worldsim integration analysis, we identified a fundamental difference in memory patterns. This document explores the trade-offs to inform architectural decisions.

---

## Pattern A: Object-Oriented (shared_ptr in containers)

### Description

Store polymorphic objects as `shared_ptr` in vectors:

```cpp
class Layer {
protected:
    std::vector<std::shared_ptr<Layer>> children;
};

// Usage
auto layer = std::make_shared<Layer>();
auto button = std::make_shared<Button>();  // Button : public Layer
layer->addChild(button);  // Stores as shared_ptr<Layer>
```

### How It Works

**Memory Layout**:
```
Vector (Layer::children):
┌──────┬──────┬──────┬──────┐
│ ptr1 │ ptr2 │ ptr3 │ ptr4 │  ← Vector stores pointers (contiguous)
└──┬───┴──┬───┴──┬───┴──┬───┘
   │      │      │      │
   ▼      ▼      ▼      ▼
 Heap   Heap   Heap   Heap     ← Actual objects (scattered)
```

**Polymorphism**: Via virtual functions
```cpp
class Layer {
public:
    virtual void Render() = 0;  // Override in derived classes
};

class Button : public Layer {
public:
    void Render() override { /* ... */ }
};

// Call virtual function through pointer
for (auto& child : children) {
    child->Render();  // Virtual dispatch
}
```

### Advantages

✅ **Works** - Proven pattern (colonysim, most game engines)
✅ **Familiar** - Standard OOP approach
✅ **Easy polymorphism** - Virtual functions just work
✅ **Uniform containers** - One vector type for all derived classes
✅ **Shared ownership** - Multiple owners possible (if needed)
✅ **Stable pointers** - Pointers/references remain valid when vector grows

### Disadvantages

❌ **Heap allocation overhead** - Every object requires `new` / `make_shared`
❌ **Cache-unfriendly** - Pointer chasing, objects scattered in memory
❌ **Reference counting cost** - Atomic inc/dec on every copy/assignment
❌ **Indirection cost** - Extra pointer dereference per access
❌ **Memory fragmentation** - Objects allocated at different times

### Performance Data

From colonysim example:

**Typical UI scene** (main menu):
```cpp
std::shared_ptr<Layer> backgroundLayer;
std::shared_ptr<Layer> buttonLayer;
std::shared_ptr<Rectangle> menuBackground;
std::shared_ptr<Button> newColonyButton;
std::shared_ptr<Button> loadColonyButton;
std::shared_ptr<Button> settingsButton;
// ... etc
```

**Per component**:
- Heap allocation: ~1 µs
- shared_ptr control block: 16-24 bytes
- Ref count ops: Atomic (slow on multi-core)

**Total for 100-component UI**:
- Allocation time: ~100 µs (acceptable for setup)
- Extra memory: ~2 KB (acceptable)
- Render traversal: Pointer chasing per node

**Measured performance** (colonysim):
- Main menu (20 components): 60 FPS ✅
- Complex UI (100+ components): 60 FPS ✅
- **Conclusion**: Adequate for UI workloads

---

## Pattern B: Data-Oriented (Value Semantics)

### Description

Store objects by value in type-specific vectors:

```cpp
struct RenderSystem {
    std::vector<Rectangle> rectangles;  // Value semantics
    std::vector<Circle> circles;
    std::vector<Text> texts;
};

// Usage
rectangles.push_back(Rectangle{/* ... */});  // No pointers!
```

### How It Works

**Memory Layout**:
```
Vector (rectangles):
┌──────────┬──────────┬──────────┬──────────┐
│ Rect #1  │ Rect #2  │ Rect #3  │ Rect #4  │  ← Objects inline (contiguous!)
└──────────┴──────────┴──────────┴──────────┘
All data packed together in memory
```

**Polymorphism**: Via `std::variant` or type erasure
```cpp
using ShapeVariant = std::variant<Rectangle, Circle, Text>;
std::vector<ShapeVariant> shapes;

// Render via std::visit
for (auto& shape : shapes) {
    std::visit([](auto& s) { s.Render(); }, shape);
}
```

### Advantages

✅ **Cache-friendly** - Sequential memory access, prefetcher loves this
✅ **No heap allocations** - Objects live in vector's internal array
✅ **No ref-counting** - Value semantics, clear ownership
✅ **Compact** - No pointer overhead per object
✅ **SIMD-friendly** - Contiguous data enables vectorization

### Disadvantages

❌ **More complex** - Requires variant or type erasure
❌ **Type-specific containers** - Need separate vector per type (or variant)
❌ **Move cost** - Objects move when vector grows (need move constructors)
❌ **Pointer invalidation** - Pointers/references invalidated on resize
❌ **Less familiar** - Not standard OOP pattern

### Performance Data

From `/docs/research/modern_rendering_architecture.md`:

**Cache miss costs** (line 15):
> "Cache misses cost 50-75x more than L1 cache hits, making memory layout the dominant performance factor"

**Structure of Arrays example** (line 17):
> "This 62% reduction in cache traffic translates to 2-3x real performance improvements measured in production particle systems"

**Overall improvement** (line 125):
> "50x improvements over pointer-based approaches through this simple technique"

**Unity Megacity demo** (line 19):
> "100,000+ dynamic entities rendering at 30-60 FPS on mobile hardware, with 4.5 million building blocks"

**Conclusion**: Massive wins at scale (10,000+ objects), moderate wins for smaller counts

---

## Pattern C: Hybrid (Best of Both Worlds)

### Description

Use OOP for organization, data-oriented for hot paths:

```cpp
// GameObject hierarchy (OOP - retained)
class Button {
    glm::vec2 m_position;
    glm::vec2 m_size;
    std::string m_label;

public:
    void Render() {
        // Don't store render data here!
        // Submit to command buffer instead
        Primitives::DrawRect({
            .bounds = {m_position.x, m_position.y, m_size.x, m_size.y},
            .style = m_style
        });
    }
};

// Rendering layer (data-oriented - command buffer)
class PrimitiveBatcher {
    std::vector<Vertex> m_vertices;      // Contiguous!
    std::vector<uint32_t> m_indices;     // Contiguous!

    void Flush() {
        // All data packed, ready for GPU
        glBufferData(GL_ARRAY_BUFFER, m_vertices.size() * sizeof(Vertex),
                     m_vertices.data(), GL_DYNAMIC_DRAW);
        glDrawElements(GL_TRIANGLES, m_indices.size(), GL_UNSIGNED_INT, 0);
    }
};
```

### Advantages

✅ **Familiar API** - OOP for game objects
✅ **Performance where it matters** - Data-oriented for rendering
✅ **Best of both worlds** - Organization + cache efficiency
✅ **Incremental adoption** - Can start with OOP, optimize hot paths
✅ **Industry standard** - Unity, Unreal, Godot all use this

### Disadvantages

⚠️ **Two systems** - Logical (OOP) and performance (DOD)
⚠️ **Synchronization** - Need to keep them in sync

### Research Support

From `/docs/research/modern_rendering_architecture.md` (line 218):
> "production engines universally adopted hybrid architectures because purity fails in practice"

**Pattern in engines**:
- **Unity**: GameObject (OOP) → DOTS (DOD) → Rendering command buffer
- **Unreal**: Actor (OOP) → Rendering proxy (DOD) → RHI commands
- **Godot**: Node (OOP) → RenderingServer (DOD) → VisualServer commands

---

## Worldsim's Decision: Pragmatic Hybrid

### For Colonysim UI Integration

**Choice**: Pattern A (shared_ptr) for Layer 4, Pattern C for overall architecture

**Rationale**:

**Layer 4 (UI Components)**:
```cpp
// Use Pattern A (OOP with shared_ptr)
class Layer {
    std::vector<std::shared_ptr<Layer>> m_children;  // Start here
};
```

**Why**:
- Proven functional (colonysim works)
- Faster to implement (less refactoring)
- UI workload is small (<1000 components typically)
- Performance is acceptable (colonysim hits 60 FPS)

**Layer 3 + 2 (Rendering)**:
```cpp
// Use Pattern B (data-oriented command buffer)
class PrimitiveBatcher {
    std::vector<Vertex> m_vertices;   // Contiguous, cache-friendly
    std::vector<uint32_t> m_indices;
};
```

**Why**:
- Rendering is hot path (every frame)
- Large data volume (10,000+ vertices)
- Batching requires contiguous data
- Already implemented this way!

**Result**: Pattern C (Hybrid) - OOP for organization, DOD for performance

### Future Optimization Path

**If profiling shows** shared_ptr bottleneck in Layer 4:

**Option 1**: Optimize to Pattern B
```cpp
// Before (Pattern A)
std::vector<std::shared_ptr<Layer>> m_children;

// After (Pattern B)
std::vector<LayerVariant> m_children;
using LayerVariant = std::variant<Rectangle, Circle, Button, Text>;

// Render method doesn't change!
void Render() {
    for (auto& child : m_children) {
        std::visit([](auto& c) { c.Render(); }, child);
    }
}
```

**Option 2**: Keep Pattern A, optimize containers
```cpp
// Use object pool instead of make_shared
class LayerPool {
    std::vector<Layer> m_storage;        // Contiguous backing storage
    std::vector<Layer*> m_freeList;      // Free objects

public:
    Layer* Allocate() {
        if (m_freeList.empty()) {
            m_storage.emplace_back();
            return &m_storage.back();
        }
        Layer* obj = m_freeList.back();
        m_freeList.pop_back();
        return obj;
    }

    void Free(Layer* obj) {
        m_freeList.push_back(obj);
    }
};
```

**Key**: Render() methods already call Primitives API, so storage refactoring doesn't affect rendering logic.

---

## Performance Comparison

### Benchmark Setup

**Scenario**: Render 10,000 rectangles

**Pattern A (shared_ptr)**:
```cpp
std::vector<std::shared_ptr<Rectangle>> rects;
for (int i = 0; i < 10000; i++) {
    rects.push_back(std::make_shared<Rectangle>(/* ... */));
}

// Render
for (auto& rect : rects) {
    rect->Render();  // Virtual call, pointer dereference
}
```

**Pattern B (value semantics)**:
```cpp
std::vector<Rectangle> rects;
rects.reserve(10000);
for (int i = 0; i < 10000; i++) {
    rects.push_back(Rectangle{/* ... */});
}

// Render
for (auto& rect : rects) {
    rect.Render();  // Direct call, no indirection
}
```

### Theoretical Analysis

**Memory access pattern A**:
```
Vector iteration: ✅ Cache-friendly (pointers are contiguous)
                  ↓
Pointer dereference: ❌ Cache miss (objects scattered)
                  ↓
Virtual call: ❌ Indirect jump (vtable lookup)
```

**Memory access pattern B**:
```
Vector iteration: ✅ Cache-friendly (objects contiguous)
                  ↓
Direct access: ✅ All data in cache already
                  ↓
Direct call: ✅ Direct jump (compiler can inline)
```

### Expected Results

**Pattern A**:
- Cache misses: ~High (scattered allocations)
- Iteration time: ~50-100 µs (10,000 objects)
- Render time: Depends on Render() implementation

**Pattern B**:
- Cache misses: ~Low (sequential access)
- Iteration time: ~10-20 µs (10,000 objects)
- Render time: Same as Pattern A (both call Primitives)

**Speedup**: 2-5x for iteration (research says up to 50x in extreme cases)

### Real-World Considerations

**For UI**:
- Typical count: 10-100 components (not 10,000)
- Update frequency: On interaction (not every frame)
- Complexity: Button logic dominates over iteration

**Result**: Iteration overhead is negligible for UI

**For particles/entities**:
- Typical count: 1,000-100,000 particles
- Update frequency: Every frame
- Complexity: Simple update logic

**Result**: Iteration overhead is significant, data-oriented wins

---

## Recommendations

### For UI Components (Worldsim)

**Use Pattern A** (shared_ptr in vectors) initially:
- Workload is small (<1000 components)
- Proven functional (colonysim)
- Faster to implement
- **Profile before optimizing**

### For Rendering Layer (Worldsim)

**Use Pattern B** (value semantics) - already implemented:
- Large data volume (10,000+ vertices)
- Every frame hot path
- Research-validated performance wins
- **Already done in BatchRenderer ✅**

### For Game Entities (Future)

**Consider Pattern C** (hybrid with ECS):
- Potentially large count (thousands of entities)
- Need both organization and performance
- Incremental: Start OOP, migrate to ECS if needed

---

## References

### Industry Examples

**Unity DOTS** (Pattern B):
```csharp
// Entities stored in contiguous chunks
struct PositionComponent { float3 Value; }
struct HealthComponent { float Value; }

// System iterates over packed data
partial class HealthRegenSystem : SystemBase {
    protected override void OnUpdate() {
        Entities.ForEach((ref HealthComponent health) => {
            health.Value += 1.0f * deltaTime;
        }).ScheduleParallel();
    }
}
```

**Unreal Engine** (Pattern C - Hybrid):
```cpp
// Actor (OOP) - logical organization
class AMyActor : public AActor {
    UPROPERTY()
    UStaticMeshComponent* MeshComponent;

    // Rendering proxy created separately (DOD)
    // Proxy contains compact data for rendering thread
};
```

**RimWorld** (Pattern A):
```csharp
// Uses List<Thing> extensively
public class ThingGrid {
    private List<List<Thing>> thingGrid;  // List of lists
}

// Still hits 60 FPS with thousands of objects
// Because game logic is fast relative to rendering
```

### Research References

- `/docs/research/modern_rendering_architecture.md` - Complete analysis
- Mike Acton's "Data-Oriented Design" talks
- Unity DOTS documentation
- Unreal Engine rendering architecture docs

### Colonysim Analysis

- `/Volumes/Code/colonysim/src/Rendering/Layer.h` - Pattern A implementation
- Measured performance: 60 FPS with 100+ UI components
- Proves Pattern A is sufficient for UI workload

---

## Appendix: Variant Example

For those considering Pattern B with polymorphism:

```cpp
// Define variant type
using Shape = std::variant<Rectangle, Circle, Line, Text>;

// Storage
std::vector<Shape> shapes;

// Add shapes
shapes.push_back(Rectangle{/* ... */});
shapes.push_back(Circle{/* ... */});

// Render with std::visit
for (auto& shape : shapes) {
    std::visit([](auto& s) {
        s.Render();  // Calls appropriate Render() method
    }, shape);
}

// Or with overloaded visitor
struct RenderVisitor {
    void operator()(Rectangle& r) { r.Render(); }
    void operator()(Circle& c) { c.Render(); }
    void operator()(Line& l) { l.Render(); }
    void operator()(Text& t) { t.Render(); }
};

for (auto& shape : shapes) {
    std::visit(RenderVisitor{}, shape);
}
```

**Pros**:
- Type-safe
- No heap allocation
- Contiguous storage

**Cons**:
- More verbose than virtual functions
- All types must be known at compile time
- Variant size = size of largest type

---

## Revision History

- 2025-10-29: Initial research document created
- 2025-10-29: Analysis of colonysim vs worldsim patterns
- 2025-10-29: Recommendations for pragmatic hybrid approach
