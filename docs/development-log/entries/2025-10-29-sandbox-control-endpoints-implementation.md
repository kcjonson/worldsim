# Sandbox Control Endpoints Implementation

**Date:** 2025-10-29

**Production Developer Tools Enhancement:**

Implemented HTTP control endpoints for the ui-sandbox, enabling remote control via simple GET requests with query parameters. Completes the sandbox development workflow with graceful lifecycle management and port conflict detection.

**Implementation:**

1. **ControlAction Enum** - Single atomic enum prevents conflicting control directives
   - Actions: `None`, `Exit`, `SceneChange`, `Pause`, `Resume`, `ReloadScene`
   - Thread-safe atomic operations (HTTP thread writes, main thread reads)

2. **Control Endpoint** - `GET /api/control?action={action}`
   - `?action=exit` - Gracefully exit sandbox
   - `?action=scene&scene=name` - Switch scenes with validation
   - `?action=pause` - Pause scene updates/rendering
   - `?action=resume` - Resume scene updates/rendering
   - `?action=reload` - Reload current scene (OnExit + OnEnter)
   - Returns JSON responses with status/error

3. **Main Loop Integration**
   - Control action checking after `glfwPollEvents()`
   - All actions execute on main thread (GLFW/OpenGL safety)
   - Scene switching with validation and logging
   - Pause state tracking (skips update/render when paused)

4. **Port Conflict Detection**
   - Disabled SO_REUSEADDR on server socket
   - Prevents multiple instances on same port
   - Clear error message with curl command to kill existing instance
   - Instant detection (no HTTP connection delay)

5. **Scene Manager Enhancement**
   - Added `GetCurrentSceneName()` method for reload functionality

**Design Decisions:**

**Single atomic enum over multiple flags:** Prevents conflicting directives. Simpler API with clear semantics.

**GET with query params over POST+JSON:** Simpler for future use. Browser-testable. No JSON parsing.

**Disabled SO_REUSEADDR:** Prevents port conflicts without separate socket check. Fast and reliable.

**Main thread execution:** All GLFW/OpenGL operations on main thread. HTTP thread only sets flags.

**Files Modified:**
- `libs/foundation/debug/debug_server.{h,cpp}` - Control endpoint, SO_REUSEADDR disable
- `libs/engine/scene/scene_manager.{h,cpp}` - Added GetCurrentSceneName()
- `apps/ui-sandbox/main.cpp` - Control action processing in main loop
- `CLAUDE.md` - Added Sandbox Control section

**Testing:**
- ✅ Pause/Resume - verified scene updates stop/start
- ✅ Scene switching - tested shapes → arena transition
- ✅ Scene reload - verified OnExit/OnEnter called
- ✅ Exit - clean shutdown with exit code 0
- ✅ Port conflict - second instance fails with helpful error


