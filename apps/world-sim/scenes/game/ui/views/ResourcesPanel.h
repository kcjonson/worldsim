#pragma once

// ResourcesPanel - colony storage, collapsible (main-game-ui-design.md, section 4). Collapsed, a
// "Storage" header with a down chevron; expanded, under it, one row per resource summed across every
// storage container (name left, count right), scrolling past a height cap, or "No stockpiles built"
// while there are no containers. kWidth wide unless its size says otherwise; its height follows.

#include "scenes/game/ui/adapters/ResourcesAdapter.h"

#include <ui/Component.h>
#include <ui/shapes/Icon.h>
#include <ui/shapes/Rectangle.h>
#include <ui/shapes/Text.h>

#include <cstddef>
#include <functional>
#include <vector>

namespace world_sim {

	class ResourcesPanel : public ui::Component {
	  public:
		static constexpr float kWidth = 232.0F;

		struct Args {
			UI_COMMON_ARGS
			std::function<void()> onToggle; // after it expands or collapses
		};

		explicit ResourcesPanel(const Args& args);

		void setResources(const std::vector<adapters::ResourceRowData>& rows, std::size_t containers);

		[[nodiscard]] bool isExpanded() const { return expanded; }

		[[nodiscard]] const char* typeName() const override { return "ResourcesPanel"; }

	  protected:
		void onMount() override;

	  private:
		void toggle();
		void show();

		std::function<void()>				   onToggle;
		bool								   expanded = false;
		std::vector<adapters::ResourceRowData> rows;
		std::size_t							   containerCount = 0;
		ui::Ref<ui::Icon>					   chevron;
		ui::Ref<ui::Rectangle>				   content;
		ui::Ref<ui::Text>					   emptyMessage;
		ui::Ref<ui::Component>				   list;
	};

} // namespace world_sim
