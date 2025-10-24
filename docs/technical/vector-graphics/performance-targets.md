# Performance Targets and Profiling

Created: 2025-10-24
Status: Design

## Target Metrics

### Frame Rate
- **Target**: 60 FPS (16.67ms per frame)
- **Minimum Acceptable**: 30 FPS (33.33ms per frame)
- **Stretch Goal**: 120 FPS (8.33ms per frame)

### Entity Count
- **Target**: 10,000 visible animated entities @ 60 FPS
- **Stretch**: 50,000 entities @ 60 FPS

### Rendering Performance
- **GPU Draw Calls**: <100 per frame (target: 10-50)
- **GPU Rendering Time**: <6ms per frame
- **Triangles**: 200k-500k per frame (batched efficiently)

### CPU Performance
- **Tessellation**: <2ms per frame
- **Animation Updates**: <1ms per frame
- **Physics/Collision**: <2ms per frame
- **Game Logic**: <4ms per frame

## Frame Budget (16.67ms @ 60 FPS)

| System | Budget | Notes |
|--------|--------|-------|
| Game Logic (ECS) | 4ms | Entity updates, AI, simulation |
| Animation Updates | 1ms | Spline deformation calculations |
| Tessellation | 2ms | Convert deformed paths → triangles |
| Physics & Collision | 2ms | Collision detection, resolution |
| GPU Upload | 1ms | Stream VBO data to GPU |
| GPU Rendering | 6ms | Draw calls, fragment shading |
| Other/Overhead | 0.67ms | Profiling, debug, misc |
| **Total** | **16.67ms** | |

## Performance Budgets by Tier

### Tier 1: Static Backgrounds

**Target**: Zero CPU cost during gameplay

| Metric | Budget |
|--------|--------|
| Load Time | <500ms for all atlases |
| Memory | 200 MB (texture atlases) |
| Draw Calls | 1-5 (one per atlas) |
| GPU Time | <1ms |

**Pre-computation**:
- Rasterization: Offline or first load only
- Atlas generation: One-time cost

### Tier 2: Semi-Static Structures

**Target**: Minimal CPU, cached GPU meshes

| Metric | Budget |
|--------|--------|
| Tessellation | Amortized <0.1ms (cached) |
| Memory | 100 MB (mesh cache) |
| Draw Calls | 5-20 (batched by material) |
| GPU Time | <2ms |

**Assumptions**:
- 1,000 unique structures on screen
- 90% cache hit rate
- Only new/evicted meshes tessellated

### Tier 3: Dynamic Animated Entities

**Target**: 10,000 entities @ 60 FPS

| Metric | Budget |
|--------|--------|
| Animation Update | 1ms (0.1μs per entity) |
| Tessellation | 2ms (0.2μs per entity) |
| Memory | 50 MB (streaming VBOs) |
| Draw Calls | 10-30 (batched by material) |
| GPU Upload | 1ms (10-20 MB/frame) |
| GPU Time | 3ms |

**Critical Optimizations**:
- LOD: 50-70% entities at reduced quality
- Dirty tracking: Only 30-50% re-tessellate per frame
- Frustum culling: Only tessellate visible entities
- Batching: Group by material (10-30 materials)

## Profiling Methodology

### CPU Profiling

**Use high-resolution timers**:

```cpp
class ScopedTimer {
    const char* name;
    std::chrono::high_resolution_clock::time_point start;

public:
    ScopedTimer(const char* n) : name(n) {
        start = std::chrono::high_resolution_clock::now();
    }

    ~ScopedTimer() {
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        LOG_PROFILE("%s: %.2f ms", name, duration.count() / 1000.0f);
    }
};

// Usage
void UpdateAnimations() {
    SCOPED_TIMER("Animation Update");
    // ... work
}
```

**Profile per-frame** (development builds):
```cpp
void RenderFrame() {
    SCOPED_TIMER("Total Frame");
    {
        SCOPED_TIMER("Game Logic");
        UpdateGameLogic();
    }
    {
        SCOPED_TIMER("Animation");
        UpdateAnimations();
    }
    {
        SCOPED_TIMER("Tessellation");
        TessellateEntities();
    }
    {
        SCOPED_TIMER("Rendering");
        RenderScene();
    }
}
```

### GPU Profiling

**Use OpenGL timer queries**:

```cpp
class GPUTimer {
    GLuint queryIDs[2];

public:
    void Begin() {
        glGenQueries(2, queryIDs);
        glQueryCounter(queryIDs[0], GL_TIMESTAMP);
    }

    float End() { // Returns milliseconds
        glQueryCounter(queryIDs[1], GL_TIMESTAMP);

        GLuint64 startTime, endTime;
        glGetQueryObjectui64v(queryIDs[0], GL_QUERY_RESULT, &startTime);
        glGetQueryObjectui64v(queryIDs[1], GL_QUERY_RESULT, &endTime);

        glDeleteQueries(2, queryIDs);

        return (endTime - startTime) / 1000000.0f; // ns → ms
    }
};

// Usage
GPUTimer timer;
timer.Begin();
RenderVectorGraphics();
float gpuTime = timer.End();
LOG_PROFILE("GPU Rendering: %.2f ms", gpuTime);
```

### Memory Profiling

**Track allocations**:

