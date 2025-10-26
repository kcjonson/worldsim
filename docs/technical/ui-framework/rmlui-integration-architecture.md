# RmlUI Integration Architecture

Created: 2025-10-26
Status: Design

## Overview

This document describes the complete architecture for integrating RmlUI into world-sim, including:
- Isolation layer (protect game code from RmlUI dependency)
- Renderer backend (RmlUI → Primitives API)
- Programmatic API wrapper (`.Args{}` style)
- Mixed usage (XML for complex UI, programmatic for simple UI)

**Key Design:** RmlUI is an implementation detail, not a public dependency.

## Architecture Layers

```
┌─────────────────────────────────────────────────────────────┐
│  Game Code                                                  │
│  - Never includes RmlUI headers                            │
│  - Uses UI::IUISystem interface only                       │
└──────────────────────┬──────────────────────────────────────┘
                       │ Depends on
┌──────────────────────▼──────────────────────────────────────┐
│  UI Interface Layer (libs/ui/include/ui/)                   │
│  - IUISystem, IDocument, IElement                          │
│  - Pure abstract interfaces                                │
│  - No implementation details                               │
└──────────────────────┬──────────────────────────────────────┘
                       │ Implemented by
┌──────────────────────▼──────────────────────────────────────┐
│  RmlUI Adapter (libs/ui/src/rmlui/)                        │
│  - RmlUISystem (implements IUISystem)                      │
│  - RmlUIDocument (implements IDocument)                    │
│  - Includes RmlUI headers ONLY in .cpp files               │
└──────────────────────┬──────────────────────────────────────┘
                       │ Uses
┌──────────────────────▼──────────────────────────────────────┐
│  RmlUI Library                                              │
│  - HTML/CSS parsing                                        │
│  - Layout engine (flexbox)                                 │
│  - DOM tree management                                     │
└──────────────────────┬──────────────────────────────────────┘
                       │ Calls
┌──────────────────────▼──────────────────────────────────────┐
│  RmlUI Render Backend (libs/ui/src/rmlui/backend.cpp)     │
│  - Implements Rml::RenderInterface                         │
│  - Converts RmlUI geometry → Primitives API calls          │
└──────────────────────┬──────────────────────────────────────┘
                       │ Calls
┌──────────────────────▼──────────────────────────────────────┐
│  Primitives API (libs/renderer/primitives.h)               │
│  - DrawRect, DrawText, DrawTexture                         │
│  - Shared by all rendering code                            │
└──────────────────────┬──────────────────────────────────────┘
                       │ Uses
┌──────────────────────▼──────────────────────────────────────┐
│  Your Renderer (libs/renderer/)                            │
│  - OpenGL abstraction                                      │
│  - Batching, VBOs, shaders                                 │
└─────────────────────────────────────────────────────────────┘
```

## Component Design

### 1. UI Interface Layer

**Public headers - game code depends on these:**

```cpp
// libs/ui/include/ui/ui_system.h

namespace UI {

// Forward declarations only - no RmlUI types!
class IDocument;
class IElement;

// Pure interface - no implementation
class IUISystem {
public:
    virtual ~IUISystem() = default;

    // Document management
    virtual std::unique_ptr<IDocument> LoadDocument(const std::string& path) = 0;
    virtual std::unique_ptr<IDocument> CreateDocument(const std::string& name) = 0;

    // Lifecycle
    virtual void Update(float deltaTime) = 0;
    virtual void Render() = 0;

    // Input (forwarded from game's input system)
    virtual void OnMouseMove(int x, int y) = 0;
    virtual void OnMouseButton(int button, bool down) = 0;
    virtual void OnKey(int key, bool down) = 0;
    virtual void OnTextInput(const std::string& text) = 0;
};

// Factory function - only way to create UI system
std::unique_ptr<IUISystem> CreateUISystem(Renderer::Renderer* renderer);

} // namespace UI
```

```cpp
// libs/ui/include/ui/ui_document.h

namespace UI {

class IDocument {
public:
    virtual ~IDocument() = default;

    // Visibility
    virtual void Show() = 0;
    virtual void Hide() = 0;
    virtual bool IsVisible() const = 0;

    // Data binding
    virtual void SetDataModel(const json& data) = 0;
    virtual json GetDataModel() const = 0;

    // Event binding
    virtual void BindEvent(const std::string& elementId,
                          const std::string& eventName,
                          std::function<void()> handler) = 0;

    // Element access
    virtual IElement* GetElementById(const std::string& id) = 0;

    // Programmatic element creation
    virtual IElement* CreateElement(const std::string& type) = 0;
    virtual void SetRootElement(IElement* root) = 0;
};

} // namespace UI
```

