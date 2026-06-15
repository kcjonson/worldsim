# Asset Manager

**Type:** Product spec (not an implementation spec)
**Status:** Draft
**Created:** 2026-06-14
**Audience:** Designers (primary), AI agents (primary), engineers (secondary)

## Summary

The Asset Manager is a standalone tool for working with the game's asset library: browse it, search it, see any asset rendered exactly as the game draws it, inspect what the loader actually parsed, validate the whole library, and render any asset to an image without launching the game. It does not create assets and it does not edit SVG art. It is a window onto the library, plus a programmable surface so AI agents can iterate on assets in a tight loop.

It has two faces over one shared core: a graphical app for designers, and a command-line/API surface for agents and CI. Both render through the game's own pipeline, so what either one shows is what ships.

## The problem

The library is going to get big, and most of it is still stubs and POCs. Assets are folder-per-def, some a single SVG, some a Lua generator that produces a family of forms. Today the only way to see how one looks is to boot the game, generate a world, and hunt for an instance. That is slow, it is non-deterministic, and for a procedural asset you can't easily see the range of what its generator makes.

Designers need to find an asset, see it faithfully, tweak its definition, and confirm the result in seconds. Agents need to do the same thing programmatically, in a loop, with no window and no game. Neither workflow exists yet. Building this tool honestly also forces a few stubbed pieces of the asset system into being properly defined, see "Foundations" below; that is a feature of the exercise, not a side effect.

## Principles

These are the non-negotiables. Everything else is a feature decision.

