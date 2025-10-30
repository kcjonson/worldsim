# C++ Test Framework Research

**Date:** 2025-10-30
**Purpose:** Evaluate test frameworks for worldsim unit testing infrastructure
**Updated:** 2025-10-30 - Added performance testing analysis, game engine considerations, CTest clarification

## Executive Summary

This document compares three modern C++ test frameworks for integration into worldsim's testing infrastructure. All three work with CMake/CTest and support GitHub Actions CI/CD.

**Recommendation:** **Google Test + Google Benchmark** - Industry standard with comprehensive performance testing for game engine development.

---

## Requirements

For worldsim, our test framework must:

1. **Headless execution** - Run in CI without graphics/GUI dependencies
2. **Performance testing** - Critical for game engine (arena allocators, tessellation, batching)
3. **Standard output formats** - JUnit XML for GitHub Actions integration
4. **Easy to write tests** - Low barrier for adding new tests
5. **CMake/CTest integration** - Work with existing build system
6. **Available in vcpkg** - Easy dependency management
7. **Good documentation** - Team can learn quickly

**Important Context:**
- **NOT using TDD workflow** - Compile time is less critical than performance testing
- **Game engine requirements** - Performance regression testing is as important as functional testing
- **CI/CD integration** - GitHub Actions will run tests on every PR

---

## Understanding CTest's Role

**Critical:** CTest is NOT a test framework - it's the test orchestrator/runner.

```
┌─────────────────────────────────────────┐
│ Test Framework                          │
│ (Google Test / Catch2 / Doctest)        │
│ - Assertions & test organization        │
│ - Generates test output                 │
└────────────────┬────────────────────────┘
                 │ writes results
                 ↓
┌─────────────────────────────────────────┐
│ CTest (Test Runner/Orchestrator)        │
│ - Discovers test executables            │
│ - Runs tests (can parallelize)          │
│ - Aggregates results                    │
│ - Formats output (JUnit XML, console)   │
└────────────────┬────────────────────────┘
                 │ output consumed by
                 ↓
┌─────────────────────────────────────────┐
│ CI System (GitHub Actions)              │
│ - Parses JUnit XML                      │
│ - Shows pass/fail/skip counts           │
│ - Displays test duration                │
│ - Fails PR on test failures             │
└─────────────────────────────────────────┘
```

**What CTest does:**
- Discovers test executables via `add_test()` in CMakeLists.txt
- Runs them (can parallelize with `-j`)
- Collects exit codes (0=pass, non-zero=fail)
- Generates reports in multiple formats
- Uploads to CDash dashboard (optional)

**Key command:**
```bash
ctest --test-dir build --output-junit results.xml
```
(Requires CMake 3.21.4+)

**Analogy:** CTest is like `npm test` or `yarn test` - it runs test executables but doesn't provide assertions or test organization. You still need a framework like Jest (in JS) or Google Test (in C++).

---

## Output Formats & Metadata

### What Reports Look Like

All three frameworks output to **console** and **structured formats**:

#### Console Output (Human-Readable)
```
[==========] Running 3 tests from 1 test suite.
[----------] Global test environment set-up.
[----------] 3 tests from ArenaTests
[ RUN      ] ArenaTests.BasicAllocation
[       OK ] ArenaTests.BasicAllocation (0 ms)
[ RUN      ] ArenaTests.MultipleAllocations
[       OK ] ArenaTests.MultipleAllocations (1 ms)
[ RUN      ] ArenaTests.Reset
[       OK ] ArenaTests.Reset (0 ms)
[----------] 3 tests from ArenaTests (1 ms total)

[==========] 3 tests from 1 test suite ran. (2 ms total)
[  PASSED  ] 3 tests.
```

#### JUnit XML (Machine-Readable)
```xml
<?xml version="1.0" encoding="UTF-8"?>
<testsuites tests="3" failures="0" errors="0" time="0.002">
  <testsuite name="ArenaTests" tests="3" failures="0" errors="0" time="0.001">
    <testcase name="BasicAllocation" status="run" time="0.000" classname="ArenaTests"/>
    <testcase name="MultipleAllocations" status="run" time="0.001" classname="ArenaTests"/>
    <testcase name="Reset" status="run" time="0.000" classname="ArenaTests"/>
  </testsuite>
</testsuites>
```

