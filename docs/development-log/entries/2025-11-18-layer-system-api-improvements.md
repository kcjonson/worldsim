# Layer System API Improvements

**Date:** 2025-11-18

**Summary:**
Refactored LayerManager API to address ergonomic issues discovered during usage. Added Container type for pure hierarchy nodes, implemented auto-zIndex based on insertion order, simplified creation to single-call pattern, and fixed zIndex=0.0F support with stable sort behavior.

**Files Modified:**
- `libs/ui/shapes/shapes.h` - Added Container struct, zIndex/visible fields to all shapes
- `libs/ui/layer/layer_manager.h` - New Create() and AddChild() overloads, auto-zIndex counter
- `libs/ui/layer/layer_manager.cpp` - Auto-assignment logic, stable_sort implementation
- `libs/ui/layer/layer_manager.test.cpp` - New tests for auto-zIndex and stable sort
- `apps/ui-sandbox/scenes/layer_scene.cpp` - Demo both auto and explicit zIndex modes

**Key Changes:**

**1. Added Container Type**
Pure hierarchy node with no visual representation:
```cpp
struct Container {
    const char* id = nullptr;
    float zIndex{-1.0F};  // -1.0F = auto-assign
    bool visible{true};
    void Render() const {} // No-op
};
```

**2. Simplified API - From 3 Steps to 1**
Before (verbose):
```cpp
Circle circle{.center = {400.0F, 250.0F}, .radius = 80.0F};
uint32_t id = m_layerManager.CreateCircle(circle);
m_layerManager.SetZIndex(id, 3.0F);
m_layerManager.AddChild(parent, id);
```

After (clean):
```cpp
Circle circle{.center = {400.0F, 250.0F}, .radius = 80.0F};
m_layerManager.AddChild(parent, circle);  // Auto zIndex!
```

**3. Auto-ZIndex Based on Insertion Order**
- Default `zIndex = -1.0F` triggers auto-assignment
- Sequential values assigned: 1.0, 2.0, 3.0, etc.
- Explicit values (>= 0.0F) used as-is
- Sentinel changed from 0.0F to -1.0F to allow explicit zero

**4. Stable Sort for Equal ZIndex**
- Changed from `std::sort` to `std::stable_sort`
- Preserves insertion order for equal zIndex values
- CSS-like behavior: same zIndex renders in insertion order

**Implementation Details:**

**Auto-Assignment Logic:**
```cpp
float m_nextAutoZIndex{1.0F};  // Counter in LayerManager

template <typename T>
uint32_t CreateLayer(const T& shapeData) {
    float assignedZIndex = shapeData.zIndex;
    if (assignedZIndex < 0.0F) {  // -1.0F = auto
        assignedZIndex = m_nextAutoZIndex;
        m_nextAutoZIndex += 1.0F;
    }
    // ... create node with assignedZIndex
}
```

**New API Methods:**
- `Create(const Container&)`, `Create(const Shape&)` - Standalone creation
- `AddChild(parent, const Container&)`, `AddChild(parent, const Shape&)` - Create and attach in one call
- Removed: `CreateRectangle()`, `CreateCircle()`, etc. (replaced by `Create()`)

**Testing:**
- 37 tests passing (up from 34)
- New test: `AutoAssignZIndexOnCreate` - Verifies sequential assignment
- New test: `ExplicitZeroZIndexAllowed` - Verifies 0.0F works explicitly
- New test: `StableSortPreservesInsertionOrder` - Verifies CSS-like behavior
- Visual verification: layer_scene shows both auto and explicit override modes

**Memory Impact:**
- +9 bytes per shape (zIndex float + visible bool + padding)
- Negligible compared to shape geometry data
- Trade-off: slightly more memory for much better API ergonomics

**Lessons Learned:**
1. **Sentinel values matter** - Using 0.0F as both default and sentinel was a design smell. -1.0F is better.
2. **CSS patterns work** - Insertion order + stable sort is intuitive for developers
3. **Ergonomics > purity** - Duplicating zIndex/visible in both shape and LayerNode is worth it for API clarity
4. **Progressive disclosure** - Auto-assignment for common case, explicit override for power users

**Next Steps:**
- Shape System improvements (Circle, Text rendering)
- InputManager port from colonysim
- UI Components (Button, TextInput)



