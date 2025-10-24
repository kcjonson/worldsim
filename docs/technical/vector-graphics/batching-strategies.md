# Batching Strategies - Technical Analysis

Created: 2025-10-24
Status: Design

## Overview

GPU batching is critical for rendering thousands of vector entities efficiently. This document analyzes strategies for minimizing draw calls and maximizing throughput when rendering dynamic geometry.

**Goal**: Render 10,000+ entities with <100 draw calls per frame @ 60 FPS.

## Core Batching Approaches

### 1. Material-Based Batching (Recommended)

**Concept**: Group all entities using the same material (shader + texture + blend mode) into a single draw call.

**Implementation**:
```cpp
std::unordered_map<MaterialID, GeometryBatch> batches;

for (Entity entity : visibleEntities) {
    MaterialID mat = GetMaterial(entity);
    batches[mat].Add(entity.geometry, entity.transform);
}

for (auto& [material, batch] : batches) {
    BindMaterial(material);
    UploadGeometry(batch);
    glDrawElements(GL_TRIANGLES, batch.indexCount, ...);
}
```

**Pros**: Simple, effective, minimal state changes
**Cons**: Number of draw calls = number of unique materials
**Draw Call Count**: 10-50 typically (depends on material variety)

### 2. Texture Atlas Batching

**Concept**: Pack multiple textures into large atlases, allow batching entities with different source textures.

**Benefits**:
- Single texture bind for hundreds of entities
- Material count reduced to blend modes only
- Can batch grass + trees + rocks in one call

**Atlas Generation**:
- Rect-packing algorithm (shelf, guillotine, maxrects)
- Multiple atlas sizes: 2048×2048, 4096×4096
- Generate UV coordinates on-the-fly or pre-compute

### 3. Uber Shader Approach

**Concept**: Single shader with material properties passed as uniforms/vertex attributes.

**Example**:
```glsl
// Vertex attributes include material ID
layout(location = 3) in uint aMaterialID;

// Fragment shader uses material ID to index into uniform buffer
uniform MaterialProperties uMaterials[256];

void main() {
    MaterialProperties mat = uMaterials[aMaterialID];
    // ... use mat.color, mat.textureLayer, etc.
}
```

**Pros**: Can batch everything in one draw call
**Cons**: Shader complexity, GPU register pressure
**Best For**: Tier 1-2 (static geometry)

## Streaming VBO Strategies

### Option A: glBufferSubData (OpenGL 3.3+)

**Simple, compatible**:
```cpp
glBindBuffer(GL_ARRAY_BUFFER, vbo);
glBufferData(GL_ARRAY_BUFFER, maxSize, nullptr, GL_STREAM_DRAW);
glBufferSubData(GL_ARRAY_BUFFER, 0, actualSize, data);
```

**Performance**: ~1-2ms upload for 10MB
**Use Case**: Good baseline, widely compatible

### Option B: Persistent Mapped Buffers (OpenGL 4.4+)

**Zero-copy, best performance**:
```cpp
GLbitfield flags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
glBufferStorage(GL_ARRAY_BUFFER, bufferSize, nullptr, flags);
void* ptr = glMapBufferRange(GL_ARRAY_BUFFER, 0, bufferSize, flags);

// Each frame (direct write, no copy):
memcpy(ptr + offset, vertexData, size);
```

**Performance**: ~0.5-1ms for 10MB (50% faster)
**Use Case**: If targeting OpenGL 4.4+, significant win

### Option C: Triple Buffering

**Avoid GPU stalls**:
```cpp
GLuint buffers[3];
size_t currentBuffer = 0;

// Each frame:
currentBuffer = (currentBuffer + 1) % 3;
glBindBuffer(GL_ARRAY_BUFFER, buffers[currentBuffer]);
// Upload to current buffer while GPU reads previous
```

**Memory Cost**: 3× VBO size (~48 MB for 16MB buffers)
**Performance**: Prevents stalls, smooth frame pacing

## Instanced Rendering

**Use Case**: Limited for us (mostly unique geometry)

