# Priority Configuration System

**Status:** Design
**Created:** 2025-01-02
**MVP Status:** Phase 2+ (after basic needs/work loop)

---

## Overview

Task priority is calculated using a lexicographic `(tier: int, score: float)` key. Tier is compared first (lower number = higher priority) and is inviolable; a far tier-4 option always beats a near tier-5 one. Score orders options within the same tier only. All score weights and tier assignments are defined in `assets/config/work/priority-tuning.xml`, allowing tuning without code changes.

The numeric bands of the earlier design (`Critical 30000`, `Needs 10000`, etc.) were replaced by the tier integer and are gone, config and accessors both: nothing read them. See [Colonist Task Arbitration](../../../technical/colonist-task-arbitration.md) for the full implementation.

**Design Goal:** Make priority tuning a data-driven process. When testers report "colonists walk too far for low-priority tasks," designers can adjust distance penalties without programmer involvement.

---

## Two Priority Concepts

**Important:** There are two distinct priority concepts. Don't confuse them.

### 1. Colonist Work Type Preference (1-9)

Each colonist has a personal preference for each work category:

| Colonist | Farming | Hauling | Crafting |
|----------|---------|---------|----------|
| Bob | 2 (prefers) | 7 (avoids) | 5 (neutral) |
| Alice | 6 | 3 | 2 |

- **Lower number = colonist prefers this work**
- Set per-colonist in the work priorities UI
- Not implemented yet: no colonist carries a preference and nothing reads one
- Affects which *type* of work a colonist gravitates toward

### 2. Goal Priority (Buildings/Entities)

Buildings and entities can be marked with urgency:

- Storage container marked "Urgent" → all haul tasks to it get +bonus
- Crafting station marked "High" → crafting orders there score higher
- Construction blueprint marked "Critical" → builds it first

- **Set on the goal (building/entity), not on individual tasks**
- Affects all tasks that target that goal
- Players define outcomes ("fill this with stone"), not individual tasks

### How They Combine (AI selection)

AI arbitration is a `(tier, score)` key, not a single float. Within the same tier:

```
score = distanceFactor          // dominant; 300*max(0,1-d/60), nearest source wins
      + skillBonus              // colonist's skill level for this work type
      + goalPriority adjustment // urgent storage etc., within-tier lift only
      + taskAgeBonus            // stale tasks slowly rise
      + hysteresisBonus         // +50 on the current in-progress option only
```

Chain steps (`servesActiveWorkOrder == true`) are classified at tier 4, not scored higher within tier 6. Work type preference (1-9) affects within-tier ordering for tier 6 opportunistic work.

---

## Priority Formula

### AI Selection: lexicographic (tier, score)

When a colonist selects their next task, options are compared as `(tier, score)` pairs. Tier comes from the task type's definition in `priority-tuning.xml <TaskTiers>` (validated at startup; unassigned types fail with an error). Score orders within a tier only:

```
score = distanceFactor      // dominant: 300 * max(0, 1 - d/60), nearest source wins
      + skillBonus          // 0 to +100
      + taskAgeBonus        // 0 to +100 (old tasks rise)
      + hysteresisBonus     // +50 applied to the in-progress option only (same tier)
      + storagePriorityBias // stocking only: the destination box's priority rank (0..3) * 300
```

`chainBonus` (+2000) is gone; a chain step is classified at tier 4 (active work order), not scored higher within tier 6. `inProgressBonus` is gone; it is replaced by the `hysteresisBonus` (50) applied within the same tier only. `kWorkOrderProvisionFloor` and `kStorageStockingFloor` are deleted; tiers make them unnecessary.

---

## Config File Format

**File:** `assets/config/work/priority-tuning.xml`. The file itself is the reference for values; its comments explain each element. What reads each one:

