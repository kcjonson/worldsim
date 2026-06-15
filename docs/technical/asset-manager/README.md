# Asset Manager — technical spec

**Type:** Implementation spec
**Status:** Draft
**Created:** 2026-06-15
**Product spec:** [../../design/features/asset-manager/README.md](../../design/features/asset-manager/README.md)

## Summary

This is the implementation plan for the Asset Manager described in the [product spec](../../design/features/asset-manager/README.md). It covers the shared render path, asset enumeration and inspection, load-time validation, the GUI app, the headless CLI, and a startup refactor that loads assets asynchronously so the splash can show real progress and a validation summary.

Read the product spec first. This document does not restate the "why"; it specifies the "how" and is grounded in how the code actually works today, not how the asset docs describe it. Where the two disagree, the code wins (see the asset-system docs' known drift around `<variation>`, inheritance, and component references).

## Decisions already made

Four architectural questions were settled before writing this; they shape everything below.

- **Packaging.** The shared core is the existing `assets` library, extended with a render contract and a validator. Two thin frontends sit on top: a GUI app (`apps/asset-manager`) and a separate, server-less CLI binary (`apps/asset-cli`). The CLI binds no fixed port, so many headless invocations run in parallel, which is what an agent loop and a CI matrix need.
- **Headless GL.** v1 uses a hidden GLFW window for the offscreen context, the same context the game creates, just never shown. No EGL or software-GL fallback in v1. CI runs the render checks where a GPU is available and skips them gracefully where there's no display, mirroring the existing `RenderToTexture` test.
- **Thumbnails.** The gallery and previews draw live tessellated geometry through the game's own render pipeline (the same `BatchRenderer` path the game uses), virtualized to the visible window. No texture cache, no second rasterizer. Faithful by construction.
- **Validation runs at load.** Validation lives in the shared loader and runs as part of loading, so the game and the tool share one rule set and one report. The game surfaces that report at launch (see the async-splash section), and the tool consumes the same report.

## Architecture

One shared core, two frontends, plus a startup change in the engine.

```
libs/engine/assets            (shared core, extended)
  - loader + registry            existing: getDefinition(s), getTemplate, generateAsset, groups
  - AssetValidator + report      new: load-time validation, single source of truth
  - AssetRenderer                new: defName(+seed) -> RGBA pixels via offscreen FBO
libs/renderer                 (extended)
  - mesh bounds + fit-to-rect    new: geometry math on TessellatedMesh
libs/ui                       (extended)
  - GridContainer                new: wrapping, virtualized grid
apps/asset-manager            (new, GUI)   links: engine ui renderer foundation assets planet-view world
apps/asset-cli                (new, CLI)   links: assets renderer foundation glfw glew opengl nlohmann_json
libs/engine/application       (refactor)
  - async asset load + splash progress + load report
```

The render path is shared at the lowest sensible layer. Both frontends turn an asset into draw-ready geometry with the same call and submit it through the same `BatchRenderer` pipeline the game uses. The GUI submits to the screen; the CLI submits to an offscreen framebuffer and reads the pixels back. Same geometry, same shader, same pipeline, so the same pixels. That shared submission is the fidelity contract; it is not re-implemented anywhere.

### Why the core is the `assets` lib, not a new lib

`assets` already depends on `renderer` (it produces `renderer::TessellatedMesh` via the tessellator) and already owns everything about turning a definition into geometry (`getTemplate`, `generateAsset`, `tessellateAsset`). The render contract and the validator are the natural next members of that library. A separate lib would only add a layer to thread the same dependencies through. `renderer` cannot host the render contract (it would have to depend on `assets`, inverting the existing direction).

## The shared render path

Today there is no "render this asset" function. Scenes assemble the pieces by hand: `GrassScene` calls `getTemplate`, builds per-instance transforms, batches with `AssetBatcher`, and submits with `Primitives::drawTriangles`; `SvgScene` loads an SVG, tessellates, then scales and centers into screen pixels by hand. We replace that hand-assembly with two shared pieces.

### Geometry prep (CPU, no GL)

A function in `assets` that takes a def name, an optional seed, and a target rectangle, and returns draw-ready geometry fit to that rectangle:

```cpp
// libs/engine/assets/AssetRenderer.h
struct PreparedAsset {
    renderer::TessellatedMesh mesh;   // vertices in target-rect pixel space
    bool hasOutput = true;            // false if the generator produced no paths
};

// seed is ignored for Simple assets; for Procedural it selects the form.
PreparedAsset prepareAsset(const std::string& defName, const Rect& target, uint32_t seed);
std::vector<PreparedAsset> prepareSamples(const std::string& defName, const Rect& target, int count, uint32_t baseSeed);
```

Internally:

- **Simple asset:** reuse the existing cached `getTemplate(defName)`. It already loads the SVG and normalizes to `worldHeight`. The seed is irrelevant; a simple asset is one drawing.
- **Procedural asset:** do not use `getTemplate`. It is hard-coded to seed 42 and memoized, so it yields exactly one form. Call `generateAsset(defName, seed, out)` directly (this is what `TreeScene` does to make 40 distinct trees), then tessellate with `tessellateAsset`. Vary the seed to sample the generator's range; fix the seed for a reproducible render. Determinism is real: `generateAsset` reseeds Lua's RNG with the seed on every call, so the same seed gives byte-identical paths.
- **Fit to frame:** procedural output is in unnormalized generator space and even the normalized templates are centered meters, with no fit helper anywhere today. Add `computeBounds(const TessellatedMesh&) -> Rect` and `fitToRect(TessellatedMesh&, srcBounds, dstRect)` to `renderer` (geometry math on a renderer type, no asset dependency). Geometry prep computes bounds, fits to the target rect in pixel space, and returns the transformed mesh.

`prepareSamples` is the procedural detail view (around ten forms) and the agent's batch render. Refresh in the GUI re-samples with new base seeds; the CLI's reproducible mode pins the base seed.

Empty output is not an error in the generator (a script that adds no paths logs a warning and still succeeds). Geometry prep detects `paths.empty()` and sets `hasOutput = false` so callers can show an explicit empty state instead of a blank frame.

### Render to image (GL, CLI side)

`AssetRenderer` wraps geometry prep, submits to an offscreen framebuffer, reads the pixels back, and optionally encodes a PNG:

```cpp
// returns RGBA8, row-major, top-left origin (already flipped from GL's bottom-left)
std::vector<uint8_t> renderToPixels(const std::string& defName, int w, int h,
                                    Foundation::Color background, uint32_t seed);
bool renderToPng(const std::string& defName, const std::string& outPath, int w, int h,
                 Foundation::Color background, uint32_t seed);
```

The pieces all exist and this is assembly:

- **Offscreen target:** `renderer::RenderToTexture` (an RGBA8 texture + framebuffer with `begin()`/`end()`). It exists and is tested but is wired into nothing in production; the Asset Manager is its first real consumer. It has no readback or PNG helper, so we add those.
- **Submission:** the same `BatchRenderer` path the game and the GUI use, pointed at the bound FBO.
- **Projection caveat (must handle):** `BatchRenderer::flush()` builds its projection from either a `CoordinateSystem` (window and DPI based) or fallback `viewportWidth/viewportHeight` members (BatchRenderer.cpp:384-389). Window-based projection is wrong for an off-size FBO. The headless path must drive the projection to the FBO dimensions: either set the batcher's viewport members to `w,h` with no coordinate system, or configure a coordinate system describing the FBO. Geometry prep already places vertices in target-rect pixel space, so the projection is a plain `ortho(0, w, h, 0)`.
- **Readback + encode:** `glReadPixels`/`glGetTexImage`, vertical flip (GL origin is bottom-left, PNG is top-left), then `stb_image_write`. The flip-and-encode logic already exists in `DebugServer::captureScreenshotIfRequested` and should be factored into a small shared helper rather than copied.

### Headless context and minimal init

The CLI creates a hidden window (`glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE)`) plus GLEW, exactly as the `RenderToTexture` test does, then initializes only what the render path needs: the renderer's shader/primitive setup and the asset registry (including `setSharedScriptsPath`, or every `@shared/` Lua def fails to resolve). It does not create scenes, a debug server, or fonts.

`AppLauncher::initializeRenderingSystems` and `initializeAssetSystem` currently bundle this init with a full app. Factor the rendering-primitive init and the asset-system init into reusable functions the CLI can call without `AppLauncher`. This is a refactor of existing code, not new behavior.

Because the CLI is a short-lived, server-less process with its own context, N invocations run in parallel up to GPU and CPU limits. The one cross-process constraint to respect: do not bind the fixed debug port from the CLI.

## Asset enumeration and inspection

The registry already exposes the browse and inspect surface. The tool walks `getDefinitionNames()` (sort it; the backing store is unordered) and calls `getDefinition(name)` for the full parsed `AssetDefinition`. Resource paths come off the def: `svgPath`, `scriptPath`, `generatorName`, resolved with `def->resolvePath`, plus the manual `@shared/` handling that `generateAsset` does (the tool must replicate that prefix logic to show the real Lua path). Groups come from `getGroups()` / `getGroupMembers()`.

The inspector model is the parsed def, the resolved resource paths, the asset's groups and placement rules, the raw file text shown beside the parsed view, and the warnings for that def from the validation report (see below). Documented-but-ignored fields are shown as warnings, not silently dropped.

Type discrimination at the data level: `assetType == Simple` is an SVG drawing; anything else is procedural, and within procedural `isLuaGenerator()` (`!scriptPath.empty()`) separates a Lua script from a registered C++ generator.

Coverage in v1 is the folder-per-def XML assets the registry loads. Tiles (`tiles/surfaces/*/pattern.svg`, no XML) and UI icons (`ui/icons/*.svg`) live outside the registry on different paths; the product spec excludes them from v1, and the orphan check below is scoped accordingly.

## Validation

Validation is part of loading. The loader is hardened to record, while it parses, the things it currently swallows, into a structured report that becomes the single source of truth for the game's launch summary, the GUI inspector, and the CLI `validate` command.

### Why the loader, not a separate parser

Most of the interesting checks concern what the loader hides. Today it silently coerces bad enums and numbers to defaults, silently overwrites duplicate `defName`s (last wins), and silently skips any XML whose filename does not match its folder (a `LOG_DEBUG`, easy to miss). A standalone validator would have to re-parse every file and stay in lockstep with the real parser forever. Recording warnings inside the one real parse keeps a single source of truth and matches the product spec's intent to harden the parser as the canonical schema.

### Report shape

Mirror the existing `ConfigValidator` conventions (`ValidationError{source, message, context}` plus a static accumulator), but this is new code; `ConfigValidator` validates work and construction configs, not visual asset defs, so there is nothing to extend directly.

```cpp
// libs/engine/assets/AssetValidator.h
enum class Severity { Warning, Error };
struct ValidationIssue {
    Severity severity;
    std::string defName;     // empty for file/folder-level issues
    std::string field;       // empty if not field-specific
    std::string message;
    std::string context;     // e.g. the raw value that was coerced
};
struct ValidationReport {
    std::vector<ValidationIssue> issues;
    int errorCount() const;
    int warningCount() const;
};
```

The loader populates the report during `loadDefinitionsFromFolder` and the per-def parse. The report is owned where the load is driven so any scene or the CLI can read it.

### v1 rule set

Static checks, recorded by the loader at parse time:

- Required fields present and schema-conformant. `defName` non-empty (already warns). `assetType` is a known value; today an unknown or misspelled type silently becomes `Procedural`, so the validator flags unknown type strings rather than letting the coercion hide them.
- References resolve. Every `svgPath` and every generator `scriptPath` (including `@shared/` resolution) points at a file that exists.
- No orphans. SVG files in an asset folder that no def references. The loader already walks the folder tree, so it can collect these during the same scan. Scoped to registry-loaded folders in v1 (tiles and UI icons are out, and when they come in this check must also scan code references).
- Primary-name integrity. Surface the filename/folder mismatches the loader silently skips, as errors. A `Foo/Bar.xml` that never loads is almost always a mistake.
- No duplicate `defName`s. The loader overwrites silently; record collisions instead.
- Documented-but-ignored fields flagged as warnings: `<variation>`, `ParentDef`/inheritance, component/variant SVG references, and the real `variantCount`-nested-in-`<rendering>` drift (the parser reads `variantCount` only as a top-level child, so the nested value in GrassBlade is silently dropped). Silent coercions (bad enum to default, bad number to default) are warnings too, with the raw value in `context`.

A dynamic check run by the tool and CI, not by the loader (it needs GL, and a per-launch render of the whole library would be too slow):

- Render smoke test. Every asset renders without error, and every procedural generator produces a form without error. This is the payoff of the render API: "does the whole library still draw" becomes one command. It runs in `asset-cli validate --render-smoke` and in CI.

CLI `validate` exits non-zero on any error so CI can gate on it; warnings do not fail the build.

## Async loading and the splash refactor

The product spec wants validation surfaced at launch. Today that is impossible without a startup change: the splash is a fixed 1.5-second timer (`SplashScene.cpp:94`), and asset loading runs fully synchronously before the first frame is ever drawn (`AppLauncher::initialize` calls `initializeAssetSystem` before the main loop). By the time the splash appears, loading is already done, and nothing surfaces load results to the player; the loader logs and continues.

The chosen approach is to make loading asynchronous so the splash can show real progress and a summary.

### New startup order

Move the asset load out of the pre-loop section. Create the window and rendering systems, construct `Application`, register scenes, switch to the splash, and start the main loop with no assets loaded yet. The splash's `onEnter` kicks off a worker thread that runs the load and validation, publishing into a shared progress structure:

```cpp
struct LoadProgress {
    std::atomic<int> defsLoaded{0};
    std::atomic<int> defsTotal{0};      // 0 until the folder scan counts files
    std::atomic<bool> done{false};
    std::mutex reportMutex;
    ValidationReport report;            // valid once done == true
};
```

The splash polls `LoadProgress` each frame and shows a real progress indicator. The transition is driven by `done`, not a timer: on completion with no errors it switches to the menu; on errors it switches to a load-report scene that lists them and blocks (rather than cramming errors onto the splash or proceeding into a broken game).

### Thread-safety boundary

Asset loading is XML parsing plus lazy geometry; tessellation is CPU work and needs no GL, so the whole load runs safely off the main thread. Two rules make the concurrency safe:

- No scene that reads the registry may be entered before `done` is true. Only the splash is active during the load, and the splash reads `LoadProgress`, never the registry. Definitions-map writes on the worker thread are therefore never racing a reader.
- Any GPU upload stays on the main thread. Parsing and tessellation can happen on the worker; instanced-mesh uploads to the GPU happen later, on the main thread, when a scene first draws. The existing `templateCacheMutex` already guards the template cache for concurrent bakers and is unchanged.

This refactor is its own workstream layered under the tool work, and it touches engine startup, so it lands behind its own tests before the GUI depends on it.

## GUI (`apps/asset-manager`)

A standalone app, sibling to `ui-sandbox`, reusing the scene system, the `SceneManager`, and the X-macro scene-registration pattern verbatim. Bootstrap is an `AppConfig` with the two scene callbacks, the same as the other apps.

### Layout

One primary browser scene composed from existing widgets plus one new layout component:

- A top bar: a `TextInput` search box and filter controls (`Select`/`DropdownButton`) for type (simple vs procedural), group, and validation state (for example, "has warnings").
- A gallery: a new `GridContainer` of thumbnails inside a `ScrollContainer`. Each cell is a faithful thumbnail, the def name, and the type. A procedural asset's gallery thumbnail is one stable representative form (a fixed seed) so the wall does not reshuffle on every visit.
- A detail pane: the faithful preview and the inspector. For a procedural asset the preview shows around ten sampled forms with a refresh that re-samples; for a simple asset, one drawing. The inspector shows the parsed def beside the raw file, the resolved resource paths, groups and placement, and this def's validation warnings.
- A validation view: the whole-library report, filterable, sharing the exact report the CLI prints.
- Reload: a refresh action that re-runs the load from disk (clear the registry, reload, invalidate the template cache) so a designer who just edited a def in their own editor sees the change without restarting. This is distinct from the preview's re-sample refresh.

### The new grid component

The UI toolkit has only single-axis stacking (`LayoutContainer` is vertical or horizontal; there is no wrap) and `ScrollContainer` scrolls only the Y axis. A thumbnail wall needs a wrapping grid. Add `GridContainer : Container` that takes a target cell size or a column count and positions children on a computed grid; `Container` already gives clipping and content-offset, so the grid only owns the positioning math, and pairing it with `ScrollContainer` gives vertical overflow.

Two constraints from the toolkit shape this. The container child arena is non-growable (64 KB default, throws on overflow) and there is no built-in virtualization; `ScrollContainer` realizes and clips all children. A library of hundreds of thumbnails would blow the arena and waste work drawing offscreen cells. So `GridContainer` virtualizes: it lays out all items logically but only realizes and draws the cells within (and slightly beyond) the visible viewport. That keeps the live-geometry thumbnail approach honest at scale.

### Thumbnails and previews

Both draw live geometry through the game's pipeline, the decision to "do what the game does." A thumbnail is the prepared mesh for a def, fit to the cell, submitted via `BatchRenderer` like the existing `Icon` component already does for small SVGs. The preview is the same at a larger size, with the procedural sample being ten prepared meshes from ten seeds. No offscreen FBO and no texture cache in the GUI; the FBO path is the CLI's. Both share geometry prep and the submission pipeline, so a preview in the tool is the same pixels as the asset in the game.

Text (def names, parsed fields) uses the `UI::Text` shape and the MSDF `FontRenderer`, available to any scene after the standard app init.

## CLI (`apps/asset-cli`)

A separate, dependency-light binary. It links `assets renderer foundation`, GLFW/GLEW/OpenGL for the hidden context, and `nlohmann_json` for machine-readable output. It does not link the UI toolkit and does not start a debug server. Subcommands:

- `list [--type simple|procedural] [--group G] [--json]`
- `search <query> [--json]`
- `inspect <defName> [--json]` — parsed fields, resolved paths, groups, placement, and warnings.
- `validate [--json] [--render-smoke]` — static validation, plus the optional render smoke test; non-zero exit on any error.
- `render <defName> --out <file.png> --size WxH [--bg <color>] [--seed N] [--samples N]` — render to PNG; `--samples` writes a batch of forms for a procedural asset; a fixed `--seed` is reproducible.

Everything the GUI can show, the CLI returns as JSON (list, search, inspect, validate); render returns a PNG. Argument parsing follows the repo's hand-rolled convention (no shared arg library exists); JSON uses `nlohmann_json`, which is available repo-wide even though the older `worldgen-cli` hand-writes its JSON. Exit codes follow the `worldgen-cli` precedent (0 success, non-zero per failure class).

The `render` and `validate --render-smoke` commands need the hidden GL context and the minimal init described above. `list`/`search`/`inspect`/`validate` (static) need only the registry and can run with no GL at all, which keeps the common agent and CI operations cheap.

## Build integration

- `apps/asset-manager/CMakeLists.txt` modeled on `ui-sandbox`: `add_executable`, link `engine ui renderer foundation assets planet-view world`, the POST_BUILD copies for `shaders/`, `fonts/`, and `assets/`, and `add_dependencies(asset-manager font-atlas)`.
- `apps/asset-cli/CMakeLists.txt` minimal: link `assets renderer foundation` plus GLFW/GLEW/OpenGL and `nlohmann_json::nlohmann_json`, copy `shaders/` and `assets/` (it needs the uber shader and the asset tree; it does not need fonts).
- Register both with `add_subdirectory` in the top-level `CMakeLists.txt` after the libs.

## Testing and CI

- Unit tests for each validation rule (missing reference, orphan SVG, folder/name mismatch, duplicate defName, ignored field, coerced value), following existing test patterns in the assets lib.
- A determinism test: `renderToPixels` with a fixed seed produces identical bytes across runs; different seeds produce different output for a procedural asset.
- Fit-to-rect tests: bounds and transform math, including degenerate cases (empty mesh, single point).
- The render smoke test over the whole library, run by `asset-cli validate --render-smoke` and in CI. Like the `RenderToTexture` test, it skips gracefully when GL context creation fails (no display), so it runs where a GPU is present and does not break headless CI where one is not.
- Async-load tests for the startup refactor: load-complete transition, error-gate transition, and the no-scene-reads-registry-before-done invariant.

## Traps to avoid

Named explicitly because each is an easy wrong turn.

- There is a second SVG rasterizer, `TilePatternBaker::bakeSvgToRgba`, that uses the NanoSVG rasterizer with no GL, no tessellator, and its own color handling. It is the banned shortcut. The "one renderer" principle means the Asset Manager never touches it; and it cannot draw procedural assets anyway.
- `getTemplate` is fixed-seed (42) and memoized. It is correct for a simple asset and for the one stable gallery thumbnail of a procedural asset, but it cannot sample a range. Procedural sampling and reproducible-by-seed renders go through `generateAsset` plus `tessellateAsset`, not `getTemplate`.
- Tiles and UI icons are outside the registry. The tool cannot get them from `getDefinitionNames()`; they are out of v1 scope, and surfacing them later means a separate disk scan and a code-reference orphan check.
- The container arena is non-growable and there is no built-in virtualization. The grid must virtualize, not lean on `ScrollContainer` to clip a few hundred realized cells.
- `BatchRenderer::flush` projects with a window or coordinate-system assumption. The FBO path must override it to the framebuffer size.

## Risks and open questions

- **Headless GL on CI.** The one real feasibility item. A hidden window works on any box with a GPU or display; a truly displayless runner needs EGL or software GL, which v1 defers. The decision recorded here is to run render checks where a GPU is available and skip otherwise. If CI must always run the render smoke test, that forces a GPU runner or the software-GL fallback, and that is a follow-up.
- **Doc and loader drift.** The asset docs describe `<variation>`, inheritance, and component references that the loader does not implement, so the inspector will visibly contradict the docs until they are reconciled. That cleanup, and the decision on whether to ever implement data-driven variation, is tracked separately from this tool.
- **Async-load surface area.** Making startup asynchronous touches engine bootstrap and the splash. It is specified as its own workstream so it can land and be tested before the GUI depends on it. The registry's population path is not currently structured to report incremental progress; the folder scan can count files up front to give `defsTotal`.
- **Naming.** `asset-manager` (GUI) and `asset-cli` (CLI) are provisional, settle when built.

## Work breakdown

Phased so each piece lands behind its own tests. The first three phases are independent of the GUI and unblock agents and CI early.

1. **Shared render path.** `computeBounds`/`fitToRect` in `renderer`; geometry prep (`prepareAsset`/`prepareSamples`) in `assets`; `AssetRenderer` (`renderToPixels`/`renderToPng`) over `RenderToTexture` with the FBO projection override and the factored flip-and-encode helper; the minimal headless init factored out of `AppLauncher`.
2. **Validation in the loader.** `AssetValidator` and `ValidationReport`; harden the loader to record coercions, ignored fields, duplicate defNames, name/folder mismatches, unresolved references, and orphans during the load; the render smoke test as a separate pass.
3. **CLI.** `apps/asset-cli` with list, search, inspect, validate, render; JSON output; exit codes; build integration.
4. **Async loading and splash.** The startup refactor: async load on a worker thread, `LoadProgress`, splash progress UI, load-complete and error-gate transitions, and the load-report scene.
5. **GUI.** `GridContainer` (wrapping and virtualized) in `libs/ui`; `apps/asset-manager` with the browser scene, faithful preview with procedural sampling and refresh, inspector, validation view, and reload; build integration.

## Related documentation

- [Asset Manager product spec](../../design/features/asset-manager/README.md)
- [Asset Definition Schema](../asset-system/asset-definitions.md) (documents some unimplemented fields; trust the loader)
- [Lua Scripting API](../asset-system/lua-scripting-api.md)
- [Vector Graphics System](../vector-graphics/INDEX.md)
- [Renderer Architecture](../renderer-architecture.md)
