#pragma once

// TasksTabView - the dossier's Tasks tab: a "Currently" panel with an amber left edge for the running
// task and its progress, then a Known Work divider over the goal tasks the colonist knows of, each with
// its detail, its distance and a status badge.

#include "scenes/game/ui/adapters/GlobalTaskAdapter.h"

#include <ui/Component.h>
#include <ui/components/ProgressBar.h>
#include <ui/shapes/Text.h>

#include <string>
#include <vector>

namespace world_sim {

	struct TasksTabData {
		std::vector<adapters::GlobalTaskDisplayData> tasks;
		size_t										 totalCount = 0;

		// The colonist's own task (BioData's), for the "Currently" panel.
		std::string currentTask;
		// The running action's progress, 0 to 1; below 0 while idle or still travelling, with no meter.
		float currentProgress = -1.0F;
	};

	class TasksTabView : public ui::Component {
	  public:
		struct Args {
			UI_COMMON_ARGS
		};

		void update(const TasksTabData& data);

		[[nodiscard]] const char* typeName() const override { return "TasksTabView"; }

	  protected:
		void onMount() override;

	  private:
		ui::Ref<ui::Text>		 current;
		ui::Ref<ui::ProgressBar> progress;
		ui::Ref<ui::Text>		 noWork;
		ui::Ref<ui::Component>	 taskList;
		ui::Ref<ui::Text>		 more;
	};

} // namespace world_sim
