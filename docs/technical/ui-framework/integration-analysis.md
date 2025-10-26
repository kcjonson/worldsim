# UI Library Integration Analysis

Created: 2025-10-26
Status: Decision Support

## Critical Questions for Web Developer

This document answers the practical questions about how each UI library integrates with our custom OpenGL renderer.

## Key Concepts for Web Developers

### Immediate Mode vs Retained Mode

**Retained Mode** (like React):
```jsx
// You describe the UI once, framework keeps it in memory
function Menu() {
  return (
    <div>
      <button onClick={handleClick}>New Game</button>
    </div>
  );
}
// React maintains this tree, updates on state changes
```

**Immediate Mode** (no web equivalent):
```cpp
// You regenerate the entire UI EVERY FRAME (60 times/second)
void RenderUI() {
    ImGui::Begin("Menu");
    if (ImGui::Button("New Game")) {
        HandleClick();
    }
    ImGui::End();
}
// Called in game loop, no persistent objects!
```

**Why it matters:**
- Your `.Args{}` API assumes retained mode (you create objects once, they persist)
- Immediate mode libraries need facade layer to match your API
- Retained mode libraries are more natural for your use case

### Single-Header Library

"Single-header" means the entire library is one `.h` file:
```cpp
#include "nuklear.h"  // That's it! Entire library included.
```

**In npm terms:** Zero dependencies, no build configuration, just copy the file.

**Trade-off:** Convenience vs large compilation time (including 30k lines every time).

## Rendering Architecture: Who Draws What?

This is CRITICAL. There are three models:

### Model 1: Library Provides Its Own Renderer

**How it works:**
- Library has internal OpenGL drawing code
- You give it an OpenGL context
- Library draws directly to screen

**Examples:** RmlUI (partial), NanoGUI (uses NanoVG)

**Problem for you:**
- Library's renderer may conflict with your custom renderer
- You're batching vector graphics - library may break batches
- Hard to control render order (tiles → entities → UI)

### Model 2: Library Expects You to Provide Renderer Backend

**How it works:**
- Library generates geometry (vertices, triangles)
- You write a "backend" that feeds this to your OpenGL renderer
- Library doesn't touch OpenGL directly

**Examples:** Dear ImGui, Nuklear

**Good for you:**
- You control all OpenGL state
- Can integrate with your batching system
- Full control over render passes

**Work required:**
- Write backend adapter (~200-500 lines)
- Connect library's vertex data to your renderer

### Model 3: Custom Implementation (You Do Everything)

**How it works:**
- You generate geometry
- You render it
- No library at all

**Good for you:**
- Total control
- Perfect integration

**Work required:**
- Everything (~4-6 weeks)

## Dependency Analysis

### Dear ImGui

**Direct Dependencies:**
```
NONE! (core library has zero dependencies)
```

**Optional Dependencies** (backends - you choose):
```
- OpenGL backend: No deps, you provide context
- GLFW backend: Uses GLFW (already in your vcpkg.json)
- OR write custom backend: 0 deps
```

**OpenGL Integration:**
- You provide renderer backend
- ImGui generates vertex buffers
- Your backend uploads to GPU using your renderer

**Conflicts with Custom Renderer?**
- NO - you control the backend
- Can integrate with your batching system
- Example backend: ~300 lines

**Build Complexity:**
- Add to vcpkg.json: `"imgui"`
- Link in CMake: `find_package(imgui CONFIG REQUIRED)`
- That's it!

### Nuklear

**Direct Dependencies:**
```
NONE! Single header file.
```

**Optional Dependencies:**
```
- Font rendering: You provide (or use stb_truetype - already have stb)
- Renderer: You write backend (like ImGui)
```

**OpenGL Integration:**
- You write backend (~400 lines)
- Nuklear generates vertex/index buffers
- Your backend uploads to GPU

**Conflicts with Custom Renderer?**
- NO - you control everything
- More manual than ImGui (C API, more boilerplate)

**Build Complexity:**
- Copy `nuklear.h` to project
- `#include "nuklear.h"`
- That's it!

### RmlUI

**Direct Dependencies:**
```
- FreeType (font rendering) ← NEW dependency
- Lua (optional, for scripting) ← You probably don't want this
```

**OpenGL Integration:**
- Provides OpenGL 3 renderer (built-in)
- OR you write custom renderer backend

**Conflicts with Custom Renderer?**
- MAYBE - built-in renderer may conflict with your batching
- Uses its own shaders, its own state management
- Render pass integration tricky

**Build Complexity:**
- Add to vcpkg: `"rmlui"`
- Link FreeType transitively
- Configure renderer backend
- Medium complexity

### NanoGUI

