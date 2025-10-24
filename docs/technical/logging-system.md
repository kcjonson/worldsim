# Structured Logging System

Created: 2025-10-12
Last Updated: 2025-10-12
Status: Active
Priority: **Implement Immediately**

## What Is Structured Logging?

Structured logging organizes log messages by **category** (which system) and **level** (how important).

**The Problem:**
```cpp
// Basic logging - everything mixed together
printf("Loaded texture grass.svg\n");
printf("Player health: 50\n");
printf("ERROR: Failed to connect to server\n");
printf("Physics tick took 2.5ms\n");

// Output is a mess, can't filter, hard to debug
```

**The Solution:**
```cpp
// Structured logging - organized and filterable
LOG(Renderer, Info, "Loaded texture: {}", "grass.svg");
LOG(Game, Debug, "Player health: {}", 50);
LOG(Network, Error, "Failed to connect to server");
LOG(Physics, Debug, "Physics tick took {}ms", 2.5);

// Can filter: show only Renderer warnings and errors
// Can disable: turn off Debug logs in release builds
```

## Why It Matters

### Development
- **Filter noise**: See only relevant logs for system you're debugging
- **Debug faster**: Know which system is logging
- **Leave code in**: Disable debug logs at runtime, not by deleting code

### Performance
- **Zero cost in release**: Debug logs compiled out completely
- **Conditional logging**: Skip expensive formatting if log won't show
- **Fast paths**: Critical code can avoid logging overhead

### Shipping
- **Customer support**: Users can enable logging to diagnose issues
- **Analytics**: Log important events without spam
- **Profiling**: Time spent in each category

## Decision

Implement category-based logging with 4 verbosity levels.

**Levels** (from most to least verbose):
1. **Debug**: Detailed diagnostic information
2. **Info**: General informational messages
3. **Warning**: Something unexpected but handleable
4. **Error**: Something failed

**Categories** (one per major system):
- Renderer, Physics, Audio, Network, Game, World, UI, Engine, etc.

## Implementation

### Core Logging System

```cpp
// libs/foundation/utils/log.h
#pragma once

#include <cstdio>
#include <cstdarg>

namespace foundation {

enum class LogLevel {
	Debug,
	Info,
	Warning,
	Error
};

enum class LogCategory {
	Renderer,
	Physics,
	Audio,
	Network,
	Game,
	World,
	UI,
	Engine,
	Foundation,
	Count
};

class Logger {
public:
	static void SetLevel(LogCategory category, LogLevel level);
	static LogLevel GetLevel(LogCategory category);

	static void Log(
		LogCategory category,
		LogLevel    level,
		const char* file,
		int         line,
		const char* format,
		...
	);

private:
	static LogLevel s_levels[static_cast<int>(LogCategory::Count)];
};

// Helper to convert enum to string
const char* CategoryToString(LogCategory cat);
const char* LevelToString(LogLevel level);

} // namespace foundation

// Convenience macros
#define LOG_DEBUG(category, format, ...) \
	foundation::Logger::Log(\
		foundation::LogCategory::category, \
		foundation::LogLevel::Debug, \
		__FILE__, __LINE__, \
		format, ##__VA_ARGS__)

#define LOG_INFO(category, format, ...) \
	foundation::Logger::Log(\
		foundation::LogCategory::category, \
		foundation::LogLevel::Info, \
		__FILE__, __LINE__, \
		format, ##__VA_ARGS__)

#define LOG_WARNING(category, format, ...) \
	foundation::Logger::Log(\
		foundation::LogCategory::category, \
		foundation::LogLevel::Warning, \
		__FILE__, __LINE__, \
		format, ##__VA_ARGS__)

#define LOG_ERROR(category, format, ...) \
	foundation::Logger::Log(\
		foundation::LogCategory::category, \
		foundation::LogLevel::Error, \
		__FILE__, __LINE__, \
		format, ##__VA_ARGS__)

// Compile out debug logs in release
#ifdef NDEBUG
	#undef LOG_DEBUG
	#define LOG_DEBUG(category, format, ...) ((void)0)
#endif
```

### Implementation

