#pragma once

// GlobalTaskRow - a task in the global task list: the description with its position and distance at
// the end, then its status (coloured) with what holds it up and, in the global view, who knows of it.
// Both wrap to the row's width. Display only.

#include "scenes/game/ui/adapters/GlobalTaskAdapter.h"

#include <ui/Component.h>
#include <ui/shapes/Text.h>

namespace world_sim {

	class GlobalTaskRow : public ui::Component {
	  public:
		struct Args {
			UI_COMMON_ARGS
			adapters::GlobalTaskDisplayData task;
		};

		explicit GlobalTaskRow(const Args& args);

		void setTaskData(const adapters::GlobalTaskDisplayData& value);

		[[nodiscard]] const char* typeName() const override { return "GlobalTaskRow"; }

	  protected:
		void onMount() override;

	  private:
		void show();

		adapters::GlobalTaskDisplayData task;
		ui::Ref<ui::Text>				description;
		ui::Ref<ui::Text>				where;
		ui::Ref<ui::Text>				line2;
	};

} // namespace world_sim
