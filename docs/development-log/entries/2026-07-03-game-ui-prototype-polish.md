# Game UI to Prototype Polish

**Date:** 2026-07-03
**Epic:** Game UI to Prototype Polish (status.md)
**PRs:** #256, #257, #258, #259, #260, #261 (merged), #263 + four Wave-3 branches (in queue at time of writing)

## Summary

Brought the game UI (chrome, not the gameplay world layer) to the prototype's level of polish, per `docs/design/ui/fidelity-gaps.md` and `docs/design/ui/ui-improvements-phase-2.md`. The priority was layout correctness across window sizes rather than pixel identity: no overlapping elements, proper spacing/alignment, text that fits. The acceptance gate is machine-checked: `/api/ui/lint` reports zero violations on every screen.

## What was accomplished

**Foundations.** A layout verification harness (`/api/ui/tree` + `/api/ui/lint` over the existing DebugServer state handshake; `libs/ui/debug/UiTreeSerializer` + `LayoutLint`) landed first, then the LayoutContainer auto-layout engine: SizeMode Fixed/Hug/Fill with fillWeight, Distribution, CrossAlign (HAlign/VAlign deleted), per-side padding, gap, a three-pass computeLayout with real nested-container recursion, and `setLayoutSize` so Fill/Stretch Text children take their wrap width from resolved layout (the "text fits" fix). Per-axis definite flags distinguish "resolved to zero" from "never resolved" (Copilot catch). Characterization tests pinned the old engine's defects before the fix flipped them. MSDF text got baseline run-origin pixel snapping, unified measurement/rendering advances, atlas metadata validation, and a per-run snap cache.

**New flow.** New Game now runs Splash → MainMenu → ScenarioSelect → PartySelect → WorldCreator (survey → landing sub-phase) → GameLoading → Game. Scenario choice drives party size (1-8); the selected crew actually spawns with names and skill levels (`ecs::Skills`); Quick Start is untouched. Landing site selection stayed on the world-creation screen by design.

**Screens.** All pre-game screens rebuilt on the engine to the prototype specs: scenario cards with pips/badges/ticks, the party roster + dossier panel, splash boot log + Enter Expedition, the spec menu item set (Continue/Load/Credits disabled stubs; Quick Start kept), WorldCreator 3-column with the new WorldSurveyPanel (hasField-gated), preset grid, collapsible Advanced section (resolution + star/orbit params kept functional), footer bar, viz SegmentedControl, landing details panel + Commit to Descent dialog, and a restyled GameLoading (no prototype existed; design language extended). Starfield gained nebula/vignette; a shared DecorativePlanet renders the quickstart planet behind the menu and world creator (planet-view learned alpha compositing).

**In-game.** EntityInfoView fully rebuilt on the engine (+947/-1778): Avatar header, Needs/Bio/Gear/Log tabs, real Draft and Go-to actions, all non-colonist selection paths preserved (crafting/storage/construction dialogs open as before). New RegionMinimapPanel (viewport rect, colonist dots, crash-site marker, coords, off-map chevrons; terrain rendering deferred to the Minimap epic). ResourcesPanel shows real colony storage. Vertical zoom column, TopBar bell + colony mark, Zones stub dropdown. Dossier: Health→Needs rename, prototype tab order (Memory/Tasks kept), Gear paperdoll with real hands/belt/pack data, footer disabled treatment.

## Technical decisions

- Verification harness before engine before screens ("build the net before the thing it catches") — the lint caught real overlaps in every wave.
- Snapshot transport is synchronous REST over the existing `requestState` drain; pre-game scenes serve it via a shared `UiStateDrain` helper called post-layout in `render()`.
- Figma-style Fixed/Hug/Fill, no constraint solver, no flex-wrap in v1; container-by-container migration.
- Prototype-only functionality became disabled stubs; every C++-only feature (Quick Start, decision inspector, dev tools, advanced worldgen params, right-click viz cycling, zoom %) was preserved functional.
- The rare dossier first-open crash was investigated (50-iteration cdb harness) and concluded to be the already-fixed #254 Memory-LRU heap corruption.

## Incidents worth remembering

- **ccache cross-worktree poisoning** (PR #260): direct-mode manifests store absolute include paths that `base_dir` doesn't rewrite, so sibling worktrees could serve stale objects compiled against each other's headers. Fixed machine-wide with `depend_mode=true` + a cache namespace bump; setup script now applies and verifies the config.
- **Shared planet destroyed through a junction**: the `quickstart-planet` target re-bakes whenever worldgen-cli is fresher than the planet and truncates the shared file through a worktree junction. Caveat added to CLAUDE.md; never build that target from a worktree.

## Known follow-ups

- Font atlas is ASCII-only: `°`, `·`, `²` render as fallback boxes (screens use spaced-text/vector workarounds). Extending the atlas charset is engine work.
- UI::Text wrapping is single-family (Roboto); threading font family through Text/FontRenderer remains.
- No window-size CLI flag exists, so multi-resolution lint passes aren't scriptable yet (layouts are viewport-relative; the harness is ready for it).
- ParameterPanel's expanded Advanced section can overflow at very short windows (<~750 px logical); the panel body doesn't scroll.
- PartySelect dossier content spans the full panel width; a max-content-width column would match the prototype proportions better.
- Nested `layout()` freezes a Hug container at its last measured size; EntityInfoView resets hug axes on content change as a workaround — candidate engine improvement.
- Landing terrain rows show raw biome enum strings; the display-name spacing exists only in the panel header.

## Related documentation

- `docs/design/ui/fidelity-gaps.md` (the delta checklist this epic executed)
- `docs/design/ui/ui-improvements-phase-2.md` (layout workstream design, open questions now resolved in code)
- `docs/technical/ui-framework/layout-system.md` (rewritten to match the shipped engine)
- Investigation reports and screenshots referenced from the PR bodies