```cpp
class MemoryTracker {
    std::atomic<size_t> totalAllocated{0};
    std::atomic<size_t> peakAllocated{0};

    void* Allocate(size_t bytes) {
        totalAllocated += bytes;
        peakAllocated = std::max(peakAllocated.load(), totalAllocated.load());
        return malloc(bytes);
    }

    void Free(void* ptr, size_t bytes) {
        totalAllocated -= bytes;
        free(ptr);
    }

    size_t GetPeakUsage() { return peakAllocated; }
};
```

**GPU memory** (via GL queries):
```cpp
GLint totalMemKB, availableMemKB;
glGetIntegerv(GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX, &totalMemKB);
glGetIntegerv(GL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX, &availableMemKB);
size_t usedMemKB = totalMemKB - availableMemKB;
```

### Frame Time Graph

**Ring buffer for visualization**:

```cpp
class FrameTimeGraph {
    static const int HISTORY_SIZE = 300; // 5 seconds @ 60 FPS
    float frameTimes[HISTORY_SIZE];
    int currentIndex = 0;

    void RecordFrame(float frameTime) {
        frameTimes[currentIndex] = frameTime;
        currentIndex = (currentIndex + 1) % HISTORY_SIZE;
    }

    float GetAverageFPS() {
        float sum = 0.0f;
        for (float t : frameTimes) sum += t;
        return 1000.0f / (sum / HISTORY_SIZE);
    }

    float Get99thPercentile() {
        std::vector<float> sorted(frameTimes, frameTimes + HISTORY_SIZE);
        std::sort(sorted.begin(), sorted.end());
        return sorted[(int)(HISTORY_SIZE * 0.99)];
    }
};
```

## Performance Testing Scenarios

### Test 1: Baseline (Static Scene)

**Setup**: 10,000 static entities, no animation
**Expected**:
- Frame time: <10ms
- Draw calls: 10-20
- GPU time: <3ms

**Validates**: Batching, rendering pipeline

### Test 2: Full Animation (Worst Case)

**Setup**: 10,000 animated entities, all visible, all animating
**Expected**:
- Frame time: <16.67ms (60 FPS)
- Animation: <1ms
- Tessellation: <2ms
- Draw calls: 10-30
- GPU time: <6ms

**Validates**: Animation system, tessellation performance

### Test 3: LOD Effectiveness

**Setup**: 10,000 entities at varying distances
**Expected**:
- Frame time: <12ms (better than worst case)
- Only ~30% tessellated each frame (LOD + dirty tracking)

**Validates**: LOD system, culling

### Test 4: Stress Test

**Setup**: 50,000 entities (stretch goal)
**Expected**:
- Frame time: <16.67ms with aggressive LOD
- <16.67ms if staying within budget

**Validates**: Scalability

## Optimization Priorities

### If Tessellation is Bottleneck (>2ms):

1. **LOD**: Reduce tessellation quality for distant objects
2. **Dirty Tracking**: Only re-tessellate changed entities
3. **Multi-threading**: Parallelize tessellation across CPU cores
4. **Simplify Curves**: Pre-flatten complex curves to polylines

### If GPU Upload is Bottleneck (>1ms):

1. **Persistent Mapped Buffers**: Upgrade to OpenGL 4.4+ zero-copy
2. **Reduce Data Size**: Quantize vertices (use half-floats)
3. **Upload Less**: Better dirty tracking, LOD

### If Draw Calls are Bottleneck (>100 calls):

1. **Better Batching**: Reduce material variety
2. **Texture Atlasing**: Pack more textures
3. **Uber Shader**: Single shader for all materials

### If GPU Rendering is Bottleneck (>6ms):

1. **Reduce Triangles**: More aggressive LOD
2. **Simplify Shaders**: Optimize fragment shader
3. **Frustum Culling**: Render fewer entities

## Performance Monitoring (Development)

**Real-time overlay** (F12 in debug builds):

```
─────────────────────────────────────
FPS: 62.3 (16.04 ms/frame)
Entities: 12,453 visible (23,891 total)
Tessellated: 3,842 (31%)
Draw Calls: 24
Triangles: 324,582

CPU Breakdown:
  Game Logic:   3.2ms
  Animation:    0.8ms
  Tessellation: 1.9ms
  Upload:       0.7ms

GPU Time: 5.4ms

Memory:
  Tier 1: 156 MB
  Tier 2: 84 MB
  Tier 3: 41 MB
  Scratch: 8 MB
─────────────────────────────────────
```

## Profiling Tools

### Recommended Tools

- **Tracy Profiler**: Real-time, low overhead
- **Optick**: Visual profiler, CPU+GPU
- **RenderDoc**: GPU frame capture, debugging
- **Nsight Graphics** (NVIDIA): Deep GPU profiling
- **Intel GPA**: Multi-vendor GPU profiling

### Integration Example (Tracy):

```cpp
#include <tracy/Tracy.hpp>

void UpdateAnimations() {
    ZoneScoped; // Tracy macro
    // ... work
}
```

## Continuous Performance Testing

**Automated benchmarks** (CI/CD):

```bash
# Run benchmark suite
./build/ui-sandbox --benchmark vector_rendering
# Outputs: JSON with performance metrics
# Fail build if metrics regress >10%
```

## Related Documentation

- [architecture.md](./architecture.md) - System design
- [batching-strategies.md](./batching-strategies.md) - Draw call optimization
- [lod-system.md](./lod-system.md) - LOD strategies
- [memory-management.md](./memory-management.md) - Memory budgets

## References

- Tracy Profiler: https://github.com/wolfpld/tracy
- OpenGL Profiling: https://www.khronos.org/opengl/wiki/Performance
- Game Engine Performance: https://www.gdcvault.com/play/1020394/
