#pragma once

// LandingSiteDetailsPanel - non-modal side panel for the landing scene.
//
// Renders a LandingSiteDetails (built by LandingSiteDetailsModel) as a corner
// panel: location header, the water verdict headline, a difficulty badge, then
// Water / Terrain / Climate sections. Immediate-mode, matching the rest of the
// landing scene. It draws in the top-right so it never covers the globe under
// the cursor.

#include "scenes/landing/LandingSiteDetailsModel.h"

#include <graphics/Rect.h>

namespace world_sim {

class LandingSiteDetailsPanel {
  public:
	// Top-right anchored: the panel's right edge sits at anchorRightX, top at
	// anchorTopY. Returns the bounds it drew into (for hit-testing if needed).
	Foundation::Rect render(const LandingSiteDetails& details,
	                        float anchorRightX, float anchorTopY) const;

	static constexpr float kWidth = 270.0F;

  private:
	static constexpr float kPadding		 = 14.0F;
	static constexpr float kSectionGap	 = 10.0F;
	static constexpr float kRowHeight	 = 19.0F;
	static constexpr float kHeaderHeight = 20.0F;
	static constexpr float kVerdictHeight = 34.0F; // two-line allowance
	static constexpr float kBadgeHeight	 = 24.0F;
};

} // namespace world_sim
