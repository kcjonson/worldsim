# World-Sim

A C++20 game project featuring 3D procedural world generation and 2D tile-based gameplay.

## Features

- 3D spherical world generation
- 2D top-down tile-based gameplay sampled from the 3D world
- Infinite pannable world via chunk-based loading
- **Vector-based assets (SVG)** with procedural variation and seamless blending
- Testable UI framework with inspection capabilities
- Custom Entity Component System (ECS)

## Prerequisites

- **C++20 compiler** (Clang, GCC, or MSVC)
- **CMake 3.20+**
- **vcpkg** (for dependency management)
- **VSCode** (recommended) with extensions:
  - C/C++ (ms-vscode.cpptools)
  - CMake Tools (ms-vscode.cmake-tools)
  - clangd (llvm-vs-code-extensions.vscode-clangd)

## Setup

### 1. Install vcpkg

```bash
git clone https://github.com/Microsoft/vcpkg.git
cd vcpkg
./bootstrap-vcpkg.sh  # or bootstrap-vcpkg.bat on Windows
export VCPKG_ROOT=/path/to/vcpkg  # Add to your shell profile
```

### 2. Configure and Build

```bash
# Configure
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake

# Build
cmake --build build

# Run
./build/apps/world-sim/world-sim
```

### 3. VSCode Setup

Open the project in VSCode. CMake should configure automatically.

**Build**: `Cmd+Shift+B` (or `Ctrl+Shift+B`)
**Format code**: `Shift+Alt+F` (manual - not on save)
**Debug**: Use the debug configurations in Run & Debug panel

## Project Structure

```
world-sim/
├── libs/              # Modular libraries (dependency layers)
│   ├── foundation/    # Math, platform, utilities (base)
│   ├── renderer/      # OpenGL rendering (2D/3D) + SVG loading
│   ├── ui/            # UI framework with inspector/testability
│   ├── world/         # World generation (pluggable generators)
│   ├── game-systems/  # Chunks, tiles, camera (game-specific)
│   └── engine/        # App lifecycle, scenes, ECS, config
├── apps/
│   ├── world-sim/     # Main game application
│   │   ├── scenes/   # Splash, menu, world creator, game
│   │   └── assets/   # SVG files, configs
│   └── ui-sandbox/    # UI testing/demo app
│       └── demos/    # Component demos
└── docs/              # Documentation
    ├── status.md      # Current development status
    ├── workflows.md   # Common development tasks
    ├── technical/     # Technical design docs
    └── specs/         # Product specifications
```

**Dependency Rules:**
- Each layer depends only on layers below it
- No circular dependencies
- `engine` → `game-systems/ui/world` → `renderer` → `foundation`

For full details, see [docs/technical/monorepo-structure.md](docs/technical/monorepo-structure.md).

## Documentation

- **[CLAUDE.md](CLAUDE.md)** - AI assistant guidance (start here for AI agents)
- **[Development Status](docs/status.md)** - Current tasks and progress
- **[Workflows](docs/workflows.md)** - Common development tasks and how-tos
- **[Technical Docs](docs/technical/INDEX.md)** - Architecture and design decisions
- **[C++ Coding Standards](docs/technical/cpp-coding-standards.md)** - Style guide and best practices
- **[Game Design Documents](docs/design/INDEX.md)** - Feature requirements and game design

## Development Workflow

1. Check **[docs/status.md](docs/status.md)** for current work
2. Read relevant technical documentation
3. Write code following **[coding standards](docs/technical/cpp-coding-standards.md)**
4. Format manually with `Shift+Alt+F`
5. Fix clang-tidy warnings
6. Test in `ui-sandbox` before integrating into main game
7. Update `docs/status.md` after significant work

For detailed workflows (adding libraries, creating scenes, etc.), see **[docs/workflows.md](docs/workflows.md)**.

## Testing

```bash
# Run all tests
ctest --test-dir build --output-on-failure

# Run UI sandbox
./build/apps/ui-sandbox/ui-sandbox --help
./build/apps/ui-sandbox/ui-sandbox --component button --http-port 8080
```

## Code Quality

- **clang-format**: Code formatting (`.clang-format`)
- **clang-tidy**: Static analysis and linting (`.clang-tidy`)
- Run formatter manually to learn the style
- Address warnings as you code

## License

TBD
