# Priority Configuration System

**Status:** Design
**Created:** 2025-01-02
**MVP Status:** Phase 2+ (after basic needs/work loop)

---

## Overview

Task priority is calculated using a formula with tunable weights. All weights are defined in XML config files, allowing designers to balance the system without code changes.

**Design Goal:** Make priority tuning a data-driven process. When testers report "colonists walk too far for low-priority tasks," designers can adjust distance penalties without programmer involvement.

---

## Priority Formula

### Full Formula (Colonist Selection)

When a colonist selects their next task:

```
finalPriority = basePriority
              + distanceBonus        // -50 to +50
              + skillBonus           // 0 to +100
              + chainBonus           // +2000 if continuing chain
              + inProgressBonus      // +200 if already doing this
              + taskAgeBonus         // 0 to +100 (old tasks rise)
```

### Simplified Formula (UI Display)

For the global task list (no colonist context):

```
displayPriority = basePriority - (distance * distancePenaltyMultiplier)
```

---

## Priority Bands (int16)

Tasks are grouped into priority bands. User-configurable priorities (1-9) map into these bands.

| Band Name | Base Value | Description |
|-----------|------------|-------------|
| Critical | 30000 | Life-threatening (< 10% need) |
| PlayerDraft | 20000 | Direct player control |
| Needs | 10000 | Actionable needs (below threshold) |
| WorkHigh | 5000 | User priority 1-3 |
| WorkMedium | 3000 | User priority 4-6 |
| WorkLow | 1000 | User priority 7-9 |
| Idle | 0 | Wander (fallback) |

### User Priority Mapping

The UI's 1-9 priority maps into the Work bands:

```
User Priority 1 → WorkHigh + 800 = 5800
User Priority 2 → WorkHigh + 700 = 5700
User Priority 3 → WorkHigh + 600 = 5600
User Priority 4 → WorkMedium + 500 = 3500
User Priority 5 → WorkMedium + 400 = 3400
User Priority 6 → WorkMedium + 300 = 3300
User Priority 7 → WorkLow + 200 = 1200
User Priority 8 → WorkLow + 100 = 1100
User Priority 9 → WorkLow + 0 = 1000
```

Formula: `bandBase + (10 - userPriority) * step`

---

## Config File Format

**File:** `assets/config/work/priority-tuning.xml`

