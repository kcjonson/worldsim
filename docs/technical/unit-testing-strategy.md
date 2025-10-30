# Unit Testing Strategy

**Date:** 2025-10-30
**Status:** Proposed
**Framework:** Google Test + Google Benchmark
**Research:** See `/docs/research/cpp-test-framework-research.md` for detailed analysis

## Overview

This document defines worldsim's unit testing strategy, chosen frameworks, and implementation plan for integrating automated functional and performance testing into the development workflow and CI/CD pipeline.

## Decision: Google Test + Google Benchmark

**Frameworks:**
- **Google Test** (gtest + gmock) - Functional testing and mocking
- **Google Benchmark** - Performance testing and benchmarking

**Rationale:** Industry standard for game engine development with comprehensive performance testing capabilities. See research doc for full analysis comparing Google Test, Catch2, and Doctest.

**Key Advantages:**
- Best-in-class performance testing (critical for game engine)
- Used by Unreal Engine, Chromium, LLVM, TensorFlow
- Comprehensive feature set (testing + mocking + benchmarking)
- Excellent CI/CD integration
- Large community and documentation

**Trade-off:** Slower compile times (2-5s vs 0.5-1.5s for Doctest), but acceptable since we're not doing TDD.

## Why This Matters for Game Engines

Game engines have unique testing requirements beyond typical application software:

### Performance is as Important as Functionality
- Memory arena allocators must be fast (10,000+ allocations/frame)
- Tessellation performance determines how many SVG shapes we can render
- Batch rendering throughput affects frame rate
- Performance regressions are bugs, just like functional bugs

### What We Need to Test
**Functional Tests:**
- Arena allocator correctness (allocates/resets properly)
- Resource handle validation (detects stale handles)
- String hashing collision detection
- Scene lifecycle and state management

**Performance Tests (Benchmarks):**
- Arena vs malloc speed comparison (should be 10-15× faster)
- Tessellation performance (triangles/second)
- Batch rendering throughput (draws/frame)
- Asset loading times

**Google Benchmark provides detailed metrics:**
- CPU time and wall time
- Memory usage tracking
- Statistical analysis (mean, stddev)
- Iterations and throughput
- Comparative benchmarks

---

## Understanding the Testing Stack

###CTest's Role (Important!)

**CTest is NOT a test framework** - it's the test orchestrator/runner.

```
Test Framework (Google Test)
  ↓ writes test results
CTest (runner)
  ↓ aggregates and formats
Output Reports (JUnit XML, console)
  ↓ consumed by
CI System (GitHub Actions)
```

**What CTest does:**
- Discovers test executables via `add_test()` in CMakeLists.txt
- Runs them (can parallelize with `-j`)
- Collects exit codes (0=pass, non-zero=fail)
- Generates reports in multiple formats
- Aggregates results across all test executables

**Analogy:** CTest is like `npm test` or `yarn test` - it runs test executables but doesn't provide assertions or test organization.

### Output Formats

**Console Output** (human-readable):
```
[==========] Running 3 tests from 1 test suite.
[ RUN      ] ArenaTests.BasicAllocation
[       OK ] ArenaTests.BasicAllocation (0 ms)
[ RUN      ] ArenaTests.Reset
[       OK ] ArenaTests.Reset (1 ms)
[==========] 3 tests ran. (2 ms total)
[  PASSED  ] 3 tests.
```

**JUnit XML** (machine-readable, for GitHub Actions):
```xml
<testsuites tests="3" failures="0" time="0.002">
  <testsuite name="ArenaTests" tests="3">
    <testcase name="BasicAllocation" time="0.000"/>
    <testcase name="Reset" time="0.001"/>
  </testsuite>
</testsuites>
```

### Metadata Beyond Pass/Fail

| Metadata | Available? | Notes |
|----------|------------|-------|
| Test duration | ✅ Automatic | All frameworks track per-test time |
| Performance metrics | ✅ Google Benchmark | CPU time, wall time, memory, iterations |
| Code coverage | ⚠️ Separate tool | Use gcov/llvm-cov (not part of frameworks) |
| Custom data | ✅ Via reporters | Can extend output format |

