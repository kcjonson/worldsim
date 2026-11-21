# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

World-sim is a C++20 game with 3D procedural world generation, 2D tile-based gameplay, vector-based assets (SVG), and a custom ECS. Built as a monorepo of modular libraries. For full details, see [README.md](README.md).

**Critical Architectural Decisions:**
- Vector assets (SVG) with procedural variation → `/docs/technical/vector-graphics/INDEX.md`
- Custom ECS in sim (`libs/sim/ecs/`) → `/docs/technical/cpp-coding-standards.md#ecs`
- Roll our own core systems where appropriate

## Project Management: Specboard

Project management lives in **Specboard** (board: Worldsim), over the Specboard MCP. Run `/whats-next` to see current work, pick up tasks, and drive the dev lifecycle (start task, note outcomes on completion, link the branch, open the PR, mark it for review).

The outcome of a planning session is an **epic with an attached requirements doc** (linked via the epic's spec doc path), not code. Plan, write the spec, create and link the epic, then implement in a later pass.

File bugs and chores directly as you hit them. Full workflow: the `specboard:whats-next` plugin skill; personal preferences in the claude-config repo's `specboard.md`.

## Development Standards

**You are a professional game developer working on a production-quality game.**

### Workflow: Before Writing Code

- Work on a branch off `main`. A worktree session already has its own; in the main checkout, create one.
- Read the Specboard item you are working on (`/whats-next`) and its linked spec, plus the technical design docs and product specs it touches.
- Keep explicit todos and work items in Specboard: child items for tracked work, the item's checklist for scratch todos. Update the item before and after doing any work.
- If you lack information for a professional decision, ask the user rather than assume.

### Complex Bug Investigation

For bugs involving multiple systems, coordinate/rendering issues, or multi-session investigation:

**Use the `/debug <issue-name>` command** to start a formal debugging session.

This activates the protocol from `/docs/technical/debugging-strategy.md` which uses:
- **Multi-agent scouting** — 3 parallel agents explore different hypothesis areas
- **Hypothesis synthesis** — Rank by consensus and evidence before deep investigation
- **Disciplined investigation** — One hypothesis at a time, user-only confirmation
- **Evidence preservation** — No premature removal of debug code, logs over screenshots


### Code Quality

- Profile before optimizing.
- Follow the dependency hierarchy strictly.
- Document non-obvious decisions.
- No hacks, temporary code, or "good enough for now".
- Don't give time or cost estimates for tasks: you can't estimate them accurately. Describe the work and its deliverables instead.

### Documentation Over Existing Code

When evaluating code style, conventions, or PR feedback, `/docs/technical/cpp-coding-standards.md` is the source of truth. Existing code may be legacy that violates it; where the two disagree, the doc is right.

Example: If you see `m_` prefixes on member variables in older code, but the coding standards say "camelCase with no prefix" - the standards are correct, the existing code is legacy that needs cleanup.

### Clean Code: No Legacy, No Fallbacks

Delete old code when you replace it, and never leave a legacy fallback.

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

#### Anti-Patterns

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

## Documentation System

### When Asked "What are we working on?" or "Where are we?"
→ Run `/whats-next` (Specboard, board: Worldsim)

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

**Scripts and tooling:**
- New scripts and tooling are Node (ESM, `.mjs`, no dependencies unless there is a `package.json` for them). No Python.

**Engine libraries (salal):** `foundation` and `renderer` come from github.com/kcjonson/salal via FetchContent, pinned by `GIT_TAG` in the root CMakeLists.txt. To build against the sibling checkout instead of the tag: `cmake -S . -B build -DFETCHCONTENT_SOURCE_DIR_SALAL=C:/Users/kevin/Code/salal`. It is cached in `build/` until `-UFETCHCONTENT_SOURCE_DIR_SALAL`. Whatever branch that checkout has is what builds, so a worldsim PR must work against a tagged salal before it is marked ready. On GCC and Clang, `-ffp-contract=off` is inherited from `salal::foundation` (PUBLIC, the deterministic math needs it); don't add it again here. salal's base shader is assembled into `world-view` at build time with the game's hooks (`libs/world-view/world/rendering/hooks/`, one so far: the groundcover deform), checked by `glslangValidator` from the vcpkg `glslang[tools]` host dependency, and compiled in; salal's `docs/shader-hooks.md` is the contract. `base_shader::source()` is what `Primitives::init` takes.

**Game libraries:** `worldgen` (the planet pipeline), `world` (chunks, tiles, terrain and the world camera), `assets` (definitions, registries, Lua and placement), `sim` (the ECS and its systems, nav, vision and construction) and `world-view` (the world's rendering, which draws what the rest hold). Each folder is one CMake target, with its include root at the folder (`libs/sim/ecs/...` is `<ecs/...>`). `world` and `sim` link neither salal's renderer nor its ui; `assets` links renderer for SVG tessellation and asset thumbnails, so sim still reaches it through assets.

**Naming:**
- Classes/Structs/Enums: PascalCase (`Shader`, `TextureManager`)
- Functions/Methods: camelCase (`loadTexture()`, `isValid()`)
- Variables/Members: camelCase, no prefix (`frameCount`, `deltaTime`)
- Constants: `k` prefix (`kMaxTextures`)
- Every name is whole readable words. No one- or two-letter names or namespace aliases (`P::`, `td::`); loop indices and x/y/z are the exceptions, and so are CSS and SVG's own names (the `a` to `f` of a transform matrix), which outrank this rule

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
- Don't change `LOG_DEBUG` to `LOG_INFO` to see a log: debug logs already show in dev tools through the HTTP log server.

**Testing:** `/docs/testing/README.md` is the strategy. Look for actual regressions, never pixel
changes: no golden images or screenshot diffs. Before committing, run the relevant happy-path
scenarios (`/docs/testing/scenarios/`) as a manual regression pass with the cheapest subagent
fleet that can, and add steps when a feature lands. Keep unit tests bounded: check for duplicates
before adding.

**⚠️ Timed perf: check the machine first, and distrust a worse result.** This machine runs several
sessions at once and its CPU and GPU are often maxed by other work. Sessions have burned days on
captures taken under load, then on re-runs chasing regressions that were never there.
- A session may time its own work when the change calls for it, scoped to what it touched (the view
  and zooms that could move, not the whole suite by reflex), and only when
  `node scripts/machine-load.mjs` answers `idle` or `mostly-idle`. Run the check before the capture
  and again after. On `busy`, don't time anything; the nightly batch will.
- Capture with `node scripts/perf.mjs capture` (scope it with `--zooms` and `--no-scroll`) and take
  the verdict from `node scripts/perf.mjs compare`, not from reading timings by eye.
- Know what the run costs before starting it: every capture launches the game on Kevin's desktop
  and holds the machine. Decide the launches up front and stop when the question is answered.
- Even with `main`, or faster, is a good result, loaded machine or not. Record it and move on. No
  repeat to confirm.
- Worse is ambiguous: it could be the change, or the machine. Unless both checks read `idle`, a
  worse number is not a finding. Don't loop on re-runs, bisect, or start investigating. One more run
  is fine once the check reads `idle`; otherwise write "worse under load, unconfirmed" in the PR's
  `Perf:` line and the nightly batch settles it. Worse with both checks `idle` is real: act on it.
- No new baselines from a run with any activity on the machine. A number goes into a doc or ledger
  for later comparison only when both checks read `idle`. Faster under load means "no regression",
  not a speedup to quote.
- Put a `Perf:` line in the PR body: what could move, which views or zooms would show it, and what
  was measured under which verdict, or `Perf: none`. Then merge on green CI as usual.
- The nightly batch times `main` in the early morning once the machine is idle, and opens a draft
  revert for a PR behind a major regression. Agents run it and decide what it covers; Kevin never
  asks for a perf run. `/docs/testing/README.md` has the rule in full and
  `/docs/testing/nightly-perf-batch.md` the batch, which the `/perf-nightly` skill runs.

**Visual Verification:**
- When user reports visual issues ("X doesn't appear", "layout is wrong")
- Capture screenshot via HTTP endpoint to see actual output
- Outside a regression pass, capture screenshots only when they answer a question; don't take them by reflex
- See `/docs/technical/observability/ui-inspection.md` for the endpoints

**Sandbox Control:**
- The dev server is salal's (`app::DevServer`), in Debug and RelWithDebInfo builds only; a Release build has none unless configured with `-DSALAL_DEV_SERVER_IN_RELEASE=ON` (for profiling). The ui tree and lint come from the top scene's salal ui context; world-sim's game scene adds the `/api/dev/<verb>` routes, the `/api/state` views and the `camera` control.
- Start: `cd build/apps/ui-sandbox && ./ui-sandbox --background` (on Windows the Multi-Config build puts it in `build/apps/ui-sandbox/<config>/`)
- **IMPORTANT: Every launch an agent makes passes `--background`**, and every world-sim launch passes `--no-settings` too (`world-sim --background --no-settings`; ui-sandbox and asset-manager have no settings file). World-sim saves the interface size, the window's size and vsync to `%APPDATA%\kcjonson\world-sim\settings.json` at every exit, shared by every worktree's build and Kevin's own play; `--no-settings` neither reads nor writes it (default window, 100% interface), so a run that stops midway or resizes leaves nothing behind. Scenario 17 tests that the settings are saved and is the one run on the real file. The `--background` flag applies to whichever app (world-sim, ui-sandbox, asset-manager) and whatever it is for: a regression pass, a visual check, a screenshot, a subagent's verification. The window opens behind every other window and unfocused, so it never lands on top of Kevin's work or takes his keyboard; it is still an ordinary window, on the taskbar, drawn every frame, and the dev server (screenshots, injected input, state) works the same. Two launches go without either flag, and only these: one made for Kevin to test something by hand, and one Kevin asked for. When in doubt it is a background launch. A subagent's brief says so too. The scripted exception is a perf capture, which `scripts/perf.mjs` launches in the foreground itself (a covered window isn't presented the same way, so its timings wouldn't compare); never add or remove the flag there by hand.
- Control: `curl "http://127.0.0.1:8090/api/control?action={action}"`
- Actions: `exit`, `scene&scene=name`, `pause`, `resume`, `reload`, `vsync&value=0|1`, `step&ticks=n` (while paused; it advances nothing in worldsim, where no scene has a fixed tick, so a step is one more rendered frame); world-sim's game scene adds `camera` (the control sets the camera, `what=camera` reads it). A control answers once it has run, at the next frame; `scene` and `reload` answer before their switch applies later that frame. In world-sim, `scene&scene=game` and a `reload` in the game go through the loading scene first (the game scene is built directly only when a loaded world is waiting, which is the loading scene's own hand-over): `what=scene` names `gameloading` with its progress, the game's own state views are a 404 until the load ends, and a `reload` loads a Quick Start world whatever world was running. An unknown action, or a scene nothing was added as, is a 404
- Input injection: `curl "http://127.0.0.1:8090/api/input?ev=click,x,y"`: synthetic input in the window's logical pixels (a screenshot is in framebuffer pixels, twice the logical size on a HiDPI display, so halve its coordinates there; on Windows they're the same). Event types `move`/`down`/`up`/`click`/`scroll,x,y,delta` with optional button `left|right|middle`, `keydown,<key>`/`keyup,<key>`, and `char,<codepoint>`; repeat `ev=` to batch a sequence in one request, a whole gesture included: the events reach the ui in order, each press and release at its own position (`ev=down,100,100&ev=move,300,300&ev=up,300,300` is a drag, `ev=click,x,y&ev=char,104` clicks then types). Injected events go the way real input does: keys and characters reach the focused ui component (`keydown,tab` moves focus), a scroll is resent until something takes it (in the game, one the ui doesn't take zooms the camera), and while the app is paused it all waits for resume. A malformed event is a 400 and queues nothing. A pointer event moves the app's pointer to `x,y` until the real cursor moves over the window or a real mouse button or scroll event lands, so hover state holds for a later request (`ev=move,x,y` then a separate screenshot works). Since salal v0.41.1 an injected move also sets the window's pointer-in-window flag, so hover-driven things (the placement ghost, the drawing tools' preview) follow it. Input is in the window's logical px while `/api/ui/tree` and `/api/ui/lint` are in ui px: multiply a tree position by `layoutScale` from `/api/state?what=uiscale` to get where to point.
- Scene state: `curl "http://127.0.0.1:8090/api/state?what=scene"` is `{"scenes":[{"name":...,"state":...}]}`, top first; read `scenes[0].state`. Any other `what` is a view the running scene registered (404 otherwise)
- Screenshot: `curl http://127.0.0.1:8090/api/ui/screenshot > screenshot.png`
- Default ports: ui-sandbox 8090, world-sim 8081, asset-manager 8070. Several worktrees run games at the same time, so in a worktree pass `--http-port <port>` with a port of your own, picked from the worktree name so it stays the same across relaunches, and use it in place of the default in every curl here.
- One instance per port: a second launch on a busy port opens its window, finds the port busy, and exits at once (logged). Sessions in other worktrees may be running their own instance on another port; close only yours, by its port.
- **Close the game as soon as your task is done** - this is a native game rendering every frame, not a web app; an idle instance burns GPU/CPU continuously. When you finish verifying, immediately shut it down: `curl "http://127.0.0.1:8090/api/control?action=exit"` (8081 for world-sim). Only leave it running if the user asked to test it themselves.