```xml
<?xml version="1.0" encoding="UTF-8"?>
<PriorityTuning>
  <!-- Priority bands define the int16 ranges for each tier -->
  <Bands>
    <Band name="Critical" base="30000">
      <description>Life-threatening situations (critical needs)</description>
    </Band>
    <Band name="PlayerDraft" base="20000">
      <description>Direct player control overrides</description>
    </Band>
    <Band name="Needs" base="10000">
      <description>Actionable needs below seek threshold</description>
    </Band>
    <Band name="WorkHigh" base="5000">
      <description>User priority 1-3 work tasks</description>
    </Band>
    <Band name="WorkMedium" base="3000">
      <description>User priority 4-6 work tasks</description>
    </Band>
    <Band name="WorkLow" base="1000">
      <description>User priority 7-9 work tasks</description>
    </Band>
    <Band name="Idle" base="0">
      <description>Wander and idle behaviors</description>
    </Band>
  </Bands>

  <!-- User priority (1-9) to band mapping -->
  <UserPriorityMapping>
    <bandAssignment priority="1-3" band="WorkHigh"/>
    <bandAssignment priority="4-6" band="WorkMedium"/>
    <bandAssignment priority="7-9" band="WorkLow"/>
    <stepSize>100</stepSize>
  </UserPriorityMapping>

  <!-- Work category tier ordering (for simple mode) -->
  <WorkCategoryOrder>
    <Category name="Emergency" tier="1"/>
    <Category name="Medical" tier="2"/>
    <Category name="Farming" tier="3"/>
    <Category name="Crafting" tier="4"/>
    <Category name="Construction" tier="5"/>
    <Category name="Hauling" tier="6"/>
    <Category name="Cleaning" tier="7"/>
  </WorkCategoryOrder>

  <!-- Bonus calculations -->
  <Bonuses>
    <!-- Distance bonus: closer tasks score higher -->
    <Distance>
      <optimalDistance>5.0</optimalDistance>
      <maxPenaltyDistance>50.0</maxPenaltyDistance>
      <maxBonus>50</maxBonus>
      <maxPenalty>50</maxPenalty>
      <formula>
        <!-- At optimalDistance: +maxBonus
             At maxPenaltyDistance: -maxPenalty
             Linear interpolation between -->
      </formula>
    </Distance>

    <!-- Skill bonus: skilled colonists prefer work they're good at -->
    <Skill>
      <multiplier>10</multiplier>
      <maxBonus>100</maxBonus>
      <formula>min(skillLevel * multiplier, maxBonus)</formula>
    </Skill>

    <!-- Chain continuation: strongly prefer finishing multi-step tasks -->
    <ChainContinuation>
      <bonus>2000</bonus>
      <description>
        Applied when colonist completed previous step of same chain.
        This ensures pickup → deposit stays together.
      </description>
    </ChainContinuation>

    <!-- In-progress: resist switching away from current task -->
    <InProgress>
      <bonus>200</bonus>
      <description>
        Applied to current task to prevent minor priority fluctuations
        from causing constant task switching.
      </description>
    </InProgress>

    <!-- Task age: old unclaimed tasks slowly rise in priority -->
    <TaskAge>
      <bonusPerMinute>1</bonusPerMinute>
      <maxBonus>100</maxBonus>
      <description>
        Prevents task starvation. After 100 minutes, even low-priority
        tasks get +100 bonus.
      </description>
    </TaskAge>
  </Bonuses>

  <!-- Thresholds and timings -->
  <Thresholds>
    <!-- Minimum priority gap to switch tasks while action in progress -->
    <taskSwitchThreshold>50</taskSwitchThreshold>

    <!-- How often colonists re-evaluate tasks (seconds) -->
    <reEvalInterval>0.5</reEvalInterval>

    <!-- Reservation timeout: release if no progress (seconds) -->
    <reservationTimeout>10.0</reservationTimeout>
  </Thresholds>

  <!-- Hauling-specific tuning to avoid the "hauling problem" -->
  <HaulingTuning>
    <!-- Boost haul priority when storage is critically low -->
    <storageCriticalThreshold>0.2</storageCriticalThreshold>
    <storageCriticalBonus>500</storageCriticalBonus>

    <!-- Boost if loose item is blocking construction -->
    <blockingConstructionBonus>1000</blockingConstructionBonus>

    <!-- Boost if item is perishable and will spoil soon -->
    <perishableSpoilThreshold>60.0</perishableSpoilThreshold>
    <perishableBonus>800</perishableBonus>

    <!-- Batch nearby items to reduce task spam -->
    <batchRadius>8.0</batchRadius>
    <maxBatchSize>5</maxBatchSize>
  </HaulingTuning>
</PriorityTuning>
```

---

## Bonus Details

### Distance Bonus

Encourages colonists to do nearby work first.

```cpp
int16_t calculateDistanceBonus(float distance, const DistanceConfig& config) {
    if (distance <= config.optimalDistance) {
        return config.maxBonus;  // +50 for very close
    }

    if (distance >= config.maxPenaltyDistance) {
        return -config.maxPenalty;  // -50 for very far
    }

    // Linear interpolation
    float range = config.maxPenaltyDistance - config.optimalDistance;
    float normalized = (distance - config.optimalDistance) / range;
    return static_cast<int16_t>(config.maxBonus - normalized * (config.maxBonus + config.maxPenalty));
}
```

**Examples:**
- 3m away: +50 (optimal)
- 15m away: +25
- 30m away: 0
- 45m away: -25
- 50m+ away: -50 (max penalty)

### Skill Bonus

Skilled colonists naturally gravitate toward work they're good at.

```cpp
int16_t calculateSkillBonus(float skillLevel, const SkillConfig& config) {
    return std::min(
        static_cast<int16_t>(skillLevel * config.multiplier),
        config.maxBonus
    );
}
```

**Examples (multiplier=10, max=100):**
- Skill level 0: +0
- Skill level 5: +50
- Skill level 8: +80
- Skill level 10+: +100 (capped)

### Chain Continuation Bonus

Keeps multi-step tasks together. A colonist who picked up an item strongly prefers depositing it.

