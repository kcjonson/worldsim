#pragma once

// GlobalTaskRow - a task in the global task list, two lines: the description with its position and
// distance, then its status (coloured) with what holds it up and, in the global view, who knows of it.
// Display only.

#include "scenes/game/ui/adapters/GlobalTaskAdapter.h"

#include <ui/Component.h>
#include <ui/shapes/Text.h>

namespace world_sim {

	class GlobalTaskRow : public ui::Component {
	  public:
		struct Args {
			UI_COMMON_ARGS
			adapters::GlobalTaskDisplayData task;
			bool							showKnownBy = true;
		};

		explicit GlobalTaskRow(const Args& args);

		void setTaskData(const adapters::GlobalTaskDisplayData& value);

		[[nodiscard]] const char* typeName() const override { return "GlobalTaskRow"; }

	  protected:
		void onMount() override;

	  private:
		void show();

		adapters::GlobalTaskDisplayData task;
		bool							showKnownBy;
		ui::Ref<ui::Text>				line1;
		ui::Ref<ui::Text>				line2;
	};

} // namespace world_sim
