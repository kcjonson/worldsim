#pragma once

#include <component/Container.h>
#include <graphics/Rect.h>
#include <layer/Layer.h>

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
		std::string				 currentTask;				   // e.g., "Eating", "Wandering"
	};

	/// Bio tab content for ColonistDetailsDialog
	/// Shows: name, age, mood, current task, traits, background
	class BioTabView : public UI::Container {
	  public:
		/// Create the tab view with content bounds from parent dialog
		void create(const Foundation::Rect& contentBounds);

		/// Update content from model data
		void update(const BioData& data);

	  private:
		UI::LayerHandle layoutHandle;
	};

} // namespace world_sim
