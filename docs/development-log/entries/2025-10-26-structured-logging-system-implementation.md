# Structured Logging System Implementation

**Date:** 2025-10-26

**Core Engine Pattern Complete:**

Implemented a production-ready structured logging system for the entire project, providing organized diagnostic output with categories and log levels.

**System Architecture:**

**Logger Class** (`libs/foundation/utils/log.{h,cpp}`):
- Four log levels: Debug, Info, Warning, Error
- Nine categories: Renderer, Physics, Audio, Network, Game, World, UI, Engine, Foundation
- Per-category level filtering (set different verbosity for each system)
- Automatic timestamping (HH:MM:SS format)
- ANSI color codes for terminal output (gray/white/yellow/red)
- File and line number capture for warnings and errors

**Convenience Macros:**
```cpp
LOG_DEBUG(category, format, ...)
LOG_INFO(category, format, ...)
LOG_WARNING(category, format, ...)
LOG_ERROR(category, format, ...)
```

**Build Configuration:**
- Development builds: All log levels available, `DEVELOPMENT_BUILD` flag enables Debug/Info/Warning
- Release builds: Only Error logs remain, Debug/Info/Warning compile to `((void)0)`
- CMake automatically sets flag for Debug and RelWithDebInfo build types

**Usage Examples:**
```cpp
LOG_INFO(Renderer, "Initializing renderer: %dx%d", width, height);
LOG_ERROR(Network, "Failed to connect to server");
LOG_DEBUG(Physics, "Tick took %f ms", deltaTime);
```

**Output Format:**
```
[19:08:10][UI][INFO] UI Sandbox - Component Testing & Demo Environment
[19:08:10][Renderer][INFO] OpenGL Version: 4.1 ATI-7.0.23
[19:08:11][Foundation][INFO] Debug server: http://localhost:8081
```

**Design Decision - Macro Naming:**

Chose **unprefixed global macros** (`LOG_ERROR` not `WSIM_LOG_ERROR`) for developer experience:
- **Pros**: Cleaner code, better readability, shorter is better for ubiquitous operations
- **Cons**: Potential conflicts with other libraries defining similar macros
- **Mitigation**: Game project (not library), we control dependencies, can refactor if needed
- **Documented**: Tradeoff explicitly documented in `/docs/technical/logging-system.md`

**Integration:**
- ui-sandbox fully converted to use logging system (replaced all `std::cout`/`std::cerr`)
- Foundation library exports logger for use by all other libraries
- Initialized in `main()` before any other systems

**Testing:**
- Verified colored output with timestamps in terminal
- Confirmed different categories display correctly
- Tested log level filtering (Debug logs visible in development builds)
- Verified ANSI color codes work on macOS terminal

**Future HTTP Streaming Integration:**

Documentation includes design for lock-free ring buffer + Server-Sent Events streaming to external debug app (from `/docs/technical/observability/developer-server.md`), to be implemented when needed.

**Files Created/Modified:**
- `libs/foundation/utils/log.h` - Logger class, enums, macros (NEW)
- `libs/foundation/utils/log.cpp` - Implementation with console output (NEW)
- `libs/foundation/CMakeLists.txt` - Added log.cpp, DEVELOPMENT_BUILD flag
- `apps/ui-sandbox/main.cpp` - Converted all output to logging system
- `docs/technical/logging-system.md` - Added macro naming convention section

**Next Engine Pattern:**
String hashing system (FNV-1a with compile-time hashing)