| Element | Read by | Effect |
|---|---|---|
| `<TaskTiers>` | `AIDecisionSystem` (`readBaseTiers`), checked at load by `validateTaskTiers` | each task type's base tier |
| `<WorkCategoryOrder>` | `ConfigValidator` only | none on decisions |
| `<Bonuses><DistanceFactor>` (`maxDistance` 60, `maxFactor` 300) | `calculateDistanceFactor` | the distance term |
| `<Bonuses><InProgress><bonus>` (50) | `getInProgressBonus` | the hysteresis term |
| `<Bonuses><TaskAge>` (1 per minute, at most 100) | `calculateTaskAgeBonus` | the task-age term |
| `<Bonuses><StoragePriority><weight>` (300) | `getStoragePriorityWeight` | the storage-priority term |
| `<Thresholds><taskSwitchThreshold>` | nothing | parsed, read by nothing; the switch margin is `<InProgress><bonus>` |
| `<Thresholds><reEvalInterval>` | nothing | parsed, read by nothing; the interval is `AIDecisionSystem::kReEvalInterval` (0.5 s) |
| `<HaulingTuning>` | nothing | parsed, read by nothing; none of its boosts or batching exist |

---

## Bonus Details

### Distance factor

The dominant within-tier term; nearest reachable source wins.

```cpp
float distanceFactor(float distance) {
    return 300.0f * std::max(0.0f, 1.0f - distance / 60.0f);
}
```

**Examples:**
- 0m away: 300 (maximum)
- 15m away: 225
- 30m away: 150
- 45m away: 75
- 60m+ away: 0

The old `±50 distanceBonus` range was too narrow relative to the skill/age terms; a high-skill colonist could prefer a tree 70m away over an adjacent one. The new range (0-300) dominates skill (0-100) and age (0-100), so the nearest source reliably wins within a tier.

### Skill Bonus

Skilled colonists naturally gravitate toward work they're good at. The bonus is `min(skillLevel * 10, 100)`, fixed in AIDecisionSystem (not tuned in the XML), and each skill's bonus is worked out once per decision.

**Examples:**
- Skill level 0: +0
- Skill level 5: +50
- Skill level 8: +80
- Skill level 10+: +100 (capped)

### Chain continuation

Chain steps are kept together by tier classification, not score inflation. Any haul or harvest task with `servesActiveWorkOrder == true` is assigned tier 4 (active work order). Since tier 4 beats tier 5 (actionable needs) and tier 6 (opportunistic work), chain steps stay bound to their parent order without a score term. The old `+2000 chainBonus` is deleted.

**Effect:** A colonist finishing a provisioning haul cannot be pulled away by a non-critical need or an opportunistic task, because the chain step is tier 4 and those alternatives are tier 5 or tier 6.

### Task Age Bonus

Prevents low-priority tasks from being starved forever.

```cpp
int16_t calculateTaskAgeBonus(float taskAge, const TaskAgeConfig& config) {
    float minutes = taskAge / 60.0f;
    return std::min(
        static_cast<int16_t>(minutes * config.bonusPerMinute),
        config.maxBonus
    );
}
```

In the game the age is not a goal's own: no goal system stamps `createdAt` (it stays 0), so the age is the AI clock (`AIDecisionSystem::m_elapsedTime`, real seconds since the session started) and every Haul and Harvest option with a goal gets the same term. It reaches +100 after 100 real minutes and orders nothing against other goals.

---

## What orders hauling

The "hauling problem" (from RimWorld) is colonists hauling everything before other work, walking across the map for low-value items. Nothing hauling-specific guards against it; the general `(tier, score)` key does, as follows.

1. **Tier.** A Haul or Harvest serving an active work order (`servesActiveWorkOrder`: provisioning a build or a craft) is tier 4; any other, storage stocking included, keeps its base tier 6. A PlacePackaged serving a work order or already carried is tier 4. Tier 6 loses to every need (tier 5, or 2 when critical) and to work orders, so an opportunistic haul only happens when nothing above it is available (`classifyTier`).
2. **Gathering rank, within a tier and before the score** (`EvaluatedOption::gatheringRank`): a loose pile toward a root the colonist gathers for (2), then a further cut toward it (1), then everything else, the carry-in included (0), then a fresh cut toward a root and item a known pile also serves (-1). A colonist fills its trip before walking it in, and takes a pile before cutting another plant.
3. **Score, within a tier and rank** (`populatePriorityBonuses`, `EvaluatedOption::computeScore`), the sum of:
   - distance factor, `300 * max(0, 1 - d / 60)`, so the nearest reachable source wins;
   - skill bonus, `min(skillLevel * 10, 100)`;
   - task age, the same for every goal today (see Task Age Bonus);
   - hysteresis, +50 on the option the colonist is already doing;
   - storage priority, for a stocking option only: the destination box's priority for the item (Low 0 to Critical 3) times `<StoragePriority><weight>` (300), so a higher-priority box beats a nearer lower one.
