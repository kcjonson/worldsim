# Config Loading System

**Related Specs:**
- [Task Generation Architecture](./task-generation-architecture.md) — Load order, validation
- [Work Types Config](../design/game-systems/colonists/work-types-config.md) — Work type definitions

---

## Overview

This document specifies **when** and **how** work configuration files are loaded during the game lifecycle. The design supports hot-reloading mods from the main menu without restarting the application.

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
```

### Mod Paths (Future)

```
mods/{mod-name}/config/actions/*.xml
mods/{mod-name}/config/work/*.xml
```

Mods load alphabetically after base game, allowing later mods to override earlier definitions.

---

## Implementation

### GameLoadingScene Integration

Add config loading to the `Initializing` phase:

```cpp
// GameLoadingScene.cpp

void initializeWorldSystems() {
    LOG_INFO(Game, "GameLoadingScene - Initializing world systems");

    // NEW: Load work configuration
    updateStatusText("Loading configuration...");
    if (!loadWorkConfigs()) {
        // Error already logged, transition to error state
        m_phase = LoadingPhase::ConfigError;
        return;
    }

    // ... existing initialization code ...
}

bool loadWorkConfigs() {
    // Clear any previous configs (supports menu → new game cycle)
    ActionTypeRegistry::Get().clear();
    TaskChainRegistry::Get().clear();
    WorkTypeRegistry::Get().clear();
    PriorityConfig::Get().clear();
    ConfigValidator::clearErrors();

    // Load in dependency order
    std::string basePath = "assets/config/";

    if (!ActionTypeRegistry::Get().loadFromFile(basePath + "actions/action-types.xml")) {
        LOG_ERROR(Game, "Failed to load action-types.xml");
        return false;
    }

    if (!TaskChainRegistry::Get().loadFromFile(basePath + "work/task-chains.xml")) {
        LOG_ERROR(Game, "Failed to load task-chains.xml");
        return false;
    }

    if (!WorkTypeRegistry::Get().loadFromFile(basePath + "work/work-types.xml")) {
        LOG_ERROR(Game, "Failed to load work-types.xml");
        return false;
    }

    if (!PriorityConfig::Get().loadFromFile(basePath + "work/priority-tuning.xml")) {
        LOG_ERROR(Game, "Failed to load priority-tuning.xml");
        return false;
    }

    // TODO: Load mod configs here (future)
    // loadModConfigs(enabledMods);

    // Validate cross-references
    if (!ConfigValidator::validateAll()) {
        LOG_ERROR(Game, "Config validation failed");
        return false;
    }

    LOG_INFO(Game, "Work configuration loaded successfully");
    return true;
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
    switch (m_phase) {
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
    if (InputManager::Get().isKeyPressed(Key::Escape)) {
        sceneManager->switchTo(SceneType::MainMenu);
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
4. `loadWorkConfigs()` loads base + enabled mods in order
5. Mod load order: alphabetical (or explicit load order in mod metadata)

See [task-generation-architecture.md](./task-generation-architecture.md#mod-loading) for mod validation rules.

---

## Summary

| Decision | Choice | Rationale |
|----------|--------|-----------|
| When to load | GameLoadingScene | Enables mod changes without restart |
| Error handling | Show on loading screen, return to menu | Clear feedback for modders |
| Reload support | Clear registries before each load | Clean slate for each game start |
