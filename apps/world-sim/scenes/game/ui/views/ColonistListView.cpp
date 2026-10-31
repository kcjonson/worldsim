#include "ColonistListView.h"

#include "scenes/game/ui/Look.h"

#include <ui/Context.h>

#include <string>
#include <vector>

namespace world_sim {

	ColonistListView::ColonistListView(const Args& args)
		: onColonistSelected(args.onColonistSelected),
		  onColonistFollowed(args.onColonistFollowed) {}

	void ColonistListView::onMount() {
		if (getSizeSpec().width.isAuto()) {
			setSize({kWidth, getSizeSpec().height});
		}
		ui::Layout column = getLayout();
		column.display = ui::Display::Flex;
		column.flexDirection = ui::FlexDirection::Column;
		column.gap = getContext()->getTheme().get(look::t::spacing::space2);
		setLayout(column);
		setPointerEvents(ui::PointerEvents::Auto);
	}

	void ColonistListView::update(ColonistListModel& model, ecs::World& world) {
		model.refresh(world);
		const ecs::EntityID						   selectedId = model.selectedId();
		const std::vector<adapters::ColonistData>* shown = &model.colonists();
		std::vector<adapters::ColonistData>		   capped;
		if (shown->size() > kMaxColonists) {
			capped.assign(shown->begin(), shown->begin() + kMaxColonists);
			shown = &capped;
		}
		// Every frame, whatever the model's change flag says: activity progress ticks each frame, and a
		// card's texts skip a value they already show.
		reconcileChildren(
			*shown,
			[](const adapters::ColonistData& colonist) { return std::to_string(colonist.id); },
			[&](const adapters::ColonistData& colonist) -> ui::Ref<ui::Component> {
				const ecs::EntityID id = colonist.id;
				return addChild<ColonistListItem>({
					.id = "colonist_" + std::to_string(id),
					.size = {ui::percent(100.0F), ui::kAuto},
					.onClick = [this, id](ui::PointerEvent& event) { clicked(id, event.time); },
					.colonist = colonist,
					.isSelected = colonist.id == selectedId,
					.onSelect = onColonistSelected,
				});
			},
			[selectedId](ui::Component& card, const adapters::ColonistData& colonist) {
				auto& item = static_cast<ColonistListItem&>(card);
				item.setColonistData(colonist);
				item.setSelected(colonist.id == selectedId);
			}
		);
	}

	// Runs before the card's own press selects it, so a double click follows, then selects.
	void ColonistListView::clicked(ecs::EntityID id, double time) {
		if (id == lastClickedId && time - lastClickTime < kDoubleClickSeconds) {
			if (onColonistFollowed) {
				onColonistFollowed(id);
			}
			// A third click starts over rather than following again.
			lastClickedId = ecs::EntityID{0};
			lastClickTime = 0.0;
			return;
		}
		lastClickedId = id;
		lastClickTime = time;
	}

} // namespace world_sim