```cpp
int16_t calculateChainBonus(
    const Task& currentTask,
    const GlobalTask& candidateTask,
    const ChainConfig& config
) {
    // Must be same chain, and candidate is next step
    if (currentTask.chainId == candidateTask.chainId &&
        candidateTask.chainStep == currentTask.chainStep + 1) {
        return config.bonus;  // +2000
    }
    return 0;
}
```

**Effect:** The +2000 bonus is larger than most band differences, ensuring chain completion unless interrupted by Critical needs.

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

**Example (1 per minute, max 100):**
- Created just now: +0
- 30 minutes old: +30
- 100+ minutes old: +100 (capped)

---

## Hauling Problem Mitigation

The "hauling problem" (from RimWorld) occurs when colonists haul everything before doing other work, often walking across the entire map for low-value items.

### Mitigation Strategies

1. **Distance Bonus:** Nearby items score higher than distant ones
2. **Storage Critical Boost:** When storage is < 20% full for an item category, boost haul priority by +500
3. **Blocking Construction:** Items blocking build sites get +1000 boost
4. **Perishable Items:** Food that will spoil in < 60s gets +800 boost
5. **Batch Nearby Items:** Group items within 8m into single task (max 5 per batch)

```cpp
int16_t calculateHaulPriority(const HaulTask& task, const HaulingConfig& config) {
    int16_t priority = WorkLow;  // Base: hauling is low priority

    // Storage critical?
    if (getStorageFullness(task.itemCategory) < config.storageCriticalThreshold) {
        priority += config.storageCriticalBonus;
    }

    // Blocking construction?
    if (isBlockingConstruction(task.sourcePosition)) {
        priority += config.blockingConstructionBonus;
    }

    // Perishable?
    if (task.spoilTime > 0 && task.spoilTime - gameTime < config.perishableSpoilThreshold) {
        priority += config.perishableBonus;
    }

    return priority;
}
```

---

## Config Loading

### Registry Class

```cpp
class PriorityConfig {
public:
    static PriorityConfig& Get();

    bool loadFromFile(const std::string& xmlPath);

    // Band queries
    int16_t getBandBase(const std::string& bandName) const;
    int16_t userPriorityToBase(uint8_t userPriority) const;

    // Bonus calculations
    int16_t calculateDistanceBonus(float distance) const;
    int16_t calculateSkillBonus(float skillLevel) const;
    int16_t calculateChainBonus(uint64_t currentChainId, uint8_t currentStep,
                                uint64_t candidateChainId, uint8_t candidateStep) const;
    int16_t calculateTaskAgeBonus(float taskAge) const;
    int16_t calculateHaulBonus(const HaulContext& context) const;

    // Thresholds
    float getTaskSwitchThreshold() const;
    float getReEvalInterval() const;
    float getReservationTimeout() const;

private:
    BandConfig m_bands;
    BonusConfig m_bonuses;
    ThresholdConfig m_thresholds;
    HaulingConfig m_hauling;
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

### When to Increase Distance Penalty

**Symptom:** Colonists walk across map for distant tasks when nearby tasks exist.

**Fix:** Increase `maxPenalty` or decrease `maxPenaltyDistance`.

### When to Increase Skill Bonus

**Symptom:** Skilled colonists don't gravitate toward their specialty.

**Fix:** Increase `multiplier` (but watch for skill bonus dominating distance).

### When to Increase Chain Bonus

**Symptom:** Colonists drop items mid-haul for other tasks.

**Fix:** Increase `chainContinuationBonus` (2000 is already large).

### When to Adjust Hauling Config

**Symptom:** Colonists haul constantly instead of doing other work.

**Fix:** Decrease `storageCriticalBonus`, or tighten `storageCriticalThreshold`.

**Symptom:** Food spoils because nobody hauls it.

**Fix:** Increase `perishableBonus` or increase `perishableSpoilThreshold`.

---

## Related Documents

- [Task Registry System](./task-registry.md) — Where priorities are used
- [Task Generation Architecture](../../technical/task-generation-architecture.md) — When priorities are calculated
- [Task Chains](./task-chains.md) — Multi-step tasks and chain bonus
- [Work Priorities](./work-priorities.md) — User-facing 1-9 priority system
