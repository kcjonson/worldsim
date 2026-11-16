# Multi-colonist crafting (follow-up spec)

**Status:** Partly answered. Section 1 is answered and built by [Task Claims](../technical/task-claims.md); section 2 is specified in [Interruptable Crafts](../technical/interruptable-crafts.md) and not built; its edge cases are answered there. Related: [known-issues-and-followups.md](../known-issues-and-followups.md).

## Context
Single-colonist crafting (one colonist provisions a bill of materials into a station, then crafts) was made reliable first (PR #240). Two or more colonists sharing work orders and stations raises coordination questions this spec must answer before implementation. The decisions below are the owner's; the open questions need a planning round.

## 1. Bill-of-materials fulfillment with multiple colonists (ANSWERED, built)
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

Refined 2026-11-13 ([Interruptable Crafts](../technical/interruptable-crafts.md)): the hold is on one unit at a time, from the crafter's first work on it until it is done, so a work order of several units is not one colonist's for its whole length; the craft's progress is saved on the job; and if the hold is released (direct player control, the crafter gone) the next crafter continues from the saved work.

### Edge cases (answered 2026-11-13)
The release path and timing are specified in [Interruptable Crafts](../technical/interruptable-crafts.md): the crafter holds the station from its first work on a unit until that unit is done, and the hold is released by direct player control, work started at another station, the crafter ceasing to exist, or the job being cancelled. Not built. Today a crafter holds the station only through the Slot claim its Craft task takes at selection; when it leaves to eat or sleep before the craft starts, the slot goes with the task and another colonist may take the station.
- What outranks the high finish-priority: danger, critical needs, player orders, and urgent needs ("very hungry", a threshold between seek and critical). A slightly hungry crafter stays on its craft. Other work never outranks it.

## Out of scope
Single-colonist behavior (PR #240, landed). The hold, its UI status, and the bench's work-in-progress look are in the interruptable crafts epic (WOR-822); station policy and the wider multi-colonist UI remain for a later pass (WOR-383).
