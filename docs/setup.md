# Development Environment Setup

Cross-platform setup reference for World-Sim (macOS, Linux, Windows). For a quick build
recipe see the main [README](../README.md); this doc covers the details and the pitfalls.

## Prerequisites

- **C++20 compiler**: Clang (macOS/Linux), or MSVC via Visual Studio 2022 / Build Tools (Windows)
- **CMake 3.25+** (`CMAKE_MSVC_DEBUG_INFORMATION_FORMAT` / CMP0141)
- **Ninja + ccache**: the presets use Ninja generators with a ccache compiler launcher.
  macOS/Linux: `brew install ccache ninja` (or apt equivalents). Windows:
  `winget install Ccache.Ccache`, then run `./scripts/setup-msvc-env.ps1` once to persist
  the MSVC toolchain paths (cl/rc/VS-bundled Ninja), INCLUDE/LIB, and VCPKG_ROOT to the
  User environment. ccache config is in the README's setup section.
- **vcpkg** for dependencies
- **Node.js / npm** (optional, only for the developer-client web app in Debug builds)

## vcpkg

Dependencies are declared in `vcpkg.json` and built in manifest mode. The exact versions are
pinned by `builtin-baseline` (a vcpkg commit SHA) plus a few `overrides`.

```bash
git clone https://github.com/Microsoft/vcpkg.git
cd vcpkg
./bootstrap-vcpkg.sh        # bootstrap-vcpkg.bat on Windows
export VCPKG_ROOT=/path/to/vcpkg   # PowerShell: $env:VCPKG_ROOT = "C:\vcpkg"
```

Set `VCPKG_ROOT` persistently (shell profile, or Windows user environment variables). The
`.vscode` and CI configs both rely on it.

> **The baseline trap.** vcpkg resolves the pinned versions from the version database in your
> local vcpkg checkout. If that checkout is older than `builtin-baseline`, configure fails with
> `no version database entry for <pkg> at <version>`. The fix is to advance vcpkg to at least the
> baseline commit:
>
> ```bash
> git -C $VCPKG_ROOT fetch
> git -C $VCPKG_ROOT checkout <builtin-baseline-from-vcpkg.json>
> $VCPKG_ROOT/bootstrap-vcpkg.sh   # or .bat
> ```
>
> Pulling the latest `master` also works (the version database only grows). CI pins its own
> vcpkg commit via `lukka/run-vcpkg`; keep it at least as new as `builtin-baseline`.

## Configure and build

The presets carry the generator (Ninja / Ninja Multi-Config), the vcpkg toolchain (via
`VCPKG_ROOT`), and the ccache launcher — always configure through them.

**macOS / Linux** (Ninja, single-config):

```bash
cmake --preset default
cmake --build build
./build/apps/world-sim/world-sim
```

**Windows** (Ninja Multi-Config + MSVC), from any shell after the one-time setup script:

```powershell
cmake --preset windows
cmake --build build --config Debug
./build/apps/world-sim/Debug/world-sim.exe
```

### Binary locations differ by generator

The Windows multi-config generator puts binaries in a per-config subdirectory
(`build/apps/world-sim/Debug/`, `.../RelWithDebInfo/`); the single-config macOS/Linux build puts
them directly in the target directory (`build/apps/world-sim/`). CMake copies runtime assets next
to the executable via `$<TARGET_FILE_DIR:...>`, so both layouts work; just mind the path when
launching by hand.

### Switching from a pre-Ninja checkout

Build dirs configured with the old Visual Studio generator error on reconfigure after the preset
change: delete `build/` once and re-run `cmake --preset windows`. With a warm ccache the rebuild
is fast (~30 s on the reference machine). Check hit rates anytime with `ccache -s`.

### Font atlas

The build generates the SDF font atlas (`fonts/Roboto-SDF.png`) from `Roboto-Regular.ttf` via the
`generate_sdf_atlas` tool whenever it is missing or the font changes. The PNG is a gitignored build
artifact, so a fresh checkout produces it automatically with no manual step.

## Tests

```bash
# Fast suite, parallel across the per-lib test exes (-C Debug only on Windows multi-config)
ctest --test-dir build -LE heavy -E benchmarks -j 13 --output-on-failure           # macOS/Linux
ctest --test-dir build -C Debug -LE heavy -E benchmarks -j 13 --output-on-failure  # Windows
```

The heavy worldgen bucket (`-L heavy`) is slow by design; CI runs it only when worldgen
paths change. See `docs/workflows.md` for the details.

## Cross-platform notes

The codebase is authored on macOS/Clang. A few things keep it building under MSVC, which is
stricter:

- **Designated initializers** must be written in struct declaration order (C++20). Clang only
  warns; MSVC errors (C7560). The build sets `-Werror=reorder-init-list` on Clang/GCC so violations
  fail fast on macOS/CI instead of surfacing later on Windows.
- **Source encoding is UTF-8.** MSVC builds with `/utf-8` so Unicode literals compile.
- **No POSIX-only APIs in portable code.** Use `<filesystem>` (e.g. `temp_directory_path()`) and
  `<numbers>` rather than `mkstemp`, hardcoded `/tmp`, or `M_PI`. Platform-specific code (e.g.
  `metrics/SystemResources.cpp`) is guarded by `#ifdef`.

## IDE

VS Code with CMake Tools, the C/C++ extension, and clangd. CMake Tools reads `VCPKG_ROOT` from the
environment via `.vscode/settings.json`. Format manually (`Shift+Alt+F`); format-on-save is off.
