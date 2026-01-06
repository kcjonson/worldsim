# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

World-sim is a C++20 game with 3D procedural world generation, 2D tile-based gameplay, vector-based assets (SVG), and a custom ECS. Built as a monorepo of modular libraries. For full details, see [README.md](README.md).

**Critical Architectural Decisions:**
- Vector assets (SVG) with procedural variation → `/docs/technical/vector-graphics/INDEX.md`
- Custom ECS in engine → `/docs/technical/cpp-coding-standards.md#ecs`
- Roll our own core systems where appropriate

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

### Complex Bug Investigation

For bugs involving multiple systems, coordinate/rendering issues, or multi-session investigation:

**Use the `/debug <issue-name>` command** to start a formal debugging session.

This activates the protocol from `/docs/technical/debugging-strategy.md` which uses:
- **Multi-agent scouting** — 3 parallel agents explore different hypothesis areas
- **Hypothesis synthesis** — Rank by consensus and evidence before deep investigation
- **Disciplined investigation** — One hypothesis at a time, user-only confirmation
- **Evidence preservation** — No premature removal of debug code, logs over screenshots


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
- **⚠️ NEVER change LOG_DEBUG to LOG_INFO** - LOG_DEBUG works fine. Debug logs are visible via the HTTP log server in dev tools. Do not change log levels to "see" logs.

### ⚠️ IMPORTANT: Documentation Over Existing Code

**When evaluating code style, conventions, or PR feedback:**
- **ALWAYS check `/docs/technical/cpp-coding-standards.md` FIRST** - This is the source of truth
- **NEVER assume existing code patterns are correct** - Legacy code may contain violations that haven't been cleaned up
- **Documented standards override observed patterns** - If code in the codebase contradicts the docs, the docs are correct

Example: If you see `m_` prefixes on member variables in `libs/ui/`, but the coding standards say "camelCase with no prefix" - the standards are correct, the existing code is legacy that needs cleanup.

### ⚠️ Clean Code: No Legacy, No Fallbacks

**This is critical. Aggressively delete old code. Never leave legacy fallbacks.**

#### Core Principles

1. **Replace, Don't Layer**
   - When implementing a new approach, DELETE the old implementation in the same commit
   - No `// legacy` comments, no `useNewSystem` flags, no fallback code paths
   - If the new code works, the old code is dead—delete it immediately

2. **One Path Rule**
   - There must be exactly ONE way to accomplish each task in the codebase
   - Multiple code paths for the same functionality = code smell requiring immediate cleanup
   - If you're adding a second way to do something, you must delete the first

3. **Delete First Workflow**
   - Before adding new code, identify what it replaces
   - Delete the old code FIRST, then add the new implementation
   - This forces clean breaks rather than accumulation of dead code

4. **No Feature Flags for Internal Changes**
   - Feature flags are ONLY for user-facing features with gradual rollout requirements
   - Internal refactors don't need flags—just change the code directly
   - If something breaks, fix it—don't create parallel paths

5. **Clean Up On Touch**
   - When modifying any file, actively look for dead code to remove
   - Delete: unused imports, unused functions, commented-out code, unreachable branches
   - Leave every file cleaner than you found it

#### Explicit Anti-Patterns (NEVER DO THESE)

```cpp
// ❌ NEVER: Conditional new/old paths
if (useNewSystem) {
    newImplementation();
} else {
    legacyImplementation();  // DELETE THIS
}

// ❌ NEVER: "Just in case" fallbacks
result = tryNewApproach();
if (!result) {
    result = oldApproach();  // DELETE THIS
}

// ❌ NEVER: Keeping old functions "for reference"
void oldFunction() { /* old implementation */ }  // DELETE THIS
void newFunction() { /* new implementation */ }

// ❌ NEVER: Renaming unused variables instead of deleting
void process(int _unusedParam) { }  // DELETE THE PARAMETER

// ❌ NEVER: Re-exporting removed types for "compatibility"
using OldTypeName [[deprecated]] = NewTypeName;  // DELETE THIS

// ❌ NEVER: TODO comments for removing legacy code
// TODO: Remove this after testing the new system  // NO - DELETE NOW

// ❌ NEVER: Version suffixes on functions
void renderV2() { }  // Just name it render() and delete the old one
```