```cpp
// libs/ui/include/ui/ui_element.h

namespace UI {

class IElement {
public:
    virtual ~IElement() = default;

    // Properties
    virtual void SetProperty(const std::string& name, const std::string& value) = 0;
    virtual std::string GetProperty(const std::string& name) const = 0;

    // Styling (CSS-like)
    virtual void SetStyle(const std::string& property, const std::string& value) = 0;

    // Content
    virtual void SetInnerHTML(const std::string& html) = 0;
    virtual void SetTextContent(const std::string& text) = 0;

    // Hierarchy
    virtual void AppendChild(IElement* child) = 0;
    virtual void RemoveChild(IElement* child) = 0;

    // Events
    virtual void AddEventListener(const std::string& eventName,
                                  std::function<void()> handler) = 0;
};

} // namespace UI
```

**Key points:**
- ✅ Pure abstract interfaces
- ✅ No RmlUI types in public headers
- ✅ Game code can't accidentally depend on RmlUI
- ✅ Can swap implementations without changing game code

### 2. RmlUI Adapter Implementation

**Hidden in .cpp files - game never sees these:**

```cpp
// libs/ui/src/rmlui/rmlui_system.h (PRIVATE header, not in include/)

#include <RmlUi/Core.h>
#include "ui/ui_system.h"

namespace UI {
namespace Internal {

class RmlUISystem : public IUISystem {
    Rml::Context* m_context;
    Renderer::Renderer* m_renderer;

public:
    RmlUISystem(Renderer::Renderer* renderer);
    ~RmlUISystem();

    // IUISystem implementation
    std::unique_ptr<IDocument> LoadDocument(const std::string& path) override;
    std::unique_ptr<IDocument> CreateDocument(const std::string& name) override;
    void Update(float deltaTime) override;
    void Render() override;
    void OnMouseMove(int x, int y) override;
    // ... other methods
};

class RmlUIDocument : public IDocument {
    Rml::ElementDocument* m_doc;

public:
    explicit RmlUIDocument(Rml::ElementDocument* doc);

    // IDocument implementation
    void Show() override { m_doc->Show(); }
    void Hide() override { m_doc->Hide(); }
    // ... other methods
};

class RmlUIElement : public IElement {
    Rml::Element* m_element;

public:
    explicit RmlUIElement(Rml::Element* element);

    // IElement implementation
    void SetProperty(const std::string& name, const std::string& value) override {
        m_element->SetProperty(name, value);
    }
    // ... other methods
};

} // namespace Internal
} // namespace UI
```

```cpp
// libs/ui/src/rmlui/rmlui_system.cpp

#include "rmlui_system.h"
#include "rmlui_backend.h"

namespace UI {

// Factory implementation
std::unique_ptr<IUISystem> CreateUISystem(Renderer::Renderer* renderer) {
    return std::make_unique<Internal::RmlUISystem>(renderer);
}

namespace Internal {

RmlUISystem::RmlUISystem(Renderer::Renderer* renderer)
    : m_renderer(renderer)
{
    // Initialize RmlUI
    Rml::SetRenderInterface(new RmlUIBackend(renderer));
    Rml::Initialise();

    // Create context
    m_context = Rml::CreateContext(
        "main",
        Rml::Vector2i(1920, 1080)  // Will be resized
    );

    // Load fonts, etc.
}

std::unique_ptr<IDocument> RmlUISystem::LoadDocument(const std::string& path) {
    auto doc = m_context->LoadDocument(path);
    if (!doc) {
        return nullptr;
    }
    return std::make_unique<RmlUIDocument>(doc);
}

void RmlUISystem::Update(float deltaTime) {
    m_context->Update();
}

void RmlUISystem::Render() {
    m_context->Render();
}

} // namespace Internal
} // namespace UI
```

### 3. RmlUI Render Backend

**Converts RmlUI rendering to Primitives API:**