```cpp
// libs/foundation/utils/log.cpp
#include "log.h"
#include <ctime>

namespace foundation {

LogLevel Logger::s_levels[static_cast<int>(LogCategory::Count)];

void Logger::SetLevel(LogCategory category, LogLevel level) {
	s_levels[static_cast<int>(category)] = level;
}

LogLevel Logger::GetLevel(LogCategory category) {
	return s_levels[static_cast<int>(category)];
}

void Logger::Log(
	LogCategory category,
	LogLevel    level,
	const char* file,
	int         line,
	const char* format,
	...
) {
	// Filter by level
	if (level < GetLevel(category)) {
		return;  // Too verbose, skip
	}

	// Get timestamp
	time_t now = time(nullptr);
	tm* timeinfo = localtime(&now);
	char timestamp[32];
	strftime(timestamp, sizeof(timestamp), "%H:%M:%S", timeinfo);

	// Print prefix: [TIME][CATEGORY][LEVEL]
	printf("[%s][%s][%s] ",
		timestamp,
		CategoryToString(category),
		LevelToString(level)
	);

	// Print message
	va_list args;
	va_start(args, format);
	vprintf(format, args);
	va_end(args);

	// Add file:line for errors
	if (level == LogLevel::Error) {
		printf(" (%s:%d)", file, line);
	}

	printf("\n");
	fflush(stdout);
}

const char* CategoryToString(LogCategory cat) {
	switch (cat) {
		case LogCategory::Renderer:   return "Renderer";
		case LogCategory::Physics:    return "Physics";
		case LogCategory::Audio:      return "Audio";
		case LogCategory::Network:    return "Network";
		case LogCategory::Game:       return "Game";
		case LogCategory::World:      return "World";
		case LogCategory::UI:         return "UI";
		case LogCategory::Engine:     return "Engine";
		case LogCategory::Foundation: return "Foundation";
		default:                      return "Unknown";
	}
}

const char* LevelToString(LogLevel level) {
	switch (level) {
		case LogLevel::Debug:   return "DEBUG";
		case LogLevel::Info:    return "INFO";
		case LogLevel::Warning: return "WARN";
		case LogLevel::Error:   return "ERROR";
		default:                return "?????";
	}
}

} // namespace foundation
```

## Usage Examples

### Basic Logging

```cpp
#include <foundation/utils/log.h>

void InitializeRenderer(int width, int height) {
	LOG_INFO(Renderer, "Initializing renderer: %dx%d", width, height);

	if (!CreateContext()) {
		LOG_ERROR(Renderer, "Failed to create OpenGL context");
		return;
	}

	LOG_DEBUG(Renderer, "OpenGL version: %s", glGetString(GL_VERSION));
	LOG_INFO(Renderer, "Renderer initialized successfully");
}
```

**Output:**
```
[10:30:15][Renderer][INFO] Initializing renderer: 1920x1080
[10:30:15][Renderer][DEBUG] OpenGL version: 4.6.0
[10:30:15][Renderer][INFO] Renderer initialized successfully
```

### Conditional Debug Logging

```cpp
void ProcessChunk(Chunk* chunk) {
	LOG_DEBUG(World, "Processing chunk at (%d, %d)", chunk->x, chunk->y);

	// ... process chunk ...

	LOG_DEBUG(World, "Chunk processed: %d tiles generated", chunk->tileCount);
}

// In release builds, LOG_DEBUG compiles to nothing:
// void ProcessChunk(Chunk* chunk) {
//     // ... process chunk ...
// }
```

### Error Logging with Context

```cpp
TextureHandle LoadTexture(const char* path) {
	LOG_INFO(Renderer, "Loading texture: %s", path);

	FILE* file = fopen(path, "rb");
	if (!file) {
		LOG_ERROR(Renderer, "Failed to open texture file: %s", path);
		return InvalidHandle;
	}

	// ... load texture ...

	LOG_DEBUG(Renderer, "Texture loaded: %dx%d, format=%d", width, height, format);
	return handle;
}
```

**Output:**
```
[10:30:20][Renderer][INFO] Loading texture: assets/grass.svg
[10:30:20][Renderer][ERROR] Failed to open texture file: assets/grass.svg (renderer.cpp:123)
```

### Setting Log Levels

```cpp
// In main.cpp or config
void ConfigureLogging() {
	// Production: only warnings and errors
	Logger::SetLevel(LogCategory::Renderer, LogLevel::Warning);
	Logger::SetLevel(LogCategory::Game, LogLevel::Warning);

	// Development: verbose physics debugging
	Logger::SetLevel(LogCategory::Physics, LogLevel::Debug);

	// Always log errors
	Logger::SetLevel(LogCategory::Network, LogLevel::Error);
}

// Or from config file
void LoadLogConfig(const json& config) {
	if (config.contains("logLevels")) {
		for (const auto& [cat, level] : config["logLevels"].items()) {
			// Parse and set levels
		}
	}
}
```

## Integration Points

### All Libraries
Every library uses logging for diagnostics:
- `libs/foundation/` - Platform, utilities
- `libs/renderer/` - Graphics operations
- `libs/ui/` - UI events and rendering
- `libs/world/` - World generation
- `libs/game-systems/` - Chunk loading, tile processing
- `libs/engine/` - Scene management, ECS

### Configuration
**Location**: `assets/config/logging.json`
```json
{
	"logLevels": {
		"Renderer": "Info",
		"Physics": "Debug",
		"Game": "Debug",
		"Network": "Error"
	},
	"logToFile": true,
	"logFilePath": "logs/world-sim.log"
}
```

## Advanced Features

### Log to File

