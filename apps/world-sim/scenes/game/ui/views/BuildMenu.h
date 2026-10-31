#pragma once

// BuildMenu - the popup of items the player can place (a crafting spot, say), titled "Build": a raised
// column of one button per item, kWidth wide, hugging its items. Choosing one calls onSelect with its
// defName; the player's cancel (Escape) while it holds focus calls onClose.

#include "scenes/game/world/placement/PlacementTypes.h"

#include <ui/shapes/Rectangle.h>

#include <functional>
#include <string>
#include <vector>

namespace world_sim {

	class BuildMenu : public ui::Rectangle {
	  public:
		static constexpr float kWidth = 180.0F;

		using SelectCallback = std::function<void(const std::string& defName)>;

		struct Args {
			UI_COMMON_ARGS
			SelectCallback		  onSelect;
			std::function<void()> onClose;
		};

		explicit BuildMenu(const Args& args);

		void setItems(const std::vector<BuildMenuItem>& items);

		[[nodiscard]] const char* typeName() const override { return "BuildMenu"; }

	  protected:
		void onMount() override;

	  private:
		void showItems();

		SelectCallback			   onSelect;
		std::function<void()>	   onClose;
		std::vector<BuildMenuItem> items;
		ui::Ref<ui::Component>	   list;
	};

} // namespace world_sim
