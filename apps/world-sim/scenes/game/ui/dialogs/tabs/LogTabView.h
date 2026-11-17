#pragma once

// LogTabView - the dossier's Log tab: the colonist's thoughts, newest first, each one block of the event,
// the feeling, the mood adjustment, the quip and when it arrived. It grows with the log, and the dossier's
// tab area scrolls it.

#include "scenes/game/ui/components/ThoughtLogList.h"

#include <ui/Component.h>

#include <cstdint>
#include <vector>

namespace world_sim {

	struct LogData {
		std::vector<ThoughtLogEntry> entries;
		/// Bumped each time the entries are built again, so an unchanged log isn't walked.
		uint64_t revision = 0;
	};

	class LogTabView : public ui::Component {
	  public:
		struct Args {
			UI_COMMON_ARGS
		};

		void update(const LogData& data);

		[[nodiscard]] const char* typeName() const override { return "LogTabView"; }

	  protected:
		void onMount() override;

	  private:
		ui::Ref<ThoughtLogList> list;
		uint64_t				shownRevision = 0;
		bool					shown = false;
	};

} // namespace world_sim
