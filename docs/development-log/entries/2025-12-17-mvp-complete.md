# 2025-12-17: MVP Complete - Autonomous Colonist

## Summary

The MVP is now complete. A colonist can autonomously take care of itself indefinitely without player intervention. The final piece was **Tier 6: Gather Food** - proactive harvesting when the colonist's inventory is empty but all needs are satisfied.

## What Was Accomplished

### Tier 6: Gather Food (this session)
- Added `evaluateGatherFood()` to AIDecisionSystem
- Colonist now proactively harvests food when inventory empty and needs satisfied
- Creates a resource loop: gather while happy → eat from inventory when hungry
- Updated DecisionTrace to show "Gathering food" in task queue

### MVP Features Complete
1. **Four Needs System**: Hunger, Thirst, Energy, Bladder with decay and restoration
2. **Decision Hierarchy**: Critical (Tier 3) → Actionable (Tier 5) → Gather Food (Tier 6) → Wander (Tier 7)
3. **Actions**: Eat, Drink, Sleep, Toilet - all with duration-based progress
4. **Inventory**: Colonist stores harvested berries, eats from inventory
5. **Bio Pile**: Created when colonist uses toilet outdoors, with smart location selection
6. **Memory**: Colonist remembers discovered entities, pathfinds only to known locations
7. **Player Observation**: Select colonist, view needs panel, expand task queue

## Files Changed

### Code
- `libs/engine/ecs/systems/AIDecisionSystem.h` - Added `evaluateGatherFood()` declaration
- `libs/engine/ecs/systems/AIDecisionSystem.cpp` - Tier 6 implementation and DecisionTrace update

### Documentation
- `docs/design/mvp-scope.md` - Changed "Harvest Wild" to "Gather Food"
- `docs/design/mvp-entities.md` - Clarified berry bush behavior (harvest → inventory → eat)
- `docs/design/game-systems/colonists/ai-behavior.md` - Updated Tier 6 description
- `docs/status.md` - Marked Tier 6 as complete, updated timestamp

## Technical Details

The implementation reuses the existing FulfillNeed task type with `needToFulfill = Hunger`. When the ActionSystem sees this and finds no food in inventory, it creates a Harvest action (not Eat). This means:

1. Colonist with empty inventory goes to bush
2. Harvests berries into inventory
3. Next tick: still hungry → finds food in inventory → creates Eat action
4. Eats from inventory

For Tier 6 "Gather Food", we trigger this same path even when not hungry:
1. Check if needs satisfied but inventory empty
2. Find nearest Harvestable in memory
3. Create FulfillNeed/Hunger task
4. ActionSystem creates Harvest action (since inventory is empty)
5. Colonist stores food for later

## Lessons Learned

1. **Documentation drift**: MVP scope said "Harvest Wild" while entity docs said "eat directly". The actual implementation was more sophisticated (harvest + eat from inventory). Fixed by aligning all docs.

2. **Reusing existing logic**: Rather than creating a new TaskType::Work, we reused FulfillNeed/Hunger. The ActionSystem already handles "no inventory → harvest" vs "has inventory → eat". This minimized code changes.

3. **Tier 6 naming**: "Harvest Wild" implies the source (wild plants), but the behavior is "gather food regardless of source". Called it "Gather Food" to be source-agnostic for future stockpile support.

## What's Next (Phase 2)

With MVP complete, potential next steps:
- Multiple colonists with simple memory sharing
- Beds and Toilets (better sleep/bathroom quality)
- Hauling work type
- Basic mood system with thoughts
- Hygiene and Recreation needs
