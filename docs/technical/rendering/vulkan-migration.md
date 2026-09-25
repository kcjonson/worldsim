# OpenGL to Vulkan: scope and spike

Created: 2026-09-25
Status: Research / scoping (no implementation, no decision yet)
Assumes: the codebase moves to Rust eventually, timing unknown, possibly before this lands. See [Rust port](#rust-port).
Related: [vector-graphics/INDEX.md](../vector-graphics/INDEX.md), [planet-view-rendering.md](../planet-view-rendering.md), [ground-textures.md](../ground-textures.md), [organic-terrain/terrain-polygons-architecture.md](../organic-terrain/terrain-polygons-architecture.md), [library-decisions.md](../library-decisions.md)

## The question

We render with OpenGL 3.3 core through GLFW and GLEW. Should we move to Vulkan, what does
that do to the work we've already done, and how big is it?

The motivations on the table are all four of the usual ones: performance headroom, compute
and modern GPU features, future-proofing, and tooling. Platform targets are Windows, Linux,
and macOS. The guiding constraint is keeping the freedom to tune our own engine around vector
graphics, with third-party tools judged case by case.

## Short answer

The change is medium-sized and well contained. About 5.4k lines of GL-bearing code get
reworked or rewritten, out of roughly 128k lines of non-test C++ (about 4%). The vector
pipeline, tessellator, `Primitives` API surface, assets, and every line of game logic
survive untouched.

The hard part isn't API coverage, it's that our renderer leans on things OpenGL does for us
implicitly: reusing one buffer many times per frame, creating and freeing GPU resources in the
middle of a frame, uploading textures between draws. Vulkan makes all of that our job. The
other cost is that there's no real backend seam today; planet-view, the world chunk renderers,
and the font atlas all call GL directly.

The deciding factor is macOS. Apple froze OpenGL at 4.1 in 2018: no compute shaders, no
`glClipControl`, no DSA, deprecated. With Mac as a ship target and compute as a goal, staying
on GL means staying on a 2010-era feature set on one of three platforms. Vulkan through
MoltenVK is the only path that gives one modern API everywhere without writing a Metal
backend.

Recommendation: raw Vulkan 1.3 behind a thin in-house render layer, done in two phases. Phase
0, in C++ on GL, puts every caller behind a C-ABI-shaped render API with Vulkan-style
semantics. Phase 1 writes the Vulkan backend **in Rust** (via `ash`) behind that same API,
making the renderer the first Rust module of the port, and deletes GL. We write the backend
once, in the language we're heading to, instead of writing it in C++ and porting it later.
Before committing, run the code spike at the end of this doc, which is also a Rust spike.

## What this means for the work so far

| Area | Lines | Fate |
|---|---:|---|
| `libs/renderer/vector/` (sweep tessellator, SVG, Bezier) | 2,954 | Survives as-is. It produces CPU meshes; the API that draws them is irrelevant. |
| `Primitives` API (drawRect, drawText, drawTriangles, pushClip) | 1,061 | Surface survives. Minor `GLuint`/`GLenum` leakage and a dead command queue come out. |
| `CoordinateSystem`, `MetricsCollector`, `ResourceManager` | ~790 | Survive. One `glViewport` call moves out; projection gets a depth-range decision. |
| Asset pipeline, Lua generators, groundcover mesh building, nav, sim, UI layout | most of the codebase | Untouched. |
| `BatchRenderer` + `InstanceData` | 1,331 | Rewritten. Same batching idea, new buffer/pipeline plumbing. |
| `libs/renderer/gl/` RAII wrappers, `shader/` loader | ~1,000 | Replaced by Vulkan equivalents. |
| GL resources (RenderToTexture, TileTextureAtlas, atlas baking upload, GPUTimer) | ~600 | Reworked; CPU-side baking (nanosvgrast) survives. |
| World rendering (`ChunkRenderer`, `BakedChunkRenderer`, `GroundcoverRenderer`, `InstancingUniforms`, `InstancedEntityRenderer`) | ~1,130 | Reworked to go through the render layer instead of raw GL. |
| `libs/planet-view` (`PlanetRenderer`, `PlanetMesh`, `PlanetColorizer`) | 728 | Reworked; biggest single island of raw GL (about 170 calls). |
| `FontRenderer` atlas, `AssetRenderer`, `DebugServer` screenshot, `GlobeView` | ~200 GL lines | Small reworks. Atlas goes RGB8 to RGBA8. |
| 12 GLSL files | 1,029 | Mechanical port to Vulkan GLSL, compiled to SPIR-V. |
| 39 scene files that only call `glClearColor`/`glClear` | 76 calls | One-line replacement each. |

The investment in vector graphics is safe. It was always CPU-side geometry fed to a thin draw
layer, and that's the shape Vulkan wants.

## Where GL lives today

### Context and frame loop

- GL 3.3 core, forward-compatible, one context, main thread only (`libs/engine/application/AppLauncher.cpp:128-145`). No shared contexts and no GL off the main thread; worker threads (asset load, planet colorize, chunk processing) are CPU-only.
- Frame: `glClear` (`Application.cpp:240`), scene render, `Primitives::endFrame`, `glfwSwapBuffers` (`:267`), then a CPU-side 120 FPS cap.
- Resize goes through `framebufferSizeCallback` (`AppLauncher.cpp:94-110`); vsync is toggled at runtime by the debug server (`:447`).
- `asset-cli` and two tests (`FontRenderer.test.cpp`, `AssetRenderer.test.cpp`) create their own hidden GLFW contexts. CI has no GPU, so every rendering test skips; CI verifies no rendering at all today.

### The partial seam

`Renderer::Primitives` over `BatchRenderer` is the closest thing to a backend boundary, and it
leaks: `GLuint` in public headers (`BatchRenderer.h:120,126,183,223`) and `getShaderProgram()`
hands the raw uber-shader program to engine code, which then sets uniforms on it directly.

Code outside `libs/renderer` that does real GL work (21 files):

| Where | GL calls | What it does |
|---|---:|---|
| `libs/planet-view/*` | ~170 | FBO with color + depth, two programs, 10 VAOs, 10 mipmapped textures, full GL state save/restore |
| `libs/engine/world/rendering/ChunkRenderer.cpp` | 53 | Per-chunk RGBA32UI tile-data textures (LRU of 32, ~128 MB), per-chunk uniforms, state save/restore |
| `libs/engine/world/rendering/BakedChunkRenderer.cpp` | 37 | Baked flora VBO/IBO per sub-chunk, per-draw alpha uniform |
| `libs/engine/world/rendering/InstancingUniforms.h` | 29 | Caches uniform locations on the borrowed uber program |
| `libs/engine/application/{AppLauncher,Application}.cpp` | 40 | Context bootstrap, main loop, swap |
| `libs/ui/font/FontRenderer.cpp` | 10 | MSDF glyph atlas texture |
| `libs/engine/world/rendering/GroundcoverRenderer.cpp` | 9 | Groundcover mode uniforms on the borrowed program |
| `libs/engine/assets/AssetRenderer.cpp`, `libs/foundation/debug/DebugServer.cpp` | 6 | `glReadPixels` for thumbnails and the screenshot endpoint |
| `apps/world-sim/scenes/shared/GlobeView.cpp` | 4 | Sub-viewport for the embedded globe |
| dev/test: `ui-sandbox` Clip/Planet/Grass scenes, `asset-cli` | 40 | Viewport demos, uniform pokes, standalone context |

### Draw model

- One 96-byte uber vertex format, CPU-transformed. The whole batch is re-uploaded with `glBufferData(GL_DYNAMIC_DRAW)` on every flush (`BatchRenderer.cpp:564,568`), and the world path forces many flushes per frame as depth-order barriers (`InstancedEntityRenderer.cpp:115-139`).
- Instancing uses `glDrawElementsInstanced` with instance data written by `glBufferSubData` into the same buffer before every draw (`BatchRenderer.cpp:918`). Groundcover draws per chunk x def x variant, overwriting one handle's buffer several times a frame; ~486k tufts at ~1.75 ms.
- 280-392 draws per frame in the game scene; 1-3 in UI scenes. That's not draw-call bound, which matters for the performance argument below.
- Plain `glUniform*` only: no UBOs, SSBOs, persistent mapping, fences, compute, geometry shaders, stencil, scissor, MSAA, or sRGB. The uber shader switches modes with an int uniform (`u_instanced` 0/1/2).
- Depth is only used inside the planet FBO. Blending is plain alpha everywhere.

### Shaders

Twelve files, all `#version 330 core`: `uber`, `tile` (+ `includes/tile.glsl`, 441 lines),
`includes/instancing.glsl`, `planet`, `blit`, and a dead `text` pair. Loaded at runtime with a
custom `#include` preprocessor (`ShaderPreprocessor.cpp`). They use derivatives for SDF/MSDF
anti-aliasing, `usampler2D` with bit unpacking, `gl_VertexID`, `discard`, and a `vec4[64]`
uniform array in `tile.frag`. Nothing exotic.

## The hard parts

These are the places where GL does work implicitly that Vulkan hands back to us. They're
ordered by how badly they bite if missed.

1. **Buffer reuse within a frame.** GL quietly orphans or stalls when we overwrite a buffer the GPU hasn't read yet. Vulkan doesn't; overwrite a buffer that's still in flight and you get corrupted geometry with no error. Fix: a per-frame linear ring allocator for all transient data (batch vertices, indices, instance data, per-draw constants), with each draw getting its own offset.
2. **Resource lifetime.** Chunk textures, baked meshes, groundcover variants, and the planet FBO are created mid-frame, and RAII frees them immediately on LRU eviction or resize. In Vulkan the GPU may still be using them. Fix: a deferred-destruction queue keyed to frame fences.
3. **Uploads between draws.** `glTexSubImage2D` and `glGenerateMipmap` (planet rhombi, chunk textures) run between draws. Vulkan copies can't happen inside a render pass. Fix: an upload phase at the start of the frame, with a staging ring and explicit barriers; mipmaps via a blit chain.
4. **Default vertex attributes.** The instanced and baked paths leave attribute locations 1, 3, 4, 5 disabled and rely on GL's default value (`BatchRenderer.cpp:803`). Vulkan requires every shader input to be bound. Fix: explicit vertex layouts per pipeline.
5. **Coordinate conventions.** Vulkan is Y-down with depth 0..1; GL is Y-up with -1..1. Our conventions are scattered: `glm::ortho(0,w,h,0,-1,1)` (`CoordinateSystem.cpp:47`), `gl_FragCoord.y` flip in `uber.frag:94-96`, flipped font PNG load (`FontRenderer.cpp:273`), flipped text UVs (`BatchRenderer.cpp:465`), flipped readback rows, bottom-left viewport math in `GlobeView.cpp:97`, and a -1..1 perspective in `OrbitCamera.cpp:28`. Fix: one decision (negative viewport height, `GLM_FORCE_DEPTH_ZERO_TO_ONE`) and an audit. Any miss is a subtle visual regression that CI can't catch today.
6. **Uniform model.** Per-draw `glUniform*` becomes push constants (128 bytes guaranteed, which the uber shader's two `mat4`s already fill) or a UBO slice from the ring. The int mode switch becomes separate pipelines.
7. **Readbacks.** The screenshot endpoint reads the back buffer after `glfwSwapBuffers`, which is undefined in GL and only works by luck. In Vulkan it becomes a copy before present, into a host-visible buffer, with a fence wait.
8. **Vsync toggle** becomes swapchain recreation (FIFO vs MAILBOX).
9. **Formats.** The `GL_RGB8` MSDF atlas and the planned RGB16F `channelFrame` texture from the terrain-polygons spec need to become four-channel; three-channel sampled formats are poorly supported in Vulkan.

## Options

| Option | For us | Verdict |
|---|---|---|
| Raw Vulkan 1.3 behind our own thin layer | Full control, one API on all three platforms (MoltenVK on Mac), compute everywhere, best validation tooling | **Recommended** |
| SDL3 GPU | Clean small API over Vulkan/D3D12/Metal; no bindless, replaces GLFW, per-backend shader blobs via shadercross (pulls DXC + SPIRV-Cross) | Good if we wanted the least work, but it's someone else's abstraction sitting between us and the GPU |
| WebGPU native (Dawn, wgpu-native) | Nice ergonomics and a browser story; WGSL, no standard push constants or bindless, heavy (Dawn) or Rust-built (wgpu) | No from C++; wgpu gets a second look under the Rust port (see [ash or wgpu](#ash-or-wgpu)) |
| bgfx / Diligent / LLGL / NVRHI | Mature abstractions with their own shader dialects or build tools; NVRHI has no Metal | No; conflicts with "tune our own engine" |
| sokol_gfx | Tiny and pleasant, but deliberately limited and its Vulkan backend is experimental (Dec 2025) | No |
| Stay on GL, move to 4.6 + AZDO (persistent mapping, multi-draw indirect, SSBOs) | Zero migration and gets most of the CPU win on Windows/Linux | Fails the Mac requirement: Mac stays at 4.1 with no compute |

An in-house layer designed around modern concepts (bindless descriptors, push constants,
buffer device address, per-frame arenas) is the direction Jasper St. Pierre and Sebastian
Aaltonen both argue for, and it's the same "borrow concepts, not code" approach we took with
the tessellator. It also keeps a Metal or D3D12 backend possible later without a rewrite of
callers.

### Be honest about performance

At 280-392 draws per frame with heavy batching and instancing, we are not draw-call bound, and
nothing in the prior art gives a clean CPU before/after number for a batched 2D workload. Don't
expect a big frame-time drop from the port alone. The performance case is headroom: explicit
async transfer queues for chunk and texture streaming, compute for worldgen, SDF baking, and
the planned Living Environment effects, multithreaded command recording if recording ever
shows up in a profile, and predictable offline-compiled shaders instead of per-vendor GL
compilers (X-Plane's stated reason for moving).

## Modern Vulkan baseline

Vulkan 1.0 is where the "900 lines for a triangle" reputation comes from. Vulkan 1.3 made
most of that boilerplate optional. Sascha Willems' 2026 sample does textured, lit, multi-object
rendering in 688 lines using the modern feature set, against 1,621 lines for the classic
tutorial at similar scope.

What we'd require (all core in 1.3, all listed as supported by MoltenVK):

- **Dynamic rendering**: no `VkRenderPass`/`VkFramebuffer` objects; begin rendering into images directly.
- **Synchronization2**: saner barrier API.
- **Descriptor indexing**: one global bindless texture array, indexed by an int in push constants. This maps well onto our atlases and chunk textures.
- **Buffer device address**: pass GPU pointers to per-frame data instead of juggling descriptor sets.
- **Extended dynamic state**: fewer pipeline permutations.
- Push descriptors (core in 1.4) are nice-to-have.

What we'd skip: `VK_EXT_shader_object` (57% of Windows devices, missing from MoltenVK) and
`VK_EXT_descriptor_buffer` (DXVK disables it on older NVIDIA and AMD for performance problems,
and Khronos's new descriptor heap extension is meant to replace it). Revisit descriptor heaps
once they're KHR and in MoltenVK.

Mojang picked Vulkan 1.2 + dynamic rendering + push descriptors as Minecraft Java's minimum for
reach. 1.3 cuts off some older hardware; check vulkan.gpuinfo.org against our minimum spec
before locking it (open question 1).

**macOS**: MoltenVK translates Vulkan to Metal and was "nearly conformant" 1.4 as of January
2026, on Intel and Apple Silicon. KosmicKrisp (LunarG, in Mesa) is conformant 1.3 but Apple
Silicon + Metal 4 only and still self-described alpha. Start on MoltenVK. We ship its dylib
with the Mac build and enable the portability enumeration extension at instance creation.

## Shaders and SPIR-V

OpenGL accepts GLSL source text and each vendor's driver compiles it at runtime, which is why
the same shader can behave differently on NVIDIA and Intel. Vulkan drivers don't take source
text. They take SPIR-V, a binary intermediate format (think of it as bytecode for shaders).
We compile GLSL to SPIR-V at build time with an offline compiler, and the driver only does
the last step to machine code.

For us that means:

- Our GLSL stays GLSL, with Vulkan dialect changes: loose uniforms move into `layout(push_constant)` or `layout(set, binding)` blocks, `gl_VertexID` becomes `gl_VertexIndex`, every in/out gets an explicit location.
- The runtime `#include` preprocessor goes away; the offline compiler handles includes.
- Shader errors show up at build time instead of at launch.
- Hot reload, if we want it later, means invoking the compiler as a library at runtime in dev builds.

Writing our own SPIR-V compiler isn't a reasonable project. The compiler is a build tool that
never ships inside the game, the same category as CMake or the MSVC compiler.

## Dependencies, case by case

| Need | Common choice | Recommendation |
|---|---|---|
| Vulkan headers + loader | Khronos (Apache-2.0) | Required; it's the API. |
| Function-pointer loading | volk | Roll our own. A table of `vkGetDeviceProcAddr` pointers, a few hundred lines. |
| Instance / device / swapchain setup | vk-bootstrap | Roll our own. Boring query-and-pick code, ~500-800 lines. Read vk-bootstrap for its list of platform quirks (MoltenVK portability, present modes). |
| GPU memory allocation | VMA | Roll our own. Our pattern is simple: a few big static buffers, atlases, a 128 MB chunk-texture LRU, and per-frame rings. A block sub-allocator per memory type plus a linear per-frame arena covers it. VMA's own FAQ says writing your own is reasonable for simple apps. We give up defragmentation and budget tracking; revisit if fragmentation shows up. |
| GLSL to SPIR-V | glslang (Khronos reference compiler) | Adopt as a build-time tool. Slang is the alternative if we ever want a better shading language. |
| Validation layers | Khronos, via the Vulkan SDK | Adopt for dev builds only. Never shipped. |
| Mac translation | MoltenVK | Adopt; ships in the Mac build. |
| Window + input | GLFW (already have) | Keep. GLFW creates Vulkan surfaces (`glfwCreateWindowSurface`). |
| GL extension loading | GLEW (already have) | Delete. |

## Rust port

We're assuming the codebase moves to Rust, timing unknown, possibly before any of this lands.
That changes where the Vulkan backend gets written, not whether we move to Vulkan. Nothing
above this section changes: the shaders, the hard parts, the Vulkan baseline, and the
dependency calls all carry over, because Vulkan and SPIR-V don't care what language the host
program is in.

### The renderer goes first

- **It's being rewritten anyway.** Writing a C++ Vulkan backend and then porting it means writing the hardest, most sync-sensitive code in the project twice.
- **It sits low in the dependency graph.** It depends on the window, logging, and metrics; everything else calls into it. Low modules port cleanly behind a C boundary while the code above them stays C++ (the strangler pattern).
- **Phase 0 already builds the boundary.** The seam we need for Vulkan is the same seam we need for a language boundary, as long as we shape it for C from the start.

This assumes an incremental port. A big-bang rewrite would throw away Phase 0's C++ caller
migration; the design would survive but the code wouldn't (open question 4).

### The boundary

C++ calls the Rust renderer through plain `extern "C"` functions. We don't need a C++/Rust
bridge generator like `cxx`; the API is small and a C ABI is the most stable thing both
languages speak.

- **Opaque handles** (integer ids) for buffers, textures, pipelines, and render targets. No pointers to Rust objects cross the line.
- **Plain data** (`#[repr(C)]` structs) for vertex formats, draw descriptions, and per-draw constants. The 96-byte uber vertex is already plain data.
- **Copy on submit.** C++ passes pointer + length; Rust copies into the frame ring before returning. No borrowed memory outlives a call, which is exactly the ring-allocator semantics Vulkan needs anyway.
- **Coarse calls.** In Phase 1 the line sits at completed batches (vertex/index arrays, draw ranges, constants), not per rectangle. Vertex generation from `Primitives` is backend-agnostic CPU work; it stays C++ for now and ports later with the rest of `Primitives`. That keeps crossings to a few hundred per frame, where a C-ABI call costs about the same as a non-inlined C++ call.
- **Errors** come back as return codes. Panics never cross the boundary; every export catches them.
- **Logging and metrics** go through callbacks the C++ side registers at init, so Rust logs still reach the HTTP log server and the dev tools.
- **Window.** C++ keeps GLFW for now and passes the native window handle (HWND, X11/Wayland, or the Cocoa view); Rust creates the Vulkan surface from it. Windowing and input move to Rust in a later step.

For Phase 0 this adds one rule: the public render API contains nothing C can't express. No
templates, STL containers, `std::function`, exceptions, or classes. The current `Primitives`
free-function style is already close.

### ash or wgpu

In C++, wgpu is a heavy Rust-built dependency. In Rust it's the standard graphics layer
(Bevy and Veloren use it), so it deserves a second look.

| | ash (raw Vulkan) | wgpu |
|---|---|---|
| Control | Everything: memory, sync, descriptors, pipelines | wgpu decides sync barriers and resource lifetimes |
| Safety | All `unsafe`; GPU data races are still ours | Safe API; tracks lifetimes and inserts barriers, at some CPU cost |
| Mac | MoltenVK | Native Metal, no translation layer |
| Other targets | Vulkan only | Vulkan, Metal, DX12, WebGPU in browsers |
| Bindless, push constants | Core Vulkan | Native-only feature flags, outside the WebGPU standard |
| Shaders | SPIR-V from our GLSL | WGSL first; SPIR-V accepted via its translator (naga) |

Recommendation stays **ash**. The goal is freedom to tune our own engine around vector
graphics, and wgpu's value (automatic sync, portability layers) is exactly the control we'd
be handing over. wgpu is the credible fallback if the Vulkan backend's sync work turns into a
swamp; the spike includes an optional wgpu comparison to settle it with numbers.

With ash, `unsafe` stays confined to the backend module behind a small safe internal API, so
the rest of the renderer crate is ordinary safe Rust.

### Rust-side dependencies, case by case

| Need | Rust choice | Recommendation |
|---|---|---|
| Vulkan bindings + loading | ash | Adopt. Thin bindings generated from the Khronos registry, 1:1 with the C API, including function loading (replaces the volk row above). Hand-writing them makes no sense. |
| Device / swapchain setup | | Roll our own, same as the C++ plan. |
| GPU memory allocation | gpu-allocator | Roll our own, same reasoning as VMA above; read gpu-allocator for concepts. |
| GLSL to SPIR-V | glslang, or naga (pure Rust) | Keep glslang as a build tool so the shader toolchain doesn't change with the language. naga's GLSL frontend is less complete. |
| Building Cargo crates from CMake | Corrosion | Adopt as a build tool; it's the standard bridge and fits the existing Ninja presets. |
| C header for the boundary | cbindgen | Hand-write it while the API is small; adopt cbindgen if it grows. |
| Native window handles | raw-window-handle | Adopt. A tiny trait crate that every Rust windowing and graphics library speaks. |

### Build and CI

- A pinned Rust toolchain (`rust-toolchain.toml`) on dev machines and CI.
- Corrosion inside the existing CMake presets; Debug and RelWithDebInfo map to Cargo profiles.
- Two package managers: vcpkg for C++, Cargo for Rust.
- CI's sccache already supports `rustc`.
- Renderer tests run as `cargo test` under lavapipe on Linux CI, so the first Rust code also brings the first real rendering tests.

## Size of the change

No time estimates here; this is sized in code.

**Phase 0, the seam, in C++ on GL (lands on main as ordinary refactors).**
Touches about 65 files, but 39 of those are one-line `glClear` replacements. The real work:

- A C-ABI-shaped render API that owns every GPU object by handle (buffers, textures, pipelines, render targets). No `GLuint` outside `libs/renderer`.
- Port planet-view, the chunk renderers, font atlas, and readbacks onto it. Stop sharing the uber program; per-draw constants become a plain struct.
- Vulkan semantics at the API: transient data is copied on submit, destruction is deferred to a frame boundary, uploads happen in a phase at frame start, every pipeline declares its vertex layout.
- Centralize coordinate conventions in `CoordinateSystem` so the Y/depth decision lives in one place.
- Screenshot readback moves before swap (fixes the undefined-behavior read today).

Because the GL backend gets deleted in Phase 1, its internals only change as far as needed to
honor the API. Don't polish GL code (UBO conversions, persistent mapping) that won't survive.

**Phase 1, Vulkan backend in Rust.** A new renderer crate built through Corrosion, behind the
Phase 0 API, replacing ~2.9k GL-specific lines in `libs/renderer`. Estimated 3-5k lines
(instance/device/swapchain, frame sync, allocator, upload, pipelines, bindless descriptors),
anchored on sokol's Vulkan backend (~3k lines) and Elias Daler's engine (~6.7k graphics lines,
more features than we need); Rust with ash is about as dense as C++. Plus the 1,029-line
shader port, glslang build integration, and the Rust toolchain in the build and CI. If Phase 0
is done well, nothing outside `libs/renderer` changes. GL and GLEW are deleted in the same PR.

**Phase 2, exploit it and keep porting.** Compute passes, async transfer queue for chunk
streaming, bindless atlases, lavapipe rendering tests in CI. On the Rust side, `Primitives`
and batching move next, then windowing and input, then upward from there.

### The one-path rule

CLAUDE.md forbids parallel old/new paths. Minecraft kept a GL/Vulkan toggle during their
transition; we shouldn't. Phase 0 is pure refactoring, so it lands on main with one path at
every step. For Phase 1, the C++ GL backend and the Rust Vulkan backend never coexist on main:
build on a branch, merge at parity, delete GL in the same PR. A temporary dual backend on main
is the alternative if the branch gets too long-lived (open question 2).

## Spike

### Paper spike (done)

The audit above is the first half. Findings that change the plan:

- The GL footprint is smaller than the file count suggests: about 548 raw GL calls, and the renderer uses no UBOs, SSBOs, compute, stencil, MSAA, or geometry shaders. There's very little GL feature surface to re-create.
- The risk is behavioral (implicit sync, mid-frame lifetime), not API coverage. Phase 0 flushes out the caller side of that on GL; the backend side is new code either way.
- This dev machine: RTX 3090, driver 610.88, Vulkan instance and device API 1.4.341. The runtime loader is installed but the **Vulkan SDK is not** (`VULKAN_SDK` unset), so there are no validation layers and no glslang yet. Installed implicit layers include Steam, EOS, and RTSS overlays; expect noise in validation output.
- CI (ubuntu-latest, windows-latest) has no GPU. Every rendering test skips today. A Vulkan backend can run headless in CI through lavapipe (Mesa's CPU Vulkan), which GL can't do in our current setup.

### Code spike (proposed)

A throwaway `spike/vulkan` branch, never merged. It's a Rust spike and a Vulkan spike at once:
a small Rust crate using ash, built through Corrosion, called from ui-sandbox over a C ABI.

1. **Build integration.** Corrosion in the Ninja presets, ui-sandbox links the Rust static library, Rust logs route through the C++ logger to the HTTP log server.
2. **Bring-up.** Native window handle from GLFW, instance/device/swapchain with our own setup code, validation layers on, clear to a color. Resize and vsync toggle via swapchain recreation.
3. **UI parity.** Port `uber.vert/frag` to Vulkan GLSL, compile with glslang, draw a ui-sandbox scene (rects, MSDF text, clip rects) from C++-generated batches through a per-frame ring. Screenshot via the debug server using copy-before-present, then pixel-diff against the GL screenshot of the same scene.
4. **Instancing stress.** The groundcover scene (~486k tufts) with instance data in the ring. Compare GPU time against GL's ~1.75 ms, compare CPU frame time, and measure the cost of the boundary crossings.
5. **Mac smoke test.** Steps 1-3 through MoltenVK on a Mac.
6. **Headless CI.** Step 3 as a `cargo test` under lavapipe on Linux, producing a screenshot artifact.
7. **Optional: wgpu comparison.** Step 3 again in wgpu, to compare line count, CPU cost, and how much control we'd give up.

Exit criteria: validation-clean frames, pixel parity on the UI scene, a measured perf delta
and boundary cost, the build working in both presets and CI, Mac viability confirmed or
ruled out, and a line count for the core layer to check the 3-5k estimate. Prerequisites:
install the Vulkan SDK from LunarG and a Rust toolchain on the dev machine.

## Prior art

| Source | What they did | Outcome | URL |
|---|---|---|---|
| Mojang, Minecraft Java | GL to Vulkan, announced Feb 2026, experimental in 26.2 snapshots | Min spec Vulkan 1.2 + dynamic rendering + push descriptors; MoltenVK on Mac; GL toggle kept during transition | https://www.gamingonlinux.com/2026/02/minecraft-java-is-switching-from-opengl-to-vulkan-for-the-vibrant-visuals-update/ |
| Laminar Research, X-Plane 11.50 | GL to Vulkan (Win/Linux) and Metal (Mac) | Big GPU-side gains, less single-core time; launch stutters on older NVIDIA; later ran legacy GL plugins on Zink | https://developer.x-plane.com/2020/04/x-plane-11-50-public-beta-1-vulkan-and-metal-are-here/ |
| Elias Daler, EDBR (solo) | Learned Vulkan from GL background; dynamic rendering, BDA, bindless, sync2 | Synchronization was the hard part; 10k sprites in 315 us with one draw; prefers Vulkan's lack of global state and its validation | https://edw.is/learning-vulkan/ |
| "Abandoning Vulkan" (indie, GameDev.net, 2022) | Kept GL and Vulkan side by side | Vulkan backend never stabilized (GPU freezes, corruption); shipped GL | https://gamedev.net/blogs/entry/2275791-abandoning-vulkan/ |
| Godot 4 | GLES3 to Vulkan RenderingDevice | Had to add a GL "Compatibility" renderer for low-end/web; 2D regressions on old hardware | https://docs.godotengine.org/en/stable/tutorials/rendering/renderers.html |
| Veloren | gfx (GL) to wgpu | Took the abstraction route; gained Vulkan/Metal/DX12 and GPU timestamps | https://veloren.net/devblog-125/ |
| floooh, sokol_gfx | Added a Vulkan backend | ~3k lines, about the size of its GL backend; still experimental | https://floooh.github.io/2025/12/01/sokol-vulkan-backend-1.html |
| Sascha Willems, "How to Vulkan in 2026" | Single-file modern Vulkan sample | 688 lines for textured lit multi-object rendering | https://www.howtovulkan.com/ |
| Jasper St. Pierre | Renderer design for modern APIs | Concept reference for our layer | https://blog.mecheye.net/2023/09/how-to-write-a-renderer-for-modern-apis/ |
| Sebastian Aaltonen, "No Graphics API" | Minimal bindless, pointer-based API design | Concept reference for our layer | https://www.sebastianaaltonen.com/blog/no-graphics-api |
| zeux, "Writing an efficient Vulkan renderer" | Guidance on submits, command buffers, threading | Stay single-threaded under ~100 draws per pass until profiles say otherwise | https://zeux.io/2020/02/27/writing-an-efficient-vulkan-renderer/ |
| Khronos, Vulkan 1.4 / Roadmap 2026 | Core feature set, descriptor heap announcement | Basis for the baseline section | https://www.khronos.org/blog/vulkan-introduces-roadmap-2026-and-new-descriptor-heap-extension |
| LunarG, state of Vulkan on Apple (Jan 2026) | MoltenVK and KosmicKrisp status | MoltenVK nearly conformant 1.4 | https://www.lunarg.com/the-state-of-vulkan-on-apple-jan-2026/ |
| Corrosion | CMake integration for Cargo crates | Standard way to mix Rust into a CMake build | https://github.com/corrosion-rs/corrosion |
| ash | Rust Vulkan bindings | Thin, generated, 1:1 with the C API | https://github.com/ash-rs/ash |

The common thread: synchronization and driver variance bite first, and the CPU win only
shows up where the GL renderer was draw-call bound.

## Open questions

1. **Minimum Vulkan version.** 1.3 core (recommended, simplest code) or 1.2 + extensions for older hardware reach, as Mojang did? Depends on our minimum spec, which we haven't set.
2. **Phase 1 delivery.** Branch until parity and swap in one PR (recommended), or a temporary dual backend on main with a named deletion milestone?
3. **Mac path.** MoltenVK now (recommended) with a native Metal backend only if MoltenVK limits bite?
4. **Shape of the Rust port.** Incremental behind C boundaries, renderer first (assumed here), or a rewrite? A rewrite skips Phase 0's C++ caller migration and designs the renderer's Rust API directly.
5. **ash or wgpu.** ash recommended; the spike's optional wgpu step can settle it with numbers.
6. **Start Phase 0 now?** It's useful whatever the answers above: it fixes a real undefined-behavior read, removes shared-program uniform pokes, and creates the boundary both Vulkan and Rust need.

## Incidental findings from the audit

Not part of the migration, but found along the way:

- `apps/ui-sandbox/scenes/GrassScene.cpp:195-213` sets `u_grassMode` and `u_grassOpenness`, which no longer exist in any shader. The calls silently do nothing.
- `text.vert`/`text.frag` are never loaded.
- `Primitives.cpp:30-84,150-160` holds a dead `DrawCommand`/`BatchKey` queue.
- `PushScissor` is a no-op (`Primitives.cpp:640`).
- `DebugServer` reads the back buffer after swap (undefined in GL).
- `libs/foundation` links `OpenGL::GL` only for the screenshot readback (`libs/foundation/CMakeLists.txt:36`).
