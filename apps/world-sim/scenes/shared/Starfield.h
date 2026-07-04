#pragma once

// Starfield - the Salvage deep-space backdrop shared by the splash, main menu,
// and world-creator screens. A faint nebula wash (the renderer has no
// radial-gradient primitive, so each glow is a triangle fan fading tint ->
// transparent from center to rim), two seeded layers of star dots, and an edge
// vignette, matching docs/ui-prototype/src/scene/Starfield.tsx. Star layout is
// deterministic per seed, so a given screen always shows the same sky.
//
// Call once as the first draw after the GL clear, before any other UI.

#include "graphics/Color.h"
#include "primitives/Primitives.h"
#include "theme/Tokens.h"

#include <cmath>
#include <cstdint>

namespace world_sim {

	namespace starfield_detail {

		// Radial-gradient stand-in: a triangle fan whose center carries the
		// tint at peakAlpha and whose rim is fully transparent.
		inline void drawNebulaGlow(float cx, float cy, float radius, Foundation::Color tint, float peakAlpha) {
			constexpr int	 kSegments = 48;
			Foundation::Vec2 v[kSegments + 1];
			uint16_t		 idx[kSegments * 3];
			Foundation::Color colors[kSegments + 1];
			v[0] = {cx, cy};
			colors[0] = Foundation::Color{tint.r, tint.g, tint.b, peakAlpha};
			for (int i = 0; i < kSegments; ++i) {
				const float angle = static_cast<float>(i) * (6.2831853F / static_cast<float>(kSegments));
				v[i + 1] = {cx + radius * std::cos(angle), cy + radius * std::sin(angle)};
				colors[i + 1] = Foundation::Color{tint.r, tint.g, tint.b, 0.0F};
				idx[i * 3] = 0;
				idx[i * 3 + 1] = static_cast<uint16_t>(i + 1);
				idx[i * 3 + 2] = static_cast<uint16_t>(1 + (i + 1) % kSegments);
			}
			Renderer::Primitives::drawTriangles(Renderer::Primitives::TrianglesArgs{
				.vertices = v, .indices = idx, .vertexCount = kSegments + 1, .indexCount = kSegments * 3, .colors = colors});
		}

		// One border of the vignette: a quad fading from `alpha` black at the
		// screen edge (outerA->outerB) to transparent inward (innerB->innerA).
		inline void drawEdgeFade(Foundation::Vec2 outerA, Foundation::Vec2 outerB,
								 Foundation::Vec2 innerB, Foundation::Vec2 innerA, float alpha) {
			const Foundation::Vec2	v[4] = {outerA, outerB, innerB, innerA};
			const uint16_t			idx[6] = {0, 1, 2, 0, 2, 3};
			const Foundation::Color edge{0.0F, 0.0F, 0.0F, alpha};
			const Foundation::Color inner{0.0F, 0.0F, 0.0F, 0.0F};
			const Foundation::Color colors[4] = {edge, edge, inner, inner};
			Renderer::Primitives::drawTriangles(Renderer::Primitives::TrianglesArgs{
				.vertices = v, .indices = idx, .vertexCount = 4, .indexCount = 6, .colors = colors});
		}

	} // namespace starfield_detail

	inline void renderStarfield(int viewportW, int viewportH, uint32_t seed, bool dim = false) {
		const float w = static_cast<float>(viewportW);
		const float h = static_cast<float>(viewportH);

		// mulberry32 - the same small PRNG the prototype uses, so the layout is
		// stable and reproducible per seed.
		uint32_t state = seed != 0U ? seed : 1U;
		auto	 next = [&state]() -> float {
			 state += 0x6D2B79F5U;
			 uint32_t z = state;
			 z = (z ^ (z >> 15)) * (z | 1U);
			 z ^= z + (z ^ (z >> 7)) * (z | 61U);
			 z = z ^ (z >> 14);
			 return static_cast<float>(z & 0x00FFFFFFU) / 16777216.0F;
		};

		const float brightness = dim ? 0.6F : 1.0F;

		// Nebula wash under the stars, per the prototype's radial gradients: a
		// warm accent bloom top-center, a cool data glow low-right, and the two
		// blurred nebula patches.
		using starfield_detail::drawNebulaGlow;
		drawNebulaGlow(0.5F * w, -0.1F * h, 0.65F * w, UI::accent, 0.05F * brightness);
		drawNebulaGlow(0.8F * w, 1.1F * h, 0.7F * w, UI::data, 0.06F * brightness);
		drawNebulaGlow(0.24F * w, 0.3F * h, 0.4F * w, UI::data, 0.07F * brightness);
		drawNebulaGlow(0.76F * w, 0.66F * h, 0.45F * w, UI::accent, 0.055F * brightness);

		// Far layer: ~220 small, faint, bluish-white dots (#cdd6e6).
		for (int i = 0; i < 220; ++i) {
			const float x = next() * w;
			const float y = next() * h;
			const float r = 0.5F + next() * 0.7F;
			const float a = (0.25F + next() * 0.55F) * brightness;
			Renderer::Primitives::drawCircle({.center = {x, y}, .radius = r, .style = {.fill = Foundation::Color{0.80F, 0.84F, 0.90F, a}}});
		}

		// Near layer: ~55 brighter, slightly larger white dots.
		for (int i = 0; i < 55; ++i) {
			const float x = next() * w;
			const float y = next() * h;
			const float r = 0.8F + next() * 1.2F;
			const float a = (0.40F + next() * 0.50F) * brightness;
			Renderer::Primitives::drawCircle({.center = {x, y}, .radius = r, .style = {.fill = Foundation::Color{1.0F, 1.0F, 1.0F, a}}});
		}

		// Vignette over the stars: the prototype's radial edge darkening,
		// squared off as four border fades (corners overlap and deepen).
		using starfield_detail::drawEdgeFade;
		const float edgeAlpha = UI::vignette_opacity * 0.6F;
		const float fadeX = w * 0.2F;
		const float fadeY = h * 0.24F;
		drawEdgeFade({0.0F, 0.0F}, {w, 0.0F}, {w, fadeY}, {0.0F, fadeY}, edgeAlpha);
		drawEdgeFade({w, h}, {0.0F, h}, {0.0F, h - fadeY}, {w, h - fadeY}, edgeAlpha);
		drawEdgeFade({0.0F, h}, {0.0F, 0.0F}, {fadeX, 0.0F}, {fadeX, h}, edgeAlpha);
		drawEdgeFade({w, 0.0F}, {w, h}, {w - fadeX, h}, {w - fadeX, 0.0F}, edgeAlpha);
	}

} // namespace world_sim