```cpp
// libs/ui/src/rmlui/rmlui_backend.h

#include <RmlUi/Core.h>
#include "renderer/primitives.h"

namespace UI {
namespace Internal {

class RmlUIBackend : public Rml::RenderInterface {
    Renderer::Renderer* m_renderer;
    std::unordered_map<Rml::TextureHandle, Renderer::TextureHandle> m_textureMap;

public:
    explicit RmlUIBackend(Renderer::Renderer* renderer);

    // Rml::RenderInterface implementation
    void RenderGeometry(
        Rml::Vertex* vertices, int num_vertices,
        int* indices, int num_indices,
        Rml::TextureHandle texture,
        const Rml::Vector2f& translation
    ) override;

    Rml::CompiledGeometryHandle CompileGeometry(
        Rml::Vertex* vertices, int num_vertices,
        int* indices, int num_indices,
        Rml::TextureHandle texture
    ) override;

    void RenderCompiledGeometry(
        Rml::CompiledGeometryHandle geometry,
        const Rml::Vector2f& translation
    ) override;

    void ReleaseCompiledGeometry(Rml::CompiledGeometryHandle geometry) override;

    void EnableScissorRegion(bool enable) override;
    void SetScissorRegion(int x, int y, int width, int height) override;

    bool LoadTexture(Rml::TextureHandle& texture_handle,
                    Rml::Vector2i& texture_dimensions,
                    const std::string& source) override;

    bool GenerateTexture(Rml::TextureHandle& texture_handle,
                        const Rml::byte* source,
                        const Rml::Vector2i& source_dimensions) override;

    void ReleaseTexture(Rml::TextureHandle texture_handle) override;

    void SetTransform(const Rml::Matrix4f* transform) override;
};

} // namespace Internal
} // namespace UI
```

```cpp
// libs/ui/src/rmlui/rmlui_backend.cpp

#include "rmlui_backend.h"

namespace UI {
namespace Internal {

void RmlUIBackend::RenderGeometry(
    Rml::Vertex* vertices, int num_vertices,
    int* indices, int num_indices,
    Rml::TextureHandle texture,
    const Rml::Vector2f& translation)
{
    // Convert RmlUI texture handle to our texture handle
    Renderer::TextureHandle ourTexture = m_textureMap[texture];

    // RmlUI gives us triangles in batches
    // For each quad (6 indices = 2 triangles):
    for (int i = 0; i < num_indices; i += 6) {
        // Extract quad from triangles
        auto& v0 = vertices[indices[i + 0]];
        auto& v1 = vertices[indices[i + 1]];
        auto& v2 = vertices[indices[i + 2]];

        // Calculate bounds
        Renderer::Rect bounds = {
            v0.position.x + translation.x,
            v0.position.y + translation.y,
            v2.position.x - v0.position.x,
            v2.position.y - v0.position.y
        };

        // UV coords
        Renderer::Rect uv = {
            v0.tex_coord.x,
            v0.tex_coord.y,
            v2.tex_coord.x - v0.tex_coord.x,
            v2.tex_coord.y - v0.tex_coord.y
        };

        // Color (RmlUI uses per-vertex colors)
        Renderer::Color color = {
            v0.colour.red / 255.0f,
            v0.colour.green / 255.0f,
            v0.colour.blue / 255.0f,
            v0.colour.alpha / 255.0f
        };

        // Draw via primitives API
        if (texture) {
            Renderer::DrawTexture(ourTexture, bounds, uv, color);
        } else {
            Renderer::DrawRect(bounds, color);
        }
    }
}

void RmlUIBackend::EnableScissorRegion(bool enable) {
    if (!enable) {
        Renderer::PopScissor();
    }
}

void RmlUIBackend::SetScissorRegion(int x, int y, int width, int height) {
    Renderer::PushScissor({
        static_cast<float>(x),
        static_cast<float>(y),
        static_cast<float>(width),
        static_cast<float>(height)
    });
}

bool RmlUIBackend::LoadTexture(
    Rml::TextureHandle& texture_handle,
    Rml::Vector2i& texture_dimensions,
    const std::string& source)
{
    // Load texture via our renderer
    auto texture = m_renderer->LoadTexture(source);
    if (!texture.IsValid()) {
        return false;
    }

    // Store mapping
    texture_handle = reinterpret_cast<Rml::TextureHandle>(m_textureMap.size() + 1);
    m_textureMap[texture_handle] = texture;

    // Get dimensions
    texture_dimensions.x = m_renderer->GetTextureWidth(texture);
    texture_dimensions.y = m_renderer->GetTextureHeight(texture);

    return true;
}

} // namespace Internal
} // namespace UI
```

## Usage Patterns

### Pattern 1: XML-Based UI (Complex Panels)

**For inventory, skill trees, complex layouts:**

```cpp
// Game code
void ShowInventoryPanel(UI::IUISystem* uiSystem) {
    auto doc = uiSystem->LoadDocument("ui/inventory.rml");

    // Bind data
    json inventoryData = {
        {"items", player->GetInventory()},
        {"maxSlots", 40}
    };
    doc->SetDataModel(inventoryData);

    // Bind events
    doc->BindEvent("close-btn", "click", []() {
        CloseInventory();
    });

    doc->BindEvent("use-item", "click", [&]() {
        UseSelectedItem();
    });

    doc->Show();
}
```

