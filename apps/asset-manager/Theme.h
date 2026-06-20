#pragma once

// Asset Manager theme - a local translation of the prototype design tokens
// (docs/ui-prototype/src/design-system/tokens.css): a "used-future ship
// terminal" - near-black hull, amber readouts, teal data, hairline borders,
// sharp corners. Scoped to this app; the shared libs/ui Theme.h still carries
// the older palette and would restyle the whole game if changed.
//
// Fonts (Chakra Petch / Barlow / JetBrains Mono) and letter-spacing aren't
// reproduced - the app only ships the Roboto SDF atlas - so hierarchy leans on
// color, size, surfaces, and uppercasing.

#include "graphics/Color.h"

namespace asset_manager::theme {

	using Foundation::Color;

	// Surfaces
	inline constexpr Color bgVoid{0.027F, 0.031F, 0.043F, 1.0F};	   // window
	inline constexpr Color bgPanel{0.071F, 0.082F, 0.110F, 1.0F};  // tree, top bar, cards
	inline constexpr Color bgRaised{0.094F, 0.110F, 0.141F, 1.0F}; // badges, hover targets
	inline constexpr Color bgInset{0.031F, 0.035F, 0.051F, 1.0F};  // preview well, code well

	// Lines
	inline constexpr Color lineHairline{0.706F, 0.784F, 0.902F, 0.10F};
	inline constexpr Color lineEdge{0.588F, 0.706F, 0.863F, 0.20F};

	// Accent (amber) + data (teal)
	inline constexpr Color accent{0.910F, 0.639F, 0.243F, 1.0F};
	inline constexpr Color accentBright{1.0F, 0.773F, 0.420F, 1.0F};
	inline constexpr Color accentFill{0.910F, 0.639F, 0.243F, 0.12F};	// selection bg / accent badge fill
	inline constexpr Color accentBorder{0.910F, 0.639F, 0.243F, 0.45F}; // accent badge border
	inline constexpr Color data{0.271F, 0.780F, 0.753F, 1.0F};
	inline constexpr Color dataBright{0.482F, 0.902F, 0.875F, 1.0F};
	inline constexpr Color dataFill{0.271F, 0.780F, 0.753F, 0.12F};
	inline constexpr Color dataBorder{0.271F, 0.780F, 0.753F, 0.45F};

	// Text
	inline constexpr Color textBright{0.957F, 0.933F, 0.886F, 1.0F};
	inline constexpr Color text{0.776F, 0.784F, 0.808F, 1.0F};
	inline constexpr Color textDim{0.541F, 0.561F, 0.608F, 1.0F};
	inline constexpr Color textFaint{0.353F, 0.373F, 0.420F, 1.0F};

	// Status
	inline constexpr Color statusWarn{0.910F, 0.639F, 0.243F, 1.0F};
	inline constexpr Color statusCrit{0.878F, 0.325F, 0.235F, 1.0F};

	// Interaction
	inline constexpr Color rowHover{1.0F, 1.0F, 1.0F, 0.045F};

	// Spacing (4px scale)
	inline constexpr float s1 = 4.0F;
	inline constexpr float s2 = 8.0F;
	inline constexpr float s3 = 12.0F;
	inline constexpr float s4 = 16.0F;
	inline constexpr float s5 = 20.0F;
	inline constexpr float s6 = 24.0F;
	inline constexpr float s8 = 32.0F;

	// Type scale
	inline constexpr float fsLabel = 11.0F;
	inline constexpr float fsSmall = 12.0F;
	inline constexpr float fsBody = 13.0F;
	inline constexpr float fsMd = 15.0F;
	inline constexpr float fsTitle = 22.0F;

	// Shape
	inline constexpr float radius = 2.0F;
	inline constexpr float borderWidth = 1.0F;

} // namespace asset_manager::theme