4. **Ties** break on `tiebreakId`, a stable id, never container order.

The boosts this section used to list (+500 when storage runs low, +1000 for items blocking construction, +800 for food about to spoil, batching items within 8 m) are `<HaulingTuning>` values that nothing reads.

---

## Config Loading

### Registry Class

```cpp
class PriorityConfig {
public:
    static PriorityConfig& Get();

    bool loadFromFile(const std::string& xmlPath);

    // Task tier queries (AI arbitration). Tier is compared first; lower = higher priority.
    int getTaskTier(const std::string& taskTypeName) const;            // base tier, or kUnassignedTier
    static constexpr int kUnassignedTier = 9999;
    bool validateTaskTiers(const std::vector<std::string>& requiredTypeNames,
                           std::vector<std::string>& missingOut) const; // fail-loud at load

    // Within-tier score terms
    float   calculateDistanceFactor(float distance) const; // dominant: 300*max(0,1-d/60)
    int16_t calculateTaskAgeBonus(float taskAge) const;
    int16_t getInProgressBonus() const;                    // within-tier hysteresis margin (50)

    // Thresholds
    int16_t getTaskSwitchThreshold() const;
    float   getReEvalInterval() const;

private:
    std::unordered_map<std::string, int> taskTiers;  // per-task-type base tier (arbitration)
    DistanceFactorConfig  distanceFactorConfig;
    InProgressBonusConfig inProgressConfig;
    TaskAgeBonusConfig    taskAgeConfig;
    HaulingTuningConfig   haulingConfig;
    TimingConfig          timingConfig;
};
```

### Loading at Startup

```cpp
// In AppLauncher.cpp or similar
void initializeGameSystems() {
    // ... existing asset loading ...

    // Load priority tuning
    std::string configPath = Foundation::findResourceString("assets/config/work/priority-tuning.xml");
    if (!PriorityConfig::Get().loadFromFile(configPath)) {
        LOG_ERROR(Engine, "Failed to load priority config from %s", configPath.c_str());
    }
}
```

---

## Tuning Guidelines

Tier is decided first and is not tuned by a score: move a task type between tiers in `<TaskTiers>`, or fix `classifyTier` when one option lands in the wrong tier. The knobs below only reorder options inside a tier.

### When to increase the distance factor

**Symptom:** Colonists walk across the map for a task when a nearby one of the same tier exists.

**Fix:** Increase `<DistanceFactor><maxFactor>` so distance outweighs skill, age, and hysteresis more, or decrease `<maxDistance>` so the factor falls to 0 sooner.

### Skill bonus (no knob)

The skill bonus is `min(skillLevel * 10, 100)`, fixed in AIDecisionSystem; `priority-tuning.xml` has no skill setting. If skill must count for more against distance, lower `maxFactor` instead.

### When to change the hysteresis margin

**Symptom:** Colonists flip between two same-tier tasks, or stick to a far task when a much nearer one appears.

**Fix:** Raise `<InProgress><bonus>` against thrashing, lower it against stickiness.

### When to change the storage priority weight

**Symptom:** A higher-priority box is stocked after a nearer lower-priority one, or the reverse is wanted.

**Fix:** `<StoragePriority><weight>` at or above `maxFactor` makes box priority beat distance within tier 6; below it, a much nearer lower box can win.

### Chain continuation (no score to tune)

There is no chain score to tune. A provisioning step (`servesActiveWorkOrder == true`) is
classified at tier 4, and tier beats score, so the colonist stays on the chain by construction.
If chains break, the bug is in tier classification (the option not being marked as serving the
work order), not in a bonus value.

### Hauling config (not read)

`<HaulingTuning>` is parsed but nothing in the decision reads it, so changing it changes no behaviour.

---

## Related Documents

- [Task Registry System](./task-registry.md) — Where priorities are used
- [Task Generation Architecture](../../technical/task-generation-architecture.md) — When priorities are calculated
- [Task Chains](./task-chains.md) — Multi-step tasks and chain bonus
- [Work Priorities](./work-priorities.md) — User-facing 1-9 priority system