**GitHub Actions consumes JUnit XML and shows:**
- Pass/fail/skip counts in PR checks
- Test duration per test
- Failure messages and details
- Can auto-fail PR on test failures

---

## Architecture

### Test Organization

```
worldsim/
├── libs/
│   ├── foundation/
│   │   ├── tests/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── unit/                    # Functional tests
│   │   │   │   ├── arena_tests.cpp
│   │   │   │   ├── logging_tests.cpp
│   │   │   │   ├── hash_tests.cpp
│   │   │   │   └── handle_tests.cpp
│   │   │   └── benchmarks/              # Performance tests
│   │   │       ├── arena_benchmarks.cpp
│   │   │       └── hash_benchmarks.cpp
│   │   ├── include/
│   │   └── src/
│   ├── engine/
│   │   ├── tests/
│   │   │   ├── CMakeLists.txt
│   │   │   ├── unit/
│   │   │   │   ├── application_tests.cpp
│   │   │   │   ├── scene_tests.cpp
│   │   │   │   └── ecs_tests.cpp
│   │   │   └── benchmarks/
│   │   │       └── ecs_benchmarks.cpp
│   │   ├── include/
│   │   └── src/
│   └── [other libraries...]
```

**Why separate unit/ and benchmarks/?**
- Unit tests compile with `-O0 -g` (debugging enabled)
- Benchmarks compile with `-O3` (optimizations enabled)
- Different binaries, different purposes

### Test Naming Convention

- **Test directories:** `unit/` and `benchmarks/`
- **Test files:** `{component}_tests.cpp` or `{component}_benchmarks.cpp`
- **Test executables:** `{library}-tests` and `{library}-benchmarks`
- **Test cases:** Descriptive sentences (e.g., "Arena allocates memory correctly")

---

## Implementation Plan

### Phase 1: Infrastructure Setup

#### 1.1 Add Dependencies

**File:** `/vcpkg.json`

```json
{
  "dependencies": [
    "gtest",           // Includes gmock for mocking
    "benchmark",       // Google Benchmark for performance testing
    "glfw3",
    "glew",
    ...
  ]
}
```

**Action:** Add `"gtest"` and `"benchmark"` to dependencies array

#### 1.2 Create Test Directories

For each library in `libs/`:
1. Create `tests/` subdirectory
2. Create `tests/unit/` for functional tests
3. Create `tests/benchmarks/` for performance tests
4. Create `tests/CMakeLists.txt`

**Libraries to set up:**
- `libs/foundation/tests/`
- `libs/engine/tests/`
- `libs/renderer/tests/`
- `libs/ui/tests/`
- `libs/world/tests/`
- `libs/game-systems/tests/`

#### 1.3 CMake Configuration

**Template:** `libs/{library}/tests/CMakeLists.txt`

```cmake
# Unit tests (functional testing)
find_package(GTest CONFIG REQUIRED)

add_executable({library}-tests
    unit/arena_tests.cpp
    unit/logging_tests.cpp
    unit/hash_tests.cpp
    # ... other unit test files
)

target_link_libraries({library}-tests
    PRIVATE
    {library}
    GTest::gtest
    GTest::gtest_main    # Provides main() function
)

# Compile with debug symbols
target_compile_options({library}-tests PRIVATE -O0 -g)

# Register with CTest
add_test(NAME {library}-tests COMMAND {library}-tests)

# Generate JUnit XML for CI
add_test(NAME {library}-tests-junit
    COMMAND {library}-tests --gtest_output=xml:${CMAKE_BINARY_DIR}/test-results/{library}-tests.xml
)

# Benchmarks (performance testing)
find_package(benchmark CONFIG REQUIRED)

add_executable({library}-benchmarks
    benchmarks/arena_benchmarks.cpp
    benchmarks/hash_benchmarks.cpp
    # ... other benchmark files
)

target_link_libraries({library}-benchmarks
    PRIVATE
    {library}
    benchmark::benchmark
    benchmark::benchmark_main
)

# Compile with optimizations
target_compile_options({library}-benchmarks PRIVATE -O3)

# Register with CTest
add_test(NAME {library}-benchmarks
    COMMAND {library}-benchmarks
        --benchmark_format=console
        --benchmark_out=${CMAKE_BINARY_DIR}/benchmark-results/{library}-benchmarks.json
        --benchmark_out_format=json
)
```

