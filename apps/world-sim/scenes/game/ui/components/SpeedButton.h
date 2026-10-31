#pragma once

// SpeedButton - one of the TopBar's pause, 1x, 3x and 10x buttons: an icon on the inset surface, filled
// with the accent while it's the current speed.

#include <ui/components/Control.h>
#include <ui/shapes/Icon.h>

#include <functional>
#include <string>

namespace world_sim {

	class SpeedButton : public ui::Control {
	  public:
		struct Args {
			UI_COMMON_ARGS
			std::string			  icon; // a name in theme::iconAtlas()
			std::function<void()> onPress;
		};

		explicit SpeedButton(const Args& args);

		// Whether this is the speed the game runs at.
		void			   setCurrent(bool value);
		[[nodiscard]] bool isCurrent() const { return current; }

		[[nodiscard]] const char* typeName() const override { return "SpeedButton"; }

	  protected:
		void onMount() override;
		void activate() override;

	  private:
		void tint();

		std::string			  iconName;
		std::function<void()> onPress;
		bool				  current = false;
		ui::Ref<ui::Icon>	  icon;
	};

} // namespace world_sim
