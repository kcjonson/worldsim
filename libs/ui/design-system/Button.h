#pragma once

// Salvage design-system primitive: Button.
//
// The interactive action element: a labeled, variant-tinted control. Draws a
// rounded background, a per-variant border, and a horizontally/vertically
// centered label. Five variants (primary, secondary, ghost, danger, data),
// three sizes (sm/md/lg) that drive height and font size.
//
// Static rendering only: the normal visual state, no hover/pressed/disabled.
// Hover glow, the press nudge, and icons (icon system not yet built) are later
// milestones. All visuals come from UI::DS tokens (see design-system/Tokens.h).
//
// Spec: docs/design/ui/design-system/components.md (Button section).

#include "design-system/Variants.h"
#include "math/Types.h"
#include <string>

namespace UI::DS {

	// Per-variant fill, border, and label treatment.
	enum class ButtonVariant { Primary, Secondary, Ghost, Danger, Data };

	class Button {
	  public:
		struct Args {
			Foundation::Vec2 position{0.0F, 0.0F};
			Foundation::Vec2 size{0.0F, 0.0F}; // {0,0} -> default footprint for sizeVariant
			std::string		 label;
			ButtonVariant	 variant = ButtonVariant::Secondary;
			// `sizeVariant`, not `size`: the Vec2 footprint above already owns `size`.
			Size sizeVariant = Size::Md;
		};

		explicit Button(Args args);

		// Draw background, border, and centered label.
		void render() const;

	  private:
		// Footprint actually used: args.size when non-zero, else a sensible
		// default (size-driven height, label-driven width).
		[[nodiscard]] Foundation::Vec2 footprint() const;

		Args args;
	};

} // namespace UI::DS
