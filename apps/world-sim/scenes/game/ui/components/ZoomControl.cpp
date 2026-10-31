#include "ZoomControl.h"

#include "scenes/shared/SalalUi.h"

#include <ui/Context.h>
#include <ui/components/Button.h>

#include <string>

namespace world_sim {

	namespace {
		constexpr float kButtonSize = 28.0F;
		constexpr float kSpacing = 4.0F;
	} // namespace

	ZoomControl::ZoomControl(const Args& args)
		: onZoomIn(args.onZoomIn),
		  onZoomOut(args.onZoomOut),
		  onZoomReset(args.onZoomReset) {}

	void ZoomControl::onMount() {
		setLayout(
			{.display = ui::Display::Flex,
			 .flexDirection = ui::FlexDirection::Column,
			 .gap = kSpacing,
			 .alignItems = ui::AlignItems::Center}
		);
		const auto zoomButton = [&](const char* id, const char* icon, const std::function<void()>& onPress) {
			addChild<ui::Button>(
				{.id = id, .size = {kButtonSize, kButtonSize}, .icon = icon, .variant = ui::ButtonVariant::Primary, .onPress = onPress}
			);
		};
		zoomButton("btn_zoom_in", "zoom_in", onZoomIn);
		zoomButton("btn_zoom_reset", "zoom_reset", onZoomReset);
		zoomButton("btn_zoom_out", "zoom_out", onZoomOut);
		ui::Style label = world_sim::text(
			getContext()->getTheme(),
			{.family = ui::tokens::font::family::mono, .size = tokens::fontSize::fsXs, .color = tokens::color::textDim}
		);
		label.textAlign = Foundation::HorizontalAlign::Center;
		readout = addChild<ui::Text>({.id = "zoom_percent", .size = {kButtonSize, ui::kAuto}, .style = label, .text = "100%"});
		setZoomPercent(zoomPercent);
	}

	void ZoomControl::setZoomPercent(int percent) {
		zoomPercent = percent;
		if (ui::Text* shown = readout.get()) {
			shown->setText(std::to_string(zoomPercent) + "%");
		}
	}

} // namespace world_sim
