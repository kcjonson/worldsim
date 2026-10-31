#pragma once

// TopBar - the colony command strip across the top of the screen. Left, the colony mark (nested
// diamonds) with the colony's name over "N survivors . Sol D"; centred, the clock (day, season, time)
// and the speed pill; right, the alert bell with its critical-toast count and the Menu button. Its
// width is the parent's and its height kHeight unless its size says otherwise.

#include "scenes/game/ui/components/SpeedButton.h"
#include "scenes/game/ui/models/TimeModel.h"

#include <ecs/systems/TimeSystem.h>

#include <ui/components/Badge.h>
#include <ui/shapes/Rectangle.h>
#include <ui/shapes/Text.h>

#include <cstddef>
#include <functional>
#include <string>

namespace world_sim {

	class TopBar : public ui::Rectangle {
	  public:
		static constexpr float kHeight = 52.0F;

		struct Args {
			UI_COMMON_ARGS
			std::function<void()>				onPause;
			std::function<void(ecs::GameSpeed)> onSpeedChange;
			std::function<void()>				onMenuClick;
			std::string							colonyName = "Hollow Reach";
		};

		explicit TopBar(const Args& args);

		// From the time model and the survivor count, each frame.
		void updateData(const TimeModel& timeModel, int survivorCount);
		// Live critical toasts, the bell's badge; none at 0.
		void setAlertCount(std::size_t count);

		[[nodiscard]] const char* typeName() const override { return "TopBar"; }

	  protected:
		void onMount() override;
		// Its background fades toward the foot, which a Style has no field for.
		void render() override;

	  private:
		void pressSpeed(ecs::GameSpeed speed);
		void showSpeed(ecs::GameSpeed speed);

		std::function<void()>				onPause;
		std::function<void(ecs::GameSpeed)> onSpeedChange;
		std::function<void()>				onMenuClick;
		std::string							colonyName;
		ui::Ref<ui::Text>					survivors;
		ui::Ref<ui::Text>					sol;
		ui::Ref<ui::Text>					day;
		ui::Ref<ui::Text>					season;
		ui::Ref<ui::Text>					clock;
		ui::Ref<SpeedButton>				pauseButton;
		ui::Ref<SpeedButton>				normalButton;
		ui::Ref<SpeedButton>				fastButton;
		ui::Ref<SpeedButton>				veryFastButton;
		ui::Ref<ui::Badge>					alertBadge;
	};

} // namespace world_sim
