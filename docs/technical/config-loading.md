# Config Loading System

**Related Specs:**
- [Task Generation Architecture](./task-generation-architecture.md) — Load order, validation
- [Work Types Config](../design/game-systems/colonists/work-types-config.md) — Work type definitions

---

## Overview

This document specifies **when** and **how** work configuration files are loaded during the game lifecycle. The design supports hot-reloading mods from the main menu without restarting the application.

## Numbers

Config numbers go through the strict parsers (`parseConfigNumber` and `parseConfigInteger`, read by `ConfigReader`, in `libs/assets/assets/ConfigNumber.h`). A leading `+` is accepted; empty text, trailing characters ("30abc"), `nan`, `inf`, and values too large for the type are refused rather than read as a number. Text inside an element is trimmed first. A missing, malformed, or out-of-range value (a field stored in 16 bits is checked against its range, never truncated) is a config error naming the file, the element (with its `name` attribute when it has one, as in `<Task name="Haul"> tier`), and the attribute, shown on the loading screen. Base tuning lives in XML, so there is no C++ default for it.

---

## Design Goals

1. **No restart required for mod changes** — Unlike RimWorld, players can enable/disable mods from the main menu and start a new game without relaunching
2. **Fail-fast validation** — Config errors shown on loading screen, not during gameplay
3. **Clear error messages** — Modders see exactly what's wrong and where
4. **Clean reload** — Registries can be cleared and reloaded for each game start

---

## When to Load

### Decision: Load in GameLoadingScene (NOT at app startup)

```
App Startup (Main.cpp)
    ↓
Main Menu ← Player can toggle mods here
    ↓
GameLoadingScene::initializeWorldSystems()
    ├── Load base configs
    ├── Load enabled mod configs
    ├── Validate all configs
    └── On error → Show error screen → Return to menu
    ↓
GameScene (gameplay begins)
```

### Why Not App Startup?

Loading at app startup (Main.cpp) would require a full restart to change mods. By loading in `GameLoadingScene`:

- Mods can be toggled from main menu
- Each "New Game" gets fresh config load
- Returning to menu and starting new game applies mod changes

---

## Load Order

Configs load in dependency order (same as [task-generation-architecture.md](./task-generation-architecture.md)):

```
1. ActionTypeRegistry    ← No dependencies
2. TaskChainRegistry     ← References ActionTypes
3. WorkTypeRegistry      ← References TaskChains
4. PriorityConfig        ← References WorkTypes
5. ConfigValidator       ← Validates cross-references
```

### Base Game Paths

```
assets/config/actions/action-types.xml
assets/config/work/task-chains.xml
assets/config/work/work-types.xml
assets/config/work/priority-tuning.xml
assets/config/construction/*.xml
assets/config/colonists/mood.xml
assets/config/colonists/traits/*.xml
assets/config/colonists/needs/*.xml
assets/config/conditions/*.xml
assets/config/thoughts/*.xml
```

