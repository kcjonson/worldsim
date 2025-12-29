#pragma once

#include <component/Component.h>
#include <graphics/Rect.h>
#include <layer/Layer.h>

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

/// Health tab content for ColonistDetailsDialog
/// Two-column layout:
/// - Left: Mood + Need bars + Mood modifiers
/// - Right: Body parts & ailments
class HealthTabView : public UI::Component {
  public:
	/// Create the tab view with content bounds from parent dialog
	void create(const Foundation::Rect& contentBounds);

	/// Update content from model data
	void update(const HealthData& data);

  private:
	UI::LayerHandle layoutHandle;
};

} // namespace world_sim