**Note:** Replace `{library}` with actual library name (e.g., `foundation`, `engine`, etc.)

---

### Phase 2: Write Initial Tests

#### 2.1 Foundation Library - Functional Tests

**Priority tests:**
- Memory arena (Arena, FrameArena, ScopedArena)
- Logging system (Logger, log levels, categories)
- Resource handles (ResourceHandle, ResourceManager)
- String hashing (FNV-1a, collision detection)

**Example:** `libs/foundation/tests/unit/arena_tests.cpp`

```cpp
#include <gtest/gtest.h>
#include "foundation/memory/arena.h"

TEST(ArenaTests, BasicAllocation) {
    Arena arena(1024);
    void* ptr = arena.Allocate(128);

    EXPECT_NE(ptr, nullptr);
    EXPECT_EQ(arena.GetUsed(), 128);
}

TEST(ArenaTests, MultipleAllocations) {
    Arena arena(1024);

    void* ptr1 = arena.Allocate(64);
    void* ptr2 = arena.Allocate(64);

    EXPECT_NE(ptr1, nullptr);
    EXPECT_NE(ptr2, nullptr);
    EXPECT_NE(ptr1, ptr2);
    EXPECT_EQ(arena.GetUsed(), 128);
}

TEST(ArenaTests, Reset) {
    Arena arena(1024);
    arena.Allocate(256);
    arena.Reset();

    EXPECT_EQ(arena.GetUsed(), 0);
}

TEST(ArenaTests, Alignment) {
    Arena arena(1024);
    void* ptr = arena.Allocate(1);

    // Default alignment should be 8 bytes
    EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr) % 8, 0);
}

TEST(ArenaTests, CustomAlignment) {
    Arena arena(1024);
    void* ptr = arena.AllocateAligned(1, 16);

    EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr) % 16, 0);
}

TEST(ArenaTests, Exhaustion) {
    Arena arena(128);

    // Allocate entire arena
    void* ptr1 = arena.Allocate(128);
    EXPECT_NE(ptr1, nullptr);

    // Next allocation should fail
    void* ptr2 = arena.Allocate(1);
    EXPECT_EQ(ptr2, nullptr);
}
```

#### 2.2 Foundation Library - Performance Tests

**Example:** `libs/foundation/tests/benchmarks/arena_benchmarks.cpp`

```cpp
#include <benchmark/benchmark.h>
#include "foundation/memory/arena.h"
#include <cstdlib>

// Benchmark arena allocation
static void BM_ArenaAllocation(benchmark::State& state) {
    Arena arena(1024 * 1024); // 1MB arena

    for (auto _ : state) {
        void* ptr = arena.Allocate(128);
        benchmark::DoNotOptimize(ptr);  // Prevent optimization
    }

    state.SetItemsProcessed(state.iterations());
    state.SetBytesProcessed(state.iterations() * 128);
}
BENCHMARK(BM_ArenaAllocation);

// Benchmark malloc for comparison
static void BM_MallocAllocation(benchmark::State& state) {
    for (auto _ : state) {
        void* ptr = malloc(128);
        benchmark::DoNotOptimize(ptr);
        free(ptr);
    }

    state.SetItemsProcessed(state.iterations());
    state.SetBytesProcessed(state.iterations() * 128);
}
BENCHMARK(BM_MallocAllocation);

// Comparative benchmark with different allocation sizes
static void BM_ArenaVsMalloc(benchmark::State& state) {
    Arena arena(1024 * 1024);
    bool useArena = state.range(0);
    size_t size = state.range(1);

    for (auto _ : state) {
        if (useArena) {
            void* ptr = arena.Allocate(size);
            benchmark::DoNotOptimize(ptr);
        } else {
            void* ptr = malloc(size);
            benchmark::DoNotOptimize(ptr);
            free(ptr);
        }
    }

    state.SetLabel(useArena ? "Arena" : "Malloc");
}
BENCHMARK(BM_ArenaVsMalloc)
    ->Args({0, 64})    // malloc, 64 bytes
    ->Args({1, 64})    // arena, 64 bytes
    ->Args({0, 128})   // malloc, 128 bytes
    ->Args({1, 128})   // arena, 128 bytes
    ->Args({0, 256})   // malloc, 256 bytes
    ->Args({1, 256});  // arena, 256 bytes

BENCHMARK_MAIN();
```

