---
description: Start a formal debugging session with the structured protocol
argument-hint: <issue-name>
---

# Formal Debug Session: $ARGUMENTS

You are entering a **formal debugging session**. You MUST follow the debugging protocol strictly.

## Read the Strategy First

@/docs/technical/debugging-strategy.md

## The 5 Rules (MANDATORY)

1. **NEVER claim "ROOT CAUSE FOUND"** - Only user confirms fixes
2. **NEVER remove debug code** until user confirms the fix works
3. **Prefer logs over screenshots** - Logs are tiny, screenshots consume context
4. **One hypothesis at a time** - Test and invalidate before moving on
5. **Track what was tried** - Update the tracking doc after each action

## Session Initialization

1. Create debug branch: `git checkout -b debug/$ARGUMENTS`
2. Check if tracking doc exists: `/docs/debug-$ARGUMENTS.md`
   - If exists: Read it and continue from last session
   - If not: Create it using the template from the strategy doc
3. Commit the tracking doc before any investigation

---

## Phase 1: Multi-Agent Hypothesis Exploration

**Before deep investigation, spawn parallel scout agents to generate diverse hypotheses.**

Use the Task tool to launch **3 parallel agents** (subagent_type: "Explore") with the following prompt template for each:

```
You are a debugging scout for issue: $ARGUMENTS

Your job is to explore the codebase and generate 2-3 plausible hypotheses for the root cause.

For each hypothesis:
1. Name it clearly (e.g., "H1: Race condition in chunk loading")
2. Provide specific evidence from the code (file paths, line numbers)
3. Explain why this could cause the observed behavior
4. Suggest a quick test to validate/invalidate it

Constraints:
- Spend ~5 minutes exploring, don't go deep
- Focus on DIFFERENT areas than obvious ones
- Look for edge cases, timing issues, coordinate transforms, off-by-one errors
- Be specific - cite actual code, not vague possibilities

Return your hypotheses in this format:
### Hypothesis 1: [Name]
**Location:** [file:line]
**Evidence:** [what you found]
**Why it could cause this:** [explanation]
**Quick test:** [how to validate]
```

**IMPORTANT:** Launch all 3 agents in a SINGLE message with multiple Task tool calls. Give each agent a slightly different exploration focus:
- Agent 1: Focus on data flow and state management
- Agent 2: Focus on coordinate systems, transforms, and math
- Agent 3: Focus on timing, initialization order, and edge cases

## Phase 2: Hypothesis Synthesis

After all scout agents return, synthesize their findings:

1. **Collect all hypotheses** from the 3 agents
2. **Deduplicate** - merge similar hypotheses
3. **Rank by likelihood** based on:
   - Strength of evidence cited
   - How well it explains the observed behavior
   - Whether multiple agents independently suggested similar causes
4. **Add to tracking doc** - Record all hypotheses in `/docs/debug-$ARGUMENTS.md`

Present the ranked list to the user:
```
## Scout Agent Findings

**Explored by:** 3 parallel agents
**Total hypotheses generated:** N
**After deduplication:** M

### Ranked Hypotheses:

1. **[Name]** (suggested by N agents)
   - Evidence: ...
   - Confidence: HIGH/MEDIUM/LOW

2. **[Name]** ...

Which hypothesis should we investigate first?
```

---

## Phase 3: Deep Investigation (Existing Protocol)

Once user selects a hypothesis, proceed with the disciplined single-hypothesis protocol:

### Before EVERY Action

Ask yourself:
- [ ] Am I repeating something already tried? (Check session log)
- [ ] Am I about to claim "root cause"? (Stop - only user confirms)
- [ ] Am I about to delete debug code? (Stop - user must confirm fix first)
- [ ] Can I use logs instead of a screenshot? (Use logs)

### How to Conclude

When you believe you have a fix:

```
I believe the fix is: [explanation]

**Please test by:**
1. [Action user should take]
2. [Expected result if fixed]
3. [What to look for if not fixed]

I will NOT mark this fixed until you confirm.
```

---

Begin the debug session for: **$ARGUMENTS**
