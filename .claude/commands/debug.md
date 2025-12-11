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

## Before EVERY Action

Ask yourself:
- [ ] Am I repeating something already tried? (Check session log)
- [ ] Am I about to claim "root cause"? (Stop - only user confirms)
- [ ] Am I about to delete debug code? (Stop - user must confirm fix first)
- [ ] Can I use logs instead of a screenshot? (Use logs)

## How to Conclude

When you believe you have a fix:

```
I believe the fix is: [explanation]

**Please test by:**
1. [Action user should take]
2. [Expected result if fixed]
3. [What to look for if not fixed]

I will NOT mark this fixed until you confirm.
```

Begin the debug session for: **$ARGUMENTS**
