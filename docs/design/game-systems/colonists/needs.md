# Colonist Needs System

**Status:** Design  
**Created:** 2024-12-04  
**MVP Status:** See [MVP Scope](../mvp-scope.md) — Hunger, Thirst, Energy, Bladder included in Phase 1

---

## Overview

Colonists have physical and emotional needs. Some needs drive behavior (actionable), others contribute to mood. This document defines ALL need mechanics in one place.

---

## Needs are data

A need is a data entry: its name, how its level moves (draining with time, or set by the colonist's surroundings), its seek, urgent, and critical levels, its mood cost, and what meets it, a chain of steps the colonist carries out or work another colonist does for it. Each kind of creature lists the needs it has, so a cow can need milking and a colonist cannot, and a mod adds a need with a file. The entries and the chains behind them are specified in [Queries and chains](../../../technical/queries-and-chains.md#needs).

## Actionable Needs

These create tasks. When they drop below threshold, colonist seeks fulfillment.

### Hunger

| Property | Value |
|----------|-------|
| Seek Threshold | ~50% |
| Critical Threshold | ~10% |
| Failure Effect | Starvation damage |
| Fulfilled By | Eating food (Edible capability) |

Different foods restore different amounts. Food quality + colonist preferneces affects mood.

**eating effects need to use bathroom**


### Thirst

| Property | Value |
|----------|-------|
| Seek Threshold | ~50% |
| Critical Threshold | ~10% |
| Failure Effect | Dehydration damage |
| Fulfilled By | Drinking water (Drinkable capability) |

Clean vs dirty water affects health. **Drinking accelerates bladder need** (biological loop).

### Energy (Sleep)

| Property | Value |
|----------|-------|
| Seek Threshold | ~30% |
| Critical Threshold | ~10% |
| Failure Effect | Collapse where standing |
| Fulfilled By | Sleeping (Sleepable capability) |

Sleep quality affects recovery rate. Better sleeping spots = faster recovery + mood bonus.

### Bladder / Poop

| Property | Value |
|----------|-------|
| Seek Threshold | ~30% |
| Critical Threshold | ~10% |
| Failure Effect | Accident |
| Fulfilled By | Toilet capability OR outdoor spot |

**Accelerated by drinking / eating.** Each drink / food adds to bladder / poop need.

**Outdoor Relief Rules:**
- Must be outdoors
- NOT adjacent to water sources
- PREFER near existing waste (clustering)
- PREFER away from high-traffic areas
- Creates "Bio Pile" entity

**Accident Consequences:**
1. Major mood penalty (embarrassment)
2. Hygiene drops to zero
3. Social penalty if witnessed
4. Creates Bio Pile at location (requires cleanup)

### Hygiene

| Property | Value |
|----------|-------|
| Seek Threshold | ~40% |
| Critical Threshold | ~15% |
| Failure Effect | Continuous mood/social penalty |
| Fulfilled By | Washing (Washable capability) |

Drops to zero on bathroom accident. Low hygiene = others like you less.

A colonist washes at water the way it drinks: below the seek threshold it walks to the nearest shore it remembers and washes there, which restores Hygiene fully. Every shore point is both Drinkable and Washable, so baths and showers are more Washable sources for the same search.

### Recreation

| Property | Value |
|----------|-------|
| Seek Threshold | ~30% |
| Critical Threshold | ~10% |
| Failure Effect | Work efficiency drops, mood penalty |
| Fulfilled By | Recreation activities, socializing; at the baseline, enjoying nature, which takes a long time for a little gain and keeps what it gained when cut short |

Decays while working, recovers while idle or recreating.

**Implementation Note:** Tracked in code now; AI does not seek recreation yet (no fulfillment sources).

**MVP Status:** Deferred to Phase 2

### Temperature

| Property | Value |
|----------|-------|
| Comfort Band | Colonist-specific range (e.g., 15-25°C)
| Effects | Outside band increases Energy decay, reduces sleep recovery, and can apply mood/health penalties when extreme |
| Fulfilled By | Warmer/colder locations, shelter/indoors, fires, clothing (future) |

Temperature is a need: a colonist needs to be neither freezing nor too hot. It does not drain with time the way hunger does. Its level is set by where the colonist is and what the weather and the hour are doing, so it falls when they are outside their comfort band and recovers when they are back inside it. A colonist meets it by going somewhere warmer or cooler: shelter, a fire, shade, and later clothing.

Until the temperature of a place exists in the sim, the need holds at full and the AI does not act on it.

### Flee

| Property | Value |
|----------|-------|
| Trigger | Active threat |
| Fulfilled By | Reaching safety |

Highest priority. See Panic in [AI Behavior](./ai-behavior.md).

**MVP Status:** Deferred (no threats in MVP)

---

## Mood System

Mood is a composite score from all contributors. Range 0-100%.

### Mood Contributors

Mood has two parts ([Mood](./mood.md)):

| Contributor | Source |
|-------------|--------|
| Need satisfaction | How well the needs are met, weighted per need |
| Mood effects | Carried by [thoughts](./thoughts.md). Everything else that changes how a colonist feels (surroundings, comfort, company, work they like, things that happen to them) reaches mood as a thought |

### Need-to-Mood Weights

Each need takes its own points off mood, and a need that is met gives no credit against one that is not: a colonist who is freezing is in a bad mood however well fed they are.

Every need has a cost, the most mood it takes off at its worst, and the penalty curve says how much of that cost applies: nothing at or above the comfortable level, rising gently to the warning level, then steeply to the full cost at an empty need. The needs score is 100 minus the sum of those costs, never below 0. Because the costs add and do not average, giving a creature another need changes nothing about what the others cost.

Every need can interrupt work when it gets bad enough, the ones that cannot kill included: work can run for days, and a colonist who never stops to wash or rest ends up in a low-mood behavior over it. The levels are tuned so that meeting the need comes first. A colonist leaves a long job to go and play a game before they have a fit and wreck the room because they have worked too long.

Each need's cost is on its own entry, and the curve, which every need shares, is in `assets/config/colonists/mood.xml`. Shipped costs: Temperature 60, Hunger 25, Thirst 25, Energy 20, Hygiene 15, Recreation 15, Bladder 10, Digestion 10.

### What low mood does

See [Colonist Mood](./mood.md): the roll curve, the behavior list, refusing work and food, and relief.

---

## Need-Capability Mapping

| Need | Capability Required | Example Entities |
|------|---------------------|------------------|
| Hunger | Edible | Berry Bush, Meals, Raw Meat |
| Thirst | Drinkable | Pond, Well, Water Bottle |
| Energy | Sleepable | Ground, Bedroll, Bed |
| Bladder | Toilet | Latrine, Toilet, Ground (fallback) |
| Hygiene | Washable | Pond, Bath, Shower |
| Recreation | Recreational | Horseshoe Pit, Chess Table |

See [Entity Capabilities](../world/entity-capabilities.md) for full capability definitions.

---

## Related Documents

- [AI Behavior](./ai-behavior.md) — How needs trigger behavior tiers
- [Colonist Attributes](./attributes.md) — Static personality traits
- [Entity Capabilities](../world/entity-capabilities.md) — How entities fulfill needs
- [MVP Scope](../mvp-scope.md) — What's included when

---

## Historical Addendum

This document consolidates content from two previous files. Original discussions preserved below.

### Previously in docs/design/mechanics/colonists.md

The original colonists.md file contained a "Basic Needs" section that listed:

**Survival Needs:**
- Eat (Hunger)
- Drink (Thirst) 
- Sleep (Energy)
- Use bathroom (Bladder)
- Wash (Hygiene)

**Wellbeing Needs:**
- Recreation/Fun
- Shelter (from weather)
- Flee (from danger)

This was duplicated in needs-system.md with more detail. The colonists.md file also had a "Dynamic Status" section with the same mood/breakdown thresholds now consolidated here.

**Note from original:** "Drinking accelerates bladder need, creating a realistic biological loop."

### Previously in docs/design/systems/needs-system.md

The original needs-system.md had its own "MVP Scope" section:

```
**Phase 1:** Hunger, Thirst, Energy, Bladder. Ground sleeping, outdoor bathroom with Bio Pile. Basic mood (need satisfaction only).

**Phase 2:** Hygiene, Recreation, Shelter. Mood thoughts.

**Phase 3:** Full mood system with all contributors. Breakdown behaviors.
```

This has been consolidated into the central [MVP Scope](../mvp-scope.md) document.

### Consolidation Date
- **2024-12-04:** Merged mechanics/colonists.md (needs sections) + systems/needs-system.md → game-systems/colonists/needs.md