### Metadata Beyond Pass/Fail

| Data Type | Google Test | Catch2 | Doctest | Notes |
|-----------|-------------|--------|---------|-------|
| **Test duration** | ✅ Automatic | ✅ Automatic | ✅ Automatic | All frameworks track per-test time |
| **Benchmark data** | ✅ Google Benchmark | ✅ Built-in | ⚠️ Limited | CPU time, wall time, iterations, memory |
| **Performance metrics** | ✅ Detailed | ⚠️ Basic | ❌ Minimal | See performance testing section |
| **Memory usage** | ✅ Via Benchmark | ❌ No | ❌ No | Google Benchmark tracks allocations |
| **Code coverage** | ⚠️ Separate tool | ⚠️ Separate tool | ⚠️ Separate tool | Use gcov/llvm-cov (not part of frameworks) |
| **Custom metadata** | ✅ Via reporters | ✅ Via reporters | ✅ Via reporters | Can extend output |

**Important:** Coverage is a **separate tool** (gcov, llvm-cov) that instruments code during compilation. Not part of test frameworks.

### How GitHub Actions Consumes

GitHub Actions parses JUnit XML and shows:
- ✅ Pass/fail/skip counts in PR checks
- ✅ Test duration per test
- ✅ Test failure details with messages
- ✅ Can fail PR automatically on failures
- ✅ Test summary in PR comments (via plugins)

**Example GitHub Actions output:**
```
✅ foundation-tests: 45 passed, 0 failed (1.2s)
✅ engine-tests: 32 passed, 0 failed (0.8s)
❌ renderer-tests: 18 passed, 2 failed (1.5s)
   ❌ ShaderTests.CompileValid - Expected: SUCCESS, Got: COMPILE_ERROR
   ❌ BatchTests.Performance - Took 125ms, expected <100ms
```

---

## Performance Testing (Critical for Game Engines)

Game engines have unique testing needs beyond pass/fail functional tests:
- Memory arena allocator speed (10,000+ allocations/frame)
- Tessellation performance (complex SVG paths)
- Batch rendering throughput (10,000+ draws)
- Asset loading times
- Animation system performance

**Performance regression testing is as important as functional testing** for game engines.

### Performance Testing Comparison

| Framework | Benchmarking | Maturity | Features | Verdict |
|-----------|--------------|----------|----------|---------|
| **Google Test + Google Benchmark** | ⭐⭐⭐⭐⭐ | Industry standard | CPU time, wall time, memory tracking, iterations, statistical analysis | Best for game engines |
| **Catch2** | ⭐⭐⭐ | Built-in (basic) | Basic micro-benchmarking, timing | Good for simple benchmarks |
| **Doctest** | ⭐ | Minimal | Very limited | Not suitable for performance testing |

### Google Benchmark Example

```cpp
#include <benchmark/benchmark.h>
#include "foundation/memory/arena.h"

static void BM_ArenaAllocation(benchmark::State& state) {
    Arena arena(1024 * 1024); // 1MB arena

    for (auto _ : state) {
        void* ptr = arena.Allocate(128);
        benchmark::DoNotOptimize(ptr);
    }

    state.SetItemsProcessed(state.iterations());
    state.SetBytesProcessed(state.iterations() * 128);
}
BENCHMARK(BM_ArenaAllocation);

static void BM_ArenaVsMalloc(benchmark::State& state) {
    Arena arena(1024 * 1024);
    bool useArena = state.range(0);

    for (auto _ : state) {
        if (useArena) {
            void* ptr = arena.Allocate(128);
            benchmark::DoNotOptimize(ptr);
        } else {
            void* ptr = malloc(128);
            benchmark::DoNotOptimize(ptr);
            free(ptr);
        }
    }
}
BENCHMARK(BM_ArenaVsMalloc)->Arg(0)->Arg(1);

BENCHMARK_MAIN();
```

