# OpenGL to Vulkan migration scoping

**Date:** 2026-09-25
**Spec:** /docs/technical/rendering/vulkan-migration.md

## Summary

Doc-only scoping of a move from OpenGL 3.3 to Vulkan. Audited every GL call site, surveyed
the options and prior art, and wrote a phased plan plus a code-spike plan. No code changed and
no decision is made yet.

## Findings

- About 5.4k lines of GL-bearing code out of ~128k lines of non-test C++. The vector pipeline, tessellator, `Primitives` surface, and all game logic survive untouched.
- The GL feature surface is small (no UBOs, SSBOs, compute, stencil, MSAA). The risk is behavioral: per-frame buffer reuse, mid-frame resource creation and destruction, uploads between draws, and scattered Y/depth conventions.
- No real backend seam: planet-view, the world chunk renderers, and the font atlas call GL directly, and engine code sets uniforms on the borrowed uber-shader program.
- macOS is the deciding factor. GL is frozen at 4.1 there with no compute; Vulkan via MoltenVK gives one modern API on all three targets.

## Decisions of note

- The codebase will be fully rewritten in Rust by an automated Claude Code (Fable) run, timing unknown. Vulkan lands as part of that rewrite; no C++ Vulkan backend and no C++ seam refactor, since both would be discarded.
- In the rewrite, everything is translated literally except rendering, which is redesigned against a renderer contract (handles, per-frame ring, deferred destruction, upload phase, explicit vertex layouts, push constants + bindless, one owner for Y/depth conventions).
- Target Vulkan 1.4 core (no optional-feature fallbacks), accepting the loss of 1.3-only drivers. Recommended `ash` over wgpu, SDL3 GPU, bgfx, and staying on GL, with wgpu as the evidence-based alternative the spike tests side by side.
- Roll our own device setup and allocator; adopt ash, glslang (build tool), winit, glam, and MoltenVK for Mac.
- The only C++ rendering work worth doing now: a golden image set captured from the GL build, and moving the screenshot readback before swap so the goldens are trustworthy.

## Next steps

- Answer the open questions in the spec (ash vs wgpu, Mac path, SVG rasterization in Rust, where goldens live).
- Capture the golden image set on the C++ build.
- Install the Vulkan SDK and a Rust toolchain, then run the vertical-slice spike on a throwaway branch.
