# UI Data Binding and ViewModel Pattern

This document describes the data binding architecture for connecting ECS game state to UI components.

## Problem Statement

The current UI system has ad-hoc data flow:
- Panels poll the full ECS world every frame
- Each panel implements its own change detection
- State is scattered (GameScene::selection, GameUI::taskListExpanded, per-panel caches)
- No clear answer to "where does UI state X live?"

As we build 20+ screens from the main-game-ui-design spec, this will lead to:
- Duplicated dirty-checking logic across panels
- Inconsistent update behavior
- Hard-to-debug state bugs
- Difficulty persisting UI state

## Design Goals

1. **Clear data ownership** - Every piece of UI state has ONE authoritative source
2. **Efficient updates** - Only refresh UI when relevant data changes
3. **Testability** - ViewModels can be unit tested without rendering
4. **Simplicity** - Build on existing patterns (adapters), don't over-engineer

## Architecture: ViewModel Pattern

### Overview

```
┌─────────────┐     ┌──────────────┐     ┌─────────────┐
│  ECS World  │────▶│  ViewModel   │────▶│  UI Panel   │
│  (source)   │     │  (transform) │     │  (render)   │
└─────────────┘     └──────────────┘     └─────────────┘
                           │
                           ▼
                    ┌──────────────┐
                    │  UI State    │
                    │  (owned)     │
                    └──────────────┘
```

Each panel has a corresponding **ViewModel** that:
1. **Transforms** ECS data into UI-friendly format
2. **Owns** UI-specific state (selected tab, expanded sections, etc.)
3. **Detects changes** and signals when refresh is needed
4. **Caches** expensive computations

### ViewModel Structure

```cpp
// libs/ui/viewmodel/ColonistListViewModel.h

struct ColonistSummary {
    ecs::EntityID id;
    std::string name;
    float mood;           // 0.0-1.0
    std::string activity; // "Eating", "Sleeping", etc.
    StatusColor status;   // Green, Yellow, Red, Gray
};

class ColonistListViewModel {
public:
    // --- Data (read by UI) ---
    const std::vector<ColonistSummary>& colonists() const { return m_colonists; }
    std::optional<ecs::EntityID> selectedId() const { return m_selectedId; }

    // --- UI State (owned by ViewModel) ---
    void setSelectedId(std::optional<ecs::EntityID> id);

    // --- Update from ECS ---
    // Returns true if data changed (UI should refresh)
    bool refresh(const ecs::World& world);

    // --- Signals (optional, for push-based updates) ---
    // Can add later if polling proves insufficient
    // Signal<> onDataChanged;
    // Signal<ecs::EntityID> onSelectionChanged;

private:
    std::vector<ColonistSummary> m_colonists;
    std::optional<ecs::EntityID> m_selectedId;

    // Change detection
    size_t m_lastColonistCount = 0;
    std::vector<float> m_lastMoods;  // For detecting mood changes
};
```

### Implementation Pattern

```cpp
// ColonistListViewModel.cpp

bool ColonistListViewModel::refresh(const ecs::World& world) {
    bool changed = false;

    // Rebuild colonist list
    std::vector<ColonistSummary> newColonists;
    for (auto [entity, colonist, needs, task] :
         world.view<ecs::Colonist, ecs::NeedsComponent, ecs::Task>()) {

        newColonists.push_back({
            .id = entity,
            .name = colonist.name,
            .mood = needs.getOverallMood(),
            .activity = task.currentActivity,
            .status = computeStatusColor(needs)
        });
    }

    // Detect structural changes (add/remove colonists)
    if (newColonists.size() != m_colonists.size()) {
        changed = true;
    }

    // Detect value changes (mood, activity)
    for (size_t i = 0; i < newColonists.size() && i < m_colonists.size(); ++i) {
        if (newColonists[i].mood != m_colonists[i].mood ||
            newColonists[i].activity != m_colonists[i].activity) {
            changed = true;
            break;
        }
    }

    if (changed) {
        m_colonists = std::move(newColonists);
    }

    return changed;
}
```

### Panel Usage

```cpp
// ColonistListPanel.cpp

class ColonistListPanel : public Container {
public:
    void update(float deltaTime, const ecs::World& world) override {
        // Let ViewModel handle change detection
        if (m_viewModel.refresh(world)) {
            rebuildUI();  // Only rebuild when data actually changed
        }
    }

private:
    ColonistListViewModel m_viewModel;

    void rebuildUI() {
        clearChildren();
        for (const auto& colonist : m_viewModel.colonists()) {
            addChild(createColonistCard(colonist));
        }
    }
};
```

## UI State Categories

### Category 1: Game State (ECS-owned)
Data that affects game simulation. Lives in ECS components.

Examples:
- Colonist health, needs, inventory
- Building contents, production queues
- Resource counts
- Game time, speed

**ViewModel role:** Read-only transform to UI format

### Category 2: Selection State (Shared)
Data about what's selected/focused. Affects both UI and game interactions.

Examples:
- Selected entity
- Hovered entity
- Multi-selection set

**Location:** Central UIState struct, shared by all ViewModels

