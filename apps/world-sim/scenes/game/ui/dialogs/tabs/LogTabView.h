#pragma once

// LogTabView - the dossier's Log tab. The game keeps no activity log yet, so it is an empty state.

#include <ui/Component.h>

namespace world_sim {

	struct LogData {};

	class LogTabView : public ui::Component {
	  public:
		struct Args {
			UI_COMMON_ARGS
		};

		void update(const LogData& data);

		[[nodiscard]] const char* typeName() const override { return "LogTabView"; }

	  protected:
		void onMount() override;
	};

} // namespace world_sim
