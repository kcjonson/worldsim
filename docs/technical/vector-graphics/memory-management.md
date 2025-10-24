# Vector Graphics Memory Management

Created: 2025-10-24
Status: Design

## Overview

Memory architecture for vector graphics rendering system across all four tiers. Total budget: ~350 MB.

## Memory Budget

| Tier | Purpose | Budget | Storage Type | Allocation Pattern |
|------|---------|--------|--------------|-------------------|
| Tier 1 | Texture atlases (static backgrounds) | 200 MB | GPU textures | Permanent |
| Tier 2 | Cached meshes (semi-static structures) | 100 MB | GPU VBOs | LRU cache |
| Tier 3 | Streaming buffers (dynamic entities) | 50 MB | GPU VBOs | Per-frame, ring buffer |
| Scratch | Tessellation temporary data | 10-20 MB | CPU RAM | Arena allocator |

**Total**: ~360-370 MB

## Tier 1: Texture Atlas Memory

**Pre-rasterized tile backgrounds**:

```cpp
class TextureAtlasManager {
    struct Atlas {
        GLuint texture;
        int width, height;        // e.g., 2048×2048 or 4096×4096
        RectPacker packer;        // Tracks free space
        size_t memoryUsage;       // RGBA = width × height × 4 bytes
    };

    std::vector<Atlas> atlases;
    size_t maxMemory = 200 * 1024 * 1024; // 200 MB
    size_t currentMemory = 0;

    AtlasRegion AddTexture(int width, int height, const uint8_t* pixels) {
        // Try to pack into existing atlas
        for (Atlas& atlas : atlases) {
            if (auto rect = atlas.packer.Pack(width, height)) {
                UploadSubTexture(atlas, rect, pixels);
                return {atlas.texture, rect};
            }
        }

        // Create new atlas if under budget
        if (currentMemory + AtlasSize() < maxMemory) {
            atlases.push_back(CreateAtlas(4096, 4096));
            currentMemory += AtlasSize();
            return AddTexture(width, height, pixels);
        }

        // Out of memory
        LOG_WARNING("Texture atlas budget exceeded");
        return nullptr;
    }
};
```

**Atlas Sizes**:
- 2048×2048 RGBA = 16 MB
- 4096×4096 RGBA = 64 MB
- Budget allows ~3-12 atlases depending on size

## Tier 2: Mesh Cache Memory

**LRU cache for semi-static meshes**:

```cpp
class MeshCache {
    struct CacheEntry {
        VectorMeshHandle mesh;
        size_t memorySize;
        uint64_t lastAccessFrame;
    };

    std::unordered_map<CacheKey, CacheEntry> cache;
    size_t maxMemory = 100 * 1024 * 1024; // 100 MB
    size_t currentMemory = 0;
    uint64_t frameCount = 0;

    VectorMeshHandle GetOrCreate(const CacheKey& key) {
        auto it = cache.find(key);
        if (it != cache.end()) {
            it->second.lastAccessFrame = frameCount;
            return it->second.mesh;
        }

        // Tessellate new mesh
        VectorMesh* mesh = Tessellate(key.asset, key.variation);
        size_t meshSize = mesh->GetMemorySize();

        // Evict LRU if needed
        while (currentMemory + meshSize > maxMemory && !cache.empty()) {
            EvictLRU();
        }

        // Add to cache
        cache[key] = {mesh, meshSize, frameCount};
        currentMemory += meshSize;
        return mesh;
    }

    void EvictLRU() {
        auto oldest = std::min_element(cache.begin(), cache.end(),
            [](const auto& a, const auto& b) {
                return a.second.lastAccessFrame < b.second.lastAccessFrame;
            });

        currentMemory -= oldest->second.memorySize;
        DeleteMesh(oldest->second.mesh);
        cache.erase(oldest);
    }
};
```

**Mesh Memory Size**:
```cpp
size_t VectorMesh::GetMemorySize() const {
    size_t vertexSize = vertices.size() * sizeof(Vertex);
    size_t indexSize = indices.size() * sizeof(uint32_t);
    return vertexSize + indexSize;
}
```

**Example**: 1,000 cached meshes × 100 KB average = 100 MB

## Tier 3: Streaming VBO Memory

**Double/triple buffering for dynamic geometry**:

```cpp
class StreamingVBO {
    static const int BUFFER_COUNT = 3;
    static const size_t BUFFER_SIZE = 16 * 1024 * 1024; // 16 MB per buffer

    GLuint buffers[BUFFER_COUNT];
    void* mappedPointers[BUFFER_COUNT];
    int currentBuffer = 0;

    void Initialize() {
        for (int i = 0; i < BUFFER_COUNT; i++) {
            glGenBuffers(1, &buffers[i]);
            glBindBuffer(GL_ARRAY_BUFFER, buffers[i]);

            // Persistent mapped buffer (OpenGL 4.4+)
            GLbitfield flags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
            glBufferStorage(GL_ARRAY_BUFFER, BUFFER_SIZE, nullptr, flags);
            mappedPointers[i] = glMapBufferRange(GL_ARRAY_BUFFER, 0, BUFFER_SIZE, flags);
        }
    }

    void* BeginFrame() {
        currentBuffer = (currentBuffer + 1) % BUFFER_COUNT;
        return mappedPointers[currentBuffer];
    }

    GLuint GetCurrentBuffer() {
        return buffers[currentBuffer];
    }
};
```

