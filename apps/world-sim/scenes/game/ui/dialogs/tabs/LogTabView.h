#pragma once

#include <component/Container.h>
#include <graphics/Rect.h>

namespace world_sim {

/// Data for Log tab (no activity-log system yet).
struct LogData {};

/// Log tab content for ColonistDetailsDialog.
///
/// There is no chronological activity log in the game yet, so this renders an
/// honest empty-state matching the dossier's style. Manual-render, like the
/// other dossier tabs.
class LogTabView : public UI::Container {
  public:
	void create(const Foundation::Rect& contentBounds);
	void update(const LogData& data);
	void render() override;

  private:
	Foundation::Rect contentBounds{};
};

} // namespace world_sim
