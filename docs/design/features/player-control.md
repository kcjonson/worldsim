# Player Control System

**Status:** Design  
**Created:** 2024-12-04

## Overview

Players can interact with colonists in multiple ways, from passive observation to direct movement control. The game plays itself, but players can intervene when needed.

## Control Modes

### Observation Mode (Default)

Player watches the colony. Click colonists or entities to view information.

**Colonist Behavior:** Fully autonomous

### Command Mode

Player has selected a colonist and can issue orders by clicking things in the world.

**How it works:**
1. Click colonist to select
2. Click entity in world → context menu appears
3. Select action → added to colonist's task queue
4. Colonist executes when able

**Examples:**
- Click berry bush → "Harvest"
- Click ground → "Move here"
- Click storage → "Haul items here"

**Queue Behavior:** Player commands go to FRONT of queue (high priority). Colonist continues autonomous behavior between commands.

### Direct Control Mode

Player takes full control using keyboard to move colonist around.

**How it works:**
1. Select colonist
2. Click "Control" button
3. Use WASD/arrows to move directly
4. Press E near entities for context menu
5. Press ESC to release control

**Colonist Behavior:** Task queue SUSPENDED. AI paused. Colonist moves exactly where player directs.

**Primary Use Case:** Exploration. Player walks colonist into unknown territory to discover resources.

## Context Menus

When clicking entities (or pressing E near them in direct control), a menu shows available interactions.

**Berry Bush:**
- Harvest (if mature)
- Inspect

**Storage Container:**
- Open (view contents)
- Prioritize hauling here

**Another Colonist:**
- Go talk to
- Follow
- View info

**Ground (outdoor):**
- Move here
- Relieve self (if needed)

## Critical Interrupts

Even under player control, some things interrupt:

**Panic:** Threat appears. Warning shown. If player doesn't respond, auto-flee engages.

**Critical Bladder:** Warning shown. If ignored, accident occurs.

**Breakdown:** Control released, break behavior plays out.

## The "Now What?" Question

When player releases direct control or finishes issuing commands, what happens?

**Answer:** Suspended task queue resumes. Colonist returns to autonomous behavior, executing any queued commands first.

If colonist was mid-task when controlled, they may return to finish it or re-evaluate based on current needs.

## UI Indicators

**Selected Colonist:** Info panel visible, can issue commands

**Controlled Colonist:** Clear visual indicator on screen, "Press ESC to release" reminder, queue shows "SUSPENDED"

**Context Menu:** Appears on entity click/E-press with available actions

## MVP Scope

**Phase 1:** Observation only. Select colonist to view info. Watch autonomous behavior.

**Phase 2:** Direct Control. WASD movement, E for interaction, basic context menus.

**Phase 3:** Command mode. Click-to-order, queue management.
