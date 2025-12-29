# GameScene Refactoring Plan

## Problem Statement

GameScene is becoming a god class, accumulating responsibilities that should be separate subsystems:
- Placement logic (modes, spawning, relocating)
- Selection handling (priorities, state, indicators)
- UI callback coordination
- ECS world management
- Rendering orchestration

As AI and gameplay systems grow, this will become unmaintainable (potentially 10,000+ lines).

## Current State

**GameScene contains:**
- `PlacementMode` state machine
- `GhostRenderer` for placement preview
- `m_relocatingEntityId` for furniture relocation
- `handleBuildToggle()`, `handleBuildItemSelected()`, `handlePlaceFurniture()`
- `spawnPlacedEntity()`
- Selection state (`Selection` variant)
- `handleEntityClick()` with priority-based selection
- `renderSelectionIndicator()`
- All UI callbacks wired up inline

## Target Architecture

### 1. PlacementSystem

Extract all placement logic into a dedicated subsystem.

```
scenes/game/world/placement/
├── PlacementSystem.h/cpp    # Main coordinator
├── PlacementMode.h/cpp      # State machine (exists, enhance)
├── GhostRenderer.h/cpp      # Preview rendering (exists)
└── PlacementValidator.h/cpp # Future: collision/validity checking
```

**PlacementSystem responsibilities:**
- Own PlacementMode and GhostRenderer
- Handle spawning new entities
- Handle relocating existing entities (packaged → placed)
- Provide callbacks for UI integration
- Work with ANY placeable entity (stations, furniture, walls, machines, etc.)

**Interface sketch:**
```cpp
class PlacementSystem {
public:
    struct Args {
        std::function<ecs::EntityID(const std::string&, Vec2)> onSpawn;
        std::function<void(ecs::EntityID, Vec2)> onRelocate;
    };

    void beginPlacement(const std::string& defName);
    void beginRelocation(ecs::EntityID entity, const std::string& defName);
    void updateGhostPosition(Vec2 worldPos);
    bool tryPlace();
    void cancel();

    void render(const WorldCamera& camera, int w, int h);

    [[nodiscard]] bool isActive() const;
    [[nodiscard]] PlacementState state() const;
};
```

### 2. SelectionSystem

Extract all selection logic into a dedicated subsystem.

```
scenes/game/world/selection/
├── SelectionSystem.h/cpp      # Main coordinator
├── SelectionTypes.h           # Variants (exists as Selection.h)
├── SelectionPriorities.h      # Priority constants and logic
└── SelectionRenderer.h/cpp    # Indicator rendering
```

**SelectionSystem responsibilities:**
- Own current selection state
- Handle click-to-select with priority ordering
- Provide selection to UI for display
- Render selection indicators

**Selection priorities (current):**
1. Priority 1.0: Colonists (ecs::Colonist)
2. Priority 1.5: Crafting stations (ecs::WorkQueue)
3. Priority 1.6: Storage containers (ecs::Inventory without WorkQueue/Colonist)
4. Priority 2.0: World entities (placed assets with capabilities)

**Interface sketch:**
```cpp
class SelectionSystem {
public:
    void handleClick(Vec2 screenPos, const WorldCamera& camera,
                     const ecs::World& world, int viewportW, int viewportH);
    void clearSelection();

    void render(const WorldCamera& camera, int w, int h);

    [[nodiscard]] const Selection& current() const;
    [[nodiscard]] bool hasSelection() const;
};
```

### 3. AI System Organization (Future)

The AI/colonist logic will grow significantly. Current organization:
- `AIDecisionSystem` - decides what colonist should do
- `ActionSystem` - executes actions
- `NeedsDecaySystem` - decays needs over time
- `VisionSystem` - updates what colonist can see

**Future organization:**
```
libs/engine/ecs/systems/
├── ai/
│   ├── AICoordinator.h/cpp      # Orchestrates AI subsystems
│   ├── DecisionSystem.h/cpp     # What to do next (utility AI)
│   ├── NeedsSystem.h/cpp        # Need decay and evaluation
│   ├── VisionSystem.h/cpp       # Perception
│   └── MemorySystem.h/cpp       # Knowledge persistence
├── action/
│   ├── ActionSystem.h/cpp       # Action execution coordinator
│   └── handlers/                # Per-action-type handlers
│       ├── GatherHandler.h/cpp
│       ├── CraftHandler.h/cpp
│       ├── HaulHandler.h/cpp
│       └── ...
└── movement/
    └── MovementSystem.h/cpp
```

### 4. Refactored GameScene

After extraction, GameScene becomes a thin coordinator:

```cpp
class GameScene : public engine::IScene {
    // Core systems
    std::unique_ptr<ecs::World> ecsWorld;
    std::unique_ptr<world_sim::GameUI> gameUI;

    // World rendering
    std::unique_ptr<engine::world::ChunkManager> m_chunkManager;
    std::unique_ptr<engine::world::WorldCamera> m_camera;
    std::unique_ptr<engine::world::ChunkRenderer> m_renderer;
    std::unique_ptr<engine::world::EntityRenderer> m_entityRenderer;

    // Game subsystems (extracted)
    std::unique_ptr<world_sim::PlacementSystem> placementSystem;
    std::unique_ptr<world_sim::SelectionSystem> selectionSystem;

    // Methods become thin wrappers
    void onEnter() override;
    void onUpdate(float dt) override;
    void onRender(int w, int h) override;
    bool handleEvent(InputEvent& e) override;
};
```

## Naming Conventions

Use generic terms that work for all placeables:
- ~~`onPlaceFurniture`~~ → `onBeginPlacement` or `onPlacePackaged`
- ~~`m_relocatingFurnitureId`~~ → `m_relocatingEntityId`
- ~~`handlePlaceFurniture`~~ → `handlePlacePackaged`
- `FurnitureSelection` - keep as-is (furniture IS a distinct category from stations)
- `Packaged` component - already generic, works for any placeable

## Migration Strategy

### Phase 1: PlacementSystem Extraction
1. Create `PlacementSystem` class
2. Move `PlacementMode`, `GhostRenderer` ownership
3. Move spawning/relocation logic
4. Update GameScene to use PlacementSystem
5. Update UI callbacks to go through PlacementSystem

### Phase 2: SelectionSystem Extraction
1. Create `SelectionSystem` class
2. Move `Selection` state ownership
3. Move click handling and priority logic
4. Move indicator rendering
5. Update GameScene and UI to use SelectionSystem

### Phase 3: AI System Reorganization (separate epic)
1. Evaluate current AI complexity
2. Design handler pattern for actions
3. Extract action handlers
4. Consider behavior tree or utility AI improvements

## Success Criteria

- GameScene < 400 lines (currently ~900)
- PlacementSystem and SelectionSystem are testable in isolation
- Adding new placeable types doesn't touch GameScene
- Adding new selection types doesn't touch GameScene
- Clear separation of concerns
