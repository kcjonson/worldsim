#pragma once

// ColonistListItem - a selectable colonist card in the roster: a mood-tinted Avatar, the
// colonist's first name with a mood percentage, and an activity meter (the current task and its
// progress). Selected, the card is raised with an amber left edge; under the player's direct control,
// a teal border and right edge. A press calls onSelect.

#include "scenes/game/ui/adapters/ColonistAdapter.h"

#include <theme/Avatar.h>

#include <ui/components/Control.h>
#include <ui/components/ProgressBar.h>
#include <ui/shapes/Text.h>

#include <functional>
#include <string>

namespace world_sim {

	class ColonistListItem : public ui::Control {
	  public:
		using SelectCallback = std::function<void(ecs::EntityID)>;

		struct Args {
			UI_COMMON_ARGS
			adapters::ColonistData colonist;
			bool				   isSelected = false;
			SelectCallback		   onSelect;
		};

		explicit ColonistListItem(const Args& args);

		void setSelected(bool value);
		void setColonistData(const adapters::ColonistData& data);

		[[nodiscard]] ecs::EntityID getEntityId() const { return data.id; }
		[[nodiscard]] bool			isSelected() const { return selected; }

		[[nodiscard]] const char* typeName() const override { return "ColonistListItem"; }

	  protected:
		void onMount() override;
		void activate() override;

	  private:
		void show();

		adapters::ColonistData	 data;
		bool					 selected;
		SelectCallback			 onSelect;
		ui::Ref<theme::Avatar>	 avatar;
		ui::Ref<ui::Text>		 name;
		ui::Ref<ui::Text>		 mood;
		ui::Ref<ui::ProgressBar> activity;
		ui::Ref<ui::Rectangle>	 selectedEdge;
		ui::Ref<ui::Rectangle>	 controlledEdge;
	};

} // namespace world_sim
