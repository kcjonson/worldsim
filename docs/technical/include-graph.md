# Include graph

`tools/resolve-includes.mjs` resolves every `#include` under `libs/` and prints a per-library matrix of include counts. It exists to measure layering drift without building anything.

## What it measures

Each include is resolved against the including file's directory (quoted includes only) and then every library's include root, which approximates the build's lookup with every library's root visible to every file. The roots are hardcoded and match CMake as of 2026-10-25; `libs/` itself is a root too, because planet-view exports it. A directory under `libs/` the script doesn't know makes it exit 2, so a new library can't be silently left out. The first hit decides the destination library; an include that resolves nowhere counts as external (system and vcpkg headers). `engine/assets/` is tallied as its own row, `engine(assets)`. game-systems has no row because it has no includes (one placeholder source).

Cells are counts of `#include` lines, with self-includes on the diagonal. The script prints an all-files matrix and a production-only matrix; a file counts as test or bench when its name contains `.test.` or `.bench.` or ends in `TestWorld.h` or `TestSupport.h` (engine's two test-support headers). After the matrices it lists ambiguous resolutions (a header name present in more than one library), includes that cross the layering, the engine -> ui coupling surface, and what engine's generic slice (`application/`, `scene/`, `input/`, `clipboard/`, the part the [salal](https://github.com/kcjonson/salal) extraction carves out as its app library) includes from `libs/`.

The layering it reports against follows the per-library "Depends on" lists in [monorepo-structure.md](./monorepo-structure.md): foundation at the bottom; geometry and renderer above it; ui and world above renderer; planet-view and game-systems above world; engine on top, allowed to include anything. planet-view's row is inferred, since that doc predates it. Violations are reported, not enforced: the script exits 0 either way.

## Running it

```
node tools/resolve-includes.mjs
node tools/resolve-includes.mjs --root path/to/another/checkout
```

`--root` defaults to the repo the script lives in. Output order is fixed (directories walked by name, rows sorted by file and line), so two runs on the same tree diff clean.

## Baseline

Production files only (test and bench files excluded), main @ db335a2b (2026-10-25). Rows are the including library, columns the library the include lands in.

| src \ dst | foundation | geometry | renderer | ui | world | planet-view | game-systems | engine | engine(assets) | external |
|---|---|---|---|---|---|---|---|---|---|---|
| foundation | 20 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 106 |
| geometry | 2 | 86 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 158 |
| renderer | 27 | 0 | 58 | 1 | 0 | 0 | 0 | 0 | 0 | 120 |
| ui | 92 | 0 | 34 | 168 | 0 | 0 | 0 | 10 | 0 | 127 |
| world | 34 | 0 | 0 | 0 | 118 | 0 | 0 | 0 | 0 | 149 |
| planet-view | 2 | 0 | 1 | 0 | 6 | 11 | 0 | 0 | 0 | 38 |
| engine | 48 | 81 | 25 | 5 | 9 | 0 | 0 | 459 | 70 | 489 |
| engine(assets) | 22 | 3 | 9 | 0 | 0 | 0 | 0 | 12 | 51 | 156 |

Nothing is ambiguous. Foundation includes nothing internal outside itself. The only two layering violations are ui -> engine (10 lines) and renderer -> ui (1 line); engine -> ui (5 lines) is allowed.

### ui -> engine

- `input/InputTypes.h`: `libs/ui/input/InputEvent.h:11`, `libs/ui/focus/Focusable.h:4`, `libs/ui/focus/FocusManager.h:4`, `libs/ui/components/contextmenu/ContextMenu.h:23`, `libs/ui/components/list/ListRow.h:18`, `libs/ui/components/button/Button.cpp:11`, `libs/ui/components/tabbar/TabBar.cpp:11`
- `input/InputManager.h`: `libs/ui/components/button/Button.cpp:10`, `libs/ui/components/TextInput/TextInput.cpp:10`
- `clipboard/ClipboardManager.h`: `libs/ui/components/TextInput/TextInput.cpp:9`

### renderer -> ui

- `font/FontRenderer.h`: `libs/renderer/primitives/Primitives.cpp:12`

These 11 lines are being removed in phase 0.5 of the [salal](https://github.com/kcjonson/salal) extraction.
