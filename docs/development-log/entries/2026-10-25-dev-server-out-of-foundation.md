# 2026-10-25 - Dev server out of foundation; the logger takes a sink (SAL-30)

## Summary

foundation no longer links OpenGL or cpp-httplib. DebugServer, LockFreeRingBuffer, and PngEncoder moved to `libs/engine`, PerformanceMetrics moved to `libs/renderer`, and the logger's raw `DebugServer*` became a `std::function` sink that AppLauncher installs. Routes, ports, JSON strings, screenshots, and log streaming are unchanged; the foundation library is now GL-free and socket-free and can be built headless.

## Details

Files moved with `git mv` (no content changes beyond two location comments):

- `libs/foundation/debug/DebugServer.{h,cpp,test.cpp}` -> `libs/engine/debug/`
- `libs/foundation/debug/LockFreeRingBuffer.h` -> `libs/engine/debug/`
- `libs/foundation/graphics/PngEncoder.{h,cpp}` -> `libs/engine/graphics/`
- `libs/foundation/metrics/PerformanceMetrics.{h,cpp}` -> `libs/renderer/metrics/`

No include line changed: every includer spells `debug/DebugServer.h`, `graphics/PngEncoder.h`, or `metrics/PerformanceMetrics.h` and already had the engine or renderer include root. foundation's `debug/` keeps Tunables, `graphics/` keeps Color, Rect, ClipTypes, and PrimitiveStyles, `metrics/` keeps SystemResources.

The logger sink. `Logger::setDebugServer(Foundation::DebugServer*)` is gone. `Logger::setSink(LogSink)` takes a `std::function<void(const foundation::LogRecord&)>`; `LogRecord` carries the category, level, formatted message (at most 255 chars, valid only for the call), `__FILE__`, and line. The sink runs before the console level filter, exactly where the pointer call used to be, so the developer client still sees every message and filters itself. AppLauncher installs a lambda that forwards to `DebugServer::updateLog` and clears it with `setSink({})` at the same spot it used to null the pointer. DebugServer's duplicate `Foundation::LogLevel` and `LogCategory` enums and their to-string helpers are deleted; `LogEntry` holds the foundation enums and `toJSON` uses `foundation::levelToString` and `categoryToString`, which produce the same strings ("DEBUG", "INFO", "WARN", "ERROR" and the category names), so the SSE wire format did not change. `Log.h` now includes `<functional>`.

PerformanceMetrics went to renderer, not engine as the ticket said. renderer's `MetricsCollector.h` includes it and returns `PerformanceMetrics` from its public API, and renderer may not depend on engine, so it sits beside MetricsCollector and GPUTimer in `libs/renderer/metrics/`.

CMake: foundation links only `glm::glm` (PUBLIC). engine compiles `debug/DebugServer.cpp` and `graphics/PngEncoder.cpp` and links `httplib::httplib` PRIVATE (plus CoreFoundation on macOS); OpenGL and GLEW already reach engine PUBLIC through renderer. renderer compiles `metrics/PerformanceMetrics.cpp`. worldgen-cli and the foundation, geometry, world, and game-systems test executables stop pulling httplib and OpenGL into their link lines; nothing in them used either.

Namespaces were kept: every moved type is still in `Foundation::`, so the diff is a verifiable pure move. Renaming them into `engine::` and `Renderer::` is tracked as WOR-518.

## Related Documentation

- `/docs/technical/build-performance.md` (link visibility note updated)
- `/docs/technical/observability/` (dev server routes, unchanged)
- `/docs/technical/logging-system.md`

## Next Steps

- WOR-518: rename the moved types out of `Foundation::` (64 DebugServer-family uses in 5 files, 7 PerformanceMetrics uses in 3 files, 2 PngEncoder uses in AssetRenderer.cpp).
