# Diagnostic Drawing (Immediate Mode)

Created: 2025-10-12
Last Updated: 2025-10-24
Status: Active
Priority: **Implement Later** (after basic rendering works)

## Scope & Purpose

**What this is:** Manual developer debugging tool for temporary diagnostic visualization in the game viewport.

**What this is NOT:**
- NOT part of the HTTP debug server UI (see [observability/developer-server.md](./observability/developer-server.md))
- NOT for automated inspection (use [UI Testability](./observability/ui-inspection.md) instead)
- NOT a persistent visualization system

**Use Case:** Developer temporarily adds `DebugDraw::Line(...)` calls while debugging a specific issue (e.g., visualizing chunk boundaries, collision boxes, camera frustum). Removes these calls when done debugging.

**No Conflict with External Debug UI:** The external debug web app (from [observability/developer-server.md](./observability/developer-server.md)) doesn't mirror the game viewport, so debug rendering doesn't interfere with it. They serve different purposes:
- **Debug Rendering** (this doc): Visual overlays IN the game viewport
- **HTTP Debug UI**: Separate browser window showing metrics/logs/profiler

## Application Scope

**Runs in:**
- Main game (Development builds only)
- `ui-sandbox` (Development builds only)
- Any application with rendering

**Build Requirement:**
- Compiled in: `DEVELOPMENT_BUILD` flag set
- Compiled out: Release builds (zero code footprint)

**Runtime Control:**
- No dedicated toggle key (F3 is for UI hover inspection)
- Always available in Development builds
- Developer adds/removes calls in code as needed

## What Is Immediate Mode Debug Rendering?

Immediate mode debug rendering lets you draw debug shapes (lines, boxes, spheres, text) with simple function calls, without managing graphics state or object lifetimes.

**Think of it like:** Console.WriteLine for graphics - just call a function and it appears.

## The Problem

```cpp
// Traditional rendering - lots of setup
void DebugDrawChunkBounds(Chunk* chunk) {
	// Create vertex buffer
	GLuint vbo;
	glGenBuffers(1, &vbo);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);

	// Upload vertices for box
	float vertices[] = { /* 24 vertices for box */ };
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);

	// Set up shader, uniforms, draw state...
	UseShader(debugShader);
	SetUniform("color", red);
	glDrawArrays(GL_LINES, 0, 24);

	// Cleanup
	glDeleteBuffers(1, &vbo);
}

// Problems:
// - Tons of boilerplate
// - Must manage buffers
// - Hard to add/remove quickly
// - State management nightmare
```

## The Solution

```cpp
// Immediate mode - just call functions!
void DebugDrawChunkBounds(Chunk* chunk) {
	vec3 min = chunk->GetMin();
	vec3 max = chunk->GetMax();

	DebugDraw::Box(min, max, Color::Red);  // Done!
}

// At end of frame, somewhere in render loop
DebugDraw::Render();  // Batches and draws everything

// Benefits:
// - One line of code
// - No state management
// - Easy to add/remove
// - Automatic batching
```

## Why It Matters

### Development Speed
- **Visualize instantly**: See chunk bounds, tile edges, paths
- **Fast iteration**: Add/remove debug draws in seconds
- **No boilerplate**: Just function calls
- **Essential tool**: Like print debugging, but visual

### Use Cases in World-Sim
1. **Chunk System**: Visualize chunk boundaries and loading
2. **Tile Blending**: Show tile edges and blend zones
3. **World Generation**: Display heightmaps, biome boundaries
4. **Camera**: Show frustum, zoom level indicators
5. **Performance**: Display frame time graphs
6. **AI/Path**: Show paths, waypoints (future)

## Implementation

### Core API