**One renderer, the game's.** Every pixel the tool shows or writes comes from the game's actual render path (NanoSVG parse, custom tessellation, the instanced/batched tiers, via the registry's tessellated templates). Nothing re-rasterizes assets in a second engine. A browser's SVG renderer, an image library, any reimplementation, all banned, because they would diverge from the game on exactly the procedural assets we most need to trust. If the tool and the game ever disagree about how an asset looks, the tool has failed at its one job. This is why the tool links the engine rather than reimplementing it, and it is why the tool is not a web page.

**Appearance is intrinsic. A tree is a tree.** An asset renders the same way no matter where it would sit in the world. The tool shows assets in pure isolation: no world seed, no biome, no placement context, no level-of-detail-by-distance. None of that touches how an asset is drawn. The only variability is internal to a procedural asset: its Lua generator produces a family of forms, and the tool can sample that family. That is the generator's own range, not the world acting on the asset. A simple SVG asset has no variability at all, it is one drawing.

**Reflect the loader, not the docs.** The asset documentation describes a richer system than the loader implements. The `<variation>` color/scale/rotation block, definition inheritance, and cross-asset references are documented but unparsed; the real per-instance variation today is brightness-only and lives in placement code, not data. The tool shows what the parser actually read and what the renderer actually draws. Where a definition sets a documented-but-ignored field, the tool flags it as a warning rather than pretending it works.

**Read-only on assets.** The tool never writes asset files. Designers edit XML in their own editor; the tool reloads on demand (a refresh action) and re-renders. This keeps the tool simple, keeps it honest about the on-disk state, and sidesteps write-safety entirely. (Editing definitions from inside the tool is a candidate for later, deliberately out of v1.)

## Form factor

Decided, and recorded here because these shaped the spec:

- **Standalone application.** Its own app, sibling to `ui-sandbox`, not a scene inside it. The sandbox is the engineer's bench for UI and rendering tests; the Asset Manager is the designer's bench for asset work. Different audiences, different tools.
- **Shared core, two faces.** A graphical app for designers and a CLI/API for agents and CI, both built on the same libraries (asset loading and registry, the renderer) and, above all, the same single "render this asset to pixels" call. That shared call is the fidelity contract: because both faces go through it, they cannot drift from each other or from the game.
- **Headless-capable.** The CLI renders to an image with no visible window, using an offscreen GL context (the FBO and readback path the renderer already uses in tests). This is what lets an agent, or CI, run it on a build box.

What this explicitly is not: not a web frontend, not a mode bolted into the game binary, not a second renderer.

## Audience

**Designers.** Open the app, browse or search the library, click an asset, see it rendered as in-game. A procedural asset shows a sample of generated forms, with a refresh to roll a new set, so the spread the generator produces is visible at a glance. Read the parsed definition next to the raw file. Edit the XML in their editor, hit refresh, confirm. Run validation before committing.

**AI agents.** Drive the CLI/API in a loop: render an asset to an image, look at it, change the XML, SVG, or Lua, render again, validate. The render-to-image call is the heart of this; without it an agent edits blind. Everything the GUI can show, the CLI can return in machine-readable form (list, search, inspect, validate as JSON; render as PNG).

**Engineers.** Secondary. A faithful, no-game-loop way to reproduce and inspect asset-pipeline issues.

## Capabilities

### Browse and search

See the whole library at a glance and narrow it fast. Browse by category, search by name and label, filter by type (simple vs procedural), by group, and by validation state (for example, show only assets with warnings). Each asset shows a faithful thumbnail, its def name, and its type. A procedural asset's thumbnail is one stable representative form, so the gallery doesn't shuffle on every visit; the variety lives in the detail view.

### Faithful preview

The core view. A selected asset rendered through the game's pipeline at a chosen size, on a chosen background, so transparency and edges read correctly.

- A **simple SVG asset** renders to a single image.
- A **procedural asset** renders a sample of its generator's output, around ten forms, with a refresh that rolls a new sample. This is how a designer sees the range an oak generator actually produces, not just one oak.

No seed field, no biome selector, no distance slider. The asset is shown as itself.

### Inspect

The definition behind the asset, shown as the loader understood it: the parsed fields, the resolved resource paths, the resources the def points at (the SVG, or the Lua script and its component SVGs), the groups it belongs to, and its placement rules. Shown beside the raw file so a designer can see the gap between what they wrote and what the loader took. Documented-but-ignored fields are called out here, not silently dropped.

### Validate

A whole-library check, runnable in the GUI and headless in CI, sharing one rule set so they never disagree. The v1 rule set:

- Required fields present and schema-conformant.
- References resolve: every `svgPath` and generator `script` points at a file that exists.
- No orphans: SVG files in an asset folder that nothing references.
- Primary-name integrity: the loader only treats `<Folder>/<Folder>.xml` as primary and silently skips a mismatch, so the validator surfaces filename/folder mismatches as errors (silent skips are a real footgun).
- No duplicate `defName`s.
- Documented-but-ignored fields flagged as warnings (so a def that sets `<variation>` knows it does nothing today).
- Render smoke test: every asset renders without error, and every procedural generator produces a form without error. This is the payoff of having a render API, it turns "does the whole library still draw" into one command.

Validation output is human-readable in the GUI and machine-readable from the CLI, with a non-zero exit on failure so CI can gate on it.

### Render to image

The agent-facing primitive, and the thing the GUI preview is built on. Given an asset name, an output size, and a background, write a PNG that is pixel-identical to how the game would draw that asset. No window, no game. For a procedural asset, the call produces a form, or a batch of forms, from its generator. The render is reproducible when an agent or CI needs it to be (a fixed internal value yields the same output every run), and the GUI's refresh deliberately re-samples. This is what closes the agent's loop: edit, render, look, repeat.

### Refresh

Reload the library from disk on demand, in the GUI, so a designer who just edited a def in their editor sees the change without restarting. The tool reads the current on-disk truth; it does not cache a stale view. (Distinct from the preview's roll-a-new-sample refresh for procedural assets.)

## Workflows

**Designer tweaks a procedural asset.** Search for "oak", select OakTree, see ten generated oaks. The canopies are too sparse, and refreshing for another ten confirms it's the generator, not one unlucky roll. Open `OakTree.xml`, raise `leafDensity`, save. Back in the Asset Manager, hit refresh; the sample re-renders denser. Run validation, green, commit.

**Designer audits the library.** Run validation. Three warnings: two defs set a `<variation>` block that does nothing, one SVG is an orphan. Filter the browser to "has warnings", confirm, fix it in their editor, refresh, re-validate.

**Agent iterates on art.** An agent is asked to make the berry bush read better. It renders BerryBush to a PNG, inspects it, edits the SVG, renders again, compares. For a procedural target it renders a batch to see the generator's range. When it's happy, it runs validation to confirm it broke nothing, then opens a PR. The agent never launches the game.

**CI guards the library.** On every change touching `assets/`, CI runs the headless validator, including the render smoke test. A broken reference, a renamed folder that orphans a def, or an asset that no longer draws fails the build.

## Foundations

What this tool reuses, and what it forces into existence. The asset system is mostly young; the tool's requirements pin down a few pieces that are currently stubs.

Reused as-is (already working): the XML loader and registry, the Lua generator and its parameter passing, the SVG loader and tessellator, and the offscreen render-to-texture path.

Built or firmed up by this effort:

- **The render contract.** "Render this asset to an image" does not exist as a function yet; scenes assemble the pieces by hand. The tool needs it as a shared engine library call (so the game, agents, and tool all share one path), with the dead-simple signature the appearance principle implies: asset plus output size, no world inputs. The pieces to build it already exist; this is assembly, not invention.
- **The schema source of truth.** There is no XSD; the parser is the de-facto schema, and it's lenient (missing fields default silently, bad values coerce silently). Validation needs a defined notion of valid, so this effort hardens and documents the parser as the canonical schema and makes silent coercions and unparsed fields surface as warnings.
- **Data-driven variation stays deferred.** The `<variation>` block remains unimplemented and flagged, not built. Procedural variety comes from the generator's own randomness, which already works.

## Scope

**In, v1:** the standalone GUI (browse, search, faithful preview with the procedural sample-and-refresh, inspect, reload); the CLI/API (list, search, inspect, validate, render-to-image), all machine-readable; the default validation rule set; coverage of entity art assets (the folder-per-def XML + SVG/Lua assets in the registry).

**Out, v1:** tile surface patterns and UI icons (they live on different render and registration paths, so they come later, and when they do the orphan check must also scan code references); creating assets; editing SVG art; editing definitions from inside the tool (read-only plus refresh instead); non-visual data defs (recipes, config, work types); data-driven `<variation>`; pixel-diff regression against golden images.

**Candidates for later:** tile patterns and UI icons as first-class catalogued assets; in-tool editing of the genuinely data-driven fields (placement, groups, generator params, rendering tiers) with write-back and re-validate; golden-image regression; deeper validation (perf budgets on `variantCount`/`maxInstances`, group sanity).

## Risks and open questions

- **Headless GL environment.** Render-to-image needs a GL context. On a workstation or a build box with a GPU, an invisible window works (proven by the existing render-to-texture path). A truly displayless sandbox needs a surfaceless/EGL or software-GL (Mesa llvmpipe) fallback. This decides where agents and CI can run the render commands, and it's the one real feasibility item to nail down. It does not threaten the GUI, only the headless renders.
- **Doc/loader drift.** The asset docs describe features the loader does not implement, so the tool will visibly contradict the docs until they're reconciled. Worth a separate cleanup pass (and a decision on whether to ever implement data-driven variation), tracked independently of this tool.
- **Thumbnail grid.** The existing UI toolkit stacks single rows and columns but has no wrapping grid layout, so a thumbnail wall and the procedural sample view need either a small reusable grid component or manual positioning. Minor, but it's the one piece of new UI that doesn't already exist.
- **Naming.** Provisional: `asset-manager` for the app, a shared CLI for the headless surface. Not load-bearing; settle when we build it.

## Success criteria

- A designer can go from "I want to see asset X" to seeing it rendered as in-game in seconds, without launching the game.
- A preview in the tool is pixel-identical to the same asset in the game.
- A procedural asset's range is visible at a glance: a sample of forms, refreshable for more.
- An agent can render any asset to a PNG with one headless call.
- One validation command checks the whole library and is the same check in the GUI and in CI.
- The tool never disagrees with the game about how an asset looks. If it could, that's the bug that matters most.

## Related documentation

- [Asset Definition Schema](../../../technical/asset-system/asset-definitions.md) (note: documents some unimplemented fields, see Principles)
- [Vector Graphics System](../../../technical/vector-graphics/INDEX.md)
- [Debug Server](../debug-server/) (precedent: a developer tool specced as a feature)
