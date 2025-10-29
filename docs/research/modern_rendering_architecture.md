# Modern rendering architecture for production C++ game engines: retained vs immediate mode

**Modern production game engines overwhelmingly use retained-mode architectures with persistent object references**, not immediate-mode drawing. Unity, Unreal Engine, and Godot all maintain scene graphs where visual objects persist in memory between frames, optimized with data-oriented patterns for cache efficiency. For your vector graphics engine, **the correct answer is a hybrid approach**: use persistent shape objects stored in contiguous arrays (not shared_ptr), organize them data-oriented (Structure of Arrays when beneficial), and render through a command buffer system that batches draw calls by state. This combines retained-mode's organizational benefits with immediate-mode's rendering control while enabling modern optimizations like GPU instancing and multi-threading.

## Every major engine chose persistent objects for fundamental reasons

The research across Unity, Unreal Engine, and Godot reveals unanimous architectural consensus. All three engines maintain **persistent scene graphs** where visual objects remain in memory until explicitly destroyed. Unity's GameObject/Component system stores objects in hierarchical relationships with dedicated renderer components. Unreal's Actor/Component model uses cached mesh drawing for static objects, rebuilding only dynamic elements each frame. Godot's node-based architecture automatically integrates visual nodes into its scene tree, with the RenderingServer managing GPU resources through persistent Resource IDs (RIDs).

This retained approach enables crucial optimizations impossible with pure immediate-mode rendering. **GPU resources stay resident in video memory** rather than streaming each frame, reducing CPU-GPU bandwidth by orders of magnitude. Scene graphs enable hierarchical culling where entire branches can be rejected early in the rendering pipeline. Transform hierarchies propagate naturally through parent-child relationships, essential for skeletal animation and attached objects. Material batching and draw call sorting require persistent object metadata to group efficiently.

The historical context illuminates why this consensus emerged. Direct3D Retained Mode (DirectX 2-3) failed not because retained mode was wrong, but because it was inflexible and high-level when developers needed control. Only two significant games ever shipped with it: Lego Island and Lego Rock Raiders. Microsoft discontinued it after DirectX 3 due to overwhelming developer demand for direct control. Modern engines learned from this failure by **separating logical scene management (retained) from rendering control (explicit command buffers)**.

## Data-oriented design transformed how persistent objects work

The ECS (Entity Component System) revolution represents the most significant architectural shift in modern game engines. Unity DOTS, Unreal Mass Entity, and frameworks like Bevy, EnTT, and Flecs demonstrate how data-oriented design addresses retained mode's traditional performance problems. The core insight from Mike Acton's influential work: **software doesn't exist to model the real world; it exists to transform data**. Cache misses cost 50-75x more than L1 cache hits, making memory layout the dominant performance factor.

Traditional object-oriented retained mode scatters data across memory through pointer indirection. Consider a naive GameObject approach storing 10,000 entities with health components, where each component contains current health, max health, regeneration rate, shield points, and immunity flags. With Array of Structures layout, approximately 3,333 cache lines must be loaded even when a health regeneration system only needs current health and regen rate. Structure of Arrays layout for the same data requires loading only the two relevant arrays—roughly 625 cache lines each, totaling 1,250 cache lines. **This 62% reduction in cache traffic translates to 2-3x real performance improvements** measured in production particle systems.

Unity's Megacity demo validated these principles at massive scale: 100,000+ dynamic entities rendering at 30-60 FPS on mobile hardware, with 4.5 million building blocks and 200,000 AI-controlled vehicles. The performance gains came from three synergistic technologies: the Burst compiler providing 10-30x speedups on C# code through LLVM optimization, the Job System parallelizing work across all CPU cores, and archetype-based memory layout minimizing cache misses. Entities with identical component compositions group together in contiguous memory chunks optimized for cache line size.

## Memory management decisions cascade through architecture

The choice between smart pointers and raw pointers fundamentally impacts performance in rendering systems. Research across Unreal Engine documentation, technical articles, and production codebases reveals consistent guidance: **raw pointers dominate hot paths, unique_ptr manages ownership, shared_ptr handles shared resources only**. Unreal's official documentation explicitly states that "some Smart Pointer types are slower than raw C++ pointers, and this overhead makes them less useful in low-level engine code, such as rendering."

