# Multi-colonist crafting (follow-up spec)

**Status:** Draft, needs a planning round. **Scope:** deferred. Single-colonist crafting is the current focus (PR #240); this captures the multi-colonist requirements for a later pass. Related: [known-issues-and-followups.md](../known-issues-and-followups.md).

## Context
Single-colonist crafting (one colonist provisions a bill of materials into a station, then crafts) is being made reliable now. Two or more colonists sharing work orders and stations raises coordination questions this spec must answer before implementation. The decisions below are the owner's; the open questions need a planning round.

## 1. Bill-of-materials fulfillment with multiple colonists (OPEN, to resolve)
When two or more colonists are available and there is a single work order:
- How is the bill of materials fulfilled across colonists? Does one colonist bring everything, or can the requirements be divided among several?
- How do we avoid DUPLICATING cut / harvest / haul tasks between colonists (two colonists both felling the same tree, or both hauling the same need)? Implies some claim or reservation so a need already in progress is not re-picked.
- Interaction with the existing model: a need resolves to one Haul/Harvest goal with finite `availableCapacity`; today a brief double-pickup self-corrects via `recordDelivery`, with no reservation by design. Decide whether multi-colonist provisioning needs real reservation, or the capacity model is enough.

These are open questions, not yet decided.

## 2. Workstation ownership during crafting (DECIDED)
Only one colonist performs the craft of a given work order. The model for an in-progress craft:

- **The workstation is marked TAKEN until that craft is done.** This is explicitly NOT RimWorld's model, which leaves a half-done recipe item that any colonist can resume. Here a craft in progress owns the station, and no other colonist can use it in the meantime. A long craft interrupted by sleep or eating is NOT handed off.
- a) **Assigned to one colonist.** The work order / station is bound to the colonist who started the craft.
- b) **Finishing is very high priority.** Completing an in-progress craft outranks starting new work by a wide margin, so if the assigned colonist leaves to sleep or eat, they come back and finish it before taking on anything new. The station stays reserved for that colonist across the interruption.
- c) **UI status.** The crafting UI shows the taken / in-progress / assigned-to state.
- d) **World visual state.** The workstation asset gets a distinct in-use visual (for example, the crafting bench shows work-in-progress sitting on it), so the state reads in the world, not just in the UI.

### Edge cases to resolve (planning round)
- Assigned colonist dies, is incapacitated, or is permanently drafted away: the station should free up and the work order become re-assignable. Define the release path.
- What, if anything, outranks the high finish-priority (immediate danger, mental break, starvation)? An in-progress craft should yield to genuine emergencies but otherwise win.
- When does the claim happen: at work-order start (provisioning begins) or at the first craft action (materials staged)? Leaning toward claiming the station at the first craft action, so provisioning can still parallelize across colonists per section 1, and the station is only locked once someone is actually crafting.

## Out of scope
Single-colonist behavior (the current PR #240 work). This spec is implemented in a later pass after that lands.