```cpp
struct UIState {
    Selection selection;  // std::variant<NoSelection, ColonistSelection, ...>
    std::set<ecs::EntityID> multiSelection;
    std::optional<ecs::EntityID> hoveredEntity;
};
```

### Category 3: UI-Only State (ViewModel-owned)
State that only affects UI display, not game simulation.

Examples:
- Which tab is active in colonist details
- Whether resources panel is expanded
- Scroll position in lists
- Pinned resources
- Filter settings in log screen

**Location:** Per-panel ViewModel

### Category 4: Persistent UI State (Save/Load)
UI state that should persist across sessions.

Examples:
- Location bookmarks (Shift+1-9)
- Pinned resources
- Panel visibility preferences
- Last used build category

**Location:** Dedicated UIPreferences struct, serialized separately from game save

```cpp
struct UIPreferences {
    std::array<std::optional<Vec2>, 9> locationBookmarks;
    std::set<DefName> pinnedResources;
    bool colonistListVisible = true;
    bool minimapVisible = true;
    // ... etc
};
```

## Existing Code Migration

### Current: SelectionAdapter
The existing `SelectionAdapter` already follows a ViewModel-like pattern:

```cpp
// Current code
PanelContent SelectionAdapter::adaptColonistStatus(
    const ecs::World& world,
    ecs::EntityID id
);
```

**Migration:** Rename to `ColonistInfoViewModel`, add state ownership:

```cpp
class ColonistInfoViewModel {
public:
    // Existing transform logic
    const PanelContent& content() const;
    bool refresh(const ecs::World& world, ecs::EntityID id);

    // New: UI state ownership
    int activeTab() const { return m_activeTab; }
    void setActiveTab(int tab) { m_activeTab = tab; }

private:
    PanelContent m_content;
    int m_activeTab = 0;
};
```

### Current: EntityInfoPanel
Already has `m_cachedSelection` for change detection and `m_activeTab` for UI state.

**Migration:** Extract ViewModel, panel becomes purely rendering:

```cpp
// Before
class EntityInfoPanel : public Container {
    CachedSelection m_cachedSelection;
    int m_activeTab;
    // ... mixing data and rendering
};

// After
class EntityInfoPanel : public Container {
    EntityInfoViewModel m_viewModel;  // All state here
    // Panel only handles rendering and input
};
```

## Implementation Plan

### Phase 1: UIState Struct
1. Create `libs/ui/state/UIState.h` with Selection and shared state
2. Move `GameScene::selection` → `UIState::selection`
3. Pass `UIState&` through GameUI instead of raw Selection

### Phase 2: First ViewModel (ColonistListPanel)
1. Create `ColonistListViewModel` following pattern above
2. Migrate ColonistListPanel to use ViewModel
3. Verify change detection works correctly

### Phase 3: Migrate EntityInfoPanel
1. Create `EntityInfoViewModel` from existing adapter + panel state
2. Consolidate `CachedSelection`, `m_activeTab`, adapter calls
3. Panel becomes pure rendering

### Phase 4: Remaining Panels
1. TaskListPanel → TaskListViewModel
2. BuildMenu → BuildMenuViewModel
3. etc.

### Phase 5: UIPreferences
1. Create persistent preferences struct
2. Add save/load for UI preferences
3. Wire up to ViewModels

## Testing Strategy

ViewModels can be unit tested without UI:

```cpp
TEST(ColonistListViewModelTest, DetectsNewColonist) {
    ecs::World world;
    ColonistListViewModel vm;

    // Initial state
    EXPECT_TRUE(vm.refresh(world));  // First refresh always returns true
    EXPECT_EQ(vm.colonists().size(), 0);

    // Add colonist
    auto id = world.createEntity();
    world.addComponent<ecs::Colonist>(id, {"Alice"});
    world.addComponent<ecs::NeedsComponent>(id);

    EXPECT_TRUE(vm.refresh(world));  // Should detect change
    EXPECT_EQ(vm.colonists().size(), 1);

    // No change
    EXPECT_FALSE(vm.refresh(world));  // No change to detect
}
```

## Future Extensions

### Push-Based Updates (Optional)
If polling proves insufficient, ViewModels can emit signals:

```cpp
class ColonistListViewModel {
public:
    Signal<> onDataChanged;
    Signal<ecs::EntityID> onColonistAdded;
    Signal<ecs::EntityID> onColonistRemoved;
};
```

### Computed Properties
ViewModels can cache expensive computations:

```cpp
class ResourcesViewModel {
public:
    // Cached hierarchical grouping
    const ResourceTree& groupedResources() const;

    // Only recomputed when raw data changes
    bool refresh(const ecs::World& world);

private:
    std::vector<ResourceEntry> m_rawResources;
    mutable std::optional<ResourceTree> m_groupedCache;
};
```

## Related Documentation

- [event-system.md](./event-system.md) - Input event propagation
- [architecture.md](./architecture.md) - Component hierarchy
- [/docs/design/main-game-ui-design.md](/docs/design/main-game-ui-design.md) - UI requirements
