# UI Improvements Phase 2

**Status:** Draft (stub)
**Created:** 2026-06-21

## Overview

Phase 2 of the UI work, after the Salvage cutover landed the unified token-styled library. The headline goal is a final polish pass across the HUD, dialogs, and world creator. But two structural gaps make a polish pass unreliable today, so they come first.

## Why this is more than a polish pass

**1. The layout is not verifiable.** The only way to inspect the UI right now is `/api/ui/screenshot`. The planned UI-inspection tree (`/docs/technical/observability/ui-inspection.md`) was designed but never built — only the screenshot endpoint survived. Without a machine-readable layout, overlap, clipping, and alignment bugs are caught by eye, which is exactly where coordinate-based UI work goes wrong.

**2. The layout engine is thin.** `LayoutContainer` does direction + margin + H/V align + a hybrid sizing model, and it currently misbehaves. It has no gap, no padding, no main-axis distribution, and no Fill/Hug sizing — so spacing is hand-computed per view. Hand-computed coordinates are the source of the bugs.

Fix both, in order: verification harness, then engine, then polish. Build the net before the thing it catches.

## Workstream 1 — Layout verification (build first)

**UI-tree snapshot (`/api/ui/tree`).** Serialize the live UI hierarchy to JSON: per element `id`, `type`, `bounds {x,y,w,h}`, `zIndex`, `visible`. Development-build only; reuse the DebugServer ring-buffer pattern from `ui-inspection.md`. This is the deterministic ground truth the linter and the agent read instead of guessing coordinates.

**Layout linter.** Over the snapshot, assert the invariants normal HTML flow gives for free:
- no two visible siblings overlap (z-index marks intentional layering),
- every child sits inside its parent's bounds,
- nothing lands off the viewport,
- no visible element has zero or negative size,
- sibling gaps match the declared spacing.

Run it as a pre-done gate and as a test.

**Golden-rect regression.** Snapshot the bounds JSON per screen as checked-in baselines; the test fails naming the element that moved. Deterministic and legible (it tells you *what* shifted, not just "pixels differ"). Keep golden screenshots for visual-only regressions.

## Workstream 2 — The auto-layout engine

Extend the existing `LayoutContainer` in place (one-path rule; no new parallel container). It does Direction + margin + align + hybrid sizing today and misbehaves, so: characterization tests on current behavior, then fix, then add the auto-layout features.

Config:
- **Direction** — Vertical | Horizontal (exists).
- **Gap** — fixed spacing between children (e.g. `gap: 10`).
- **Padding** — per-side container insets.
- **Main-axis distribution** — Start | Center | End | SpaceBetween | SpaceAround | SpaceEvenly.
- **Cross-axis alignment** — Start | Center | End | Stretch.
- **Per-child sizing** — Fixed | Hug | Fill (Fill children share the leftover after Fixed/Hug are measured).

```
LayoutContainer{ direction: Vertical, gap: 10, padding: 16,
                 distribution: SpaceBetween, align: Stretch }
```

A thorough unit suite covers each dimension, nesting, dynamic content, and edge cases (zero children, overflow, single Fill, mixed Fixed/Hug/Fill). The engine's computed bounds flow into the snapshot, so Workstream 1's linter validates the engine for free.

## Workstream 3 — Migrate and polish

- Migrate the HUD, dialogs, and world creator onto the container; delete the old hand-positioning code.
- Dialog layout cleanup (footers, tab strips, list rows) on the container.
- Final polish pass: consistent spacing and padding driven by tokens plus the container, not per-view constants.
- Carried from Salvage UI Cutover: re-wire or delete the orphaned per-colonist `TaskListView`.

Each step is verified by the linter and golden-rects, with a screenshot for visual confirmation.

## Agent guidance (lands in CLAUDE.md)

After any coordinate or UI change: pull `/api/ui/tree`, run the layout linter, then screenshot for visual polish. Don't trust hand-computed coordinates. The point is structural — a real layout engine plus an external checker make spatial correctness stop depending on the author's coordinate arithmetic.

## Decisions taken

- Extend `LayoutContainer` in place, not a new container.
- Build the UI-tree snapshot — it does not exist today (only screenshots).
- Golden-rects for layout regression; golden screenshots for visual.
- Verification guidance lives in `CLAUDE.md`.

## Open questions

- Sizing model: adopt Figma's Fixed/Hug/Fill exactly, or a constraint-based model?
- Wrapping: is multi-line wrap (flex-wrap) in scope for v1, or single-line only?
- Migration: big-bang swap of all views, or container-by-container behind the existing API?
- Snapshot threading: confirm the ring-buffer / SSE pattern from `ui-inspection.md` is the right fit versus a simple synchronous REST snapshot.

## Out of scope

- Animation and transitions (separate epic — UI Architecture: Animation System, needs spec).
- A full constraint solver or 2D grid layout.

## References

- UI Inspection (designed, never built) — `/docs/technical/observability/ui-inspection.md`
- Layout System (current `LayoutContainer`) — `/docs/technical/ui-framework/layout-system.md`; `libs/ui/layout/LayoutContainer.h`
- Salvage UI Cutover (predecessor) — `/docs/design/ui/`