**Direct Dependencies:**
```
- NanoVG (vector graphics library) ← NEW dependency
- Eigen (math library) OR glm ← You have glm, but Eigen different
- OpenGL (you have this)
- GLFW (you have this)
```

**OpenGL Integration:**
- Uses NanoVG for all drawing
- NanoVG has its own stencil-based renderer
- You integrate at high level (create windows, widgets)

**Conflicts with Custom Renderer?**
- YES - NanoVG manages OpenGL state
- Draws using stencil buffer (may conflict with your vector graphics)
- Hard to interleave with your rendering

**Build Complexity:**
- Add to vcpkg: `"nanogui"`
- Pulls in NanoVG, potentially Eigen
- Medium complexity

### MyGUI

**Direct Dependencies:**
```
- OpenGL (you have)
- FreeType (fonts) ← NEW
- OIS (input system) OR you write platform ← Complex
```

**OpenGL Integration:**
- Expects you to provide "Platform" and "RenderManager"
- You write OpenGL backend (~500+ lines)
- XML parsing for layouts

**Conflicts with Custom Renderer?**
- NO if you write backend correctly
- But backend is complex

**Build Complexity:**
- Add to vcpkg: `"mygui"`
- Configure platform layer
- Write render backend
- HIGH complexity

### Custom Implementation

**Direct Dependencies:**
```
- msdfgen (for SDF fonts) ← NEW, but small
- OR stb_truetype (you have stb already)
```

**OpenGL Integration:**
- You control everything
- Perfect integration with your renderer
- Your batching system

**Conflicts?**
- NONE - it's your code

**Build Complexity:**
- Write everything yourself
- 4-6 weeks of code

## Transitive Dependency Summary

**Zero New Dependencies:**
- Dear ImGui ✅
- Nuklear ✅
- Custom ✅

**Minimal New Dependencies:**
- Custom + SDF fonts: `msdfgen` only

**Medium New Dependencies:**
- RmlUI: FreeType
- NanoGUI: NanoVG, possibly Eigen

**Heavy New Dependencies:**
- MyGUI: FreeType, OIS, XML parser

## Rendering Conflict Analysis

**Safe (No Conflicts):**
- Dear ImGui - you control backend ✅
- Nuklear - you control backend ✅
- Custom - your code ✅

**Potential Conflicts:**
- NanoGUI - NanoVG manages state, uses stencil buffer
- RmlUI - built-in renderer may conflict

**Complex Integration:**
- MyGUI - need custom render backend

## Z-Index / Layering

**Question:** If UI has z-index, does it conflict with our vector graphics batching?

**Answer:** Depends on model:

### Model 1 (ImGui, Nuklear, Custom):
**You control render passes:**
```cpp
void RenderFrame() {
    // Pass 1: Tiles (z: 0-100)
    RenderTiles();

    // Pass 2: Vector entities (z: 100-200)
    RenderVectorEntities();

    // Pass 3: UI (z: 200+)
    RenderUI();  // ← ImGui/Nuklear backend here
}
```

No conflict - different render passes. UI always on top.

### Model 2 (NanoGUI, RmlUI):
**Library manages rendering:**
```cpp
void RenderFrame() {
    RenderTiles();
    RenderEntities();

    // Library draws here - may change GL state!
    nanoGUI->draw();  // ← Modifies stencil, shaders, state

    // Your renderer state may be broken now!
}
```

Potential conflict - need careful state save/restore.

## Decision Matrix

| Library | New Deps | Render Conflict Risk | Backend Work | Matches `.Args{}` | Retained Mode |
|---------|----------|---------------------|--------------|-------------------|---------------|
| **Dear ImGui** | None | None (you control) | ~300 lines | No (need facade) | No |
| **Nuklear** | None | None (you control) | ~400 lines | No (need facade) | No |
| **RmlUI** | FreeType | Medium (built-in renderer) | Low (has backend) | No (uses HTML) | Yes |
| **NanoGUI** | NanoVG | High (NanoVG state) | Low (integrated) | Somewhat | Yes |
| **MyGUI** | FreeType, OIS | Low (custom backend) | High (~500+ lines) | No (uses XML) | Yes |
| **Custom** | msdfgen (optional) | None | High (4-6 weeks) | Perfect | Yes |

## Recommended Questions to Ask Yourself

### 1. How much new complexity can you tolerate?

**Low tolerance:**
- → ImGui (zero deps, you control backend)
- → Nuklear (zero deps, more manual)

**Medium tolerance:**
- → Custom (msdfgen only, full control)

**High tolerance:**
- → RmlUI (FreeType, HTML/CSS learning curve)

### 2. How important is zero rendering conflicts?

**Critical:**
- → ImGui (you write backend)
- → Custom (your code)

**Can manage:**
- → Nuklear (you write backend)
- → RmlUI (may need state save/restore)

