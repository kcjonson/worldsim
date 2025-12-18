# C++ Coding Standards

Created: 2025-10-12
Last Updated: 2025-11-30
Status: Active

## Philosophy

**Build it ourselves.** For core systems, we implement our own solutions rather than pulling in external libraries. This gives us:
- Full control over behavior and performance
- Deep understanding of our systems
- No external dependencies for critical code
- Learning and growth opportunities

Use external libraries only for:
- Platform/OS interfaces (glfw, OpenGL)
- Standard formats (JSON, image loading)
- Complex external systems we don't want to maintain

## Linting and Formatting

### Tools

**clang-format**: Automatic code formatting
- Configuration: `.clang-format` in project root
- Run manually: `Shift+Alt+F` (VSCode) or `clang-format -i <file>`
- NOT automatic on save (manual control to learn formatting rules)

**clang-tidy**: Static analysis and linting
- Configuration: `.clang-tidy` in project root
- Runs automatically in VSCode via clangd
- Checks for bugs, performance issues, style violations
- Enforces naming conventions

### Usage

**Format code manually:**
- VSCode: `Shift+Alt+F` or right-click > "Format Document"
- Command line: `clang-format -i file.cpp file.h`

**View clang-tidy warnings:**
- Shows inline in VSCode (yellow/red squiggles)
- Hover over warnings to see details and suggested fixes

**Fix clang-tidy issues:**
- Many have automatic fixes (click lightbulb icon)
- Others require manual correction

### Naming Enforcement

clang-tidy enforces our naming conventions:
- Classes/Structs/Enums: PascalCase → `Shader`, `TextureManager`
- Functions/Methods: camelCase → `loadTexture()`, `isValid()`
- Variables: camelCase → `frameCount`, `deltaTime`
- Members: camelCase (no prefix) → `shader`, `isInitialized`
- Constants: k prefix → `kMaxTextures`, `kDefaultFOV`

Violations show as warnings during development.

### Disabled Checks

Some clang-tidy checks are disabled in `.clang-tidy` due to false positives or conflicts with our coding style:

