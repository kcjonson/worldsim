# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

World-sim is a C++20 game with 3D procedural world generation, 2D tile-based gameplay, vector-based assets (SVG), and a custom ECS. Built as a monorepo of modular libraries. For full details, see [README.md](README.md).

**Critical Architectural Decisions:**
- Vector assets (SVG) with procedural variation → `/docs/technical/vector-graphics/INDEX.md`
- Custom ECS in engine → `/docs/technical/cpp-coding-standards.md#ecs`
- Roll our own core systems (not external libraries)

## Development Standards

**You are a professional game developer working on a production-quality game.**

### Workflow: Before Writing Code

**ALWAYS follow this process:**

0. **Determine git status**
   - Are you on the correct branch? You most likely need to be on main

1. **Read documentation first**
   - `/docs/status.md` - Current project state
   - Keep explicit todos and work items in this list, with checkboxes
   - Update this document before and after doing any work
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

5. **Prepare for Coding**
   - Create a new branch to do work in


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
- **⚠️ NEVER provide time estimates or cost estimates for tasks** - You cannot accurately estimate how long work will take. Focus on clear task descriptions and deliverables instead.

## Documentation System

### When Asked "What are we working on?" or "Where are we?"
→ Check `/docs/status.md` FIRST

### For Technical Implementation Details
→ Check `/docs/technical/INDEX.md` for list of design documents

### For Game Design & Feature Requirements
→ Check `/docs/design/INDEX.md` for game design documents

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

**Visual Verification:**
- When user reports visual issues ("X doesn't appear", "layout is wrong")
- Capture screenshot via HTTP endpoint to see actual output
- See `/docs/workflows.md` → "Verifying Visual Output"

**Sandbox Control:**
- Start: `./build/apps/ui-sandbox/ui-sandbox --http-port=8081`
- Control: `curl "http://127.0.0.1:8081/api/control?action={action}"`
- Actions: `exit`, `scene&scene=name`, `pause`, `resume`, `reload`
- Screenshot: `curl http://127.0.0.1:8081/api/ui/screenshot > screenshot.png`
- If port in use: Sandbox exits with message to kill existing instance

**CRITICAL: Testing Visual Changes**
When you make code changes and need to verify visually:
1. **Kill old instance**: `curl "http://127.0.0.1:8081/api/control?action=exit"`
2. **Rebuild**: `cmake --build build --target ui-sandbox -j8`
3. **Launch new instance**: `cd build/apps/ui-sandbox && ./ui-sandbox --scene=<scene> --http-port=8081 &`
4. **Wait for startup**: `sleep 3`
5. **Take screenshot**: `curl -s http://127.0.0.1:8081/api/ui/screenshot > /tmp/screenshot.png`

**NEVER skip the rebuild step!** An old instance will not reflect your changes.

## Quick Reference

| When You Need... | Check... |
|------------------|----------|
| Current project status | `/docs/status.md` |
| Detailed implementation notes | `/docs/development-log.md` |
| Game design & requirements | `/docs/design/INDEX.md` |
| How to add a new library | `/docs/workflows.md` |
| Project structure | `/docs/technical/monorepo-structure.md` |
| Build commands | `README.md` |
| Coding standards | `/docs/technical/cpp-coding-standards.md` |
| Engine patterns to use | `/docs/technical/INDEX.md` (Engine Patterns section) |

## Status.md Format and Workflow

### Structure: Epic/Story/Task Hierarchy

`/docs/status.md` is a **CHECKLIST-ONLY** document. NO long-form content, architectural decisions, or detailed rationale.

**Format:**
- **Epic** → **Story** → **Task** → **Sub-task** (max 3 levels of nesting)
- Template provided at top of status.md
- Use terminology: Epic (top level), Story (major feature), Task (implementation step), Sub-task (detail)

**Required Fields for Each Epic:**
```markdown
## Epic Title
**Spec/Documentation:** /path/to/doc.md or /path/to/folder/
**Dependencies:** Other Epic Name (if applicable)
**Status:** ready | in progress | blocked | needs spec

**Tasks:**
- [ ] Story Title
  - [ ] Task Title
    - [ ] Sub-task Title
```

**Sections in status.md:**
1. **Recently Completed Epics** - Last 4 completed (with ALL tasks marked [x])
2. **In Progress Epics** - Currently active work (at least one task incomplete)
3. **Planned Epics** - Future work
4. **Blockers & Issues** - Current problems

### When to Update status.md

**Before starting work:**
1. Find or create the relevant Epic
2. Mark the Story/Task you're working on as in-progress (mental note or comment)
3. Check Dependencies field for prerequisite work

**After completing a task:**
1. Mark task as [x]
2. If all tasks in an Epic are complete, mark Epic as complete
3. Update Last Updated timestamp

**When Epic is complete:**
1. Move Epic to "Recently Completed Epics" section
2. If more than 4 completed epics, move oldest to development-log.md
3. Add development-log.md entry with:
   - Date and title
   - What was accomplished
   - Files created/modified
   - Technical decisions made
   - Lessons learned
   - Next steps

### Content Placement Rules

**status.md contains:**
- ✅ Checklists (Epic/Story/Task/Sub-task)
- ✅ Status indicators (ready/in progress/blocked/needs spec)
- ✅ Dependencies between epics
- ✅ Performance targets (as success criteria)
- ✅ Blockers & issues

**status.md does NOT contain:**
- ❌ Architectural decisions (→ development-log.md if short, technical docs if long)
- ❌ Implementation rationale (→ development-log.md or technical docs)
- ❌ Detailed technical discussion (→ technical docs)
- ❌ Long-form content or paragraphs (→ development-log.md or technical docs)
- ❌ Historical "Recent Decisions" lists (→ development-log.md)
- ❌ Notes section (→ notes go in the spec/documentation for each epic)

**Where architectural content goes:**
- **Short decisions** (<1 paragraph): development-log.md
- **Long decisions** (>1 paragraph): Relevant technical doc (e.g., `/docs/technical/architecture.md`)
- **Rationale and context**: development-log.md or technical docs
- **Performance targets**: Can stay in status.md as success criteria for epics

## After Significant Work

### Update `/docs/status.md`:
- Mark completed tasks with `[x]`
- Update "Blockers & Issues" if any
- Update "Last Updated" timestamp
- When Epic complete, move to "Recently Completed" and create development-log.md entry
- Notes belong in the spec/documentation for each epic, NOT in status.md

### Add entry to `/docs/development-log.md`:
Add a new timestamped section (newest at top) with:
- **Date and title** (`### 2025-MM-DD - Title`)
- **What was accomplished** - Brief summary
- **Files created/modified** - List of changed files
- **Technical details** - Implementation notes, decisions made
- **Lessons learned** - What worked, what didn't
- **Next steps** - What comes next

**IMPORTANT**: Detailed implementation decisions, technical rationale, and design choices go in development-log.md, NOT status.md. The development log is the detailed historical record; status.md is only the current snapshot of project state.


### Workflow: After Writing Code

1. **Open a PR**

2. **Switch back to the main branch**