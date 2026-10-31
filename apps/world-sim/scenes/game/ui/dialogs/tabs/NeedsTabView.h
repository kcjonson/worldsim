#pragma once

// NeedsTabView - the dossier's Needs tab: a full-width Mood meter and its caption, then two columns,
// the vital and (dimmed) comfort needs as meters on the left and a Body & Ailments empty state on the
// right.

#include <ui/Component.h>
#include <ui/components/ProgressBar.h>

#include <array>
#include <string>

namespace world_sim {

	struct NeedsData {
		// 0-100 for all 8 needs, by NeedType.
		std::array<float, 8> needValues{};
		// Below its seek threshold.
		std::array<bool, 8> needsAttention{};
		std::array<bool, 8> isCritical{};

		float		mood = 100.0F;
		std::string moodLabel;
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
		ui::Ref<ui::ProgressBar>				moodMeter;
		std::array<ui::Ref<ui::ProgressBar>, 8> needMeters;
	};

} // namespace world_sim