#### The Right Way

```cpp
// ✅ CORRECT: One implementation, no alternatives
void render() {
    // The only render implementation
}

// ✅ CORRECT: Delete parameters you don't need
void process() {  // Removed unused parameter entirely
    // ...
}

// ✅ CORRECT: Replace types directly
using TypeName = NewImplementation;  // Old type is gone

// ✅ CORRECT: Clean, single code path
Result doOperation() {
    return modernApproach();  // No fallback, this is THE way
}
```

#### When Refactoring

1. **Understand** the old code completely
2. **Write** the new implementation
3. **Update** all call sites to use the new code
4. **Delete** the old implementation in the same commit
5. **Test** to ensure nothing breaks

If tests fail, fix the new code—don't revert to keeping both versions.

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
- Constants: `k` prefix (`kMaxTextures`)

**Terminology:**
- Use "scene" not "screen" (`SplashScene`, not `SplashScreen`)

**Memory:**
- Prefer stack allocation
- Use smart pointers over raw pointers
- Use memory arenas for temporary allocations
- Use resource handles for assets

**Logging:**
- Use structured logging with categories
- `LOG_DEBUG`, `LOG_INFO`, `LOG_WARNING`, `LOG_ERROR`
- Debug logs compile out in release builds
- **CRITICAL: LOG_DEBUG WORKS FINE. NEVER switch LOG_DEBUG to LOG_INFO "to see logs". Debug logs are visible in dev tools via the HTTP log server. Do not change log levels.**

**Visual Verification:**
- When user reports visual issues ("X doesn't appear", "layout is wrong")
- Capture screenshot via HTTP endpoint to see actual output
- IMPORTANT: Only capture a screenshot when requested by the user, avoid them where possible
- See `/docs/workflows.md` → "Verifying Visual Output"

**Sandbox Control:**
- Start: `cd build/apps/ui-sandbox && ./ui-sandbox`
- Control: `curl "http://127.0.0.1:8081/api/control?action={action}"`
- Actions: `exit`, `scene&scene=name`, `pause`, `resume`, `reload`
- Screenshot: `curl http://127.0.0.1:8081/api/ui/screenshot > screenshot.png`
- **IMPORTANT: Port 8081 is the DEFAULT - do NOT specify --http-port unless using a non-standard port**
- **IMPORTANT: Only ONE instance can run at a time** - the app has built-in port conflict detection. Do not assume multiple instances exist.

**CRITICAL: Testing Visual Changes**
When you make code changes and need to verify visually:
1. **Kill old instance** (if running): `curl "http://127.0.0.1:8081/api/control?action=exit"`
   - This is a **blocking call** - it returns only after shutdown is complete and the port is free
   - Response: `{"status":"ok","action":"exit","shutdown":"complete"}`
   - When you receive the OK response, proceed immediately (NO sleep needed)
   - If no instance is running, curl will fail with connection refused - that's fine, proceed
2. **Rebuild**: `cmake --build build --target ui-sandbox -j8`
3. **Launch new instance**: Use Bash tool with `run_in_background: true`:
   - Command: `cd /Volumes/Code/worldsim/build/apps/ui-sandbox && ./ui-sandbox --scene=<scene>`
   - Do NOT use shell `&` - it blocks waiting for output
4. **Take screenshot**: `curl -s http://127.0.0.1:8081/api/ui/screenshot > /tmp/screenshot.png`
   - The screenshot endpoint implicitly waits for the app to be ready
   - No sleep needed - curl will block until screenshot is captured

**Key Points:**
- **avoid sleeps** - the exit is blocking and screenshot waits for startup
- **NEVER skip the rebuild step!** An old instance will not reflect your changes
- **Always use `run_in_background: true`** for launching - shell `&` doesn't work properly

## Quick Reference

