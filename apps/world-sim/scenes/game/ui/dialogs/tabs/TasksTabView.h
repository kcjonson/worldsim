#pragma once

// TasksTabView - Tab showing tasks known by a specific colonist.
//
// Mirrors the Salvage prototype's Tasks panel: a prominent "Currently" panel
// (amber left border) for the active task, then a "Known Work" divider and a
// list of known goal-tasks, each with a status Badge. Manual-render, like the
// other dossier tabs.

#include "scenes/game/ui/adapters/GlobalTaskAdapter.h"

#include <component/Container.h>
#include <graphics/Rect.h>

#include <string>
#include <vector>

namespace world_sim {

/// Data for Tasks tab - uses adapter's GlobalTaskDisplayData for consistency
struct TasksTabData {
	std::vector<adapters::GlobalTaskDisplayData> tasks;
	size_t totalCount = 0;

	// Current self-assigned task (from BioData), shown in the "Currently" panel.
	std::string currentTask;
};

class TasksTabView : public UI::Container {
  public:
	void create(const Foundation::Rect& contentBounds);
	void update(const TasksTabData& data);
	void render() override;

  private:
	Foundation::Rect contentBounds{};
	TasksTabData	 data_{};
};

} // namespace world_sim
