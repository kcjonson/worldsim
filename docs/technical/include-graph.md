# Include graph

`tools/resolve-includes.mjs` resolves every `#include` under `libs/` and prints a per-library matrix of include counts. It exists to measure layering drift without building anything.

## What it measures

Each include is resolved against the including file's directory (quoted includes only) and then every library's include root, which approximates the build's lookup with every library's root visible to every file. The roots are hardcoded and match CMake as of 2026-10-25; `libs/` itself is a root too, because planet-view exports it. foundation and renderer are not in this repo (they come from [salal](https://github.com/kcjonson/salal) through FetchContent), so their two roots, and their two rows, are read from a salal checkout given with `--salal`; their paths print with a `salal/` prefix. A directory under `libs/` the script doesn't know makes it exit 2, so a new library can't be silently left out, and so does a missing salal checkout or a stray `libs/foundation` or `libs/renderer`. The first hit decides the destination library; an include that resolves nowhere counts as external (system and vcpkg headers). `engine/assets/` is tallied as its own row, `engine(assets)`. game-systems has no row because it has no includes (one placeholder source).

Cells are counts of `#include` lines, with self-includes on the diagonal. The script prints an all-files matrix and a production-only matrix; a file counts as test or bench when its name contains `.test.` or `.bench.` or ends in `TestWorld.h` or `TestSupport.h` (engine's two test-support headers). After the matrices it lists ambiguous resolutions (a header name present in more than one library), includes that cross the layering, the engine -> ui coupling surface, and what engine's generic slice (`application/`, `scene/`, `input/`, `clipboard/`, the part the [salal](https://github.com/kcjonson/salal) extraction carves out as its app library) includes from `libs/`.

The layering it reports against follows the per-library "Depends on" lists in [monorepo-structure.md](./monorepo-structure.md): foundation at the bottom; geometry and renderer above it; ui and world above renderer; planet-view and game-systems above world; engine on top, allowed to include anything. planet-view's row is inferred, since that doc predates it. Violations are reported, not enforced: the script exits 0 either way.

## Running it

```
node tools/resolve-includes.mjs
node tools/resolve-includes.mjs --salal path/to/salal
node tools/resolve-includes.mjs --root path/to/another/checkout
```

`--root` defaults to the repo the script lives in. `--salal` defaults to `build/_deps/salal-src`, which a configure against the pinned tag populates; a build configured with `-DFETCHCONTENT_SOURCE_DIR_SALAL` has nothing there, so pass that checkout. Output order is fixed (directories walked by name, rows sorted by file and line), so two runs on the same tree diff clean.

## Baseline

Production files only (test and bench files excluded), main @ c3a22afa plus the groundcover deform as an engine hook on salal's base shader (2026-10-26), with foundation and renderer read from salal v0.6.0. Against the previous baseline (main @ b08d0468 plus the call sites that hand `Primitives::init` a `GLBackend`, read from v0.5.0) the renderer row grew from 93 and 168 to 95 and 196, nearly all of it the base shader's build-time assembler (`assembler/BaseShaderAssembler.{h,cpp}` and `Main.cpp`, a host tool: two includes of its own header and 23 standard headers), the rest five standard headers in the backend and `Primitives.h`. The engine row rose by one to foundation, renderer and engine and by six to external with `GroundcoverUniforms.h` (`math/Types.h`, `primitives/Primitives.h`, the generated `BaseShader.h` and four standard headers, included by `GroundcoverRenderer.cpp`) and `AppLauncher.cpp`'s `BaseShader.h`; the generated header sits in the build tree, under no library root, so it counts as external. Rows are the including library, columns the library the include lands in.

| src \ dst | foundation | geometry | renderer | ui | world | planet-view | game-systems | engine | engine(assets) | external |
|---|---|---|---|---|---|---|---|---|---|---|
| foundation | 12 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 70 |
| geometry | 2 | 86 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 158 |
| renderer | 34 | 0 | 95 | 0 | 0 | 0 | 0 | 0 | 0 | 196 |
| ui | 97 | 0 | 42 | 157 | 0 | 0 | 0 | 0 | 0 | 116 |
| world | 34 | 0 | 0 | 0 | 118 | 0 | 0 | 0 | 0 | 149 |
| planet-view | 2 | 0 | 1 | 0 | 6 | 11 | 0 | 0 | 0 | 38 |
| engine | 49 | 81 | 26 | 5 | 9 | 0 | 0 | 463 | 68 | 524 |
| engine(assets) | 23 | 3 | 9 | 0 | 0 | 0 | 0 | 13 | 54 | 157 |

Nothing is ambiguous. Foundation includes nothing internal outside itself. There are no layering violations: ui -> engine and renderer -> ui are both 0. engine -> ui (5 lines) is allowed.

### Violations

None. The ui -> engine list is empty: `input/InputTypes.h` now lives at `libs/foundation/input/InputTypes.h` (SAL-26, which also dropped engine from ui's link list), and the `input/InputManager.h` and `clipboard/ClipboardManager.h` includes went earlier in phase 0.5 of the [salal](https://github.com/kcjonson/salal) extraction, as did renderer's one `font/FontRenderer.h` include of ui.

Before phase 0.5 (main @ db335a2b, 2026-10-25) the matrix read ui -> engine 10 and renderer -> ui 1: `input/InputTypes.h` (7 lines), `input/InputManager.h` (2), `clipboard/ClipboardManager.h` (1), and `font/FontRenderer.h` (1).
