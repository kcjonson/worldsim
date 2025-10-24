# Level of Detail (LOD) System

Created: 2025-10-24
Status: Design

## Overview

LOD reduces rendering cost by adapting detail level based on distance/zoom. For vector graphics, this means adjusting tessellation quality and animation complexity.

## LOD Strategies

### 1. Tessellation Quality

**Reduce triangle count for distant objects**:

```cpp
enum LODLevel {
    LOD_HIGH,    // Close: <10 units, high quality tessellation
    LOD_MEDIUM,  // Medium: 10-30 units, moderate quality
    LOD_LOW,     // Far: 30-50 units, low quality
    LOD_MINIMAL  // Very far: >50 units, minimal/static
};

LODLevel GetLOD(float distance) {
    if (distance < 10.0f) return LOD_HIGH;
    if (distance < 30.0f) return LOD_MEDIUM;
    if (distance < 50.0f) return LOD_LOW;
    return LOD_MINIMAL;
}

float GetTessellationTolerance(LODLevel lod) {
    switch (lod) {
        case LOD_HIGH:    return 0.5f;  // 0.5 pixel error
        case LOD_MEDIUM:  return 2.0f;  // 2 pixel error
        case LOD_LOW:     return 5.0f;  // 5 pixel error
        case LOD_MINIMAL: return 20.0f; // Very coarse
    }
}
```

**Impact**:
- LOD_HIGH: 100 triangles per grass blade
- LOD_MEDIUM: 30 triangles
- LOD_LOW: 10 triangles
- LOD_MINIMAL: 3 triangles (single triangle fan)

### 2. Animation Quality

**Different animation techniques per LOD**:

```cpp
void UpdateEntityAnimation(Entity& entity, float distance) {
    LODLevel lod = GetLOD(distance);

    switch (lod) {
        case LOD_HIGH:
            // Full spline deformation + re-tessellation
            UpdateSplineAnimation(entity);
            Tessellate(entity);
            break;

        case LOD_MEDIUM:
            // Simplified deformation (fewer control points)
            UpdateSimplifiedAnimation(entity);
            Tessellate(entity);
            break;

        case LOD_LOW:
            // Vertex shader animation only (no re-tessellation)
            UpdateShaderAnimation(entity);
            break;

        case LOD_MINIMAL:
            // Static, no animation
            break;
    }
}
```

### 3. Update Rate Reduction

**Update distant entities less frequently**:

```cpp
int GetUpdateInterval(LODLevel lod) {
    switch (lod) {
        case LOD_HIGH:    return 1; // Every frame (60 Hz)
        case LOD_MEDIUM:  return 2; // Every 2 frames (30 Hz)
        case LOD_LOW:     return 4; // Every 4 frames (15 Hz)
        case LOD_MINIMAL: return 0; // Never
    }
}

void UpdateAnimations(float deltaTime) {
    for (Entity& e : entities) {
        LODLevel lod = GetLOD(DistanceToCamera(e));
        int interval = GetUpdateInterval(lod);

        if (interval == 0) continue;
        if (frameCount % interval != e.id % interval) continue;

        UpdateAnimation(e, deltaTime * interval);
    }
}
```

### 4. Visibility Culling

**Don't render at all beyond threshold**:

```cpp
float maxRenderDistance = 100.0f; // Configurable

for (Entity& e : entities) {
    float distance = DistanceToCamera(e);
    if (distance > maxRenderDistance) continue; // Skip entirely

    LODLevel lod = GetLOD(distance);
    Render(e, lod);
}
```

## Screen-Space Error Metric

**Adaptive tessellation based on screen size**:

```cpp
float GetScreenSpaceError(Entity& entity, Camera& camera) {
    float distance = DistanceToCamera(entity, camera);
    float screenHeight = camera.viewportHeight;
    float fov = camera.fieldOfView;

    // Project entity size to screen space
    float entitySize = entity.bounds.GetSize();
    float screenSize = (entitySize / distance) * (screenHeight / tan(fov / 2.0f));

    return screenSize; // Pixels on screen
}

float GetTessellationTolerance(float screenSize) {
    // Allow 1 pixel error per 50 pixels of screen size
    return max(0.5f, screenSize / 50.0f);
}
```

## Zoom-Based LOD

**For tile-based game with camera zoom**:

