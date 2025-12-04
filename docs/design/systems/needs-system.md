# Needs System

**Status:** Design  
**Created:** 2024-12-04

## Overview

Colonists have physical and emotional needs. Some needs drive behavior (actionable), others contribute to mood.

## Actionable Needs

These create tasks. When they drop below threshold, colonist seeks fulfillment.

### Hunger

| Property | Value |
|----------|-------|
| Seek Threshold | ~50% |
| Critical | ~10% |
| Failure | Starvation damage |
| Fulfilled By | Eating food |

Different foods restore different amounts. Food quality affects mood.

### Thirst

| Property | Value |
|----------|-------|
| Seek Threshold | ~50% |
| Critical | ~10% |
| Failure | Dehydration damage |
| Fulfilled By | Drinking water |

Clean vs dirty water affects health. **Drinking accelerates bladder need.**

### Energy (Sleep)

| Property | Value |
|----------|-------|
| Seek Threshold | ~30% |
| Critical | ~10% |
| Failure | Collapse where standing |
| Fulfilled By | Sleeping (ground, bed, etc.) |

Sleep quality affects recovery rate. Better sleeping spots = faster recovery, mood bonus.

### Bladder

| Property | Value |
|----------|-------|
| Seek Threshold | ~30% |
| Critical | ~10% |
| Failure | Accident |
| Fulfilled By | Using toilet OR finding outdoor spot |

**Accelerated by drinking.** Each drink adds to bladder need, creating a realistic biological loop.

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
| Critical | ~15% |
| Failure | Continuous mood/social penalty |
| Fulfilled By | Washing (water source, bath) |

Drops to zero on bathroom accident. Low hygiene = others like you less.

### Recreation

| Property | Value |
|----------|-------|
| Seek Threshold | ~30% |
| Critical | ~10% |
| Failure | Work efficiency drops, mood penalty |
| Fulfilled By | Recreation activities, socializing |

Decays while working, recovers while idle or recreating. Different recreation types may satisfy variety needs.

### Shelter

| Property | Value |
|----------|-------|
| Trigger | Dangerous weather |
| Fulfilled By | Going indoors |

Not a percentage bar - binary trigger based on weather conditions (storms, extreme temperatures).

### Flee

| Property | Value |
|----------|-------|
| Trigger | Active threat |
| Fulfilled By | Reaching safety |

See Panic in [Colonist AI](./colonist-ai.md). Highest priority actionable need.

## Mood Contributors

These affect overall mood but don't create direct actions.

### Beauty
Aesthetic quality of surroundings. Nice rooms = mood boost, garbage/corpses = penalty.

### Comfort  
Quality of furniture being used. Good chair while working = bonus.

### Temperature
Comfort relative to colonist's tolerance. Too hot or cold = penalty, extreme = damage.

### Social
Running tally of social interaction quality. NOT "I need to socialize" but "my interactions have been good/bad." Colonists who hate everyone around them have low social mood.

### Fulfillment
Satisfaction from using preferred skills. Colonists with farming affinity get mood boost from farming work.

### Purpose
Satisfaction from helping the colony. Completing meaningful tasks provides purpose.

### Thoughts
Temporary modifiers from specific events:
- "Ate a good meal" (+5, 6 hours)
- "Slept on ground" (-10, 1 day)
- "Had accident in public" (-20, 1 day)
- "Talked with friend" (+5, 6 hours)

## Mood and Breakdown

**Mood** is calculated from all contributors. Range 0-100%.

**Breakdown Thresholds:**
- Below 35%: Minor break risk
- Below 20%: Major break risk
- Below 5%: Extreme break risk

## MVP Scope

**Phase 1:** Hunger, Thirst, Energy, Bladder. Ground sleeping, outdoor bathroom with Bio Pile. Basic mood (need satisfaction only).

**Phase 2:** Hygiene, Recreation, Shelter. Mood thoughts.

**Phase 3:** Full mood system with all contributors. Breakdown behaviors.
