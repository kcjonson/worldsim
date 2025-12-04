# Pixel-Perfect UI Rendering & Window Sizing

**Date:** 2025-10-26

**Fixed Critical Rendering Bug:**

The primitive rendering system was using a hardcoded 800x600 virtual coordinate system, causing shapes to physically change pixel dimensions when the window was resized. This violated the design specification for 1:1 pixel mapping.

**Root Cause:**
- Projection matrix was `ortho(0, 800, 600, 0)` regardless of actual framebuffer size
- `Rect(50, 50, 200, 100)` would be 320px wide in an 1280x720 window, but 640px wide in a 2560x1440 window
- Shapes scaled with window resize, breaking pixel-perfect rendering

**Fix Applied:**
- Changed projection matrix to use actual framebuffer dimensions: `ortho(0, m_viewportWidth, m_viewportHeight, 0)`
- Added `SetViewport(width, height)` to BatchRenderer and Primitives API
- Called `SetViewport()` on window creation and framebuffer resize callback
- Now `Rect(50, 50, 200, 100)` is **always exactly 200×100 pixels**, regardless of window size

**UI Sandbox Improvements:**
- Window now launches at 80% of screen size (was hardcoded 800x600)
- Queries primary monitor via GLFW to calculate appropriate initial size
- Created demo system structure (`demos/demo.h`, `demos/shapes_demo.cpp`)
- Moved rendering code from `main.cpp` to proper demo implementation
- Console output shows screen and window dimensions on startup

**Industry Standard Alignment:**
- RmlUI uses pixel coordinates with 1:1 mapping (per our documentation)
- ImGui uses pixel coordinates with 1:1 mapping
- Unity UI default is "Constant Pixel Size" mode
- Our implementation now matches these standards

**Files Modified:**
- `libs/renderer/primitives/batch_renderer.{h,cpp}` - Added viewport tracking, fixed projection matrix
- `libs/renderer/primitives/primitives.{h,cpp}` - Exposed SetViewport() API
- `apps/ui-sandbox/main.cpp` - Added monitor querying, demo system integration, viewport updates
- `apps/ui-sandbox/demos/demo.h` - Created demo interface (NEW)
- `apps/ui-sandbox/demos/shapes_demo.cpp` - Implemented shapes demo (NEW)
- `apps/ui-sandbox/CMakeLists.txt` - Added shapes_demo.cpp to build

**Test Results:**
```
Screen: 3200x1800
Window: 2560x1440 (80% of screen)
```

Shapes now maintain constant pixel dimensions when window is resized. ✅


