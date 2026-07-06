#pragma once

// WorldSurveyPanel - right-column survey readout for WorldCreatorScene.
//
// Renders into the caller-provided rect (the scene sizes it via contentHeight
// so the Landing Zone pane can stack beneath). Empty state ("survey data
// resolves...") until a world is set; then a Stat grid, a habitability pip
// row, climate-distribution meters grouped from the biome histogram, and
// water/habitability badges. Renders only stats whose WorldField bits the
// pipeline actually produced.

#include <worldgen/data/GeneratedWorld.h>

#include <graphics/Rect.h>

#include <memory>
#include <string>
#include <vector>

namespace world_sim {

class WorldSurveyPanel {
  public:
	// nullptr returns the panel to its empty (pending) state.
	void setWorld(std::shared_ptr<const worldgen::GeneratedWorld> world);

	void render(const Foundation::Rect& bounds) const;

	// Frame height the current content needs at the given width, so the caller
	// can size the panel to its content and stack another pane beneath it.
	float contentHeight(float width) const;

  private:
	struct StatEntry {
		std::string label;
		std::string value;
		std::string unit;
		bool		dataTone{false};
	};
	struct MeterEntry {
		std::string label;
		float		fraction{0.0F};
	};
	struct BadgeEntry {
		std::string label;
		int			tone{0}; // 0 ok, 1 warn, 2 crit
	};

	std::shared_ptr<const worldgen::GeneratedWorld> world;

	// Derived once in setWorld so render stays cheap.
	std::vector<StatEntry>	stats;
	std::vector<MeterEntry> climate;
	std::vector<BadgeEntry> badges;
	float					habitability{-1.0F}; // <0 = field absent
};

} // namespace world_sim
