# Debugging Strategy

This document defines the debugging protocol for complex bugs. Following this protocol is **mandatory** when invoked via `/debug <issue-name>` or when debugging issues that span multiple sessions.

> **Why this exists:** A 2-day debugging session (water detection bug) demonstrated that without a systematic approach, debugging becomes circular - the same approaches get tried repeatedly, false success is claimed, debug code gets deleted prematurely, and context gets wasted on screenshots.

---

## Quick Reference: The 5 Rules

1. **NEVER claim "ROOT CAUSE FOUND"** - Only user confirms fixes
2. **NEVER remove debug code** until user confirms the fix works
3. **Prefer logs over screenshots** - Logs are tiny, screenshots consume context
4. **One hypothesis at a time** - Test and invalidate before moving on
5. **Track what was tried** - Use tracking doc to prevent repetition

---

## Protocol Overview: Three Phases

The `/debug` command follows a three-phase approach:

| Phase | Purpose | Method |
|-------|---------|--------|
| **Phase 1: Scout** | Generate diverse hypotheses | 3 parallel agents explore independently |
| **Phase 2: Synthesize** | Rank and deduplicate | Main agent merges findings |
| **Phase 3: Investigate** | Deep single-hypothesis work | Disciplined protocol (the 5 rules) |

This approach prevents "tunnel vision" by ensuring multiple perspectives explore the problem before committing to deep investigation.

---

## When to Use This Protocol

**Invoke with:** `/debug <issue-name>`

**Use for:**
- Bugs persisting after initial investigation
- Multiple systems/files involved
- Coordinate, rendering, or timing-related issues
- Investigation carrying across context windows

---

## Session Initialization

When using full protocol:

1. **Create debug branch:** `git checkout -b debug/<issue-name>`
2. **Create tracking doc:** `/docs/debug-<issue-name>.md` (use template below)
3. **Commit tracking doc** before starting investigation

### Tracking Document Template

```markdown
# Debug Log: [Issue Name]

**Issue:** [One-line description]
**Started:** YYYY-MM-DD
**Status:** INVESTIGATING | AWAITING_USER_CONFIRMATION | CONFIRMED_FIXED

---

## Problem Summary
[Observed behavior vs expected behavior]

## Key Facts Established
[Numbered list - only CONFIRMED facts]

## Scout Agent Findings
**Explored by:** 3 parallel agents
**Total hypotheses generated:** N
**After deduplication:** M

## Hypotheses (Ranked)

### H1: [Name]
- **Status:** untested | testing | invalidated | promising
- **Suggested by:** [N agents / which focus areas]
- **Evidence for:**
- **Evidence against:**
- **Test approach:**

---

## Investigation Log

### Session N (YYYY-MM-DD)
**Goal:**
**Tried:**
**Learned:**
**Debug code added:** [file:line - description]
**Hypothesis status:**
**Next:**
```

---

## Phase 1: Multi-Agent Scouting

Before deep investigation, spawn **3 parallel agents** to explore the codebase independently. Each agent focuses on a different area:

| Agent | Focus Area | Looking For |
|-------|------------|-------------|
| **Agent 1** | Data flow & state | Race conditions, stale state, missing updates |
| **Agent 2** | Coordinates & math | Transform errors, off-by-one, unit mismatches |
| **Agent 3** | Timing & edge cases | Initialization order, async issues, boundary conditions |

### Why Multi-Agent Scouting Works

1. **Diverse perspectives** — Different agents frame the problem differently
2. **Avoids tunnel vision** — No single agent commits too early to one hypothesis
3. **Consensus signal** — If multiple agents independently suggest the same cause, it's likely correct
4. **Parallel exploration** — 3x coverage in the same wall-clock time

### Scout Agent Output Format

Each agent returns 2-3 hypotheses in this format:
```
### Hypothesis: [Name]
**Location:** [file:line]
**Evidence:** [specific code/behavior found]
**Why it could cause this:** [explanation]
**Quick test:** [how to validate/invalidate]
```

### Synthesis Rules

