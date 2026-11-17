#pragma once

// What the splash and the loading scene show when a load fails: an inset panel titled "Loading failed" with the
// first kShownMessages messages in full, wrapped to its width, "and N more" when there are more, where the run's
// log file is, and what the player can do next. Display only.

#include <ui/Component.h>

#include <cstddef>
#include <string>
#include <vector>

namespace world_sim {

	class LoadFailureView : public ui::Component {
	  public:
		// As many as fit the smallest canvas under the splash's identity block at three lines each.
		static constexpr std::size_t kShownMessages = 5;

		struct Args {
			UI_COMMON_ARGS
			std::vector<std::string> messages;
			std::string				 instruction; // what the player can do next, under the messages
		};

		explicit LoadFailureView(const Args& args);

		[[nodiscard]] const char* typeName() const override { return "LoadFailureView"; }

	  protected:
		void onMount() override;

	  private:
		std::vector<std::string> messages;
		std::string				 instruction;
	};

} // namespace world_sim
