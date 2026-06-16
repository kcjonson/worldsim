#pragma once

// Salvage design-system primitive: Modal.
//
// A centered, scrimmed dialog built on Panel: the system's blocking overlay. A
// full-bleed scrim covers the viewport, with a glowing, bracketed Panel centered
// on top. Size sets the dialog width (sm/md/lg). Accent passes through to the
// underlying Panel.
//
// Static rendering only (renders the open state). All visuals come from UI::DS
// tokens. The prototype's Escape/scrim-click close behavior and pop-in animation
// are not modeled here.
//
// Spec: docs/design/ui/design-system/components.md (Modal section).

#include "design-system/Panel.h"
#include "design-system/Variants.h"
#include "math/Types.h"
#include <string>

namespace UI::DS {

	class Modal {
	  public:
		struct Args {
			Foundation::Vec2 viewport{0.0F, 0.0F}; // full screen size, for scrim + centering
			std::string		 title;
			std::string		 kicker;
			Size			 size = Size::Md;	   // sm/md/lg -> width 400/560/760
			float			 height = 0.0F;		   // dialog height; 0 -> sensible default
			PanelAccent		 accent = PanelAccent::Accent;
			std::string		 body; // optional placeholder body line; empty -> none
		};

		explicit Modal(Args args);

		// Fill the viewport with the scrim, then draw the centered dialog Panel.
		void render() const;

	  private:
		[[nodiscard]] float dialogWidth() const;
		[[nodiscard]] float dialogHeight() const;

		Args args;
	};

} // namespace UI::DS
