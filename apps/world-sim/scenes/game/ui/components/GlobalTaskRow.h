#pragma once

// GlobalTaskRow - A display row for a task in the global task list
//
// Layout (two lines):
// ┌────────────────────────────────────────────┐
// │ Harvest Berry Bush         (10, 15)  5m    │
// │ Available • Known by: Bob, Alice           │
// └────────────────────────────────────────────┘
//
// Line 1: Task description + position + distance
// Line 2: Status (colored) + "Known by" (if global view)
//
// This is a display-only component with no click handling.

#include "scenes/game/ui/adapters/GlobalTaskAdapter.h"

#include <component/Component.h>
#include <graphics/Color.h>
#include <layer/Layer.h>
#include <shapes/Shapes.h>

namespace world_sim {

/// A display row for a single task
class GlobalTaskRow : public UI::Component {
  public:
	struct Args {
		adapters::GlobalTaskDisplayData task;
		float width = 280.0F;
		bool showKnownBy = true;
		std::string id = "task_row";
	};

	explicit GlobalTaskRow(const Args& args);

	/// Update the displayed data
	void setTaskData(const adapters::GlobalTaskDisplayData& task);

	/// Override to update child positions when LayoutContainer positions us
	void setPosition(float x, float y) override;

  private:
	[[nodiscard]] Foundation::Color getStatusColor(const adapters::GlobalTaskDisplayData& task) const;

	float rowWidth;
	bool showKnownBy;

	// Child handles
	UI::LayerHandle line1Handle;
	UI::LayerHandle line2Handle;

	// Layout constants
	static constexpr float kRowHeight = 36.0F;
	static constexpr float kPadding = 4.0F;
	static constexpr float kLine1FontSize = 12.0F;
	static constexpr float kLine2FontSize = 10.0F;
	static constexpr float kLineSpacing = 18.0F;
};

} // namespace world_sim
