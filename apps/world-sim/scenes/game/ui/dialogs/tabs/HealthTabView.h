#pragma once

#include <component/Container.h>
#include <graphics/Rect.h>

#include <array>
#include <string>

namespace world_sim {

/// Data for Health tab
struct HealthData {
	/// Need values (0-100) for all 8 needs, indexed by NeedType
	std::array<float, 8> needValues{};

	/// Whether each need is below seek threshold
	std::array<bool, 8> needsAttention{};

	/// Whether each need is critical
	std::array<bool, 8> isCritical{};

	float mood = 100.0F;
	std::string moodLabel;
};

/// Health tab content for ColonistDetailsDialog.
///
/// Mirrors the Salvage prototype's Health panel: a full-width Mood meter, then a
/// two-column body. Left column lists Vital Needs and (dimmed) Comfort needs as
/// meter rows; right column holds a Body & Ailments empty-state. Rendered with
/// explicit manual positioning relative to the view's content origin.
class HealthTabView : public UI::Container {
  public:
	/// Create the tab view with content bounds from parent dialog
	void create(const Foundation::Rect& contentBounds);

	/// Update content from model data
	void update(const HealthData& data);

	/// Draw the Health panel at the view's content origin.
	void render() override;

  private:
	Foundation::Rect contentBounds{};
	HealthData		 data_{};
};

} // namespace world_sim
