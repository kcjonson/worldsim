#pragma once

// Salvage design-system primitive: Divider.
//
// A horizontal rule, optionally carrying a centered stencil label. Without a
// label it is a single hairline line across the width. With one, it is a
// three-part row: a hairline segment, the centered mono/uppercase/faint label,
// and another hairline segment, with the segment widths computed from the total
// width minus the measured label and its gaps.
//
// Static, non-interactive. All visuals come from UI tokens.
//
// Spec: docs/design/ui/design-system/components.md (Divider section).

#include "math/Types.h"
#include <string>

namespace UI {

	class Divider {
	  public:
		struct Args {
			Foundation::Vec2 position{0.0F, 0.0F};
			float			 width = 0.0F;
			std::string		 label; // empty -> a bare hairline rule
		};

		explicit Divider(Args args);

		// Draw the rule, or the labeled three-part row.
		void render() const;

	  private:
		Args args;
	};

} // namespace UI
