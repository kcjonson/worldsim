# Colonist AI System

**Status:** Design  
**Created:** 2024-12-04

## Overview

Colonists are autonomous agents who make their own decisions. The player influences behavior through priority settings and environmental design, but does not micromanage individual actions.

**Core Experience Goal:** Leave the game running and watch colonists take care of themselves. They eat when hungry, sleep when tired, work when able, and wander when idle.

## Decision Hierarchy

Colonists always evaluate what to do in this fixed order. Higher tiers interrupt lower tiers.

| Tier | Name | Trigger | Example |
|------|------|---------|---------|
| 1 | Panic | Active threat | Being chased by predator |
| 2 | Breakdown | Low mood | Mental break at <20% mood |
| 3 | Critical Needs | Need below ~10% | Bladder emergency |
| 4 | Player Control | Player takes control | Direct movement mode |
| 5 | Actionable Needs | Need below threshold | Hungry, seeks food |
| 6 | Work | Needs satisfied | Farming, building, hauling |
| 7 | Wander | Nothing to do | Idle exploration |

### Tier 1: Panic (Flee)

Colonist is actively threatened - chased by predator, caught in fire, etc.

**Behavior:** Seek safety by running toward shelter or other colonists. NOT random fleeing.

**Design Note:** RimWorld's panic is criticized because colonists flee randomly into more danger. Our panic must pathfind to actual safety.

### Tier 2: Breakdown

Mood has dropped below breakdown thresholds (35% / 20% / 5%).

**Behavior:** Varies by severity - wandering sad, binge eating, social withdrawal, destructive outbursts at extremes.

### Tier 3: Critical Needs

Any physical need hits critical threshold (~10%). Colonist immediately addresses it, bypassing all other priorities.

### Tier 4: Player Control

Player has taken direct control. See [Player Control System](./player-control.md).

### Tier 5: Actionable Needs

Physical needs have dropped below comfortable thresholds. Colonist seeks entities that fulfill the most urgent need.

**Key Constraint:** Colonist must KNOW where to find fulfillment. No omniscient pathfinding to unseen resources.

### Tier 6: Work

All needs comfortable. Colonist performs work based on personal priority settings.

### Tier 7: Wander

Nothing else to do. Colonist moves randomly through known areas, which:
- Looks natural (no frozen standing)
- Discovers new entities
- Creates social interaction opportunities

## The Task Queue

Each colonist has a visible queue showing current and pending tasks. This helps players understand behavior and debug issues.

**Shows:**
- Current task with progress
- Pending tasks in priority order
- Reason each task was selected
- Recent completed tasks

**Player Value:** "Why isn't Bob building?" → Check queue → "Oh, he's prioritizing farming and there are crops ready."

## Reservation

When a colonist decides to use a resource, they "reserve" it. Others skip reserved resources. This prevents two colonists walking to the same berry bush.

## MVP Scope

**Phase 1:** Tiers 5-7 only. Four needs (Hunger, Thirst, Energy, Bladder). One work type (Foraging). Basic queue display.

**Later:** Panic, Breakdown, Player Control, complex work types.
