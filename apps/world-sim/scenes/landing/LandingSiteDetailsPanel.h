#pragma once

// LandingSiteDetailsPanel - the "Landing Zone" pane, stacked beneath the World
// Survey in the world creator's right column during review.
//
// Shows a LandingSiteDetails (built by LandingSiteDetailsModel) in a panel
// (title "Landing Zone", kicker "Site Analysis") filling the host the scene
// gives it, its body scrolling: coords readout, biome-name header, Recommended
// badge, Temp/Rainfall stat row, a skull-pip difficulty inset, the water
// verdict, and the field-report sections. Each show() rebuilds the body.

#include "scenes/landing/LandingSiteDetailsModel.h"

#include <ui/Component.h>
#include <ui/theme/Theme.h>

namespace world_sim {

class LandingSiteDetailsPanel {
  public:
	// `host` is a flex column; the panel fills it.
	void mount(ui::Ref<ui::Component> host, const ui::Theme& theme);

	void show(const LandingSiteDetails& details);

  private:
	ui::Ref<ui::Component> body;
	ui::Ref<ui::Component> content;
	const ui::Theme*	   theme = nullptr;
};

} // namespace world_sim
