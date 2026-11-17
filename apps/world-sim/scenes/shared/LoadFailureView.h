#pragma once

// What the splash and the loading scene show when a load fails: an inset panel titled "Loading failed" with the
// first kShownMessages messages in full, wrapped to its width and scrolling past kMessagesMaxHeight, "and N more"
// when there are more, where the run's log file is, and what the player can do next. Display only.

#include <ui/Component.h>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace world_sim {

	// Logs every message at error level under the scene's name, so the log the view points at holds them all.
	void logLoadFailure(std::string_view scene, const std::vector<std::string>& messages);

	class LoadFailureView : public ui::Component {
	  public:
		// The most messages listed; the rest are counted.
		static constexpr std::size_t kShownMessages = 5;
		// The messages' height in ui px before they scroll, which keeps the panel clear of the splash's button on the
		// smallest canvas at the largest interface size.
		static constexpr float kMessagesMaxHeight = 170.0F;

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
