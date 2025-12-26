#pragma once

// ColonistListItem - A selectable item in the colonist list panel.
//
// Displays a single colonist with:
// - Portrait (mesh rendered with clipping)
// - Name text (centered)
// - Mood bar (horizontal bar with color gradient)
// - Selection highlight when selected
//
// Implements IComponent for use in LayoutContainer.

#include "scenes/game/ui/adapters/ColonistAdapter.h"

#include <component/Component.h>
#include <ecs/World.h>
#include <graphics/Color.h>
#include <layer/Layer.h>
#include <shapes/Shapes.h>

#include <functional>
#include <string>
#include <vector>

namespace world_sim {

/// A single colonist item for list display
class ColonistListItem : public UI::Component {
  public:
	using SelectCallback = std::function<void(ecs::EntityID)>;

	struct Args {
		adapters::ColonistData colonist;
		float width = 60.0F;
		float height = 50.0F;
		bool isSelected = false;
		float itemMargin = 0.0F;
		SelectCallback onSelect = nullptr;
		std::string id = "colonist_item";
	};

	explicit ColonistListItem(const Args& args);

	// IComponent overrides
	void render() override;
	void setPosition(float x, float y) override;
	bool handleEvent(UI::InputEvent& event) override;
	bool containsPoint(Foundation::Vec2 point) const override;

	// Data updates
	void setSelected(bool selected);
	void setMood(float mood);
	void setColonistData(const adapters::ColonistData& data);

	// Accessors
	[[nodiscard]] ecs::EntityID getEntityId() const { return entityId; }
	[[nodiscard]] bool isSelected() const { return selected; }

  private:
	void updateBackgroundStyle();
	void updateMoodBar();
	void renderPortrait();

	// Cached mesh data for portrait rendering
	struct CachedMeshData {
		float minX = 0.0F;
		float maxX = 0.0F;
		float minY = 0.0F;
		float maxY = 0.0F;
		float width = 0.0F;
		float height = 0.0F;
		float scale = 0.0F;
		bool valid = false;
	};

	// State
	ecs::EntityID entityId;
	std::string name;
	float mood = 100.0F;
	bool selected = false;
	SelectCallback onSelect;

	// Child handles
	UI::LayerHandle backgroundHandle;
	UI::LayerHandle nameTextHandle;
	UI::LayerHandle moodBarHandle;

	// Cached data for portrait rendering
	static CachedMeshData cachedMesh;
	std::vector<Foundation::Vec2> screenVerts;

	// Layout constants
	static constexpr float kPortraitSize = 32.0F;
	static constexpr float kPortraitMargin = 4.0F;
	static constexpr float kMoodBarHeight = 4.0F;
	static constexpr float kMoodBarOffset = 6.0F;
};

} // namespace world_sim
