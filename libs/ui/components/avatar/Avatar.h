#pragma once

// Salvage design-system primitive: Avatar.
//
// A deterministic colonist portrait: a generated silhouette plus initials,
// framed like a dossier photo, with an optional mood ring. The seed (the
// colonist's name) drives both the color and the initials via an FNV-1a hash, so
// the same name always yields the same avatar with no stored state.
//
// Static rendering only. The frame, ring color, and glow come from UI tokens
// and per-instance HSL math; the silhouette SVG is approximated by a filled disc
// behind centered initials.
//
// Spec: docs/design/ui/design-system/components.md (Avatar section).

#include "math/Types.h"
#include <string>

namespace UI {

	class Avatar {
	  public:
		struct Args {
			Foundation::Vec2 position{0.0F, 0.0F};
			float			 size = 44.0F;
			std::string		 seed;			// hashed -> deterministic hue + initials
			float			 mood = 1.0F;	// 0..1 -> ring tint + glow
			bool			 hasMood = true; // false -> neutral ring, no glow
			bool			 selected = false;
		};

		explicit Avatar(Args args);

		// Draw the framed background, initials, corner tick, mood ring, and (if
		// selected) the accent outline.
		void render() const;

	  private:
		Args args;
	};

} // namespace UI
