#pragma once

#include <component/Container.h>
#include <graphics/Color.h>
#include <graphics/Rect.h>

#include <string>
#include <vector>

namespace world_sim {

	/// Data for Bio tab
	struct BioData {
		std::string				 name;
		std::string				 age = "--";				   // Placeholder until age system
		std::vector<std::string> traits;					   // Empty for now
		std::string				 background = "No background"; // Placeholder
		float					 mood = 100.0F;				   // 0-100
		std::string				 moodLabel;					   // "Happy", "Content", etc.
		std::string				 currentTask;				   // e.g., "Going to food", "Re-routing"
		Foundation::Color		 currentTaskColor{0.80F, 0.80F, 0.85F, 1.0F}; // default matches bodyColor()
	};

	/// Bio tab content for ColonistDetailsDialog.
	///
	/// Mirrors the Salvage prototype's Bio panel: an Avatar + compact 2x2 Stat
	/// grid header, then a Background paragraph, a Traits row, and the
	/// current-task note. Rendered with explicit manual positioning relative to
	/// the view's content origin - no nested layout, no adapter wrappers.
	class BioTabView : public UI::Container {
	  public:
		/// Create the tab view with content bounds from parent dialog
		void create(const Foundation::Rect& contentBounds);

		/// Update content from model data
		void update(const BioData& data);

		/// Draw the Bio panel at the view's content origin.
		void render() override;

	  private:
		Foundation::Rect contentBounds{};
		BioData			 data_{};
	};

} // namespace world_sim
