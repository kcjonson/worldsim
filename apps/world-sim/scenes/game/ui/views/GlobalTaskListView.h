#pragma once

// GlobalTaskListView - the colony's tasks, collapsible, in the top-right stack under ResourcesPanel.
// Collapsed, a "Tasks (N)" header with a down chevron; expanded, under it a fixed-height box whose rows
// (GlobalTaskRow: what, where and how far, then its status and who knows of it) scroll. Display only.
// kWidth wide unless its size says otherwise.

#include "scenes/game/ui/adapters/GlobalTaskAdapter.h"

#include <ui/Component.h>
#include <ui/components/Button.h>
#include <ui/shapes/Icon.h>
#include <ui/shapes/Rectangle.h>

#include <cstddef>
#include <vector>

namespace world_sim {

	class GlobalTaskListView : public ui::Component {
	  public:
		static constexpr float kWidth = 232.0F;

		struct Args {
			UI_COMMON_ARGS
		};

		GlobalTaskListView() = default;

		// When the model's data changes; the count follows the rows.
		void setTasks(const std::vector<adapters::GlobalTaskDisplayData>& tasks);
		// The header's count, which can run ahead of the rows the model last sent.
		void setTaskCount(std::size_t count);

		[[nodiscard]] bool isExpanded() const { return expanded; }

		[[nodiscard]] const char* typeName() const override { return "GlobalTaskListView"; }

	  protected:
		void onMount() override;

	  private:
		void toggle();
		void show();

		bool				   expanded = false;
		std::size_t			   taskCount = 0;
		ui::Ref<ui::Button>	   header;
		ui::Ref<ui::Icon>	   chevron;
		ui::Ref<ui::Rectangle> content;
		ui::Ref<ui::Component> list;
	};

} // namespace world_sim
