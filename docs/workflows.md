# Development Workflows

Common tasks and workflows for developing World-Sim.

## Adding a New Library

1. Create directory structure:
   ```bash
   mkdir -p libs/mylib/{subdir,tests}
   ```

2. Create `libs/mylib/CMakeLists.txt`:
   ```cmake
   add_library(mylib
       subdir/file.cpp
   )

   target_include_directories(mylib
       PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
   )

   target_compile_features(mylib PUBLIC cxx_std_20)

   target_link_libraries(mylib
       PUBLIC
           foundation  # Add dependencies
   )
   ```

3. Add to root `CMakeLists.txt` (in dependency order):
   ```cmake
   add_subdirectory(libs/mylib)
   ```

4. Update `CLAUDE.md` project structure section if it's a major library

## Adding Files to a Library

1. Create `.h` and `.cpp` files in appropriate subdirectory:
   ```bash
   touch libs/renderer/gl/shader.h
   touch libs/renderer/gl/shader.cpp
   ```

2. Add `.cpp` file to library's `CMakeLists.txt`:
   ```cmake
   add_library(renderer
       gl/shader.cpp          # Add this
       gl/texture.cpp
   )
   ```

3. No need to list `.h` files (included via parent directory)

## Creating a New Scene

1. Create scene files:
   ```bash
   touch apps/world-sim/scenes/my_scene.h
   touch apps/world-sim/scenes/my_scene.cpp
   ```

2. Add to `apps/world-sim/CMakeLists.txt`:
   ```cmake
   add_executable(world-sim
       scenes/my_scene.cpp    # Add this
       ...
   )
   ```

3. Register scene with scene manager in `main.cpp`:
   ```cpp
   sceneManager.RegisterScene("MyScene", new MyScene());
   ```

## Working with UI Components

### 1. Create Component in ui-sandbox First
```bash
touch apps/ui-sandbox/demos/my_component_demo.cpp
```

Add demo to ui-sandbox CMakeLists.txt and implement demo.

### 2. Test Component
```bash
./build/apps/ui-sandbox/ui-sandbox --component my_component
```

### 3. Test with HTTP Inspector
```bash
./build/apps/ui-sandbox/ui-sandbox --component my_component --http-port 8080
```

Then use curl or browser:
```bash
curl http://localhost:8080/ui/tree
```

### 4. Once Working, Use in Main Game
Add component to `libs/ui/components/` and use in game scenes.

## Running Tests

### All Tests
```bash
ctest --test-dir build --output-on-failure
```

### Specific Test
```bash
ctest --test-dir build -R TestName --output-on-failure
```

### With Verbose Output
```bash
ctest --test-dir build --verbose
```

## Debugging

### VSCode Debugging

1. Set breakpoints in code
2. Press F5 or use Run & Debug panel
3. Choose launch configuration:
   - "(lldb) Launch world-sim"
   - "(lldb) Launch ui-sandbox"

### Command Line Debugging

```bash
lldb ./build/apps/world-sim/world-sim
(lldb) breakpoint set --name main
(lldb) run
(lldb) continue
(lldb) step
(lldb) print variableName
```

## Formatting and Linting

### Format Code
Press `Shift+Alt+F` in VSCode, or:
```bash
clang-format -i path/to/file.cpp path/to/file.h
```

### View Linting Issues
Linting (clang-tidy) runs automatically in VSCode. Look for:
- Yellow squiggly lines (warnings)
- Red squiggly lines (errors)
- Hover for details

### Fix Auto-Fixable Issues
Click lightbulb icon next to warning.

## Hot-Reloading Assets

1. Modify SVG file in `apps/world-sim/assets/`
2. Game detects change and reloads
3. No restart needed (once implemented)

## Adding a Log Category

1. Add to enum in `libs/foundation/utils/log.h`:
   ```cpp
   enum class LogCategory {
       MySystem,  // Add this
       // ...
   };
   ```

2. Add to string conversion:
   ```cpp
   const char* CategoryToString(LogCategory cat) {
       case LogCategory::MySystem: return "MySystem";
       // ...
   }
   ```

3. Use in code:
   ```cpp
   LOG_INFO(MySystem, "Something happened");
   ```

## Profiling Performance

1. Add profiling scopes:
   ```cpp
   void ExpensiveFunction() {
       PROFILE_SCOPE("ExpensiveFunction");
       // ... work ...
   }
   ```

2. Run game with profiler enabled
3. Analyze results

(Note: Profiling system to be implemented)

## Updating Documentation

### After Making Technical Decision
1. Check if decision affects existing tech doc
2. If yes, update tech doc
3. If new system, create new tech doc in `/docs/technical/`
4. Add to `/docs/technical/INDEX.md`
5. Update `/docs/status.md` with decision

### After Implementing Feature
1. Mark implementation status in tech doc as complete
2. Add code references (file:line) to tech doc
3. Update `/docs/status.md` progress

### When Changing Architecture
1. Update affected tech docs
2. Update `CLAUDE.md` if high-level structure changed
3. Update `/docs/status.md` with rationale
