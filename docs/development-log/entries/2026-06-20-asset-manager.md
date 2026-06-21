# Asset Manager: shared render path, validation, CLI, async splash, designer GUI

**Date:** 2026-06-20

## Summary

A standalone tool to browse, inspect, validate, and render the asset library through the
game's own render path, with a designer GUI (`apps/asset-manager`) and a headless CLI
(`apps/asset-cli`) over one shared core. Before this there was no function that turned a def
name into pixels: scenes hand-assembled "draw this asset" themselves (`GrassScene`,
`SvgScene`), so there was nothing the tool, the validator, or the previews could reuse. The
epic built that foundation first, then layered the rest on top.

Built in five phases, each its own merged PR. Phases 1 through 4 landed earlier; Phase 5 (the
GUI) is this PR and completes the epic.

## Phase 1: shared render path

The piece everything else rests on. One def-name-plus-seed-to-pixels path, going through the
real tessellator and `BatchRenderer`, never the second CPU rasterizer
(`TilePatternBaker::bakeSvgToRgba` is banned for this).

- `renderer` `MeshBounds` (`computeBounds`, `fitToRect`): bounds of a `TessellatedMesh` and a
  uniform-scale, centered, aspect-preserving fit into a target rect. This is the helper scenes
  used to hardcode (e.g. `SvgScene` kScale/kCenterX).
- `AssetRegistry::buildMesh(defName, seed, out)`: the single public, uncached seed-to-mesh.
  Simple assets copy the SVG template (seed irrelevant); procedural assets run `generateAsset`
  then the private `tessellateAsset`. Not `getTemplate` for procedural sampling, that is
  fixed-seed-42 and memoized, so it yields one form.
- `assets` `AssetRenderer`: CPU prep (`prepareAsset`/`prepareSamples`: build + fit, `hasOutput`
  false on empty) and GL render to image (`renderToPixels`/`renderToPng`: `RenderToTexture` FBO,
  viewport-fallback projection so `BatchRenderer` uses `ortho(0,w,h,0)`, `glReadPixels`, vertical
  flip).
- `foundation` `PngEncoder`: the single `STB_IMAGE_WRITE_IMPLEMENTATION` site
  (`encodePngToMemory`/`writePngToFile`); `DebugServer` screenshot code refactored onto it.
- Tests: determinism (same seed yields byte-identical buffers, different seeds differ) and
  fidelity (a Simple asset's center pixel near its SVG fill), with a hidden GL context that
  `GTEST_SKIP`s when no display.

## Phase 2: load-time validation

`AssetValidator` + `ValidationReport` (Severity, issues, error/warning counts). Validates on
load and the registry exposes `getValidationReport()`. Catches missing refs, duplicate
defNames, name/folder mismatches, ignored fields, variantCount drift, bad assetType, orphan
SVGs. Per-probe `error_code` (no shared/throwing filesystem calls). The full GL render smoke
test is deferred to the CLI, where a context exists.

## Phase 3: headless CLI (`apps/asset-cli`)

`list`/`search`/`inspect`/`validate`/`render`, `--json`, real exit codes, server-less so many
can run in parallel. Render uses a hidden GL context. Implementing `Foundation::getExecutableDir`
on Windows (it was a TODO stub returning empty) made shaders and assets resolve relative to the
exe via `findResource` regardless of cwd, which is what unblocked rendering from the repo root.

## Phase 4: async loading + splash

`AssetRegistry::beginLoadAsync` loads on a worker thread with an atomic `LoadProgress`.
`AppConfig.loadAssetsAsync` turns it on, but only on the default splash path; a `--scene`
override loads synchronously (only the splash, reading atomics, is live during the async load,
which keeps it thread-safe without locks). The splash polls progress, transitions to the menu on
a clean load, or blocks with an error summary when validation finds errors.

## Phase 5: designer GUI (`apps/asset-manager`)

A master-detail browser, dogfooded from `libs/ui` components rather than raw rendering. The
first cut was a thumbnail grid; that was rejected for a tree-plus-detail layout.

- Left: a category tree (`ScrollContainer` + `LayoutContainer`) grouped by the asset's parent
  folder, collapsible, search-filtered, each row a custom `AssetThumbnail` leaf (mesh from
  `prepareAsset`, file-scope cache so rebuilt rows do not re-tessellate) next to its label, with
  hover and an amber selection bar.
- Right: a detail pane with the faithful preview, metadata, per-asset validation warnings, and
  the asset's XML config in a scrolled code well.
- Top bar: search input, reload (clears the thumbnail cache + registry and re-validates), and a
  validation summary (asset/error/warning counts).
- Tree rebuilds (selection, toggle, search) are deferred to `update()` via a pending flag so
  rows are never freed mid-event-dispatch.

Styling uses a scoped `Theme.h` (the prototype's used-future tokens as `Foundation::Color`
constants) rather than touching the shared `libs/ui` theme, which would restyle the whole game.
CRLF handling on file reads and `wordWrap` on multi-line `UI::Text` fixed the XML/metadata
rendering ("?" at line ends), and the detail pane stacks by measured `getHeight()` to avoid
overlap (HTML gives this for free; in C++ it is manual).

Two follow-ups the GUI motivated but does not yet do: a procedural sample-and-refresh strip
(reroll seeds for procedural assets), and real font atlases (Chakra Petch / JetBrains Mono) to
match the prototype's type.

## Dev tooling

The sandbox and asset manager moved off the shared debug port 8081 to their own defaults
(ui-sandbox 8090, asset-manager 8070; world-sim stays 8081) so they no longer collide; one
instance per port, `--http-port` still overrides.

## Related documentation

- Spec: `/docs/design/features/asset-manager/`, `/docs/technical/asset-manager/`
- Plan: `/docs/development-log/plans/` (build plan, archived on epic completion)

## Next steps

Procedural sample-and-refresh strip; SDF font atlases for the prototype typefaces; update the
stale "Sandbox Control" port note in `CLAUDE.md`.
