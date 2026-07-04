#pragma once

// LandingSiteDetailsPanel - the right-column "Landing Zone" panel of the
// landing sub-phase.
//
// Renders a LandingSiteDetails (built by LandingSiteDetailsModel) inside a
// UI::Panel (title "Landing Zone", kicker "Site Analysis", accent): coords
// readout, biome-name header, Recommended badge, Temp/Rainfall stat row, a
// skull-pip difficulty inset, the water verdict, and the field-report
// sections. Immediate-mode; the scene hands it the column rect.

#include "scenes/landing/LandingSiteDetailsModel.h"

#include <graphics/Rect.h>

namespace world_sim {

class LandingSiteDetailsPanel {
  public:
	void render(const LandingSiteDetails& details, const Foundation::Rect& bounds) const;

	static constexpr float kWidth = 360.0F;
};

} // namespace world_sim
