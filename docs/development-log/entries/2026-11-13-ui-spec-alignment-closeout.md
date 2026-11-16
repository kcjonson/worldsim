# 2026-11-13 - UI spec alignment: closed against salal, with the last GL state reads removed

## Summary

The epic "UI engine alignment with the layered UI rendering spec" was written on 2026-09-07 against `libs/ui` and `libs/renderer`. Both moved to salal, where the ui was rebuilt to the same specification for dual-deck-builder's C++ port and worldsim's conversion. Nobody had gone back to the epic, so its seven tasks still read as not started. An audit of each step against salal's code and tests at v0.40.0 (the tag this repo pinned that morning; main moved to v0.41.0 during the work) found nearly all of it done there. What was left for worldsim was the spec's gap 26, synchronous GL state reads around the game's own GL passes, and a few leftovers of the old ui.

## Details

### What salal already closed

Checked in salal's source and test names, not its docs:

- Step 1, layout freeze and upward invalidation: parent set in `addChild`, dirtiness walking up to a layout boundary, sizes resolved per pass, the spec's two freeze tests first in `Layout.test.cpp`, layout as its own pass before render.
- Step 2, one z key: an `int32` key on every draw, the ui's one sorted list as both paint and hit order, overlays in slots above the scene instead of the planned `Primitives::pushLayer`, the batcher sort test, and `RenderContext`, `BatchKey`, `DrawCommand` and the legacy text shaders gone.
- Step 3: an empty clip culls on the CPU, `TextInput` clips through `overflow`, flat is its own shader mode, border colour keeps its alpha.
- Step 4: a modal dialog's focus scope is its subtree.
- Step 5: enter and leave derived in the dispatcher, one pointer captor, bubbling.
- Step 7: `onMount`, no ui singletons, removal deferred during a dispatch.
- Step 8: premultiplied alpha, SDF circles, parent-local coordinates, one draw group per text run, and the rest. Golden screenshots (gap 24) were decided against in both repos.

Step 6, metrics hygiene, is half done in salal (input split from update, a layout section, per-submission GPU spans). Its one worldsim change, feeding `gpuRenderMs` from salal's GPU timing so the figure includes the ui, stays under the overnight perf pass because it changes what every capture reports and needs a baseline reset. The rest is on salal's board.

### What changed here

- `libs/planet-view/PlanetRenderer`: `render()` and `blitToScreen()` are one `render()` taking a `ScreenArea` (the destination rectangle in framebuffer pixels and the window's size). About twenty `glGet` and `glIsEnabled` reads and their restores are gone. The pass sets what it needs, calls `Renderer::Primitives::invalidateState()` between the offscreen draw and the composite, and puts back by hand only what salal's renderer never rebinds on its own: the window's framebuffer and its viewport. Everything else is covered by salal's contract, where each renderer draw makes its own bindings (program, vertex array, textures, blend) and depth test, culling and scissor are fixed state.
- `GlobeView`, `DecorativePlanet` and the sandbox's `PlanetScene` take the window from `App::viewport()` instead of reading `GL_VIEWPORT`.
- `ChunkRenderer::render`: the three `glIsEnabled` reads and the restore block around the per-frame tile pass are gone. It turns blend off (a flush that submitted nothing leaves blend as the last GL left it) and relies on the fixed state for depth test and culling.
- `GameScene`: the comment over the pre-ui `flush()` described a ui z space (panels 0, dialogs 500, menus 1000) that no longer exists. The flush stays, for its real reason: the scene's GPU timer span has to end after the world's draws are submitted. Draw order between a scene and its ui is `SceneStack`'s sort barrier.
- `TopBar`: the alert badge's `zIndex = 1` was redundant; it is the bell's last child.
- `UIState.h` deleted; nothing included it.

Left alone: `GpuHold` (a dev-only perf ballast that saves and restores its own state), and the two `GL_UNPACK_ALIGNMENT` reads at texture upload, which are client state and which salal's backend does the same way.

### Verification

`planet-view-tests`, `world-view-tests` and `world-sim-tests` pass. In a Debug build: the main menu's planet, the world creator's globe with its panels over it, the main menu again after leaving a game, the sandbox's planet scene followed by a ui scene, the game HUD, and the ground at four zooms all draw as before, with lint at 0 on the pre-game scenes. Scenarios 07 and 08 pass. The tests and the planet, HUD and sandbox views were run again after the pin moved to v0.41.0, whose backend adds stencil and colour-write state to what a draw restores and what `invalidateState()` resets; nothing changed on screen. No perf capture: the machine was busy.

The check also ran into the dry landing river on a freshly baked planet, already filed. Its cause is now on that bug: `TerrainChannelBuilder` drops the landing's channel rings as self-overlapping at the hairpin.

## Related Documentation

- salal `docs/ui-plan.md` and `docs/ui-design.md`: the record of the ui's design and which spec gaps each PR closed.
- salal `libs/renderer/backend/Backend.h`, the `State:` comment on `Backend`: the contract a game's own GL relies on.
- The specification: dual-deck-builder `docs/ui-rendering-spec/`, chapter 16 for the gap list.

## Next Steps

- The game scene's GPU figure (step 6), with a perf baseline reset.
- On salal's board: GPU sample validation and memory/CPU figures on Windows; ui test gaps and the scrollbar thumb's own capture field.