```cpp
// libs/renderer/debug/debug_draw.h
#pragma once

#include <foundation/math/vector.h>
#include <vector>

namespace renderer {

struct Color {
	float r, g, b, a;

	static const Color Red;
	static const Color Green;
	static const Color Blue;
	static const Color Yellow;
	static const Color White;
	static const Color Black;
};

class DebugDraw {
public:
	// Initialize (call once at startup)
	static void Initialize();
	static void Shutdown();

	// Primitives
	static void Line(vec3 start, vec3 end, Color color = Color::White);
	static void Ray(vec3 origin, vec3 direction, Color color = Color::White);
	static void Box(vec3 min, vec3 max, Color color = Color::White);
	static void Sphere(vec3 center, float radius, Color color = Color::White);
	static void Circle(vec3 center, vec3 normal, float radius, Color color = Color::White);
	static void Axes(vec3 origin, float size = 1.0f);  // XYZ axes

	// 2D
	static void Line2D(vec2 start, vec2 end, Color color = Color::White);
	static void Rect(vec2 min, vec2 max, Color color = Color::White);
	static void Circle2D(vec2 center, float radius, Color color = Color::White);

	// Text (world space)
	static void Text(vec3 position, const char* format, ...);

	// Text (screen space)
	static void TextScreen(vec2 position, const char* format, ...);

	// Render all (call once per frame)
	static void Render(const mat4& viewProj);

	// Clear all (call after render)
	static void Clear();

private:
	struct DebugLine {
		vec3 start, end;
		Color color;
	};

	struct DebugText {
		vec3 position;
		char text[256];
		Color color;
	};

	static std::vector<DebugLine> s_lines;
	static std::vector<DebugText> s_texts;
};

} // namespace renderer
```

### Implementation Sketch

```cpp
// libs/renderer/debug/debug_draw.cpp
#include "debug_draw.h"

namespace renderer {

std::vector<DebugDraw::DebugLine> DebugDraw::s_lines;
std::vector<DebugDraw::DebugText> DebugDraw::s_texts;

void DebugDraw::Line(vec3 start, vec3 end, Color color) {
	s_lines.push_back({start, end, color});
}

void DebugDraw::Box(vec3 min, vec3 max, Color color) {
	// Draw 12 edges of box
	vec3 corners[8] = {
		{min.x, min.y, min.z}, {max.x, min.y, min.z},
		{max.x, max.y, min.z}, {min.x, max.y, min.z},
		{min.x, min.y, max.z}, {max.x, min.y, max.z},
		{max.x, max.y, max.z}, {min.x, max.y, max.z}
	};

	// Bottom
	Line(corners[0], corners[1], color);
	Line(corners[1], corners[2], color);
	Line(corners[2], corners[3], color);
	Line(corners[3], corners[0], color);

	// Top
	Line(corners[4], corners[5], color);
	Line(corners[5], corners[6], color);
	Line(corners[6], corners[7], color);
	Line(corners[7], corners[4], color);

	// Sides
	Line(corners[0], corners[4], color);
	Line(corners[1], corners[5], color);
	Line(corners[2], corners[6], color);
	Line(corners[3], corners[7], color);
}

void DebugDraw::Render(const mat4& viewProj) {
	if (s_lines.empty() && s_texts.empty()) {
		return;
	}

	// Set up debug shader
	UseShader(s_debugShader);
	SetUniform("u_viewProj", viewProj);

	// Batch all lines into single draw call
	std::vector<float> vertices;
	vertices.reserve(s_lines.size() * 12);  // 2 verts * 6 floats each

	for (const auto& line : s_lines) {
		// Start vertex: position + color
		vertices.push_back(line.start.x);
		vertices.push_back(line.start.y);
		vertices.push_back(line.start.z);
		vertices.push_back(line.color.r);
		vertices.push_back(line.color.g);
		vertices.push_back(line.color.b);

		// End vertex: position + color
		vertices.push_back(line.end.x);
		vertices.push_back(line.end.y);
		vertices.push_back(line.end.z);
		vertices.push_back(line.color.r);
		vertices.push_back(line.color.g);
		vertices.push_back(line.color.b);
	}

	// Upload and draw
	glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
	glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float),
		vertices.data(), GL_DYNAMIC_DRAW);

	glEnableVertexAttribArray(0);  // position
	glEnableVertexAttribArray(1);  // color
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), 0);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
		(void*)(3 * sizeof(float)));

	glDrawArrays(GL_LINES, 0, s_lines.size() * 2);

	// Render text (using font rendering system)
	for (const auto& text : s_texts) {
		RenderDebugText(text.position, text.text, text.color);
	}
}

void DebugDraw::Clear() {
	s_lines.clear();
	s_texts.clear();
}

} // namespace renderer
```

## Usage Examples

### Visualize Chunk Boundaries

