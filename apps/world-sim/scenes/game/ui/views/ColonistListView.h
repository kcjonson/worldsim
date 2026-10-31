#pragma once

// ColonistListView - the colonist roster down the left of the screen: a ColonistListItem card for each
// colonist, up to kMaxColonists, in the model's order. A press selects the colonist; a second press on
// the same card within kDoubleClickSeconds also follows it. The gaps between cards are the roster's, so
// a press there goes no further. kWidth wide unless its size says otherwise; its height follows.

#include "scenes/game/ui/components/ColonistListItem.h"
#include "scenes/game/ui/models/ColonistListModel.h"

#include <ecs/EntityID.h>
#include <ecs/World.h>

#include <ui/Component.h>

#include <cstddef>
#include <functional>

namespace world_sim {

	class ColonistListView : public ui::Component {
	  public:
		static constexpr float		 kWidth = 208.0F;
		static constexpr std::size_t kMaxColonists = 20;
		static constexpr double		 kDoubleClickSeconds = 0.4;

		struct Args {
			UI_COMMON_ARGS
			std::function<void(ecs::EntityID)> onColonistSelected;
			std::function<void(ecs::EntityID)> onColonistFollowed;
		};

		explicit ColonistListView(const Args& args);

		// Each frame: refreshes the model from the world, then the cards from the model.
		void update(ColonistListModel& model, ecs::World& world);

		[[nodiscard]] const char* typeName() const override { return "ColonistListView"; }

	  protected:
		void onMount() override;

	  private:
		void clicked(ecs::EntityID id, double time);

		std::function<void(ecs::EntityID)> onColonistSelected;
		std::function<void(ecs::EntityID)> onColonistFollowed;
		ecs::EntityID					   lastClickedId{0};
		double							   lastClickTime = 0.0;
	};

} // namespace world_sim
