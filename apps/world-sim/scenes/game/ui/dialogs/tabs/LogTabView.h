#pragma once

// LogTabView - the dossier's Log tab: the colonist's thoughts, newest first, each one block of the event,
// the feeling, the mood adjustment, the quip and when it arrived, under the Notable / All filter and a page
// at a time. The dossier doesn't size itself to it: the tab fills the tab area the other tabs set, and scrolls.

#include "scenes/game/ui/components/ThoughtLogList.h"

#include <ui/Component.h>

#include <functional>

namespace world_sim {

	class LogTabView : public ui::Component {
	  public:
		struct Args {
			UI_COMMON_ARGS
			std::function<void(ThoughtLogFilter)> onFilter;
			std::function<void()>				  onShowOlder;
		};

		explicit LogTabView(const Args& args)
			: onFilter(args.onFilter),
			  onShowOlder(args.onShowOlder) {}

		void update(const ThoughtLogModel& log);

		[[nodiscard]] const char* typeName() const override { return "LogTabView"; }

	  protected:
		void onMount() override;

	  private:
		std::function<void(ThoughtLogFilter)> onFilter;
		std::function<void()>				  onShowOlder;
		ui::Ref<ThoughtLogList>				  list;
	};

} // namespace world_sim
