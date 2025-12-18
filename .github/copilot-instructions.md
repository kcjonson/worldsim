# Copilot Code Review Instructions for World-Sim

## Repository Overview

**World-sim** is a C++20 game with 3D procedural world generation and 2D tile-based gameplay. Uses vector-based assets (SVG) and a custom Entity Component System (ECS). Built as a monorepo (~86K lines, 259 C++ files) using CMake + vcpkg, OpenGL, and custom core systems.

**Monorepo Structure** - Strict dependency layers (each depends only on layers below):
```
foundation → renderer → (ui | world | game-systems) → engine
```
**Critical**: No circular dependencies. No upward dependencies.

## Essential Documentation (MUST REVIEW)

- **`/docs/technical/cpp-coding-standards.md`** - Primary style guide, naming, memory management, patterns
- **`/docs/technical/monorepo-structure.md`** - Dependency rules and architecture
- **`.clang-tidy`** - Enforced checks (warnings as errors)
- **`.clang-format`** - LLVM-based, tabs, 140 columns

## Code Review Checklist

### 1. Coding Standards (CRITICAL - enforced by clang-tidy)

**Naming**: Classes/Structs/Enums=`PascalCase`, Functions/Methods/Variables=`camelCase`, Members=`camelCase` (NO prefix - not `m_`), Constants=`kPascalCase`, Namespaces=`lowercase`

**Files**: .h/.cpp side-by-side, `#pragma once` only, one class per file, filename matches class

**Parameter Shadowing Bug** (common):
```cpp
void setText(const std::string& text) { text = text; }  // BUG: self-assignment
void setText(const std::string& newText) { text = newText; }  // CORRECT
```

### 2. Architecture & Dependencies

**Critical**: Verify includes respect dependency hierarchy. No upward/cross-layer dependencies.

**Memory**: Prefer stack allocation. Use smart pointers over raw new/delete. RAII for resources. Object pooling for tiles/chunks/entities.

**ECS** (custom): Use for game tiles, chunks, entities. NOT for UI, managers, rendering systems.

### 3. Performance Red Flags

**Memory**: Large object copies (use const ref), missing const methods, vectors without .reserve(), resource leaks

**Algorithms**: O(n²)+ nested loops, repeated map/vector lookups, unnecessary full dataset processing, string concat in loops

**Cache**: Non-contiguous memory access in hot loops (consider Struct of Arrays for large datasets)

**Resources**: OpenGL objects not deleted, missing RAII for C resources, file/connection leaks

### 4. Code Quality Essentials

**Functions**: Single responsibility, < 100 lines. Use C++20 designated initializers for 3+ params. Look for code duplication.

**Error Handling**: Descriptive assertions, appropriate log levels (ERROR/WARNING/INFO/DEBUG)

**C++20**: Use constexpr, std::span, structured bindings, range-for, auto, concepts

**Files**: > 500 lines = evaluate for splitting

**Project Patterns**:
- Build custom systems (ECS, vector graphics) over external libs
- Use "scene" not "screen"
- Prefer in-class member initializers: `GLFWwindow* window{nullptr};`
- Forward declarations in headers, full includes in .cpp

**Shaders** (UberShader pattern):
- Use single UberShader uploaded once to GPU (not multiple shader programs)
- Organizationally OK to split shader code across multiple files for structure
- Shader code should be well-structured and maintainable
- Combine files before uploading to GPU as one shader program

### 5. Review Style

**Be specific**: "Extract lines 45-67 into `calculateDistance()`" not "This could be better"

**Explain why**: "Mark const because it doesn't modify state" not just "Use const"

**Ask questions** when intent is unclear

**Prioritize**: Critical (crashes, leaks, architecture) > Important (performance, duplication) > Nice-to-have (style)

**Reference docs**: Link to coding standards and design docs when applicable

## Common Pitfalls

1. Dependency hierarchy violations (upward/cross-layer imports)
2. Parameter shadowing bugs (setter params named same as members)
3. Missing const (methods, references)
4. Raw pointers without RAII
5. Hot path inefficiency (allocations/copies in loops)
6. Naming convention violations
7. `#ifndef` instead of `#pragma once`
8. Split include/src directories (should be side-by-side)
9. External libs for core systems
10. Silent failures (no asserts/logging)

## Focus Areas

Review for **correctness** (standards, architecture), **performance** (inefficiencies), **maintainability** (clarity, organization), and **consistency** (patterns, conventions).

**Authority**: `/docs/technical/cpp-coding-standards.md` is the definitive style guide.