**Output:**
```
---------------------------------------------------------------
Benchmark                     Time             CPU   Iterations
---------------------------------------------------------------
BM_ArenaAllocation         12.5 ns         12.5 ns     56000000
BM_ArenaVsMalloc/0          145 ns          145 ns      4820000    (malloc)
BM_ArenaVsMalloc/1         12.3 ns         12.3 ns     56900000    (arena)
```

**This shows arena is 11.8× faster than malloc** - critical data for game engine optimization.

### Catch2 Benchmark Example

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>

TEST_CASE("Arena performance", "[benchmark]") {
    Arena arena(1024 * 1024);

    BENCHMARK("Arena allocation") {
        return arena.Allocate(128);
    };
}
```

**Output:**
```
benchmark name                       samples       iterations    est run time
                                     mean          low mean      high mean
                                     std dev       low std dev   high std dev
-------------------------------------------------------------------------------
Arena allocation                     100           10000         1.25 ms
                                     12.5 ns       12.3 ns       12.8 ns
                                     1.2 ns        0.9 ns        1.6 ns
```

**Verdict:** Catch2's benchmarking is simpler but less detailed than Google Benchmark.

### Doctest Performance Testing

Doctest has very limited benchmarking capabilities. Not recommended for performance-critical code.

---

## Framework Comparison

### 1. Google Test + Google Benchmark

**Repository:** https://github.com/google/googletest, https://github.com/google/benchmark
**License:** Apache 2.0 / BSD-3-Clause
**First Release:** 2008 (GoogleTest), 2015 (Benchmark)
**Maturity:** Industry standard, very mature

#### Features
- ✅ Comprehensive assertion library (`EXPECT_EQ`, `ASSERT_TRUE`, etc.)
- ✅ Mature mocking framework (gmock) included
- ✅ **Industry-standard performance testing (Google Benchmark)**
- ✅ Parameterized tests
- ✅ Death tests (test for crashes/assertions)
- ✅ Test fixtures with SetUp/TearDown
- ✅ Excellent documentation and examples
- ✅ JUnit XML output
- ⚠️ Slower compile times (separate library to link)

#### Compile Time
- **Slowest of the three** - Requires linking against gtest, gmock, and benchmark libraries
- Typical test file: ~2-5 seconds incremental compile
- Full test suite rebuild: Can be significant for large projects
- **Trade-off:** Slower compiles, but best performance testing

#### Example Test
```cpp
#include <gtest/gtest.h>

TEST(ArenaTests, BasicAllocation) {
    Arena arena(1024);
    void* ptr = arena.Allocate(128);
    EXPECT_NE(ptr, nullptr);
    EXPECT_EQ(arena.GetUsed(), 128);
}

TEST(ArenaTests, Reset) {
    Arena arena(1024);
    arena.Allocate(256);
    arena.Reset();
    EXPECT_EQ(arena.GetUsed(), 0);
}
```

#### CI Integration
- ✅ Excellent - Standard in the industry
- ✅ Works seamlessly with CMake and CTest
- ✅ JUnit XML output for GitHub Actions
- ✅ Google Benchmark integrates with CTest
- ✅ Many examples and tutorials available

#### Pros
- **Best performance testing** - Google Benchmark is the industry standard
- Used by Chromium, LLVM, TensorFlow, Unreal Engine
- Most comprehensive feature set (testing + mocking + benchmarking)
- Excellent documentation and community support
- Familiar to most C++ developers
- Very stable and mature

#### Cons
- Slowest compile times of the three options
- More verbose syntax
- Heavier dependency (three libraries: gtest, gmock, benchmark)
- Older design patterns (pre-C++11 style)

#### Verdict for Worldsim
✅ **Best choice for game engine** - Performance testing is critical. Compile time trade-off is acceptable since we're not doing TDD. Industry standard used by major game engines.

---

### 2. Catch2

**Repository:** https://github.com/catchorg/Catch2
**License:** BSL-1.0 (Boost Software License)
**First Release:** 2010
**Maturity:** Very mature, modern C++ design

#### Features
- ✅ BDD-style test syntax (SCENARIO/GIVEN/WHEN/THEN)
- ✅ Natural expression-based assertions
- ✅ **Built-in basic micro-benchmarking**
- ✅ Header-only or compiled library (v3+)
- ✅ Beautiful test output with colored diffs
- ✅ Test fixtures and sections
- ✅ Generators for data-driven tests
- ✅ JUnit XML, TAP, and custom reporters
- ⚠️ Benchmarking less detailed than Google Benchmark
- ❌ No built-in mocking (use 3rd party like Trompeloeil)

#### Compile Time
- **Medium** - Header-only adds compile overhead
- Catch2 v3 offers compiled library option (faster than header-only)
- Typical test file: ~1-3 seconds incremental compile
- Can be mitigated with precompiled headers

#### Example Test
```cpp
#include <catch2/catch_test_macros.hpp>

