#pragma once

// BioTabView - the dossier's Bio tab: the background paragraph, a Traits divider over the traits, and
// what the colonist is doing now, in the accent.

#include <graphics/Color.h>
#include <ui/Component.h>
#include <ui/shapes/Text.h>

#include <string>
#include <vector>

namespace world_sim {

	struct BioData {
		std::string				 name;
		std::string				 age = "--"; // until there is an age system
		std::vector<std::string> traits;
		std::string				 background = "No background";
		float					 mood = 100.0F; // 0-100
		std::string				 moodLabel;		// "Happy", "Content", etc.
		std::string				 currentTask;	// "Going to food", "Re-routing"
		Foundation::Color		 currentTaskColor{0.80F, 0.80F, 0.85F, 1.0F};
	};

	class BioTabView : public ui::Component {
	  public:
		struct Args {
			UI_COMMON_ARGS
		};

		void update(const BioData& data);

		[[nodiscard]] const char* typeName() const override { return "BioTabView"; }

	  protected:
		void onMount() override;

	  private:
		ui::Ref<ui::Text> background;
		ui::Ref<ui::Text> traits;
		ui::Ref<ui::Text> currently;
	};

} // namespace world_sim
