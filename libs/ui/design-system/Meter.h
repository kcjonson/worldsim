#pragma once

// Salvage design-system primitive: Meter.
//
// A horizontal progress/level bar with an optional label and value readout.
// Draws an inset pill track, a tone-colored fill (with a faux glow), optional
// segment notches, and either a header row (label left, value right, above the
// track) or an inline overlay (label and value drawn inside the bar).
//
// Static rendering only: the fill is drawn at its final width, no animation.
// Real glow is a later shader milestone; here it is faked with a taller,
// more-transparent fill behind the bar. All visuals come from UI::DS tokens.
//
// Spec: docs/design/ui/design-system/components.md (Meter section).

#include "design-system/Variants.h"
#include "math/Types.h"
#include <string>

namespace UI::DS {

	class Meter {
	  public:
		struct Args {
			Foundation::Vec2 position{0.0F, 0.0F};
			float			 width = 200.0F;
			float			 value = 0.0F; // clamped to [0, 1]
			std::string		 label;
			std::string		 valueText;
			Tone			 tone = Tone::Accent;
			Size			 size = Size::Md;
			bool			 segmented = false;	  // overlay notch ticks on the fill
			bool			 inlineLabel = false; // draw label + value inside the bar
		};

		explicit Meter(Args args);

		// Draw header/overlay, track, fill, glow, and notches.
		void render() const;

	  private:
		Args args;
	};

} // namespace UI::DS
