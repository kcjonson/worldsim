#pragma once

// Shared variant vocabulary for the Salvage design-system primitives.
//
// Tone and Size are the cross-primitive knobs (a Meter, a Badge, and a Stat all
// share the same notion of "warn" or "small"). toneColor resolves a tone to a
// UI token; Auto picks by value using the prototype's thresholds.

#include "graphics/Color.h"
#include "theme/CssTokens.h"
#include "theme/Fonts.h"

namespace UI {

	enum class Tone { Accent, Data, Ok, Warn, Crit, Auto, Default };
	enum class Size { Sm, Md, Lg };

	// Resolve a tone to its token color. For Auto, value (0..1) picks the band:
	// <0.25 crit, <0.50 warn, else ok (matches Meter.tsx).
	inline Foundation::Color toneColor(Tone tone, float value = 1.0F) {
		switch (tone) {
			case Tone::Accent:
				return accent;
			case Tone::Data:
				return data;
			case Tone::Ok:
				return status_ok;
			case Tone::Warn:
				return status_warn;
			case Tone::Crit:
				return status_crit;
			case Tone::Auto:
				return value < 0.25F ? status_crit : (value < 0.5F ? status_warn : status_ok);
			case Tone::Default:
				break;
		}
		return text;
	}

	// Same color at a different alpha (for tinted fills, faux glow washes).
	inline Foundation::Color withAlpha(Foundation::Color color, float alpha) {
		return {color.r, color.g, color.b, alpha};
	}

} // namespace UI
