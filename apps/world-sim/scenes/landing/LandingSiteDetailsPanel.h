#pragma once

// LandingSiteDetailsPanel - the "Landing Zone" pane, stacked beneath the World
// Survey in the world creator's right column during review.
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
};

} // namespace world_sim
