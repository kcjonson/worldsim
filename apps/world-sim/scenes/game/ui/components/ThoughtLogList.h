#pragma once

// ThoughtLogList - a colonist's thoughts as a column of blocks, newest first: when it arrived (mono, faint),
// the event, feeling and mood adjustment, and the quip in quotes when it has one. An empty log shows an
// empty state. Used by the info panel's and the dossier's Log tabs.

#include "scenes/game/ui/models/ThoughtLogModel.h"

#include <ui/Component.h>
#include <ui/shapes/Text.h>

#include <vector>

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
		};

		/// Adds blocks for new thoughts, updates the ones whose text moved, and drops the ones gone.
		void show(const std::vector<ThoughtLogEntry>& entries);

		[[nodiscard]] const char* typeName() const override { return "ThoughtLogList"; }

	  protected:
		void onMount() override;

	  private:
		ui::Ref<ui::Component> blocks;
		ui::Ref<ui::Component> empty;
	};

} // namespace world_sim