```cpp
class Logger {
	static FILE* s_logFile;

public:
	static void OpenLogFile(const char* path) {
		s_logFile = fopen(path, "w");
	}

	static void CloseLogFile() {
		if (s_logFile) {
			fclose(s_logFile);
			s_logFile = nullptr;
		}
	}

	static void Log(...) {
		// ... format message ...

		// Output to console
		printf("%s\n", message);

		// Also output to file
		if (s_logFile) {
			fprintf(s_logFile, "%s\n", message);
			fflush(s_logFile);
		}
	}
};
```

### Performance Profiling

```cpp
void UpdatePhysics(float dt) {
	LOG_DEBUG(Physics, "Physics update started");

	auto start = std::chrono::high_resolution_clock::now();

	// ... physics work ...

	auto end = std::chrono::high_resolution_clock::now();
	auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

	LOG_DEBUG(Physics, "Physics update took %lld μs", duration.count());
}
```

### Formatted Logging (Modern C++)

If using a library like `fmt` or C++20 `<format>`:

```cpp
LOG_INFO(Renderer, "Loaded texture: {} ({}x{}, {} KB)",
	textureName, width, height, sizeKB);
```

## Best Practices

### DO
```cpp
// Use appropriate levels
LOG_DEBUG(Physics, "Detailed diagnostic info");
LOG_INFO(Game, "Normal operation message");
LOG_WARNING(World, "Unexpected but handled: retrying");
LOG_ERROR(Network, "Operation failed");

// Include context in errors
LOG_ERROR(Renderer, "Failed to compile shader: %s (error: %s)",
	shaderPath, errorMessage);

// Use debug logs liberally (free in release)
LOG_DEBUG(Game, "Player moved to (%f, %f)", x, y);
```

### DON'T
```cpp
// Don't log in tight loops (even if filtered)
for (int i = 0; i < 1000000; i++) {
	LOG_DEBUG(Physics, "Iteration %d", i);  // BAD! Too much
}

// Don't use wrong levels
LOG_ERROR(Game, "Player pressed jump button");  // BAD! Not an error

// Don't log sensitive data
LOG_INFO(Network, "Password: %s", password);  // BAD! Security risk

// Don't format if log will be filtered (do this instead)
if (Logger::GetLevel(LogCategory::Physics) <= LogLevel::Debug) {
	std::string expensiveString = GenerateDebugInfo();
	LOG_DEBUG(Physics, "Debug info: %s", expensiveString.c_str());
}
```

## Performance Characteristics

### Zero Cost in Release
```cpp
// Debug build
LOG_DEBUG(Game, "Expensive: %s", GenerateString().c_str());
// Calls logger, formats string, outputs

// Release build (NDEBUG defined)
LOG_DEBUG(Game, "Expensive: %s", GenerateString().c_str());
// Compiles to: ((void)0)
// GenerateString() is never called!
```

### Filtering Cost
```cpp
// If level check fails, no formatting happens
LOG_INFO(Renderer, "Texture: %s", GetTextureName());
// Level check: ~1-2 CPU cycles
// If filtered: GetTextureName() never called
```

## Trade-offs

**Pros:**
- Organized, filterable output
- Zero cost debug logs in release
- Easy to use throughout codebase
- Helps debugging immensely

**Cons:**
- Slight boilerplate (macros)
- Must choose appropriate categories
- Can't filter by multiple criteria (e.g., "Renderer AND Physics")

**Decision:** Essential tool for development, worth minor complexity.

## Alternatives Considered

### Option 1: printf/cout everywhere
**Rejected** - No filtering, no structure, hard to debug

### Option 2: Use existing library (spdlog, glog)
**Considered** - Good libraries, but adds dependency. Our needs are simple enough to implement.

### Option 3: No logging, use debugger
**Rejected** - Need logs for post-mortem debugging, customer support, automated testing

## Implementation Status

- [ ] Core logger implementation
- [ ] Category and level enums
- [ ] Convenience macros
- [ ] File output support
- [ ] Config integration
- [ ] Documentation

## Implementation Order

1. **Phase 1: Basic System** (~2 hours)
   - Core Logger class
   - Category and level enums
   - Basic macros
   - Console output only

2. **Phase 2: Features** (~1 hour)
   - File output
   - Timestamp formatting
   - Level filtering

3. **Phase 3: Integration** (~30 min)
   - Config loading (logging.json)
   - Default levels per category

4. **Phase 4: Usage** (ongoing)
   - Add logging throughout codebase
   - Establish conventions

## Related Documentation

- Code: `libs/foundation/utils/log.h` (once implemented)
- Code: `libs/foundation/utils/log.cpp` (once implemented)

## Notes

**Log Spam:**
Be careful with logs in hot paths (per-frame, per-tile, etc.). Use Debug level and ensure they're compiled out in release.

**Thread Safety:**
Basic implementation is not thread-safe. When we add threading, wrap printf calls with mutex.

**Colors:**
Consider ANSI color codes for different levels:
- Debug: Gray
- Info: White
- Warning: Yellow
- Error: Red
