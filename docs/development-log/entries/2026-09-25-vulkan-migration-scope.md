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

- Recommended raw Vulkan 1.3 behind a thin in-house layer over SDL3 GPU, WebGPU, bgfx, and staying on GL 4.6.
- Recommended rolling our own function loader, device setup, and allocator, while adopting glslang as a build-time shader compiler and MoltenVK for Mac.
- Proposed Phase 0 (build the seam on GL) as worth doing regardless of the Vulkan decision.
- Assumed an eventual Rust port (timing unknown). The Vulkan backend gets written once, in Rust with `ash`, as the first Rust module behind a C-ABI boundary; Phase 0 shapes the render API so C can express it. wgpu reconsidered as the Rust-native alternative and kept as the fallback.

## Next steps

- Answer the open questions in the spec (minimum Vulkan version, branch-vs-dual-backend delivery, Mac path, Phase 0 now).
- Install the Vulkan SDK and a Rust toolchain, then run the code spike (Rust + ash via Corrosion, driven from ui-sandbox) on a throwaway branch.
