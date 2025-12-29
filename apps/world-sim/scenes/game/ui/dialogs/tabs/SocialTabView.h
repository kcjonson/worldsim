#pragma once

#include <component/Component.h>
#include <graphics/Rect.h>
#include <layer/Layer.h>

#include <string>

namespace world_sim {

/// Data for Social tab (placeholder)
struct SocialData {
	std::string placeholder = "Relationships not yet tracked";
};

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