```html
<!-- ui/inventory.rml -->
<rml>
<head>
    <link type="text/rcss" href="inventory.rcss"/>
</head>
<body>
    <div class="inventory-panel">
        <h1>Inventory</h1>

        <div class="item-grid">
            <div data-for="item in items" class="item-slot">
                <img src="{item.icon}"/>
                <span>{item.name}</span>
                <span class="quantity">{item.quantity}</span>
            </div>
        </div>

        <button id="close-btn">Close</button>
    </div>
</body>
</rml>
```

```css
/* ui/inventory.rcss */
.inventory-panel {
    width: 600px;
    height: 800px;
    background-color: #2a2a2a;
    padding: 20px;
}

.item-grid {
    display: flex;
    flex-wrap: wrap;
    gap: 10px;
}

.item-slot {
    width: 64px;
    height: 64px;
    background-color: #3a3a3a;
    border: 2px solid #555;
}

.item-slot:hover {
    border-color: #fff;
}
```

### Pattern 2: Programmatic UI (Simple Panels)

**For simple menus, dialogs:**

```cpp
void ShowMainMenu(UI::IUISystem* uiSystem) {
    auto doc = uiSystem->CreateDocument("main_menu");

    // Create container
    auto container = doc->CreateElement("div");
    container->SetStyle("width", "400px");
    container->SetStyle("display", "flex");
    container->SetStyle("flex-direction", "column");
    container->SetStyle("gap", "20px");
    container->SetStyle("padding", "50px");

    // Create buttons
    auto newGameBtn = doc->CreateElement("button");
    newGameBtn->SetTextContent("New Game");
    newGameBtn->SetStyle("width", "200px");
    newGameBtn->SetStyle("height", "60px");
    newGameBtn->AddEventListener("click", []() { StartNewGame(); });
    container->AppendChild(newGameBtn);

    auto loadGameBtn = doc->CreateElement("button");
    loadGameBtn->SetTextContent("Load Game");
    loadGameBtn->SetStyle("width", "200px");
    loadGameBtn->SetStyle("height", "60px");
    loadGameBtn->AddEventListener("click", []() { ShowLoadScreen(); });
    container->AppendChild(loadGameBtn);

    doc->SetRootElement(container);
    doc->Show();
}
```

### Pattern 3: Hybrid (XML Structure + Programmatic Updates)

**For dynamic content with static layout:**

```cpp
void ShowPlayerList(UI::IUISystem* uiSystem, const std::vector<Player>& players) {
    // Load static structure from XML
    auto doc = uiSystem->LoadDocument("ui/player_list.rml");

    // Get container from XML
    auto container = doc->GetElementById("player-container");

    // Populate dynamically
    for (auto& player : players) {
        auto playerCard = doc->CreateElement("div");
        playerCard->SetProperty("class", "player-card");

        // Player name
        auto name = doc->CreateElement("span");
        name->SetTextContent(player.name);
        playerCard->AppendChild(name);

        // Health bar (programmatic)
        auto healthBar = doc->CreateElement("div");
        healthBar->SetStyle("width", std::to_string(player.health * 100) + "px");
        healthBar->SetStyle("height", "5px");
        healthBar->SetStyle("background-color", "#ff0000");
        playerCard->AppendChild(healthBar);

        container->AppendChild(playerCard);
    }

    doc->Show();
}
```

```html
<!-- ui/player_list.rml -->
<rml>
<body>
    <div class="player-list-panel">
        <h1>Players</h1>
        <div id="player-container" class="player-grid">
            <!-- Populated programmatically -->
        </div>
    </div>
</body>
</rml>
```

## Integration with Game Loop

```cpp
// apps/world-sim/src/main.cpp

class Game {
    std::unique_ptr<Renderer::Renderer> m_renderer;
    std::unique_ptr<UI::IUISystem> m_uiSystem;
    std::unique_ptr<UI::IDocument> m_currentMenu;

public:
    void Initialize() {
        // Create renderer
        m_renderer = std::make_unique<Renderer::Renderer>();

        // Initialize primitives (used by RmlUI backend)
        Renderer::InitPrimitives(m_renderer.get());

        // Create UI system (internally creates RmlUI)
        m_uiSystem = UI::CreateUISystem(m_renderer.get());

        // Show main menu
        m_currentMenu = m_uiSystem->LoadDocument("ui/main_menu.rml");
        m_currentMenu->Show();
    }

    void Update(float dt) {
        // Update UI system
        m_uiSystem->Update(dt);

        // Update game logic
        // ...
    }

    void Render() {
        m_renderer->Clear();

        // 1. Render game world
        Renderer::BeginPrimitiveFrame();
        RenderTiles();
        RenderEntities();
        Renderer::EndPrimitiveFrame();

        // 2. Render world-space UI (health bars, etc.)
        Renderer::BeginPrimitiveFrame();
        for (auto& entity : m_entities) {
            RenderHealthBar(entity);
        }
        Renderer::EndPrimitiveFrame();

        // 3. Render screen-space UI (RmlUI)
        m_uiSystem->Render();  // Calls primitives internally

        m_renderer->Present();
    }

    void OnInput(InputEvent event) {
        // Forward input to UI system
        if (event.type == InputEvent::MouseMove) {
            m_uiSystem->OnMouseMove(event.x, event.y);
        }
        // ... other input events
    }
};
```

