#include "NeedBar.h"

#include <algorithm>

namespace world_sim {

	namespace {
		constexpr float kCompactHeight = 10.0F;
	} // namespace

	NeedBar::NeedBar(const Args& args)
		: ui::ProgressBar(
			  {.value = std::clamp(args.percent, 0.0F, 100.0F) / 100.0F,
			   .tone = ui::Tone::Auto,
			   .label = args.label,
			   .controlSize = ui::ControlSize::Sm,
			   .inlineLabel = true}
		  ),
		  barSize(args.barSize) {}

	void NeedBar::onMount() {
		ui::ProgressBar::onMount();
		if (barSize == NeedBarSize::Compact && getSizeSpec().height.isAuto()) {
			setSize({getSizeSpec().width, kCompactHeight});
		}
	}

	void NeedBar::setPercent(float percent) {
		setValue(std::clamp(percent, 0.0F, 100.0F) / 100.0F);
	}

} // namespace world_sim
