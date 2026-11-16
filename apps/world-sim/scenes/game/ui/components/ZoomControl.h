#pragma once

// ZoomControl - the vertical zoom column (+, reset, -) with a mono percent readout under it, docked
// bottom right by ZoomControlPanel.

#include <ui/Component.h>
#include <ui/shapes/Text.h>

#include <functional>

namespace world_sim {

	class ZoomControl : public ui::Component {
	  public:
		struct Args {
			UI_COMMON_ARGS
			std::function<void()> onZoomIn;
			std::function<void()> onZoomOut;
			std::function<void()> onZoomReset;
		};

		explicit ZoomControl(const Args& args);

		void setZoomPercent(int percent);

		[[nodiscard]] const char* typeName() const override { return "ZoomControl"; }

	  protected:
		void onMount() override;

	  private:
		std::function<void()> onZoomIn;
		std::function<void()> onZoomOut;
		std::function<void()> onZoomReset;
		int					  zoomPercent = 100;
		ui::Ref<ui::Text>	  readout;
	};

} // namespace world_sim
