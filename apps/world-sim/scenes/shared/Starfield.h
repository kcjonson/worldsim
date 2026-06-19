#pragma once

// Starfield - the Salvage deep-space backdrop shared by the splash, main menu,
// and world-creator screens. Two seeded layers of star dots scattered over the
// void: a dense layer of faint far stars and a sparser layer of brighter near
// stars. Deterministic per seed, so a given screen always shows the same sky.
//
// Call once as the first draw after the GL clear, before any other UI.

#include "graphics/Color.h"
#include "primitives/Primitives.h"

#include <cstdint>

namespace world_sim {

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
	}

} // namespace world_sim
