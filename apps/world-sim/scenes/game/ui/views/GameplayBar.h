#pragma once

// GameplayBar - the action bar along the bottom of the screen: the Actions, Build, Production,
// Furniture and Zones dropdowns, each opening its menu upward, then the Rooms overlay toggle. A
// panel-surface row hugging its buttons, kHeight tall.

#include <ui/components/Button.h>
#include <ui/components/DropdownButton.h>
#include <ui/shapes/Rectangle.h>

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace world_sim {

	class GameplayBar : public ui::Rectangle {
	  public:
		static constexpr float kHeight = 48.0F;

		struct Args {
			UI_COMMON_ARGS
			std::function<void(const std::string&)> onActionSelected;
			std::function<void(const std::string&)> onProductionSelected;
			std::function<void(const std::string&)> onFurnitureSelected;
			std::function<void(const std::string&)> onStructureSelected; // a structure tool, such as "foundation"
			std::function<void()>					onRoomsToggle;		 // the rooms overlay, as the R hotkey flips it
		};

		explicit GameplayBar(const Args& args);

		void closeAllDropdowns();
		// The Production menu: {defName, label} for each placeable production station.
		void setProductionItems(const std::vector<std::pair<std::string, std::string>>& items);
		// The Rooms toggle's fill follows the overlay, which GameUI pushes so the button and the hotkey agree.
		void setRoomsActive(bool active);

		[[nodiscard]] const char* typeName() const override { return "GameplayBar"; }

	  protected:
		void onMount() override;

	  private:
		void tintRooms();

		std::function<void(const std::string&)>	 onActionSelected;
		std::function<void(const std::string&)>	 onProductionSelected;
		std::function<void(const std::string&)>	 onFurnitureSelected;
		std::function<void(const std::string&)>	 onStructureSelected;
		std::function<void()>					 onRoomsToggle;
		bool									 roomsActive = false;
		std::vector<ui::Ref<ui::DropdownButton>> dropdowns;
		ui::Ref<ui::DropdownButton>				 production;
		ui::Ref<ui::Button>						 rooms;
	};

} // namespace world_sim
