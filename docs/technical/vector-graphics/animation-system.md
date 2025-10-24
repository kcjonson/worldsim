# Vector Animation System Design

Created: 2025-10-24
Status: Design

## Overview

The animation system enables real-time deformation of vector graphics for organic movement: grass bending, trees swaying, water rippling. Uses **spline-based deformation** to modify Bezier curves before tessellation.

## Requirements

- **Real-time**: Deform and re-tessellate at 60 FPS
- **Natural Movement**: Smooth, organic animations (wind, physics)
- **Interactive**: Entities respond to player movement (trampling grass)
- **Scalable**: Handle 10,000+ animated entities
- **Deterministic**: Same inputs = same animation (for networking)

## Animation Data Model

### Base Representation

```cpp
struct VectorAsset {
    std::vector<BezierPath> paths;     // Base shape (unanimated)
    AnimationMetadata animData;         // Animation parameters
};

struct BezierPath {
    std::vector<BezierCurve> curves;   // Cubic Bezier segments
};

struct BezierCurve {
    vec2 p0, p1, p2, p3;               // Control points
};
```

### Animation Metadata

Stored in SVG `<metadata>` tags:

```xml
<metadata>
    <animation type="grass_bend">
        <pivot x="0.5" y="1.0"/>        <!-- Anchor point (bottom center) -->
        <frequency>2.5</frequency>       <!-- Base sway frequency -->
        <amplitude>0.15</amplitude>      <!-- Max bend angle -->
        <stiffness>0.8</stiffness>       <!-- Resistance to forces -->
    </animation>
</metadata>
```

## Deformation Techniques

### 1. Spline-Based Deformation (Primary)

**Concept**: Apply transformation to Bezier control points, re-tessellate.

**Grass Blade Example**:
```cpp
void DeformGrassBlade(BezierPath& path, float bendAngle, vec2 pivot) {
    mat3 rotation = RotationMatrix(bendAngle);
    mat3 pivotTransform = TranslateMatrix(-pivot) * rotation * TranslateMatrix(pivot);

    for (BezierCurve& curve : path.curves) {
        curve.p0 = pivotTransform * curve.p0;
        curve.p1 = pivotTransform * curve.p1;
        curve.p2 = pivotTransform * curve.p2;
        curve.p3 = pivotTransform * curve.p3;
    }

    // Now tessellate deformed path
    Tessellate(path);
}
```

**Pros**: Flexible, works with any Bezier shape
**Cons**: Must re-tessellate each frame (CPU cost)

### 2. Vertex Shader Deformation (Secondary)

**Concept**: For simple deformations, bend in vertex shader (no re-tessellation).

**Example** (simple sine wave):
```glsl
vec2 BendVertex(vec2 pos, float time, float frequency) {
    float bendAmount = sin(pos.y * frequency + time) * 0.1;
    return vec2(pos.x + bendAmount, pos.y);
}
```

**Pros**: Zero CPU cost, very fast
**Cons**: Limited to simple deformations, requires pre-tessellated mesh

**Use Case**: Simple swaying for distant objects (LOD)

## Animation Controllers

### Wind Simulation

**Approach**: Perlin noise for natural wind patterns.

```cpp
class WindSystem {
    float windSpeed = 2.0f;
    vec2 windDirection = vec2(1.0f, 0.3f);
    float time = 0.0f;

    float GetWindForce(vec2 position) {
        // Sample Perlin noise for spatially-varying wind
        float noise = PerlinNoise(position * 0.1f + time * windSpeed);
        return noise * windSpeed;
    }

    void UpdateGrass(GrassEntity& grass, float deltaTime) {
        time += deltaTime;

        float windForce = GetWindForce(grass.position);
        float bendAngle = windForce * grass.amplitude;

        // Apply damping (spring simulation)
        grass.currentBend = Lerp(grass.currentBend, bendAngle, grass.stiffness * deltaTime);

        // Deform path
        DeformGrassBlade(grass.path, grass.currentBend, grass.pivot);
        grass.MarkDirty();
    }
};
```

### Trampling System

**Approach**: Apply force when entities walk through grass.

```cpp
class TramplingSystem {
    void Update(float deltaTime) {
        for (GrassEntity& grass : grassEntities) {
            // Check for nearby entities
            float force = 0.0f;
            for (Entity& entity : nearbyEntities) {
                float distance = Distance(entity.position, grass.position);
                if (distance < trampleRadius) {
                    force = max(force, 1.0f - distance / trampleRadius);
                }
            }

            // Apply trampling force (overrides wind)
            if (force > 0.0f) {
                grass.trampleForce = force;
                grass.currentBend = grass.maxTrampleAngle * force;
                grass.MarkDirty();
            } else {
                // Spring back to wind-driven state
                grass.trampleForce = Lerp(grass.trampleForce, 0.0f, deltaTime * 5.0f);
            }
        }
    }
};
```

### Tree Sway

**Multi-Point Deformation**:

