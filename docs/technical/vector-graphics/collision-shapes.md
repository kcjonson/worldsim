# Collision Shapes - Dual Representation Design

Created: 2025-10-24
Status: Design

## Overview

Vector graphics entities have **two separate geometric representations**:
1. **Render Geometry**: Complex, detailed, many triangles (for visuals)
2. **Collision Geometry**: Simplified, few shapes (for physics)

This separation optimizes both rendering and physics performance.

## Rationale

**Rendering Needs**:
- High detail for visual quality
- Smooth curves (many triangles)
- Can have 100-500 triangles per entity

**Physics Needs**:
- Fast collision detection (simple shapes)
- Convex shapes preferred (faster algorithms)
- Typically 1-10 shapes per entity

**Example - Tree**:
- Render: 500 triangles (trunk + leaves)
- Collision: 1 AABB for trunk (4 vertices)
- **100× simpler collision geometry**

## Collision Shape Types

### 1. Axis-Aligned Bounding Box (AABB)

**Simplest, fastest**:
```cpp
struct AABB {
    vec2 min, max;
};

bool Collides(const AABB& a, const AABB& b) {
    return a.min.x < b.max.x && a.max.x > b.min.x &&
           a.min.y < b.max.y && a.max.y > b.min.y;
}
```

**Use For**: Trees, buildings, simple objects
**Pros**: Extremely fast, trivial to compute
**Cons**: Poor fit for rotated objects

### 2. Oriented Bounding Box (OBB)

**AABB with rotation**:
```cpp
struct OBB {
    vec2 center;
    vec2 halfExtents;
    float rotation;
};
```

**Use For**: Walls, fences, rectangular structures
**Pros**: Better fit than AABB, still fast
**Cons**: More complex collision detection (SAT)

### 3. Circle

**Simple, rotation-invariant**:
```cpp
struct Circle {
    vec2 center;
    float radius;
};

bool Collides(const Circle& a, const Circle& b) {
    float dist = Distance(a.center, b.center);
    return dist < (a.radius + b.radius);
}
```

**Use For**: Barrels, boulders, round objects
**Pros**: Fastest collision check, rotation-free
**Cons**: Poor fit for elongated shapes

### 4. Convex Polygon

**Flexible, accurate**:
```cpp
struct ConvexPolygon {
    std::vector<vec2> vertices; // Must be convex, CCW order
};

// Collision via Separating Axis Theorem (SAT)
```

**Use For**: Complex shapes needing accuracy
**Pros**: Better fit than primitives
**Cons**: Slower (SAT is O(n))

### 5. Compound Shapes

**Multiple primitives combined**:
```cpp
struct CompoundShape {
    std::vector<CollisionShape> shapes; // Mix of AABBs, circles, etc.
};
```

**Example**: Character = circle (body) + AABB (weapon)
**Pros**: Flexibility, accuracy
**Cons**: Multiple collision checks

## Generating Collision Shapes from Vector Paths

### Method 1: Bounding Box (Simplest)

```cpp
AABB ComputeBoundingBox(const VectorPath& path) {
    vec2 min = vec2(FLT_MAX);
    vec2 max = vec2(-FLT_MAX);

    for (const BezierCurve& curve : path.curves) {
        // Sample curve points
        for (float t = 0.0f; t <= 1.0f; t += 0.1f) {
            vec2 p = EvaluateBezier(curve, t);
            min = Min(min, p);
            max = Max(max, p);
        }
    }

    return {min, max};
}
```

### Method 2: Convex Hull

**Generate minimal convex polygon containing all points**:

```cpp
ConvexPolygon ComputeConvexHull(const VectorPath& path) {
    std::vector<vec2> points = SamplePath(path, 0.05f);
    return GrahamScan(points); // O(n log n) convex hull algorithm
}
```

**Algorithms**:
- Graham Scan: O(n log n)
- Jarvis March: O(nh) where h = hull points
- QuickHull: O(n log n) average

### Method 3: Manual Definition in SVG

**Artist-defined collision shapes** (recommended):

```xml
<metadata>
    <collision>
        <shape type="aabb">
            <min x="10" y="90"/>
            <max x="20" y="100"/>
        </shape>
    </collision>
</metadata>
```

**Pros**: Perfect control, optimal performance
**Cons**: Manual work for artists