TEST_CASE("Arena allocates memory", "[arena]") {
    Arena arena(1024);

    SECTION("Basic allocation") {
        void* ptr = arena.Allocate(128);
        REQUIRE(ptr != nullptr);
        REQUIRE(arena.GetUsed() == 128);
    }

    SECTION("Reset clears memory") {
        arena.Allocate(256);
        arena.Reset();
        REQUIRE(arena.GetUsed() == 0);
    }
}
```

#### CI Integration
- ✅ Excellent - Well-supported in CI environments
- ✅ Seamless CMake and CTest integration
- ✅ JUnit XML output for GitHub Actions
- ✅ Many output formats (XML, JSON, TAP)

#### Pros
- Modern C++ design (C++14+)
- Very readable test syntax
- Excellent assertion messages with diffs
- BDD-style tests if desired
- Built-in benchmarking (though basic)
- Active development and community
- Good balance of features and simplicity

#### Cons
- Header-only can slow compile times
- Compiled library option requires additional setup
- No built-in mocking (need third-party)
- Benchmarking less comprehensive than Google Benchmark
- Not as widely used in game engine development

#### Verdict for Worldsim
⚠️ **Good but not ideal** - Built-in benchmarking is convenient but less powerful than Google Benchmark. Better for projects prioritizing compile speed over performance testing.

---

### 3. Doctest

**Repository:** https://github.com/doctest/doctest
**License:** MIT
**First Release:** 2016
**Maturity:** Mature, actively maintained

#### Features
- ✅ Fastest compile times (by design)
- ✅ Catch2-compatible syntax
- ✅ Single-header library
- ✅ Minimal overhead (tests compile out in production builds)
- ✅ Natural expression-based assertions
- ✅ Test fixtures and subcases (like Catch2 sections)
- ✅ JUnit XML output
- ✅ Extremely lightweight (~6000 lines, single header)
- ❌ **Very limited performance testing**
- ❌ No built-in mocking (use 3rd party)

#### Compile Time
- **Fastest of the three** - Explicitly designed for speed
- Typical test file: ~0.5-1.5 seconds incremental compile
- 2-3× faster than Catch2, 5-10× faster than Google Test
- **Best for TDD workflow** (not worldsim's use case)

#### Example Test
```cpp
#include <doctest/doctest.h>

