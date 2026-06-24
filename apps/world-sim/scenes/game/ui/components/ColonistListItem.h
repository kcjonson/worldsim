#pragma once

// ColonistListItem - a selectable colonist card in the roster (Salvage look).
//
// A mood-tinted Avatar, the colonist's first name with a mood percentage, and an
// activity meter (current task label + progress). The active card raises its
// background and shows an amber left edge. Drawn inline via the Primitives/Avatar/
// ProgressBar APIs.

#include "scenes/game/ui/adapters/ColonistAdapter.h"

#include <component/Component.h>
#include <components/progress/ProgressBar.h>
#include <ecs/EntityID.h>
#include <input/InputEvent.h>

#include <functional>
#include <string>

namespace world_sim {

/// A single colonist card for the roster.
class ColonistListItem : public UI::Component {
  public:
	using SelectCallback = std::function<void(ecs::EntityID)>;

	struct Args {
		adapters::ColonistData colonist;
		float width = 184.0F;
		float height = 46.0F;
		bool isSelected = false;
		float itemMargin = 0.0F;
		SelectCallback onSelect = nullptr;
		std::string id = "colonist_item";
	};

	explicit ColonistListItem(const Args& args);

	// IComponent overrides
	void render() override;
	bool handleEvent(UI::InputEvent& event) override;
	bool containsPoint(Foundation::Vec2 point) const override;

	// Data updates
	void setSelected(bool newSelected) { selected = newSelected; }
	void setMood(float newMood) { mood = newMood; }
	void setActivity(const std::string& label, float progress) {
		activity = label;
		activityProgress = progress;
	}
	void setColonistData(const adapters::ColonistData& data);

	// Accessors
	[[nodiscard]] ecs::EntityID getEntityId() const { return entityId; }
	[[nodiscard]] bool isSelected() const { return selected; }

  private:
	static std::string firstNameOf(const std::string& full);

	ecs::EntityID entityId;
	std::string name;
	std::string firstName;
	float mood = 100.0F;
	std::string activity;             // current task label, empty when idle
	float activityProgress = -1.0F;   // 0..1 while acting; <0 = traveling / idle
	bool selected = false;
	SelectCallback onSelect;
};

}  // namespace world_sim
