#pragma once

#include <component/Container.h>
#include <graphics/Rect.h>

#include <string>
#include <vector>

namespace world_sim {

/// A single known entity for Memory tab display
struct MemoryEntity {
	std::string name;     // e.g., "Berry Bush"
	float x = 0.0F;       // Position
	float y = 0.0F;
};

/// A category of known entities
struct MemoryCategory {
	std::string name;                    // e.g., "Food Sources"
	std::vector<MemoryEntity> entities;
	size_t count = 0;                    // Total count (may differ if truncated)
};

/// Data for Memory tab
struct MemoryData {
	std::vector<MemoryCategory> categories;
	size_t totalKnown = 0;
	float sightRadius = 30.0F;
};

/// Memory tab content for ColonistDetailsDialog.
///
/// Mirrors the Salvage prototype's Memory panel: a summary row (locations known +
/// sight range) over a 2-column grid of capability categories, each a name + count
/// badge with a few sample entity rows. Manual-render, like the other dossier tabs.
class MemoryTabView : public UI::Container {
  public:
	void create(const Foundation::Rect& contentBounds);
	void update(const MemoryData& data);
	void render() override;

  private:
	Foundation::Rect contentBounds{};
	MemoryData		 data_{};
};

} // namespace world_sim