**Expected output:**
```
---------------------------------------------------------------
Benchmark                     Time             CPU   Iterations
---------------------------------------------------------------
BM_ArenaAllocation         12.5 ns         12.5 ns     56000000
BM_MallocAllocation         145 ns          145 ns      4820000
BM_ArenaVsMalloc/0/64       142 ns          142 ns      4930000   Malloc
BM_ArenaVsMalloc/1/64      12.3 ns         12.3 ns     56900000   Arena
BM_ArenaVsMalloc/0/128      145 ns          145 ns      4820000   Malloc
BM_ArenaVsMalloc/1/128     12.5 ns         12.5 ns     56000000   Arena
```

**This shows arena is ~11-12× faster than malloc** - critical performance data.

#### 2.3 Engine Library - Functional Tests

**Priority tests:**
- Application lifecycle (with mocked GLFW)
- Scene management (creation, switching, destruction)
- ECS basics (entity creation, component storage)

**Mocking Strategy:**
- GLFW: Create `MockWindow` interface
- OpenGL: Use function pointers for testing
- File I/O: Inject file system interface

**Example:** `libs/engine/tests/unit/scene_tests.cpp`

```cpp
#include <gtest/gtest.h>
#include "engine/scene/scene.h"

class TestScene : public IScene {
public:
    bool initCalled = false;
    bool updateCalled = false;
    bool renderCalled = false;
    float lastDeltaTime = 0.0f;

    void Init() override {
        initCalled = true;
    }

    void HandleInput(GLFWwindow* window) override {
        // No-op for test
    }

    void Update(float deltaTime) override {
        updateCalled = true;
        lastDeltaTime = deltaTime;
    }

    void Render() override {
        renderCalled = true;
    }
};

TEST(SceneTests, Lifecycle) {
    TestScene scene;

    scene.Init();
    EXPECT_TRUE(scene.initCalled);

    scene.Update(0.016f);
    EXPECT_TRUE(scene.updateCalled);
    EXPECT_FLOAT_EQ(scene.lastDeltaTime, 0.016f);

    scene.Render();
    EXPECT_TRUE(scene.renderCalled);
}

TEST(SceneTests, MultipleUpdates) {
    TestScene scene;
    scene.Init();

    scene.Update(0.016f);
    scene.Update(0.033f);

    EXPECT_FLOAT_EQ(scene.lastDeltaTime, 0.033f);
}
```

#### 2.4 Renderer Library - Functional Tests

**Priority tests:**
- Shader compilation logic (mock GL context)
- Vertex buffer management
- Resource handle validation

**Note:** Graphics tests test LOGIC, not rendering. Mock OpenGL context for unit tests.

---

### Phase 3: Local Execution

#### 3.1 Build Tests

```bash
# Configure with tests enabled
cmake -B build -S . \
  -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake \
  -DBUILD_TESTING=ON

# Build all test targets
cmake --build build --target foundation-tests foundation-benchmarks

# Build all tests at once
cmake --build build -j8
```

#### 3.2 Run Tests

```bash
# Run all tests
ctest --test-dir build --output-on-failure

# Run specific library tests
ctest --test-dir build -R foundation-tests --output-on-failure

# Run only unit tests (exclude benchmarks)
ctest --test-dir build -R ".*-tests$" --output-on-failure

# Run only benchmarks
ctest --test-dir build -R ".*-benchmarks$" --output-on-failure

# Verbose output
ctest --test-dir build --verbose

# Parallel execution
ctest --test-dir build -j8

# Generate JUnit XML
ctest --test-dir build --output-junit test-results.xml
```

**Note:** Requires CMake 3.21.4+ for `--output-junit`