The performance overhead comes from reference counting atomic operations. Every shared_ptr copy or assignment must atomically increment or decrement counters, introducing synchronization overhead and preventing compiler optimizations. The control block requires separate heap allocation beyond the object itself. For rendering systems processing thousands of objects per frame at 60 Hz, these costs accumulate rapidly. More critically, **shared_ptr encourages scattered memory allocation**, the exact pattern data-oriented design fights against.

Object pooling provides deterministic allocation patterns essential for stable frame times. Frequent allocation and deallocation causes memory fragmentation critical on console and mobile platforms, plus garbage collection spikes creating 30-50ms frame time stutters. Pre-allocated pools with free list management achieve constant-time O(1) allocation versus malloc's logarithmic search, eliminate fragmentation entirely, and provide predictable memory usage. Production measurements show **50%+ reduction in frame time spikes** from proper object pooling.

Modern explicit APIs like Vulkan and DirectX 12 impose additional memory management requirements. Vulkan typically limits applications to 4,096 total memory allocations, forcing sub-allocation from larger 256MB blocks. Applications must manually query memory requirements, allocate device memory, and bind resources. The VMA (Vulkan Memory Allocator) and D3D12MA libraries became essential because hand-rolling these systems correctly proves extremely complex. These APIs make memory management a first-class architectural concern, not an implementation detail.

## Scene graphs versus flat rendering: the false dichotomy

The perceived conflict between scene graphs and flat rendering lists represents a misunderstanding of modern engine architecture. Production engines **use scene graphs for logical organization and flatten to optimized arrays for rendering**. This separation of concerns appears consistently across Unity, Unreal, and Godot despite their different architectural philosophies.

Scene graphs excel at specific tasks: transform hierarchies naturally express parent-child relationships essential for skeletal animation, cameras attached to vehicles, or weapons held by characters. Frustum culling benefits from hierarchical rejection where testing a parent's bounding volume can cull entire subtrees. Game logic often thinks in hierarchical terms—UI panels containing buttons, characters with equipped items. For these logical relationships, tree structures provide intuitive APIs and efficient spatial queries.

Rendering requires entirely different data organization. Optimal rendering order cannot be determined by traversing a scene graph—such structures must be linearized and sorted by rendering state. The cost hierarchy from most to least expensive: shader changes, texture binding, mesh switching, per-instance parameters. Reducing shader switches by 50% can double frame rate. This requires sorting all draw calls by packed state keys, impossible in tree traversal order. Physics systems similarly flatten spatial data into octrees, BVHs, or broad-phase grids, never operating on the game's logical scene graph.

The hybrid pattern appears in production code: maintain a GameObject or Actor hierarchy for game logic, but rendering systems query visible objects, pack them into flat arrays, sort by material/shader, and issue batched GPU commands. Godot's RenderingServer architecture exemplifies this separation—the node tree remains intact for game logic while the rendering backend receives optimized command streams. Unity DOTS takes this further by completely separating GameObject authoring (edit time) from entity runtime (optimized execution). The scene graph becomes an authoring convenience, not a runtime structure.

## Draw call batching determines rendering performance

Modern rendering performance depends primarily on minimizing GPU state changes, not raw triangle count. A single shader change costs more than drawing 10,000 triangles with that shader. This fundamental GPU architecture fact shapes all rendering engine design decisions. Unity documentation demonstrates that **sorting draw calls by shader can increase rendering performance twice**, far exceeding gains from geometric optimization.

Static batching pre-combines meshes at load time, storing them in world-space coordinates. This eliminates per-object draw calls for static geometry, achieving 90%+ draw call reduction for level architecture. The memory overhead—storing combined meshes—matters less than the massive CPU and GPU time savings. However, individual objects cannot be culled, and the approach only works for truly static geometry.

GPU instancing represents the modern solution: one draw call renders thousands of identical objects with per-instance data (transform, color, material parameters). Modern APIs (Vulkan, DX12, Metal) provide explicit instancing commands where the CPU submits one command buffer and the GPU processes instance arrays directly. This scales to tens of thousands of objects per call. Unity's DOTS renderer and Unreal's instanced static meshes rely heavily on this technique.

