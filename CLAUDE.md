# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

World-sim is a C++20 game with 3D procedural world generation, 2D tile-based gameplay, vector-based assets (SVG), and a custom ECS. Built as a monorepo of modular libraries. For full details, see [README.md](README.md).

**Critical Architectural Decisions:**
- Vector assets (SVG) with procedural variation → `/docs/technical/vector-asset-pipeline.md`
- Custom ECS in engine → `/docs/technical/cpp-coding-standards.md#ecs`
- Roll our own core systems (not external libraries)

## Development Standards

**You are a professional game developer working on a production-quality game.**

### Workflow: Before Writing Code

**ALWAYS follow this process:**

1. **Read documentation first**
   - `/docs/status.md` - Current project state
   - Relevant technical design documents
   - Product specifications if implementing features

2. **Gather context**
   - Understand the system you're modifying
   - Read existing code in the area
   - Identify dependencies and interfaces

3. **Ask questions when needed**
   - If you lack information for a professional decision, **ASK THE USER**
   - Don't make uninformed assumptions
   - Better to ask than to implement incorrectly

4. **Plan thoughtfully**
   - Consider architecture implications
   - Think about performance impact
   - Plan for maintainability

### Code Quality

- **Industry best practices**: Follow game dev conventions
- **Performance-conscious**: Efficient code, profile before optimizing
- **Maintainable**: Clear structure, logical organization
- **Production-quality**: No hacks, no "good enough for now"

**DO:**
- Write production-quality code every time
- Follow the dependency hierarchy strictly
- Document non-obvious decisions
- Consider edge cases and error handling

**DON'T:**
- Make uninformed decisions without context
- Skip reading documentation
- Write quick hacks or temporary code
- Ignore established patterns
- Create technical debt

## Documentation System

### When Asked "What are we working on?" or "Where are we?"
→ Check `/docs/status.md` FIRST

### For Technical Implementation Details
→ Check `/docs/technical/INDEX.md` for list of design documents

### For Feature Requirements
→ Check `/docs/specs/INDEX.md` for product specifications

### For Common Tasks (adding files, creating scenes, etc.)
→ Check `/docs/workflows.md`

### For C++ Style and Patterns
→ Check `/docs/technical/cpp-coding-standards.md`

## Key Project Conventions

**File Organization:**
- Headers (.h) and implementation (.cpp) side-by-side in same directory
- Use `#pragma once` for header guards

**Naming:**
- Classes/Functions: PascalCase (`Shader`, `LoadTexture`)
- Variables: camelCase (`frameCount`, `deltaTime`)
- Members: `m_` prefix (`m_shader`, `m_isInitialized`)
- Constants: `k` prefix (`kMaxTextures`)

**Terminology:**
- Use "scene" not "screen" (`SplashScene`, not `SplashScreen`)
- Assets are "SVG files" not "images" or "textures"

**Memory:**
- Prefer stack allocation
- Use smart pointers over raw pointers
- Use memory arenas for temporary allocations
- Use resource handles for assets

**Logging:**
- Use structured logging with categories
- `LOG_DEBUG`, `LOG_INFO`, `LOG_WARNING`, `LOG_ERROR`
- Debug logs compile out in release builds

## Quick Reference

| When You Need... | Check... |
|------------------|----------|
| Current project status | `/docs/status.md` |
| How to add a new library | `/docs/workflows.md` |
| Project structure | `/docs/technical/monorepo-structure.md` |
| Build commands | `README.md` |
| Coding standards | `/docs/technical/cpp-coding-standards.md` |
| Engine patterns to use | `/docs/technical/INDEX.md` (Engine Patterns section) |

## After Significant Work

Update `/docs/status.md` with:
- Completed tasks
- New decisions made
- Blockers encountered
- Development log entry
