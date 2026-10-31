#pragma once

// ZoomControlPanel - the floating zoom column (ZoomControl) GameUI docks in the bottom-right corner. It
// hugs the column, so the place it gets is the corner's insets.

#include "scenes/game/ui/components/ZoomControl.h"

#include <ui/Component.h>

#include <functional>

namespace world_sim {

	class ZoomControlPanel : public ui::Component {
	  public:
		struct Args {
			UI_COMMON_ARGS
			std::function<void()> onZoomIn;
			std::function<void()> onZoomOut;
			std::function<void()> onZoomReset;
		};

		explicit ZoomControlPanel(const Args& args);

		void setZoomPercent(int percent);

		[[nodiscard]] const char* typeName() const override { return "ZoomControlPanel"; }

	  protected:
		void onMount() override;

	  private:
		std::function<void()> onZoomIn;
		std::function<void()> onZoomOut;
		std::function<void()> onZoomReset;
		ui::Ref<ZoomControl>  control;
	};

} // namespace world_sim
