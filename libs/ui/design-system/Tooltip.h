#pragma once

// Salvage design-system primitive: Tooltip.
//
// A helper-text bubble: a raised rounded panel with a hairline-strong border
// and a small pointer triangle toward the anchored side. The prototype shows
// this on hover/focus; we have no hover system yet, so render() draws a STATIC
// bubble at the given position (the eventual TooltipManager owns delay, edge
// clamping, and trigger logic). This primitive is the visual contract for the
// bubble itself. All visuals come from UI::DS tokens.
//
// Spec: docs/design/ui/design-system/components.md (Tooltip section).

#include "math/Types.h"
#include <string>

namespace UI::DS {

	// Which edge the pointer triangle sits on (and, conceptually, which side of
	// the trigger the bubble would float toward).
	enum class TooltipSide { Top, Bottom, Left, Right };

	class Tooltip {
	  public:
		struct Args {
			Foundation::Vec2 position{0.0F, 0.0F}; // top-left of the bubble
			std::string		 content;
			TooltipSide		 side = TooltipSide::Top; // edge the pointer points from
		};

		explicit Tooltip(Args args);

		// Draw the bubble (sized to the content) and its pointer.
		void render() const;

	  private:
		Args args;
	};

} // namespace UI::DS
