#pragma once

// Salvage design-system primitive: SegmentedControl.
//
// A compact pill of mutually-exclusive options: an inset well (bg_inset,
// hairline border, rounded) holding equal-width segments. The selected segment
// is a raised, glowing chip filled in the tone color with near-black label
// text; the others carry dim labels. The signature is the active chip's faux
// glow (a larger, more-transparent rect behind it in the tone's glow color).
//
// Static rendering: the segment at Args::selected is drawn active. No icons yet
// (the icon system is a later milestone), so segments render label text only.
// All visuals come from UI::DS tokens.
//
// Spec: docs/design/ui/design-system/components.md (SegmentedControl section).

#include "design-system/Variants.h"
#include "math/Types.h"
#include <string>
#include <vector>

namespace UI::DS {

	class SegmentedControl {
	  public:
		struct Args {
			Foundation::Vec2		 position{0.0F, 0.0F};
			float					 width = 0.0F;	// total group width; 0 -> size from segmentWidth or labels
			float					 segmentWidth = 0.0F; // per-segment width; 0 -> equal split of width (or auto-fit)
			std::vector<std::string> options;
			int						 selected = 0;
			Size					 size = Size::Md;
			Tone					 tone = Tone::Accent;
		};

		explicit SegmentedControl(Args args);

		// Draw the inset well, the segments, and the active chip with its glow.
		void render() const;

		// Total group footprint (well bounds), resolved from width/segmentWidth or
		// auto-fit to the measured labels.
		[[nodiscard]] Foundation::Vec2 footprint() const;

	  private:
		[[nodiscard]] float segmentInnerWidth() const;

		Args args;
	};

} // namespace UI::DS
