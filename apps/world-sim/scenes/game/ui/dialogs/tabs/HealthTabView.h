#pragma once

#include "../ColonistDetailsModel.h"

#include <component/Component.h>
#include <graphics/Rect.h>
#include <layer/Layer.h>

namespace world_sim {

/// Health tab content for ColonistDetailsDialog
/// Two-column layout:
/// - Left: Mood + Need bars + Mood modifiers
/// - Right: Body parts & ailments
class HealthTabView : public UI::Component {
  public:
	/// Create the tab view with content bounds from parent dialog
	void create(const Foundation::Rect& contentBounds);

	/// Update content from model data
	void update(const HealthData& data);

  private:
	UI::LayerHandle layoutHandle;
};

} // namespace world_sim
