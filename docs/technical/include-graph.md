# Include graph

`tools/resolve_includes.py` resolves every `#include` under `libs/` and prints a per-library matrix of include counts. It exists to measure layering drift without building anything.

## What it measures

Each include is resolved against the including file's directory (quoted includes only) and then every library's include root, the way the build finds it. `libs/` itself is a root too, because planet-view exports it. The first hit decides the destination library; an include that resolves nowhere counts as external (system and vcpkg headers). `engine/assets/` is tallied as its own row, `engine(assets)`.

Cells are counts of `#include` lines, with self-includes on the diagonal. Production files and test/bench files (`*.test.cpp`, `*.bench.cpp`) get separate matrices. The script also lists ambiguous resolutions (a header name present in more than one library), includes that break the layering, and the engine -> ui coupling surface.

The layering it enforces is foundation at the bottom; geometry and renderer above it; ui and world above renderer; planet-view and game-systems above world; engine on top, allowed to include anything.

## Running it

```
python -I tools/resolve_includes.py
python -I tools/resolve_includes.py --root path/to/another/checkout
```

`--root` defaults to the repo the script lives in.

## Baseline

Production files only, main @ db335a2b (2026-10-25). Rows are the including library, columns the library the include lands in.

| src \ dst | foundation | geometry | renderer | ui | world | planet-view | game-systems | engine | engine(assets) | external |
|---|---|---|---|---|---|---|---|---|---|---|
| foundation | 20 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 106 |
| geometry | 2 | 86 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 158 |
| renderer | 27 | 0 | 58 | 1 | 0 | 0 | 0 | 0 | 0 | 120 |
| ui | 92 | 0 | 34 | 168 | 0 | 0 | 0 | 10 | 0 | 127 |
| world | 34 | 0 | 0 | 0 | 118 | 0 | 0 | 0 | 0 | 149 |
| planet-view | 2 | 0 | 1 | 0 | 6 | 11 | 0 | 0 | 0 | 38 |
| engine | 48 | 86 | 25 | 5 | 13 | 0 | 0 | 466 | 70 | 505 |
| engine(assets) | 22 | 3 | 9 | 0 | 0 | 0 | 0 | 12 | 51 | 156 |

Nothing is ambiguous. Foundation includes nothing internal outside itself. The only two layering violations are ui -> engine (10 lines) and renderer -> ui (1 line); engine -> ui (5 lines) is allowed.

### ui -> engine

- `input/InputTypes.h`: `libs/ui/input/InputEvent.h:11`, `libs/ui/focus/Focusable.h:4`, `libs/ui/focus/FocusManager.h:4`, `libs/ui/components/contextmenu/ContextMenu.h:23`, `libs/ui/components/list/ListRow.h:18`, `libs/ui/components/button/Button.cpp:11`, `libs/ui/components/tabbar/TabBar.cpp:11`
- `input/InputManager.h`: `libs/ui/components/button/Button.cpp:10`, `libs/ui/components/TextInput/TextInput.cpp:10`
- `clipboard/ClipboardManager.h`: `libs/ui/components/TextInput/TextInput.cpp:9`

### renderer -> ui

- `font/FontRenderer.h`: `libs/renderer/primitives/Primitives.cpp:12`

These 11 lines are being removed in phase 0.5 of the salal extraction.
