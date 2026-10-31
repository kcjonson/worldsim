#pragma once

// NeedBar - a colonist's need, 0 to 100, in the Salvage look: salal's ProgressBar on its auto tone, so
// a depleted need reads red and a satisfied one green, its label inside the bar. Compact is the
// shorter bar under a colonist's header.

#include <ui/components/ProgressBar.h>

#include <cstdint>
#include <string>

namespace world_sim {

	enum class NeedBarSize : std::uint8_t {
		Normal, // the needs panel's bar
		Compact // the header's mood bar
	};

	class NeedBar : public ui::ProgressBar {
	  public:
		struct Args {
			UI_COMMON_ARGS
			std::string label;
			float		percent = 100.0F;
			NeedBarSize barSize = NeedBarSize::Normal;
		};

		explicit NeedBar(const Args& args);

		// 0 to 100, clamped.
		void setPercent(float percent);

		[[nodiscard]] const char* typeName() const override { return "NeedBar"; }

	  protected:
		void onMount() override;

	  private:
		NeedBarSize barSize;
	};

} // namespace world_sim
