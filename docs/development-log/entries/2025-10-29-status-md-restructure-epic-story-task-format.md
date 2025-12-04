# Status.md Restructure: Epic/Story/Task Format

**Date:** 2025-10-29

**Documentation System Overhaul:**

Transformed status.md from a mixed-format document (containing checklists, architectural decisions, and long-form content) into a pure Epic/Story/Task checklist system.

**New Format:**
- **Epic > Story > Task > Sub-task** hierarchy (max 3 levels of nesting)
- Only last 4 completed epics + in-progress + planned epics
- Completed epics move to development-log.md with context
- Template provided at top of document for consistency
- Epic is complete only when ALL sub-tasks are [x]

**Structure:**
1. **Recently Completed Epics** - Last 4 completed (with full task lists)
2. **In Progress Epics** - Currently active work
3. **Planned Epics** - Future work with dependencies
4. **Blockers & Issues** - Current problems
5. **Notes** - Brief status updates (not detailed rationale)

**Content Migration:**
- Architectural decisions → development-log.md (short) or technical docs (long)
- Performance targets → Keep with epics (success criteria)
- Recent Decisions list → development-log.md (preserved below)

**Recent Architectural Decisions (Preserved from status.md):**

**Documentation & Organization (2025-10-12):**
- Tech docs instead of ADRs, no numbering (topic-based organization)
- Created workflows.md for common tasks (separate from CLAUDE.md)
- CLAUDE.md streamlined to ~124 lines (navigation guide only)

**C++ Standards & Tools (2025-10-12):**
- Naming: PascalCase classes/functions, camelCase variables, m_ prefix members, k prefix constants
- Header guards: `#pragma once` (not traditional guards)
- File organization: Headers (.h) and implementation (.cpp) side-by-side
- Linting: clang-format (manual) + clang-tidy (automatic)
- User's .clang-format: tabs, 140 column limit

**Architecture Decisions:**

**Vector-Based Assets (2025-10-12):**
- All game assets use SVG format with dynamic rasterization
- Roll our own core systems (not external libraries)

**Client/Server Architecture (2025-10-24):**
- Two-process design from day one (world-sim + world-sim-server)
- Server spawns on-demand (only when playing, not during main menu)
- HTTP + WebSocket protocol: HTTP for control, WebSocket for 60 Hz gameplay
- HTTP Debug Server: Separate debugging system (port 8080) using SSE

**Procedural Rendering (2025-10-26):**
- Tiles are code-generated (not SVG-based)
- Biome Influence Percentage System: Tiles have multiple biome influences creating natural ecotones
- SVG Asset Categorization: (1) Decorations/Entities, (2) Texture Patterns, (3) Animated Vegetation
- Ground Covers vs Biomes: Ground covers are physical surfaces, biomes determine appearance
- 1:1 Pixel Mapping for UI: Primitives use framebuffer dimensions for pixel-perfect rendering

**Singleton Architecture Decision (2025-10-29):**
- **Keep singletons for rendering** (industry best practice for game engines)
- Performance: Zero indirection, cache-friendly, no parameter overhead
- Industry standard: Unreal, Unity, id Tech all use singletons for core systems
- Colonysim's singleton architecture is correct for game engine performance

**Rendering Integration Strategy (2025-10-29):**
- **Decision deferred** until after compatibility analysis
- **Option A (Recommended)**: Adopt colonysim's VectorGraphics as implementation behind worldsim's Primitives API
  - Get mature batching + text + scissor support
  - Keep worldsim's clean API
  - Minimal refactoring
- **Option B**: Keep worldsim's BatchRenderer, port colonysim code to use Primitives API
  - More work, but keeps worldsim architecture pure

**Logging Macro Naming (2025-10-26):**
- Use unprefixed global macros (`LOG_ERROR` not `WSIM_LOG_ERROR`) for brevity
- Trade-off: Potential library conflicts vs developer experience
- Acceptable risk: Game project (not library), we control dependencies

**Engine Patterns Implemented (2025-10-27):**
- Resource handles (32-bit IDs with generation) - ✅ IMPLEMENTED
- Memory arenas (linear allocators) - ✅ IMPLEMENTED (14× faster than malloc)
- String hashing (FNV-1a, compile-time) - ✅ IMPLEMENTED
- Structured logging (categories + levels) - ✅ IMPLEMENTED
- Application class (unified game loop) - ✅ IMPLEMENTED (2025-10-29)
- Immediate mode debug rendering - Planned for later

**Files Modified:**
- `/docs/status.md` - Complete rewrite to Epic/Story/Task format (~560 lines → ~400 lines)
- `/docs/development-log.md` - Added this entry with preserved architectural decisions

**Workflow Documentation Created:**
Will be added to CLAUDE.md to ensure process is followed in future sessions


