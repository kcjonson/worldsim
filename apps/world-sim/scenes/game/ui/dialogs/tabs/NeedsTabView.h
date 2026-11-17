#pragma once

// NeedsTabView - the dossier's Needs tab: a full-width Mood meter and its caption, then two columns,
// the vital and (dimmed) comfort needs as meters on the left, and on the right the mood breakdown (the
// needs' share, each held thought's amount and time left, and the target they make) over a Body &
// Ailments empty state.

#include "scenes/game/ui/models/ThoughtLogModel.h"

#include <ui/Component.h>
#include <ui/components/ProgressBar.h>
#include <ui/shapes/Text.h>

#include <array>
#include <string>

namespace world_sim {

	struct NeedsData {
		// 0-100 for all 8 needs, by NeedType.
		std::array<float, 8> needValues{};
		// Below its seek threshold.
		std::array<bool, 8> needsAttention{};
		std::array<bool, 8> isCritical{};

		float		  mood = 100.0F;
		std::string	  moodLabel;
		MoodBreakdown moodBreakdown;
	};

	class NeedsTabView : public ui::Component {
	  public:
		struct Args {
			UI_COMMON_ARGS
		};

		void update(const NeedsData& data);

		[[nodiscard]] const char* typeName() const override { return "NeedsTabView"; }

	  protected:
		void onMount() override;

	  private:
		void showBreakdown(const MoodBreakdown& breakdown);

		ui::Ref<ui::ProgressBar>				moodMeter;
		std::array<ui::Ref<ui::ProgressBar>, 8> needMeters;
		ui::Ref<ui::Text>						needsShare;
		ui::Ref<ui::Component>					thoughtRows;
		ui::Ref<ui::Text>						noThoughts;
		ui::Ref<ui::Text>						moodTarget;
		MoodBreakdown							shownBreakdown;
		bool									breakdownShown = false;
	};

} // namespace world_sim
