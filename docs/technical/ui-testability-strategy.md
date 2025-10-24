# UI Testability Strategy

Created: 2025-10-12
Last Updated: 2025-10-12
Status: Active

## Context

In a previous iteration of this project, the UI became untestable by AI agents because there was no way to inspect or verify changes without manual visual inspection. This is a major problem for C++ applications where there's no built-in inspector like web browsers provide.

**Goal:** Make the C++ UI as inspectable and testable as a web application.

## Decision

Implement multiple layers of testability in the UI library:

### 1. Scene Graph Serialization

Export the complete UI hierarchy to JSON at any point in time.

**What's captured:**
```json
{
  "type": "Container",
  "id": "main_menu",
  "visible": true,
  "bounds": {"x": 0, "y": 0, "width": 1920, "height": 1080},
  "children": [
    {
      "type": "Button",
      "id": "create_world_btn",
      "text": "Create World",
      "enabled": true,
      "visible": true,
      "bounds": {"x": 860, "y": 400, "width": 200, "height": 60}
    }
  ]
}
```

**API:**
```cpp
std::string ui::SceneGraph::toJSON();
ui::Element* ui::SceneGraph::findById(const std::string& id);
```

### 2. HTTP Debug Server

Embed a lightweight HTTP server (using cpp-httplib) that runs only in debug builds.

**Endpoints:**
- `GET /ui/tree` - Returns current UI hierarchy as JSON
- `GET /ui/screenshot` - Returns PNG screenshot
- `POST /ui/click` - Simulates click at coordinates or on element ID
- `POST /ui/type` - Simulates keyboard input
- `GET /ui/element/:id` - Returns specific element properties

**Usage:**
```bash
# Query UI state
curl http://localhost:8080/ui/tree

# Click a button
curl -X POST http://localhost:8080/ui/click -d '{"id":"create_world_btn"}'

# Take screenshot
curl http://localhost:8080/ui/screenshot > screenshot.png
```

This allows AI agents (like Claude) to inspect and interact with the UI programmatically.

### 3. Visual Regression Testing

Compare screenshots against golden images using perceptual hashing or pixel-perfect comparison.

**Process:**
1. Capture "golden" screenshots of UI states
2. Store in `tests/golden/`
3. During tests, capture new screenshots
4. Compare using image diff tools (e.g., libpng + custom diff)
5. Fail test if difference exceeds threshold

**API:**
```cpp
testing::captureGolden("main_menu");
testing::compareWithGolden("main_menu", threshold=0.01);
```

### 4. UI Inspector Overlay

In debug builds, show a visual inspector similar to browser DevTools.

**Features:**
- Hover over UI elements to highlight bounds
- Click to select and show properties panel
- Display hierarchy tree in side panel
- Live property editing
- Toggle with F12 key

### 5. Event Recording & Playback

Record user interactions as JSON sequences for regression testing.

**Format:**
```json
{
  "events": [
    {"type": "click", "time": 0.0, "x": 960, "y": 430},
    {"type": "keypress", "time": 1.5, "key": "Enter"},
    {"type": "mouse_move", "time": 2.0, "x": 100, "y": 200}
  ]
}
```

**API:**
```cpp
Recorder::startRecording("test_session");
Recorder::stopRecording();
Player::playback("test_session.json");
```

### 6. Accessibility/Semantic Tree

Similar to web accessibility APIs, create a semantic tree that describes UI purpose, not just appearance.

**Example:**
```json
{
  "role": "button",
  "label": "Create World",
  "enabled": true,
  "focusable": true,
  "state": "normal"
}
```

This allows testing interactions through semantic meaning rather than pixel coordinates.

## Implementation Details

### Core Classes

```cpp
namespace ui {

class Inspector {
public:
    static std::string exportToJSON();
    static void enableHTTPServer(int port = 8080);
    static void enableOverlay(bool enable);
};

class Element {
public:
    virtual std::string toJSON() const;
    std::string getId() const;
    Bounds getBounds() const;
    bool isVisible() const;
    // ... other properties
};

} // namespace ui
```

### Integration with ui-sandbox

The `ui-sandbox` application serves as the primary testing ground:

```bash
# Show specific component
./ui-sandbox --component button

# Enable HTTP server
./ui-sandbox --http-port 8080

# Run in test mode (headless with virtual framebuffer)
./ui-sandbox --test --output screenshot.png
```

## Trade-offs

**Pros:**
- AI agents can test UI changes automatically
- Faster iteration cycle (no manual visual verification)
- Regression tests catch UI bugs
- Debug overlay helps developers too

**Cons:**
- Additional complexity in UI library
- HTTP server adds dependency
- Screenshot tests can be brittle
- Performance overhead in debug builds

**Decision:** The testability benefits far outweigh the complexity cost. Performance overhead only applies to debug builds.

## Alternatives Considered

### Option: Manual Testing Only
**Rejected** - This is what failed in the previous iteration. Not acceptable.

### Option: Unit Tests Only (No Visual Testing)
**Rejected** - UI bugs are often visual. Need screenshot comparison.

### Option: External Testing Framework
**Rejected** - External tools can't access internal C++ state easily. Embedding inspector is more flexible.

## Implementation Status

- [x] Strategy defined
- [ ] Scene graph JSON serialization
- [ ] HTTP debug server
- [ ] Visual regression testing
- [ ] Inspector overlay
- [ ] Event recording/playback
- [ ] Integration with ui-sandbox

## Related Documentation

- Spec: [UI Sandbox Application](/docs/specs/features/ui-framework/ui-sandbox.md)
- Tech: [Monorepo Structure](./monorepo-structure.md)
- Code: `libs/ui/include/ui/inspector/` (once implemented)

## Notes

Start with scene graph JSON export and HTTP server - these provide the most value with least complexity. Add visual regression and inspector overlay in later iterations.
