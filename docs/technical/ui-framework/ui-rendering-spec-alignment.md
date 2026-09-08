# UI engine alignment with the layered UI rendering specification

Created: 2026-09-07
Status: planned (epic on the board; no code changed yet)

## What this is

The UI library (`libs/ui`, `libs/renderer`) was abstracted into a backend-agnostic specification so a second implementation (TypeScript, WebGL2) could be built to it and both projects could be scored against it. The specification lives in the dual-deck-builder repository at `docs/ui-rendering-spec/` ([README](https://github.com/kcjonson/dual-deck-builder/blob/main/docs/ui-rendering-spec/README.md)); it may move to its own repository, in which case this document is the one place to update the link. Rules are cited as `R3.14` (chapter 3, rule 14).

Five independent reviews challenged it. Review 03 checked every "worldsim does X" claim against this repository at main `c2e06dd` (2026-07-06) and is kept verbatim in the specification's `review/03-worldsim-maintainer.md`. The chapters that matter here:

- [Chapter 16](https://github.com/kcjonson/dual-deck-builder/blob/main/docs/ui-rendering-spec/16-backend-opengl.md): the mapping from the specification onto this codebase (16.1), the 32-item gap list (16.2), what the specification took from worldsim unchanged (16.3), and the migration order this document adopts (16.4).
- [Chapter 14](https://github.com/kcjonson/dual-deck-builder/blob/main/docs/ui-rendering-spec/14-testing-and-conformance.md): the conformance table with a worldsim column, corrected by review 03.
- [Chapter 17](https://github.com/kcjonson/dual-deck-builder/blob/main/docs/ui-rendering-spec/17-review-log.md), section 17.4: every review 03 finding and what the specification did with it.

## Why do any of it

The Game UI to Prototype Polish epic closed with zero lint violations on every screen, and the queued work is gameplay and world rendering. So the order below is by bugs prevented on the next screens, not by conformance score. Two things are worth knowing before deciding to skip it entirely:

- Item 8 of the gap list is visible today: the border path stores RGB plus width and forces borders opaque, so `line_hairline`, `line_edge`, and `line_strong` (alphas 0.10, 0.20, 0.36) render at full alpha on every bordered control.
- Items 1 to 3 are the last open class of ordering bug. Three shapes forward the parent's z through a thread-local, nine composite widgets forward their own z with `+1`, `+2`, and `+0.1` offsets, and `Text`, `Button`, and `Dialog` chrome forward nothing; a dialog's scrim and panel sit at z 0 and any HUD rectangle given a z for the lint sorts above them. `TaskListView.cpp:66-92` is a live case (background at z 2 over its label at z 3).

## Migration order (the epic's tasks)

From specification 16.4, each step one pull request unless noted.

1. Layout freeze and upward invalidation (gap items 15 and part of 16; R10.5, R10.18). A parent pointer set in `addChild`, invalidation walking up to a Fixed-on-both-axes container or a root, and per-pass resolved sizes instead of permanently definite axes. Removes the `EntityInfoView` hug-reset workaround. Write the two freeze tests from specification 10.9 first; the current 57-test suite passes with the freeze in place.
2. One z key (items 1 to 3), integer form. Add `Primitives::pushLayer(int)` for the six popup hosts, delete `RenderContext` and the per-widget offsets so `zIndex` is purely sibling-local, keep the batcher key an int; the named ladder (R3.5) is a constexpr table on top and can wait for the next token regeneration. Dispatch walks layers first using the same layer stamps as paint, which removes `GameUI`'s hand ordering. Land a batcher sort unit test in the same change (R2.22 names it as the gap) and delete `BatchKey`, `DrawCommand`, and the legacy `text.vert`/`text.frag` (item 27). Two consequences: the HUD's lint-exemption z values become layer promotions, and the lint exempts pairs that differ in effective layer as well as `zIndex`; the world domain keeps its numeric keys (R3.23 allows any integer key in a non-UI domain).
3. Small renderer corrections, one PR: three-state clip (item 4; keep `(0, 0, 0, 0)` as the shader's no-clip because the instanced and baked branches rely on it, cull on the CPU while the stack is empty, R4.1), a real clip on `TextInput` (item 6), an explicit flat mode (item 7; a constant in `data2.w` and one branch, which also removes the undefined `smoothstep(0, 0, x)` from lines, circle fans, and icon strokes), and an RGBA border (item 8).
4. Focus scope wiring (item 11): derive a dialog's focusables from its subtree instead of the never-populated `contentFocusables`.
5. Central hover and a captor (item 12): stop synthesising a `MouseMove` every frame in `Application.cpp`, derive enter and leave in the dispatcher, one captor pointer for `Slider`, `ScrollContainer`, and `TextInput`. Bubbling (R9.6) comes last, if at all here; each earlier step is observable on its own and the conformance row for the bubble stays "no" until then.
6. Metrics hygiene (items 20 to 22): GPU timer scope including the final flush, flush reasons, `inputHandleMs` split from update, per-window section maxima. Do it with the next performance capture.
7. Mount lifecycle and layout before render (items 14 and 16) when the next new screen is built so the migration has a customer. `FocusManager::Get()` stays as a shim during it. Two invariants the arena imposes: unmount runs before the arena's destructors, and the mount context outlives every component.
8. Opportunistic (items 9, 10, 13, 17, 19, 23 to 26, 28 to 32): premultiplied alpha, parent-local coordinates, SDF circles, tree primitives, per-glyph draw groups, and the rest. These pay off only if the UI is composited over something or shares code with the TypeScript implementation.

## Decisions recorded for this codebase

- Removal is a tombstone, not a free: the arena is a bump allocator, so `removeChild` marks the slot dead, the children and order views skip it, and `clearChildren` reclaims. Handles stay valid (R8.6). `reconcileChildren` is recommended here, not required (R8.27); clear-and-rebuild stays the documented idiom.
- The drag service, the component transform, and the animator score "not applicable" under R14.9 until a screen needs them. The animator, when it is wanted, is the existing Animation System epic built to R8.28.
- Straight alpha may stay until the UI is composited over another surface (R5.22 says so); the RGBA border in step 3 does not depend on it.
- The process-wide `Primitives` API is fine (R1.6 scopes the no-globals rule to layers 3 and 4); the singletons that go are focus, tooltip, and input.
- Positions are absolute here and parent-local in the specification (item 28). Nothing in steps 1 to 7 requires changing that.

## Relation to existing epics

- UI Improvements Phase 2: its engine tasks (tree snapshot, lint, LayoutContainer characterisation and auto-layout, unit suite) landed in kcjonson/worldsim#259 and kcjonson/worldsim#263 and read as still open on the board; the polish tasks remain and sit on top of step 1.
- UI Architecture: Animation System: the animator of R8.28; unaffected by this epic, but should be built to that rule when picked up.
- GPU-Based SDF Rendering for UI Primitives: its design document predates the shipped uber shader; step 3 holds the SDF corrections that are still open.

## Verification per step

Lint zero on every screen (`/api/ui/lint`), the LayoutContainer suite plus the freeze tests (step 1), the batcher sort test (step 2), screenshot comparison of the sandbox scenes before and after (steps 2 and 3, where borders change on purpose), the ui-tests target green, and a perf capture for step 6.
