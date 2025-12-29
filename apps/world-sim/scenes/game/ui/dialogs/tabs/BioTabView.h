#pragma once

#include "../ColonistDetailsModel.h"

#include <component/Component.h>
#include <graphics/Rect.h>
#include <layer/Layer.h>

namespace world_sim {

/// Bio tab content for ColonistDetailsDialog
/// Shows: name, age, mood, current task, traits, background
class BioTabView : public UI::Component {
  public:
	/// Create the tab view with content bounds from parent dialog
	void create(const Foundation::Rect& contentBounds);

	/// Update content from model data
	void update(const BioData& data);

  private:
	UI::LayerHandle layoutHandle;
};

} // namespace world_sim