The sorting algorithm requires careful implementation. Pack rendering state into 64-bit sort keys with most expensive state changes in the most significant bits: shader ID in bits 48-63, texture ID in 32-47, mesh ID in 16-31, depth or instance ID in 0-15. A single std::sort establishes optimal rendering order. For transparency, compute camera distance and sort back-to-front separately. This approach, combined with proper state caching (don't rebind already-bound resources), minimizes redundant GPU operations.

## ECS rendering patterns provide concrete implementation guidance

Unity DOTS demonstrates production ECS rendering through the Hybrid Renderer system bridging ECS entities with Unity's Scriptable Render Pipeline. Required components for rendering include LocalToWorld for transform, RenderMesh for geometry and material, and RenderBounds for culling. Material properties can be overridden per-entity through IComponentData components decorated with MaterialProperty attributes. Systems update these components in parallel using Burst-compiled C# code, achieving near-native performance.

```csharp
[MaterialProperty("_BaseColor")]
public struct CustomColor : IComponentData {
    public float4 Value;
}

partial class ColorAnimationSystem : SystemBase {
    protected override void OnUpdate() {
        float time = (float)Time.ElapsedTime;
        Entities.ForEach((ref CustomColor color) => {
            color.Value = new float4(
                math.abs(math.sin(time)),
                math.abs(math.cos(time)),
                0.5f, 1.0f
            );
        }).ScheduleParallel();
    }
}
```

Bevy's architecture separates the main ECS world (game logic) from the render world (GPU operations). The Extract stage synchronizes necessary data between worlds at a clear boundary. This enables pipelined rendering where the renderer processes frame N while gameplay updates frame N+1. The render world's entities clear each frame while resources persist, providing clean separation between transient commands and persistent GPU resources.

EnTT's sparse set architecture provides exceptional performance for component iteration. Each component type maintains its own compact array of entities possessing that component. Views across multiple component types iterate only entities matching all requirements. The library's header-only design and zero-registration requirement enable immediate adoption. Production usage in Minecraft Bedrock Edition, Diablo II Resurrected, and Call of Duty Vanguard validates the architecture at AAA scale.

Flecs introduces pipeline phases allowing custom rendering stages. Systems declare dependencies through phase membership, enabling automatic scheduling that respects rendering order (PreRender → Render → PostRender). This declarative approach ensures correct execution order while enabling parallelization where dependencies allow. The modular design permits removing unused features, crucial for embedded and web platforms.

## Concrete architecture recommendation for vector graphics engines

For a vector graphics game engine, the optimal architecture combines retained object management with immediate rendering control through a command buffer system. **Do not use std::shared_ptr for individual shapes**—the reference counting overhead and scattered allocation pattern directly conflicts with performance requirements. Instead, store shape components in std::vector containers enabling contiguous memory layout.

```cpp
// Component arrays (cache-friendly)
struct RenderSystem {
    std::vector<Transform> transforms_;
    std::vector<CircleShape> circles_;
    std::vector<RectangleShape> rectangles_;
    std::vector<PathShape> paths_;
    std::vector<StyleData> styles_;  // Fill, stroke, gradients
    
    // Spatial acceleration
    Quadtree spatialIndex_;
    
    // Command buffer (rebuilt each frame)
    std::vector<RenderCommand> commands_;
    
    void update(Camera& camera) {
        commands_.clear();
        
        // 1. Spatial query for visible shapes
        std::vector<uint32_t> visible;
        spatialIndex_.query(camera.bounds, visible);
        
        // 2. Build render commands
        for (uint32_t idx : visible) {
            commands_.push_back(createCommand(idx));
        }
        
        // 3. Sort by rendering state (shader, blend mode, texture)
        std::sort(commands_.begin(), commands_.end(),
            [](auto& a, auto& b) { return a.sortKey < b.sortKey; });
    }
    
    void render() {
        for (auto& cmd : commands_) {
            cmd.execute();
        }
    }
};
```

Shape lifecycle management requires careful attention to active/inactive patterns. Keep active shapes contiguous in arrays by swapping with the last active element on deactivation. This enables pure sequential iteration without branching on active flags, achieving dramatically better cache utilization. Measurements show **50x improvements over pointer-based approaches** through this simple technique.

Consider Structure of Arrays layout when shape types have many independent properties. Vector graphics often includes position, rotation, scale, fill color, stroke color, stroke width, blend mode, and shadow properties. If rendering passes need only position and style (culling, picking), or only geometry data (tessellation), SoA layout prevents loading unused properties. However, if most systems need most properties, Array of Structures simplifies code without performance cost.

Use smart pointers judiciously at architectural boundaries: unique_ptr for subsystem ownership (RenderSystem owns MaterialManager, ShaderCache), shared_ptr for genuinely shared resources like texture atlases or compiled shader programs. Raw pointers or indices for component references within the rendering system. This pattern provides clear ownership semantics at system boundaries while enabling performance-critical tight loops.

## Threading and multi-frame patterns enable modern hardware utilization

Modern CPUs provide 8-16+ cores, but naive rendering architectures bottleneck on single-threaded command buffer generation. The solution: **separate game logic, rendering command generation, and GPU submission across threads and frames**. Unity DOTS achieves this through the C# Job System automatically parallelizing entity iteration. Unreal Engine runs dedicated render thread(s) processing commands while the game thread updates the next frame. Bevy's two-world architecture explicitly enables this separation.

Double-buffering provides thread-safe communication between game and render threads. The game thread writes commands to a back buffer while the render thread consumes the front buffer. A mutex protects only the buffer swap operation, not individual command submission. This enables completely lock-free submission from game code.

```cpp
class RenderQueue {
    std::vector<RenderCommand> buffers_[2];
    std::atomic<int> readIndex_{0};
    std::mutex swapMutex_;
    
    void submit(RenderCommand cmd) {
        int writeIndex = 1 - readIndex_.load();
        buffers_[writeIndex].push_back(cmd);  // Lock-free
    }
    
    void present() {
        std::lock_guard lock(swapMutex_);
        readIndex_.store(1 - readIndex_.load());
        int writeIndex = 1 - readIndex_.load();
        buffers_[writeIndex].clear();
    }
    
    void render() {
        int read = readIndex_.load();
        for (auto& cmd : buffers_[read]) {
            cmd.execute();
        }
    }
};
```

Frame pipelining extends this concept: while the CPU generates commands for frame N+1, the GPU processes frame N. This requires careful synchronization to prevent overwriting data the GPU still needs. Modern explicit APIs provide fence objects signaling GPU completion, enabling CPU waiting only when necessary. The pattern requires buffering render resources (typically 2-3 frames), increasing memory usage but dramatically improving throughput on multi-core systems.

## The fixed timestep pattern remains essential for game engines

Glenn Fiedler's "Fix Your Timestep" article established the industry-standard game loop pattern: **fixed timestep for game logic updates, variable timestep for rendering**. This architecture ensures deterministic physics and gameplay behavior regardless of frame rate, critical for networked games, replays, and fair gameplay. The render function receives an interpolation factor enabling smooth visual motion even when logic updates at discrete intervals.

```cpp
double previous = getCurrentTime();
double lag = 0.0;
const double MS_PER_UPDATE = 1.0/60.0;  // 60 Hz logic

while (true) {
    double current = getCurrentTime();
    double elapsed = current - previous;
    previous = current;
    lag += elapsed;
    
    processInput();
    
    // Update at fixed timestep until caught up
    while (lag >= MS_PER_UPDATE) {
        update(MS_PER_UPDATE);
        lag -= MS_PER_UPDATE;
    }
    
    // Render with interpolation
    render(lag / MS_PER_UPDATE);
}
```

This pattern handles both fast and slow hardware gracefully. On fast systems, rendering runs at high frame rates with interpolated positions providing smooth motion between logic updates. On slow systems, the logic continues at fixed rate while rendering may drop frames, but gameplay remains fair and deterministic. Without this separation, physics behavior depends on frame rate—objects move faster on faster hardware, tunneling occurs at high speeds, and networked games desynchronize.

Vector graphics rendering benefits especially from this architecture. Tessellation and path stroking are computationally expensive—when the logic update rate separates from render rate, the engine can dynamically adjust tessellation detail based on frame time budget without affecting gameplay. Animations interpolate smoothly regardless of render performance.

## Make architectural decisions based on your engine's specific constraints

The universal patterns (retained objects, data-oriented layout, state sorting, fixed timestep) apply broadly, but specific implementation depends on your engine's requirements. Consider these decisive factors for a vector graphics engine:

**Target platform constraints**: Mobile platforms demand memory efficiency and power management. Desktop enables aggressive caching and higher tessellation. Web requires WASM compatibility and limited memory. Console provides fixed hardware for predictable optimization. **Choose memory layouts accordingly**—mobile benefits most from SoA reducing bandwidth, desktop can afford AoS simplicity when cache isn't critical.

**Content complexity expectations**: Rendering 100 shapes requires different architecture than 100,000. Small counts permit simpler immediate-mode patterns without performance consequence. Large counts demand careful batching, spatial indexing, and potentially ECS organization. Games like Spider-Man and Unity's Megacity demo required fundamental architectural changes to scale. **Profile your target content scale early** to avoid premature or late optimization.

**Team expertise and velocity**: ECS architecture requires different thinking than traditional OOP. Unity DOTS has steep learning curves but enables massive scale. Godot's node system provides intuitive development at the cost of some performance. Unreal balances both with traditional Actors for most content and Mass entities for crowds. **Match architecture to team capabilities**—a brilliant architecture the team can't maintain fails**.

**Visual requirements**: Real-time ray tracing, advanced lighting, and photorealistic materials favor Unreal-style deferred rendering. Stylized graphics and 2D games benefit from simpler forward rendering. Vector graphics specifically benefits from GPU-accelerated path rendering (see Pathfinder, Vello) or traditional CPU tessellation with instanced quad rendering. **Research modern GPU vector approaches** like NV_path_rendering or compute shader rasterization before committing to CPU tessellation.

## Novel insights: what the research collectively reveals

Across all researched engines, frameworks, and postmortems, several non-obvious patterns emerge that textbooks rarely emphasize. First, **successful engines separate authoring from runtime more than beginners expect**. Unity's scene files differ fundamentally from runtime entity data. Unreal's Blueprint visual scripting compiles to optimized C++. Godot's node trees serve editors while RenderingServer handles execution. The user-facing API can provide high-level object-oriented convenience while the runtime uses flat optimized data.

Second, **the immediate vs retained debate represents a false dichotomy** resolved through command buffer architectures. Modern engines maintain retained scene data (objects persist), build immediate command streams (optimal render order), execute on explicit APIs (Vulkan/DX12 command buffers). This three-layer approach captures benefits from all paradigms: scene organization, rendering control, and hardware efficiency. Dear ImGui's success for tool UIs proves immediate mode's value for specific domains while engines' unanimous retained approach validates it for complex 3D scenes.

Third, **memory layout matters more than algorithmic complexity** for modern rendering performance. Cache misses cost 50-75x more than L1 hits. A linear search through 1,000 contiguous elements often outperforms a binary search through scattered memory. Mike Acton's insistence that "data is everything" proves correct in production code. The 62% cache line reduction from SoA layout translates directly to frame rate improvements, often exceeding gains from smarter algorithms operating on poor layouts.

Finally, **production engines universally adopted hybrid architectures** because purity fails in practice. Pure immediate mode requires rebuilding everything each frame (wasteful). Pure retained mode locks into inflexible scene graphs (limiting). Pure ECS forces awkward patterns for hierarchical data (transforms, UI). Successful engines combine paradigms based on specific subsystem needs: retained objects, flattened rendering, ECS for performance-critical systems, traditional OOP for tool code, immediate UI for editors.

For your vector graphics engine: **start with simple persistent shape objects in contiguous arrays, build command buffers for rendering, measure your specific performance characteristics, then optimize the identified bottlenecks**. This pragmatic approach, validated across decades of production engines, provides the fastest path to a working, performant renderer that you can systematically improve based on real data rather than theoretical concerns.