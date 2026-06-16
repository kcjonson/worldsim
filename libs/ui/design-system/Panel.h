#pragma once

// Salvage design-system primitive: Panel.
//
// The framing surface for everything: a titled, bracketed container. Draws a
// variant-tinted background, a hairline/edge border, optional L-bracket corner
// ticks, and an optional header (mono kicker eyebrow above a larger title).
//
// All visuals come from UI::DS tokens (see design-system/Tokens.h). Texture
// passes (scanlines, grain, glow) and real display fonts are later milestones.
//
// Spec: docs/design/ui/design-system/components.md (Panel section).

#include "design-system/Tokens.h"
#include "graphics/Rect.h"
#include "math/Types.h"
#include <string>

namespace UI::DS {

	// Surface tint and border treatment.
	enum class PanelVariant { Panel, Raised, Inset };

	// Color of the corner brackets and kicker text.
	enum class PanelAccent { Accent, Data, None };

	class Panel {
	  public:
		struct Args {
			Foundation::Vec2 position{0.0F, 0.0F};
			Foundation::Vec2 size{320.0F, 200.0F};
			std::string		 title;	 // empty -> no title line
			std::string		 kicker; // small mono eyebrow above the title; empty -> none
			PanelVariant	 variant = PanelVariant::Panel;
			PanelAccent		 accent = PanelAccent::Accent;
			bool			 corners = true;  // draw the four L-bracket ticks
			bool			 compact = false; // denser header/body padding for HUD panels
			bool			 flush = false;	  // remove body padding (edge-to-edge content)
		};

		explicit Panel(Args args);

		// Draw background, border, corner brackets, and header.
		void render() const;

		// Content area below the header, inset by body padding.
		[[nodiscard]] Foundation::Rect bodyBounds() const;

	  private:
		// Header occupies a band only when there is a title or kicker.
		[[nodiscard]] bool  hasHeader() const;
		[[nodiscard]] float headerHeight() const;

		Args args;
	};

} // namespace UI::DS