#### 3.3 Run Benchmarks Directly

```bash
# Run specific benchmark executable
./build/libs/foundation/tests/foundation-benchmarks

# With JSON output
./build/libs/foundation/tests/foundation-benchmarks \
  --benchmark_format=json \
  --benchmark_out=results.json

# Filter benchmarks
./build/libs/foundation/tests/foundation-benchmarks --benchmark_filter=Arena.*

# Run for specific time
./build/libs/foundation/tests/foundation-benchmarks --benchmark_min_time=2.0
```

---

### Phase 4: GitHub Actions Integration

#### 4.1 Create CI Workflow

**File:** `.github/workflows/tests.yml`

```yaml
name: Unit Tests & Benchmarks

on:
  push:
    branches: [main]
  pull_request:
    branches: [main]

jobs:
  test:
    runs-on: macos-latest
    timeout-minutes: 30

    steps:
      - name: Checkout code
        uses: actions/checkout@v4
        with:
          submodules: recursive

      - name: Setup vcpkg
        uses: lukka/run-vcpkg@v11
        with:
          vcpkgDirectory: '${{ github.workspace }}/vcpkg'
          vcpkgGitCommitId: 'latest'

      - name: Configure CMake
        run: |
          cmake -B build -S . \
            -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake \
            -DBUILD_TESTING=ON \
            -DCMAKE_BUILD_TYPE=Release

      - name: Build tests
        run: cmake --build build --config Release -j$(sysctl -n hw.ncpu)

      - name: Run unit tests
        run: |
          ctest --test-dir build \
            --output-on-failure \
            --build-config Release \
            -R ".*-tests$" \
            -j$(sysctl -n hw.ncpu)

      - name: Run benchmarks
        run: |
          ctest --test-dir build \
            --output-on-failure \
            --build-config Release \
            -R ".*-benchmarks$"

      - name: Generate test report (JUnit XML)
        if: always()
        run: |
          ctest --test-dir build \
            --output-junit test-results.xml \
            --build-config Release

      - name: Upload test results
        if: always()
        uses: actions/upload-artifact@v4
        with:
          name: test-results
          path: build/test-results/*.xml

      - name: Upload benchmark results
        if: always()
        uses: actions/upload-artifact@v4
        with:
          name: benchmark-results
          path: build/benchmark-results/*.json

      - name: Publish test results
        if: always()
        uses: EnricoMi/publish-unit-test-result-action/macos@v2
        with:
          files: build/test-results/*.xml
```

#### 4.2 Branch Protection

Configure GitHub repository settings:
- Require tests to pass before merging
- Enable status checks for PR review
- Consider adding performance regression checks

#### 4.3 Performance Regression Detection (Future)