| When You Need... | Check... |
|------------------|----------|
| Current project status | `/docs/status.md` |
| Detailed implementation notes | `/docs/development-log/` (individual entry files) |
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
2. If more than 4 completed epics, remove oldest from status.md (archived in development log)
3. Create a new entry file in `/docs/development-log/entries/`:
   - Filename: `YYYY-MM-DD-epic-name-slug.md`
   - Include: summary, what was accomplished, files modified, technical decisions, next steps
   - See `/docs/development-log/README.md` for template

### Content Placement Rules

**status.md contains:**
- ✅ Checklists (Epic/Story/Task/Sub-task)
- ✅ Status indicators (ready/in progress/blocked/needs spec)
- ✅ Dependencies between epics
- ✅ Performance targets (as success criteria)
- ✅ Blockers & issues

**status.md does NOT contain:**
- ❌ Architectural decisions (→ development log entry if short, technical docs if long)
- ❌ Implementation rationale (→ development log entry or technical docs)
- ❌ Detailed technical discussion (→ technical docs)
- ❌ Long-form content or paragraphs (→ development log entry or technical docs)
- ❌ Historical "Recent Decisions" lists (→ development log entries)
- ❌ Notes section (→ notes go in the spec/documentation for each epic)

**Where architectural content goes:**
- **Short decisions** (<1 paragraph): Development log entry
- **Long decisions** (>1 paragraph): Relevant technical doc (e.g., `/docs/technical/architecture.md`)
- **Rationale and context**: Development log entry or technical docs
- **Performance targets**: Can stay in status.md as success criteria for epics

## After Significant Work

### Update `/docs/status.md`:
- Mark completed tasks with `[x]`
- Update "Blockers & Issues" if any
- Update "Last Updated" timestamp
- When Epic complete, move to "Recently Completed" and create a development log entry
- Notes belong in the spec/documentation for each epic, NOT in status.md

### Create development log entry (for significant work):
Create a new file in `/docs/development-log/entries/` with format `YYYY-MM-DD-topic-slug.md`:
- **Summary** - What was accomplished
- **Details** - Files created/modified, technical decisions made
- **Related Documentation** - Links to specs
- **Next Steps** - What comes next (if applicable)

See `/docs/development-log/README.md` for the full template.

**IMPORTANT**: Detailed implementation decisions, technical rationale, and design choices go in development log entries, NOT status.md. The development log is the detailed historical record; status.md is only the current snapshot of project state.


### Workflow: After Writing Code

1. **Open a PR**

2. **Switch back to the main branch**

## Worktree Management

**IMPORTANT: Never remove worktrees.** They're valuable for future work.

### When a PR is Merged

After a worktree's branch has been merged, switch it to a temporary placeholder branch:

```bash
# From the worktree directory (e.g., /Volumes/Code/worldsim-treeA):
git checkout -b worldsim-treeA-temp

# Or from main checkout:
git -C /Volumes/Code/worldsim-treeA checkout -b worldsim-treeA-temp
```

**Naming convention:** `{worktree-name}-temp` (e.g., `worldsim-treeA-temp`, `worldsim-treeB-temp`)

### Rules for Temp Branches

- **Never push temp branches** - they exist only as local placeholders
- **Never do work on temp branches** - always create a new feature branch first
- **Why this exists:** Git doesn't allow multiple worktrees to have the same branch checked out, so we can't have all worktrees on `main`

### Starting New Work in a Worktree

When reusing a worktree for new work:

```bash
# From the worktree directory:
git fetch origin
git checkout -b feature/new-feature-name origin/main
```

This creates a new feature branch based on latest main, ready for development.

## Plan File Archival

When a plan file is confirmed complete (has `# COMPLETE` on first line), archive it to the development log:

1. **Move the file** from `.claude/plans/` (or `~/.claude/plans/` for global plans) to `/docs/development-log/plans/`
2. **Rename with date prefix**: `YYYY-MM-DD-{original-name}.md`
   - Example: `uber-shader.md` → `2025-12-18-uber-shader.md`
3. **Create the plans directory** if it doesn't exist: `/docs/development-log/plans/`

This keeps completed plans discoverable alongside the development log while cleaning up the active plans folder.