```cpp
enum ZoomLevel {
    ZOOM_FAR,    // Zoomed out, see large area
    ZOOM_NORMAL, // Default view
    ZOOM_CLOSE   // Zoomed in, see detail
};

LODLevel GetLODFromZoom(float cameraZoom) {
    if (cameraZoom > 2.0f) return LOD_HIGH;    // Very zoomed in
    if (cameraZoom > 1.0f) return LOD_MEDIUM;  // Normal
    if (cameraZoom > 0.5f) return LOD_LOW;     // Zoomed out
    return LOD_MINIMAL;                        // Very zoomed out
}
```

## Tile-Specific LOD

**Different LOD for tile backgrounds vs entities**:

- **Tile Backgrounds (Tier 1)**: Pre-rasterized at multiple resolutions
  - 128×128 for zoom close
  - 64×64 for zoom normal
  - 32×32 for zoom far
  - 16×16 for zoom very far

- **Entities (Tier 3)**: Dynamic tessellation quality

## LOD Transitions

**Avoid popping** (sudden quality changes):

### Option 1: Hysteresis

```cpp
// Different thresholds for upgrading vs downgrading LOD
LODLevel UpdateLOD(Entity& entity, float distance) {
    static const float upgradeThresholds[] = {8.0f, 25.0f, 45.0f};
    static const float downgradeThresholds[] = {12.0f, 35.0f, 55.0f};

    LODLevel current = entity.currentLOD;

    // Check for LOD change
    if (distance < upgradeThresholds[current]) {
        return (LODLevel)(current - 1); // Upgrade
    } else if (distance > downgradeThresholds[current]) {
        return (LODLevel)(current + 1); // Downgrade
    }

    return current; // No change
}
```

### Option 2: Temporal Amortization

**Spread LOD updates over multiple frames**:

```cpp
void UpdateLODs() {
    int entitiesToUpdate = entities.size() / 10; // Update 10% per frame

    for (int i = 0; i < entitiesToUpdate; i++) {
        int index = (frameCount * entitiesToUpdate + i) % entities.size();
        entities[index].lod = CalculateLOD(entities[index]);
    }
}
```

## Performance Impact

**Estimated savings** (10,000 entities):

| LOD Distribution | Avg Triangles/Entity | Total Triangles | Tessellation Time |
|------------------|----------------------|-----------------|-------------------|
| All High | 100 | 1,000,000 | 15ms |
| Mixed (realistic) | 30 | 300,000 | 5ms |
| All Low | 10 | 100,000 | 1.5ms |

**Realistic Distribution** (camera in center of scene):
- 10% High (close): 1,000 entities × 100 tri = 100k tri
- 30% Medium: 3,000 entities × 30 tri = 90k tri
- 40% Low: 4,000 entities × 10 tri = 40k tri
- 20% Minimal: 2,000 entities × 3 tri = 6k tri
- **Total**: 236k triangles (4× reduction from all-high)

## Implementation

```cpp
class LODManager {
    struct LODConfig {
        std::vector<float> distanceThresholds;
        std::vector<float> tessellationTolerances;
        std::vector<int> updateIntervals;
    };

    LODConfig config;

    LODLevel GetLOD(float distance) {
        for (int i = 0; i < config.distanceThresholds.size(); i++) {
            if (distance < config.distanceThresholds[i]) {
                return (LODLevel)i;
            }
        }
        return LOD_MINIMAL;
    }

    void UpdateEntity(Entity& entity, const Camera& camera) {
        float distance = Distance(camera.position, entity.position);
        LODLevel newLOD = GetLOD(distance);

        if (newLOD != entity.currentLOD) {
            entity.currentLOD = newLOD;
            entity.tessellationTolerance = config.tessellationTolerances[newLOD];
            entity.MarkDirty(); // Re-tessellate with new quality
        }
    }
};
```

## Related Documentation

- [architecture.md](./architecture.md) - Tier system and LOD integration
- [animation-system.md](./animation-system.md) - Animation LOD
- [tessellation-options.md](./tessellation-options.md) - Adaptive tessellation
- [performance-targets.md](./performance-targets.md) - Performance budgets

## References

- Screen-Space Error: https://www.gamasutra.com/view/feature/131596/realtime_procedural_terrain_.php
- LOD Systems: https://www.gdcvault.com/play/1020394/Advanced-Visual-Effects-with