Use tools like [Bencher](https://bencher.dev) to track benchmark results over time and fail CI if performance degrades.

---

### Phase 5: Documentation

#### 5.1 Update Project Documentation

**Files to update:**
- `README.md` - Add "Running Tests" section
- `/docs/workflows.md` - Add testing workflow
- `/docs/technical/cpp-coding-standards.md` - Add testing conventions

**README.md addition:**
```markdown
## Running Tests

### Build and Run All Tests
```bash
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

### Run Specific Tests
```bash
ctest --test-dir build -R foundation-tests --output-on-failure
```

### Run Benchmarks
```bash
ctest --test-dir build -R ".*-benchmarks$"
```
```

#### 5.2 Create Testing Guidelines

See `/docs/technical/testing-guidelines.md` (to be created) for:
- When to write unit vs integration vs performance tests
- Mocking strategies for platform dependencies
- Test organization patterns
- Common test scenarios and examples
- Benchmark writing best practices

---

## Testing Principles

### What to Test (Unit Tests)

✅ **Business logic and algorithms**
- Memory arena allocation and tracking
- Resource handle validation and generation tracking
- Hash collision detection
- ECS entity/component logic

✅ **Data structures and utilities**
- String hashing correctness
- Math utilities
- Collection types
- State machines

✅ **Error handling**
- Boundary conditions
- Invalid input handling
- Resource exhaustion

### What to Benchmark (Performance Tests)

✅ **Critical path performance**
- Memory allocation speed (arena vs malloc)
- String hashing performance
- Tessellation throughput
- Batch rendering performance

✅ **Comparative benchmarks**
- Arena vs malloc
- Different hash algorithms
- Different data structures

### What NOT to Test (Unit Tests)

❌ **Graphics rendering**
- OpenGL calls (test logic, not rendering)
- Visual output (use sandbox for visual verification)
- GPU-dependent behavior

❌ **External dependencies**
- GLFW window creation (mock it)
- File system operations (inject interface)
- Network calls

❌ **Performance (use benchmarks instead)**
- Don't test performance in unit tests
- Dedicated benchmarks compile with optimizations

### Test Independence

- ✅ Each test must run independently
- ✅ No shared state between tests
- ✅ Tests must not depend on execution order
- ✅ Clean up resources in test fixtures
- ❌ Never use global state or singletons in tests

### Mocking Strategy

**Simple mocking approach:**
1. Define interface for platform dependencies
2. Create mock implementations for testing
3. Inject dependencies via constructor or setter
4. Test logic against mock interface

**Example:**
```cpp
// Interface
class IWindow {
public:
    virtual ~IWindow() = default;
    virtual bool ShouldClose() const = 0;
    virtual void SwapBuffers() = 0;
};

// Production implementation
class GLFWWindow : public IWindow {
    GLFWwindow* m_window;
public:
    bool ShouldClose() const override {
        return glfwWindowShouldClose(m_window);
    }
    void SwapBuffers() override {
        glfwSwapBuffers(m_window);
    }
};

// Test mock
class MockWindow : public IWindow {
public:
    bool shouldClose = false;
    int swapBuffersCount = 0;

    bool ShouldClose() const override {
        return shouldClose;
    }

    void SwapBuffers() override {
        swapBuffersCount++;
    }
};

// Test
TEST(ApplicationTests, ExitsWhenWindowCloses) {
    MockWindow window;
    Application app(&window);

    window.shouldClose = true;
    app.Run(); // Should exit immediately

    EXPECT_FALSE(app.IsRunning());
}
```

---

## Test Coverage Goals

**Initial target:** 60% coverage for core libraries
- Focus on critical paths first
- Gradually increase coverage over time

**Priority order:**
1. Foundation library (memory, logging, handles)
2. Engine library (application, scenes, ECS)
3. Renderer library (non-GL logic)
4. UI library (layout, input handling)
5. World library (generation algorithms)
6. Game-systems library (game-specific logic)

**Do NOT obsess over 100% coverage:**
- Some code is not worth testing (trivial getters/setters)
- Visual/integration tests better suited for some scenarios
- Focus on high-value, high-risk areas first

---

## Workflow Integration

### Before Committing Code

1. Write tests for new functionality (functional and/or performance)
2. Run relevant test suite locally
3. Ensure all tests pass
4. Fix any failing tests before committing

### During Code Review

- Review test coverage for new code
- Verify tests actually test the intended behavior
- Check for test independence and clarity
- Suggest additional test cases if needed
- Review benchmark results for performance-critical code

### Continuous Integration

- All PRs must have passing tests
- Test failures block merge
- Review test results in GitHub Actions
- Address failures promptly
- Monitor benchmark results for performance regressions

---

## Migration Path

If we need to switch functional testing frameworks later:
- Google Test → Catch2 requires rewriting tests (different syntax)
- Google Test → Doctest requires rewriting tests (different syntax)
- **Benchmark framework is independent** - can keep Google Benchmark regardless

Test organization and CMake setup remains the same.

---

## Next Steps

1. **Get approval on framework choice (Google Test + Google Benchmark)**
2. Add gtest and benchmark to vcpkg.json
3. Create test directories and CMakeLists.txt files
4. Write initial tests for foundation library (3-5 unit tests, 2-3 benchmarks)
5. Verify local execution with ctest
6. Create GitHub Actions workflow
7. Document testing patterns in testing-guidelines.md
8. Gradually expand test coverage across all libraries

---

**Questions or concerns?** Review `/docs/research/cpp-test-framework-research.md` for detailed framework comparison and rationale.
