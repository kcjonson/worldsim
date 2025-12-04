# UI Framework Architecture - Complete Design

**Date:** 2025-10-26

**Critical Gap Addressed:**

Comprehensive planning exists for vector graphics rendering and procedural tile systems, but **no technical design for UI framework** - a foundational requirement for ui-sandbox and complex game UI (inventory grids, skill trees, resource management).

**Production game context established:** This is not a learning project - need production-quality UI for complex management game with thousands of dynamic UI elements.

**User Context:**
- Web/React developer background (familiar with declarative UI)
- Previous C++ project used modern designated initializer syntax (`.position = value`)
- Hit scrolling container issues (performance/clipping in OpenGL)
- Wants crisp text rendering matching vector aesthetic

**Documentation Created** (`/docs/technical/ui-framework/`):

**Seven comprehensive design documents:**

1. **INDEX.md** - Navigation hub for UI framework documentation
   - Desired API style examples (CSS-like `.Args{}` syntax)
   - Integration with observability system
   - Common UI patterns (menus, forms, HUD)

2. **ui-architecture-fundamentals.md** - Foundational concepts for production games
   - Immediate vs retained mode explained in React/web terms
   - Scene graphs = React's virtual DOM
   - What production games actually use (Unity, Unreal, indies)
   - RmlUI as HTML/CSS for C++ games
   - Clear recommendation: Retained mode for complex game UI

3. **library-options.md** - Initial comparative analysis (6 libraries)
   - Later superseded by fundamentals doc with production focus

4. **integration-analysis.md** - Technical integration deep dive
   - Immediate vs retained mode in React terms
   - OpenGL rendering models (who draws what)
   - Transitive dependencies for each library
   - Rendering conflict analysis (NanoVG issues)
   - Backend implementation examples
   - Dependency table (RmlUI: FreeType, NanoGUI: NanoVG+Eigen)

5. **library-isolation-strategy.md** - Risk mitigation and architecture isolation
   - Interface pattern to isolate RmlUI from game code
   - What CAN be isolated (game logic, event handlers)
   - What CANNOT be isolated (.rml files, data binding syntax)
   - Hidden constraints (threading, memory management, font rendering)
   - Conflicts with project goals analysis
   - Migration cost assessment (1-2 weeks simple, 4-8 weeks complex)
   - Prototype-first recommendation

6. **rendering-boundaries.md** - Critical architectural boundaries
   - **The "two different APIs" problem identified**
   - RmlUI for screen-space panels only (menus, inventory, skill trees)
   - Custom rendering for game world (tiles, entities)
   - Custom rendering for world-space UI (health bars, tooltips)
   - **Solution: Unified Primitive Rendering API**
   - Complete render loop architecture
   - Real-world examples (RimWorld, Factorio, Unity, Unreal)

7. **primitive-rendering-api.md** - Foundation layer design
   - Unified API for all 2D drawing (DrawRect, DrawText, DrawTexture)
   - Used by RmlUI backend, game rendering, world-space UI
   - Immediate mode API, retained mode implementation (batching)
   - Scissor/clipping stack for scrollable containers
   - Transform stack for world-space rendering
   - Integration with existing renderer and vector graphics
   - Performance targets (<1ms per frame for all primitives)
   - Complete code examples (health bars, tooltips, minimap)

8. **rmlui-integration-architecture.md** - Complete integration design
   - Multi-layer architecture (game → interface → adapter → RmlUI → primitives)
   - IUISystem, IDocument, IElement abstract interfaces
   - RmlUI adapter hidden in .cpp files (game never sees RmlUI headers)
   - Renderer backend (RmlUI geometry → Primitives API)
   - Three usage patterns: XML, programmatic, hybrid
   - Complete game loop integration example
   - CMake configuration with private RmlUI linking
   - Testing strategy (unit tests with mock, integration tests with real)
   - Migration strategy (game code unchanged, rewrite adapter only)

**Key Architectural Decisions:**

**1. RmlUI for Screen-Space Complex UI**
- HTML/CSS-like markup for complex layouts (inventory grids, skill trees)
- Flexbox layout engine (handles nested containers automatically)
- Can be used programmatically (NO .rml files required)
- Isolated via interface layer (game code never depends on RmlUI)

**2. Unified Primitive Rendering API**
- **Solves "two different APIs for rectangles" problem**
- One DrawRect/DrawText/DrawTexture API used by everything:
  - RmlUI backend
  - Game world rendering
  - World-space UI (health bars)
  - Custom UI components
- Batching implementation (minimize draw calls)
- Integration with existing renderer

**3. Clear Rendering Boundaries**
- **RmlUI**: Screen-space panels only (menus, inventory, dialogs)
- **Custom/Primitives**: Game world + world-space UI + simple HUD
- **Vector Graphics**: Complex SVG entities (trees, decorations)
- No overlap or confusion about which system to use

**4. Isolation Strategy**
- Game code depends on IUISystem/IDocument/IElement interfaces
- RmlUI adapter hidden in private .cpp files
- CMake links RmlUI privately (not exposed to game)
- Migration cost: Rewrite adapter (~500 lines), game code unchanged

**Critical Insights:**

**Production Game Requirements Clarified:**
- Not a learning project or toy
- Complex management game UI (thousands of elements)
- Inventory grids, skill trees, resource management panels
- Performance critical (60 FPS with dynamic updates)

**RmlUI Programmatic API Discovery:**
- Can create all UI from C++ (NO .rml files required)
- Avoids markup file lock-in
- Enables `.Args{}` wrapper pattern
- Still get flexbox layout engine benefits

**Text Rendering:**
- RmlUI requires FreeType (can't avoid)
- Can write custom font backend using msdfgen for SDF fonts
- Integration point with vector graphics system

**Scrolling Containers:**
- OpenGL scissor test (hardware clipping)
- RmlUI handles automatically
- Primitives API exposes PushScissor/PopScissor for custom usage

**Next Steps:**

1. ✅ **Complete architecture documentation** (DONE)
2. **Prototype RmlUI integration** (1-2 weeks)
   - Implement primitive rendering API
   - Build RmlUI backend + isolation layer
   - Create one complex UI panel (inventory or skill tree)
   - Validate performance and integration
3. **Evaluate prototype results**
   - Does flexbox handle layouts?
   - Is performance acceptable?
   - Are constraints manageable?
4. **Make final decision**
   - Commit to RmlUI if prototype succeeds
   - Build custom if critical issues found

**Documentation Organization:**
- Created `/docs/technical/ui-framework/` subdirectory
- Updated `/docs/technical/INDEX.md` with UI Framework section
- All documents cross-referenced and navigable