### Method 4: Simplified Polygon

**Tessellate then simplify** (Douglas-Peucker algorithm):

```cpp
std::vector<vec2> SimplifyPolygon(const std::vector<vec2>& points, float epsilon) {
    // Douglas-Peucker: recursively remove points within epsilon distance
    return DouglasPeucker(points, epsilon);
}
```

## Storage Format

### In-Memory Representation

```cpp
struct VectorEntity {
    // Rendering
    AssetID assetID;
    TessellatedMesh renderMesh;  // Hundreds of triangles

    // Collision
    CollisionShapeType collisionType;
    union {
        AABB aabb;
        Circle circle;
        OBB obb;
        ConvexPolygon* polygon; // Heap-allocated if complex
    } collisionShape;
};
```

### In SVG File

```xml
<svg>
    <metadata>
        <collision>
            <shape type="aabb" min="10,90" max="20,100"/>
            <!-- or -->
            <shape type="circle" center="15,50" radius="5"/>
            <!-- or -->
            <shape type="polygon" vertices="0,0 10,0 10,10 0,10"/>
            <!-- or compound -->
            <shapes>
                <shape type="circle" center="15,30" radius="8"/>
                <shape type="aabb" min="10,90" max="20,100"/>
            </shapes>
        </collision>
    </metadata>

    <!-- Render paths -->
    <path d="..." fill="#..."/>
</svg>
```

## Integration with Physics/ECS

```cpp
// Components
struct RenderComponent {
    TessellatedMesh mesh;
};

struct CollisionComponent {
    CollisionShape shape;
    CollisionLayer layer;  // What this collides with
};

// Collision System (separate from rendering)
class CollisionSystem : public System {
    void Update(float deltaTime) {
        auto view = registry.view<CollisionComponent, TransformComponent>();

        // Broad phase: spatial grid
        SpatialGrid grid(chunkSize);
        for (auto entity : view) {
            auto& collision = view.get<CollisionComponent>(entity);
            auto& transform = view.get<TransformComponent>(entity);
            grid.Insert(entity, GetBounds(collision.shape, transform));
        }

        // Narrow phase: actual collision checks
        for (auto entity : view) {
            auto nearby = grid.Query(GetBounds(entity));
            for (auto other : nearby) {
                if (Collides(entity, other)) {
                    HandleCollision(entity, other);
                }
            }
        }
    }
};
```

## Performance Considerations

### Collision Check Complexity

| Shape Type | vs AABB | vs Circle | vs Polygon (n vertices) |
|------------|---------|-----------|-------------------------|
| AABB | O(1) | O(1) | O(n) |
| Circle | O(1) | O(1) | O(n) |
| Polygon | O(n+m) | O(n) | O(n×m) |

**Recommendation**: Use simple shapes (AABB, Circle) whenever possible.

### Spatial Partitioning

**Grid-based** (recommended for tile-based game):
```cpp
class SpatialGrid {
    float cellSize;
    std::unordered_map<ivec2, std::vector<EntityID>> cells;

    void Insert(EntityID entity, AABB bounds) {
        ivec2 minCell = WorldToCell(bounds.min);
        ivec2 maxCell = WorldToCell(bounds.max);

        for (int y = minCell.y; y <= maxCell.y; y++) {
            for (int x = minCell.x; x <= maxCell.x; x++) {
                cells[{x, y}].push_back(entity);
            }
        }
    }

    std::vector<EntityID> Query(AABB bounds) {
        // Return all entities in overlapping cells
    }
};
```

**Benefit**: O(1) broad-phase queries instead of O(n²) all-pairs

## Related Documentation

- [architecture.md](./architecture.md) - Dual representation in tiers
- [svg-parsing-options.md](./svg-parsing-options.md) - Parsing collision metadata
- [/docs/design/features/vector-graphics/environmental-interactions.md](../../../design/features/vector-graphics/environmental-interactions.md) - Gameplay interactions

## References

- Separating Axis Theorem: https://www.gamedev.net/tutorials/programming/general-and-gameplay-programming/2d-rotated-rectangle-collision-r2604/
- Convex Hull Algorithms: https://en.wikipedia.org/wiki/Convex_hull_algorithms
- Spatial Hashing: https://www.gamedev.net/tutorials/programming/general-and-gameplay-programming/spatial-hashing-r2697/