After scouts return:
1. **Collect** all hypotheses from all agents
2. **Deduplicate** — Merge substantially similar hypotheses
3. **Rank** by:
   - Number of agents who suggested it (consensus)
   - Strength of evidence cited
   - How well it explains observed behavior
4. **Record** all hypotheses in tracking doc (even low-ranked ones)
5. **Present** ranked list to user for selection

---

## Phase 3: Deep Investigation

Once the user selects a hypothesis to investigate, proceed with the disciplined single-hypothesis protocol.

### Hypothesis Status Transitions
```
untested -> testing -> invalidated (with evidence)
                   -> promising (needs user confirmation)
                   -> confirmed (USER says it's fixed)
```

### Rules
- **One at a time:** Don't jump to H2 until H1 is invalidated with evidence
- **Document why:** Record specific evidence that invalidates a hypothesis
- **No assumptions:** "I think this is fixed" != fixed

### Anti-Pattern (from water detection bug)
```
BAD:
Session 2: "ROOT CAUSE FOUND: Y-flip"
Session 3: "Reverted, that wasn't it"
Session 5: "ROOT CAUSE FOUND: Camera input"
Session 12: "ROOT CAUSE FOUND: Pure chunks"

GOOD:
H1 (Y-flip): INVALIDATED - caused inverse movement
H2 (Camera): INVALIDATED - coordinates verified matching
H3 (Pure chunks): PROMISING - awaiting user test
```

---

## Evidence Collection

### Prefer Logs Over Screenshots

| Logs | Screenshots |
|------|-------------|
| Text (tiny context) | Images (huge context) |
| Grep-able | Manual inspection |
| Precise values | Visual approximation |
| Always use first | Only when necessary |

### When Screenshots MAY Be Appropriate
- Initial bug report (once)
- Visual artifacts that can't be described in text
- Final confirmation **when user requests**

### Log Format
```cpp
// Use [SystemName] prefix for grep-ability
LOG_DEBUG(World, "[VisionScan] chunk(%d,%d) tile(%d,%d) groundCover=%s", ...);
LOG_DEBUG(Engine, "[Movement] entity %llu: pos=(%.1f,%.1f) -> target", ...);
```

### Comparison Logging
When debugging mismatches between systems:
```cpp
// Add BOTH logs, run together, compare output
LOG_DEBUG(Engine, "[PlacementCoord] chunk=(%d,%d) -> origin=(%.1f,%.1f)");
LOG_DEBUG(Renderer, "[RenderCoord] chunk=(%d,%d) -> origin=(%.1f,%.1f)");
```

---

## Code Preservation

### Debug Code Lifecycle
1. Add debug logging on debug branch
2. Commit: `debug: add [SystemName] logging for [issue]`
3. Test hypothesis
4. **IF invalidated:** Leave logging, document what it proved
5. **IF user confirms fix:** Then cleanup commit

### CRITICAL RULE
**Never delete debug code until user confirms fix.**

The tracking document records which files have instrumentation. This code stays until:
1. User explicitly says "the bug is fixed"
2. User has tested themselves
3. Only THEN create cleanup commit

---

## User Confirmation Gate

### Actions Requiring User Confirmation

| Action | Requires Confirmation |
|--------|----------------------|
| Mark hypothesis "confirmed" | YES |
| Remove debug instrumentation | YES |
| Mark bug "fixed" in status.md | YES |
| Merge debug branch to main | YES |

### How to Request Confirmation
```
I believe the fix is: [explanation]

**Please test by:**
1. [Action user should take]
2. [Expected result if fixed]
3. [What to look for if not fixed]

I will NOT mark this fixed until you confirm.
```

---

## Anti-Pattern Checklist

Before each debugging action, verify you are NOT:

- [ ] Repeating a previously tried approach (check session log)
- [ ] Claiming root cause without user confirmation
- [ ] Removing debug code before confirmation
- [ ] Using screenshots when logs would work
- [ ] Jumping to new hypothesis without invalidating current
- [ ] Assuming fix works without user test
