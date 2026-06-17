#include "NeedBar.h"

#include <algorithm>

namespace world_sim {

	NeedBar::NeedBar(const Args& args)
		: height((args.height > 0.0F) ? args.height : ((args.size == NeedBarSize::Compact) ? kCompactHeight : kNormalHeight)) {

		position = args.position;
		size = {args.width, height};

		// Auto-toned Salvage bar: the value bands the fill (crit/warn/ok), with the
		// label drawn inline over the track.
		progressBarHandle = addChild(UI::ProgressBar(UI::ProgressBar::Args{
			.position = args.position,
			.width = args.width,
			.value = value / 100.0F, // Convert 0-100 to 0-1
			.tone = UI::Tone::Auto,
			.label = args.label,
			.size = UI::Size::Sm,
			.inlineLabel = true,
		}));
	}

	void NeedBar::setValue(float newValue) {
		value = std::clamp(newValue, 0.0F, 100.0F);
		if (auto* progressBar = getChild<UI::ProgressBar>(progressBarHandle)) {
			progressBar->setValue(value / 100.0F);
		}
	}

	void NeedBar::setLabel(const std::string& newLabel) {
		if (auto* progressBar = getChild<UI::ProgressBar>(progressBarHandle)) {
			progressBar->setLabel(newLabel);
		}
	}

	float NeedBar::getTotalHeight() const { return height; }

	void NeedBar::setPosition(Foundation::Vec2 newPos) {
		position = newPos;
		if (auto* progressBar = getChild<UI::ProgressBar>(progressBarHandle)) {
			progressBar->setPosition(newPos);
		}
	}

	void NeedBar::setWidth(float newWidth) {
		size.x = newWidth;
		if (auto* progressBar = getChild<UI::ProgressBar>(progressBarHandle)) {
			progressBar->setWidth(newWidth);
		}
	}

} // namespace world_sim
