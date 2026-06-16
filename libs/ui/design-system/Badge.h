#pragma once

// Salvage design-system primitive: Badge.
//
// A small inline status/label pill: a fixed-height rounded rect with a tinted
// fill and a tone-colored, uppercase-styled label, plus an optional leading
// glowing status dot. Width fits the label (measured) plus padding.
//
// Static rendering only. The dot's faux glow approximates the prototype's
// box-shadow with a larger, more-transparent circle behind it. All visuals
// come from UI::DS tokens.
//
// Spec: docs/design/ui/design-system/components.md (Badge section).

#include "design-system/Variants.h"
#include "math/Types.h"
#include <string>

namespace UI::DS {

	class Badge {
	  public:
		struct Args {
			Foundation::Vec2 position{0.0F, 0.0F};
			std::string		 label;
			Tone			 tone = Tone::Default;
			bool			 dot = false; // leading glowing status dot
		};

		explicit Badge(Args args);

		// Draw the pill background, optional dot, and label.
		void render() const;

	  private:
		Args args;
	};

} // namespace UI::DS
