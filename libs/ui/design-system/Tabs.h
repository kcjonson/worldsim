#pragma once

// Salvage design-system primitive: Tabs.
//
// An underline tab bar for switching content views: a horizontal row of mono,
// uppercase-styled labels over a hairline baseline. The selected tab's label is
// bright with a 2px accent underline sitting flush on the baseline; the others
// are dim. Tabs are laid left to right, each measured for width plus padding,
// separated by a small gap.
//
// Static rendering: the tab at Args::selected is drawn active. No icons yet, so
// tabs render label text only. All visuals come from UI::DS tokens.
//
// Spec: docs/design/ui/design-system/components.md (Tabs section).

#include "math/Types.h"
#include <string>
#include <vector>

namespace UI::DS {

	class Tabs {
	  public:
		struct Args {
			Foundation::Vec2		 position{0.0F, 0.0F};
			std::vector<std::string> tabs;
			int						 selected = 0;
			float					 gap = -1.0F; // inter-tab gap; <0 -> token default (space_1)
		};

		explicit Tabs(Args args);

		// Draw the baseline, the tab labels, and the active underline.
		void render() const;

		// Total bar footprint (sum of tab widths + gaps, 34px tall).
		[[nodiscard]] Foundation::Vec2 footprint() const;

	  private:
		Args args;
	};

} // namespace UI::DS
