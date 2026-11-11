# Multi-colonist crafting (follow-up spec)

**Status:** Draft, needs a planning round. **Scope:** deferred. Single-colonist crafting is the current focus (PR #240); this captures the multi-colonist requirements for a later pass. Related: [known-issues-and-followups.md](../known-issues-and-followups.md).

## Context
Single-colonist crafting (one colonist provisions a bill of materials into a station, then crafts) is being made reliable now. Two or more colonists sharing work orders and stations raises coordination questions this spec must answer before implementation. The decisions below are the owner's; the open questions need a planning round.

## 1. Bill-of-materials fulfillment with multiple colonists (OPEN, to resolve)
When two or more colonists are available and there is a single work order:
- How is the bill of materials fulfilled across colonists? Does one colonist bring everything, or can the requirements be divided among several?
- How do we avoid DUPLICATING cut / harvest / haul tasks between colonists (two colonists both felling the same tree, or both hauling the same need)? Implies some claim or reservation so a need already in progress is not re-picked.
- Interaction with the existing model: goal counters (`availableCapacity`, the goal-level delivery credit) are gone; a root's remaining need is read live from what has landed at its destination, less what other tasks are bringing.

Answered in [Task Claims](../technical/task-claims.md): requirements divide across colonists, each cut or fetch records in-flight and inbound claims against the craft goal so no need is picked twice, and the capacity model alone was not enough.

## 2. Workstation ownership during crafting (DECIDED)
Only one colonist performs the craft of a given work order. The model for an in-progress craft:

- **The workstation is marked TAKEN until that craft is done.** This is explicitly NOT RimWorld's model, which leaves a half-done recipe item that any colonist can resume. Here a craft in progress owns the station, and no other colonist can use it in the meantime. A long craft interrupted by sleep or eating is NOT handed off.
- a) **Assigned to one colonist.** The work order / station is bound to the colonist who started the craft.
- b) **Finishing is very high priority.** Completing an in-progress craft outranks starting new work by a wide margin, so if the assigned colonist leaves to sleep or eat, they come back and finish it before taking on anything new. The station stays reserved for that colonist across the interruption.
- c) **UI status.** The crafting UI shows the taken / in-progress / assigned-to state.
- d) **World visual state.** The workstation asset gets a distinct in-use visual (for example, the crafting bench shows work-in-progress sitting on it), so the state reads in the world, not just in the UI.

### Edge cases to resolve (planning round)
The release path and claim timing are specified as the craft binding in [Task Claims](../technical/task-claims.md): bound at the first craft action, released on job completion or cancellation, crafter death, incapacitation, direct player control, or a craft started elsewhere.
- What, if anything, outranks the high finish-priority (immediate danger, mental break, starvation)? An in-progress craft should yield to genuine emergencies but otherwise win.

## Out of scope
Single-colonist behavior (the current PR #240 work). This spec is implemented in a later pass after that lands.
