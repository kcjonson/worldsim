# UI Scale setting

**Status:** planned (requirements only)
**Why:** The game renders the UI at a denser scale than the Salvage prototype. With the
window at 3072x1728, the logical viewport the UI lays out against is ~2.5x the
prototype's reference width, so elements built from the shared design tokens
(`fs_*`, `space_*`, sizes) come out visibly smaller on screen than the prototype mocks
(e.g. the colonist dossier renders at ~19% of screen width vs ~47% in the mock). This
is global — it affects every screen, not just the dossier. Rather than hand-enlarge
individual screens (which desyncs them from each other and from the tokens), expose a
single UI-scale factor the player controls.

## Goal

One user-facing setting that scales the entire UI up or down uniformly, so players on
high-DPI / large displays can make the UI larger (toward the prototype's proportions)
and players who want more on screen can make it denser. The default should land the UI
close to the prototype's intended proportions on a typical display.

## Requirements

- **Single global factor.** A scalar (e.g. 0.75x–1.5x, default TBD after a calibration
  pass) applied once, high in the render/layout path, so all screens — menus, worldgen,
  in-game HUD, dialogs — scale together. No per-screen scale constants.
- **Applies to layout, not just zoom.** Scaling must change the logical viewport the UI
  lays out against (so text, panels, gaps, hit-testing all scale coherently and reflow),
  not post-scale a rendered bitmap (which would blur text and break input mapping).
- **Input stays correct.** Pointer hit-testing must use the same scaled space the UI is
  drawn in (today input is in framebuffer pixels; verify it still lines up after scaling).
- **Live, no restart.** Changing the setting re-lays-out the UI immediately. Open
  screens/dialogs reflow; nothing needs a scene reload or planet regen.
- **Persisted** in user settings alongside the planned metric/imperial toggle (see
  [[units-naming-convention]]), surfaced in the Settings screen as a slider or stepped
  control with a live preview.
- **Crisp at any scale.** Text is MSDF and vector icons are tessellated, so both should
  stay sharp when the logical viewport changes; confirm the font atlas pixel-range and
  icon tessellation hold up at the extremes.
- **Sensible bounds + reset.** Clamp to a tested range; provide a "reset to default".

## Approach notes (not a design)

- The factor most likely belongs where `getLogicalViewport()` is derived (renderer
  coordinate system), so `logicalSize = framebufferSize / uiScale` and the whole UI
  layout follows. Confirm whether DPI is already folded in there before adding a second
  factor.
- Do a calibration pass first: measure the current logical viewport vs the prototype's
  1600x1000 reference and pick a default that matches the mock proportions on a common
  display, then expose the range around it.
- Auto-detecting a default from DPI/resolution is a possible enhancement; ship the
  manual setting first.

## Out of scope

- Per-element or per-screen overrides.
- Separate font-only scaling (the global factor covers it).
