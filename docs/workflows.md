# Development Workflows

Common development tasks and processes for the worldsim project.

---

## Documentation Workflows

### Recording Implementation Work

When you complete significant work (features, refactors, major fixes):

1. **Create a development log entry:**
   ```
   docs/development-log/entries/YYYY-MM-DD-topic-name.md
   ```

2. **Use this template:**
   ```markdown
   # [Date] - [Title]
   
   ## Summary
   Brief description of what was accomplished.
   
   ## Details
   - What was built/changed
   - Technical decisions made
   - Files created/modified
   
   ## Related Documentation
   - Links to specs created/updated
   
   ## Next Steps
   What should happen next (if applicable)
   ```

3. **Add entry to the index:**
   - Edit `docs/development-log/README.md`
   - Add link under appropriate date section (newest first)

4. **Update status.md:**
   - Mark completed tasks
   - Update "Current Focus" if applicable

**Key principle:** Development log entries are immutable history. Don't edit old entries — create new ones if things change.

### Creating Design Documents

For new game features or systems:

1. **Determine location:**
   - Player-facing features → `docs/design/game-systems/` or `docs/design/features/`
   - Technical implementation → `docs/technical/`

2. **Check MVP scope:**
   - Reference `docs/design/mvp-scope.md`
   - Don't create "MVP Scope" sections in individual docs
   - Instead write: "**MVP Status:** See [MVP Scope](../mvp-scope.md) — This feature: Phase X"

3. **Use consistent structure:**
   - Overview / Purpose
   - Details / Mechanics
   - Related Documents (links)
   - Historical Addendum (if consolidating/superseding other docs)

4. **Update indexes:**
   - Add to `docs/design/INDEX.md` or `docs/technical/INDEX.md`
   - Add cross-references in related documents

### Making Technical Decisions

When choosing libraries, architectures, or approaches:

1. **Document options:**
   - Create comparative analysis in technical docs
   - List pros/cons objectively

2. **Prototype if significant:**
   - Test in ui-sandbox
   - Measure performance

3. **Record decision:**
   - Add to `docs/technical/library-decisions.md` for libraries
   - Add to relevant technical doc for architecture decisions

4. **Log the work:**
   - Create development log entry with rationale

---

## Code Workflows

### Adding New Libraries

1. **Evaluate and document:**
   - Add options to relevant technical doc
   - Get decision recorded in library-decisions.md

2. **Add to vcpkg:**
   ```json
   // vcpkg.json
   {
     "dependencies": [
       "new-library-name"
     ]
   }
   ```

3. **Add to CMake:**
   ```cmake
   find_package(NewLibrary CONFIG REQUIRED)
   target_link_libraries(your-target PRIVATE NewLibrary::NewLibrary)
   ```

4. **Update documentation:**
   - library-decisions.md
   - Relevant technical docs

### Creating New Library (in libs/)

1. **Create directory structure:**
   ```
   libs/new-lib/
   ├── CMakeLists.txt
   ├── include/new-lib/
   │   └── public_header.h
   ├── src/
   │   └── implementation.cpp
   └── tests/
       └── new_lib_tests.cpp
   ```

2. **Add to root CMakeLists.txt:**
   ```cmake
   add_subdirectory(libs/new-lib)
   ```

3. **Follow dependency rules:**
   - Check `docs/technical/monorepo-structure.md`
   - Don't create circular dependencies

### Running Tests

```bash
# Build with tests
cmake -B build -DBUILD_TESTING=ON
cmake --build build

# Run all tests
ctest --test-dir build

# Run specific test
./build/libs/new-lib/tests/new_lib_tests
```

### Creating Scenes (in ui-sandbox)

1. **Create scene class:**
   ```cpp
   class NewScene : public IScene {
   public:
       void OnEnter() override;
       void OnExit() override;
       void Update(float dt) override;
       void Render() override;
   };
   ```

2. **Register in SceneManager:**
   ```cpp
   sceneManager.RegisterScene("NewScene", std::make_unique<NewScene>());
   ```

3. **Add keyboard shortcut** (if desired) in main loop

---

## Debugging Workflows

### Using the Developer Client

1. **Start application** (ui-sandbox or world-sim)
2. **Open browser** to `http://localhost:8080` (or 8081/8082)
3. **View real-time metrics:**
   - Frame time graph
   - Memory usage
   - Log streaming

### Using Tracy Profiler

1. **Enable Tracy** in CMake:
   ```cmake
   set(TRACY_ENABLE ON)
   ```

2. **Add instrumentation:**
   ```cpp
   ZoneScoped;  // Function scope
   ZoneScopedN("CustomName");  // Named scope
   ```

3. **Run Tracy server** and connect

### Using RenderDoc

1. **Launch application** through RenderDoc
2. **Capture frame** (F12 or trigger)
3. **Analyze:**
   - Draw call count
   - State changes
   - Shader performance

---

## Quick Reference

| Task | Command/Location |
|------|------------------|
| Build | `cmake --build build` |
| Run tests | `ctest --test-dir build` |
| Run ui-sandbox | `./build/apps/ui-sandbox/ui-sandbox` |
| View dev log | `docs/development-log/README.md` |
| Check MVP scope | `docs/design/mvp-scope.md` |
| Library decisions | `docs/technical/library-decisions.md` |
| Project status | `docs/status.md` |