```cpp
void RenderChunks() {
	for (Chunk* chunk : loadedChunks) {
		// Normal rendering
		RenderChunk(chunk);

		// Debug visualization
		#ifdef DEBUG_DRAW_CHUNKS
		vec3 min = chunk->GetWorldMin();
		vec3 max = chunk->GetWorldMax();
		DebugDraw::Box(min, max, Color::Green);
		DebugDraw::Text(
			vec3((min.x + max.x) / 2, max.y + 1, (min.z + max.z) / 2),
			"Chunk (%d, %d)", chunk->x, chunk->y
		);
		#endif
	}
}
```

### Show Tile Edges

```cpp
void DebugTileBlending(Tile* tile) {
	// Show tile bounds
	DebugDraw::Rect(tile->GetMin2D(), tile->GetMax2D(), Color::Yellow);

	// Show blend zones
	if (tile->BlendWithNeighbor(Edge::Top)) {
		vec2 blendStart = {tile->x, tile->y + tile->height};
		vec2 blendEnd = {tile->x + tile->width, tile->y + tile->height};
		DebugDraw::Line2D(blendStart, blendEnd, Color::Red);
	}

	// Show tile type
	DebugDraw::TextScreen(
		tile->GetCenter2D(),
		"Type: %s", GetTileTypeName(tile->type)
	);
}
```

### Visualize Camera Frustum

```cpp
void DebugCamera(const Camera& camera) {
	// Draw frustum
	std::array<vec3, 8> corners = camera.GetFrustumCorners();
	for (int i = 0; i < 4; i++) {
		// Near plane
		DebugDraw::Line(corners[i], corners[(i + 1) % 4], Color::Green);
		// Far plane
		DebugDraw::Line(corners[i + 4], corners[((i + 1) % 4) + 4], Color::Green);
		// Connecting lines
		DebugDraw::Line(corners[i], corners[i + 4], Color::Green);
	}

	// Draw axes at camera position
	DebugDraw::Axes(camera.GetPosition(), 2.0f);
}
```

### Frame Time Graph

```cpp
void RenderFrameTimeGraph() {
	static float frameTimes[100] = {};
	static int frameIndex = 0;

	frameTimes[frameIndex] = GetLastFrameTime();
	frameIndex = (frameIndex + 1) % 100;

	// Draw graph
	vec2 graphPos = {10, 10};
	vec2 graphSize = {200, 100};

	for (int i = 0; i < 99; i++) {
		float t1 = frameTimes[i] / 33.0f;  // Normalize to 33ms
		float t2 = frameTimes[i + 1] / 33.0f;

		vec2 p1 = {graphPos.x + i * 2, graphPos.y + t1 * graphSize.y};
		vec2 p2 = {graphPos.x + (i + 1) * 2, graphPos.y + t2 * graphSize.y};

		Color color = t1 > 1.0f ? Color::Red : Color::Green;
		DebugDraw::Line2D(p1, p2, color);
	}

	// 60 FPS line
	vec2 targetLine = {graphPos.x, graphPos.y + graphSize.y * (16.67f / 33.0f)};
	DebugDraw::Line2D(
		targetLine,
		{targetLine.x + graphSize.x, targetLine.y},
		Color::Yellow
	);
}
```

## Integration with Renderer

```cpp
// In main render loop
void RenderFrame() {
	// Normal rendering
	RenderWorld();
	RenderUI();

	// Debug rendering (last, on top of everything)
	#ifdef ENABLE_DEBUG_DRAW
	mat4 viewProj = camera.GetViewProjectionMatrix();
	DebugDraw::Render(viewProj);
	DebugDraw::Clear();
	#endif
}
```

## Configuration

```cpp
// Runtime enable/disable (no dedicated key - developer controls in code)
bool g_enableDebugDraw = false;  // Disabled by default

void EnableDebugDraw(bool enable) {
	g_enableDebugDraw = enable;
}

// Only render if enabled
if (g_enableDebugDraw) {
	DebugDraw::Render(viewProj);
}

// Developer can enable via console command or code
// (No keyboard shortcut - F3 is reserved for UI hover inspection)
```

## Performance Considerations

**Per-Frame Cost:**
- Store debug primitives: ~100 bytes each
- Single batched draw call: Fast
- Text rendering: Depends on font system

