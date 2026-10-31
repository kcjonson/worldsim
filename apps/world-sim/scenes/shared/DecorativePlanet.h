#pragma once

// DecorativePlanet - non-interactive planet backdrop for the pre-game scenes
// (main menu, world creator). Loads the shipped quickstart planet at most once
// per session on a background thread, then renders a slowly spinning GlobeView
// docked at a caller-given rect with a backdrop -> transparent scrim so
// foreground text stays legible over the disc. Fades in when the first
// colorizer bake lands; skips silently (starfield only) when the planet file
// is absent.
//
// render() flushes pending 2D primitives first, so draw the starfield before
// calling it and the foreground UI after.

#include "GlobeView.h"

namespace world_sim {

class DecorativePlanet {
  public:
	void update(float dt);
	// `backdrop` is the scrim's colour, the scene's clear colour.
	void render(const Foundation::Rect& rect, float logicalW, float logicalH, Foundation::Color backdrop);

  private:
	GlobeView globe;
	bool	  worldSet{false};
	bool	  planetAbsent{false};
	float	  fade{0.0F};
};

} // namespace world_sim