TEST_CASE("Arena allocates memory") {
    Arena arena(1024);

    SUBCASE("Basic allocation") {
        void* ptr = arena.Allocate(128);
        CHECK(ptr != nullptr);
        CHECK(arena.GetUsed() == 128);
    }

    SUBCASE("Reset clears memory") {
        arena.Allocate(256);
        arena.Reset();
        CHECK(arena.GetUsed() == 0);
    }
}
```

#### CI Integration
- ✅ Excellent - Designed for CI environments
- ✅ Seamless CMake and CTest integration
- ✅ JUnit XML output for GitHub Actions
- ✅ Very fast test execution

#### Pros
- **Fastest compile times by far** - Primary design goal
- Catch2-like syntax (easy migration path if needed)
- Single header, minimal setup
- Very lightweight and focused
- Active development
- MIT license (most permissive)
- Tests can be embedded in production code (compile out)

#### Cons
- **No meaningful performance testing** - Critical for game engines
- Newer than the other two (2016 vs 2008/2010)
- Smaller community than gtest/Catch2
- No built-in mocking
- Less comprehensive documentation than gtest
- Not commonly used in game engine development

#### Verdict for Worldsim
❌ **Not recommended** - Compile speed advantage doesn't outweigh lack of performance testing. Worldsim is not doing TDD (where compile speed matters most), but DOES need comprehensive benchmarking.

---

## Side-by-Side Feature Comparison

| Feature | Google Test + Benchmark | Catch2 | Doctest |
|---------|------------------------|--------|---------|
| **Compile Time** | Slow (2-5s) | Medium (1-3s) | Fast (0.5-1.5s) |
| **Performance Testing** | ⭐⭐⭐⭐⭐ | ⭐⭐⭐ | ⭐ |
| **Benchmark Metrics** | CPU, wall, memory, iterations | Basic timing | Minimal |
| **Assertions** | ✅ Comprehensive | ✅ Natural expressions | ✅ Natural expressions |
| **Mocking** | ✅ Built-in (gmock) | ❌ Third-party | ❌ Third-party |
| **BDD Syntax** | ❌ No | ✅ Yes | ❌ No |
| **Test Fixtures** | ✅ Yes | ✅ Yes | ✅ Yes |
| **Sections/Subcases** | ❌ No | ✅ Sections | ✅ Subcases |
| **JUnit XML** | ✅ Yes | ✅ Yes | ✅ Yes |
| **CMake Integration** | ✅ Excellent | ✅ Excellent | ✅ Excellent |
| **vcpkg Available** | ✅ Yes | ✅ Yes | ✅ Yes |
| **Header-Only** | ❌ No | ⚠️ Optional | ✅ Yes |
| **Documentation** | ⭐⭐⭐⭐⭐ | ⭐⭐⭐⭐ | ⭐⭐⭐ |
| **Game Engine Usage** | ⭐⭐⭐⭐⭐ | ⭐⭐⭐ | ⭐⭐ |
| **Maturity** | 17 years | 15 years | 9 years |
| **License** | Apache 2.0 / BSD-3 | BSL-1.0 | MIT |
| **GitHub Stars** | 34k+ / 8k+ | 18k+ | 6k+ |

---

## Game Engine Testing Patterns

Based on research of game engine testing practices:

### What to Test

**Functional Tests (Unit/Integration):**
- ✅ Memory arena allocators (correctness)
- ✅ Resource handle validation
- ✅ String hashing and collision detection
- ✅ Scene lifecycle and state management
- ✅ ECS entity/component logic
- ✅ Input handling and event propagation

**Performance Tests (Benchmarks):**
- ✅ Memory allocation speed (arena vs malloc)
- ✅ Tessellation performance (SVG → triangles)
- ✅ Batch rendering throughput
- ✅ Animation system update times
- ✅ Asset loading and parsing speed
- ✅ Serialization performance

### What NOT to Test with Unit Tests

- ❌ Visual rendering output (use sandbox + screenshots)
- ❌ GPU-specific behavior (too platform-dependent)
- ❌ Window creation (mock GLFW)
- ❌ File system operations (inject interface)

### Best Practices

From industry research:
1. **Test at lowest level of abstraction** - Unit tests for algorithms, integration tests for systems
2. **Separate performance tests from unit tests** - Different binaries with different compile flags
3. **Run benchmarks in CI** - Prevent performance regressions like you prevent feature regressions
4. **Compile performance tests with optimizations** - Unit tests often have optimizations disabled for debugging
5. **Treat performance bugs as seriously as functional bugs**

---

## Recommendation: Google Test + Google Benchmark

### Why Google Test + Google Benchmark?

**1. Performance Testing is Critical for Game Engines**
- Memory arena allocators must be fast (10,000+ allocations/frame)
- Tessellation performance determines how many SVG shapes we can render
- Batch rendering throughput affects frame rate
- Google Benchmark provides detailed metrics (CPU time, memory, iterations)
- Industry standard used by Unreal Engine, Chromium, LLVM

**2. Compile Time is Less Critical (No TDD)**
- Worldsim is NOT using TDD workflow
- Tests run in CI on PR, not constantly during development
- 2-3 second longer compile time is acceptable trade-off
- Developers won't be running tests every few minutes

**3. Industry Standard for Game Engines**
- Widely used in game engine development
- Excellent documentation and examples
- Large community with game dev expertise
- Familiar to most C++ game developers

**4. Comprehensive Feature Set**
- Functional testing (Google Test)
- Mocking (gmock) for GLFW/OpenGL
- Performance testing (Google Benchmark)
- All-in-one solution, no need to mix frameworks

**5. Best CI/CD Integration**
- JUnit XML output works perfectly with GitHub Actions
- Can run benchmarks in CI and fail on regressions
- Detailed performance metrics in CI logs

### What We Give Up vs Catch2
- BDD-style test syntax (not needed)
- Slightly faster compile times (acceptable trade-off)
- Built-in benchmarking (Google Benchmark is better)

### What We Give Up vs Doctest
- Fastest compile times (not critical without TDD)
- Lightweight single-header (prefer comprehensive features)

### What We Gain
- **Best-in-class performance testing** (critical for game engine)
- Industry standard with huge community
- Used by major game engines and frameworks
- Comprehensive feature set (testing + mocking + benchmarking)
- Excellent documentation and examples

---

## Implementation Approach

### Dependencies

Add to `vcpkg.json`:
```json
{
  "dependencies": [
    "gtest",           // Includes gmock
    "benchmark",       // Google Benchmark
    ...
  ]
}
```

### Test Organization

```
libs/foundation/tests/
├── CMakeLists.txt
├── unit/                    # Functional tests
│   ├── arena_tests.cpp
│   ├── logging_tests.cpp
│   └── hash_tests.cpp
└── benchmarks/              # Performance tests
    ├── arena_benchmarks.cpp
    └── hash_benchmarks.cpp
