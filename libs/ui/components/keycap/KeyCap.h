#pragma once

// Salvage design-system primitive: KeyCap.
//
// A small inline keyboard-key glyph for shortcut legends: a rounded rect with a
// bg_inset fill and a line_edge border, plus a thicker bottom edge that reads as
// a faint extruded key. The label is mono and dim, centered. Width fits the
// label (measured) plus horizontal padding, with a min-width floor so single
// characters don't collapse narrower than multi-character caps.
//
// Static. All visuals come from UI tokens.
//
// Spec: docs/design/ui/design-system/components.md (KeyCap section).

#include "math/Types.h"
#include <string>

namespace UI {

	class KeyCap {
	  public:
		struct Args {
			Foundation::Vec2 position{0.0F, 0.0F};
			std::string		 label;
		};

		explicit KeyCap(Args args);

		// Draw the key body, the beveled bottom edge, and the centered label.
		void render() const;

		// Total cap footprint (label width + padding, floored to min width; 18px tall).
		[[nodiscard]] Foundation::Vec2 footprint() const;

	  private:
		Args args;
	};

} // namespace UI
