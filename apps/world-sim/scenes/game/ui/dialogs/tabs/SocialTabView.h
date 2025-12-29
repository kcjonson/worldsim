#pragma once

#include "../ColonistDetailsModel.h"

#include <component/Component.h>
#include <graphics/Rect.h>
#include <layer/Layer.h>

namespace world_sim {

/// Social tab content for ColonistDetailsDialog
/// Placeholder for future relationship tracking
class SocialTabView : public UI::Component {
  public:
	/// Create the tab view with content bounds from parent dialog
	void create(const Foundation::Rect& contentBounds);

	/// Update content from model data
	void update(const SocialData& data);

  private:
	UI::LayerHandle layoutHandle;
};

} // namespace world_sim