```

**Why separate?** Benchmarks compile with `-O3`, unit tests with `-O0 -g` for debugging.

### CMakeLists.txt Example

```cmake
# Unit tests (debug build)
find_package(GTest CONFIG REQUIRED)

add_executable(foundation-tests
    unit/arena_tests.cpp
    unit/logging_tests.cpp
    unit/hash_tests.cpp
)

target_link_libraries(foundation-tests
    PRIVATE
    foundation
    GTest::gtest
    GTest::gtest_main
)

add_test(NAME foundation-tests COMMAND foundation-tests)

# Benchmarks (release build)
find_package(benchmark CONFIG REQUIRED)

add_executable(foundation-benchmarks
    benchmarks/arena_benchmarks.cpp
    benchmarks/hash_benchmarks.cpp
)

target_link_libraries(foundation-benchmarks
    PRIVATE
    foundation
    benchmark::benchmark
    benchmark::benchmark_main
)

target_compile_options(foundation-benchmarks PRIVATE -O3)

add_test(NAME foundation-benchmarks COMMAND foundation-benchmarks --benchmark_format=json --benchmark_out=benchmark_results.json)
```

---

## Alternatives Considered

**Why not Catch2?**
- Built-in benchmarking is less comprehensive than Google Benchmark
- Would need to add Google Benchmark anyway for detailed metrics
- Better to standardize on one ecosystem (Google Test + Benchmark)

**Why not Doctest?**
- No meaningful performance testing capabilities
- Compile speed advantage only matters for TDD (not worldsim's workflow)
- Not commonly used in game engine development

**Why not custom framework?**
- Testing is infrastructure, not core game feature
- Mature frameworks are battle-tested
- Focus engineering time on game development

---

## Conclusion

**Google Test + Google Benchmark** is the best choice for worldsim because:
- ✅ **Best performance testing** - Critical for game engine development
- ✅ Industry standard used by major game engines
- ✅ Comprehensive feature set (testing + mocking + benchmarking)
- ✅ Excellent CI/CD integration with GitHub Actions
- ✅ Large community and documentation
- ✅ Compile time trade-off acceptable (no TDD workflow)

The performance testing capabilities alone justify this choice for a game engine emphasizing professional development and production quality.

**Next Steps:**
1. Get approval on framework choice
2. Add Google Test and Google Benchmark to vcpkg.json
3. Create test infrastructure for 6 libraries
4. Write example unit tests and benchmarks
5. Configure GitHub Actions workflow
6. Create testing guidelines document