`colonists/traits/`, `colonists/needs/`, `conditions/`, and `thoughts/` load through `ecs::loadColonistRules(configRoot)` (libs/sim/ecs/ColonistRules.h),
stopping at the first stage with errors. The traits load first, then the need thresholds (`loadNeedLevels`: `assets::loadNeedsConfig` reads the folder, a later file's `<Need>` replacing an earlier one of the same name and logged at info level, and `ecs::NeedLevels` checks that every need is given and each name is a need). A needs file with any error adds nothing, and its errors name the file's path as loaded and the `<Need name="...">`. There is no C++ default for a threshold: every colonist's `NeedsComponent` is constructed from the loaded `NeedLevels`, so the AI and the thought sources read the same numbers. Then the condition vocabulary is declared, answered by the sim's terms, and frozen, then the thought
types are read and their conditions parsed against it (`loadConditionVocabulary` and `loadThoughtTypes`,
which the tests use too). Each folder's own files load before its subfolders' (`assets::configFilesIn`),
so a mod adds or replaces by adding a file there. A clean load logs its counts at info level (traits; needs; terms;
feelings, thought types, and sources). `GameLoadingScene` adds its errors, prefixed `[traits]`, `[needs]`, `[conditions]`,
or `[thoughts]`, to the config errors the loading screen shows, and hands the rules to `GameScene` through
`GameWorldState`. A direct switch to the game scene skips the loading scene, so `GameScene` loads them
itself, once; when they are broken it hands the errors to the splash (`world_sim::failColonistRules`),
which shows them after the ground bake and loads nothing.

`mood.xml` loads into `assets::MoodConfig` (the mood rate, the need penalty curve, the per-need
weights by need name, and the band names) and is checked by `ConfigValidator::validateMood`.
The need names belong to the sim, so `GameLoadingScene` adds `ecs::moodConfigErrors` (a name that is
no need, a need with no weight) to `loadWorkConfig`'s errors before the game scene is entered.

Every `.xml` file under `colonists/traits/` loads into an `assets::TraitRegistry` (the trait ids, names and
descriptions, the 1 to 3 count, and the exclusive groups): the folder's own files first, then its subfolders' in
path order, a later file replacing a trait of the same id with a log line. `<Exclusive>` groups from every file
accumulate, so a trait in several groups is excluded from all of their members. A `<Count>` min of 0 is legal; a min
the exclusive groups make unreachable is an error, and a max above what they allow is legal (the roll gives as many
as fit). `TraitRegistry::reachable` answers both the load check and each draw of `ecs::rollTraits`, so a roll never
gives fewer than the min; it rejects an unreachable min by a bound (at most one of each group) before any search. A
trait id that is a word of the condition language (`assets::kConditionReservedWords`, which the condition parser
also reads) is an error. Like the conditions and thoughts, any problem (a bad number, an unknown element or attribute)
means the folder loads nothing.

The registry is no singleton: `ecs::loadColonistRules` builds it as the first stage and `ecs::ColonistRules` owns it
beside the vocabulary and the thought types, so whatever needs it (the colonist spawn, the dossier's Bio tab) takes it
from the rules the world was built with. The conditions take their `trait` names from it: the condition loader declares
the `trait` symbol set itself, and a conditions file declaring `<Symbols name="trait">` is an error naming that file.
So a `trait(...)` naming an id no file declares is a condition error naming the thought or condition file. Its errors
are prefixed `[traits]` and reach the loading screen and the splash the same way as theirs.

### Mod Paths (Future)

```
mods/{mod-name}/config/actions/*.xml
mods/{mod-name}/config/work/*.xml
```

Mods load alphabetically after base game, allowing later mods to override earlier definitions.

---

## Implementation

### Loading the work config

`assets::loadWorkConfig(configRoot)` (libs/assets/assets/WorkConfig.h) clears the work-config
registries (ActionTypeRegistry, TaskChainRegistry, WorkTypeRegistry, PriorityConfig,
ConstructionRegistry, MoodConfig, and ConfigValidator's errors), loads them from `configRoot` in the
order above, checks that priority-tuning.xml gives every task type an arbitration tier, loads
construction and mood, and cross-validates. It returns every error as a string; an empty list means the config is loaded. It needs
no GL and no scene, so the headless sim calls it too.

The loading scene clears `ecs::GoalTaskRegistry` itself (a sim singleton libs/assets can't reach),
then calls it in the `Initializing` phase:

```cpp
// GameLoadingScene.cpp, initializeWorldSystems()
updateStatusText("Loading configuration...");
ecs::GoalTaskRegistry::Get().clear();
const auto configErrors = assets::loadWorkConfig("assets/config");
if (!configErrors.empty()) {
    for (const auto& error : configErrors) {
        LOG_ERROR(Game, "%s", error.c_str());
    }
    phase = LoadingPhase::ConfigError;
    return;
}
```

### New Loading Phase: ConfigError

Add error handling for config failures:

```cpp
enum class LoadingPhase {
    Initializing,
    ConfigError,      // NEW: Config load/validation failed
    LoadingChunks,
    PlacingEntities,
    Complete,
    Cancelling
};

void update(float dt) override {
    switch (phase) {
        case LoadingPhase::ConfigError:
            handleConfigError();
            break;
        // ... other cases ...
    }
}

void handleConfigError() {
    // Show error details
    updateStatusText("Configuration Error - Press ESC to return to menu");

    // Build error message from ConfigValidator
    std::string errorMsg;
    for (const auto& error : ConfigValidator::getErrors()) {
        errorMsg += error.source + ": " + error.message + "\n";
        if (!error.context.empty()) {
            errorMsg += "  " + error.context + "\n";
        }
    }

    // Display in UI (or log for now)
    LOG_ERROR(Game, "Config errors:\n%s", errorMsg.c_str());

    // ESC returns to menu
    if (theme::key(frame.input, foundation::Key::Escape).pressed()) {
        owner->scenes().switchTo(world_sim::scenes::MainMenu.name);
    }
}
```

---

## Status Text Updates

The loading screen should show progress:

| Phase | Status Text |
|-------|-------------|
| Config loading | "Loading configuration..." |
| Config error | "Configuration Error - Press ESC to return to menu" |
| Terrain | "Generating terrain..." |
| Entities | "Placing entities... X%" |
| Complete | "Ready!" |

---

## Testing

### Unit Tests (existing)

The 19 tests in `WorkConfig.test.cpp` cover registry loading and validation.

### Integration Test (manual)

1. Start game → Main Menu
2. Start New Game
3. Verify loading screen shows "Loading configuration..."
4. Verify game starts successfully
5. Return to Main Menu
6. Start New Game again
7. Verify configs reload cleanly (no stale data)

### Error Handling Test (manual)

1. Introduce typo in `action-types.xml` (e.g., invalid XML)
2. Start New Game
3. Verify error screen shows with clear message
4. Press ESC → return to menu
5. Fix the typo
6. Start New Game → should work

---

## Future: Mod Support

When mod support is added:

1. Main Menu gets "Mods" button
2. Mods screen shows available mods with enable/disable toggles
3. Enabled mods stored in save file or user preferences
4. `assets::loadWorkConfig` loads base + enabled mods in order
5. Mod load order: alphabetical (or explicit load order in mod metadata)

See [task-generation-architecture.md](./task-generation-architecture.md#mod-loading) for mod validation rules.

---

## Summary

| Decision | Choice | Rationale |
|----------|--------|-----------|
| When to load | GameLoadingScene | Enables mod changes without restart |
| Error handling | Show on loading screen, return to menu | Clear feedback for modders |
| Reload support | Clear registries before each load | Clean slate for each game start |