## File Organization

```
libs/ui/
├── include/ui/
│   ├── ui_system.h         # IUISystem interface (public)
│   ├── ui_document.h       # IDocument interface (public)
│   └── ui_element.h        # IElement interface (public)
│
└── src/
    ├── rmlui/              # RmlUI implementation (private)
    │   ├── rmlui_system.h
    │   ├── rmlui_system.cpp
    │   ├── rmlui_document.h
    │   ├── rmlui_document.cpp
    │   ├── rmlui_element.h
    │   ├── rmlui_element.cpp
    │   ├── rmlui_backend.h     # Render backend
    │   └── rmlui_backend.cpp
    │
    └── ui_system.cpp       # Factory implementation
```

## CMake Integration

```cmake
# libs/ui/CMakeLists.txt

add_library(ui
    # Public interfaces
    src/ui_system.cpp

    # RmlUI implementation (private)
    src/rmlui/rmlui_system.cpp
    src/rmlui/rmlui_document.cpp
    src/rmlui/rmlui_element.cpp
    src/rmlui/rmlui_backend.cpp
)

target_include_directories(ui
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src  # For private headers
)

# Link RmlUI privately (not exposed to consumers)
find_package(RmlUi CONFIG REQUIRED)
target_link_libraries(ui
    PRIVATE
        RmlUi::RmlUi
        freetype  # Required by RmlUI
    PUBLIC
        renderer  # For Primitives API
        foundation  # For json, etc.
)

target_compile_features(ui PUBLIC cxx_std_20)
```

**Key:** RmlUI is linked PRIVATELY - game code can't accidentally depend on it.

## Testing Strategy

**Unit tests (mock UI system):**
```cpp
// tests/ui_tests.cpp

class MockUISystem : public UI::IUISystem {
    // Implement interface without RmlUI
    // For testing game logic
};

TEST(GameLogic, InventoryOpensOnKeyPress) {
    MockUISystem mockUI;
    Game game(&mockUI);

    game.OnKeyPress(Key::I);

    EXPECT_TRUE(mockUI.WasDocumentLoaded("ui/inventory.rml"));
}
```

**Integration tests (real RmlUI):**
```cpp
TEST(RmlUIIntegration, LoadsInventoryDocument) {
    auto renderer = CreateTestRenderer();
    auto uiSystem = UI::CreateUISystem(renderer.get());

    auto doc = uiSystem->LoadDocument("test_assets/inventory.rml");

    EXPECT_NE(doc, nullptr);
    EXPECT_TRUE(doc->GetElementById("item-grid") != nullptr);
}
```

## Migration Strategy

**If we need to switch from RmlUI later:**

1. ✅ **Game code unchanged** (uses IUISystem interface)
2. ❌ **Rewrite adapter** (~500 lines in src/rmlui/)
3. ❌ **Convert .rml files** (if using XML heavily)
4. ✅ **Primitives API unchanged** (new backend uses same API)

**Estimated migration time:**
- Small project (20 UI screens): 1-2 weeks
- Large project (100+ screens with heavy XML): 4-8 weeks

## Performance Considerations

**Overhead of isolation layer:**
- Virtual function calls: ~1-2ns each (negligible)
- No extra allocations
- No runtime type checking

**RmlUI backend overhead:**
- Converting RmlUI geometry: ~0.1ms for typical UI
- Negligible compared to actual rendering

**Total overhead: <0.2ms per frame** ✅

## Related Documentation

- [library-isolation-strategy.md](./library-isolation-strategy.md) - Why we isolate
- [primitive-rendering-api.md](./primitive-rendering-api.md) - What RmlUI backend uses
- [rendering-boundaries.md](./rendering-boundaries.md) - When to use RmlUI vs custom
- [ui-architecture-fundamentals.md](./ui-architecture-fundamentals.md) - Scene graphs, retained mode

## Revision History

- 2025-10-26: Initial RmlUI integration architecture design