**Where Applicable**:
- Identical grass blades (same base mesh, different positions)
- Repeated decorative elements

**Example**:
```cpp
// Upload per-instance data (transforms)
glBindBuffer(GL_ARRAY_BUFFER, instanceVBO);
glBufferData(GL_ARRAY_BUFFER, sizeof(Transform) * count, transforms, GL_STREAM_DRAW);

// Draw all instances in one call
glDrawElementsInstanced(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, 0, instanceCount);
```

**Limitation**: All instances must share exact same mesh. For animated/deformed entities, less useful.

## Optimization Techniques

### 1. Frustum Culling
Only batch visible entities:
```cpp
for (Entity entity : allEntities) {
    if (camera.frustum.Contains(entity.bounds)) {
        AddToBatch(entity);
    }
}
```

**Savings**: 50-90% reduction in geometry (depends on camera)

### 2. Occlusion Culling
Skip entities behind opaque objects (advanced, defer).

### 3. LOD (Level of Detail)
Reduce tessellation quality for distant objects:
```cpp
float distance = Distance(camera, entity);
TessellationQuality quality = GetLOD(distance);
// Fewer triangles = faster upload, rendering
```

### 4. Dirty Tracking
Only re-tessellate changed entities:
```cpp
if (entity.animationState.Changed() || entity.physics.AppliedForce()) {
    entity.mesh.MarkDirty();
}

// Only process dirty entities
for (Entity e : entities) {
    if (e.mesh.IsDirty()) {
        Tessellate(e);
        e.mesh.ClearDirty();
    }
}
```

**Savings**: 50-80% CPU time if most entities static

### 5. Spatial Partitioning
Chunk-based batching:
```cpp
for (Chunk chunk : visibleChunks) {
    for (Entity entity : chunk.entities) {
        AddToBatch(entity);
    }
}
```

## Memory Layout Optimization

### Structure of Arrays (SoA) vs Array of Structures (AoS)

**AoS** (less efficient):
```cpp
struct Vertex {
    vec2 position;
    vec2 texCoord;
    vec4 color;
};
std::vector<Vertex> vertices;
```

**SoA** (more efficient, SIMD-friendly):
```cpp
struct VertexData {
    std::vector<vec2> positions;
    std::vector<vec2> texCoords;
    std::vector<vec4> colors;
};
```

**Recommendation**: AoS is fine for simplicity unless profiling shows bottleneck.

## Comparison Matrix

| Strategy | Draw Calls | Upload Speed | Complexity | Best For |
|----------|------------|--------------|------------|----------|
| Material Batching | 10-50 | Good | Low | General use |
| + Texture Atlas | 5-20 | Good | Medium | Reduce texture switches |
| + Uber Shader | 1-10 | Good | High | Ultimate batching |
| Persistent Buffers | Same | Excellent | Medium | OpenGL 4.4+ |
| Triple Buffering | Same | Good | Medium | Avoid stalls |
| Instancing | 1 per type | Excellent | Low | Identical geometry |

## Recommended Strategy

**Phase 1 (Initial)**:
- Material-based batching
- glBufferSubData for uploads
- Basic texture atlasing

**Phase 2 (Optimize)**:
- Persistent mapped buffers (if OpenGL 4.4+)
- Better atlas packing
- Frustum culling

**Phase 3 (Polish)**:
- LOD system
- Dirty tracking
- Multi-threaded tessellation

## Related Documentation

- [architecture.md](./architecture.md) - Overall rendering tiers
- [rendering-backend-options.md](./rendering-backend-options.md) - Custom batched renderer
- [performance-targets.md](./performance-targets.md) - Performance budgets
- [memory-management.md](./memory-management.md) - VBO memory strategies

## References

- LearnOpenGL Instancing: https://learnopengl.com/Advanced-OpenGL/Instancing
- Persistent Mapped Buffers: https://www.khronos.org/opengl/wiki/Buffer_Object#Persistent_mapping
- Texture Atlasing: https://en.wikipedia.org/wiki/Texture_atlas