**`cppcoreguidelines-init-variables`** - DISABLED (2025-11-02)
- **Reason**: Produces false positives for variables initialized on the same line via function calls
- **Example false positive**: `const float value = someFunction();` flagged as uninitialized
- **Bug prevention**: While this check prevents used-before-set bugs, it incorrectly flags correct code
- **References**: [LLVM Issue #58755](https://github.com/llvm/llvm-project/issues/58755)
- **Alternative**: Rely on compiler warnings and code review for actual uninitialized variables

## File Organization

### Headers and Implementation Side-by-Side

Headers (.h) and implementation (.cpp) files live together in the same directory:

```
libs/renderer/gl/
├── shader.h
├── shader.cpp
├── texture.h
└── texture.cpp
```

**NOT** separated into `include/` and `src/` directories.

### Header Protection

Use `#pragma once` at the top of every header file:

```cpp
#pragma once

class Shader {
    // ...
};
```

**Don't use** traditional header guards (`#ifndef`/`#define`/`#endif`).

### One Class Per File

Each class gets its own `.h` and `.cpp` file pair named after the class:
- `Shader` → `shader.h` + `shader.cpp`
- `TextureManager` → `texture_manager.h` + `texture_manager.cpp`

### Include Order

Organize includes in this order (with blank lines between groups):

```cpp
// 1. Corresponding header (in .cpp files only)
#include "shader.h"

// 2. Project headers (same library)
#include "texture.h"
#include "gl_wrapper.h"

// 3. Project headers (other libraries)
#include <renderer/renderer2d.h>
#include <foundation/math/vector.h>

// 4. Third-party libraries
#include <glm/glm.hpp>

// 5. Standard library
#include <string>
#include <vector>
```

## Naming Conventions

### Classes and Structs

**PascalCase** - capitalize first letter of each word:

```cpp
class Shader { };
class TextureManager { };
struct WorldData { };
```

### Functions and Methods

**camelCase** for all functions and methods:

```cpp
void loadShader();
void updateWorldState();
bool isValid() const;
```

### Variables

**camelCase** - lowercase first letter, capitalize subsequent words:

```cpp
int frameCount;
float deltaTime;
Shader* currentShader;
```

### Member Variables

**camelCase** with no prefix:

```cpp
class Renderer {
private:
    Shader* shader;
    int frameCount;
    bool isInitialized;
};
```

⚠️ **Common Bug: Parameter Shadowing**

When a function parameter has the same name as a member variable, the parameter shadows the member. This leads to subtle bugs where assignments become self-assignments:

```cpp
// BUG: Parameter 'text' shadows member 'text'
void setText(const std::string& text) {
    text = text;  // Self-assignment! Member is never set
}

// CORRECT: Rename parameter to avoid shadowing
void setText(const std::string& newText) {
    text = newText;  // Now assigns to member correctly
}
```

**Where this commonly occurs:**
- Setters: `setFoo(Type foo)` → `setFoo(Type newFoo)`
- Initializers: `Initialize(Window* window)` → `Initialize(Window* newWindow)`
- Methods taking arrays: `AddTriangles(Vec2* vertices, ...)` → `AddTriangles(Vec2* inputVertices, ...)`

**Compiler won't warn you:** Self-assignment is valid C++, so there's no compiler error. The bug only manifests at runtime when the member retains its old value.

**Prevention strategies:**
1. Always use prefixes (`new`, `input`, `initial`) for parameters that could shadow members
2. In constructors, the initializer list syntax avoids this: `: position(position)` works correctly
3. Code review: Watch for parameter names that match member names

### Member Initialization

**Prefer in-class initializers** for member variables with default values:

```cpp
class Application {
private:
    GLFWwindow* window{nullptr};     // Prefer in-class init
    bool isRunning{false};           // Makes default explicit
    float deltaTime{0.0F};           // Visible at declaration

    // std::function and other default-constructible types
    std::function<void()> callback{};  // Initializes to empty/null
    std::string name{};                // Initializes to empty string
    std::vector<int> data{};           // Initializes to empty vector
};
```

**Benefits:**
- **Single source of truth**: Default values visible where variables are declared
- **Prevents bugs**: All constructors get correct defaults even if you forget
- **Satisfies clang-tidy**: Prevents `cppcoreguidelines-pro-type-member-init` warnings
- **Modern C++ best practice**: Recommended by C++ Core Guidelines (C.48, C.45)

**When to override in constructor:**

Use constructor initializer lists when a value depends on constructor parameters:

```cpp
Application::Application(GLFWwindow* newWindow)
    : window(newWindow) {  // Override: parameter-dependent
    // isRunning, deltaTime, etc. use in-class defaults
}
```

**Trade-offs:**
- ✅ **Pro**: Makes defaults explicit and prevents accidental uninitialized members
- ✅ **Pro**: Reduces constructor boilerplate for common default values
- ⚠️ **Con**: Changing default requires header recompilation (vs .cpp only)

### Constants

**PascalCase** with `k` prefix:

```cpp
const int kMaxTextures = 16;
const float kDefaultFOV = 60.0f;
constexpr size_t kBufferSize = 1024;
```

### Namespaces

**lowercase** or **snake_case**:

```cpp
namespace renderer { }
namespace world_gen { }
```

### Enums

Enum types use **PascalCase**, values use **PascalCase**:

```cpp
enum class ShaderType {
    Vertex,
    Fragment,
    Geometry
};

// Usage
ShaderType type = ShaderType::Vertex;
```

### File Names

**PascalCase** - match the class/module name exactly:
- `Shader.h` / `Shader.cpp`
- `TextureManager.h` / `TextureManager.cpp`
- `StringUtils.h` (utility files also use PascalCase)

This provides direct mapping: class `TextureManager` → file `TextureManager.h`

### Folder Names

Folder naming depends on the folder's purpose:

**PascalCase** - when folder contains a single primary class of the same name:
```
libs/renderer/CoordinateSystem/
├── CoordinateSystem.h
├── CoordinateSystem.cpp
└── CoordinateSystem.test.cpp
```

**camelCase** - when folder is a category containing multiple related items:
```
libs/renderer/shader/
├── Shader.h
├── Shader.cpp
├── ShaderLoader.h
└── ShaderLoader.cpp

libs/ui/components/
├── Button.h
├── TextInput.h
└── ...
```

**Rule of thumb:** If renaming the folder would require renaming a class, use PascalCase. If the folder groups related but distinct items, use camelCase.

## Function Arguments and API Design

### Designated Initializers for Multiple Parameters

For functions/methods with more than 2-3 parameters, use **C++20 designated initializers** with an Args struct.

**Pattern:**

```cpp
struct FunctionNameArgs {
    // Required parameters first
    Type requiredParam;

    // Optional parameters with defaults
    Type optionalParam = defaultValue;

    // For inspection/debugging (optional)
    const char* id = nullptr;
};

void functionName(const FunctionNameArgs& args);
```

**Usage:**

```cpp
functionName({
    .requiredParam = value,
    .optionalParam = value,  // Can omit if using default
    .id = "debug_name"
});
```

**Real example from primitives API:**

```cpp
struct RectArgs {
    Rect bounds;
    RectStyle style;
    const char* id = nullptr;
};

void drawRect(const RectArgs& args);

// Usage
drawRect({
    .bounds = {50, 50, 200, 100},
    .style = {
        .fill = Color::red(),
        .border = {.color = Color::yellow(), .width = 3.0f}
    },
    .id = "test_rect"
});
```

**Benefits:**

- **Clarity** - Obvious what each argument means (no positional confusion)
- **Extensibility** - Easy to add new optional parameters without breaking existing code
- **Self-documenting** - API reads like configuration
- **Debuggable** - Optional `.id` field enables inspection tools

**Rules:**

1. **Field order matters** - Fields must appear in same order as struct definition (C++20 requirement)
2. **Naming convention** - Use `FunctionNameArgs` or `ClassNameArgs` for argument structs
3. **Placement** - Define Args struct near the function that uses it (same header file)
4. **Defaults** - Mark optional parameters with `= defaultValue`
5. **Required vs optional** - Put required parameters first, optional ones last

**When to use:**

✅ Functions with 3+ parameters
✅ Functions where parameter meaning isn't obvious
✅ Public APIs that may evolve over time
✅ Any API that benefits from named parameters

**When NOT to use:**

❌ Simple functions with 1-2 obvious parameters (e.g., `Add(int a, int b)`)
❌ Performance-critical inner loops (though compiler usually optimizes it away)
❌ Standard callbacks/lambdas with fixed signatures

**Comparison to other languages:**

This pattern is similar to:
- **JavaScript**: Object destructuring in function parameters
- **Python**: Keyword arguments
- **Kotlin**: Named parameters
- **Swift**: Named parameters

C++'s version requires parameters in declaration order but provides compile-time type safety.

**Industry adoption:**

- **Google (Abseil)**: Official recommendation (Tip of the Week #172)
- **Chromium**: Approved for C++20 codebases
- **C++ Standard**: Active proposals to expand the feature (P2287R3, P3405R0)

## Memory Management

### Prefer Stack Allocation

Allocate on the stack when possible:

```cpp
// Good
Shader shader;
std::array<Vertex, 4> vertices;

// Avoid
Shader* shader = new Shader();  // Don't do this
```

### Smart Pointers Over Raw Pointers

Use smart pointers when heap allocation is necessary:

```cpp
// Unique ownership
std::unique_ptr<Texture> texture = std::make_unique<Texture>();

// Shared ownership (use sparingly)
std::shared_ptr<Mesh> mesh = std::make_shared<Mesh>();
```

### RAII (Resource Acquisition Is Initialization)

Resources are acquired in constructors, released in destructors:

```cpp
class Texture {
public:
    Texture() {
        glGenTextures(1, &id);
    }

    ~Texture() {
        glDeleteTextures(1, &id);
    }

    // Delete copy, implement move
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
    Texture(Texture&&) noexcept = default;
    Texture& operator=(Texture&&) noexcept = default;

private:
    GLuint id;
};
```

### Object Pooling for Hot Paths

For frequently created/destroyed objects, use object pools:

```cpp
class TilePool {
    std::vector<Tile> pool;
    std::vector<Tile*> available;

public:
    Tile* acquire();
    void release(Tile* tile);
};
```

**Use pooling for:**
- Tiles
- Chunk data
- Frequently spawned game entities
- Particles (if/when we have them)

**Don't pool:**
- UI components
- One-time initialization objects
- Resources loaded once

### Avoid new/delete in Game Code

Use object pools, stack allocation, or smart pointers instead:

```cpp
// Bad
Tile* tile = new Tile();
delete tile;

// Good
auto tile = tilePool.Acquire();
tilePool.Release(tile);
```

## Entity Component System (ECS)

### Custom Implementation

We implement our own ECS system in `libs/engine/ecs/`. Key concepts:

**Entities** are lightweight IDs:
```cpp
using EntityID = uint64_t;
```

**Components** are pure data (POD structs):
```cpp
struct Position {
    float x, y, z;
};

struct Velocity {
    float dx, dy, dz;
};

struct Renderable {
    MeshID mesh;
    MaterialID material;
};
```

**Systems** process entities with specific component combinations:
```cpp
class PhysicsSystem {
public:
    void Update(float deltaTime) {
        // Process all entities with Position + Velocity
        for (auto [entity, pos, vel] : GetEntities<Position, Velocity>()) {
            pos.x += vel.dx * deltaTime;
            pos.y += vel.dy * deltaTime;
            pos.z += vel.dz * deltaTime;
        }
    }
};
```

### What Uses ECS

**YES - Use ECS for:**
- Game world tiles
- Chunks and chunk metadata
- Spawned game entities/objects
- Anything that needs data-oriented performance

**NO - Don't use ECS for:**
- UI components (use traditional OO)
- One-off manager classes
- Rendering systems themselves (they consume ECS data)

### System Update Order

Systems update in fixed order each frame:
1. Input systems
2. AI/logic systems
3. Physics systems
4. Animation systems (if/when we have them)
5. Rendering systems (query data, don't modify)

## Modern C++20 Features

### Use These Features

**`constexpr` for compile-time computation:**
```cpp
constexpr int Square(int x) { return x * x; }
constexpr int kTileSize = Square(16);
```

**`std::span` for array views:**
```cpp
void processVertices(std::span<const Vertex> vertices);
```

**Structured bindings for clarity:**
```cpp
auto [x, y, z] = GetPosition();
for (const auto& [key, value] : map) { }
```

**Range-based for loops:**
```cpp
for (const auto& entity : entities) { }
```

**`auto` for obvious types:**
```cpp
auto texture = std::make_unique<Texture>();  // Clear from context
```

**Concepts for template constraints** (when templates are needed):
```cpp
template<typename T>
concept Numeric = std::is_arithmetic_v<T>;

template<Numeric T>
T Add(T a, T b) { return a + b; }
```

### Use Sparingly or Avoid

**Templates**: Only when necessary for performance or type safety
**Virtual functions**: Avoid in hot paths (use data-driven design)
**Exceptions**: TBD - decide project-wide policy
**Multiple inheritance**: Avoid except for interfaces

## Performance Patterns

### Pass by const Reference

For non-trivial types, pass by const reference:

```cpp
void processMesh(const Mesh& mesh);  // Good
void processMesh(Mesh mesh);         // Bad - copies entire mesh
```

### Use const Liberally

Mark everything const that doesn't modify:

```cpp
class Transform {
public:
    glm::mat4 getMatrix() const;  // Doesn't modify
    bool isIdentity() const;

    void setPosition(const glm::vec3& pos);  // Modifies
};
```

### Reserve Vector Capacity

When size is known upfront:

```cpp
std::vector<Vertex> vertices;
vertices.reserve(expectedCount);  // Avoid reallocation
```

### Cache-Friendly Data Structures

**Array of Structs (AoS)** - default, good for general use:
```cpp
struct Particle {
    glm::vec3 position;
    glm::vec3 velocity;
    float lifetime;
};
std::vector<Particle> particles;
```

**Struct of Arrays (SoA)** - better for processing one field:
```cpp
struct ParticleSystem {
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> velocities;
    std::vector<float> lifetimes;
};
```

Use SoA when processing large arrays of data where you only need some fields.

## Code Organization

### Forward Declarations

Use forward declarations to reduce compile times:

```cpp
// shader.h
#pragma once

class Texture;  // Forward declaration

class Shader {
    void bindTexture(const Texture* texture);
};
```

Only include full header in `.cpp` file:
```cpp
// shader.cpp
#include "shader.h"
#include "texture.h"  // Full include here
```

### Avoid Circular Dependencies

If A needs B and B needs A, you have a design problem. Solutions:
1. Extract common interface to third class
2. Use forward declarations and pointers
3. Reconsider the design

### Keep Files Focused

Each file should have a single, clear purpose. If a file is growing large (>500 lines), consider splitting it.

## Error Handling

### Assertions

Use assertions liberally in debug builds:

```cpp
void setShader(Shader* newShader) {
    assert(newShader != nullptr && "Shader cannot be null");
    assert(newShader->isCompiled() && "Shader must be compiled");
    shader = newShader;
}
```

### Logging

Log at appropriate levels:
- **Error**: Something broke, needs attention
- **Warning**: Something unexpected, but we can continue
- **Info**: Useful runtime information
- **Debug**: Detailed information for debugging

```cpp
Log::Error("Failed to load texture: {}", filename);
Log::Warning("Texture format not optimal, converting...");
Log::Info("Initialized renderer: {} x {}", width, height);
Log::Debug("Allocated {} bytes for vertex buffer", size);
```

### Return Values vs Exceptions

**TBD** - Project needs to decide:
- Option 1: Use exceptions for errors
- Option 2: Use return values (bool, std::optional, error codes)
- Option 3: Mix (exceptions for setup, return values in hot paths)

Document decision here once made.

## Comments and Documentation

### When to Comment

**DO comment:**
- Why code does something (not what it does)
- Non-obvious algorithms
- Performance considerations
- Temporary implementations (with TODO)
- Public API documentation

**DON'T comment:**
- Obvious code
- What code does (code should be self-documenting)

### Examples

```cpp
// Bad - obvious
// Increment frame count
frameCount++;

// Good - explains why
// Skip first frame to avoid initialization hiccups
if (frameCount > 0) {
    updatePhysics(deltaTime);
}

// Good - documents temporary implementation
// TODO: Replace with proper terrain generation algorithm
// See docs/technical/world-generation-architecture.md
float generateTerrain(float x, float y) {
    return perlinNoise(x, y);  // TEMPORARY
}
```

### TODO Comments

Format: `// TODO: Description`

Link to documentation if relevant:
```cpp
// TODO: Implement frustum culling for better performance
// See docs/technical/renderer-architecture.md for design
```

## Code Cleanliness: Aggressive Deletion

**Principle: Dead code is a liability. Delete it immediately.**

### No Legacy Fallbacks

When replacing functionality, delete the old code in the same commit:

```cpp
// ❌ WRONG: Keeping both paths
void renderNew() { /* new implementation */ }
void renderOld() { /* kept "just in case" */ }  // DELETE THIS

// ✅ RIGHT: One implementation
void render() { /* the only implementation */ }
```

### No Feature Flags for Internal Changes

Feature flags add complexity. For internal refactoring, just change the code:

```cpp
// ❌ WRONG: Internal feature flag
if (gUseNewRenderer) {
    newRender();
} else {
    oldRender();
}

// ✅ RIGHT: Direct implementation
void render() {
    // Modern implementation, no alternatives
}
```

### Delete Unused Code Immediately

When you encounter dead code while working in a file, delete it:

```cpp
// ❌ WRONG: Commenting out instead of deleting
// void unusedFunction() { ... }

// ❌ WRONG: Marking as unused instead of removing
void process([[maybe_unused]] int oldParam) { }

// ✅ RIGHT: Just delete it
void process() { }
```

### Refactoring Workflow

1. **Read** and understand the old implementation
2. **Write** the new implementation
3. **Update** all call sites
4. **Delete** the old code (same commit)
5. **Test** and fix any issues in the new code

Never keep both versions "temporarily"—this becomes permanent debt.

### What to Delete

When touching a file, look for and remove:
- Unused `#include` directives
- Unused function declarations and definitions
- Commented-out code blocks
- Unused member variables
- Unused parameters (remove from signature, not just rename to `_unused`)
- Dead branches in conditionals
- Deprecated type aliases

### What NOT to Delete

- Code that's used but you don't recognize (search first)
- Public API that external code might depend on (check usages)
- Debug/logging code that's intentionally conditional

## Related Documentation

- [Monorepo Structure](./monorepo-structure.md) - Library organization
- [Development Standards](../CLAUDE.md#development-standards) - Workflow requirements
