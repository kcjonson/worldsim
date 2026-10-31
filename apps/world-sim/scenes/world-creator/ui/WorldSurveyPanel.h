#pragma once

// WorldSurveyPanel - right-column survey readout for WorldCreatorScene.
//
// A panel built into the host the scene gives it, rebuilt on each setWorld. Empty state ("survey
// data resolves...") until a world is set, the panel then filling the host; with one, sized to its
// content: a Stat grid, a habitability pip row, climate-distribution meters grouped from the biome
// histogram, and water/habitability badges. Shows only stats whose WorldField bits the pipeline
// actually produced.

#include <worldgen/data/GeneratedWorld.h>

#include <ui/Component.h>
#include <ui/theme/Theme.h>

#include <memory>
#include <string>
#include <vector>

namespace world_sim {

class WorldSurveyPanel {
  public:
	// `host` is a flex column; the panel is its only child.
	void mount(ui::Ref<ui::Component> host, const ui::Theme& theme);

	// nullptr returns the panel to its empty (pending) state.
	void setWorld(std::shared_ptr<const worldgen::GeneratedWorld> world);

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

	void rebuild();

	ui::Ref<ui::Component> host;
	ui::Ref<ui::Component> panel;
	const ui::Theme*	   theme = nullptr;

	std::shared_ptr<const worldgen::GeneratedWorld> world;

	std::vector<StatEntry>	stats;
	std::vector<MeterEntry> climate;
	std::vector<BadgeEntry> badges;
	float					habitability{-1.0F}; // <0 = field absent
};

} // namespace world_sim
