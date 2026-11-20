#pragma once

// ThoughtLogList - a colonist's thoughts as a column of blocks, newest first: when it arrived (mono, faint),
// the event, feeling and mood adjustment, and the quip in quotes when it has one. Above them a Notable / All
// control picks the filter, and below them "Show older" adds another page. An empty log, or one with nothing
// notable, shows an empty state. Used by the info panel's and the dossier's Log tabs.

#include "scenes/game/ui/models/ThoughtLogModel.h"

#include <ui/Component.h>
#include <ui/components/Button.h>
#include <ui/components/SegmentedControl.h>
#include <ui/shapes/Text.h>

#include <cstdint>
#include <functional>

namespace world_sim {

	class ThoughtLogBlock : public ui::Component {
	  public:
		struct Args {
			UI_COMMON_ARGS
			ThoughtLogEntry entry;
		};

		explicit ThoughtLogBlock(const Args& args)
			: entry(args.entry) {}

		void setEntry(const ThoughtLogEntry& newEntry);

		[[nodiscard]] const char* typeName() const override { return "ThoughtLogBlock"; }

	  protected:
		void onMount() override;

	  private:
		void show();

		ThoughtLogEntry	  entry;
		ui::Ref<ui::Text> whenText;
		ui::Ref<ui::Text> summaryText;
		ui::Ref<ui::Text> lineText;
	};

	class ThoughtLogList : public ui::Component {
	  public:
		struct Args {
			UI_COMMON_ARGS
			std::function<void(ThoughtLogFilter)> onFilter;
			std::function<void()>				  onShowOlder;
		};

		explicit ThoughtLogList(const Args& args)
			: onFilter(args.onFilter),
			  onShowOlder(args.onShowOlder) {}

		/// Adds blocks for new thoughts, updates the ones whose text moved, and drops the ones gone; nothing when
		/// the model hasn't been built again since the last show.
		void show(const ThoughtLogModel& log);

		[[nodiscard]] const char* typeName() const override { return "ThoughtLogList"; }

	  protected:
		void onMount() override;

	  private:
		std::function<void(ThoughtLogFilter)> onFilter;
		std::function<void()>				  onShowOlder;
		ui::Ref<ui::SegmentedControl>		  filterChoice;
		ui::Ref<ui::Component>				  blocks;
		ui::Ref<ui::Component>				  empty;
		ui::Ref<ui::Component>				  nothingNotable;
		ui::Ref<ui::Button>					  olderButton;
		uint64_t							  shownVersion = 0;
		bool								  shown = false;
	};

} // namespace world_sim