**Total Memory**: 3 × 16 MB = 48 MB

## Scratch Memory: Arena Allocators

**Per-frame temporary allocations for tessellation**:

```cpp
class MemoryArena {
    uint8_t* memory;
    size_t capacity;
    size_t used;

public:
    MemoryArena(size_t size) : capacity(size), used(0) {
        memory = (uint8_t*)malloc(size);
    }

    void* Allocate(size_t bytes, size_t alignment = 8) {
        size_t padding = (alignment - (used % alignment)) % alignment;
        size_t offset = used + padding;

        if (offset + bytes > capacity) {
            LOG_ERROR("Arena out of memory");
            return nullptr;
        }

        void* ptr = memory + offset;
        used = offset + bytes;
        return ptr;
    }

    void Reset() {
        used = 0; // Zero-cost "free"
    }

    ~MemoryArena() {
        free(memory);
    }
};

// Per-frame usage
class TessellationSystem {
    MemoryArena frameArena; // 10 MB

    void Update() {
        frameArena.Reset(); // Reset at frame start

        for (Entity& e : entities) {
            // Allocate temporary data from arena
            vec2* points = (vec2*)frameArena.Allocate(sizeof(vec2) * pointCount);
            // Use points for tessellation...
            // No need to free - arena resets next frame
        }
    }
};
```

**Benefits**:
- **10-100× faster** than malloc/free
- **Zero fragmentation**
- **Simple**: Just reset at frame end

## Memory Profiling

```cpp
class MemoryProfiler {
    struct Stats {
        size_t tier1Textures;
        size_t tier2Meshes;
        size_t tier3Streaming;
        size_t scratchUsage;
        size_t totalGPU;
        size_t totalCPU;
    };

    Stats GetStats() {
        Stats stats;
        stats.tier1Textures = atlasManager.GetMemoryUsage();
        stats.tier2Meshes = meshCache.GetMemoryUsage();
        stats.tier3Streaming = streamingVBO.GetMemoryUsage();
        stats.scratchUsage = frameArena.GetUsed();
        stats.totalGPU = stats.tier1Textures + stats.tier2Meshes + stats.tier3Streaming;
        stats.totalCPU = stats.scratchUsage;
        return stats;
    }

    void LogStats() {
        Stats s = GetStats();
        LOG_INFO("Memory: Tier1=%zu MB, Tier2=%zu MB, Tier3=%zu MB, Scratch=%zu MB",
                 s.tier1Textures / MB, s.tier2Meshes / MB,
                 s.tier3Streaming / MB, s.scratchUsage / MB);
    }
};
```

## Allocation Strategies

### GPU Memory

**Prefer persistent allocations**:
- Tier 1: Allocate once at startup/load
- Tier 2: Allocate on-demand, cache with LRU
- Tier 3: Allocate once, reuse every frame

**Avoid**:
- Per-frame glGenBuffers/glDeleteBuffers (very slow)
- Frequent texture uploads (use atlases)

### CPU Memory

**Use arenas for temporary data**:
```cpp
// Good: Arena allocation
void ProcessEntities() {
    MemoryArena arena(1024 * 1024);
    for (Entity& e : entities) {
        float* tempData = (float*)arena.Allocate(sizeof(float) * 1000);
        Process(e, tempData);
    }
    // Arena destroyed, all memory freed at once
}

// Bad: Per-entity allocation
void ProcessEntities() {
    for (Entity& e : entities) {
        float* tempData = new float[1000];  // Slow!
        Process(e, tempData);
        delete[] tempData;                  // Slow!
    }
}
```

## Out-of-Memory Handling

**Tier 1 (Textures)**:
- Fallback to lower LOD
- Evict least-used atlases (if dynamic)
- Warning to user about asset limits

**Tier 2 (Meshes)**:
- LRU eviction (automatic)
- Reduce cache size if needed
- Fall back to Tier 3 (re-tessellate each frame)

**Tier 3 (Streaming)**:
- Reduce entity count (cull more aggressively)
- Lower LOD
- Skip distant/offscreen entities

## Related Documentation

- [architecture.md](./architecture.md) - Memory architecture per tier
- [batching-strategies.md](./batching-strategies.md) - VBO streaming
- [performance-targets.md](./performance-targets.md) - Memory budgets
- [/docs/technical/memory-arenas.md](../memory-arenas.md) - Memory arena implementation

## References

- Memory Arenas: https://www.gingerbill.org/article/2019/02/08/memory-allocation-strategies-002/
- GPU Memory Management: https://developer.nvidia.com/opengl-vulkan