**Best Practices:**
- Compile out in release builds (#ifdef)
- Limit number of debug draws (e.g., max 1000 lines)
- Use simple primitives (lines are cheapest)
- Don't draw every frame if not needed

## Build Configuration

**Development Build** (`-DCMAKE_BUILD_TYPE=Development`):
- `DEVELOPMENT_BUILD` flag set
- Debug rendering compiled in
- `DebugDraw::*()` functions available
- Minimal overhead when not used (~0.01ms idle)
- ~300 KB memory for implementation

**Release Build** (`-DCMAKE_BUILD_TYPE=Release`):
- `DEVELOPMENT_BUILD` not defined
- Entire system compiled out via `#ifdef`
- Zero code footprint in binary
- Zero runtime overhead

**CMake Integration:**
```cmake
if(CMAKE_BUILD_TYPE STREQUAL "Development")
    target_compile_definitions(renderer PRIVATE DEVELOPMENT_BUILD)
    # DebugDraw implementation included
endif()
```

## Trade-offs

**Pros:**
- Essential development tool for manual debugging
- Fast iteration (add/remove calls quickly)
- Easy to use (simple function calls)
- No state management
- Visual debugging more intuitive than print statements

**Cons:**
- Extra rendering pass (but only when used)
- Memory for debug data (~1 MB for line buffers)
- ~300 lines of code to implement
- Need simple shader
- Renders IN game viewport (can obscure view)

**Decision:** Invaluable for development, worth the implementation effort. Complements (doesn't replace) external debug UI.

## Distinction from Other Debug Systems

**When to use Debug Rendering:**
- Visualizing chunk boundaries during world generation debugging
- Drawing collision boxes during physics debugging
- Showing camera frustum during camera system work
- Displaying AI paths during pathfinding implementation
- Temporary diagnostic overlays during active development

**When to use HTTP Debug Server instead:**
- Monitoring real-time metrics (FPS, memory, frame time)
- Viewing log output in organized interface
- Profiling function execution times
- Inspecting UI element hierarchy
- Automated testing and inspection

## Implementation Status

- [ ] Core DebugDraw class
- [ ] Line, box, sphere primitives
- [ ] Text rendering integration
- [ ] 2D primitives
- [ ] Batched rendering
- [ ] Runtime toggle
- [ ] Example usage

## Cross-Reference: Observability Systems

| System | Purpose | Access Method | In-Game UI | Availability |
|--------|---------|---------------|------------|--------------|
| **Diagnostic Drawing** (this doc) | Manual visual debugging | `DebugDraw::*()` in code | Yes (viewport) | Development builds |
| [UI Inspection](./observability/ui-inspection.md) | Inspect UI hierarchy, hover data | SSE `/stream/ui`, `/stream/hover` | No (external) | Development builds |
| [Logging](./logging-system.md) | Diagnostic messages | Console/file + SSE `/stream/logs` | No | All builds (Error only in Release) |
| [Developer Server](./observability/developer-server.md) | Application monitoring | SSE (metrics, profiler) | No (external) | Development builds |

**Key Distinctions:**
- **Diagnostic Drawing** (this doc): Temporary lines/boxes drawn IN viewport during manual debugging
- **UI Inspection**: Stream UI state to external app, inspect element hierarchy
- **Logging**: Text diagnostic output
- **Developer Server**: Monitor application performance and state

## Related Documentation

- **Related Systems**: [Developer Server](./observability/developer-server.md) - External monitoring (doesn't mirror viewport)
- **Related Systems**: [UI Inspection](./observability/ui-inspection.md) - UI inspection (external app)
- **Related Systems**: [Logging System](./logging-system.md) - Text diagnostics
- [Observability Overview](./observability/INDEX.md) - Full observability system overview
- **Tech**: [Renderer Architecture](./renderer-architecture.md) (if exists)
- **Code**: `libs/renderer/debug/debug_draw.h` (once implemented)

## Notes

**Depth Testing:** Usually want debug draws on top of everything. Disable depth test or draw last with depth test but no depth write.

**Thread Safety:** If debug drawing from multiple threads, need to synchronize access to static vectors.

**Release Builds:** Compile out entirely with `#ifdef DEVELOPMENT_BUILD` to ensure zero cost in shipping game.

**No Keyboard Toggle:** F3 is reserved for UI hover inspection. Debug rendering has no dedicated toggle - developer controls it in code.
