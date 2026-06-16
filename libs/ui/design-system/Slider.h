#pragma once

// Salvage design-system primitive: Slider.
//
// A labeled range control: an optional header row (label left, value right)
// over an inset pill track, a tone-accented fill with a faux glow, a square
// thumb at the value position, and an optional reference detent tick.
//
// Render-only. The value is supplied already normalized to [0, 1]; the caller
// maps from its own min/max range. There is no drag/hover interaction yet, so
// render() draws the resting visual state. Real glow is a later shader pass;
// here it is faked with translucent washes behind the fill and thumb. All
// visuals come from UI::DS tokens.
//
// Spec: docs/design/ui/design-system/components.md (Slider section).

#include "math/Types.h"
#include <string>

namespace UI::DS {

	class Slider {
	  public:
		struct Args {
			Foundation::Vec2 position{0.0F, 0.0F};
			float			 width = 200.0F;
			float			 value = 0.5F; // already normalized to [0, 1] for rendering
			std::string		 label;		  // optional, drawn above the track
			std::string		 valueText;	  // optional, drawn right of the label
			float			 detent = -1.0F; // [0, 1] reference marker; < 0 = none
		};

		explicit Slider(Args args);

		// Draw the optional header, track, fill, glow, thumb, and detent.
		void render() const;

	  private:
		Args args;
	};

} // namespace UI::DS