```cpp
struct TreeAnimation {
    std::vector<DeformationPoint> deformPoints; // Trunk segments
    float phase;                                // Animation offset
};

void UpdateTreeSway(TreeEntity& tree, float wind, float deltaTime) {
    // Each segment bends based on position (top bends more)
    for (size_t i = 0; i < tree.deformPoints.size(); i++) {
        float heightFactor = (float)i / tree.deformPoints.size();
        float bendAmount = wind * heightFactor * heightFactor; // Quadratic

        tree.deformPoints[i].angle = sin(time + tree.phase) * bendAmount;
    }

    // Apply cascading deformation to path
    DeformTreePath(tree.path, tree.deformPoints);
    tree.MarkDirty();
}
```

## Performance Optimizations

### 1. Dirty Tracking

Only re-tessellate changed entities:

```cpp
struct AnimatedEntity {
    bool dirty = false;
    BezierPath currentPath;
    TessellatedMesh cachedMesh;

    void MarkDirty() { dirty = true; }

    const TessellatedMesh& GetMesh() {
        if (dirty) {
            cachedMesh = Tessellate(currentPath);
            dirty = false;
        }
        return cachedMesh;
    }
};
```

**Savings**: ~50-80% CPU time (most entities don't change every frame)

### 2. LOD for Animation

**Far Away**: Skip animation, use static mesh
**Medium**: Simple vertex shader deformation
**Close**: Full spline deformation

```cpp
AnimationQuality GetAnimationLOD(float distance) {
    if (distance > 50.0f) return STATIC;
    if (distance > 20.0f) return SIMPLE_SHADER;
    return FULL_SPLINE;
}
```

### 3. Spatial Culling

Only animate visible entities:

```cpp
for (Entity& e : entities) {
    if (!camera.frustum.Contains(e.bounds)) continue;
    UpdateAnimation(e, deltaTime);
}
```

### 4. Update Rate Reduction

Distant entities update at lower rate:

```cpp
int updateInterval = GetUpdateInterval(distance);
if (frameCount % updateInterval == entityID % updateInterval) {
    UpdateAnimation(entity, deltaTime * updateInterval);
}
```

**Example**: Distance objects update every 4 frames (15 Hz instead of 60 Hz)

### 5. Multi-Threading

Parallelize animation updates:

```cpp
#pragma omp parallel for
for (int i = 0; i < entities.size(); i++) {
    UpdateAnimation(entities[i], deltaTime);
}
```

**Requirement**: Thread-safe tessellation (use per-thread memory arenas)

## Data Structures

### Per-Entity Animation State

```cpp
struct AnimationState {
    float currentBend;      // Current bend angle/offset
    float targetBend;       // Target from wind/physics
    float velocity;         // For spring simulation
    float phase;            // Time offset for variation
    vec2 pivot;             // Rotation pivot point
    bool dirty;             // Needs re-tessellation
};
```

### Animation Templates

Share animation parameters across entity types:

```cpp
struct AnimationTemplate {
    float frequency;
    float amplitude;
    float stiffness;
    float dampening;
    vec2 pivotNormalized; // 0-1 in entity space
};

std::unordered_map<AssetID, AnimationTemplate> animationTemplates;

// Apply template to entity
grassEntity.animation = animationTemplates[AssetID::Grass];
grassEntity.animation.phase = Random(); // Variation
```

## Integration with ECS

```cpp
// Component
struct AnimatedVectorComponent {
    AssetID assetID;
    AnimationState animState;
    BezierPath deformedPath;
    bool dirty;
};

// System
class VectorAnimationSystem : public System {
    void Update(float deltaTime) override {
        auto view = registry.view<AnimatedVectorComponent, TransformComponent>();

        for (auto entity : view) {
            auto& anim = view.get<AnimatedVectorComponent>(entity);
            auto& transform = view.get<TransformComponent>(entity);

            // Apply wind/physics forces
            float windForce = windSystem.GetForce(transform.position);
            anim.animState.targetBend = windForce;

            // Spring simulation
            float delta = anim.animState.targetBend - anim.animState.currentBend;
            anim.animState.velocity += delta * stiffness * deltaTime;
            anim.animState.velocity *= (1.0f - dampening);
            anim.animState.currentBend += anim.animState.velocity * deltaTime;

            // Deform path
            DeformPath(anim.deformedPath, anim.animState);
            anim.dirty = true;
        }
    }
};
```

## Animation File Format

**SVG with Custom Metadata**:

```xml
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100">
    <metadata>
        <game:animation xmlns:game="http://worldsim.game">
            <type>grass_blade</type>
            <pivot x="50" y="100"/>
            <frequency>2.5</frequency>
            <amplitude>15</amplitude>
            <stiffness>0.8</stiffness>
            <dampening>0.95</dampening>
        </game:animation>
    </metadata>

    <path d="M 50 100 Q 50 50, 55 0" fill="#2a5" stroke="#1a3" stroke-width="2"/>
</svg>
```

## Related Documentation

- [architecture.md](./architecture.md) - Tier 3 dynamic entities
- [tessellation-options.md](./tessellation-options.md) - Re-tessellation after deformation
- [performance-targets.md](./performance-targets.md) - Animation budget
- [/docs/design/features/vector-graphics/animated-vegetation.md](../../../design/features/vector-graphics/animated-vegetation.md) - Gameplay design

## References

- Bezier Curve Math: https://pomax.github.io/bezierinfo/
- Perlin Noise: https://adrianb.io/2014/08/09/perlinnoise.html
- Spring Simulation: https://www.ryanjuckett.com/damped-springs/
