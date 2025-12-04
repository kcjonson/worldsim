# Application Class - Unified Game Loop Architecture

**Date:** 2025-10-29

**Production-Quality Game Loop Complete:**

Implemented a unified Application class for managing the main game loop across all applications (ui-sandbox, world-sim). Based on ColonySim's ScreenManager pattern, adapted for WorldSim's Scene-based architecture.

**Implementation:**

**Application Class** (`libs/engine/application/application.{h,cpp}`):
- Owns main game loop with consistent delta time calculation
- Provides pause/resume functionality
- Exception handling and error recovery
- GLFW window integration with framebuffer resize callbacks
- Manages viewport updates for rendering system
- Integrates with DebugServer for metrics and control

**IScene Interface Updates**:
- Added `HandleInput(float dt)` method for input processing phase
- Separates concerns: HandleInput → Update → Render
- All scenes now implement HandleInput() in addition to Update() and Render()

**SceneManager Enhancement**:
- Forwards HandleInput() calls to current scene
- Maintains existing Update() and Render() forwarding
- Provides unified scene lifecycle management

**Refactored Applications**:
- **ui-sandbox**: Removed 141 lines of boilerplate game loop code
- **world-sim**: Implemented using Application pattern (133+ lines of clean integration)
- Both apps now share identical game loop (DRY principle)

**Key Benefits:**

**Shared Infrastructure**: Both applications use exact same game loop implementation - no duplication.

**Testability**: Scenes can be tested independently without GLFW dependency (mock Application).

**Foundation for UI**: Ready for ColonySim UI integration - matches the architecture patterns used in production.

**Application-Level Features**: Pause, metrics, exception handling, viewport management all handled at application level, not per-scene.

**Separation of Concerns**: Clear lifecycle phases (HandleInput → Update → Render) make scene logic easier to reason about.

**Files Created:**
- `libs/engine/application/application.h` - Application class interface (117 lines)
- `libs/engine/application/application.cpp` - Game loop implementation (172 lines)

**Files Modified:**
- `apps/ui-sandbox/main.cpp` - Refactored to use Application (-141 lines of boilerplate)
- `apps/world-sim/main.cpp` - Implemented using Application pattern (+133 lines clean code)
- `libs/engine/scene/scene.h` - Added HandleInput() to IScene interface
- `libs/engine/scene/scene_manager.{h,cpp}` - Added HandleInput() forwarding
- All scene files in `apps/ui-sandbox/scenes/` - Implemented HandleInput() method

**Testing:**
- Verified ui-sandbox runs with all scenes (shapes, font_test, vector_perf, etc.)
- Confirmed metrics stream correctly to debug server
- Tested pause/resume functionality via control endpoints
- Validated exception handling with deliberate errors

**Next Steps:**
This Application class provides the foundation for integrating ColonySim's UI systems, which expect similar lifecycle management patterns.