**Risk tolerance:**
- → NanoGUI (NanoVG may conflict with your vector rendering)

### 3. Do you want to learn OpenGL deeply?

**Yes, deep learning:**
- → Custom (you implement everything)
- → ImGui/Nuklear with custom backend (you learn backend integration)

**No, focus on game:**
- → RmlUI (has backend, HTML/CSS familiar)
- → NanoGUI (integrated, less OpenGL exposure)

### 4. How close to your `.Args{}` API?

**Perfect match:**
- → Custom (you design it)

**Need facade layer:**
- → ImGui (most popular, lots of examples)
- → Nuklear (manual, C-style)

**Different paradigm:**
- → RmlUI (HTML/CSS, not `.Args{}`)
- → MyGUI (XML, not `.Args{}`)

## Rendering Backend Example (Dear ImGui)

To clarify what "backend" means, here's what you'd write for ImGui:

```cpp
// imgui_renderer_backend.cpp (~300 lines total)

// Called once at startup
void ImGui_ImplRenderer_Init() {
    // Create shader for ImGui
    m_shader = renderer->createShader("imgui.vert", "imgui.frag");

    // Create vertex buffer for ImGui data
    m_vbo = renderer->createDynamicVBO();

    // Create font texture atlas
    unsigned char* pixels;
    int width, height;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    m_fontTexture = renderer->createTexture(pixels, width, height);
}

// Called every frame to draw ImGui
void ImGui_ImplRenderer_RenderDrawData(ImDrawData* draw_data) {
    // For each ImGui draw command:
    for (int n = 0; n < draw_data->CmdListsCount; n++) {
        const ImDrawList* cmd_list = draw_data->CmdLists[n];

        // Upload vertex/index data to your VBO
        renderer->uploadVertices(cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.Size);
        renderer->uploadIndices(cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size);

        // Render each draw command
        for (const ImDrawCmd& cmd : cmd_list->CmdBuffer) {
            // Set scissor rect (clipping)
            renderer->setScissor(cmd.ClipRect);

            // Bind texture
            renderer->bindTexture(cmd.TextureId);

            // Draw
            renderer->drawIndexed(cmd.ElemCount, cmd.IdxOffset, cmd.VtxOffset);
        }
    }
}
```

**That's it!** ImGui generates geometry, you feed it to your renderer. Total control.

## My Recommendation (Updated)

Based on:
- You want ONE library for all C++ UI
- You want minimal new dependencies
- You want to avoid rendering conflicts
- You're building a custom renderer anyway

**Top Choice: Dear ImGui**

**Why:**
1. **Zero dependencies** - no FreeType, no NanoVG, nothing
2. **You control backend** - perfect integration with your renderer
3. **No render conflicts** - you decide when/how to draw
4. **Battle-tested** - used in thousands of projects
5. **Facade is doable** - wrap in your `.Args{}` API

**Facade approach:**
```cpp
// Your declarative API
auto button = std::make_shared<UI::Button>(UI::Button::Args{
    .label = "New Game",
    .position = glm::vec2(300, 200),
    .size = glm::vec2(200, 60)
});

// Internally, in render loop:
void UI::Button::render() {
    ImGui::SetCursorPos(ImVec2(m_args.position.x, m_args.position.y));
    ImGui::PushID(this);  // Unique ID
    if (ImGui::Button(m_args.label.c_str(), ImVec2(m_args.size.x, m_args.size.y))) {
        if (m_args.onClick) m_args.onClick();
    }
    ImGui::PopID();
}
```

**Trade-offs you accept:**
- Immediate mode under the hood (you abstract it away)
- Need facade layer (~500 lines for basic components)
- Styling limited (but good enough for game UI)

**Alternative if you hate facades: Custom Implementation**
- 4-6 weeks work
- Perfect API from day one
- Deep learning experience
- msdfgen only dependency

## Next Steps

1. **Decide:** ImGui vs Custom?
2. If ImGui: I'll write `imgui-integration.md` (backend + facade design)
3. If Custom: I'll write `architecture.md` (scene graph, event system)

Either way, scrolling containers work the same (OpenGL scissor test).

## References

**Renderer Backends Examples:**
- ImGui OpenGL3 backend: https://github.com/ocornut/imgui/blob/master/backends/imgui_impl_opengl3.cpp
- Nuklear OpenGL3 backend: https://github.com/Immediate-Mode-UI/Nuklear/blob/master/demo/gl/main.c

**Dependency Documentation:**
- vcpkg imgui: https://github.com/microsoft/vcpkg/tree/master/ports/imgui
- vcpkg rmlui: https://github.com/microsoft/vcpkg/tree/master/ports/rmlui

## Revision History

- 2025-10-26: Created integration analysis for decision support
