#pragma once

#include <component/Container.h>
#include <graphics/Rect.h>

namespace world_sim {

/// Data for Skills tab (no skill system yet).
struct SkillsData {};

/// Skills tab content for ColonistDetailsDialog.
///
/// There is no skill-proficiency system in the game yet, so this renders an
/// honest empty-state matching the dossier's style. Manual-render, like the
/// other dossier tabs.
class SkillsTabView : public UI::Container {
  public:
	void create(const Foundation::Rect& contentBounds);
	void update(const SkillsData& data);
	void render() override;

  private:
	Foundation::Rect contentBounds{};
};

} // namespace world_sim