**Asset Manager (use this to test/preview world assets):**
- A designer GUI (`apps/asset-manager`) that renders every asset through the game's real pipeline. Use it to verify new/edited flora, rocks, and other assets instead of dev-spawning them in world-sim.
- Build: `cmake --build build --config Debug --target asset-manager`. Run: `build/apps/asset-manager/Debug/asset-manager.exe --background` (dev server on port **8070**, same `/api/ui/screenshot`, `/api/ui/tree`, `/api/ui/lint`, `/api/input`, `/api/control` as the game).
- Shows a searchable category tree of asset thumbnails, a header with total count + validation error/warning counts, a per-asset detail pane (large preview + full XML + that asset's validation issues), and a **Reload** button to hot-reload after editing assets (no restart). Building the app target after an asset-only edit restages `assets/` beside the exe (pruning deleted files) with no relink, for every app that stages assets (`cmake/stage-assets.cmake`).
- Surfaces SVG tessellation/validation problems and lets you eyeball art far faster than spawning + cropping screenshots. Close it when done (`curl "http://127.0.0.1:8070/api/control?action=exit"`).

**Testing Visual Changes**
When you make code changes and need to verify visually:
1. **Kill old instance** (if running): `curl "http://127.0.0.1:8090/api/control?action=exit"`
   - This is a **blocking call** - it returns only after the scenes have exited, GL is torn down and the port is free
   - Response: `{"status":"ok","action":"exit","shutdown":"complete"}`
   - When you receive the OK response, proceed immediately (NO sleep needed)
   - If no instance is running, curl will fail with connection refused - that's fine, proceed
2. **Rebuild**: `cmake --build build --target ui-sandbox -j8`
3. **Launch new instance**: Use Bash tool with `run_in_background: true`:
   - Command: `cd build/apps/ui-sandbox && ./ui-sandbox --scene=<scene> --background`
   - Do NOT use shell `&` - it blocks waiting for output
4. **Verify layout (after UI changes)**: `curl http://127.0.0.1:8090/api/ui/tree` for element bounds, `curl http://127.0.0.1:8090/api/ui/lint` for invariant violations - expect `"count":0` before screenshotting. Don't verify coordinates by eye. Both walk the scene's `getUiRoots()`: a scene that draws without components (most ui-sandbox demos) answers an empty tree, and its `"count":0` checks nothing.
5. **Take screenshot**: `curl -s http://127.0.0.1:8090/api/ui/screenshot > /tmp/screenshot.png`
   - The screenshot endpoint implicitly waits for the app to be ready
   - No sleep needed - curl will block until screenshot is captured

## Quick Reference

| When You Need... | Check... |
|------------------|----------|
| Current project status | Specboard (`/whats-next`) |
| Detailed implementation notes | `/docs/development-log/` (individual entry files) |
| Game design & requirements | `/docs/design/INDEX.md` |
| How to add a new library | `/docs/workflows.md` |
| Project structure | `/docs/technical/monorepo-structure.md` |
| Build commands | `README.md` |
| Coding standards | `/docs/technical/cpp-coding-standards.md` |
| Engine patterns to use | `/docs/technical/INDEX.md` (Engine Patterns section) |

## After Significant Work

### Update Specboard:
- Note the outcome on the item, link the branch and PR, and set its status
- File follow-ups and bugs found along the way as their own items
- Blockers go on the item as blockers, with a reason
- When an epic is complete, create a development log entry
- Notes that outlive the work belong in the epic's spec, not in the item's activity log

### Create development log entry (for significant work):
Create a new file in `/docs/development-log/entries/` with format `YYYY-MM-DD-topic-slug.md`:
- **Summary** - What was accomplished
- **Details** - Files created/modified, technical decisions made
- **Related Documentation** - Links to specs
- **Next Steps** - What comes next (if applicable)

See `/docs/development-log/README.md` for the full template.

Detailed implementation decisions, technical rationale, and design choices go in development log entries, not Specboard items. The development log is the detailed historical record; Specboard is the current state of the work.


### Workflow: After Writing Code

Open a PR. In the main checkout, switch back to `main` afterwards; a worktree keeps its branch.

## Worktree Management

Worktrees are disposable scratch space, not permanent fixtures. Remove a worktree when the session that created it ends; its branch and commits stay in the repo after removal, so nothing is lost. This goes double for sub-agent worktrees (`isolation: "worktree"`): they are per-agent and should be removed as soon as the agent finishes, never left to accumulate under `.claude/worktrees/`.

- Every worktree lives under the main checkout's `.claude/worktrees/`, never beside it or in the home directory. ccache's `base_dir` is the main checkout, so only worktrees under it share compile-cache hits. Each keeps its own `build/` (and `node_modules` where an app has one); the planet below is the one shared file.
- The one permanent worktree is `.claude/worktrees/perf-nightly`, where the nightly perf batch builds and runs. Never remove it, and don't work in it.
- Clean up with `git worktree remove <path>`, then `git worktree prune`. It refuses a worktree with uncommitted changes unless you pass `--force`; leave those in place and surface them rather than discarding unsaved work.
- Removing a worktree keeps its branch. If there's uncommitted work worth saving, commit it to the branch before removing.

### Sharing the pre-generated world across worktrees

The quickstart planet (`planets/quickstart.wsplanet`, ~1.3 GiB) is gitignored and baked per worktree by the `quickstart-planet` CMake target (~90 s; n=2048, seed 424242). A fresh worktree has no `planets/` dir, so it re-bakes on first build and keeps its own 1.3 GiB copy. Don't let it. Point the worktree's `planets/` at the main checkout's via a directory junction (Windows, no admin or Developer Mode needed):

```powershell
New-Item -ItemType Junction -Path "<worktree>\planets" -Target "C:\Users\kevin\Code\worldsim\planets"
```

The game loads a copy staged beside the exe at `build/apps/world-sim/<config>/assets/planets/quickstart.wsplanet` (that staged copy, via `findResource`, not the source, is what's read). With the junction in place the build stages from the shared source instead of re-baking. To skip even the per-config staged copy, hardlink it (same volume, no admin):

```powershell
New-Item -ItemType HardLink -Path "<worktree>\build\apps\world-sim\RelWithDebInfo\assets\planets\quickstart.wsplanet" -Target "C:\Users\kevin\Code\worldsim\planets\quickstart.wsplanet"
```

Caveat: the planet is format-versioned (`kFormatVersion`). Sharing is safe across branches that only change game logic. A branch that changes the worldgen binary format won't load the shared planet (it fails gracefully to a config error, no corruption) and needs its own fresh bake, so junction only when the branch's worldgen format matches main's.

**⚠️ NEVER build the `quickstart-planet` target from a junctioned worktree.** Its custom command declares `planets/quickstart.wsplanet` as OUTPUT with `DEPENDS worldgen-cli`, so a freshly compiled worldgen-cli (always the case in a new worktree) outdates the shared file and ninja re-runs the bake — which truncates the 1.3 GiB shared planet THROUGH the junction, and an interrupted bake deletes it outright (this destroyed the shared planet on 2026-07-03). Stage the per-config copy with the hardlink above instead. Restore recipe: from the main checkout, `cmake --build build --config RelWithDebInfo --target quickstart-planet` (~80 s).

## Plan File Archival

When a plan file is confirmed complete (has `# COMPLETE` on first line), archive it to the development log:

1. **Move the file** from `.claude/plans/` (or `~/.claude/plans/` for global plans) to `/docs/development-log/plans/`
2. **Rename with date prefix**: `YYYY-MM-DD-{original-name}.md`
   - Example: `clipping-system.md` → `2025-12-18-clipping-system.md`
3. **Create the plans directory** if it doesn't exist: `/docs/development-log/plans/`

This keeps completed plans discoverable alongside the development log while cleaning up the active plans folder.
