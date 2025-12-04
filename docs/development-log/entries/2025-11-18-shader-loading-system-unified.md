# Shader Loading System Unified

**Date:** 2025-11-18

**Summary:**
Consolidated two separate shader loading systems into a single unified RAII-based approach. Moved all shaders to centralized location (`libs/renderer/shaders/`) and updated both FontRenderer and BatchRenderer to use the same `Renderer::Shader` class. Eliminated ~120 lines of duplicate shader compilation code and inline shader strings from BatchRenderer.

**Files Created:**
- `libs/renderer/shaders/primitive.vert` - Primitive vertex shader (extracted from inline code)
- `libs/renderer/shaders/primitive.frag` - Primitive fragment shader (extracted from inline code)

**Files Moved:**
- `/shaders/text.vert` → `libs/renderer/shaders/text.vert` - Text vertex shader
- `/shaders/text.frag` → `libs/renderer/shaders/text.frag` - Text fragment shader

**Files Modified:**
- `libs/renderer/CMakeLists.txt` - Added POST_BUILD command to copy shaders to build directory
- `apps/ui-sandbox/CMakeLists.txt` - Added POST_BUILD command to copy shaders to executable directory
- `libs/renderer/primitives/batch_renderer.h` - Changed from `GLuint m_shader` to `Shader m_shader`, removed CompileShader() declaration
- `libs/renderer/primitives/batch_renderer.cpp` - Removed ~70 lines of inline shader source code, removed ~50 lines in CompileShader() method, updated Init/Shutdown/Flush to use Shader class API

**Key Implementation Details:**

**1. RAII Pattern Benefits**
The existing `Renderer::Shader` class uses RAII (Resource Acquisition Is Initialization) for automatic resource cleanup:
```cpp
// Before (BatchRenderer): Manual cleanup required
GLuint m_shader;
void Init() {
    m_shader = CompileShader();  // ~50 lines of manual GL calls
}
void Shutdown() {
    glDeleteProgram(m_shader);  // Easy to forget!
}

// After: Automatic cleanup via RAII
Shader m_shader;
void Init() {
    m_shader.LoadFromFile("primitive.vert", "primitive.frag");
}
void Shutdown() {
    // Shader destructor automatically calls glDeleteProgram
}
```

**2. Centralized Shader Storage**
All shaders now live in one location with automated copying:
```cmake
# libs/renderer/CMakeLists.txt - copies to build/shaders/
add_custom_command(TARGET renderer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders
    ${CMAKE_BINARY_DIR}/shaders
)

# apps/ui-sandbox/CMakeLists.txt - copies to executable directory
add_custom_command(TARGET ui-sandbox POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_BINARY_DIR}/shaders
    $<TARGET_FILE_DIR:ui-sandbox>/shaders
)
```

**3. Shader Loading API**
Both FontRenderer and BatchRenderer now use identical loading pattern:
```cpp
// FontRenderer (existing, unchanged)
m_shader.LoadFromFile("text.vert", "text.frag");

// BatchRenderer (newly updated)
m_shader.LoadFromFile("primitive.vert", "primitive.frag");
```

**Architecture Decisions:**

**Why RAII Over Manual Cleanup:**
- Exception safety: Automatic cleanup even when exceptions occur
- Resource leak prevention: Impossible to forget cleanup
- Clear ownership semantics: Shader class owns the GPU resource
- Move semantics support: Compatible with future resource manager pattern
- No performance overhead: Zero-cost abstraction

**Why Centralized Shader Location:**
- Single source of truth for all GLSL code
- Easier to find and modify shaders
- Consistent build process across all executables
- Simpler relative path resolution (always `shaders/filename`)

**Future Compatibility:**
This RAII pattern is compatible with centralized resource management for splash screen loading:
```cpp
class ResourceManager {
    std::unordered_map<std::string, Shader> m_shaders;

    void LoadDuringSplashScreen() {
        Shader primitiveShader;
        primitiveShader.LoadFromFile("primitive.vert", "primitive.frag");
        m_shaders["primitive"] = std::move(primitiveShader);  // Transfer ownership
        UpdateProgress(33);
    }
};
```

**Testing:**
- Verified primitive rendering (shapes scene) works correctly
- Verified text rendering (font_test scene) works correctly
- Both systems load shaders from centralized location successfully

**Lessons Learned:**
- Shader loading requires proper CMake commands to copy files to executable directory, not just build directory
- RAII eliminates entire classes of bugs (resource leaks, double-frees)
- Consolidating duplicate code early prevents divergence and maintenance burden

**Next Steps:**
None required - shader loading is now unified across the project.



